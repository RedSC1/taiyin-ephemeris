#include "taiyin/ziwei/anchors.h"
#include "taiyin/ziwei/chart.h"
#include "taiyin/ziwei/rules_loader.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

#ifndef TAIYIN_ZIWEI_TEST_ROOT
#define TAIYIN_ZIWEI_TEST_ROOT "."
#endif

namespace {

taiyin::ziwei::Ganzhi ganzhi(uint8_t stem, uint8_t branch) {
    return taiyin::ziwei::Ganzhi{
        static_cast<taiyin::ziwei::Stem>(stem),
        static_cast<taiyin::ziwei::Branch>(branch),
    };
}

}  // namespace

int main() {
    using namespace taiyin;
    using namespace taiyin::ziwei;

    // Oracle: the author's MIT-licensed Dart ziwei_core 0.13.0, evaluated for
    // 0181-08-20 08:00 male with its default rules. This historical case is
    // also retained by ziwei_core as the Zhuge Liang demonstration.
    CalendarFacts facts = {};
    facts.birth.gender = Gender::Male;
    facts.lunar_date.year = 181;
    facts.lunar_date.month = 7u;
    facts.lunar_date.day = 23u;
    facts.effective_lunar_year = 181;
    facts.effective_lunar_month = 7u;
    facts.solar_day_from_previous_jie = 1u;
    const Pillars oracle_pillars = {
        ganzhi(7u, 9u), ganzhi(2u, 8u),
        ganzhi(9u, 1u), ganzhi(2u, 4u),
    };
    facts.solar_term_pillars = oracle_pillars;
    facts.lunar_pillars = oracle_pillars;

    Anchors anchors;
    Branch body = Branch::Zi;
    if (compute_anchors(facts, default_anchor_options(), &anchors, &body)
        != TAIYIN_STATUS_OK) {
        std::cerr << "failed to compute oracle anchors\n";
        return 1;
    }
    if (anchors.palace_positions[to_index(PalaceId::Life)] != Branch::Chen
        || body != Branch::Zi
        || anchors.bureau != Bureau::Water2
        || anchors.ziwei != Branch::Zi
        || anchors.tianfu != Branch::Chen) {
        std::cerr << "31-anchor result differs from Dart oracle\n";
        return 1;
    }

    const LoadedRules loaded = load_rules_from_toml(
        std::string(TAIYIN_ZIWEI_TEST_ROOT) + "/rules/default.toml");
    NatalChart chart;
    if (make_natal_chart(
            facts,
            anchors,
            body,
            default_natal_rule_options(),
            loaded.compiled,
            &chart)
        != TAIYIN_STATUS_OK) {
        std::cerr << "failed to build oracle chart\n";
        return 1;
    }
    StarId lianzhen = kInvalidStarId;
    StarId tiantong = kInvalidStarId;
    loaded.registry.find("lianzhen", &lianzhen);
    loaded.registry.find("tiantong", &tiantong);
    if (chart.life_master != lianzhen || chart.body_master != tiantong) {
        std::cerr << "life/body master differs from Dart oracle\n";
        return 1;
    }
    if (chart.palace_stems[to_index(Branch::Zi)] != Stem::Geng
        || chart.palace_stems[to_index(Branch::Chen)] != Stem::Ren) {
        std::cerr << "palace stems differ from Dart oracle\n";
        return 1;
    }
    std::vector<uint8_t> actual;
    if (dump_natal_star_positions(chart, &actual) != TAIYIN_STATUS_OK) {
        std::cerr << "failed to dump oracle chart\n";
        return 1;
    }

    const std::array<uint8_t, 115u> expected = {{
        0, 11, 9, 8, 7, 4, 4, 5, 6, 7, 8, 9, 10, 2, 10, 4, 6, 8,
        2, 6, 9, 11, 10, 8, 7, 2, 7, 3, 6, 0, 7, 3, 6, 8, 6, 3, 5,
        10, 6, 1, 9, 1, 1, 11, 7, 1, 0, 5, 4, 10, 9, 11, 9, 3, 9, 5,
        2, 2, 11, 5, 5, 6, 2, 1, 6, 2, 2, 9, 8, 7, 6, 5, 4, 3, 2, 1,
        0, 11, 10, 9, 10, 11, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11,
        0, 1, 2, 3, 4, 5, 6, 7, 8, 8, 7, 6, 5, 4, 3, 2, 1, 0, 11,
        10, 9,
    }};
    if (actual.size() != loaded.registry.size()
        || actual.size() != expected.size() + 44u) {
        std::cerr << "oracle star count differs: " << actual.size() << '\n';
        return 1;
    }
    for (std::size_t star = 0u; star < expected.size(); ++star) {
        if (actual[star] == expected[star]) continue;
        std::cerr << "oracle mismatch at StarId " << star
                  << ": expected " << static_cast<int>(expected[star])
                  << ", actual " << static_cast<int>(actual[star]) << '\n';
        return 1;
    }
    for (std::size_t star = expected.size(); star < actual.size(); ++star) {
        if (actual[star] == 0xffu) continue;
        std::cerr << "flow-only StarId unexpectedly appears in natal chart: "
                  << star << '\n';
        return 1;
    }

    std::cout << "115-star Dart oracle differential passed\n";
    return 0;
}
