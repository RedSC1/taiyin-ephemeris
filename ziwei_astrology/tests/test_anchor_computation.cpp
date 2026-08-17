#include "taiyin/ziwei/anchors.h"

#include <cstddef>
#include <cstdint>
#include <iostream>
#include <limits>

namespace {

void expect(bool condition, const char* message, int* failures) {
    if (condition) return;
    std::cerr << "FAIL: " << message << '\n';
    ++*failures;
}

taiyin::ziwei::Ganzhi ganzhi(uint8_t stem, uint8_t branch) {
    return taiyin::ziwei::Ganzhi{
        static_cast<taiyin::ziwei::Stem>(stem),
        static_cast<taiyin::ziwei::Branch>(branch),
    };
}

taiyin::ziwei::CalendarFacts base_facts() {
    using namespace taiyin::ziwei;
    CalendarFacts facts = {};
    facts.lunar_date.year = 2026;
    facts.lunar_date.month = 1u;
    facts.lunar_date.day = 1u;
    facts.effective_lunar_year = 2026;
    facts.effective_lunar_month = 1u;
    facts.solar_day_from_previous_jie = 1u;
    facts.solar_term_pillars = Pillars{
        ganzhi(0u, 0u), ganzhi(2u, 2u),
        ganzhi(4u, 4u), ganzhi(6u, 6u),
    };
    facts.lunar_pillars = Pillars{
        ganzhi(0u, 0u), ganzhi(2u, 2u),
        ganzhi(4u, 4u), ganzhi(6u, 0u),
    };
    return facts;
}

}  // namespace

