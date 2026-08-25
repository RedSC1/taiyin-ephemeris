#include "taiyin/angle.h"
#include "taiyin/body_id.h"
#include "taiyin/chinese_calendar/calendar.h"
#include "taiyin/runtime/ephemeris_route.h"
#include "taiyin/runtime/native_context.h"
#include "taiyin/runtime/runtime.h"
#include "taiyin/runtime/solar_time.h"
#include "taiyin/time.h"
#include "taiyin/ziwei/flow_calendar_adapter.h"
#include "taiyin/ziwei/debug_dump.h"
#include "taiyin/ziwei/rules_loader.h"

#include <cmath>
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

bool make_solar_clock_target(
    const taiyin::runtime::NativeCalcContext& astronomy,
    const taiyin::CalendarDateTime& desired_virtual_time,
    double longitude_rad,
    bool apparent,
    taiyin::SplitJulianDate* out_instant_ut,
    taiyin::CalendarDateTime* out_virtual_time,
    taiyin::runtime::EphemerisEvalDiagnostic* diagnostic
) {
    taiyin::SplitJulianDate virtual_jd;
    taiyin::SplitJulianDate local_mean_jd;
    if (!out_instant_ut || !out_virtual_time
        || !taiyin::julian_day_split(desired_virtual_time, &virtual_jd)) {
        return false;
    }
    if (apparent) {
        const taiyin::Status status =
            taiyin::runtime::local_apparent_to_mean_solar_time(
                &astronomy,
                virtual_jd,
                longitude_rad,
                &local_mean_jd,
                diagnostic);
        if (status != taiyin::TAIYIN_STATUS_OK) {
            std::cerr << "local apparent to mean failed: " << status << '\n';
            return false;
        }
    } else {
        local_mean_jd = virtual_jd;
    }
    if (!taiyin::add_days_to_split_jd(
            local_mean_jd,
            -longitude_rad / taiyin::TAIYIN_TWO_PI,
            out_instant_ut)) {
        return false;
    }
    return taiyin::reverse_julian_day_split(virtual_jd, out_virtual_time);
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

    // A precise Li-Chun crossing changes the solar year/month immediately at
    // its physical UTC instant. The solar-day index then advances only at the
    // selected Ziwei logical-day boundary: 23:00 for unified Rat hours, or
    // civil midnight for either split-Rat convention.
    chinese_calendar::SolarTermEvent li_chun_2024;
    expect(chinese_calendar::getSpecificJieQi(
            &calendar, 2024, 21u, &li_chun_2024, &late_rat_diagnostic)
            == TAIYIN_STATUS_OK,
        "resolve the 2024 Li-Chun instant", &failures);
    SplitJulianDate li_chun_before_instant;
    SplitJulianDate li_chun_after_instant;
    CalendarDateTime li_chun_before_clock;
    CalendarDateTime li_chun_after_clock;
    expect(add_seconds_to_split_jd(
            li_chun_2024.jd_ut, -60.0, &li_chun_before_instant)
        && add_seconds_to_split_jd(
            li_chun_2024.jd_ut, 60.0, &li_chun_after_instant)
        && reverse_julian_day_split(
            li_chun_before_instant + 8.0 / 24.0,
            &li_chun_before_clock)
        && reverse_julian_day_split(
            li_chun_after_instant + 8.0 / 24.0,
            &li_chun_after_clock),
        "decode clocks around the 2024 Li-Chun instant", &failures);
    const BirthResolutionOptions precise_jie_options =
        default_birth_resolution_options();
    ResolvedBirth li_chun_before_birth;
    ResolvedBirth li_chun_after_birth;
    expect(resolve_birth_from_calendar(
            &calendar,
            li_chun_before_instant,
            li_chun_before_clock,
            Gender::Male,
            precise_jie_options,
            &li_chun_before_birth,
            &late_rat_diagnostic) == TAIYIN_STATUS_OK
        && resolve_birth_from_calendar(
            &calendar,
            li_chun_after_instant,
            li_chun_after_clock,
            Gender::Male,
            precise_jie_options,
            &li_chun_after_birth,
            &late_rat_diagnostic) == TAIYIN_STATUS_OK
        && (li_chun_before_birth.facts.solar_term_pillars.year.stem
                != li_chun_after_birth.facts.solar_term_pillars.year.stem
            || li_chun_before_birth.facts.solar_term_pillars.year.branch
                != li_chun_after_birth.facts.solar_term_pillars.year.branch)
        && (li_chun_before_birth.facts.solar_term_pillars.month.stem
                != li_chun_after_birth.facts.solar_term_pillars.month.stem
            || li_chun_before_birth.facts.solar_term_pillars.month.branch
                != li_chun_after_birth.facts.solar_term_pillars.month.branch)
        && li_chun_before_birth.facts.solar_day_from_previous_jie > 1u
        && li_chun_after_birth.facts.solar_day_from_previous_jie == 1u,
        "physical Li-Chun instant switches the natal solar boundary",
        &failures);

    CalendarDateTime li_chun_2230 = li_chun_after_clock;
    li_chun_2230.hour = 22;
    li_chun_2230.minute = 30;
    li_chun_2230.second = 0.0;
    CalendarDateTime li_chun_2330 = li_chun_2230;
    li_chun_2330.hour = 23;
    SplitJulianDate li_chun_2230_instant;
    SplitJulianDate li_chun_2330_instant;
    ResolvedBirth li_chun_2230_birth;
    ResolvedBirth li_chun_2330_unified_birth;
    BirthResolutionOptions split_jie_options = precise_jie_options;
    split_jie_options.rat_hour_mode =
        chinese_calendar::TAIYIN_GANZHI_RAT_HOUR_TODAY_GAN;
    ResolvedBirth li_chun_2330_split_birth;
    expect(li_chun_after_clock.hour < 22
        && encode_china_standard(li_chun_2230, &li_chun_2230_instant)
        && encode_china_standard(li_chun_2330, &li_chun_2330_instant)
        && resolve_birth_from_calendar(
            &calendar,
            li_chun_2230_instant,
            li_chun_2230,
            Gender::Male,
            precise_jie_options,
            &li_chun_2230_birth,
            &late_rat_diagnostic) == TAIYIN_STATUS_OK
        && resolve_birth_from_calendar(
            &calendar,
            li_chun_2330_instant,
            li_chun_2330,
            Gender::Male,
            precise_jie_options,
            &li_chun_2330_unified_birth,
            &late_rat_diagnostic) == TAIYIN_STATUS_OK
        && resolve_birth_from_calendar(
            &calendar,
            li_chun_2330_instant,
            li_chun_2330,
            Gender::Male,
            split_jie_options,
            &li_chun_2330_split_birth,
            &late_rat_diagnostic) == TAIYIN_STATUS_OK
        && li_chun_2230_birth.facts.solar_day_from_previous_jie == 1u
        && li_chun_2330_unified_birth.facts.solar_day_from_previous_jie == 2u
        && li_chun_2330_split_birth.facts.solar_day_from_previous_jie == 1u,
        "natal solar day follows the selected Ziwei day boundary", &failures);
    CalendarDateTime li_chun_next_0030;
    SplitJulianDate li_chun_next_0030_local;
    SplitJulianDate li_chun_next_0030_instant;
    ResolvedBirth li_chun_next_0030_split_birth;
    expect(julian_day_split(li_chun_2330, &li_chun_next_0030_local)
        && add_seconds_to_split_jd(
            li_chun_next_0030_local, 3600.0, &li_chun_next_0030_local)
        && reverse_julian_day_split(
            li_chun_next_0030_local, &li_chun_next_0030)
        && encode_china_standard(
            li_chun_next_0030, &li_chun_next_0030_instant)
        && resolve_birth_from_calendar(
            &calendar,
            li_chun_next_0030_instant,
            li_chun_next_0030,
            Gender::Male,
            split_jie_options,
            &li_chun_next_0030_split_birth,
            &late_rat_diagnostic) == TAIYIN_STATUS_OK
        && li_chun_next_0030_split_birth.facts.solar_day_from_previous_jie
            == 2u,
        "split-Rat natal solar day advances at civil midnight", &failures);

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

    // Mean/apparent solar clocks are the virtual clocks used by Ganzhi and
    // Ziwei. A JD round trip at an exact Shi-Chen boundary must enter the new
    // branch, while a real millisecond before it must remain in the old one.
    const double solar_clock_longitude = deg_to_rad(118.5);
    for (int apparent_index = 0; apparent_index < 2; ++apparent_index) {
        const bool apparent = apparent_index != 0;
        const CalendarDateTime exact_birth_clock = {
            2003, 3, 13, 11, 0, 0.0};
        SplitJulianDate exact_birth_virtual_jd;
        expect(julian_day_split(
            exact_birth_clock, &exact_birth_virtual_jd),
            "encode exact solar-clock birth boundary", &failures);
        SplitJulianDate before_birth_virtual_jd;
        expect(add_seconds_to_split_jd(
            exact_birth_virtual_jd, -0.001, &before_birth_virtual_jd),
            "encode pre-boundary solar-clock birth", &failures);
        CalendarDateTime before_birth_clock;
        expect(reverse_julian_day_split(
            before_birth_virtual_jd, &before_birth_clock),
            "decode pre-boundary solar clock", &failures);

        const CalendarDateTime solar_birth_clocks[] = {
            before_birth_clock,
            exact_birth_clock,
        };
        const uint8_t expected_birth_hour_branches[] = {5u, 6u};
        for (std::size_t boundary = 0u; boundary < 2u; ++boundary) {
            SplitJulianDate solar_birth_instant;
            CalendarDateTime solar_birth_virtual;
            expect(make_solar_clock_target(
                    astronomy,
                    solar_birth_clocks[boundary],
                    solar_clock_longitude,
                    apparent,
                    &solar_birth_instant,
                    &solar_birth_virtual,
                    &diagnostic),
                "construct mean/apparent solar birth target", &failures);
            ResolvedBirth solar_birth;
            NatalChart solar_natal;
            expect(resolve_birth_from_calendar(
                    &calendar,
                    solar_birth_instant,
                    solar_birth_virtual,
                    Gender::Female,
                    birth_options,
                    &solar_birth,
                    &diagnostic) == TAIYIN_STATUS_OK
                && make_natal_chart(
                    solar_birth.facts,
                    solar_birth.anchors,
                    solar_birth.body_palace,
                    birth_options.anchor_options.rules,
                    rules.compiled,
                    &solar_natal) == TAIYIN_STATUS_OK
                && to_index(solar_birth.facts.solar_term_pillars.hour.branch)
                    == expected_birth_hour_branches[boundary]
                && to_index(solar_birth.facts.lunar_pillars.hour.branch)
                    == expected_birth_hour_branches[boundary]
                && to_index(
                    solar_natal.birth_facts.solar_term_pillars.hour.branch)
                    == expected_birth_hour_branches[boundary],
                "solar-clock Ziwei natal uses the corrected virtual hour",
                &failures);
        }

        const CalendarDateTime exact_flow_clock = {
            2003, 4, 8, 11, 0, 0.0};
        SplitJulianDate exact_flow_virtual_jd;
        SplitJulianDate before_flow_virtual_jd;
        CalendarDateTime before_flow_clock;
        expect(julian_day_split(exact_flow_clock, &exact_flow_virtual_jd)
            && add_seconds_to_split_jd(
                exact_flow_virtual_jd, -0.001, &before_flow_virtual_jd)
            && reverse_julian_day_split(
                before_flow_virtual_jd, &before_flow_clock),
            "construct solar-clock flow boundaries", &failures);
        const CalendarDateTime solar_flow_clocks[] = {
            before_flow_clock,
            exact_flow_clock,
        };
        const uint8_t expected_flow_hour_branches[] = {5u, 6u};
        for (std::size_t boundary = 0u; boundary < 2u; ++boundary) {
            SplitJulianDate solar_flow_instant;
            CalendarDateTime solar_flow_virtual;
            ResolvedFlow solar_flow;
            const bool made_target = make_solar_clock_target(
                    astronomy,
                    solar_flow_clocks[boundary],
                    solar_clock_longitude,
                    apparent,
                    &solar_flow_instant,
                    &solar_flow_virtual,
                    &diagnostic);
            const Status flow_status = made_target
                ? resolve_flow_from_calendar(
                    &calendar,
                    birth,
                    natal,
                    solar_flow_instant,
                    solar_flow_virtual,
                    default_flow_resolution_options(),
                    &solar_flow,
                    &diagnostic)
                : TAIYIN_ERROR_INVALID_ARGUMENT;
            if (flow_status == TAIYIN_STATUS_OK
                && solar_flow.target_hour_index
                    != expected_flow_hour_branches[boundary]) {
                std::cerr << "solar flow hour mismatch apparent="
                          << apparent << " boundary=" << boundary
                          << " clock=" << solar_flow_virtual.hour << ':'
                          << solar_flow_virtual.minute << ':'
                          << solar_flow_virtual.second
                          << " actual="
                          << static_cast<int>(solar_flow.target_hour_index)
                          << " expected="
                          << static_cast<int>(
                              expected_flow_hour_branches[boundary])
                          << '\n';
            }
            if (!made_target || flow_status != TAIYIN_STATUS_OK) {
                std::cerr << "solar flow resolution failed apparent="
                          << apparent << " boundary=" << boundary
                          << " made=" << made_target
                          << " status=" << flow_status << '\n';
            }
            expect(made_target
                && flow_status == TAIYIN_STATUS_OK
                && solar_flow.target_hour_index
                    == expected_flow_hour_branches[boundary],
                "solar-clock Ziwei flow uses the corrected virtual hour",
                &failures);
        }
    }

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
        && flow_dump.size() == 54u
        && flow_dump[0] == kNumericDumpFormatVersion
        && flow_dump[1]
            == static_cast<uint8_t>(NumericDumpKind::ResolvedFlow)
        && flow_dump[6]
            == static_cast<int64_t>(to_index(
                installed.target_month_building_branch)),
        "resolved-flow dump has stable fixed-width layout", &failures);
    ResolvedFlow malformed_dump = installed;
    malformed_dump.target_month_sequence = 16u;
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

    ResolvedFlow li_chun_before_flow;
    ResolvedFlow li_chun_after_flow;
    ResolvedFlow li_chun_2330_unified_flow;
    ResolvedFlow li_chun_2330_split_flow;
    ResolvedFlow li_chun_next_0030_split_flow;
    FlowResolutionOptions solar_split_options = solar_options;
    solar_split_options.rat_hour_mode =
        chinese_calendar::TAIYIN_GANZHI_RAT_HOUR_TODAY_GAN;
    expect(resolve_flow_from_calendar(
            &calendar, birth, natal,
            li_chun_before_instant, li_chun_before_clock,
            solar_options, &li_chun_before_flow, &diagnostic)
            == TAIYIN_STATUS_OK
        && resolve_flow_from_calendar(
            &calendar, birth, natal,
            li_chun_after_instant, li_chun_after_clock,
            solar_options, &li_chun_after_flow, &diagnostic)
            == TAIYIN_STATUS_OK
        && li_chun_before_flow.effective_target_year == 2023
        && li_chun_before_flow.target_month == 12u
        && li_chun_before_flow.target_day > 1u
        && li_chun_after_flow.effective_target_year == 2024
        && li_chun_after_flow.target_month == 1u
        && li_chun_after_flow.target_day == 1u,
        "physical Li-Chun instant switches the solar flow boundary",
        &failures);
    expect(resolve_flow_from_calendar(
            &calendar, birth, natal,
            li_chun_2330_instant, li_chun_2330,
            solar_options, &li_chun_2330_unified_flow, &diagnostic)
            == TAIYIN_STATUS_OK
        && resolve_flow_from_calendar(
            &calendar, birth, natal,
            li_chun_2330_instant, li_chun_2330,
            solar_split_options, &li_chun_2330_split_flow, &diagnostic)
            == TAIYIN_STATUS_OK
        && resolve_flow_from_calendar(
            &calendar, birth, natal,
            li_chun_next_0030_instant, li_chun_next_0030,
            solar_split_options, &li_chun_next_0030_split_flow, &diagnostic)
            == TAIYIN_STATUS_OK
        && li_chun_2330_unified_flow.target_day == 2u
        && li_chun_2330_split_flow.target_day == 1u
        && li_chun_next_0030_split_flow.target_day == 2u,
        "solar flow day follows the selected Ziwei day boundary", &failures);

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
    expect(rat_flows[0].target_day
            == static_cast<uint8_t>(rat_flows[1].target_day + 1u)
        && rat_flows[1].target_day == rat_flows[2].target_day,
        "unified late Rat advances the lunar flow day", &failures);
    expect(rat_flows[1].day.limit.coordinate.stem
            == rat_flows[2].day.limit.coordinate.stem
        && rat_flows[1].hour.limit.coordinate.stem
            != rat_flows[2].hour.limit.coordinate.stem,
        "TOMORROW_GAN preserves today's day but uses tomorrow's hour stem",
        &failures);

    // The logical-day shift must carry the full lunar identity and metadata
    // across a month/year boundary, not merely increment target_day in place.
    const CalendarDateTime new_year_rat_clock = {2024, 2, 9, 23, 30, 0.0};
    SplitJulianDate new_year_rat_instant;
    ResolvedFlow unified_new_year;
    expect(encode_china_standard(
            new_year_rat_clock, &new_year_rat_instant)
        && resolve_flow_from_calendar(
            &calendar,
            birth,
            natal,
            new_year_rat_instant,
            new_year_rat_clock,
            lunar_options,
            &unified_new_year,
            &diagnostic) == TAIYIN_STATUS_OK
        && unified_new_year.effective_target_year == 2024
        && unified_new_year.target_month == 1u
        && unified_new_year.target_month_sequence == 1u
        && unified_new_year.target_day == 1u
        && !unified_new_year.target_month_is_leap
        && unified_new_year.target_month_building_branch == Branch::Yin,
        "unified late Rat crosses the lunar new-year boundary", &failures);
    FlowResolutionOptions split_new_year_options = lunar_options;
    split_new_year_options.rat_hour_mode =
        chinese_calendar::TAIYIN_GANZHI_RAT_HOUR_TODAY_GAN;
    ResolvedFlow split_new_year;
    expect(resolve_flow_from_calendar(
            &calendar,
            birth,
            natal,
            new_year_rat_instant,
            new_year_rat_clock,
            split_new_year_options,
            &split_new_year,
            &diagnostic) == TAIYIN_STATUS_OK
        && split_new_year.effective_target_year == 2023
        && split_new_year.target_month == 12u
        && split_new_year.target_day == 30u,
        "split late Rat retains the physical lunar date", &failures);

    // Navigation uses 13 logical slots for split modes. Hai -> Late Zi ->
    // next-day Early Zi -> Chou uses one-hour steps while preserving the
    // position inside the hour; all other transitions use two hours.
    const CalendarDateTime exact_hai_clock = {2024, 5, 20, 22, 0, 0.0};
    SplitJulianDate exact_hai_instant;
    expect(encode_china_standard(exact_hai_clock, &exact_hai_instant),
        "encode exact Hai navigation target", &failures);
    SplitJulianDate exact_late_instant;
    CalendarDateTime exact_late_clock;
    RatHourSegment exact_late_segment = RatHourSegment::None;
    for (int32_t split_mode =
             chinese_calendar::TAIYIN_GANZHI_RAT_HOUR_TODAY_GAN;
         split_mode <= chinese_calendar::TAIYIN_GANZHI_RAT_HOUR_TOMORROW_GAN;
         ++split_mode) {
        expect(step_flow_hour_target(
            exact_hai_instant,
            exact_hai_clock,
            split_mode,
            1,
            &exact_late_instant,
            &exact_late_clock,
            &exact_late_segment) == TAIYIN_STATUS_OK
            && exact_late_clock.year == 2024
            && exact_late_clock.month == 5
            && exact_late_clock.day == 20
            && exact_late_clock.hour == 23
            && exact_late_clock.minute == 0
            && exact_late_clock.second == 0.0
            && std::fabs(seconds_between_split_jd(
                exact_hai_instant, exact_late_instant) - 3600.0) < 1.0e-9
            && exact_late_segment == RatHourSegment::Late,
            "both split modes step exact 22:00 to 23:00", &failures);
    }

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
        && stepped_clock.day == 21 && stepped_clock.hour == 1
        && stepped_clock.minute == 30
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
        && stepped_clock.day == 21 && stepped_clock.hour == 1
        && stepped_clock.minute == 30
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

    const CalendarDateTime late_leap_eleven_clock = {
        2034, 1, 6, 12, 0, 0.0,
    };
    SplitJulianDate late_leap_eleven_instant;
    ResolvedFlow late_leap_eleven;
    expect(encode_china_standard(
            late_leap_eleven_clock, &late_leap_eleven_instant)
        && resolve_flow_from_calendar(
            &calendar, birth, natal,
            late_leap_eleven_instant, late_leap_eleven_clock,
            lunar_options, &late_leap_eleven, &diagnostic)
            == TAIYIN_STATUS_OK
        && late_leap_eleven.target_month == 11u
        && late_leap_eleven.target_month_sequence == 12u
        && late_leap_eleven.target_month_is_leap
        && late_leap_eleven.target_day == 16u
        && late_leap_eleven.month.effective_year == 2033
        && late_leap_eleven.month.effective_month == 12u
        && late_leap_eleven.month.palace_month_index == 12u
        && late_leap_eleven.month.limit.coordinate.branch
            == leap_eleven.month.limit.coordinate.branch
        && late_leap_eleven.month.limit.coordinate.stem
            != leap_eleven.month.limit.coordinate.stem,
        "split leap-eleven changes effective month without moving its palace",
        &failures);

    FlowResolutionOptions effective_palace_options = lunar_options;
    effective_palace_options.flow_month_palace_strategy =
        FlowMonthPalaceStrategy::EffectiveMonth;
    ResolvedFlow effective_palace_leap_eleven;
    expect(resolve_flow_from_calendar(
            &calendar, birth, natal,
            leap_eleven_instant, leap_eleven_clock,
            effective_palace_options,
            &effective_palace_leap_eleven, &diagnostic)
            == TAIYIN_STATUS_OK
        && effective_palace_leap_eleven.month.palace_month_index == 11u
        && effective_palace_leap_eleven.month.limit.coordinate.branch
            != leap_eleven.month.limit.coordinate.branch,
        "flow-month palace strategy can follow the effective month",
        &failures);

    const CalendarDateTime normal_twelve_clock = {2034, 1, 20, 12, 0, 0.0};
    SplitJulianDate normal_twelve_instant;
    ResolvedFlow normal_twelve;
    expect(encode_china_standard(normal_twelve_clock, &normal_twelve_instant)
        && resolve_flow_from_calendar(
            &calendar, birth, natal,
            normal_twelve_instant, normal_twelve_clock,
            lunar_options, &normal_twelve, &diagnostic)
            == TAIYIN_STATUS_OK
        && normal_twelve.target_month == 12u
        && normal_twelve.target_month_sequence == 13u
        && !normal_twelve.target_month_is_leap
        && normal_twelve.month.effective_month == 12u,
        "normal month after leap-eleven retains sequence thirteen without a leap flag",
        &failures);

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
    // months in one actual calendar year. Preserve that physical sequence;
    // do not invent a synthetic leap-twelve label merely to cap it at 13.
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
        && ancient_flow.target_month_sequence == 15u
        && !ancient_flow.target_month_is_leap
        && ancient_flow.target_day == 2u,
        "historical reform preserves the fifteenth physical month",
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
            -104, 11u, 2u, false, 27u, 6u,
            {2u, 0u, 6u, 1u, 3u, 3u, 2u, 9u}},
        {"Taichu", {-103, 1, 20, 12, 0, 0.0},
            PillarBoundary::Lunar, &reform_birth, &reform_natal,
            -103, 11u, 2u, false, 27u, 6u,
            {3u, 1u, 8u, 2u, 6u, 4u, 8u, 10u}},
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
            &reform_birth, &reform_natal, -104, Branch::Zi, Stem::Geng},
        {"Taichu Jian-Zi", {-103, 1, 20, 12, 0, 0.0},
            &reform_birth, &reform_natal, -103, Branch::Zi, Stem::Ren},
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
