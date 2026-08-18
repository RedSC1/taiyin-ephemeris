#include "taiyin/body_id.h"
#include "taiyin/chinese_calendar/calendar.h"
#include "taiyin/runtime/ephemeris_route.h"
#include "taiyin/runtime/native_context.h"
#include "taiyin/runtime/runtime.h"
#include "taiyin/time.h"
#include "taiyin/ziwei/flow_calendar_adapter.h"
#include "taiyin/ziwei/debug_dump.h"
#include "taiyin/ziwei/rules_loader.h"

#include <cstdlib>
#include <iostream>
#include <string>

#ifndef TAIYIN_ZIWEI_TEST_ROOT
#define TAIYIN_ZIWEI_TEST_ROOT "."
#endif

namespace {

void expect(bool condition, const char* message, int* failures) {
    if (condition) return;
    std::cerr << "FAIL: " << message << '\n';
    ++*failures;
}

bool encode_china_standard(
    const taiyin::CalendarDateTime& local,
    taiyin::SplitJulianDate* out
) {
    return taiyin::julian_day_split(local, out)
        && taiyin::add_seconds_to_split_jd(
            *out, -8.0 * 3600.0, out);
}

}  // namespace

int main() {
    using namespace taiyin;
    using namespace taiyin::ziwei;
    int failures = 0;

    runtime::EphemerisRuntimeConfig runtime_config;
    runtime_config.load_packaged_data = true;
    expect(runtime::initialize_global_ephemeris_runtime(runtime_config),
        "initialize packaged ephemeris runtime", &failures);

    runtime::NativeCalcContext astronomy;
    expect(runtime::native_context_set_geocentric_observer(
        &astronomy, TAIYIN_BODY_EARTH, TAIYIN_BODY_EARTH)
        == TAIYIN_STATUS_OK, "configure observer", &failures);
    expect(runtime::native_context_set_route_rule(
        &astronomy, runtime::TAIYIN_EPHEMERIS_ROUTE_AUTO)
        == TAIYIN_STATUS_OK, "configure route", &failures);

    chinese_calendar::ChineseCalendarContext calendar;
    const chinese_calendar::ChineseCalendarConfig calendar_config =
        chinese_calendar::historical_china_config();
    expect(chinese_calendar::initialize_context(
        &calendar, &astronomy, &calendar_config) == TAIYIN_STATUS_OK,
        "initialize historical China calendar", &failures);

    // With an unsplit Rat hour, a Jie that occurs from 23:00 to midnight is
    // already part of the following logical day. The minute after it must be
    // solar day 1, rather than day 2 from applying the late-Rat shift only to
    // the current time.
    bool found_late_rat_jie = false;
    runtime::EphemerisEvalDiagnostic late_rat_diagnostic;
    for (int year = 2000; year <= 2010 && !found_late_rat_jie; ++year) {
        for (uint8_t term = 0u; term < 24u && !found_late_rat_jie; ++term) {
            chinese_calendar::SolarTermEvent jie;
            if (chinese_calendar::getSpecificJieQi(
                    &calendar, year, term, &jie, &late_rat_diagnostic)
                != TAIYIN_STATUS_OK) {
                continue;
            }
            const SplitJulianDate jie_virtual = jie.jd_ut + 8.0 / 24.0;
            CalendarDateTime jie_clock;
            if (!reverse_julian_day_split(jie_virtual, &jie_clock)
                || jie_clock.hour < 23) {
                continue;
            }
            const SplitJulianDate target_instant = jie.jd_ut + 60.0 / 86400.0;
            CalendarDateTime target_clock;
            ResolvedBirth resolved;
            const BirthResolutionOptions options =
                default_birth_resolution_options();
            expect(reverse_julian_day_split(
                    jie_virtual + 60.0 / 86400.0, &target_clock)
                    && resolve_birth_from_calendar(
                        &calendar, target_instant, target_clock,
                        Gender::Female, options, &resolved,
                        &late_rat_diagnostic)
                            == TAIYIN_STATUS_OK
                    && resolved.facts.solar_day_from_previous_jie == 1u,
                "late-Rat Jie starts solar day one", &failures);
            found_late_rat_jie = true;
        }
    }
    expect(found_late_rat_jie, "find a late-Rat Jie regression fixture",
        &failures);

    const LoadedRules rules = load_rules_from_toml(
        std::string(TAIYIN_ZIWEI_TEST_ROOT) + "/rules/default.toml");
    runtime::EphemerisEvalDiagnostic diagnostic;

    const CalendarDateTime birth_clock = {2003, 3, 13, 14, 15, 0.0};
    SplitJulianDate birth_instant;
    expect(encode_china_standard(birth_clock, &birth_instant),
        "encode birth instant", &failures);
    const BirthResolutionOptions birth_options =
        default_birth_resolution_options();
    ResolvedBirth birth;
    expect(resolve_birth_from_calendar(
        &calendar,
        birth_instant,
        birth_clock,
        Gender::Female,
        birth_options,
        &birth,
        &diagnostic) == TAIYIN_STATUS_OK,
        "resolve birth", &failures);
    NatalChart natal;
    expect(make_natal_chart(
        birth.facts,
        birth.anchors,
        birth.body_palace,
        birth_options.anchor_options.rules,
        rules.compiled,
        &natal) == TAIYIN_STATUS_OK,
        "build natal chart", &failures);

    // A same-lunar-year target before the physical birth instant is never a
    // valid flow stack, even though its inclusive virtual age would be one.
    const CalendarDateTime pre_birth_same_year_clock = {
        2003, 3, 13, 14, 14, 0.0};
    SplitJulianDate pre_birth_same_year_instant;
    ResolvedFlow pre_birth_same_year_flow;
    const FlowResolutionOptions pre_birth_same_year_options =
        default_flow_resolution_options();
    expect(encode_china_standard(
            pre_birth_same_year_clock, &pre_birth_same_year_instant)
        && resolve_flow_from_calendar(
            &calendar, birth, natal, pre_birth_same_year_instant,
            pre_birth_same_year_clock, pre_birth_same_year_options,
            &pre_birth_same_year_flow, &diagnostic)
            == TAIYIN_ERROR_INVALID_ARGUMENT,
        "flow rejects a same-year target before the birth instant", &failures);

    // Calendar facts are part of a natal chart identity, not merely the
    // finite anchor encoding. This simulates a caller mixing an otherwise
    // identical resolved birth with the wrong physical instant.
    ResolvedBirth mismatched_birth = birth;
    mismatched_birth.facts.birth.instant_utc = birth_instant + 1.0;
    expect(resolve_flow_from_calendar(
            &calendar, mismatched_birth, natal, birth_instant, birth_clock,
            pre_birth_same_year_options, &pre_birth_same_year_flow, &diagnostic)
            == TAIYIN_ERROR_INVALID_ARGUMENT,
        "flow rejects natal charts paired with different birth facts", &failures);

    // A leap twelfth-month birth may be treated as the following logical
    // month/year for natal placement. Flow-year comparison must nevertheless
    // use the same physical lunar-year labeling for both birth and target.
    const CalendarDateTime leap_twelve_clock = {1575, 1, 15, 8, 0, 0.0};
    SplitJulianDate leap_twelve_instant;
    BirthResolutionOptions leap_twelve_options = birth_options;
    leap_twelve_options.leap_month_strategy = LeapMonthStrategy::AsNext;
    ResolvedBirth leap_twelve_birth;
    NatalChart leap_twelve_natal;
    expect(encode_china_standard(leap_twelve_clock, &leap_twelve_instant)
        && resolve_birth_from_calendar(
            &calendar, leap_twelve_instant, leap_twelve_clock,
            Gender::Male, leap_twelve_options, &leap_twelve_birth,
            &diagnostic) == TAIYIN_STATUS_OK
        && make_natal_chart(
            leap_twelve_birth.facts, leap_twelve_birth.anchors,
            leap_twelve_birth.body_palace,
            leap_twelve_options.anchor_options.rules, rules.compiled,
            &leap_twelve_natal) == TAIYIN_STATUS_OK,
        "build AsNext leap-twelfth natal chart", &failures);
    ResolvedFlow leap_twelve_flow;
    const FlowResolutionOptions leap_twelve_flow_options =
        default_flow_resolution_options();
    expect(resolve_flow_from_calendar(
        &calendar, leap_twelve_birth, leap_twelve_natal,
        leap_twelve_instant, leap_twelve_clock, leap_twelve_flow_options,
        &leap_twelve_flow, &diagnostic) == TAIYIN_STATUS_OK,
        "flow at an AsNext leap-twelfth birth is not before birth", &failures);

    // This target is leap second month, day four.  Its physical sequence is
    // three for the Liu-Nian Dou-Jun palace progression, but it has no
    // Zhong-Qi, so its Wu-Hu-Dun stem must repeat the preceding Mao-built
    // month rather than advance as a third ordinary month.
    const CalendarDateTime target_clock = {2023, 3, 25, 10, 30, 0.0};
    SplitJulianDate target_instant;
    expect(encode_china_standard(target_clock, &target_instant),
        "encode leap-month target", &failures);
    const FlowResolutionOptions lunar_options =
        default_flow_resolution_options();
    ResolvedFlow lunar;
    expect(resolve_flow_from_calendar(
        &calendar,
        birth,
        natal,
        target_instant,
        target_clock,
        lunar_options,
        &lunar,
        &diagnostic) == TAIYIN_STATUS_OK,
        "resolve complete lunar-boundary flow", &failures);
    expect(lunar.effective_target_year == 2023
        && lunar.target_month == 2u
        && lunar.target_month_sequence == 3u
        && lunar.target_month_is_leap
        && lunar.target_day == 4u
        && lunar.target_hour_index == 5u,
        "resolve leap-month calendar coordinates", &failures);

    // The historical Dart engine advanced the month stem by sequence through
    // a leap month.  The calendar-backed adapter intentionally differs here:
    // it derives the lunar flow-month stem from the contained/preceding
    // Zhong-Qi, while keeping the physical sequence for Dou-Jun.
    expect(lunar.decade.index == 2u
        && lunar.decade.limit.coordinate.stem == Stem::Xin
        && lunar.decade.limit.coordinate.branch == Branch::You
        && lunar.small_limit.virtual_age == 21
        && lunar.small_limit.coordinate.stem == Stem::Ding
        && lunar.small_limit.coordinate.branch == Branch::Si
        && lunar.year.limit.coordinate.stem == Stem::Gui
        && lunar.year.limit.coordinate.branch == Branch::Mao
        && lunar.month.limit.coordinate.stem == Stem::Yi
        && lunar.month.limit.coordinate.branch == Branch::Hai
        && lunar.day.limit.coordinate.stem == Stem::Ren
        && lunar.day.limit.coordinate.branch == Branch::Yin
        && lunar.hour.limit.coordinate.stem == Stem::Yi
        && lunar.hour.limit.coordinate.branch == Branch::Wei,
        "Dart physical-time flow oracle", &failures);

    Chart chart;
    chart.natal = natal;
    ResolvedFlow installed;
    expect(set_flow_stack_from_calendar(
        &calendar,
        birth,
        target_instant,
        target_clock,
        lunar_options,
        rules.compiled,
        &chart,
        &installed,
        &diagnostic) == TAIYIN_STATUS_OK,
        "install complete flow stack", &failures);
    expect(chart.flow_stack.size() == kFlowLevelCount
        && chart.flow_stack[0].level == FlowLevel::Decade
        && chart.flow_stack[1].level == FlowLevel::Year
        && chart.flow_stack[2].level == FlowLevel::Month
        && chart.flow_stack[3].level == FlowLevel::Day
        && chart.flow_stack[4].level == FlowLevel::Hour,
        "calendar adapter installs the canonical five-level order", &failures);
    for (std::size_t level = 0u; level < chart.flow_stack.size(); ++level) {
        std::size_t count = 0u;
        for (std::size_t branch = 0u; branch < kBranchCount; ++branch) {
            count += chart.flow_stack[level].stars[branch].count();
        }
        expect(count == 44u, "each resolved flow layer places 44 stars",
            &failures);
    }
    Chart month_only;
    month_only.natal = natal;
    expect(set_flow_stack_through_from_calendar(
        &calendar,
        birth,
        target_instant,
        target_clock,
        lunar_options,
        FlowLevel::Month,
        rules.compiled,
        &month_only,
        NULL,
        &diagnostic) == TAIYIN_STATUS_OK
        && month_only.flow_stack.size() == 3u
        && month_only.flow_stack.back().level == FlowLevel::Month,
        "calendar adapter can stop at a selected contiguous flow depth",
        &failures);

    std::vector<int64_t> chart_dump;
    std::vector<int64_t> repeated_chart_dump;
    std::vector<int64_t> flow_dump;
    expect(dump_chart_numeric(chart, &chart_dump) == TAIYIN_STATUS_OK
        && dump_chart_numeric(chart, &repeated_chart_dump) == TAIYIN_STATUS_OK
        && chart_dump == repeated_chart_dump,
        "complete chart numeric dump is deterministic", &failures);
    Chart mixed_registry_chart = chart;
    mixed_registry_chart.flow_stack[0].rule_registry_fingerprint ^=
        UINT64_C(0x1);
    expect(dump_chart_numeric(mixed_registry_chart, &repeated_chart_dump)
            == TAIYIN_ERROR_INVALID_ARGUMENT,
        "chart dump rejects a flow layer from another StarId registry",
        &failures);
    expect(chart_dump.size() == 55u + 2u * rules.registry.size()
            + kFlowLevelCount * (8u + rules.registry.size())
        && chart_dump[0] == kNumericDumpFormatVersion
        && chart_dump[1] == static_cast<uint8_t>(NumericDumpKind::Chart)
        && chart_dump[2] == static_cast<int64_t>(rules.registry.size())
        && chart_dump[3] == static_cast<int64_t>(kFlowLevelCount),
        "chart dump carries a versioned self-describing prefix", &failures);
    expect(dump_resolved_flow_numeric(installed, &flow_dump)
            == TAIYIN_STATUS_OK
        && flow_dump.size() == 50u
        && flow_dump[0] == kNumericDumpFormatVersion
        && flow_dump[1]
            == static_cast<uint8_t>(NumericDumpKind::ResolvedFlow)
        && flow_dump[6]
            == static_cast<int64_t>(to_index(
                installed.target_month_building_branch)),
        "resolved-flow dump has stable fixed-width layout", &failures);
    ResolvedFlow malformed_dump = installed;
    malformed_dump.target_month_sequence = 14u;
    const std::vector<int64_t> preserved_dump = flow_dump;
    expect(dump_resolved_flow_numeric(malformed_dump, &flow_dump)
            == TAIYIN_ERROR_INVALID_ARGUMENT
        && flow_dump == preserved_dump,
        "failed dump validation leaves caller output unchanged", &failures);
    malformed_dump = installed;
    malformed_dump.target_month_building_branch = static_cast<Branch>(12u);
    expect(dump_resolved_flow_numeric(malformed_dump, &flow_dump)
            == TAIYIN_ERROR_INVALID_ARGUMENT
        && flow_dump == preserved_dump,
        "resolved-flow dump validates its month-building branch", &failures);

    FlowResolutionOptions solar_options = lunar_options;
    solar_options.boundary = PillarBoundary::SolarTerm;
    ResolvedFlow solar;
    expect(resolve_flow_from_calendar(
        &calendar,
        birth,
        natal,
        target_instant,
        target_clock,
        solar_options,
        &solar,
        &diagnostic) == TAIYIN_STATUS_OK,
        "resolve solar-term-boundary flow", &failures);
    expect(solar.effective_target_year == 2023
        && solar.target_month == 2u
        && solar.target_month_sequence == 2u
        && !solar.target_month_is_leap
        && solar.target_day == 20u
        && solar.month.limit.coordinate.stem == Stem::Yi
        && solar.month.limit.coordinate.branch == Branch::Xu
        && solar.day.limit.coordinate.stem == Stem::Ren
        && solar.day.limit.coordinate.branch == Branch::Si
        && solar.hour.limit.coordinate.stem == Stem::Yi
        && solar.hour.limit.coordinate.branch == Branch::Xu,
        "solar-term mode uses Jie month/day coordinates", &failures);

    // All three late-Rat conventions must survive the calendar adapter. In
    // particular TOMORROW_GAN keeps today's day pillar while deriving the
    // hour stem from tomorrow; reconstructing it from the day would collapse
    // it into TODAY_GAN.
    const CalendarDateTime late_rat_clock = {2024, 5, 20, 23, 30, 0.0};
    SplitJulianDate late_rat_instant;
    expect(encode_china_standard(late_rat_clock, &late_rat_instant),
        "encode late-Rat target", &failures);
    ResolvedFlow rat_flows[3];
    for (int32_t mode = chinese_calendar::TAIYIN_GANZHI_RAT_HOUR_NO_SPLIT;
         mode <= chinese_calendar::TAIYIN_GANZHI_RAT_HOUR_TOMORROW_GAN;
         ++mode) {
        FlowResolutionOptions rat_options = lunar_options;
        rat_options.rat_hour_mode = mode;
        expect(resolve_flow_from_calendar(
            &calendar,
            birth,
            natal,
            late_rat_instant,
            late_rat_clock,
            rat_options,
            &rat_flows[mode],
            &diagnostic) == TAIYIN_STATUS_OK,
            "resolve late-Rat flow convention", &failures);
    }
    expect(rat_flows[0].target_rat_hour_segment == RatHourSegment::Unified
        && rat_flows[1].target_rat_hour_segment == RatHourSegment::Late
        && rat_flows[2].target_rat_hour_segment == RatHourSegment::Late,
        "late-Rat segment identity follows split policy", &failures);
    expect(rat_flows[1].day.limit.coordinate.stem
            == rat_flows[2].day.limit.coordinate.stem
        && rat_flows[1].hour.limit.coordinate.stem
            != rat_flows[2].hour.limit.coordinate.stem,
        "TOMORROW_GAN preserves today's day but uses tomorrow's hour stem",
        &failures);

    // Navigation uses 13 logical slots for split modes. Hai -> Late Zi ->
    // next-day Early Zi -> Chou must never jump backwards like the old Dart
    // center-snapping implementation did.
    const CalendarDateTime hai_clock = {2024, 5, 20, 22, 30, 0.0};
    SplitJulianDate hai_instant;
    expect(encode_china_standard(hai_clock, &hai_instant),
        "encode Hai navigation target", &failures);
    SplitJulianDate stepped_instant;
    CalendarDateTime stepped_clock;
    RatHourSegment stepped_segment = RatHourSegment::None;
    expect(step_flow_hour_target(
        hai_instant,
        hai_clock,
        chinese_calendar::TAIYIN_GANZHI_RAT_HOUR_TODAY_GAN,
        1,
        &stepped_instant,
        &stepped_clock,
        &stepped_segment) == TAIYIN_STATUS_OK
        && stepped_clock.year == 2024 && stepped_clock.month == 5
        && stepped_clock.day == 20 && stepped_clock.hour == 23
        && stepped_clock.minute == 30
        && stepped_segment == RatHourSegment::Late,
        "split nextHour enters Late Zi", &failures);
    SplitJulianDate early_instant;
    CalendarDateTime early_clock;
    expect(step_flow_hour_target(
        stepped_instant,
        stepped_clock,
        chinese_calendar::TAIYIN_GANZHI_RAT_HOUR_TODAY_GAN,
        1,
        &early_instant,
        &early_clock,
        &stepped_segment) == TAIYIN_STATUS_OK
        && early_clock.year == 2024 && early_clock.month == 5
        && early_clock.day == 21 && early_clock.hour == 0
        && early_clock.minute == 30
        && stepped_segment == RatHourSegment::Early,
        "split nextHour crosses Late Zi to next-day Early Zi", &failures);
    expect(step_flow_hour_target(
        early_instant,
        early_clock,
        chinese_calendar::TAIYIN_GANZHI_RAT_HOUR_TODAY_GAN,
        1,
        &stepped_instant,
        &stepped_clock,
        &stepped_segment) == TAIYIN_STATUS_OK
        && stepped_clock.day == 21 && stepped_clock.hour == 2
        && stepped_clock.minute == 0
        && stepped_segment == RatHourSegment::None,
        "split nextHour advances Early Zi to Chou", &failures);
    expect(step_flow_hour_target(
        stepped_instant,
        stepped_clock,
        chinese_calendar::TAIYIN_GANZHI_RAT_HOUR_TODAY_GAN,
        -1,
        &stepped_instant,
        &stepped_clock,
        &stepped_segment) == TAIYIN_STATUS_OK
        && stepped_clock.day == 21 && stepped_clock.hour == 0
        && stepped_clock.minute == 30
        && stepped_segment == RatHourSegment::Early,
        "previousHour reverses the split transition", &failures);

    const CalendarDateTime no_split_late = {2024, 5, 20, 23, 30, 17.25};
    SplitJulianDate no_split_instant;
    expect(encode_china_standard(no_split_late, &no_split_instant)
        && step_flow_hour_target(
            no_split_instant,
            no_split_late,
            chinese_calendar::TAIYIN_GANZHI_RAT_HOUR_NO_SPLIT,
            1,
            &stepped_instant,
            &stepped_clock,
            &stepped_segment) == TAIYIN_STATUS_OK
        && stepped_clock.day == 21 && stepped_clock.hour == 2
        && stepped_segment == RatHourSegment::None,
        "unified late Zi advances to next-day Chou", &failures);
    expect(step_flow_day_target(
        no_split_instant,
        no_split_late,
        1,
        &stepped_instant,
        &stepped_clock) == TAIYIN_STATUS_OK
        && stepped_clock.day == 21 && stepped_clock.hour == 23
        && stepped_clock.minute == 30
        && stepped_clock.second == 17.25
        && days_between_split_jd(no_split_instant, stepped_instant) == 1.0,
        "nextDay preserves exact wall-clock fields and physical pairing", &failures);

    // A leap month near the end of the lunar year is the difficult case for
    // sequence discovery because the enclosing winter-solstice window starts
    // at month eleven. The adapter must still recover sequence twelve.
    const CalendarDateTime leap_eleven_clock = {2033, 12, 22, 12, 0, 0.0};
    SplitJulianDate leap_eleven_instant;
    expect(encode_china_standard(
        leap_eleven_clock, &leap_eleven_instant),
        "encode 2033 leap-eleven target", &failures);
    ResolvedFlow leap_eleven;
    const Status leap_eleven_status = resolve_flow_from_calendar(
        &calendar,
        birth,
        natal,
        leap_eleven_instant,
        leap_eleven_clock,
        lunar_options,
        &leap_eleven,
        &diagnostic);
    expect(leap_eleven_status == TAIYIN_STATUS_OK
        && leap_eleven.effective_target_year == 2033
        && leap_eleven.target_month == 11u
        && leap_eleven.target_month_sequence == 12u
        && leap_eleven.target_month_is_leap
        && leap_eleven.target_month_building_branch == Branch::Zi
        && leap_eleven.target_day == 1u,
        "resolve late-year leap-month sequence", &failures);

    // calcY owns the continuous month-building sequence.  This must not be
    // reconstructed by selecting a Zhong-Qi in the Ziwei adapter: a lunar
    // month can contain two Zhong-Qi, while the calendar rule is anchored at
    // the winter-solstice month and repeats exactly the selected leap month.
    chinese_calendar::ChineseCalendarYear leap_calendar_year;
    expect(chinese_calendar::calcY(
            &calendar, leap_eleven_instant, &leap_calendar_year, &diagnostic)
            == TAIYIN_STATUS_OK,
        "calculate leap-eleven calendar year", &failures);
    bool continuous_month_building = leap_calendar_year.month_count > 0u
        && leap_calendar_year.months[0].month_building_branch
            == static_cast<uint8_t>(Branch::Zi);
    bool saw_intercalary_month = false;
    for (std::size_t i = 1u;
         i < leap_calendar_year.month_count;
         ++i) {
        const chinese_calendar::ChineseCalendarMonth& previous =
            leap_calendar_year.months[i - 1u];
        const chinese_calendar::ChineseCalendarMonth& current =
            leap_calendar_year.months[i];
        const uint8_t expected_branch = static_cast<uint8_t>(
            (previous.month_building_branch
                + (current.is_leap != 0u ? 0u : 1u)) % 12u);
        continuous_month_building = continuous_month_building
            && current.month_building_branch == expected_branch;
        saw_intercalary_month = saw_intercalary_month
            || current.is_leap != 0u;
    }
    expect(continuous_month_building && saw_intercalary_month,
        "calcY keeps a winter-solstice-anchored branch sequence across leap months",
        &failures);

    // Historical written month labels are not month-building branches.  At
    // the Qin/Han transition this month is labeled from the Zhuanxu calendar
    // convention, while its physical winter-solstice-anchored building branch
    // remains Si (3).
    chinese_calendar::ChineseCalendarYear qin_han_calendar_year;
    expect(chinese_calendar::calcY(
            &calendar,
            SplitJulianDate(INT64_C(1640788), 0.0),
            &qin_han_calendar_year,
            &diagnostic) == TAIYIN_STATUS_OK,
        "calculate Qin/Han transition calendar year", &failures);
    bool found_qin_han_month = false;
    bool qin_han_branch_matches = false;
    for (std::size_t i = 0u;
         i < qin_han_calendar_year.month_count;
         ++i) {
        const chinese_calendar::ChineseCalendarMonth& month =
            qin_han_calendar_year.months[i];
        if (month.first_civil_day_number != INT64_C(1640788)) continue;
        found_qin_han_month = true;
        qin_han_branch_matches = month.month_building_branch == 3u;
        break;
    }
    expect(found_qin_han_month && qin_han_branch_matches,
        "early historical building branch follows physical winter sequence",
        &failures);

    // Historical reform red zones can contain more than thirteen structural
    // month labels. Ziwei keeps those physical dates chartable by collapsing
    // an overflow occurrence onto slot thirteen as leap month twelve.
    const CalendarDateTime ancient_birth_clock = {181, 8, 20, 8, 0, 0.0};
    SplitJulianDate ancient_birth_instant;
    expect(encode_china_standard(
        ancient_birth_clock, &ancient_birth_instant),
        "encode historical birth", &failures);
    ResolvedBirth ancient_birth;
    expect(resolve_birth_from_calendar(
        &calendar,
        ancient_birth_instant,
        ancient_birth_clock,
        Gender::Male,
        birth_options,
        &ancient_birth,
        &diagnostic) == TAIYIN_STATUS_OK,
        "resolve historical birth", &failures);
    NatalChart ancient_natal;
    expect(make_natal_chart(
        ancient_birth.facts,
        ancient_birth.anchors,
        ancient_birth.body_palace,
        birth_options.anchor_options.rules,
        rules.compiled,
        &ancient_natal) == TAIYIN_STATUS_OK,
        "build historical natal chart", &failures);
    const CalendarDateTime ancient_target_clock = {701, 1, 15, 12, 0, 0.0};
    SplitJulianDate ancient_target_instant;
    expect(encode_china_standard(
        ancient_target_clock, &ancient_target_instant),
        "encode historical target", &failures);
    ResolvedFlow ancient_flow;
    const Status ancient_status = resolve_flow_from_calendar(
        &calendar,
        ancient_birth,
        ancient_natal,
        ancient_target_instant,
        ancient_target_clock,
        lunar_options,
        &ancient_flow,
        &diagnostic);
    expect(ancient_status == TAIYIN_STATUS_OK
        && ancient_flow.effective_target_year == 700
        && ancient_flow.target_month == 12u
        && ancient_flow.target_month_sequence == 13u
        && ancient_flow.target_month_is_leap
        && ancient_flow.target_day == 2u,
        "historical reform overflow collapses to leap month twelve",
        &failures);

    const CalendarDateTime reform_birth_clock = {-456, 4, 4, 8, 0, 0.0};
    SplitJulianDate reform_birth_instant;
    expect(encode_china_standard(reform_birth_clock, &reform_birth_instant),
        "encode early historical birth", &failures);
    ResolvedBirth reform_birth;
    expect(resolve_birth_from_calendar(
        &calendar,
        reform_birth_instant,
        reform_birth_clock,
        Gender::Female,
        birth_options,
        &reform_birth,
        &diagnostic) == TAIYIN_STATUS_OK,
        "resolve early historical birth", &failures);
    NatalChart reform_natal;
    expect(make_natal_chart(
        reform_birth.facts,
        reform_birth.anchors,
        reform_birth.body_palace,
        birth_options.anchor_options.rules,
        rules.compiled,
        &reform_natal) == TAIYIN_STATUS_OK,
        "build early historical natal chart", &failures);

    struct FlowBoundaryProbe {
        const char* label;
        CalendarDateTime clock;
        PillarBoundary boundary;
        const ResolvedBirth* birth;
        const NatalChart* natal;
        int32_t year;
        uint8_t month;
        uint8_t sequence;
        bool is_leap;
        uint8_t day;
        uint8_t hour;
        uint8_t coordinates[8];
    };
    const FlowBoundaryProbe boundary_probes[] = {
        {"before Li Chun", {2024, 2, 4, 16, 0, 0.0},
            PillarBoundary::SolarTerm, &birth, &natal,
            2023, 12u, 12u, false, 30u, 8u,
            {9u, 3u, 1u, 8u, 4u, 1u, 6u, 9u}},
        {"after Li Chun", {2024, 2, 4, 16, 40, 0.0},
            PillarBoundary::SolarTerm, &birth, &natal,
            2024, 1u, 1u, false, 1u, 8u,
            {0u, 4u, 2u, 10u, 4u, 10u, 6u, 6u}},
        {"before lunar new year", {2024, 2, 9, 22, 0, 0.0},
            PillarBoundary::Lunar, &birth, &natal,
            2023, 12u, 13u, false, 30u, 11u,
            {9u, 3u, 1u, 9u, 9u, 2u, 9u, 1u}},
        {"after lunar new year", {2024, 2, 10, 1, 0, 0.0},
            PillarBoundary::Lunar, &birth, &natal,
            2024, 1u, 1u, false, 1u, 1u,
            {0u, 4u, 2u, 10u, 0u, 10u, 1u, 11u}},
        {"pre-Taichu", {-104, 1, 3, 12, 0, 0.0},
            PillarBoundary::Lunar, &reform_birth, &reform_natal,
            -105, 11u, 2u, false, 27u, 6u,
            {1u, 11u, 4u, 0u, 3u, 2u, 2u, 8u}},
        {"Taichu", {-103, 1, 20, 12, 0, 0.0},
            PillarBoundary::Lunar, &reform_birth, &reform_natal,
            -104, 11u, 2u, false, 27u, 6u,
            {2u, 0u, 6u, 1u, 6u, 3u, 8u, 9u}},
        {"Xin alternate twelve", {23, 12, 2, 12, 0, 0.0},
            PillarBoundary::Lunar, &reform_birth, &reform_natal,
            23, 12u, 12u, false, 1u, 6u,
            {9u, 7u, 0u, 6u, 3u, 6u, 2u, 0u}},
        {"Xin ordinary twelve", {24, 1, 12, 12, 0, 0.0},
            PillarBoundary::Lunar, &reform_birth, &reform_natal,
            23, 12u, 13u, false, 13u, 6u,
            {9u, 7u, 1u, 7u, 4u, 7u, 4u, 1u}},
        {"Jingchu 28-day month end", {237, 4, 11, 12, 0, 0.0},
            PillarBoundary::Lunar, &ancient_birth, &ancient_natal,
            237, 2u, 2u, false, 28u, 6u,
            {3u, 5u, 9u, 4u, 2u, 7u, 0u, 1u}},
        {"Jingchu next month", {237, 4, 12, 12, 0, 0.0},
            PillarBoundary::Lunar, &ancient_birth, &ancient_natal,
            237, 4u, 3u, false, 1u, 6u,
            {3u, 5u, 0u, 5u, 3u, 5u, 2u, 11u}},
        {"Wu Zetian first month", {689, 12, 18, 12, 0, 0.0},
            PillarBoundary::Lunar, &ancient_birth, &ancient_natal,
            690, 1u, 1u, false, 1u, 6u,
            {6u, 2u, 4u, 0u, 6u, 0u, 8u, 6u}},
        {"Wu Zetian alternate one", {690, 2, 15, 12, 0, 0.0},
            PillarBoundary::Lunar, &ancient_birth, &ancient_natal,
            690, 1u, 3u, false, 1u, 6u,
            {6u, 2u, 4u, 2u, 5u, 2u, 6u, 8u}},
        {"Tang renamed first month", {761, 12, 2, 12, 0, 0.0},
            PillarBoundary::Lunar, &ancient_birth, &ancient_natal,
            762, 1u, 1u, false, 1u, 6u,
            {8u, 2u, 8u, 0u, 8u, 0u, 2u, 6u}},
        {"Tang reform end", {762, 3, 30, 12, 0, 0.0},
            PillarBoundary::Lunar, &ancient_birth, &ancient_natal,
            762, 5u, 5u, false, 1u, 6u,
            {8u, 2u, 0u, 4u, 6u, 4u, 8u, 10u}},
    };
    for (std::size_t i = 0u;
         i < sizeof(boundary_probes) / sizeof(boundary_probes[0]); ++i) {
        SplitJulianDate probe_instant;
        expect(encode_china_standard(
            boundary_probes[i].clock, &probe_instant),
            "encode flow-boundary probe", &failures);
        FlowResolutionOptions probe_options = lunar_options;
        probe_options.boundary = boundary_probes[i].boundary;
        ResolvedFlow probe_flow;
        Chart probe_chart;
        probe_chart.natal = *boundary_probes[i].natal;
        const Status probe_status = set_flow_stack_from_calendar(
            &calendar,
            *boundary_probes[i].birth,
            probe_instant,
            boundary_probes[i].clock,
            probe_options,
            rules.compiled,
            &probe_chart,
            &probe_flow,
            &diagnostic);
        bool fields_match = probe_status == TAIYIN_STATUS_OK;
        if (fields_match) {
            const FlowCoordinate coordinates[4] = {
                probe_flow.year.limit.coordinate,
                probe_flow.month.limit.coordinate,
                probe_flow.day.limit.coordinate,
                probe_flow.hour.limit.coordinate,
            };
            fields_match = probe_flow.effective_target_year
                    == boundary_probes[i].year
                && probe_flow.target_month == boundary_probes[i].month
                && probe_flow.target_month_sequence
                    == boundary_probes[i].sequence
                && probe_flow.target_month_is_leap
                    == boundary_probes[i].is_leap
                && probe_flow.target_day == boundary_probes[i].day
                && probe_flow.target_hour_index == boundary_probes[i].hour;
            for (std::size_t level = 0u; level < 4u; ++level) {
                fields_match = fields_match
                    && to_index(coordinates[level].stem)
                        == boundary_probes[i].coordinates[level * 2u]
                    && to_index(coordinates[level].branch)
                        == boundary_probes[i].coordinates[level * 2u + 1u];
            }
        }
        if (!fields_match) {
            std::cerr << "flow-boundary mismatch: "
                      << boundary_probes[i].label;
            if (probe_status == TAIYIN_STATUS_OK) {
                std::cerr << " actual="
                          << probe_flow.effective_target_year << ','
                          << static_cast<int>(probe_flow.target_month) << ','
                          << static_cast<int>(
                              probe_flow.target_month_sequence) << ','
                          << probe_flow.target_month_is_leap << ','
                          << static_cast<int>(probe_flow.target_day) << ','
                          << static_cast<int>(probe_flow.target_hour_index)
                          << " coords="
                          << static_cast<int>(to_index(
                              probe_flow.year.limit.coordinate.stem)) << ':'
                          << static_cast<int>(to_index(
                              probe_flow.year.limit.coordinate.branch)) << '/'
                          << static_cast<int>(to_index(
                              probe_flow.month.limit.coordinate.stem)) << ':'
                          << static_cast<int>(to_index(
                              probe_flow.month.limit.coordinate.branch)) << '/'
                          << static_cast<int>(to_index(
                              probe_flow.day.limit.coordinate.stem)) << ':'
                          << static_cast<int>(to_index(
                              probe_flow.day.limit.coordinate.branch)) << '/'
                          << static_cast<int>(to_index(
                              probe_flow.hour.limit.coordinate.stem)) << ':'
                          << static_cast<int>(to_index(
                              probe_flow.hour.limit.coordinate.branch));
            } else {
                std::cerr << " status=" << probe_status;
            }
            std::cerr << '\n';
        }
        expect(fields_match,
            "boundary flow coordinates match the locked regression record",
            &failures);

        bool layers_complete = probe_status == TAIYIN_STATUS_OK
            && probe_chart.flow_stack.size() == kFlowLevelCount;
        if (layers_complete) {
            for (std::size_t level = 0u;
                 level < probe_chart.flow_stack.size(); ++level) {
                std::size_t star_count = 0u;
                for (std::size_t branch = 0u;
                     branch < kBranchCount; ++branch) {
                    star_count +=
                        probe_chart.flow_stack[level].stars[branch].count();
                }
                layers_complete = layers_complete && star_count == 44u;
            }
        }
        if (!layers_complete) {
            std::cerr << "incomplete boundary flow chart: "
                      << boundary_probes[i].label << '\n';
        }
        expect(layers_complete,
            "boundary flow chart keeps five complete 44-star layers",
            &failures);
    }

    // In historical reform eras, the written lunar month name is not a safe
    // proxy for its month-building branch.  These records lock the separate
    // Zhong-Qi/civil-day result and the Wu-Hu-Dun stem derived from the
    // *labelled lunar year's* stem.  In particular, Xin's alternate
    // "twelfth month" is Jian-Zi, hence Gui-year Jia-Zi, rather than a
    // month stem inferred from the written "twelve" label.
    struct HistoricalMonthBuildingProbe {
        const char* label;
        CalendarDateTime clock;
        const ResolvedBirth* birth;
        const NatalChart* natal;
        int32_t lunar_year;
        Branch month_building_branch;
        Stem month_stem;
    };
    const HistoricalMonthBuildingProbe historical_month_building_probes[] = {
        {"pre-Taichu Jian-Zi", {-104, 1, 3, 12, 0, 0.0},
            &reform_birth, &reform_natal, -105, Branch::Zi, Stem::Wu},
        {"Taichu Jian-Zi", {-103, 1, 20, 12, 0, 0.0},
            &reform_birth, &reform_natal, -104, Branch::Zi, Stem::Geng},
        {"Xin alternate twelve is Jian-Zi", {23, 12, 2, 12, 0, 0.0},
            &reform_birth, &reform_natal, 23, Branch::Zi, Stem::Jia},
        {"Xin ordinary twelve is Jian-Chou", {24, 1, 12, 12, 0, 0.0},
            &reform_birth, &reform_natal, 23, Branch::Chou, Stem::Yi},
        {"Wu Zhou alternate one is Jian-Yin", {690, 2, 15, 12, 0, 0.0},
            &ancient_birth, &ancient_natal, 690, Branch::Yin, Stem::Wu},
        {"Tang reform month is Jian-Chen", {762, 3, 30, 12, 0, 0.0},
            &ancient_birth, &ancient_natal, 762, Branch::Chen, Stem::Jia},
    };
    for (std::size_t i = 0u;
         i < sizeof(historical_month_building_probes)
                 / sizeof(historical_month_building_probes[0]);
         ++i) {
        const HistoricalMonthBuildingProbe& probe =
            historical_month_building_probes[i];
        SplitJulianDate probe_instant;
        ResolvedFlow probe_flow;
        const Status status = encode_china_standard(probe.clock, &probe_instant)
                ? resolve_flow_from_calendar(
                    &calendar,
                    *probe.birth,
                    *probe.natal,
                    probe_instant,
                    probe.clock,
                    lunar_options,
                    &probe_flow,
                    &diagnostic)
                : TAIYIN_ERROR_INVALID_ARGUMENT;
        const bool matches = status == TAIYIN_STATUS_OK
                && probe_flow.effective_target_year == probe.lunar_year
                && probe_flow.target_month_building_branch
                    == probe.month_building_branch
                && probe_flow.month.limit.coordinate.stem == probe.month_stem;
        if (!matches && status == TAIYIN_STATUS_OK) {
            std::cerr << "historical month-building mismatch: "
                      << probe.label << " actual year="
                      << probe_flow.effective_target_year << " branch="
                      << static_cast<int>(to_index(
                          probe_flow.target_month_building_branch))
                      << " stem=" << static_cast<int>(to_index(
                          probe_flow.month.limit.coordinate.stem)) << '\n';
        }
        expect(matches, probe.label, &failures);
    }

    // Mismatched birth/natal inputs must fail before changing an existing
    // stack. This guards bindings from accidentally mixing two users' charts.
    const std::size_t existing_size = chart.flow_stack.size();
    ResolvedBirth mismatched = birth;
    mismatched.facts.birth.gender = Gender::Male;
    expect(set_flow_stack_from_calendar(
        &calendar,
        mismatched,
        target_instant,
        target_clock,
        lunar_options,
        rules.compiled,
        &chart,
        NULL,
        &diagnostic) == TAIYIN_ERROR_INVALID_ARGUMENT
        && chart.flow_stack.size() == existing_size,
        "mismatch fails atomically", &failures);

    const CalendarDateTime before_birth_clock = {2002, 3, 13, 14, 15, 0.0};
    SplitJulianDate before_birth;
    expect(encode_china_standard(before_birth_clock, &before_birth),
        "encode before-birth instant", &failures);
    expect(resolve_flow_from_calendar(
        &calendar,
        birth,
        natal,
        before_birth,
        before_birth_clock,
        lunar_options,
        &lunar,
        &diagnostic) == TAIYIN_ERROR_INVALID_ARGUMENT,
        "flow target before birth is rejected", &failures);

    if (failures != 0) {
        std::cerr << failures << " Ziwei flow-calendar adapter checks failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "Ziwei flow-calendar adapter checks passed\n";
    return EXIT_SUCCESS;
}