int main() {
    using namespace taiyin;
    using namespace taiyin::ziwei;
    int failures = 0;

    {
        LunarDateFacts lunar = {};
        lunar.year = 2026;
        lunar.month = 4u;
        lunar.day = 15u;
        lunar.is_leap = 1u;
        int32_t year = 0;
        uint8_t month = 0u;
        expect(resolve_effective_lunar_month(lunar,
            LeapMonthStrategy::AsPrevious, &year, &month) == TAIYIN_STATUS_OK
            && year == 2026 && month == 4u,
            "leap month as previous", &failures);
        expect(resolve_effective_lunar_month(lunar,
            LeapMonthStrategy::AsNext, &year, &month) == TAIYIN_STATUS_OK
            && year == 2026 && month == 5u,
            "leap month as next", &failures);
        expect(resolve_effective_lunar_month(lunar,
            LeapMonthStrategy::SplitAfterFifteenth, &year, &month)
                == TAIYIN_STATUS_OK && month == 4u,
            "split keeps fifteenth in previous month", &failures);
        lunar.day = 16u;
        expect(resolve_effective_lunar_month(lunar,
            LeapMonthStrategy::SplitAfterFifteenth, &year, &month)
                == TAIYIN_STATUS_OK && month == 5u,
            "split advances after fifteenth", &failures);
        lunar.month = 13u;
        expect(resolve_effective_lunar_month(lunar,
            LeapMonthStrategy::AsNext, &year, &month) == TAIYIN_STATUS_OK
            && year == 2027 && month == 1u,
            "month thirteen normalizes then advances across year", &failures);
        lunar.year = std::numeric_limits<int32_t>::max();
        expect(resolve_effective_lunar_month(lunar,
            LeapMonthStrategy::AsNext, &year, &month)
                == TAIYIN_ERROR_INVALID_ARGUMENT,
            "reject unrepresentable lunar-year rollover", &failures);
    }

    {
        CalendarFacts facts = base_facts();
        Anchors anchors;
        Branch body = Branch::Zi;
        const AnchorOptions options = default_anchor_options();
        expect(compute_anchors(facts, options, &anchors, &body)
            == TAIYIN_STATUS_OK, "representative anchor computation", &failures);
        expect(anchors.palace_positions[to_index(PalaceId::Life)] == Branch::Yin,
            "first month Zi hour puts life in Yin", &failures);
        expect(body == Branch::Yin, "representative body palace", &failures);
        expect(anchors.bureau == Bureau::Fire6,
            "Jia-year Bing-Yin life palace gives Fire6", &failures);
        expect(anchors.ziwei == Branch::You && anchors.tianfu == Branch::Wei,
            "day-one Fire6 Ziwei/Tianfu oracle", &failures);
        expect(anchors.solar_term.year.stem == Stem::Jia
            && anchors.lunar.year.stem == Stem::Jia,
            "both pillar sets preserved", &failures);
    }

    {
        CalendarFacts facts = base_facts();
        facts.solar_term_pillars.year = ganzhi(0u, 0u);
        facts.lunar_pillars.year = ganzhi(1u, 1u);
        Anchors solar_anchors;
        Anchors lunar_anchors;
        Branch ignored = Branch::Zi;
        AnchorOptions solar_options = default_anchor_options();
        solar_options.rules.wu_hu_dun_year_boundary = PillarBoundary::SolarTerm;
        AnchorOptions lunar_options = default_anchor_options();
        lunar_options.rules.wu_hu_dun_year_boundary = PillarBoundary::Lunar;
        expect(compute_anchors(facts, solar_options, &solar_anchors, &ignored)
            == TAIYIN_STATUS_OK, "solar boundary anchors", &failures);
        expect(compute_anchors(facts, lunar_options, &lunar_anchors, &ignored)
            == TAIYIN_STATUS_OK, "lunar boundary anchors", &failures);
        expect(solar_anchors.bureau == Bureau::Fire6
            && lunar_anchors.bureau == Bureau::Earth5,
            "palace-stem boundary is an explicit school option", &failures);
    }

    std::size_t exhaustive_cases = 0u;
    for (uint8_t stem = 0u; stem < kStemCount; ++stem) {
        for (uint8_t month = 1u; month <= 12u; ++month) {
            for (uint8_t hour = 0u; hour < kBranchCount; ++hour) {
                for (uint8_t day = 1u; day <= 30u; ++day) {
                    for (uint8_t mode = 0u; mode < 3u; ++mode) {
                        CalendarFacts facts = base_facts();
                        const uint8_t year_branch = stem & 1u;
                        const uint8_t hour_stem = hour & 1u;
                        facts.solar_term_pillars.year = ganzhi(stem, year_branch);
                        facts.lunar_pillars.year = ganzhi(stem, year_branch);
                        facts.lunar_pillars.hour = ganzhi(hour_stem, hour);
                        facts.lunar_date.day = day;
                        facts.effective_lunar_month = month;

                        AnchorOptions options = default_anchor_options();
                        options.chart_mode = static_cast<ZiweiChartMode>(mode);
                        Anchors anchors;
                        Branch body = Branch::Zi;
                        const Status status = compute_anchors(
                            facts, options, &anchors, &body);
                        if (status != TAIYIN_STATUS_OK
                            || !validate_anchors(anchors)
                            || !is_valid(body)) {
                            expect(false, "exhaustive anchor state", &failures);
                            stem = kStemCount;
                            month = 13u;
                            hour = kBranchCount;
                            day = 31u;
                            mode = 3u;
                            break;
                        }
                        ++exhaustive_cases;
                    }
                }
            }
        }
    }
    expect(exhaustive_cases == 10u * 12u * 12u * 30u * 3u,
        "exhausted 129600 finite anchor states", &failures);

    {
        CalendarFacts invalid = base_facts();
        invalid.effective_lunar_month = 0u;
        Anchors anchors;
        Branch body = Branch::Zi;
        expect(compute_anchors(invalid, default_anchor_options(), &anchors, &body)
            == TAIYIN_ERROR_INVALID_ARGUMENT,
            "reject invalid effective month", &failures);
    }

    if (failures != 0) {
        std::cerr << failures << " Ziwei anchor-computation checks failed\n";
        return 1;
    }
    std::cout << "Ziwei anchor-computation checks passed ("
              << exhaustive_cases << " exhaustive states)\n";
    return 0;
}
