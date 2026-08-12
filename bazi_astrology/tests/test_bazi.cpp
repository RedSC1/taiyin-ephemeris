#include "taiyin/bazi/bazi.h"
#include "taiyin/body_id.h"
#include "taiyin/runtime/ephemeris_route.h"
#include "taiyin/runtime/native_context.h"
#include "taiyin/runtime/runtime.h"
#include "taiyin/time.h"

#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

namespace {

int failures = 0;

void expect(bool condition, const char* label) {
    if (!condition) {
        std::cerr << "FAIL: " << label << "\n";
        ++failures;
    }
}

void expect_status(taiyin::Status status, const char* label) {
    if (status != taiyin::TAIYIN_STATUS_OK) {
        std::cerr << "FAIL: " << label << ": "
                  << taiyin::status_name(status) << "\n";
        ++failures;
    }
}

void expect_byte(uint8_t actual, uint8_t expected, const char* label) {
    if (actual != expected) {
        std::cerr << "FAIL: " << label << ": actual=0x" << std::hex
                  << static_cast<unsigned int>(actual) << " expected=0x"
                  << static_cast<unsigned int>(expected) << std::dec << "\n";
        ++failures;
    }
}

bool triple_matches(uint8_t a, uint8_t b, uint8_t c, const uint8_t group[3]) {
    if (a == b || a == c || b == c) return false;
    const uint8_t values[3] = {a, b, c};
    for (size_t i = 0; i < 3; ++i) {
        if (values[i] != group[0] && values[i] != group[1] && values[i] != group[2]) {
            return false;
        }
    }
    return true;
}

void test_sexagenary_encoding() {
    expect(taiyin::bazi::BaziWuXingWater == 0, "WuXing ID inherits bazi_core water");
    expect(taiyin::bazi::BaziWuXingWood == 1, "WuXing ID inherits bazi_core wood");
    expect(taiyin::bazi::BaziWuXingMetal == 2, "WuXing ID inherits bazi_core metal");
    expect(taiyin::bazi::BaziWuXingEarth == 3, "WuXing ID inherits bazi_core earth");
    expect(taiyin::bazi::BaziWuXingFire == 4, "WuXing ID inherits bazi_core fire");

    uint8_t wu_wu = 0;
    expect_status(
        taiyin::chinese_calendar::make_ganzhi(4, 6, &wu_wu),
        "make Wu-Wu Ganzhi");
    expect(wu_wu == 0x46u, "Ganzhi encoding is stem high nibble and branch low nibble");

    uint8_t ji_wei = 0;
    expect_status(
        taiyin::chinese_calendar::advance_ganzhi(wu_wu, 1, &ji_wei),
        "advance Ganzhi");
    expect(ji_wei == 0x57u, "Wu-Wu advances to Ji-Wei");

    expect(
        taiyin::chinese_calendar::make_ganzhi(0, 1, &wu_wu) == taiyin::TAIYIN_ERROR_INVALID_ARGUMENT,
        "reject incompatible Ganzhi parity");

    for (uint8_t stem = 0; stem < 10u; ++stem) {
        for (uint8_t month_index = 0; month_index < 12u; ++month_index) {
            uint8_t value = taiyin::chinese_calendar::kInvalidGanzhi;
            expect_status(
                taiyin::chinese_calendar::get_month_ganzhi(stem, month_index, &value),
                "transcribe sxwnl_spa_dart monthGanZhi");
            const uint8_t expected_stem = static_cast<uint8_t>(
                (((stem % 5u) * 2u + 2u) + month_index) % 10u);
            const uint8_t expected_branch = static_cast<uint8_t>((month_index + 2u) % 12u);
            expect(value == static_cast<uint8_t>((expected_stem << 4) | expected_branch),
                "all Wu-Hu-Dun month pillars match sxwnl_spa_dart");
        }
        for (uint8_t hour_index = 0; hour_index < 12u; ++hour_index) {
            uint8_t value = taiyin::chinese_calendar::kInvalidGanzhi;
            expect_status(
                taiyin::chinese_calendar::get_hour_ganzhi(stem, hour_index, &value),
                "transcribe sxwnl_spa_dart hourGanZhi");
            const uint8_t expected_stem = static_cast<uint8_t>(
                ((stem % 5u) * 2u + hour_index) % 10u);
            expect(value == static_cast<uint8_t>((expected_stem << 4) | hour_index),
                "all Wu-Shu-Dun hour pillars match sxwnl_spa_dart");
        }
    }
    expect(
        taiyin::chinese_calendar::get_month_ganzhi(0, 12, &wu_wu)
            == taiyin::TAIYIN_ERROR_INVALID_ARGUMENT,
        "reject invalid traditional month index");
    expect(
        taiyin::chinese_calendar::get_hour_ganzhi(0, 12, &wu_wu)
            == taiyin::TAIYIN_ERROR_INVALID_ARGUMENT,
        "reject invalid traditional hour index");

    struct KongWangOracle {
        uint8_t ganzhi;
        uint8_t first;
        uint8_t second;
    };
    // Direct cases from bazi_core/test/bazi_core_test.dart.
    const KongWangOracle kong_wang_oracles[] = {
        {0x00u, 10u, 11u},
        {0x0au, 8u, 9u},
        {0x08u, 6u, 7u},
        {0x11u, 11u, 10u},
    };
    for (const KongWangOracle& oracle : kong_wang_oracles) {
        uint8_t branches[2] = {0xffu, 0xffu};
        expect_status(
            taiyin::bazi::get_kong_wang(oracle.ganzhi, branches),
            "transcribe GanZhi.getKongWang");
        expect(branches[0] == oracle.first && branches[1] == oracle.second,
            "Kong Wang ordering matches bazi_core");
    }

    for (uint8_t day_stem = 0; day_stem < 10u; ++day_stem) {
        for (uint8_t target_stem = 0; target_stem < 10u; ++target_stem) {
            uint8_t ten_god = taiyin::chinese_calendar::kInvalidGanzhi;
            expect_status(
                taiyin::bazi::get_ten_god(day_stem, target_stem, &ten_god),
                "transcribe Relationship.getShiShen");
            const uint8_t expected = static_cast<uint8_t>(
                (((static_cast<int>(target_stem >> 1)
                    - static_cast<int>(day_stem >> 1) + 5) % 5) << 1)
                | ((day_stem ^ target_stem) & 1u));
            expect(ten_god == expected, "all ten-god pairs match bazi_core");
        }
    }

    static const uint8_t expected_hidden_count[12] = {
        1, 3, 3, 1, 3, 3, 2, 3, 3, 1, 3, 2,
    };
    static const uint8_t expected_hidden_stems[12][3] = {
        {9, 0xffu, 0xffu}, {5, 9, 7}, {0, 2, 4}, {1, 0xffu, 0xffu},
        {4, 1, 9}, {2, 6, 4}, {3, 5, 0xffu}, {5, 3, 1},
        {6, 8, 4}, {7, 0xffu, 0xffu}, {4, 7, 3}, {8, 0, 0xffu},
    };
    for (uint8_t branch = 0; branch < 12u; ++branch) {
        uint8_t stems[3] = {0, 0, 0};
        uint8_t count = 0;
        expect_status(
            taiyin::bazi::get_hidden_stems(branch, stems, &count),
            "transcribe BaziTable.getCangGan");
        expect(count == expected_hidden_count[branch],
            "hidden-stem count matches bazi_core table");
        for (size_t i = 0; i < 3; ++i) {
            expect(stems[i] == expected_hidden_stems[branch][i],
                "hidden-stem order matches bazi_core table");
        }
    }

    uint32_t relation_flags = 0;
    uint8_t combined_element = taiyin::bazi::kInvalidWuXing;
    expect_status(
        taiyin::bazi::calculate_stem_relation(
            0, 5, &relation_flags, &combined_element),
        "calculate Jia-Ji stem relation");
    expect((relation_flags & taiyin::bazi::BaziStemRelationCombination) != 0u,
        "Jia-Ji has stem combination");
    expect(combined_element == taiyin::bazi::BaziWuXingEarth,
        "Jia-Ji transforms to earth");

    expect_status(
        taiyin::bazi::calculate_branch_relation(
            0, 1, &relation_flags, &combined_element),
        "calculate Zi-Chou branch relation");
    expect((relation_flags & taiyin::bazi::BaziBranchRelationCombination) != 0u,
        "Zi-Chou has branch combination");
    expect(combined_element == taiyin::bazi::BaziWuXingEarth,
        "Zi-Chou transforms to earth");

    expect_status(
        taiyin::bazi::calculate_branch_relation(
            4, 4, &relation_flags, &combined_element),
        "calculate Chen-Chen branch relation");
    expect((relation_flags & taiyin::bazi::BaziBranchRelationSelfPunishment) != 0u,
        "Chen-Chen has self punishment");

    expect_status(
        taiyin::bazi::calculate_branch_triple_relation(
            8, 0, 4, &relation_flags, &combined_element),
        "calculate Shen-Zi-Chen triple relation");
    expect((relation_flags & taiyin::bazi::BaziBranchTripleRelationCombination) != 0u,
        "Shen-Zi-Chen forms water triple combination");
    expect(combined_element == taiyin::bazi::BaziWuXingWater,
        "Shen-Zi-Chen transforms to water");
    expect_status(
        taiyin::bazi::calculate_branch_triple_relation(
            2, 5, 8, &relation_flags, &combined_element),
        "calculate Yin-Si-Shen triple relation");
    expect((relation_flags & taiyin::bazi::BaziBranchTripleRelationPunishment) != 0u,
        "Yin-Si-Shen forms triple punishment");

    static const int8_t stem_combo[10] = {5, 6, 7, 8, 9, 0, 1, 2, 3, 4};
    static const int8_t stem_clash[10] = {6, 7, 8, 9, -1, -1, 0, 1, 2, 3};
    static const uint8_t stem_combo_element[10] = {3, 2, 0, 1, 4, 3, 2, 0, 1, 4};
    for (uint8_t a = 0; a < 10u; ++a) {
        for (uint8_t b = 0; b < 10u; ++b) {
            uint32_t flags = 0;
            uint8_t element = taiyin::bazi::kInvalidWuXing;
            expect_status(
                taiyin::bazi::calculate_stem_relation(a, b, &flags, &element),
                "transcribe all BaziTable stem relations");
            uint32_t expected_flags = 0;
            uint8_t expected_element = taiyin::bazi::kInvalidWuXing;
            if (stem_combo[a] == b) {
                expected_flags |= taiyin::bazi::BaziStemRelationCombination;
                expected_element = stem_combo_element[a];
            }
            if (stem_clash[a] == b) {
                expected_flags |= taiyin::bazi::BaziStemRelationClash;
            }
            if (((a + 4u) % 10u) == b || ((b + 4u) % 10u) == a) {
                expected_flags |= taiyin::bazi::BaziStemRelationRestraint;
            }
            expect(flags == expected_flags, "all stem relation flags match bazi_core");
            expect(element == expected_element, "all stem combination elements match bazi_core");
        }
    }

    static const int8_t branch_combo[12] = {1, 0, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2};
    static const int8_t branch_clash[12] = {6, 7, 8, 9, 10, 11, 0, 1, 2, 3, 4, 5};
    static const int8_t branch_harm[12] = {7, 6, 5, 4, 3, 2, 1, 0, 11, 10, 9, 8};
    static const int8_t branch_destruction[12] = {9, 4, 11, 6, 1, 8, 3, 10, 5, 0, 7, 2};
    static const int8_t branch_hidden[12] = {5, 2, 1, 8, -1, 0, 11, -1, 3, -1, -1, 6};
    static const int8_t branch_severance[12] = {5, -1, 9, 8, -1, 0, 11, -1, 3, 2, -1, 6};
    static const uint8_t branch_combo_element[12] = {3, 3, 1, 4, 2, 0, 3, 3, 0, 2, 4, 1};
    const auto is_pair = [](uint8_t a, uint8_t b, uint8_t left, uint8_t right) {
        return (a == left && b == right) || (a == right && b == left);
    };
    for (uint8_t a = 0; a < 12u; ++a) {
        for (uint8_t b = 0; b < 12u; ++b) {
            uint32_t flags = 0;
            uint8_t element = taiyin::bazi::kInvalidWuXing;
            expect_status(
                taiyin::bazi::calculate_branch_relation(a, b, &flags, &element),
                "transcribe all BaziTable branch relations");
            uint32_t expected_flags = 0;
            uint8_t expected_element = taiyin::bazi::kInvalidWuXing;
            if (branch_combo[a] == b) {
                expected_flags |= taiyin::bazi::BaziBranchRelationCombination;
                expected_element = branch_combo_element[a];
            }
            if (branch_clash[a] == b) expected_flags |= taiyin::bazi::BaziBranchRelationClash;
            if (branch_harm[a] == b) expected_flags |= taiyin::bazi::BaziBranchRelationHarm;
            if (branch_destruction[a] == b) expected_flags |= taiyin::bazi::BaziBranchRelationDestruction;
            const bool punishment = a != b && (
                is_pair(a, b, 0, 3) || is_pair(a, b, 2, 5)
                || is_pair(a, b, 2, 8) || is_pair(a, b, 5, 8)
                || is_pair(a, b, 1, 10) || is_pair(a, b, 1, 7)
                || is_pair(a, b, 7, 10));
            if (punishment) expected_flags |= taiyin::bazi::BaziBranchRelationPunishment;
            if (a == b && (a == 4u || a == 6u || a == 9u || a == 11u)) {
                expected_flags |= taiyin::bazi::BaziBranchRelationSelfPunishment;
            }
            if (branch_hidden[a] == b) {
                expected_flags |= taiyin::bazi::BaziBranchRelationHiddenCombination;
            }
            if (branch_severance[a] == b) {
                expected_flags |= taiyin::bazi::BaziBranchRelationSeverance;
            }
            expect(flags == expected_flags, "all branch relation flags match bazi_core");
            expect(element == expected_element, "all branch combination elements match bazi_core");
        }
    }

    static const uint8_t triple_combination[4][3] = {
        {8, 0, 4}, {11, 3, 7}, {2, 6, 10}, {5, 9, 1},
    };
    static const uint8_t triple_direction[4][3] = {
        {11, 0, 1}, {2, 3, 4}, {5, 6, 7}, {8, 9, 10},
    };
    static const uint8_t triple_punishment[2][3] = {{2, 5, 8}, {1, 10, 7}};
    static const uint8_t triple_element[4] = {0, 1, 4, 2};
    for (uint8_t a = 0; a < 12u; ++a) {
        for (uint8_t b = 0; b < 12u; ++b) {
            for (uint8_t c = 0; c < 12u; ++c) {
                uint32_t flags = 0;
                uint8_t element = taiyin::bazi::kInvalidWuXing;
                expect_status(
                    taiyin::bazi::calculate_branch_triple_relation(
                        a, b, c, &flags, &element),
                    "transcribe all BaziTable triple relations");
                uint32_t expected_flags = 0;
                uint8_t expected_element = taiyin::bazi::kInvalidWuXing;
                for (size_t group = 0; group < 4; ++group) {
                    if (triple_matches(a, b, c, triple_combination[group])) {
                        expected_flags |= taiyin::bazi::BaziBranchTripleRelationCombination;
                        expected_element = triple_element[group];
                    }
                    if (triple_matches(a, b, c, triple_direction[group])) {
                        expected_flags |= taiyin::bazi::BaziBranchTripleRelationDirection;
                        expected_element = triple_element[group];
                    }
                }
                for (size_t group = 0; group < 2; ++group) {
                    if (triple_matches(a, b, c, triple_punishment[group])) {
                        expected_flags |= taiyin::bazi::BaziBranchTripleRelationPunishment;
                    }
                }
                expect(flags == expected_flags, "all triple relation flags match bazi_core");
                expect(element == expected_element, "all triple relation elements match bazi_core");
            }
        }
    }

    uint8_t life_stage = taiyin::chinese_calendar::kInvalidGanzhi;
    expect_status(
        taiyin::bazi::get_life_stage(
            4, 2, taiyin::bazi::BaziEarthPalaceFireEarth, &life_stage),
        "calculate Wu-Yin fire-earth life stage");
    expect(life_stage == 0u, "Wu reaches Chang Sheng at Yin in fire-earth mode");
    expect_status(
        taiyin::bazi::get_life_stage(
            4, 8, taiyin::bazi::BaziEarthPalaceWaterEarth, &life_stage),
        "calculate Wu-Shen water-earth life stage");
    expect(life_stage == 0u, "Wu reaches Chang Sheng at Shen in water-earth mode");

    static const uint8_t fire_earth_starts[10] = {11, 6, 2, 9, 2, 9, 5, 0, 8, 3};
    for (int32_t mode = taiyin::bazi::BaziEarthPalaceFireEarth;
         mode <= taiyin::bazi::BaziEarthPalaceWaterEarth;
         ++mode) {
        for (uint8_t stem = 0; stem < 10u; ++stem) {
            uint8_t start = fire_earth_starts[stem];
            if (mode == taiyin::bazi::BaziEarthPalaceWaterEarth && stem == 4u) start = 8u;
            if (mode == taiyin::bazi::BaziEarthPalaceWaterEarth && stem == 5u) start = 3u;
            for (uint8_t branch = 0; branch < 12u; ++branch) {
                uint8_t stage = taiyin::chinese_calendar::kInvalidGanzhi;
                expect_status(
                    taiyin::bazi::get_life_stage(stem, branch, mode, &stage),
                    "transcribe BaziTable.getLifeStage");
                const uint8_t expected = (stem & 1u) == 0u
                    ? static_cast<uint8_t>((branch + 12u - start) % 12u)
                    : static_cast<uint8_t>((start + 12u - branch) % 12u);
                expect(stage == expected, "all life stages match bazi_core");
            }
        }
    }
}

void test_chart_from_four_pillars() {
    taiyin::bazi::BaziContext context;
    const taiyin::bazi::BaziContextConfig config =
        taiyin::bazi::default_context_config();
    expect_status(
        taiyin::bazi::initialize_context(&context, &config),
        "initialize BaZi interpretation context");

    taiyin::chinese_calendar::GanzhiFourPillars pillars;
    pillars.year = 0x26u;
    pillars.month = 0x62u;
    pillars.day = 0x42u;
    pillars.hour = 0x35u;

    taiyin::bazi::BaziChart chart;
    expect_status(
        taiyin::bazi::calculate_chart(&context, pillars, &chart),
        "interpret a precomputed four-pillar chart");
    expect(chart.pillars.year == pillars.year && chart.pillars.month == pillars.month
            && chart.pillars.day == pillars.day && chart.pillars.hour == pillars.hour,
        "BaZi preserves the calendar-owned four pillars");
    expect(chart.extra.ming_gong == 0x4au, "Ming Gong is Wu-Xu");
    expect(chart.extra.shen_gong == 0x28u, "Shen Gong is Bing-Shen");
    expect(chart.extra.tai_yuan == 0x75u, "Tai Yuan is Xin-Si");
    expect(chart.extra.tai_xi == 0x9bu, "Tai Xi is Gui-Hai");
    expect(chart.hidden_stem_count[2] == 3u, "Yin has three hidden stems");
    expect(chart.hidden_stems[2][0] == 0u, "Yin principal hidden stem is Jia");
    expect(chart.visible_ten_gods[2] == 0u, "day master is Bi Jian");
    expect(chart.life_stages[0] == 4u, "Wu reaches Di Wang at Wu");
    expect(chart.life_stages[1] == 0u, "Wu reaches Chang Sheng at Yin");
    expect(chart.nayin_ids[0] == 21u, "Bing-Wu NaYin ID");
    expect(chart.nayin_ids[1] == 13u, "Geng-Yin NaYin ID");
    expect(chart.nayin_ids[2] == 7u, "Wu-Yin NaYin ID");
    expect(chart.nayin_ids[3] == 26u, "Ding-Si NaYin ID");

    struct TaiXiOracle {
        uint8_t day;
        uint8_t tai_xi;
        const char* label;
    };
    // The day anchors are asserted by test_ganzhi_calendar; this locks the
    // separate BaZi-only Tai Xi transform against the same Dart oracle.
    const TaiXiOracle tai_xi_oracles[] = {
        {0x00u, 0x51u, "Jia-Zi Tai Xi is Ji-Chou"},
        {0x22u, 0x7bu, "Bing-Yin Tai Xi is Xin-Hai"},
    };
    for (const TaiXiOracle& oracle : tai_xi_oracles) {
        pillars.day = oracle.day;
        expect_status(taiyin::bazi::calculate_chart(&context, pillars, &chart), oracle.label);
        expect(chart.extra.tai_xi == oracle.tai_xi, oracle.label);
    }

    pillars.day = 0xffu;
    expect(taiyin::bazi::calculate_chart(&context, pillars, &chart)
            == taiyin::TAIYIN_ERROR_INVALID_ARGUMENT,
        "BaZi rejects invalid precomputed pillars");
}

void test_flow_primitives() {
    using namespace taiyin::bazi;

    uint8_t value = taiyin::chinese_calendar::kInvalidGanzhi;
    expect_status(calculate_flow_year(1984, &value), "calculate 1984 flow year");
    expect(value == 0x00u, "1984 is Jia-Zi");
    expect_status(calculate_flow_year(2024, &value), "calculate 2024 flow year");
    expect(value == 0x04u, "2024 is Jia-Chen");
    expect_status(calculate_flow_year(4, &value), "calculate year zero boundary");
    expect(value == 0x00u, "year 4 is Jia-Zi");

    expect_status(calculate_flow_month(0x04u, 2u, &value),
        "calculate Jia-Chen flow Yin month");
    expect(value == 0x22u, "Jia-year Yin month is Bing-Yin");
    expect_status(calculate_flow_month(0x04u, 0u, &value),
        "calculate Jia-Chen flow Zi month");
    expect(value == 0x20u, "Jia-year Zi month is Bing-Zi");
    expect(calculate_flow_month(0x05u, 2u, &value)
            == taiyin::TAIYIN_ERROR_INVALID_ARGUMENT,
        "flow month rejects an incompatible year pillar");

    const taiyin::CalendarDateTime new_year_day = {2000, 1, 1, 23, 59, 0.0};
    expect_status(calculate_flow_day(new_year_day, &value),
        "calculate flow day from civil date");
    expect(value == 0x46u, "2000-01-01 is Wu-Wu day");
    uint8_t direct_day = taiyin::chinese_calendar::kInvalidGanzhi;
    expect_status(taiyin::chinese_calendar::calculate_day_pillar(
        new_year_day, &direct_day), "calculate shared Ganzhi day pillar");
    expect(direct_day == value, "flow day reuses the shared Ganzhi day helper");

    expect_status(calculate_flow_hour(value, 0u, &value),
        "calculate flow Zi hour");
    expect(value == 0x80u, "Wu-Wu day Zi hour is Ren-Zi");
    expect_status(calculate_flow_hour(0x46u, 11u, &value),
        "calculate flow Hai hour");
    expect(value == 0x9bu, "Wu-Wu day Hai hour is Gui-Hai");

    BaziChart chart;
    chart.pillars.hour = 0x35u;
    uint8_t expected = taiyin::chinese_calendar::kInvalidGanzhi;
    expect_status(taiyin::chinese_calendar::advance_ganzhi(
        chart.pillars.hour, 1, &expected), "advance xiao-yun oracle");
    expect_status(calculate_xiaoyun(&chart, 1, 1, &value),
        "calculate first forward xiao-yun");
    expect(value == expected, "xiao-yun starts from the natal hour pillar");
    expect_status(calculate_xiaoyun(&chart, -1, 7, &value),
        "calculate reverse xiao-yun");
    expect_status(taiyin::chinese_calendar::advance_ganzhi(
        chart.pillars.hour, -7, &expected), "advance reverse xiao-yun oracle");
    expect(value == expected, "reverse xiao-yun follows the selected direction");
    expect(calculate_xiaoyun(&chart, 0, 1, &value)
            == taiyin::TAIYIN_ERROR_INVALID_ARGUMENT,
        "xiao-yun rejects a zero direction");
    expect(calculate_xiaoyun(&chart, 1, 0, &value)
            == taiyin::TAIYIN_ERROR_INVALID_ARGUMENT,
        "xiao-yun rejects zero virtual age");

    BaziXiaoYun entries[3];
    size_t entry_count = 0u;
    expect_status(fill_xiaoyun(
        &chart, 1, 1, 3, entries, 3, &entry_count),
        "fill a pre-qi-yun xiao-yun range");
    expect(entry_count == 3u && entries[0].age == 1u
            && entries[1].age == 2u && entries[2].age == 3u,
        "xiao-yun batch preserves the requested virtual ages");
    expect(entries[0].ganzhi != taiyin::chinese_calendar::kInvalidGanzhi
            && entries[2].ganzhi != taiyin::chinese_calendar::kInvalidGanzhi,
        "xiao-yun batch returns valid Ganzhi values");
    entry_count = 0u;
    expect_status(fill_xiaoyun(
        &chart, -1, 1, 3, nullptr, 0, &entry_count),
        "count pre-qi-yun xiao-yun entries");
    expect(entry_count == 3u, "xiao-yun count-only call reports its range");
}

void test_dayun_cycle() {
    taiyin::bazi::BaziContext context;
    const taiyin::bazi::BaziContextConfig config =
        taiyin::bazi::default_context_config();
    expect_status(
        taiyin::bazi::initialize_context(&context, &config),
        "initialize da-yun rule context");

    const taiyin::CalendarDateTime civil = {2030, 10, 12, 13, 27, 49.0};
    taiyin::bazi::BaziChart chart;
    chart.pillars.month = 0x62u;
    taiyin::bazi::BaziQiYunResult qiyun;
    qiyun.time_model = taiyin::bazi::BaziQiYunTraditionalCalendar;
    qiyun.start_civil_time = civil;
    expect(taiyin::julian_day_split(civil, &qiyun.start_jd_ut),
        "encode da-yun rule start time");

    for (int32_t direction = -1; direction <= 1; direction += 2) {
        qiyun.direction = direction;
        taiyin::bazi::BaziDaYun entries[60];
        size_t count = 0;
        expect_status(taiyin::bazi::fill_dayun(
            &context,
            civil,
            &chart,
            &qiyun,
            60,
            entries,
            60,
            &count), "fill complete da-yun cycle");
        expect(count == 60u, "da-yun cycle returns all requested entries");
        for (size_t i = 0; i < count; ++i) {
            uint8_t expected_ganzhi = taiyin::chinese_calendar::kInvalidGanzhi;
            expect_status(taiyin::chinese_calendar::advance_ganzhi(
                chart.pillars.month,
                direction * static_cast<int32_t>(i + 1u),
                &expected_ganzhi), "advance da-yun oracle Ganzhi");
            expect(entries[i].ganzhi == expected_ganzhi,
                "da-yun rule matches the 60-cycle advance oracle");
        }
    }
}

void test_renyuan_siling_tables() {
    using namespace taiyin::bazi;
    struct ExpectedEntry {
        uint8_t stem;
        uint8_t duration;
        uint8_t origin;
    };
    const ExpectedEntry expected[2][12][3] = {
        {
            {{8, 7, 0}, {9, 23, 0}, {0xffu, 0, 0}},
            {{9, 7, 0}, {6, 5, 0}, {5, 18, 0}},
            {{4, 5, 1}, {2, 5, 0}, {0, 20, 0}},
            {{0, 7, 0}, {1, 23, 0}, {0xffu, 0, 0}},
            {{1, 7, 0}, {8, 5, 0}, {4, 18, 0}},
            {{4, 7, 0}, {6, 5, 0}, {2, 18, 0}},
            {{2, 7, 0}, {3, 23, 0}, {0xffu, 0, 0}},
            {{3, 7, 0}, {0, 5, 0}, {5, 18, 0}},
            {{4, 5, 2}, {8, 5, 0}, {6, 20, 0}},
            {{6, 7, 0}, {7, 23, 0}, {0xffu, 0, 0}},
            {{7, 7, 0}, {2, 5, 0}, {4, 18, 0}},
            {{4, 5, 0}, {0, 5, 0}, {8, 20, 0}},
        },
        {
            {{8, 10, 0}, {9, 20, 0}, {0xffu, 0, 0}},
            {{9, 9, 0}, {7, 3, 0}, {5, 18, 0}},
            {{4, 7, 0}, {2, 7, 0}, {0, 16, 0}},
            {{0, 10, 0}, {1, 20, 0}, {0xffu, 0, 0}},
            {{1, 9, 0}, {9, 3, 0}, {4, 18, 0}},
            {{4, 5, 0}, {6, 9, 0}, {2, 16, 0}},
            {{2, 10, 0}, {5, 9, 0}, {3, 11, 0}},
            {{3, 9, 0}, {1, 3, 0}, {5, 18, 0}},
            {{4, 10, 0}, {8, 3, 0}, {6, 17, 0}},
            {{6, 10, 0}, {7, 20, 0}, {0xffu, 0, 0}},
            {{7, 9, 0}, {3, 3, 0}, {4, 18, 0}},
            {{4, 7, 0}, {0, 5, 0}, {8, 18, 0}},
        },
    };

    for (int32_t model = BaziRenyuanSilingSanMingTongHui;
         model <= BaziRenyuanSilingCommon; ++model) {
        for (uint8_t branch = 0u; branch < 12u; ++branch) {
            BaziRenyuanSilingSegment segments[kRenyuanSilingMaxSegments];
            size_t count = 0u;
            expect_status(get_renyuan_siling_segments(
                branch, model, nullptr, 0u, &count),
                "count Renyuan Siling table");
            expect(count == (expected[model][branch][2].stem == 0xffu ? 2u : 3u),
                "Renyuan Siling segment count matches Dart table");
            expect_status(get_renyuan_siling_segments(
                branch, model, segments, kRenyuanSilingMaxSegments, &count),
                "copy Renyuan Siling table");
            double accumulated = 0.0;
            for (size_t i = 0; i < count; ++i) {
                const ExpectedEntry& item = expected[model][branch][i];
                expect(segments[i].stem_id == item.stem,
                    "Renyuan Siling stem matches Dart table");
                expect(segments[i].origin_kind == item.origin,
                    "Renyuan Siling origin matches Dart table");
                expect(segments[i].segment_index == i,
                    "Renyuan Siling segment index is stable");
                expect(segments[i].start_day == accumulated,
                    "Renyuan Siling segment starts at accumulated duration");
                accumulated += item.duration;
                expect(segments[i].end_day == accumulated,
                    "Renyuan Siling segment ends at accumulated duration");
            }
            expect(accumulated == 30.0,
                "every Renyuan Siling month profile spans 30 model days");
        }
    }

    size_t count = 0u;
    BaziRenyuanSilingSegment short_buffer[1];
    expect(get_renyuan_siling_segments(
        2u, BaziRenyuanSilingSanMingTongHui,
        short_buffer, 1u, &count) == taiyin::TAIYIN_ERROR_OUT_OF_MEMORY
        && count == 3u,
        "Renyuan Siling reports required count for a short buffer");
    expect(get_renyuan_siling_segments(
        12u, BaziRenyuanSilingSanMingTongHui,
        nullptr, 0u, &count) == taiyin::TAIYIN_ERROR_INVALID_ARGUMENT,
        "Renyuan Siling rejects an invalid month branch");
}

std::vector<taiyin::bazi::BaziRelation> collect_relations(
    const taiyin::bazi::BaziChart& chart,
    uint32_t pillar_mask,
    uint32_t relation_mask,
    const char* label
) {
    size_t count = 0u;
    expect_status(
        taiyin::bazi::collect_chart_relations(
            &chart, pillar_mask, relation_mask, nullptr, 0u, &count),
        label);
    std::vector<taiyin::bazi::BaziRelation> results(count);
    size_t copied_count = 0u;
    expect_status(
        taiyin::bazi::collect_chart_relations(
            &chart, pillar_mask, relation_mask,
            results.empty() ? nullptr : results.data(), results.size(), &copied_count),
        label);
    expect(copied_count == results.size(), "relation count-only result matches copied result");
    return results;
}

bool has_relation(
    const std::vector<taiyin::bazi::BaziRelation>& results,
    taiyin::bazi::BaziRelationKind kind,
    uint32_t pillar_mask,
    uint8_t combined_element = taiyin::bazi::kInvalidWuXing
) {
    for (size_t i = 0; i < results.size(); ++i) {
        if (results[i].kind == kind && results[i].pillar_mask == pillar_mask
            && results[i].combined_element_id == combined_element) {
            return true;
        }
    }
    return false;
}

bool bitset_contains(const uint64_t* words, size_t word_count, size_t bit) {
    const size_t word = bit / 64u;
    return word < word_count && (words[word] & (uint64_t{1} << (bit % 64u))) != 0u;
}

bool index_in(const uint8_t* values, size_t count, uint8_t value) {
    for (size_t i = 0; i < count; ++i) {
        if (values[i] == value) return true;
    }
    return false;
}

uint8_t valid_ganzhi_for_stem(uint8_t stem) {
    return static_cast<uint8_t>((stem << 4u) | (stem & 1u));
}

uint8_t valid_ganzhi_for_branch(uint8_t branch) {
    return static_cast<uint8_t>(((branch & 1u) << 4u) | branch);
}

bool branch_mask_contains(uint16_t mask, uint8_t branch) {
    return (mask & (uint16_t{1} << branch)) != 0u;
}

void test_simple_shen_sha_oracles() {
    using namespace taiyin::bazi;

    BaziChart chart;
    chart.pillars.year = 0x00u;   // Jia-Zi
    chart.pillars.month = 0x22u;  // Bing-Yin (spring)
    chart.pillars.day = 0x42u;    // Wu-Yin
    chart.pillars.hour = 0x30u;   // Ding-Zi

    size_t word_count = 0u;
    expect_status(collect_target_shen_sha(
        &chart, 0x51u, BaziShenShaTargetYear,
        nullptr, 0u, &word_count), "count simple Shen Sha words");
    expect(word_count == kBaziShenShaWordCount,
        "Shen Sha count-only reports stable bitset width");

    uint64_t short_words[1] = {0u};
    expect(collect_target_shen_sha(
               &chart, 0x51u, BaziShenShaTargetYear,
               short_words, 1u, &word_count) == taiyin::TAIYIN_ERROR_OUT_OF_MEMORY
            && word_count == kBaziShenShaWordCount,
        "Shen Sha short buffer reports required words without partial output");
    expect(short_words[0] == 0u, "Shen Sha short buffer is not partially written");

    uint64_t words[kBaziShenShaWordCount] = {};
    expect_status(collect_target_shen_sha(
        &chart, 0x51u, BaziShenShaTargetYear,
        words, kBaziShenShaWordCount, &word_count),
        "collect Tian Yi oracle");
    expect(bitset_contains(words, word_count, BaziShenShaTianYiGuiRen),
        "Jia/Wu sees Chou as Tian Yi Gui Ren");

    expect_status(collect_target_shen_sha(
        &chart, 0x68u, BaziShenShaTargetFlowYear,
        words, kBaziShenShaWordCount, &word_count),
        "collect Yi Ma oracle");
    expect(bitset_contains(words, word_count, BaziShenShaYiMa),
        "Yin source sees Shen as Yi Ma");

    const uint8_t month_pillars[4] = {0x22u, 0x55u, 0x68u, 0x9bu};
    const uint8_t tian_she[4] = {14u, 30u, 44u, 0u};
    const uint8_t kui_gang[] = {16u, 28u, 34u, 46u};
    const uint8_t shi_ling[] = {40u, 11u, 52u, 33u, 54u, 46u, 26u, 47u, 38u, 19u};
    const uint8_t ba_zhuan[] = {50u, 51u, 43u, 34u, 55u, 56u, 57u, 49u};
    const uint8_t liu_xiu[] = {42u, 43u, 24u, 54u, 25u, 55u};
    const uint8_t jiu_chou[] = {33u, 24u, 54u, 15u, 45u, 27u, 57u, 48u, 18u};
    const uint8_t si_fei[4][2] = {{56u, 57u}, {48u, 59u}, {50u, 51u}, {42u, 53u}};
    const uint8_t shi_e_da_bai[] = {40u, 41u, 32u, 23u, 34u, 25u, 16u, 17u, 8u, 59u};
    const uint8_t yin_cha_yang_cuo[] = {12u, 13u, 14u, 27u, 28u, 29u, 42u, 43u, 44u, 57u, 58u, 59u};
    const uint8_t gu_luan[] = {41u, 53u, 47u, 44u, 50u, 54u, 48u, 42u};

    for (uint8_t season = 0; season < 4u; ++season) {
        chart.pillars.month = month_pillars[season];
        for (uint8_t index = 0; index < 60u; ++index) {
            uint8_t target = 0xffu;
            expect_status(taiyin::chinese_calendar::advance_ganzhi(
                0x00u, index, &target), "construct Shen Sha day oracle Ganzhi");
            expect_status(collect_target_shen_sha(
                &chart, target, BaziShenShaTargetDay,
                words, kBaziShenShaWordCount, &word_count),
                "collect exhaustive simple Shen Sha oracle");
            expect(bitset_contains(words, word_count, BaziShenShaTianSheDay)
                    == (index == tian_she[season]),
                "Tian She matches old Dart season/day rule");
            expect(bitset_contains(words, word_count, BaziShenShaKuiGang)
                    == index_in(kui_gang, sizeof(kui_gang), index),
                "Kui Gang set matches old Dart");
            expect(bitset_contains(words, word_count, BaziShenShaShiLingDay)
                    == index_in(shi_ling, sizeof(shi_ling), index),
                "Shi Ling set matches old Dart");
            expect(bitset_contains(words, word_count, BaziShenShaBaZhuanDay)
                    == index_in(ba_zhuan, sizeof(ba_zhuan), index),
                "Ba Zhuan set matches old Dart");
            expect(bitset_contains(words, word_count, BaziShenShaLiuXiuDay)
                    == index_in(liu_xiu, sizeof(liu_xiu), index),
                "Liu Xiu set matches old Dart");
            expect(bitset_contains(words, word_count, BaziShenShaJiuChouDay)
                    == index_in(jiu_chou, sizeof(jiu_chou), index),
                "Jiu Chou set matches old Dart");
            expect(bitset_contains(words, word_count, BaziShenShaSiFeiDay)
                    == index_in(si_fei[season], 2u, index),
                "Si Fei matches old Dart season/day rule");
            expect(bitset_contains(words, word_count, BaziShenShaShiEDaBai)
                    == index_in(shi_e_da_bai, sizeof(shi_e_da_bai), index),
                "Shi E Da Bai set matches old Dart");
            expect(bitset_contains(words, word_count, BaziShenShaYinChaYangCuo)
                    == index_in(yin_cha_yang_cuo, sizeof(yin_cha_yang_cuo), index),
                "Yin Cha Yang Cuo set matches old Dart");
            expect(bitset_contains(words, word_count, BaziShenShaGuLuanSha)
                    == index_in(gu_luan, sizeof(gu_luan), index),
                "Gu Luan set matches old Dart");
        }
    }

    expect_status(collect_target_shen_sha(
        &chart, 0x64u, BaziShenShaTargetFlowYear,
        words, kBaziShenShaWordCount, &word_count),
        "collect non-day target restriction oracle");
    expect(!bitset_contains(words, word_count, BaziShenShaKuiGang)
            && !bitset_contains(words, word_count, BaziShenShaTianSheDay),
        "day-only Shen Sha do not leak onto transit-year targets");

    const uint16_t xian_chi[12] = {
        1u << 9u, 1u << 6u, 1u << 3u, 1u << 0u,
        1u << 9u, 1u << 6u, 1u << 3u, 1u << 0u,
        1u << 9u, 1u << 6u, 1u << 3u, 1u << 0u,
    };
    const uint16_t hong_luan[12] = {
        1u << 3u, 1u << 2u, 1u << 1u, 1u << 0u,
        1u << 11u, 1u << 10u, 1u << 9u, 1u << 8u,
        1u << 7u, 1u << 6u, 1u << 5u, 1u << 4u,
    };
    const uint16_t tian_xi[12] = {
        1u << 9u, 1u << 8u, 1u << 7u, 1u << 6u,
        1u << 5u, 1u << 4u, 1u << 3u, 1u << 2u,
        1u << 1u, 1u << 0u, 1u << 11u, 1u << 10u,
    };
    const uint16_t zai_sha[12] = {
        1u << 6u, 1u << 3u, 1u << 0u, 1u << 9u,
        1u << 6u, 1u << 3u, 1u << 0u, 1u << 9u,
        1u << 6u, 1u << 3u, 1u << 0u, 1u << 9u,
    };
    const uint16_t jie_sha[12] = {
        1u << 5u, 1u << 2u, 1u << 11u, 1u << 8u,
        1u << 5u, 1u << 2u, 1u << 11u, 1u << 8u,
        1u << 5u, 1u << 2u, 1u << 11u, 1u << 8u,
    };
    const uint16_t wang_shen[12] = {
        1u << 11u, 1u << 8u, 1u << 5u, 1u << 2u,
        1u << 11u, 1u << 8u, 1u << 5u, 1u << 2u,
        1u << 11u, 1u << 8u, 1u << 5u, 1u << 2u,
    };
    chart.pillars.month = 0x22u;
    for (uint8_t year_branch = 0; year_branch < 12u; ++year_branch) {
        chart.pillars.year = valid_ganzhi_for_branch(year_branch);
        for (uint8_t day_branch = 0; day_branch < 12u; ++day_branch) {
            chart.pillars.day = valid_ganzhi_for_branch(day_branch);
            for (uint8_t target_branch = 0; target_branch < 12u; ++target_branch) {
                expect_status(collect_target_shen_sha(
                    &chart, valid_ganzhi_for_branch(target_branch),
                    BaziShenShaTargetFlowYear, words,
                    kBaziShenShaWordCount, &word_count),
                    "collect branch-table Shen Sha oracle");
                expect(bitset_contains(words, word_count, BaziShenShaXianChiTaoHua)
                        == (branch_mask_contains(xian_chi[year_branch], target_branch)
                            || branch_mask_contains(xian_chi[day_branch], target_branch)),
                    "Xian Chi table matches old Dart");
                expect(bitset_contains(words, word_count, BaziShenShaHongLuan)
                        == branch_mask_contains(hong_luan[year_branch], target_branch),
                    "Hong Luan uses the old Dart year-branch table only");
                expect(bitset_contains(words, word_count, BaziShenShaTianXi)
                        == branch_mask_contains(tian_xi[year_branch], target_branch),
                    "Tian Xi uses the old Dart year-branch table only");
                expect(bitset_contains(words, word_count, BaziShenShaZaiSha)
                        == (branch_mask_contains(zai_sha[year_branch], target_branch)
                            || branch_mask_contains(zai_sha[day_branch], target_branch)),
                    "Zai Sha table matches old Dart");
                expect(bitset_contains(words, word_count, BaziShenShaJieSha)
                        == (branch_mask_contains(jie_sha[year_branch], target_branch)
                            || branch_mask_contains(jie_sha[day_branch], target_branch)),
                    "Jie Sha table matches old Dart");
                expect(bitset_contains(words, word_count, BaziShenShaWangShen)
                        == (branch_mask_contains(wang_shen[year_branch], target_branch)
                            || branch_mask_contains(wang_shen[day_branch], target_branch)),
                    "Wang Shen table matches old Dart");
            }
        }
    }

    const uint16_t yang_ren[10] = {
        1u << 3u, 1u << 2u, 1u << 6u, 1u << 5u, 1u << 6u,
        1u << 5u, 1u << 9u, 1u << 8u, 1u << 0u, 1u << 11u,
    };
    const uint16_t fei_ren[10] = {
        1u << 9u, 1u << 8u, 1u << 0u, 1u << 11u, 1u << 0u,
        1u << 11u, 1u << 3u, 1u << 2u, 1u << 6u, 1u << 5u,
    };
    const uint16_t fu_xing[10] = {
        (1u << 2u) | (1u << 0u), (1u << 3u) | (1u << 1u),
        (1u << 2u) | (1u << 0u), 1u << 11u, 1u << 8u,
        1u << 7u, 1u << 6u, 1u << 5u, 1u << 4u,
        (1u << 3u) | (1u << 1u),
    };
    const uint16_t tian_chu[10] = {
        1u << 5u, 1u << 6u, 1u << 5u, 1u << 6u, 1u << 8u,
        1u << 9u, 1u << 11u, 1u << 0u, 1u << 2u, 1u << 3u,
    };
    for (uint8_t year_stem = 0; year_stem < 10u; ++year_stem) {
        chart.pillars.year = valid_ganzhi_for_stem(year_stem);
        for (uint8_t day_stem = 0; day_stem < 10u; ++day_stem) {
            chart.pillars.day = valid_ganzhi_for_stem(day_stem);
            for (uint8_t target_branch = 0; target_branch < 12u; ++target_branch) {
                expect_status(collect_target_shen_sha(
                    &chart, valid_ganzhi_for_branch(target_branch),
                    BaziShenShaTargetFlowYear, words,
                    kBaziShenShaWordCount, &word_count),
                    "collect stem-table Shen Sha oracle");
                expect(bitset_contains(words, word_count, BaziShenShaYangRen)
                        == branch_mask_contains(yang_ren[day_stem], target_branch),
                    "Yang Ren uses the old Dart day-stem table only");
                expect(bitset_contains(words, word_count, BaziShenShaFeiRen)
                        == branch_mask_contains(fei_ren[day_stem], target_branch),
                    "Fei Ren uses the old Dart day-stem table only");
                expect(bitset_contains(words, word_count, BaziShenShaFuXingGuiRen)
                        == (branch_mask_contains(fu_xing[year_stem], target_branch)
                            || branch_mask_contains(fu_xing[day_stem], target_branch)),
                    "Fu Xing Gui Ren table matches old Dart");
                expect(bitset_contains(words, word_count, BaziShenShaTianChuGuiRen)
                        == (branch_mask_contains(tian_chu[year_stem], target_branch)
                            || branch_mask_contains(tian_chu[day_stem], target_branch)),
                    "Tian Chu Gui Ren table matches old Dart");
            }
        }
    }
}

uint64_t fnv1a_word(uint64_t hash, uint64_t word) {
    constexpr uint64_t kFnvPrime = UINT64_C(0x100000001b3);
    for (unsigned int shift = 0; shift < 64u; shift += 8u) {
        hash ^= (word >> shift) & UINT64_C(0xff);
        hash *= kFnvPrime;
    }
    return hash;
}

void test_exhaustive_shen_sha_dart_fingerprint() {
    using namespace taiyin::bazi;

    BaziChart chart;
    chart.pillars.month = 0x22u;
    chart.pillars.hour = 0x00u;
    uint64_t words[kBaziShenShaWordCount] = {};
    size_t word_count = 0u;
    uint64_t hash = UINT64_C(0xcbf29ce484222325);
    uint64_t gender_hash[2] = {
        UINT64_C(0xcbf29ce484222325),
        UINT64_C(0xcbf29ce484222325),
    };
    size_t chart_count = 0u;

    for (uint8_t year_index = 0; year_index < 60u; ++year_index) {
        expect_status(taiyin::chinese_calendar::advance_ganzhi(
            0x00u, year_index, &chart.pillars.year),
            "construct exhaustive Shen Sha year");
        const uint8_t year_stem = chart.pillars.year >> 4u;
        for (uint8_t month_index = 0; month_index < 12u; ++month_index) {
            expect_status(taiyin::chinese_calendar::get_month_ganzhi(
                year_stem, month_index, &chart.pillars.month),
                "construct exhaustive Shen Sha month");
            for (uint8_t day_index = 0; day_index < 60u; ++day_index) {
                expect_status(taiyin::chinese_calendar::advance_ganzhi(
                    0x00u, day_index, &chart.pillars.day),
                    "construct exhaustive Shen Sha day");
                const uint8_t day_stem = chart.pillars.day >> 4u;
                for (uint8_t hour_index = 0; hour_index < 12u; ++hour_index) {
                    expect_status(taiyin::chinese_calendar::get_hour_ganzhi(
                        day_stem, hour_index, &chart.pillars.hour),
                        "construct exhaustive Shen Sha hour");
                    const uint8_t targets[4] = {
                        chart.pillars.year,
                        chart.pillars.month,
                        chart.pillars.day,
                        chart.pillars.hour,
                    };
                    for (int32_t target_kind = BaziShenShaTargetYear;
                         target_kind <= BaziShenShaTargetHour;
                         ++target_kind) {
                        expect_status(collect_target_shen_sha(
                            &chart, targets[target_kind], target_kind,
                            words, kBaziShenShaWordCount, &word_count),
                            "collect exhaustive Shen Sha fingerprint");
                        expect(word_count == kBaziShenShaWordCount,
                            "exhaustive Shen Sha word count");
                        for (size_t word = 0; word < word_count; ++word) {
                            hash = fnv1a_word(hash, words[word]);
                        }
                        for (int32_t gender = BaziGenderFemale;
                             gender <= BaziGenderMale; ++gender) {
                            expect_status(collect_target_shen_sha_with_gender(
                                &chart, targets[target_kind], target_kind, gender,
                                words, kBaziShenShaWordCount, &word_count),
                                "collect gender-aware Shen Sha fingerprint");
                            expect(word_count == kBaziShenShaWordCount,
                                "gender-aware Shen Sha word count");
                            for (size_t word = 0; word < word_count; ++word) {
                                gender_hash[gender] = fnv1a_word(
                                    gender_hash[gender], words[word]);
                            }
                        }
                    }
                    ++chart_count;
                }
            }
        }
    }

    expect(chart_count == 518400u, "exhaustive Shen Sha chart-space size");
    expect(hash == UINT64_C(0xf786d8f1fe672575),
        "all 518400 charts match the old Dart Shen Sha fingerprint");
    expect(gender_hash[BaziGenderMale] == UINT64_C(0xeadbdb6530c916a5),
        "male gender-aware Shen Sha rules match the old Dart fingerprint");
    expect(gender_hash[BaziGenderFemale] == UINT64_C(0x15f9f46ec22459ed),
        "female gender-aware Shen Sha rules match the old Dart fingerprint");
}

void test_relation_aggregation() {
    using namespace taiyin::bazi;

    BaziChart stem_chart;
    stem_chart.pillars.year = 0x00u;   // Jia-Zi
    stem_chart.pillars.month = 0x55u;  // Ji-Si
    stem_chart.pillars.day = 0x04u;    // Jia-Chen
    stem_chart.pillars.hour = 0x59u;   // Ji-You
    stem_chart.extra.ming_gong = 0x00u;
    stem_chart.extra.shen_gong = 0x15u;
    stem_chart.extra.tai_yuan = 0x24u;
    stem_chart.extra.tai_xi = 0x68u;

    const std::vector<BaziRelation> primary_stems = collect_relations(
        stem_chart, BaziRelationPillarPrimary, kBaziRelationKindMaskAll,
        "collect primary relation graph");
    expect(has_relation(
               primary_stems, BaziRelationStemCombination,
               BaziRelationPillarPrimary, BaziWuXingEarth),
        "Jia/Ji copies merge into one primary stem combination");

    const std::vector<BaziRelation> extended_stems = collect_relations(
        stem_chart, BaziRelationPillarPrimary | BaziRelationPillarMingGong,
        1u << BaziRelationStemCombination,
        "collect extended stem relation graph");
    expect(extended_stems.size() == 1u
            && has_relation(
                extended_stems, BaziRelationStemCombination,
                BaziRelationPillarPrimary | BaziRelationPillarMingGong,
                BaziWuXingEarth),
        "extra pillars participate only when their explicit mask is selected");

    BaziChart triple_chart;
    triple_chart.pillars.year = 0x68u;   // Geng-Shen
    triple_chart.pillars.month = 0x20u;  // Bing-Zi
    triple_chart.pillars.day = 0x44u;    // Wu-Chen
    triple_chart.pillars.hour = 0x55u;   // Ji-Si
    const std::vector<BaziRelation> triple_results = collect_relations(
        triple_chart, BaziRelationPillarPrimary, kBaziRelationKindMaskAll,
        "collect Shen-Zi-Chen relation graph");
    const uint32_t triple_mask = BaziRelationPillarYear
        | BaziRelationPillarMonth | BaziRelationPillarDay;
    expect(has_relation(
               triple_results, BaziRelationBranchTripleCombination,
               triple_mask, BaziWuXingWater),
        "Shen-Zi-Chen produces the old Dart triple-combination result");
    for (size_t i = 0; i < triple_results.size(); ++i) {
        const BaziRelation& relation = triple_results[i];
        const bool is_internal_pair = (relation.pillar_mask & ~triple_mask) == 0u;
        expect(!(is_internal_pair && (relation.kind == BaziRelationBranchHalfCombination
            || relation.kind == BaziRelationBranchArchingCombination
            || relation.kind == BaziRelationBranchPunishment)),
            "full triple suppresses internal half/arching/punishment pairs");
    }

    BaziChart direction_chart;
    direction_chart.pillars.year = 0x1bu;   // Yi-Hai
    direction_chart.pillars.month = 0x20u; // Bing-Zi
    direction_chart.pillars.day = 0x11u;   // Yi-Chou
    direction_chart.pillars.hour = 0x55u;  // Ji-Si
    const std::vector<BaziRelation> direction_results = collect_relations(
        direction_chart, BaziRelationPillarPrimary,
        1u << BaziRelationBranchTripleDirection,
        "collect Hai-Zi-Chou triple-direction graph");
    expect(has_relation(
               direction_results, BaziRelationBranchTripleDirection,
               triple_mask, BaziWuXingWater),
        "Hai-Zi-Chou produces the old Dart triple-direction result");

    const uint32_t half_and_arching_mask =
        (1u << BaziRelationBranchHalfCombination)
        | (1u << BaziRelationBranchArchingCombination);
    const std::vector<BaziRelation> half_results = collect_relations(
        triple_chart, BaziRelationPillarYear | BaziRelationPillarMonth,
        half_and_arching_mask, "collect Shen-Zi half-combination graph");
    expect(has_relation(
               half_results, BaziRelationBranchHalfCombination,
               BaziRelationPillarYear | BaziRelationPillarMonth, BaziWuXingWater),
        "Shen-Zi produces the old Dart half-combination result");

    const std::vector<BaziRelation> arching_results = collect_relations(
        triple_chart, BaziRelationPillarYear | BaziRelationPillarDay,
        half_and_arching_mask, "collect Shen-Chen arching-combination graph");
    expect(has_relation(
               arching_results, BaziRelationBranchArchingCombination,
               BaziRelationPillarYear | BaziRelationPillarDay, BaziWuXingWater),
        "Shen-Chen produces the old Dart arching combination result");

    BaziChart hidden_chart;
    hidden_chart.pillars.year = 0x00u;   // Jia-Zi
    hidden_chart.pillars.month = 0x15u;  // Yi-Si
    hidden_chart.pillars.day = 0x02u;    // Jia-Yin
    hidden_chart.pillars.hour = 0x44u;   // Wu-Chen
    const uint32_t hidden_and_severance_mask =
        (1u << BaziRelationBranchHiddenCombination)
        | (1u << BaziRelationBranchSeverance);
    const std::vector<BaziRelation> hidden_results = collect_relations(
        hidden_chart, BaziRelationPillarPrimary, hidden_and_severance_mask,
        "collect Zi-Si hidden/severance graph");
    expect(has_relation(
               hidden_results, BaziRelationBranchHiddenCombination,
               BaziRelationPillarYear | BaziRelationPillarMonth),
        "Zi-Si produces the old Dart hidden-combination result");
    expect(has_relation(
               hidden_results, BaziRelationBranchSeverance,
               BaziRelationPillarYear | BaziRelationPillarMonth),
        "Zi-Si produces the old Dart severance result");

    BaziChart punishment_chart;
    punishment_chart.pillars.year = 0x02u;   // Jia-Yin
    punishment_chart.pillars.month = 0x15u;  // Yi-Si
    punishment_chart.pillars.day = 0x68u;    // Geng-Shen
    punishment_chart.pillars.hour = 0x00u;   // Jia-Zi
    const std::vector<BaziRelation> punishment_results = collect_relations(
        punishment_chart, BaziRelationPillarPrimary, kBaziRelationKindMaskAll,
        "collect Yin-Si-Shen punishment graph");
    expect(has_relation(
               punishment_results, BaziRelationBranchTriplePunishment, triple_mask),
        "Yin-Si-Shen produces the old Dart triple-punishment result");

    BaziChart self_chart;
    self_chart.pillars.year = 0x04u;
    self_chart.pillars.month = 0x24u;
    self_chart.pillars.day = 0x44u;
    self_chart.pillars.hour = 0x64u;
    const std::vector<BaziRelation> self_results = collect_relations(
        self_chart, BaziRelationPillarPrimary,
        1u << BaziRelationBranchSelfPunishment,
        "collect Chen self-punishment graph");
    expect(self_results.size() == 1u && has_relation(
               self_results, BaziRelationBranchSelfPunishment,
               BaziRelationPillarPrimary),
        "repeated Chen values merge into one self-punishment relation");

    size_t required_count = 123u;
    BaziRelation too_small[1];
    expect(taiyin::bazi::collect_chart_relations(
               &triple_chart, BaziRelationPillarPrimary, kBaziRelationKindMaskAll,
               too_small, 1u, &required_count) == taiyin::TAIYIN_ERROR_OUT_OF_MEMORY
            && required_count > 1u,
        "relation collection reports required count for a short buffer");
    expect(taiyin::bazi::collect_chart_relations(
               &triple_chart, 0u, kBaziRelationKindMaskAll,
               nullptr, 0u, &required_count) == taiyin::TAIYIN_ERROR_INVALID_ARGUMENT,
        "relation collection rejects an empty pillar mask");
}

}  // namespace

int main() {
    test_sexagenary_encoding();
    test_chart_from_four_pillars();
    test_flow_primitives();
    test_dayun_cycle();
    test_renyuan_siling_tables();
    test_relation_aggregation();
    test_simple_shen_sha_oracles();
    test_exhaustive_shen_sha_dart_fingerprint();
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
