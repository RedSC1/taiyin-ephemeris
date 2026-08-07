#include "runtime/visibility/moon_visibility_internal.h"
#include "runtime/visibility/visibility_math_internal.h"

#include "taiyin/body_id.h"
#include "taiyin/runtime/native_context.h"
#include "taiyin/runtime/runtime.h"
#include "taiyin/status.h"
#include "taiyin/time.h"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <string>

namespace {

void expect_true(bool value, const char* label, int* failures) {
    if (!value) {
        std::cerr << "FAIL: expected true: " << label << "\n";
        ++(*failures);
    }
}

void expect_equal(int actual, int expected, const char* label, int* failures) {
    if (actual != expected) {
        std::cerr << "FAIL: " << label << ": actual=" << actual
                  << " expected=" << expected << "\n";
        ++(*failures);
    }
}

void expect_status(taiyin::Status actual, taiyin::Status expected, const char* label, int* failures) {
    if (actual != expected) {
        std::cerr << "FAIL: " << label << ": actual=" << actual
                  << " expected=" << expected << "\n";
        ++(*failures);
    }
}

void expect_near(double actual, double expected, double tolerance, const char* label, int* failures) {
    const double diff = std::fabs(actual - expected);
    if (!(diff <= tolerance)) {
        std::cerr << "FAIL: " << label << ": actual=" << actual
                  << " expected=" << expected
                  << " diff=" << diff
                  << " tolerance=" << tolerance << "\n";
        ++(*failures);
    }
}

taiyin::SplitJulianDate split_jd(double jd) {
    taiyin::SplitJulianDate out;
    if (!taiyin::split_julian_date_from_double(jd, &out)) {
        out.day_fraction = NAN;
    }
    return out;
}

void expect_near(
    const taiyin::SplitJulianDate& actual,
    double expected,
    double tolerance,
    const char* label,
    int* failures
) {
    expect_near(taiyin::split_julian_date_to_double(actual), expected, tolerance, label, failures);
}

std::string repo_opm2_major_body_root() {
    const char* root = std::getenv("TAIYIN_REPO_ROOT");
    if (root && root[0] != '\0') {
        return std::string(root) + "/data/ephemerides/opm2/major-bodies/600y";
    }
    return "../data/ephemerides/opm2/major-bodies/600y";
}

bool initialize_runtime(int* failures) {
    taiyin::runtime::EphemerisRuntimeConfig config;
    const std::string data_root = repo_opm2_major_body_root();
    const char* source_paths[] = { data_root.c_str() };
    config.source_paths = source_paths;
    config.source_path_count = 1;
    config.load_packaged_data = false;
    config.segment_cache_max_entries = 256;
    const bool ok = taiyin::runtime::initialize_global_ephemeris_runtime(config);
    expect_true(ok, "initialize OPM2 runtime", failures);
    return ok;
}

double jd_ut(int year, int month, int day, double hour) {
    return taiyin::julian_day({year, month, day, static_cast<int>(hour), 0, (hour - static_cast<int>(hour)) * 3600.0});
}

taiyin::runtime::NativeCalcContext make_context(
    double longitude_deg,
    double latitude_deg,
    double height_m,
    bool atmosphere = true
) {
    taiyin::runtime::NativeCalcContext context;
    taiyin::runtime::native_context_set_geocentric_observer(
        &context,
        taiyin::TAIYIN_BODY_EARTH,
        taiyin::TAIYIN_BODY_EARTH);
    taiyin::runtime::native_context_set_observer_location(
        &context,
        taiyin::runtime::native_observer_location_degrees(longitude_deg, latitude_deg, height_m));
    if (atmosphere) {
        taiyin::runtime::native_context_set_atmosphere(&context, taiyin::runtime::native_standard_atmosphere());
    }
    taiyin::runtime::native_context_use_solar_deflector(&context);
    context.apparent_options.flags =
        taiyin::TAIYIN_APPARENT_SPHERICAL
        | taiyin::TAIYIN_APPARENT_LIGHT_TIME
        | taiyin::TAIYIN_APPARENT_ABERRATION
        | taiyin::TAIYIN_APPARENT_DEFLECTION;
    context.apparent_options.output_frame_id = taiyin::TAIYIN_APPARENT_FRAME_TRUE_ECLIPTIC_OF_DATE;
    return context;
}

void expect_crossing_against_swiss(
    const taiyin::runtime::VisibilityAltitudeSearchResult& result,
    double swiss_jd_ut,
    double tolerance_seconds,
    int expected_direction,
    const char* label,
    int* failures
) {
    expect_equal(result.altitude_state, taiyin::runtime::TAIYIN_VISIBILITY_ALTITUDE_STATE_CROSSES, label, failures);
    expect_equal(result.crossing_direction, expected_direction, label, failures);
    expect_near(result.jd_ut, swiss_jd_ut, tolerance_seconds / 86400.0, label, failures);
}

struct RiseSetOracle {
    const char* label;
    int event_kind;
    int limb_kind;
    uint32_t flags;
    double swiss_jd_ut;
    double tolerance_seconds;
};

struct TransitOracle {
    const char* label;
    double longitude_deg;
    double latitude_deg;
    double height_m;
    double start_jd_ut;
    int event_kind;
    double swiss_jd_ut;
};

void test_denver_oracles(int* failures) {
    using namespace taiyin::runtime;

    NativeCalcContext context = make_context(-104.9903, 39.7392, 1609.0);
    const taiyin::SplitJulianDate start = split_jd(jd_ut(2024, 4, 8, 6.0));
    const taiyin::SplitJulianDate end = start + 1.0;
    EphemerisEvalDiagnostic diagnostic;
    VisibilityAltitudeSearchResult result;

    const RiseSetOracle oracles[] = {
        {"denver_moon_rise_default", TAIYIN_MOON_VISIBILITY_EVENT_RISE, TAIYIN_MOON_VISIBILITY_LIMB_UPPER, TAIYIN_MOON_VISIBILITY_FLAG_REFRACTION, 2460409.020203506574, 5.0},
        {"denver_moon_set_default", TAIYIN_MOON_VISIBILITY_EVENT_SET, TAIYIN_MOON_VISIBILITY_LIMB_UPPER, TAIYIN_MOON_VISIBILITY_FLAG_REFRACTION, 2460409.576166798826, 5.0},
        {"denver_moon_rise_no_refraction", TAIYIN_MOON_VISIBILITY_EVENT_RISE, TAIYIN_MOON_VISIBILITY_LIMB_UPPER, 0u, 2460409.022281939164, 1.0},
        {"denver_moon_set_no_refraction", TAIYIN_MOON_VISIBILITY_EVENT_SET, TAIYIN_MOON_VISIBILITY_LIMB_UPPER, 0u, 2460409.573980987538, 1.0},
        {"denver_moon_rise_center_refraction", TAIYIN_MOON_VISIBILITY_EVENT_RISE, TAIYIN_MOON_VISIBILITY_LIMB_CENTER, TAIYIN_MOON_VISIBILITY_FLAG_REFRACTION, 2460409.021232043859, 5.0},
        {"denver_moon_set_center_refraction", TAIYIN_MOON_VISIBILITY_EVENT_SET, TAIYIN_MOON_VISIBILITY_LIMB_CENTER, TAIYIN_MOON_VISIBILITY_FLAG_REFRACTION, 2460409.575088066049, 5.0},
        {"denver_moon_rise_lower_refraction", TAIYIN_MOON_VISIBILITY_EVENT_RISE, TAIYIN_MOON_VISIBILITY_LIMB_LOWER, TAIYIN_MOON_VISIBILITY_FLAG_REFRACTION, 2460409.022260144819, 5.0},
        {"denver_moon_set_lower_refraction", TAIYIN_MOON_VISIBILITY_EVENT_SET, TAIYIN_MOON_VISIBILITY_LIMB_LOWER, TAIYIN_MOON_VISIBILITY_FLAG_REFRACTION, 2460409.574010306504, 5.0},
        {"denver_moon_rise_lower_no_refraction", TAIYIN_MOON_VISIBILITY_EVENT_RISE, TAIYIN_MOON_VISIBILITY_LIMB_LOWER, 0u, 2460409.024336538278, 1.0},
        {"denver_moon_set_lower_no_refraction", TAIYIN_MOON_VISIBILITY_EVENT_SET, TAIYIN_MOON_VISIBILITY_LIMB_LOWER, 0u, 2460409.571828746237, 1.0},
        {"denver_moon_rise_fixed_disc_upper_no_refraction", TAIYIN_MOON_VISIBILITY_EVENT_RISE, TAIYIN_MOON_VISIBILITY_LIMB_UPPER, TAIYIN_MOON_VISIBILITY_FLAG_FIXED_DISC_SIZE, 2460409.022348993924, 1.0},
        {"denver_moon_set_fixed_disc_upper_no_refraction", TAIYIN_MOON_VISIBILITY_EVENT_SET, TAIYIN_MOON_VISIBILITY_LIMB_UPPER, TAIYIN_MOON_VISIBILITY_FLAG_FIXED_DISC_SIZE, 2460409.573913708795, 1.0},
        {"denver_moon_rise_fixed_disc_lower_no_refraction", TAIYIN_MOON_VISIBILITY_EVENT_RISE, TAIYIN_MOON_VISIBILITY_LIMB_LOWER, TAIYIN_MOON_VISIBILITY_FLAG_FIXED_DISC_SIZE, 2460409.024269387126, 1.0},
        {"denver_moon_set_fixed_disc_lower_no_refraction", TAIYIN_MOON_VISIBILITY_EVENT_SET, TAIYIN_MOON_VISIBILITY_LIMB_LOWER, TAIYIN_MOON_VISIBILITY_FLAG_FIXED_DISC_SIZE, 2460409.571896088310, 1.0},
    };
    for (const RiseSetOracle& oracle : oracles) {
        expect_status(
            moon_visibility_search_rise_set_ut(
                &context,
                start,
                end,
                oracle.event_kind,
                oracle.limb_kind,
                oracle.flags,
                &result,
                &diagnostic),
            taiyin::TAIYIN_STATUS_OK,
            oracle.label,
            failures);
        expect_crossing_against_swiss(
            result,
            oracle.swiss_jd_ut,
            oracle.tolerance_seconds,
            oracle.event_kind == TAIYIN_MOON_VISIBILITY_EVENT_RISE
                ? TAIYIN_VISIBILITY_CROSSING_RISING
                : TAIYIN_VISIBILITY_CROSSING_SETTING,
            oracle.label,
            failures);
    }
}

void test_polar_no_event_oracles(int* failures) {
    using namespace taiyin::runtime;

    EphemerisEvalDiagnostic diagnostic;
    VisibilityAltitudeSearchResult result;

    NativeCalcContext longyearbyen = make_context(15.6333, 78.2232, 10.0);
    const taiyin::SplitJulianDate longyearbyen_start = split_jd(jd_ut(2024, 6, 20, 22.0));
    expect_status(
        moon_visibility_search_rise_set_ut(
            &longyearbyen,
            longyearbyen_start,
            longyearbyen_start + 1.0,
            TAIYIN_MOON_VISIBILITY_EVENT_RISE,
            TAIYIN_MOON_VISIBILITY_LIMB_UPPER,
            TAIYIN_MOON_VISIBILITY_FLAG_REFRACTION,
            &result,
            &diagnostic),
        taiyin::TAIYIN_STATUS_OK,
        "Longyearbyen summer Moon rise no-event",
        failures);
    expect_equal(result.altitude_state, TAIYIN_VISIBILITY_ALTITUDE_STATE_ALWAYS_BELOW, "Longyearbyen Moon always below", failures);
    expect_true(result.max_residual_rad < 0.0, "Longyearbyen Moon max negative", failures);

    NativeCalcContext tromso = make_context(18.9553, 69.6492, 10.0);
    const taiyin::SplitJulianDate tromso_start = split_jd(jd_ut(2024, 6, 20, 22.0));
    expect_status(
        moon_visibility_search_rise_set_ut(
            &tromso,
            tromso_start,
            tromso_start + 1.0,
            TAIYIN_MOON_VISIBILITY_EVENT_SET,
            TAIYIN_MOON_VISIBILITY_LIMB_UPPER,
            TAIYIN_MOON_VISIBILITY_FLAG_REFRACTION,
            &result,
            &diagnostic),
        taiyin::TAIYIN_STATUS_OK,
        "Tromso summer Moon set no-event",
        failures);
    expect_equal(result.altitude_state, TAIYIN_VISIBILITY_ALTITUDE_STATE_ALWAYS_BELOW, "Tromso Moon always below", failures);
    expect_true(result.max_residual_rad < 0.0, "Tromso Moon max negative", failures);
}

void test_transit_oracles(int* failures) {
    using namespace taiyin::runtime;

    const TransitOracle oracles[] = {
        {"denver_moon_upper_transit", -104.9903, 39.7392, 1609.0, jd_ut(2024, 4, 8, 6.0), TAIYIN_MOON_VISIBILITY_EVENT_UPPER_TRANSIT, 2460409.293405554257},
        {"denver_moon_lower_transit", -104.9903, 39.7392, 1609.0, jd_ut(2024, 4, 8, 6.0), TAIYIN_MOON_VISIBILITY_EVENT_LOWER_TRANSIT, 2460408.775522538461},
        {"longyearbyen_winter_moon_upper_transit", 15.6333, 78.2232, 10.0, jd_ut(2024, 12, 20, 23.0), TAIYIN_MOON_VISIBILITY_EVENT_UPPER_TRANSIT, 2460665.657549926080},
        {"longyearbyen_winter_moon_lower_transit", 15.6333, 78.2232, 10.0, jd_ut(2024, 12, 20, 23.0), TAIYIN_MOON_VISIBILITY_EVENT_LOWER_TRANSIT, 2460666.171696477570},
        {"tromso_winter_moon_upper_transit", 18.9553, 69.6492, 10.0, jd_ut(2024, 12, 20, 23.0), TAIYIN_MOON_VISIBILITY_EVENT_UPPER_TRANSIT, 2460665.648057329003},
        {"tromso_winter_moon_lower_transit", 18.9553, 69.6492, 10.0, jd_ut(2024, 12, 20, 23.0), TAIYIN_MOON_VISIBILITY_EVENT_LOWER_TRANSIT, 2460666.162210747600},
    };

    EphemerisEvalDiagnostic diagnostic;
    VisibilityAltitudeSearchResult result;
    for (const TransitOracle& oracle : oracles) {
        NativeCalcContext context = make_context(oracle.longitude_deg, oracle.latitude_deg, oracle.height_m);
        expect_status(
            moon_visibility_search_transit_ut(
                &context,
                split_jd(oracle.start_jd_ut),
                split_jd(oracle.start_jd_ut + 1.0),
                oracle.event_kind,
                &result,
                &diagnostic),
            taiyin::TAIYIN_STATUS_OK,
            oracle.label,
            failures);
        expect_equal(result.altitude_state, TAIYIN_VISIBILITY_ALTITUDE_STATE_CROSSES, oracle.label, failures);
        expect_near(result.jd_ut, oracle.swiss_jd_ut, 2.0 / 86400.0, oracle.label, failures);
    }
}

void test_invalid_arguments_and_atmosphere(int* failures) {
    using namespace taiyin::runtime;

    NativeCalcContext context = make_context(-104.9903, 39.7392, 1609.0);
    const taiyin::SplitJulianDate start = split_jd(jd_ut(2024, 4, 8, 6.0));
    VisibilityAltitudeSearchResult result;
    EphemerisEvalDiagnostic diagnostic;

    expect_status(
        moon_visibility_search_rise_set_ut(
            0,
            start,
            start + 1.0,
            TAIYIN_MOON_VISIBILITY_EVENT_RISE,
            TAIYIN_MOON_VISIBILITY_LIMB_UPPER,
            0u,
            &result,
            &diagnostic),
        taiyin::TAIYIN_ERROR_INVALID_ARGUMENT,
        "null Moon rise context",
        failures);
    expect_status(
        moon_visibility_search_rise_set_ut(
            &context,
            start,
            start + 1.0,
            TAIYIN_MOON_VISIBILITY_EVENT_RISE,
            TAIYIN_MOON_VISIBILITY_LIMB_UPPER,
            0u,
            0,
            &diagnostic),
        taiyin::TAIYIN_ERROR_INVALID_ARGUMENT,
        "null Moon rise result",
        failures);
    expect_status(
        moon_visibility_search_rise_set_ut(
            &context,
            split_jd(std::nan("")),
            start + 1.0,
            TAIYIN_MOON_VISIBILITY_EVENT_RISE,
            TAIYIN_MOON_VISIBILITY_LIMB_UPPER,
            0u,
            &result,
            &diagnostic),
        taiyin::TAIYIN_ERROR_INVALID_ARGUMENT,
        "invalid Moon rise start JD",
        failures);
    expect_status(
        moon_visibility_search_rise_set_ut(
            &context,
            start,
            start,
            TAIYIN_MOON_VISIBILITY_EVENT_RISE,
            TAIYIN_MOON_VISIBILITY_LIMB_UPPER,
            0u,
            &result,
            &diagnostic),
        taiyin::TAIYIN_ERROR_INVALID_ARGUMENT,
        "invalid Moon rise JD range",
        failures);
    expect_status(
        moon_visibility_search_rise_set_ut(
            &context,
            start,
            start + 1.0,
            99,
            TAIYIN_MOON_VISIBILITY_LIMB_UPPER,
            0u,
            &result,
            &diagnostic),
        taiyin::TAIYIN_ERROR_INVALID_ARGUMENT,
        "invalid Moon event kind",
        failures);
    expect_status(
        moon_visibility_search_rise_set_ut(
            &context,
            start,
            start + 1.0,
            TAIYIN_MOON_VISIBILITY_EVENT_RISE,
            99,
            0u,
            &result,
            &diagnostic),
        taiyin::TAIYIN_ERROR_INVALID_ARGUMENT,
        "invalid Moon limb kind",
        failures);
    expect_status(
        moon_visibility_search_transit_ut(
            &context,
            start,
            start + 1.0,
            99,
            &result,
            &diagnostic),
        taiyin::TAIYIN_ERROR_INVALID_ARGUMENT,
        "invalid Moon transit kind",
        failures);

    NativeCalcContext no_atmosphere = make_context(-104.9903, 39.7392, 1609.0, false);
    expect_status(
        moon_visibility_search_rise_set_ut(
            &no_atmosphere,
            start,
            start + 1.0,
            TAIYIN_MOON_VISIBILITY_EVENT_RISE,
            TAIYIN_MOON_VISIBILITY_LIMB_UPPER,
            TAIYIN_MOON_VISIBILITY_FLAG_REFRACTION,
            &result,
            &diagnostic),
        taiyin::TAIYIN_ERROR_INVALID_ARGUMENT,
        "Moon upper limb refraction requires atmosphere",
        failures);
}

}  // namespace

int main() {
    int failures = 0;
    if (initialize_runtime(&failures)) {
        test_denver_oracles(&failures);
        test_polar_no_event_oracles(&failures);
        test_transit_oracles(&failures);
        test_invalid_arguments_and_atmosphere(&failures);
    }
    if (failures != 0) {
        std::cerr << failures << " Moon visibility checks failed\n";
        return 1;
    }
    return 0;
}
