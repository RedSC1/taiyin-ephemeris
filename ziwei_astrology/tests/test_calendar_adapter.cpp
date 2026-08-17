#include "taiyin/body_id.h"
#include "taiyin/chinese_calendar/calendar.h"
#include "taiyin/runtime/ephemeris_engine.h"
#include "taiyin/runtime/ephemeris_route.h"
#include "taiyin/runtime/native_context.h"
#include "taiyin/runtime/runtime.h"
#include "taiyin/time.h"
#include "taiyin/ziwei/calendar_adapter.h"
#include "taiyin/ziwei/reverse_lookup.h"
#include "taiyin/ziwei/rules_loader.h"

#include <cstdlib>
#include <cstddef>
#include <iostream>
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
    }

    if (failures != 0) {
        std::cerr << failures << " Ziwei calendar-adapter checks failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "Ziwei calendar-adapter checks passed\n";
    return EXIT_SUCCESS;
}
