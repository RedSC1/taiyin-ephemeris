#include "taiyin/body_id.h"
#include "taiyin/chinese_calendar/calendar.h"
#include "taiyin/runtime/ephemeris_engine.h"
#include "taiyin/runtime/ephemeris_route.h"
#include "taiyin/runtime/native_context.h"
#include "taiyin/runtime/runtime.h"
#include "taiyin/time.h"
#include "taiyin/ziwei/calendar_adapter.h"
#include "taiyin/ziwei/flow_calendar_adapter.h"
#include "taiyin/ziwei/reverse_lookup.h"
#include "taiyin/ziwei/rules_loader.h"

#include <cstdlib>
#include <cmath>
#include <cstddef>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

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
        && taiyin::add_seconds_to_split_jd(*out, -8.0 * 3600.0, out);
}

int32_t star_branch(
    const taiyin::ziwei::NatalChart& chart,
    const taiyin::ziwei::StarRegistry& registry,
    const char* key
) {
    taiyin::ziwei::StarId id = taiyin::ziwei::kInvalidStarId;
    if (!registry.find(key, &id)) return -1;
    for (std::size_t branch = 0u; branch < taiyin::ziwei::kBranchCount; ++branch) {
        if (chart.palaces[branch].stars.test(id)) {
            return static_cast<int32_t>(branch);
        }
    }
    return -1;
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
        == TAIYIN_STATUS_OK, "configure geocentric observer", &failures);
    expect(runtime::native_context_set_route_rule(
        &astronomy, runtime::TAIYIN_EPHEMERIS_ROUTE_AUTO)
        == TAIYIN_STATUS_OK, "configure automatic route", &failures);

    chinese_calendar::ChineseCalendarContext calendar;
    const chinese_calendar::ChineseCalendarConfig calendar_config =
        chinese_calendar::historical_china_config();
    expect(chinese_calendar::initialize_context(
        &calendar, &astronomy, &calendar_config) == TAIYIN_STATUS_OK,
        "initialize historical China calendar", &failures);

    // Zhuge Liang demo used by the Dart oracle corpus: local China-standard
    // wall time 0181-08-20 08:00, encoded below as the corresponding UTC
    // instant. It exercises the historical-calendar adapter rather than a
    // hand-built CalendarFacts fixture.
    const CalendarDateTime virtual_time = {181, 8, 20, 8, 0, 0.0};
    SplitJulianDate instant_utc;
    expect(encode_china_standard(virtual_time, &instant_utc),
        "encode historical birth instant", &failures);

    runtime::EphemerisEvalDiagnostic diagnostic;
    ResolvedBirth resolved;
    const BirthResolutionOptions options =
        default_birth_resolution_options();
    const Status resolve_status = resolve_birth_from_calendar(
        &calendar,
        instant_utc,
        virtual_time,
        Gender::Male,
        options,
        &resolved,
        &diagnostic);
    expect(resolve_status == TAIYIN_STATUS_OK,
        "resolve Taiyin calendar facts", &failures);
    if (resolve_status == TAIYIN_STATUS_OK) {
        expect(resolved.anchors.palace_positions[to_index(PalaceId::Life)]
                == Branch::Chen
            && resolved.body_palace == Branch::Zi,
            "Dart oracle life/body palaces", &failures);
        expect(resolved.anchors.bureau == Bureau::Water2
            && resolved.anchors.ziwei == Branch::Zi
            && resolved.anchors.tianfu == Branch::Chen,
            "Dart oracle bureau and principal anchors", &failures);
        expect(resolved.facts.solar_day_from_previous_jie >= 1u,
            "solar-day fact is populated", &failures);
    }

    const LoadedRules loaded = load_rules_from_toml(
        std::string(TAIYIN_ZIWEI_TEST_ROOT) + "/rules/default.toml");
    NatalChart chart;
    expect(make_natal_chart_from_calendar(
        &calendar,
        instant_utc,
        virtual_time,
        Gender::Male,
        options,
        loaded.compiled,
        &chart,
        &diagnostic) == TAIYIN_STATUS_OK,
        "build natal chart directly from calendar", &failures);

    // Reverse lookup must use exactly the same calendar adapter and forward
    // chart rules.  A single-slot search is enough to lock its physical
    // instant/local-clock pairing without duplicating calendar arithmetic.
    ReverseLookupRequest reverse_request;
    reverse_request.start_instant_utc = instant_utc;
    reverse_request.end_instant_utc = instant_utc;
    reverse_request.start_virtual_time = virtual_time;
    reverse_request.gender = Gender::Male;
    reverse_request.query.lucun_branch = star_branch(chart, loaded.registry, "lucun");
    reverse_request.query.hongluan_branch = star_branch(chart, loaded.registry, "hongluan");
    reverse_request.query.zuofu_branch = star_branch(chart, loaded.registry, "zuofu");
    reverse_request.query.wenchang_branch = star_branch(chart, loaded.registry, "wenchang");
    reverse_request.query.santai_branch = star_branch(chart, loaded.registry, "santai");
    std::vector<ReverseLookupCandidate> reverse_candidates;
    expect(reverse_lookup_tier1_from_calendar(
        &calendar, reverse_request, loaded.compiled, loaded.registry,
        &reverse_candidates, &diagnostic) == TAIYIN_STATUS_OK
        && reverse_candidates.size() == 1u
        && reverse_candidates[0].virtual_time.year == virtual_time.year
        && reverse_candidates[0].virtual_time.month == virtual_time.month
        && reverse_candidates[0].virtual_time.day == virtual_time.day
        && reverse_candidates[0].virtual_time.hour == virtual_time.hour,
        "reverse lookup verifies a forward chart through the calendar adapter",
        &failures);

    StarRegistry mismatched_registry;
    for (std::size_t id = loaded.registry.size(); id-- > 0u;) {
        const StarMetadata& metadata = loaded.registry.at(
            static_cast<StarId>(id));
        mismatched_registry.add(metadata.key, metadata.category);
    }
    expect(reverse_lookup_tier1_from_calendar(
        &calendar, reverse_request, loaded.compiled, mismatched_registry,
        &reverse_candidates, &diagnostic) == TAIYIN_ERROR_INVALID_ARGUMENT,
        "reverse lookup rejects a same-sized but differently ordered registry",
        &failures);

    // Exercise the integration boundary where Ziwei inherits the caller's
    // historical-calendar and Rat-hour policies. The calendar module owns the
    // actual reform tables; Ziwei must accept their structured output without
    // a modern-date-only assumption.
    const CalendarDateTime boundary_clocks[] = {
        {-720, 1, 15, 12, 0, 0.0},
        {-220, 1, 15, 12, 0, 0.0},
        {237, 1, 15, 12, 0, 0.0},
        {701, 1, 15, 12, 0, 0.0},
        {762, 1, 15, 12, 0, 0.0},
        {2023, 3, 25, 10, 30, 0.0},
        {2033, 12, 22, 12, 0, 0.0},
    };
    for (std::size_t i = 0u;
         i < sizeof(boundary_clocks) / sizeof(boundary_clocks[0]); ++i) {
        SplitJulianDate boundary_instant;
        expect(encode_china_standard(
            boundary_clocks[i], &boundary_instant),
            "encode calendar boundary fixture", &failures);
        NatalChart boundary_chart;
        const Status status = make_natal_chart_from_calendar(
            &calendar,
            boundary_instant,
            boundary_clocks[i],
            (i & 1u) == 0u ? Gender::Male : Gender::Female,
            options,
            loaded.compiled,
            &boundary_chart,
            &diagnostic);
        std::vector<uint8_t> boundary_positions;
        expect(status == TAIYIN_STATUS_OK
            && dump_natal_star_positions(
                boundary_chart, &boundary_positions) == TAIYIN_STATUS_OK
            && boundary_positions.size() == loaded.registry.size(),
            "historical/calendar boundary remains chartable", &failures);
    }

    const CalendarDateTime rat_clock = {2000, 1, 1, 23, 30, 0.0};
    SplitJulianDate rat_instant;
    expect(encode_china_standard(rat_clock, &rat_instant),
        "encode late Rat-hour fixture", &failures);
    chinese_calendar::LunarDate physical_rat_lunar;
    SplitJulianDate next_day_instant;
    chinese_calendar::LunarDate next_day_rat_lunar;
    expect(chinese_calendar::fromInstant(
            &calendar, rat_instant, &physical_rat_lunar, &diagnostic)
            == TAIYIN_STATUS_OK
        && add_seconds_to_split_jd(
            rat_instant, 3600.0, &next_day_instant)
        && chinese_calendar::fromInstant(
            &calendar, next_day_instant, &next_day_rat_lunar, &diagnostic)
            == TAIYIN_STATUS_OK,
        "resolve physical and logical late Rat lunar dates", &failures);
    for (int32_t mode = chinese_calendar::TAIYIN_GANZHI_RAT_HOUR_NO_SPLIT;
         mode <= chinese_calendar::TAIYIN_GANZHI_RAT_HOUR_TOMORROW_GAN;
         ++mode) {
        BirthResolutionOptions rat_options = options;
        rat_options.rat_hour_mode = mode;
        ResolvedBirth rat_birth;
        expect(resolve_birth_from_calendar(
            &calendar,
            rat_instant,
            rat_clock,
            Gender::Female,
            rat_options,
            &rat_birth,
            &diagnostic) == TAIYIN_STATUS_OK
            && is_valid(rat_birth.anchors.solar_term.hour)
            && is_valid(rat_birth.anchors.lunar.hour),
            "all Rat-hour modes cross the calendar adapter", &failures);
        const chinese_calendar::LunarDate& expected_lunar =
            mode == chinese_calendar::TAIYIN_GANZHI_RAT_HOUR_NO_SPLIT
                ? next_day_rat_lunar : physical_rat_lunar;
        expect(rat_birth.facts.lunar_date.year == expected_lunar.year
            && rat_birth.facts.lunar_date.month == expected_lunar.month
            && rat_birth.facts.lunar_date.day == expected_lunar.day
            && rat_birth.facts.lunar_date.is_leap
                == (expected_lunar.is_leap != 0u),
            "late Rat lunar date follows the selected split policy", &failures);
    }

    // Legacy ziwei_core's default no-split convention treats the unified
    // 23:00--00:59 Zi hour as the following logical lunar day. This fixture
    // previously exposed a split state where the pillars advanced but the
    // lunar day used by day-dependent stars remained on the physical date.
    const CalendarDateTime oracle_rat_clock = {1984, 2, 4, 23, 30, 0.0};
    SplitJulianDate oracle_rat_instant;
    ResolvedBirth oracle_rat_birth;
    expect(encode_china_standard(oracle_rat_clock, &oracle_rat_instant)
        && resolve_birth_from_calendar(
            &calendar,
            oracle_rat_instant,
            oracle_rat_clock,
            Gender::Male,
            options,
            &oracle_rat_birth,
            &diagnostic) == TAIYIN_STATUS_OK
        && oracle_rat_birth.facts.lunar_date.year == 1984
        && oracle_rat_birth.facts.lunar_date.month == 1u
        && oracle_rat_birth.facts.lunar_date.day == 4u
        && !oracle_rat_birth.facts.lunar_date.is_leap,
        "1984 legacy oracle advances the unified late Rat lunar day",
        &failures);

    // The caller-selected virtual clock can differ from the calendar
    // context's civil clock (for example after a true-solar-time correction).
    // Resolve the Ziwei logical date from that virtual clock rather than by
    // adding a fixed hour to the physical instant: at virtual 23:00 below the
    // context clock is still 22:44, so instant+1h would not cross midnight.
    const CalendarDateTime offset_context_clock = {
        2000, 1, 1, 22, 44, 0.0,
    };
    const CalendarDateTime offset_virtual_clock = {
        2000, 1, 1, 23, 0, 0.0,
    };
    SplitJulianDate offset_instant;
    ResolvedBirth offset_context_birth;
    ResolvedBirth offset_no_split_birth;
    BirthResolutionOptions offset_split_options = options;
    offset_split_options.rat_hour_mode =
        chinese_calendar::TAIYIN_GANZHI_RAT_HOUR_TODAY_GAN;
    ResolvedBirth offset_split_birth;
    chinese_calendar::SolarDate offset_today;
    offset_today.year = 2000;
    offset_today.month = 1u;
    offset_today.day = 1u;
    chinese_calendar::SolarDate offset_tomorrow;
    offset_tomorrow.year = 2000;
    offset_tomorrow.month = 1u;
    offset_tomorrow.day = 2u;
    chinese_calendar::LunarDate offset_today_lunar;
    chinese_calendar::LunarDate offset_tomorrow_lunar;
    expect(encode_china_standard(offset_context_clock, &offset_instant)
        && chinese_calendar::fromSolar(
            &calendar, &offset_today, &offset_today_lunar, &diagnostic)
            == TAIYIN_STATUS_OK
        && chinese_calendar::fromSolar(
            &calendar, &offset_tomorrow, &offset_tomorrow_lunar, &diagnostic)
            == TAIYIN_STATUS_OK
        && resolve_birth_from_calendar(
            &calendar,
            offset_instant,
            offset_context_clock,
            Gender::Male,
            options,
            &offset_context_birth,
            &diagnostic) == TAIYIN_STATUS_OK
        && resolve_birth_from_calendar(
            &calendar,
            offset_instant,
            offset_virtual_clock,
            Gender::Male,
            options,
            &offset_no_split_birth,
            &diagnostic) == TAIYIN_STATUS_OK
        && resolve_birth_from_calendar(
            &calendar,
            offset_instant,
            offset_virtual_clock,
            Gender::Male,
            offset_split_options,
            &offset_split_birth,
            &diagnostic) == TAIYIN_STATUS_OK
        && offset_no_split_birth.facts.lunar_date.year
            == offset_tomorrow_lunar.year
        && offset_no_split_birth.facts.lunar_date.month
            == offset_tomorrow_lunar.month
        && offset_no_split_birth.facts.lunar_date.day
            == offset_tomorrow_lunar.day
        && offset_split_birth.facts.lunar_date.year
            == offset_today_lunar.year
        && offset_split_birth.facts.lunar_date.month
            == offset_today_lunar.month
        && offset_split_birth.facts.lunar_date.day
            == offset_today_lunar.day
        && offset_no_split_birth.facts.solar_term_pillars.year.stem
            == offset_context_birth.facts.solar_term_pillars.year.stem
        && offset_no_split_birth.facts.solar_term_pillars.year.branch
            == offset_context_birth.facts.solar_term_pillars.year.branch
        && offset_no_split_birth.facts.solar_term_pillars.month.stem
            == offset_context_birth.facts.solar_term_pillars.month.stem
        && offset_no_split_birth.facts.solar_term_pillars.month.branch
            == offset_context_birth.facts.solar_term_pillars.month.branch,
        "virtual clock controls the Ziwei logical lunar date", &failures);

    // A search must visit hour boundaries, not preserve its initial minutes.
    for (int scenario = 0; scenario < 6; ++scenario) {
        const int mode = scenario % 3;
        const bool use_apparent = scenario >= 3;
        BirthResolutionOptions search_options = options;
        search_options.rat_hour_mode = mode;
        const int starts[] = {0, 10, 22, 23};
        for (int h : starts) {
            const CalendarDateTime start = {2026, 9, 9, h, 30, 0.0};
            const CalendarDateTime finish = {2026, 9, h == 23 ? 10 : 9, (h + 1) % 24, 30, 0.0};
            const CalendarDateTime wanted = {2026, 9, h == 23 ? 10 : 9, (h + 1) % 24, 5, 0.0};
            SplitJulianDate wanted_jd;
            encode_china_standard(wanted, &wanted_jd);
            NatalChart wanted_chart;
            expect(make_natal_chart_from_calendar(&calendar, wanted_jd, wanted,
                Gender::Male, search_options, loaded.compiled, &wanted_chart, &diagnostic)
                == TAIYIN_STATUS_OK, "build overlapping hour", &failures);
            ReverseLookupRequest request;
            request.birth_options = search_options;
            request.start_virtual_time = start;
            encode_china_standard(start, &request.start_instant_utc);
            encode_china_standard(finish, &request.end_instant_utc);
            request.query.wenchang_branch = star_branch(wanted_chart, loaded.registry, "wenchang");
            std::vector<ReverseLookupCandidate> hits;
            ChartClock clock;
            clock.mode = ChartClockMode::ApparentSolar;
            clock.longitude_rad = 2.07;
            if (use_apparent) {
                expect(chart_time_to_ut1(&calendar, clock, start, &request.start_instant_utc)
                    == TAIYIN_STATUS_OK && chart_time_to_ut1(&calendar, clock, finish,
                    &request.end_instant_utc) == TAIYIN_STATUS_OK,
                    "invert reverse search endpoints", &failures);
            }
            const Status found = use_apparent
                ? reverse_lookup_tier1_at_ut1(&calendar, request, clock, loaded.compiled,
                    loaded.registry, &hits, &diagnostic)
                : reverse_lookup_tier1_from_calendar(&calendar, request, loaded.compiled,
                    loaded.registry, &hits, &diagnostic);
            // No-split Zi is one logical slot across midnight; split Zi has two.
            const bool unified_midnight = mode == 0 && h == 23;
            const std::size_t expected = h == 23 && mode != 0 ? 2u : 1u;
            bool correct_time = false;
            if (!hits.empty()) {
                CalendarDateTime expected_time = unified_midnight ? start : wanted;
                if (!unified_midnight) expected_time.minute = 0;
                SplitJulianDate a, b;
                julian_day_split(expected_time, &a);
                julian_day_split(hits.back().virtual_time, &b);
                correct_time = std::fabs(a - b) * 86400 < 0.001;
            }
            expect(found == TAIYIN_STATUS_OK
                && hits.size() == expected
                && correct_time,
                "partial hour interval is not skipped", &failures);
        }
    }

    // Check the complete carried date, including the Gregorian calendar gap.
    {
        const CalendarDateTime evenings[] = {{100,1,1,23,30,0}, {2026,12,31,23,30,0},
            {1984,2,29,23,30,0}, {1582,10,4,23,30,0}};
        const CalendarDateTime mornings[] = {{100,1,2,0,0,0}, {2027,1,1,0,0,0},
            {1984,3,1,0,0,0}, {1582,10,15,0,0,0}};
        for (std::size_t i = 0; i < 4; ++i) {
            const CalendarDateTime start = evenings[i];
            const CalendarDateTime midnight = mornings[i];
            SplitJulianDate jd, boundary;
            encode_china_standard(start, &jd);
            encode_china_standard(midnight, &boundary);
            for (int mode = 0; mode <= 2; ++mode) {
                BirthResolutionOptions opts = options; opts.rat_hour_mode = mode;
                ResolvedBirth wanted;
                expect(resolve_birth_from_calendar(&calendar, boundary, midnight,
                    Gender::Male, opts, &wanted, &diagnostic) == TAIYIN_STATUS_OK,
                    "resolve canonical next-day birth", &failures);
                NatalChart natal;
                make_natal_chart(wanted.facts, wanted.anchors, wanted.body_palace,
                    opts.anchor_options.rules, loaded.compiled, &natal);
                ReverseLookupRequest request;
                request.start_instant_utc = jd;
                request.end_instant_utc = boundary + 0.5 / 24;
                request.start_virtual_time = start;
                request.birth_options = opts;
                request.query.wenchang_branch = star_branch(natal, loaded.registry, "wenchang");
                for (int explicit_clock = 0; mode != 0 && explicit_clock < 2; ++explicit_clock) {
                    std::vector<ReverseLookupCandidate> hits;
                    ChartClock clock;
                    const Status s = explicit_clock
                        ? reverse_lookup_tier1_at_ut1(&calendar, request, clock, loaded.compiled,
                            loaded.registry, &hits)
                        : reverse_lookup_tier1_from_calendar(&calendar, request, loaded.compiled,
                            loaded.registry, &hits, &diagnostic);
                    expect(s == TAIYIN_STATUS_OK && hits.size() == 2u,
                        "both split-rat slots survive midnight", &failures);
                    if (hits.size() == 2u) {
                        const auto& hit = hits.back();
                        expect(hit.virtual_time.year == midnight.year
                            && hit.virtual_time.month == midnight.month
                            && hit.virtual_time.day == midnight.day
                            && hit.virtual_time.hour == 0 && hit.virtual_time.minute == 0
                            && hit.virtual_time.second == 0
                            && std::fabs(hit.instant_utc - boundary) < 1e-12
                            && hit.lunar_date.year == wanted.facts.lunar_date.year
                            && hit.lunar_date.month == wanted.facts.lunar_date.month
                            && hit.lunar_date.day == wanted.facts.lunar_date.day,
                            "reverse candidate physical date and lunar date agree", &failures);
                    }
                }
                CalendarDateTime at_23 = start; at_23.minute = 0;
                SplitJulianDate before, next, back;
                encode_china_standard(at_23, &before);
                CalendarDateTime next_time, back_time;
                RatHourSegment segment;
                expect(step_flow_hour_target(before, at_23, mode, 1, &next, &next_time,
                    &segment) == TAIYIN_STATUS_OK
                    && next_time.year == midnight.year && next_time.month == midnight.month
                    && next_time.day == midnight.day && next_time.hour == (mode == 0 ? 1 : 0),
                    "hour navigation carries full date", &failures);
                expect(step_flow_hour_target(next, next_time, mode, -1, &back, &back_time,
                    &segment) == TAIYIN_STATUS_OK
                    && back_time.year == at_23.year && back_time.month == at_23.month
                    && back_time.day == at_23.day && back_time.hour == 23,
                    "reverse hour navigation carries date backwards", &failures);
            }
            SplitJulianDate next;
            CalendarDateTime next_time;
            expect(step_flow_day_target(jd, start, 1, &next, &next_time) == TAIYIN_STATUS_OK
                && next_time.year == midnight.year && next_time.month == midnight.month
                && next_time.day == midnight.day && next_time.hour == 23 && next_time.minute == 30,
                "day navigation preserves clock with full date carry", &failures);
            SplitJulianDate back;
            CalendarDateTime back_time;
            expect(step_flow_day_target(next, next_time, -1, &back, &back_time) == TAIYIN_STATUS_OK
                && back_time.year == start.year && back_time.month == start.month
                && back_time.day == start.day && back_time.hour == 23 && back_time.minute == 30,
                "day navigation carries backwards across calendar boundaries", &failures);
        }
    }

    // Historical pillar midnight and the solar-day origin must be identical.
    for (int policy = 0; policy < 3; ++policy) {
        chinese_calendar::ChineseCalendarConfig config = calendar_config;
        config.pillar_historical_mode = policy;
        chinese_calendar::ChineseCalendarContext historical;
        expect(chinese_calendar::initialize_context(&historical, &astronomy, &config)
            == TAIYIN_STATUS_OK, "historical boundary context", &failures);
        CalendarDateTime begin = {100, 1, 1, 12, 0, 0.0};
        SplitJulianDate jd;
        encode_china_standard(begin, &jd);
        chinese_calendar::SolarTermEvent term;
        expect(chinese_calendar::getNextJie(&historical, jd, &term, &diagnostic)
            == TAIYIN_STATUS_OK, "historical next Jie", &failures);
        SplitJulianDate boundary;
        expect(chinese_calendar::pillar_term_boundary(&historical, term, &boundary)
            == TAIYIN_STATUS_OK, "effective pillar boundary", &failures);
        ResolvedBirth before, after;
        CalendarDateTime pre, post;
        reverse_julian_day_split(boundary - 1.0 / 86400 + 8.0 / 24, &pre);
        reverse_julian_day_split(boundary + 1.0 / 86400 + 8.0 / 24, &post);
        expect(resolve_birth_from_calendar(&historical, boundary - 1.0 / 86400,
            pre, Gender::Male, options, &before, &diagnostic) == TAIYIN_STATUS_OK
            && resolve_birth_from_calendar(&historical, boundary + 1.0 / 86400,
            post, Gender::Male, options, &after, &diagnostic) == TAIYIN_STATUS_OK
            && before.facts.solar_term_pillars.month.branch != after.facts.solar_term_pillars.month.branch
            && after.facts.solar_day_from_previous_jie == 1u,
            "solar day resets at the actual pillar boundary", &failures);

        // A custom placement depending on solar month can change within an hour.
        CompiledRules custom = loaded.compiled;
        StarId wc;
        loaded.registry.find("wenchang", &wc);
        for (std::size_t i = 0; i < custom.placement.natal.size(); ++i) {
            PlacementRule& rule = custom.placement.natal[i];
            if (rule.star_id != wc) continue;
            rule.inputs.assign(1, RuleInputSource::SolarMonthBranch);
            rule.strides.assign(1, 1u);
            rule.table.resize(12);
            for (std::size_t j = 0; j < 12; ++j) rule.table[j] = static_cast<uint8_t>(j);
        }
        custom.registry_fingerprint = compiled_rules_fingerprint(custom, loaded.registry.fingerprint());
        ReverseLookupRequest request;
        request.start_instant_utc = boundary - 1.0 / 86400;
        request.end_instant_utc = boundary + 1.0 / 86400;
        request.start_virtual_time = pre;
        request.query.wenchang_branch = to_index(after.facts.solar_term_pillars.month.branch);
        std::vector<ReverseLookupCandidate> hits;
        const Status reverse_status = reverse_lookup_tier1_from_calendar(&historical, request, custom,
            loaded.registry, &hits, &diagnostic);
        if (reverse_status != TAIYIN_STATUS_OK || hits.size() != 1u) {
            std::cerr << "policy=" << policy << " reverse=" << reverse_status << " hits=" << hits.size() << '\n';
        }
        expect(reverse_status == TAIYIN_STATUS_OK && hits.size() == 1u,
            "reverse visits a Jie inside the hour", &failures);
        if (policy != chinese_calendar::TAIYIN_GANZHI_PILLAR_HISTORICAL_OFF) {
            expect(boundary != term.jd_ut, "fixture uses an assigned historical day", &failures);
            request.start_instant_utc = boundary - 4.0e-11;
            request.end_instant_utc = boundary + 4.0e-11;
            reverse_julian_day_split(request.start_instant_utc + 8.0 / 24,
                &request.start_virtual_time);
            chinese_calendar::SolarTermEvent unused;
            SplitJulianDate next;
            expect(chinese_calendar::next_pillar_jie(&historical,
                request.start_instant_utc, &unused, &next, &diagnostic)
                == TAIYIN_STATUS_OK && next == boundary,
                "retain historical midnight only microseconds ahead", &failures);
            expect(chinese_calendar::next_pillar_jie(&historical,
                boundary, &unused, &next, &diagnostic)
                == TAIYIN_STATUS_OK && next - boundary > 20.0,
                "do not repeat an exact historical midnight", &failures);
            hits.clear();
            expect(reverse_lookup_tier1_from_calendar(&historical, request, custom,
                loaded.registry, &hits, &diagnostic) == TAIYIN_STATUS_OK
                && hits.size() == 1u && hits.front().instant_utc == boundary,
                "short reverse interval retains historical month change", &failures);
            ChartClock clock;
            hits.clear();
            expect(reverse_lookup_tier1_at_ut1(&historical, request, clock, custom,
                loaded.registry, &hits, &diagnostic) == TAIYIN_STATUS_OK
                && hits.size() == 1u && hits.front().instant_utc == boundary,
                "explicit clock reverse retains historical month change", &failures);
        }
    }

    LunarDateFacts later_nine = {-200, -200, 9, 16, 1,
        chinese_calendar::TAIYIN_CHINESE_MONTH_NAME_LATER_NINE};
    for (int strategy = 1; strategy <= 2; ++strategy) {
        int32_t year = 0; uint8_t month = 0;
        expect(resolve_effective_lunar_month(later_nine,
            static_cast<LeapMonthStrategy>(strategy), &year, &month) == TAIYIN_STATUS_OK
            && year == -199 && month == 10,
            "advancing historical later-nine crosses into the next year", &failures);
    }
    {
        later_nine.month_name = chinese_calendar::TAIYIN_CHINESE_MONTH_NAME_NORMAL;
        int32_t year = 0; uint8_t month = 0;
        expect(resolve_effective_lunar_month(later_nine, LeapMonthStrategy::AsNext,
            &year, &month) == TAIYIN_STATUS_OK && year == -200 && month == 10,
            "ordinary leap-nine does not advance the year", &failures);
    }

    {
        CalendarDateTime begin = {2026, 9, 1, 12, 0, 0.0};
        SplitJulianDate jd; encode_china_standard(begin, &jd);
        chinese_calendar::SolarTermEvent term;
        SplitJulianDate boundary;
        expect(chinese_calendar::next_pillar_jie(&calendar, jd, &term, &boundary, &diagnostic)
            == TAIYIN_STATUS_OK, "find modern intra-hour Jie", &failures);
        ReverseLookupRequest request;
        request.start_instant_utc = boundary - 1.0 / 86400;
        request.end_instant_utc = boundary + 1.0 / 86400;
        reverse_julian_day_split(request.start_instant_utc + 8.0 / 24, &request.start_virtual_time);
        NatalChart base;
        expect(make_natal_chart_from_calendar(&calendar, request.start_instant_utc,
            request.start_virtual_time, Gender::Male, options, loaded.compiled, &base, &diagnostic)
            == TAIYIN_STATUS_OK, "build unchanged pre-Jie chart", &failures);
        request.query.wenchang_branch = star_branch(base, loaded.registry, "wenchang");
        std::vector<ReverseLookupCandidate> hits;
        expect(reverse_lookup_tier1_from_calendar(&calendar, request, loaded.compiled,
            loaded.registry, &hits, &diagnostic) == TAIYIN_STATUS_OK && hits.size() == 1u,
            "unchanged intra-hour Jie placement is not duplicated", &failures);
        SplitJulianDate next;
        expect(chinese_calendar::next_pillar_jie(&calendar, boundary, &term, &next, &diagnostic)
            == TAIYIN_STATUS_OK && next - boundary > 20.0,
            "next pillar Jie cannot repeat the previous numerical root", &failures);
    }

    // Explicit clocks must be evaluated at both endpoints, never carried as
    // a constant equation-of-time offset from the birth epoch.
    for (int clock_mode = 0; clock_mode < 3; ++clock_mode) {
        ChartClock clock;
        clock.mode = static_cast<ChartClockMode>(clock_mode);
        clock.longitude_rad = 118.582 * 3.14159265358979323846 / 180.0;
        const CalendarDateTime start = {2026, 9, 9, 22, 30, 0.0};
        SplitJulianDate jd;
        expect(chart_time_to_ut1(&calendar, clock, start, &jd) == TAIYIN_STATUS_OK,
            "invert explicit chart clock", &failures);
        CalendarDateTime roundtrip;
        expect(chart_time_from_ut1(&calendar, clock, jd, &roundtrip) == TAIYIN_STATUS_OK,
            "evaluate explicit chart clock", &failures);
        SplitJulianDate expected, actual;
        julian_day_split(start, &expected);
        julian_day_split(roundtrip, &actual);
        expect(std::fabs(actual - expected) * 86400 < 0.001,
            "chart clock round trip within one millisecond", &failures);
        SplitJulianDate tomorrow;
        CalendarDateTime tomorrow_time;
        expect(step_flow_day_at_ut1(&calendar, jd, clock, 1, &tomorrow,
            &tomorrow_time) == TAIYIN_STATUS_OK,
            "step day through clock inverse", &failures);
        expect(chart_time_from_ut1(&calendar, clock, tomorrow, &roundtrip) == TAIYIN_STATUS_OK,
            "re-evaluate stepped day", &failures);
        julian_day_split(roundtrip, &actual);
        expect(std::fabs(actual - expected - 1.0) * 86400 < 0.001,
            "day navigation preserves apparent clock", &failures);
        if (clock_mode == 2)
            expect(std::fabs(tomorrow - jd - 1.0) * 86400 > 1.0,
                "apparent day is not a fixed 86400 seconds", &failures);
        for (int mode = 0; mode < 3; ++mode) {
            SplitJulianDate next;
            CalendarDateTime next_time;
            RatHourSegment segment;
            expect(step_flow_hour_at_ut1(&calendar, jd, clock, mode, 1, &next,
                &next_time, &segment) == TAIYIN_STATUS_OK,
                "step apparent hour", &failures);
            expect(chart_time_from_ut1(&calendar, clock, next, &roundtrip) == TAIYIN_STATUS_OK,
                "re-evaluate stepped hour", &failures);
            julian_day_split(roundtrip, &actual);
            julian_day_split(next_time, &expected);
            expect(std::fabs(actual - expected) * 86400 < 0.001,
                "hour navigation returns a consistent physical instant", &failures);
            BirthResolutionOptions opts = options;
            opts.rat_hour_mode = mode;
            ResolvedBirth birth;
            expect(resolve_birth_at_ut1(&calendar, next, clock, Gender::Male,
                opts, &birth) == TAIYIN_STATUS_OK, "clock-aware natal facts", &failures);
            NatalChart natal;
            expect(make_natal_chart(birth.facts, birth.anchors, birth.body_palace,
                opts.anchor_options.rules, loaded.compiled, &natal)
                == TAIYIN_STATUS_OK, "clock-aware natal chart", &failures);
            ReverseLookupRequest request;
            request.birth_options = opts;
            request.start_instant_utc = jd;
            request.end_instant_utc = next;
            request.query.wenchang_branch = star_branch(natal, loaded.registry, "wenchang");
            std::vector<ReverseLookupCandidate> hits;
            expect(reverse_lookup_tier1_at_ut1(&calendar, request, clock,
                loaded.compiled, loaded.registry, &hits) == TAIYIN_STATUS_OK && !hits.empty(),
                "clock-aware reverse lookup crosses Rat boundary", &failures);
        }
    }

    // Place the Jie almost at solar midnight. Its equation of time must be
    // evaluated at the Jie itself, not borrowed from a later target epoch.
    {
        ChartClock clock;
        clock.mode = ChartClockMode::ApparentSolar;
        SplitJulianDate start;
        encode_china_standard(CalendarDateTime{2026, 9, 1, 12, 0, 0.0}, &start);
        chinese_calendar::SolarTermEvent term;
        SplitJulianDate jie;
        expect(chinese_calendar::next_pillar_jie(&calendar, start, &term, &jie, &diagnostic)
            == TAIYIN_STATUS_OK, "clock test Jie", &failures);
        CalendarDateTime greenwich;
        expect(chart_time_from_ut1(&calendar, clock, jie, &greenwich) == TAIYIN_STATUS_OK,
            "Jie apparent clock at Greenwich", &failures);
        const double seconds = greenwich.hour * 3600.0 + greenwich.minute * 60.0 + greenwich.second;
        double longitude = (1.0 - seconds) / 86400.0 * 6.2831853071795864769;
        if (longitude < -3.14159265358979323846) longitude += 6.2831853071795864769;
        clock.longitude_rad = longitude;
        for (int mode = 0; mode < 3; ++mode) {
            BirthResolutionOptions opts = options;
            opts.rat_hour_mode = mode;
            ResolvedBirth birth;
            expect(resolve_birth_at_ut1(&calendar, jie + 10.0, clock, Gender::Male,
                opts, &birth) == TAIYIN_STATUS_OK, "nonlinear Jie day natal", &failures);
            CalendarDateTime term_time, target_time;
            chart_time_from_ut1(&calendar, clock, jie, &term_time);
            chart_time_from_ut1(&calendar, clock, jie + 10.0, &target_time);
            SplitJulianDate a, b;
            julian_day_split(term_time, &a); julian_day_split(target_time, &b);
            if (mode == 0 && term_time.hour >= 23) a += 1.0 / 24;
            if (mode == 0 && target_time.hour >= 23) b += 1.0 / 24;
            const int64_t expected_day = (b + 0.5).day_number - (a + 0.5).day_number + 1;
            expect(birth.facts.solar_day_from_previous_jie == expected_day,
                "Jie and target independently assigned to solar days", &failures);
            NatalChart natal;
            make_natal_chart(birth.facts, birth.anchors, birth.body_palace,
                opts.anchor_options.rules, loaded.compiled, &natal);
            FlowResolutionOptions flow_options = default_flow_resolution_options();
            flow_options.rat_hour_mode = mode;
            flow_options.boundary = PillarBoundary::SolarTerm;
            ResolvedFlow flow;
            expect(resolve_flow_at_ut1(&calendar, birth, natal, jie + 10.0, clock,
                flow_options, &flow) == TAIYIN_STATUS_OK && flow.target_day == expected_day,
                "clock-aware flow resolution", &failures);
            Chart chart;
            chart.natal = natal;
            expect(set_flow_stack_through_at_ut1(&calendar, birth, jie + 10.0, clock,
                flow_options, FlowLevel::Hour, loaded.compiled, &chart, &flow)
                == TAIYIN_STATUS_OK && chart.flow_stack.size() == 5u,
                "clock-aware complete flow stack", &failures);
        }
        SplitJulianDate unchanged = start;
        clock.mode = static_cast<ChartClockMode>(99);
        expect(chart_time_to_ut1(&calendar, clock, greenwich, &unchanged)
            == TAIYIN_ERROR_INVALID_ARGUMENT && unchanged == start,
            "invalid clock preserves output", &failures);
    }

    {
        const CalendarDateTime time = {2026, 9, 9, 12, 0, 0.0};
        ChartClock clock;
        SplitJulianDate expected;
        expect(chart_time_to_ut1(&calendar, clock, time, &expected) == TAIYIN_STATUS_OK,
            "fixed clock baseline", &failures);
        const double longitudes[] = {std::numeric_limits<double>::quiet_NaN(),
            std::numeric_limits<double>::infinity(), 4.0, -4.0};
        for (double longitude : longitudes) {
            clock.longitude_rad = longitude;
            for (int mode = 0; mode < 3; ++mode) {
                clock.mode = static_cast<ChartClockMode>(mode);
                SplitJulianDate actual = expected;
                CalendarDateTime decoded = time;
                const Status wanted = mode == 0 ? TAIYIN_STATUS_OK : TAIYIN_ERROR_INVALID_ARGUMENT;
                expect(chart_time_to_ut1(&calendar, clock, time, &actual) == wanted
                    && actual == expected, "longitude only validated for solar inverse", &failures);
                expect(chart_time_from_ut1(&calendar, clock, expected, &decoded) == wanted
                    && decoded.year == time.year && decoded.month == time.month
                    && decoded.day == time.day && decoded.hour == time.hour
                    && decoded.minute == time.minute && decoded.second == time.second,
                    "longitude only validated for solar forward mapping", &failures);
            }
        }
    }

    if (failures != 0) {
        std::cerr << failures << " Ziwei calendar-adapter checks failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "Ziwei calendar-adapter checks passed\n";
    return EXIT_SUCCESS;
}
