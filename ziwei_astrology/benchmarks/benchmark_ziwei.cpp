#include "taiyin/ziwei/ziweicore.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>

#ifndef TAIYIN_ZIWEI_RULES_FILE
#define TAIYIN_ZIWEI_RULES_FILE "rules/default.toml"
#endif

namespace {

taiyin::ziwei::Ganzhi ganzhi(uint8_t stem, uint8_t branch) {
    return taiyin::ziwei::Ganzhi{
        static_cast<taiyin::ziwei::Stem>(stem),
        static_cast<taiyin::ziwei::Branch>(branch),
    };
}

taiyin::ziwei::CalendarFacts representative_facts() {
    using namespace taiyin::ziwei;
    CalendarFacts facts = {};
    facts.birth.gender = Gender::Female;
    facts.lunar_date.year = 2003;
    facts.lunar_date.month = 2u;
    facts.lunar_date.day = 11u;
    facts.effective_lunar_year = 2003;
    facts.effective_lunar_month = 2u;
    facts.solar_day_from_previous_jie = 8u;
    facts.solar_term_pillars = Pillars{
        ganzhi(9u, 7u), ganzhi(1u, 3u),
        ganzhi(1u, 9u), ganzhi(9u, 7u),
    };
    facts.lunar_pillars = facts.solar_term_pillars;
    return facts;
}

}  // namespace

int main(int argc, char** argv) {
    using namespace taiyin;
    using namespace taiyin::ziwei;
    std::size_t iterations = 10000u;
    if (argc == 2) {
        char* end = NULL;
        const unsigned long parsed = std::strtoul(argv[1], &end, 10);
        if (end == argv[1] || *end != '\0' || parsed == 0u) {
            std::cerr << "usage: benchmark_ziwei [positive-iterations]\n";
            return EXIT_FAILURE;
        }
        iterations = static_cast<std::size_t>(parsed);
    }

    ZiweiDataCatalog data_catalog(TAIYIN_ZIWEI_RULES_FILE);
    const ZiweiContext ziwei_context = data_catalog.create_context();
    const CompiledRules& tables = ziwei_context.compiled_tables();
    const CalendarFacts facts = representative_facts();
    const AnchorOptions options = default_anchor_options();
    volatile uint64_t digest = 0u;

    const std::chrono::steady_clock::time_point begin =
        std::chrono::steady_clock::now();
    for (std::size_t i = 0u; i < iterations; ++i) {
        Anchors anchors;
        Branch body = Branch::Zi;
        NatalChart natal;
        if (compute_anchors(facts, options, &anchors, &body)
                != TAIYIN_STATUS_OK
            || make_natal_chart(
                facts,
                anchors,
                body,
                options.rules,
                tables,
                &natal) != TAIYIN_STATUS_OK) {
            std::cerr << "benchmark calculation failed\n";
            return EXIT_FAILURE;
        }
        digest ^= static_cast<uint64_t>(to_index(anchors.ziwei))
            + natal.palaces[i % kBranchCount].stars.count();
    }
    const std::chrono::steady_clock::time_point end =
        std::chrono::steady_clock::now();
    const double elapsed_us =
        std::chrono::duration_cast<std::chrono::duration<double, std::micro> >(
            end - begin).count();
    std::cout << "iterations=" << iterations
              << " us_per_natal=" << elapsed_us / iterations
              << " charts_per_second=" << iterations * 1000000.0 / elapsed_us
              << " digest=" << digest << '\n';
    return EXIT_SUCCESS;
}
