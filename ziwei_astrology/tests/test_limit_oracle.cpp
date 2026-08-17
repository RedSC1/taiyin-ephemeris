#include "taiyin/ziwei/anchors.h"
#include "taiyin/ziwei/chart.h"
#include "taiyin/ziwei/limits.h"
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

bool parse_record(
    const std::string& line,
    std::size_t expected,
    std::vector<int32_t>* out
) {
    if (out == NULL) return false;
    std::vector<int32_t> values;
    std::istringstream input(line);
    std::string field;
    while (std::getline(input, field, ',')) {
        char* end = NULL;
        const long value = std::strtol(field.c_str(), &end, 10);
        if (end == field.c_str() || *end != '\0') return false;
        values.push_back(static_cast<int32_t>(value));
    }
    if (values.size() != expected) return false;
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
        pair_at(values, offset), pair_at(values, offset + 2u),
        pair_at(values, offset + 4u), pair_at(values, offset + 6u),
    };
}

bool next_data_line(std::ifstream* input, std::string* out) {
    if (input == NULL || out == NULL) return false;
    while (std::getline(*input, *out)) {
        if (!out->empty() && (*out)[0] != '#') return true;
    }
    return false;
}

}  // namespace

int main() {
    using namespace taiyin;
    using namespace taiyin::ziwei;
    const LoadedRules loaded = load_rules_from_toml(
        std::string(TAIYIN_ZIWEI_TEST_ROOT) + "/rules/default.toml");
    std::ifstream natal_input((std::string(TAIYIN_ZIWEI_TEST_ROOT)
        + "/tests/data/ziwei_core_0_13_0_natal.csv").c_str());
    std::ifstream limit_input((std::string(TAIYIN_ZIWEI_TEST_ROOT)
        + "/tests/data/ziwei_core_0_13_0_limits.csv").c_str());
    if (!natal_input || !limit_input) {
        std::cerr << "cannot open limit oracle resources\n";
        return EXIT_FAILURE;
    }

    std::string natal_line;
    std::string limit_line;
    std::size_t record_index = 0u;
    while (next_data_line(&natal_input, &natal_line)
        && next_data_line(&limit_input, &limit_line)) {
        std::vector<int32_t> natal_values;
        std::vector<int32_t> expected;
        if (!parse_record(natal_line, 142u, &natal_values)
            || !parse_record(limit_line, 25u, &expected)
            || expected[0] != static_cast<int32_t>(record_index)) {
            std::cerr << "malformed paired limit oracle record\n";
            return EXIT_FAILURE;
        }

        CalendarFacts facts = {};
        facts.birth.gender = static_cast<Gender>(natal_values[0]);
        facts.lunar_date.year = natal_values[1];
        facts.lunar_date.month = static_cast<uint8_t>(natal_values[2]);
        facts.lunar_date.day = static_cast<uint8_t>(natal_values[3]);
        facts.lunar_date.is_leap = static_cast<uint8_t>(natal_values[4]);
        facts.effective_lunar_year = natal_values[5];
        facts.effective_lunar_month = static_cast<uint8_t>(natal_values[6]);
        facts.solar_day_from_previous_jie =
            static_cast<uint16_t>(natal_values[7]);
        facts.solar_term_pillars = pillars_at(natal_values, 8u);
        facts.lunar_pillars = pillars_at(natal_values, 16u);
        Anchors anchors;
        Branch body = Branch::Zi;
        NatalChart natal;
        if (compute_anchors(
                facts, default_anchor_options(), &anchors, &body)
                != TAIYIN_STATUS_OK
            || make_natal_chart(
                facts,
                anchors,
                body,
                default_natal_rule_options(),
                loaded.compiled,
                &natal) != TAIYIN_STATUS_OK) {
            std::cerr << "cannot build limit oracle natal chart "
                      << record_index << '\n';
            return EXIT_FAILURE;
        }

        DecadeLimit decade;
        SmallLimit small;
        FlowYearLimit year;
        FlowMonthLimit month;
        FlowDayLimit day;
        FlowHourLimit hour;
        const int32_t birth_year = expected[1];
        const int32_t target_year = expected[2];
        const int32_t virtual_age = expected[8];
        if (make_decade_for_year(
                natal,
                birth_year,
                target_year,
                ChildhoodStrategy::Skip,
                &decade) != TAIYIN_STATUS_OK
            || make_small_limit(
                natal,
                facts.solar_term_pillars.year.branch,
                virtual_age,
                &small) != TAIYIN_STATUS_OK
            || make_flow_year(natal, target_year, &year) != TAIYIN_STATUS_OK
            || make_flow_month(
                natal,
                target_year,
                static_cast<uint8_t>(expected[13]),
                static_cast<uint8_t>(expected[14]),
                expected[15] != 0,
                facts.effective_lunar_month,
                facts.solar_term_pillars.hour.branch,
                &month) != TAIYIN_STATUS_OK
            || make_flow_day(
                natal,
                month,
                static_cast<uint8_t>(expected[18]),
                static_cast<Stem>(expected[19]),
                &day) != TAIYIN_STATUS_OK
            || make_flow_hour(
                natal, day, static_cast<uint8_t>(expected[22]), &hour)
                != TAIYIN_STATUS_OK) {
            std::cerr << "limit calculation failed in record "
                      << record_index << '\n';
            return EXIT_FAILURE;
        }

        const bool matches =
            decade.index == expected[3]
            && decade.start_age == expected[4]
            && decade.end_age == expected[5]
            && to_index(decade.limit.coordinate.stem) == expected[6]
            && to_index(decade.limit.coordinate.branch) == expected[7]
            && small.virtual_age == expected[8]
            && to_index(small.coordinate.stem) == expected[9]
            && to_index(small.coordinate.branch) == expected[10]
            && to_index(year.limit.coordinate.stem) == expected[11]
            && to_index(year.limit.coordinate.branch) == expected[12]
            && month.month == expected[13]
            && month.sequence == expected[14]
            && static_cast<int>(month.is_leap) == expected[15]
            && to_index(month.limit.coordinate.stem) == expected[16]
            && to_index(month.limit.coordinate.branch) == expected[17]
            && day.day == expected[18]
            && to_index(day.limit.coordinate.stem) == expected[20]
            && to_index(day.limit.coordinate.branch) == expected[21]
            && hour.hour_index == expected[22]
            && to_index(hour.limit.coordinate.stem) == expected[23]
            && to_index(hour.limit.coordinate.branch) == expected[24];
        if (!matches) {
            std::cerr << "limit mismatch in Dart oracle record "
                      << record_index << '\n';
            return EXIT_FAILURE;
        }
        ++record_index;
    }
    if (record_index != 23u) {
        std::cerr << "unexpected limit oracle record count\n";
        return EXIT_FAILURE;
    }
    std::cout << "23-record Dart flow-limit oracle passed\n";
    return EXIT_SUCCESS;
}
