#include "taiyin/body_id.h"
#include "taiyin/chinese_calendar/calendar.h"
#include "taiyin/runtime/ephemeris_route.h"
#include "taiyin/runtime/native_context.h"
#include "taiyin/runtime/runtime.h"
#include "taiyin/time.h"
#include "taiyin/ziwei/ziweicore.h"

#include <cstdlib>
#include <iostream>
#include <string>

#ifndef TAIYIN_ZIWEI_RULES_FILE
#define TAIYIN_ZIWEI_RULES_FILE "ziwei_astrology/rules/default.toml"
#endif

namespace {

bool require(taiyin::Status status, const char* operation) {
    if (status == taiyin::TAIYIN_STATUS_OK) return true;
    std::cerr << operation << " failed with status " << status << '\n';
    return false;
}

}  // namespace

int main() {
    using namespace taiyin;
    using namespace taiyin::ziwei;

    runtime::EphemerisRuntimeConfig runtime_config;
    runtime_config.load_packaged_data = true;
    if (!runtime::initialize_global_ephemeris_runtime(runtime_config)) {
        std::cerr << "failed to initialize the ephemeris runtime\n";
        return EXIT_FAILURE;
    }

    runtime::NativeCalcContext astronomy;
    if (!require(runtime::native_context_set_geocentric_observer(
            &astronomy, TAIYIN_BODY_EARTH, TAIYIN_BODY_EARTH),
            "configure observer")
        || !require(runtime::native_context_set_route_rule(
            &astronomy, runtime::TAIYIN_EPHEMERIS_ROUTE_AUTO),
            "configure ephemeris route")) {
        return EXIT_FAILURE;
    }

    chinese_calendar::ChineseCalendarContext calendar;
    const chinese_calendar::ChineseCalendarConfig calendar_config =
        chinese_calendar::historical_china_config();
    if (!require(chinese_calendar::initialize_context(
            &calendar, &astronomy, &calendar_config),
            "initialize Chinese calendar")) {
        return EXIT_FAILURE;
    }

    // China-standard local time: 2003-03-13 14:15.
    const CalendarDateTime local_time = {2003, 3, 13, 14, 15, 0.0};
    SplitJulianDate instant_utc;
    if (!julian_day_split(local_time, &instant_utc)
        || !add_seconds_to_split_jd(
            instant_utc, -8.0 * 3600.0, &instant_utc)) {
        std::cerr << "failed to encode the birth instant\n";
        return EXIT_FAILURE;
    }

    // Parsing is a catalog operation, not a per-chart operation. Multiple
    // option-selected contexts may share this one immutable TOML snapshot.
    ZiweiDataCatalog data_catalog(TAIYIN_ZIWEI_RULES_FILE);
    const ZiweiContext ziwei_context = data_catalog.create_context();
    const CompiledRules& tables = ziwei_context.compiled_tables();
    const StarRegistry& stars = ziwei_context.star_registry();
    const BirthResolutionOptions options =
        default_birth_resolution_options();
    runtime::EphemerisEvalDiagnostic diagnostic;
    ResolvedBirth birth;
    if (!require(resolve_birth_from_calendar(
            &calendar,
            instant_utc,
            local_time,
            Gender::Male,
            options,
            &birth,
            &diagnostic),
            "resolve birth")) {
        return EXIT_FAILURE;
    }
    NatalChart natal;
    if (!require(make_natal_chart(
            birth.facts,
            birth.anchors,
            birth.body_palace,
            options.anchor_options.rules,
            tables,
            &natal),
            "construct natal chart")) {
        return EXIT_FAILURE;
    }

    std::vector<uint8_t> positions;
    if (!require(dump_natal_star_positions(natal, &positions),
            "serialize natal positions")) {
        return EXIT_FAILURE;
    }
    std::cout << "life_palace="
              << static_cast<unsigned>(to_index(
                     natal.anchors.palace_positions[
                         to_index(PalaceId::Life)]))
              << " body_palace="
              << static_cast<unsigned>(to_index(natal.body_palace)) << '\n';
    for (StarId id = 0u; id < tables.natal_star_count; ++id) {
        std::cout << stars.at(id).key << '='
                  << static_cast<unsigned>(positions[id]) << '\n';
    }
    StarId ziwei = kInvalidStarId;
    Brightness ziwei_brightness = Brightness::None;
    if (!stars.find("ziwei", &ziwei)
        || !require(brightness_at(
            tables,
            ziwei,
            static_cast<Branch>(positions[ziwei]),
            &ziwei_brightness),
            "query Ziwei brightness")) {
        return EXIT_FAILURE;
    }
    std::cout << "ziwei_brightness="
              << static_cast<int>(ziwei_brightness) << '\n';

    // Resolve one target instant and atomically build the complete five-level
    // Decade->Year->Month->Day->Hour stack.
    const CalendarDateTime target_local = {2025, 6, 1, 12, 0, 0.0};
    SplitJulianDate target_utc;
    if (!julian_day_split(target_local, &target_utc)
        || !add_seconds_to_split_jd(
            target_utc, -8.0 * 3600.0, &target_utc)) {
        std::cerr << "failed to encode the flow target\n";
        return EXIT_FAILURE;
    }
    Chart chart;
    chart.natal = natal;
    ResolvedFlow flow;
    if (!require(set_flow_stack_from_calendar(
            &calendar,
            birth,
            target_utc,
            target_local,
            default_flow_resolution_options(),
            tables,
            &chart,
            &flow,
            &diagnostic),
            "construct flow stack")) {
        return EXIT_FAILURE;
    }
    std::vector<int64_t> numeric_dump;
    if (!require(dump_chart_numeric(chart, &numeric_dump),
            "dump complete chart")) {
        return EXIT_FAILURE;
    }
    std::cout << "flow_layers=" << chart.flow_stack.size()
              << " numeric_dump_values=" << numeric_dump.size() << '\n';
    return EXIT_SUCCESS;
}
