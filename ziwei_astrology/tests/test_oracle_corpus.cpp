#include "taiyin/ziwei/anchors.h"
#include "taiyin/ziwei/chart.h"
#include "taiyin/ziwei/rules_loader.h"

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#ifndef TAIYIN_ZIWEI_TEST_ROOT
#define TAIYIN_ZIWEI_TEST_ROOT "."
#endif

namespace {

const std::size_t kRecordFieldCount = 142u;
const std::size_t kPositionOffset = 27u;
const std::size_t kNatalStarCount = 115u;

bool parse_record(const std::string& line, std::vector<int32_t>* out) {
    if (out == NULL) return false;
    std::vector<int32_t> values;
    std::istringstream input(line);
    std::string field;
    while (std::getline(input, field, ',')) {
        if (field.empty()) return false;
        char* end = NULL;
        const long value = std::strtol(field.c_str(), &end, 10);
        if (end == field.c_str() || *end != '\0') return false;
        values.push_back(static_cast<int32_t>(value));
    }
    if (values.size() != kRecordFieldCount) return false;
    *out = values;
    return true;
}

taiyin::ziwei::Ganzhi pair_at(
    const std::vector<int32_t>& values,
    std::size_t offset
) {
    return taiyin::ziwei::Ganzhi{
        static_cast<taiyin::ziwei::Stem>(values[offset]),
        static_cast<taiyin::ziwei::Branch>(values[offset + 1u]),
    };
}

taiyin::ziwei::Pillars pillars_at(
    const std::vector<int32_t>& values,
    std::size_t offset
) {
    return taiyin::ziwei::Pillars{
        pair_at(values, offset),
        pair_at(values, offset + 2u),
        pair_at(values, offset + 4u),
        pair_at(values, offset + 6u),
    };
}

}  // namespace

int main() {
    using namespace taiyin;
    using namespace taiyin::ziwei;
    const LoadedRules loaded = load_rules_from_toml(
        std::string(TAIYIN_ZIWEI_TEST_ROOT) + "/rules/default.toml");
    std::ifstream input((std::string(TAIYIN_ZIWEI_TEST_ROOT)
        + "/tests/data/ziwei_core_0_13_0_natal.csv").c_str());
    if (!input) {
        std::cerr << "cannot open Dart oracle corpus\n";
        return EXIT_FAILURE;
    }

    std::string line;
    std::size_t record_index = 0u;
    while (std::getline(input, line)) {
        if (line.empty() || line[0] == '#') continue;
        std::vector<int32_t> values;
        if (!parse_record(line, &values)) {
            std::cerr << "malformed oracle record " << record_index << '\n';
            return EXIT_FAILURE;
        }

        CalendarFacts facts = {};
        facts.birth.gender = static_cast<Gender>(values[0]);
        facts.lunar_date.year = values[1];
        facts.lunar_date.month = static_cast<uint8_t>(values[2]);
        facts.lunar_date.day = static_cast<uint8_t>(values[3]);
        facts.lunar_date.is_leap = static_cast<uint8_t>(values[4]);
        facts.effective_lunar_year = values[5];
        facts.effective_lunar_month = static_cast<uint8_t>(values[6]);
        facts.solar_day_from_previous_jie = static_cast<uint16_t>(values[7]);
        facts.solar_term_pillars = pillars_at(values, 8u);
        facts.lunar_pillars = pillars_at(values, 16u);

        Anchors anchors;
        Branch body = Branch::Zi;
        if (compute_anchors(
                facts, default_anchor_options(), &anchors, &body)
                != TAIYIN_STATUS_OK
            || to_index(anchors.palace_positions[to_index(PalaceId::Life)])
                != values[24]
            || to_index(body) != values[25]
            || to_index(anchors.bureau) != values[26]) {
            std::cerr << "anchor mismatch in oracle record "
                      << record_index << '\n';
            return EXIT_FAILURE;
        }

        NatalChart chart;
        if (make_natal_chart(
                facts,
                anchors,
                body,
                default_natal_rule_options(),
                loaded.compiled,
                &chart) != TAIYIN_STATUS_OK) {
            std::cerr << "chart failure in oracle record "
                      << record_index << '\n';
            return EXIT_FAILURE;
        }
        std::vector<uint8_t> positions;
        if (dump_natal_star_positions(chart, &positions)
                != TAIYIN_STATUS_OK
            || positions.size() != loaded.registry.size()) {
            std::cerr << "dump failure in oracle record "
                      << record_index << '\n';
            return EXIT_FAILURE;
        }
        for (std::size_t star = 0u; star < kNatalStarCount; ++star) {
            if (positions[star] == values[kPositionOffset + star]) continue;
            std::cerr << "star mismatch in oracle record " << record_index
                      << ", StarId " << star << '\n';
            return EXIT_FAILURE;
        }
        for (std::size_t star = kNatalStarCount;
             star < positions.size(); ++star) {
            if (positions[star] == 0xffu) continue;
            std::cerr << "flow star leaked into natal oracle record "
                      << record_index << '\n';
            return EXIT_FAILURE;
        }
        ++record_index;
    }
    if (record_index != 23u) {
        std::cerr << "unexpected oracle record count " << record_index << '\n';
        return EXIT_FAILURE;
    }
    std::cout << "23-record / 2645-star Dart oracle corpus passed\n";
    return EXIT_SUCCESS;
}
