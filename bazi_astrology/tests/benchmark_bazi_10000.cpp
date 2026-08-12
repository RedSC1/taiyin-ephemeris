#include "taiyin/bazi/bazi.h"

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <vector>

namespace {

constexpr std::size_t kChartCount = 10000u;
constexpr uint64_t kFnvOffsetBasis = UINT64_C(14695981039346656037);
constexpr uint64_t kFnvPrime = UINT64_C(1099511628211);

struct Input {
    taiyin::chinese_calendar::GanzhiFourPillars pillars;
};

bool make_pillar(uint32_t index, uint8_t* out) {
    return taiyin::chinese_calendar::make_ganzhi(
        static_cast<uint8_t>(index % 10u),
        static_cast<uint8_t>(index % 12u), out) == taiyin::TAIYIN_STATUS_OK;
}

void mix_value(uint64_t value, uint64_t* digest) {
    for (unsigned int byte = 0u; byte < 8u; ++byte) {
        *digest ^= value & UINT64_C(0xff);
        *digest *= kFnvPrime;
        value >>= 8u;
    }
}

uint64_t run(const std::vector<Input>& inputs) {
    taiyin::bazi::BaziContext context;
    const taiyin::bazi::BaziContextConfig config = taiyin::bazi::default_context_config();
    if (taiyin::bazi::initialize_context(&context, &config) != taiyin::TAIYIN_STATUS_OK) {
        std::abort();
    }
    // This digest prevents the benchmark workload from being optimized away.
    // Behavioral equivalence is checked by compare_qiyun_records.py and the
    // exhaustive unit tests, not by treating a benchmark digest as an oracle.
    uint64_t workload_digest = kFnvOffsetBasis;
    for (std::size_t i = 0; i < inputs.size(); ++i) {
        taiyin::bazi::BaziChart chart;
        if (taiyin::bazi::calculate_chart(&context, inputs[i].pillars, &chart)
            != taiyin::TAIYIN_STATUS_OK) std::abort();

        taiyin::bazi::BaziRelation relations[512];
        std::size_t relation_count = 0u;
        if (taiyin::bazi::collect_chart_relations(
                &chart,
                taiyin::bazi::BaziRelationPillarAll,
                taiyin::bazi::kBaziRelationKindMaskAll,
                relations,
                512u,
                &relation_count) != taiyin::TAIYIN_STATUS_OK) std::abort();

        uint64_t words[taiyin::bazi::kBaziShenShaWordCount] = {};
        std::size_t word_count = 0u;
        for (int32_t target_kind = taiyin::bazi::BaziShenShaTargetYear;
             target_kind <= taiyin::bazi::BaziShenShaTargetHour;
             ++target_kind) {
            const uint8_t target = target_kind == taiyin::bazi::BaziShenShaTargetYear
                ? chart.pillars.year
                : target_kind == taiyin::bazi::BaziShenShaTargetMonth
                    ? chart.pillars.month
                    : target_kind == taiyin::bazi::BaziShenShaTargetDay
                        ? chart.pillars.day : chart.pillars.hour;
            if (taiyin::bazi::collect_target_shen_sha_with_gender(
                    &chart, target, target_kind, taiyin::bazi::BaziGenderMale,
                    words, taiyin::bazi::kBaziShenShaWordCount, &word_count)
                != taiyin::TAIYIN_STATUS_OK) std::abort();
            mix_value(static_cast<uint64_t>(target_kind), &workload_digest);
            mix_value(static_cast<uint64_t>(word_count), &workload_digest);
            for (std::size_t word = 0; word < word_count; ++word) {
                mix_value(words[word], &workload_digest);
            }
        }
        mix_value(chart.pillars.year, &workload_digest);
        mix_value(chart.pillars.month, &workload_digest);
        mix_value(chart.pillars.day, &workload_digest);
        mix_value(chart.pillars.hour, &workload_digest);
        for (std::size_t pillar = 0; pillar < 4u; ++pillar) {
            mix_value(chart.nayin_ids[pillar], &workload_digest);
        }
        mix_value(chart.extra.ming_gong, &workload_digest);
        mix_value(chart.extra.shen_gong, &workload_digest);
        mix_value(chart.extra.tai_yuan, &workload_digest);
        mix_value(chart.extra.tai_xi, &workload_digest);
        mix_value(static_cast<uint64_t>(relation_count), &workload_digest);
        for (std::size_t relation = 0; relation < relation_count; ++relation) {
            mix_value(static_cast<uint64_t>(relations[relation].kind), &workload_digest);
            mix_value(relations[relation].pillar_mask, &workload_digest);
            mix_value(relations[relation].combined_element_id, &workload_digest);
        }
    }
    return workload_digest;
}

}  // namespace

int main() {
    std::vector<Input> inputs(kChartCount);
    for (std::size_t i = 0; i < inputs.size(); ++i) {
        Input& input = inputs[i];
        if (!make_pillar(static_cast<uint32_t>((i * 7u + 1u) % 60u), &input.pillars.year)
            || !make_pillar(static_cast<uint32_t>((i * 11u + 13u) % 60u), &input.pillars.month)
            || !make_pillar(static_cast<uint32_t>((i * 13u + 29u) % 60u), &input.pillars.day)
            || !make_pillar(static_cast<uint32_t>((i * 17u + 41u) % 60u), &input.pillars.hour)) {
            return 2;
        }
    }

    const std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
    const uint64_t workload_digest = run(inputs);
    const std::chrono::steady_clock::time_point end = std::chrono::steady_clock::now();
    const double milliseconds = std::chrono::duration<double, std::milli>(end - start).count();
    std::cout << "charts=" << kChartCount
              << " elapsed_ms=" << std::fixed << std::setprecision(3) << milliseconds
              << " charts_per_second=" << std::setprecision(1)
              << (static_cast<double>(kChartCount) * 1000.0 / milliseconds)
              << " workload_digest=" << workload_digest << "\n";
    return 0;
}
