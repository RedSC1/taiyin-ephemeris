#include "runtime/visibility/solar_visibility_internal.h"
#include "runtime/apparent/fast_apparent.h"
#include "runtime/visibility/visibility_math_internal.h"

#include "taiyin/body_id.h"
#include "taiyin/angle.h"
#include "taiyin/runtime/native_context.h"
#include "taiyin/runtime/runtime.h"
#include "taiyin/status.h"
#include "taiyin/time.h"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
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
    taiyin::split_julian_date_from_double(jd, &out);
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

struct DenverRiseSetOracle {
    const char* label;
    int event_kind;
    int limb_kind;
    uint32_t flags;
    double horizon_altitude_rad;
    double swiss_jd_ut;
    double tolerance_seconds;
};

struct DenverTwilightOracle {
    const char* label;
    int event_kind;
    int twilight_kind;
    double swiss_jd_ut;
};

void test_denver_swiss_oracles(int* failures) {
    using namespace taiyin::runtime;

    NativeCalcContext context = make_context(-104.9903, 39.7392, 1609.0);
    const taiyin::SplitJulianDate start = split_jd(jd_ut(2024, 4, 8, 6.0));
    const taiyin::SplitJulianDate end = start + 1.0;
    EphemerisEvalDiagnostic diagnostic;
    VisibilityAltitudeSearchResult result;

    const DenverRiseSetOracle rise_set_oracles[] = {
        {"rise_default_upper_limb_refraction", TAIYIN_SOLAR_VISIBILITY_EVENT_RISE, TAIYIN_SOLAR_VISIBILITY_LIMB_UPPER, TAIYIN_SOLAR_VISIBILITY_FLAG_REFRACTION, 0.0, 2460409.022335537709, 2.0},
        {"set_default_upper_limb_refraction", TAIYIN_SOLAR_VISIBILITY_EVENT_SET, TAIYIN_SOLAR_VISIBILITY_LIMB_UPPER, TAIYIN_SOLAR_VISIBILITY_FLAG_REFRACTION, 0.0, 2460409.563677349128, 2.0},
        {"rise_no_refraction", TAIYIN_SOLAR_VISIBILITY_EVENT_RISE, TAIYIN_SOLAR_VISIBILITY_LIMB_UPPER, 0u, 0.0, 2460409.024388565216, 0.25},
        {"set_no_refraction", TAIYIN_SOLAR_VISIBILITY_EVENT_SET, TAIYIN_SOLAR_VISIBILITY_LIMB_UPPER, 0u, 0.0, 2460409.561618985143, 0.25},
        {"rise_disc_center_refraction", TAIYIN_SOLAR_VISIBILITY_EVENT_RISE, TAIYIN_SOLAR_VISIBILITY_LIMB_CENTER, TAIYIN_SOLAR_VISIBILITY_FLAG_REFRACTION, 0.0, 2460409.023311978672, 2.0},
        {"set_disc_center_refraction", TAIYIN_SOLAR_VISIBILITY_EVENT_SET, TAIYIN_SOLAR_VISIBILITY_LIMB_CENTER, TAIYIN_SOLAR_VISIBILITY_FLAG_REFRACTION, 0.0, 2460409.562698507681, 2.0},
        {"rise_lower_limb_refraction", TAIYIN_SOLAR_VISIBILITY_EVENT_RISE, TAIYIN_SOLAR_VISIBILITY_LIMB_LOWER, TAIYIN_SOLAR_VISIBILITY_FLAG_REFRACTION, 0.0, 2460409.024287729058, 2.0},
        {"set_lower_limb_refraction", TAIYIN_SOLAR_VISIBILITY_EVENT_SET, TAIYIN_SOLAR_VISIBILITY_LIMB_LOWER, TAIYIN_SOLAR_VISIBILITY_FLAG_REFRACTION, 0.0, 2460409.561720380560, 2.0},
        {"rise_lower_limb_no_refraction", TAIYIN_SOLAR_VISIBILITY_EVENT_RISE, TAIYIN_SOLAR_VISIBILITY_LIMB_LOWER, 0u, 0.0, 2460409.026337929536, 0.25},
        {"set_lower_limb_no_refraction", TAIYIN_SOLAR_VISIBILITY_EVENT_SET, TAIYIN_SOLAR_VISIBILITY_LIMB_LOWER, 0u, 0.0, 2460409.559664948378, 0.25},
        {"rise_fixed_disc_upper_no_refraction", TAIYIN_SOLAR_VISIBILITY_EVENT_RISE, TAIYIN_SOLAR_VISIBILITY_LIMB_UPPER, TAIYIN_SOLAR_VISIBILITY_FLAG_FIXED_DISC_SIZE, 0.0, 2460409.024387161247, 0.25},
        {"set_fixed_disc_upper_no_refraction", TAIYIN_SOLAR_VISIBILITY_EVENT_SET, TAIYIN_SOLAR_VISIBILITY_LIMB_UPPER, TAIYIN_SOLAR_VISIBILITY_FLAG_FIXED_DISC_SIZE, 0.0, 2460409.561620542314, 0.25},
        {"rise_fixed_disc_lower_no_refraction", TAIYIN_SOLAR_VISIBILITY_EVENT_RISE, TAIYIN_SOLAR_VISIBILITY_LIMB_LOWER, TAIYIN_SOLAR_VISIBILITY_FLAG_FIXED_DISC_SIZE, 0.0, 2460409.026339331642, 0.25},
        {"set_fixed_disc_lower_no_refraction", TAIYIN_SOLAR_VISIBILITY_EVENT_SET, TAIYIN_SOLAR_VISIBILITY_LIMB_LOWER, TAIYIN_SOLAR_VISIBILITY_FLAG_FIXED_DISC_SIZE, 0.0, 2460409.559663394000, 0.25},
        {"rise_true_horizon_zero", TAIYIN_SOLAR_VISIBILITY_EVENT_RISE, TAIYIN_SOLAR_VISIBILITY_LIMB_UPPER, TAIYIN_SOLAR_VISIBILITY_FLAG_REFRACTION, 0.0, 2460409.022335450165, 2.0},
        {"set_true_horizon_zero", TAIYIN_SOLAR_VISIBILITY_EVENT_SET, TAIYIN_SOLAR_VISIBILITY_LIMB_UPPER, TAIYIN_SOLAR_VISIBILITY_FLAG_REFRACTION, 0.0, 2460409.563677391969, 2.0},
        {"rise_true_horizon_plus_1deg", TAIYIN_SOLAR_VISIBILITY_EVENT_RISE, TAIYIN_SOLAR_VISIBILITY_LIMB_UPPER, TAIYIN_SOLAR_VISIBILITY_FLAG_REFRACTION, taiyin::TAIYIN_DEG_TO_RAD, 2460409.026595990174, 2.0},
        {"set_true_horizon_plus_1deg", TAIYIN_SOLAR_VISIBILITY_EVENT_SET, TAIYIN_SOLAR_VISIBILITY_LIMB_UPPER, TAIYIN_SOLAR_VISIBILITY_FLAG_REFRACTION, taiyin::TAIYIN_DEG_TO_RAD, 2460409.5594058847, 2.0},
        {"rise_true_horizon_minus_0_5deg_no_refraction", TAIYIN_SOLAR_VISIBILITY_EVENT_RISE, TAIYIN_SOLAR_VISIBILITY_LIMB_UPPER, 0u, -0.5 * taiyin::TAIYIN_DEG_TO_RAD, 2460409.0225552721, 0.25},
        {"set_true_horizon_minus_0_5deg_no_refraction", TAIYIN_SOLAR_VISIBILITY_EVENT_SET, TAIYIN_SOLAR_VISIBILITY_LIMB_UPPER, 0u, -0.5 * taiyin::TAIYIN_DEG_TO_RAD, 2460409.5634570932, 0.25},
    };
    for (const DenverRiseSetOracle& oracle : rise_set_oracles) {
        const taiyin::Status st = solar_visibility_search_rise_set_at_horizon_ut(
            &context,
            start,
            end,
            oracle.event_kind,
            oracle.limb_kind,
            oracle.horizon_altitude_rad,
            oracle.flags,
            &result,
            &diagnostic);
        expect_status(st, taiyin::TAIYIN_STATUS_OK, oracle.label, failures);
        if (st != taiyin::TAIYIN_STATUS_OK) continue;
        expect_crossing_against_swiss(
            result,
            oracle.swiss_jd_ut,
            oracle.tolerance_seconds,
            oracle.event_kind == TAIYIN_SOLAR_VISIBILITY_EVENT_RISE
                ? TAIYIN_VISIBILITY_CROSSING_RISING
                : TAIYIN_VISIBILITY_CROSSING_SETTING,
            oracle.label,
            failures);
    }

    const DenverTwilightOracle twilight_oracles[] = {
        {"civil_dawn", TAIYIN_SOLAR_VISIBILITY_EVENT_RISE, TAIYIN_SOLAR_VISIBILITY_TWILIGHT_CIVIL, 2460409.0031926637},
        {"civil_dusk", TAIYIN_SOLAR_VISIBILITY_EVENT_SET, TAIYIN_SOLAR_VISIBILITY_TWILIGHT_CIVIL, 2460409.582876127},
        {"nautical_dawn", TAIYIN_SOLAR_VISIBILITY_EVENT_RISE, TAIYIN_SOLAR_VISIBILITY_TWILIGHT_NAUTICAL, 2460408.98045977},
        {"nautical_dusk", TAIYIN_SOLAR_VISIBILITY_EVENT_SET, TAIYIN_SOLAR_VISIBILITY_TWILIGHT_NAUTICAL, 2460409.605691832025},
        {"astronomical_dawn", TAIYIN_SOLAR_VISIBILITY_EVENT_RISE, TAIYIN_SOLAR_VISIBILITY_TWILIGHT_ASTRONOMICAL, 2460408.956840595},
        {"astronomical_dusk", TAIYIN_SOLAR_VISIBILITY_EVENT_SET, TAIYIN_SOLAR_VISIBILITY_TWILIGHT_ASTRONOMICAL, 2460409.629424176},
    };
    for (const DenverTwilightOracle& oracle : twilight_oracles) {
        const taiyin::Status st = solar_visibility_search_twilight_ut(
            &context,
            start,
            end,
            oracle.event_kind,
            oracle.twilight_kind,
            &result,
            &diagnostic);
        expect_status(st, taiyin::TAIYIN_STATUS_OK, oracle.label, failures);
        if (st != taiyin::TAIYIN_STATUS_OK) continue;
        expect_crossing_against_swiss(
            result,
            oracle.swiss_jd_ut,
            0.25,
            oracle.event_kind == TAIYIN_SOLAR_VISIBILITY_EVENT_RISE
                ? TAIYIN_VISIBILITY_CROSSING_RISING
                : TAIYIN_VISIBILITY_CROSSING_SETTING,
            oracle.label,
            failures);
    }
}

struct PolarNoEventOracle {
    const char* label;
    bool twilight;
    int event_kind;
    int twilight_kind;
    int expected_state;
};

struct PolarCrossingOracle {
    const char* label;
    int event_kind;
    int twilight_kind;
    double swiss_jd_ut;
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

void test_polar_oracles(int* failures) {
    using namespace taiyin::runtime;

    NativeCalcContext longyearbyen = make_context(15.6333, 78.2232, 10.0);
    EphemerisEvalDiagnostic diagnostic;
    VisibilityAltitudeSearchResult result;

    const taiyin::SplitJulianDate summer_start = split_jd(jd_ut(2024, 6, 20, 22.0));
    const PolarNoEventOracle summer_no_events[] = {
        {"longyearbyen_summer_rise", false, TAIYIN_SOLAR_VISIBILITY_EVENT_RISE, 0, TAIYIN_VISIBILITY_ALTITUDE_STATE_ALWAYS_ABOVE},
        {"longyearbyen_summer_set", false, TAIYIN_SOLAR_VISIBILITY_EVENT_SET, 0, TAIYIN_VISIBILITY_ALTITUDE_STATE_ALWAYS_ABOVE},
        {"longyearbyen_summer_civil_dawn", true, TAIYIN_SOLAR_VISIBILITY_EVENT_RISE, TAIYIN_SOLAR_VISIBILITY_TWILIGHT_CIVIL, TAIYIN_VISIBILITY_ALTITUDE_STATE_ALWAYS_ABOVE},
        {"longyearbyen_summer_civil_dusk", true, TAIYIN_SOLAR_VISIBILITY_EVENT_SET, TAIYIN_SOLAR_VISIBILITY_TWILIGHT_CIVIL, TAIYIN_VISIBILITY_ALTITUDE_STATE_ALWAYS_ABOVE},
        {"longyearbyen_summer_nautical_dawn", true, TAIYIN_SOLAR_VISIBILITY_EVENT_RISE, TAIYIN_SOLAR_VISIBILITY_TWILIGHT_NAUTICAL, TAIYIN_VISIBILITY_ALTITUDE_STATE_ALWAYS_ABOVE},
        {"longyearbyen_summer_nautical_dusk", true, TAIYIN_SOLAR_VISIBILITY_EVENT_SET, TAIYIN_SOLAR_VISIBILITY_TWILIGHT_NAUTICAL, TAIYIN_VISIBILITY_ALTITUDE_STATE_ALWAYS_ABOVE},
        {"longyearbyen_summer_astronomical_dawn", true, TAIYIN_SOLAR_VISIBILITY_EVENT_RISE, TAIYIN_SOLAR_VISIBILITY_TWILIGHT_ASTRONOMICAL, TAIYIN_VISIBILITY_ALTITUDE_STATE_ALWAYS_ABOVE},
        {"longyearbyen_summer_astronomical_dusk", true, TAIYIN_SOLAR_VISIBILITY_EVENT_SET, TAIYIN_SOLAR_VISIBILITY_TWILIGHT_ASTRONOMICAL, TAIYIN_VISIBILITY_ALTITUDE_STATE_ALWAYS_ABOVE},
    };
    for (const PolarNoEventOracle& oracle : summer_no_events) {
        const taiyin::Status st = oracle.twilight
            ? solar_visibility_search_twilight_ut(
                &longyearbyen, summer_start, summer_start + 1.0, oracle.event_kind, oracle.twilight_kind, &result, &diagnostic)
            : solar_visibility_search_rise_set_ut(
                &longyearbyen,
                summer_start,
                summer_start + 1.0,
                oracle.event_kind,
                TAIYIN_SOLAR_VISIBILITY_LIMB_UPPER,
                TAIYIN_SOLAR_VISIBILITY_FLAG_REFRACTION,
                &result,
                &diagnostic);
        expect_status(st, taiyin::TAIYIN_STATUS_OK, oracle.label, failures);
        if (st != taiyin::TAIYIN_STATUS_OK) continue;
        expect_equal(result.altitude_state, oracle.expected_state, oracle.label, failures);
    }

    const taiyin::SplitJulianDate winter_start = split_jd(jd_ut(2024, 12, 20, 23.0));
    const PolarNoEventOracle winter_no_events[] = {
        {"longyearbyen_winter_rise", false, TAIYIN_SOLAR_VISIBILITY_EVENT_RISE, 0, TAIYIN_VISIBILITY_ALTITUDE_STATE_ALWAYS_BELOW},
        {"longyearbyen_winter_set", false, TAIYIN_SOLAR_VISIBILITY_EVENT_SET, 0, TAIYIN_VISIBILITY_ALTITUDE_STATE_ALWAYS_BELOW},
        {"longyearbyen_winter_civil_dawn", true, TAIYIN_SOLAR_VISIBILITY_EVENT_RISE, TAIYIN_SOLAR_VISIBILITY_TWILIGHT_CIVIL, TAIYIN_VISIBILITY_ALTITUDE_STATE_ALWAYS_BELOW},
        {"longyearbyen_winter_civil_dusk", true, TAIYIN_SOLAR_VISIBILITY_EVENT_SET, TAIYIN_SOLAR_VISIBILITY_TWILIGHT_CIVIL, TAIYIN_VISIBILITY_ALTITUDE_STATE_ALWAYS_BELOW},
    };
    for (const PolarNoEventOracle& oracle : winter_no_events) {
        const taiyin::Status st = oracle.twilight
            ? solar_visibility_search_twilight_ut(
                &longyearbyen, winter_start, winter_start + 1.0, oracle.event_kind, oracle.twilight_kind, &result, &diagnostic)
            : solar_visibility_search_rise_set_ut(
                &longyearbyen,
                winter_start,
                winter_start + 1.0,
                oracle.event_kind,
                TAIYIN_SOLAR_VISIBILITY_LIMB_UPPER,
                TAIYIN_SOLAR_VISIBILITY_FLAG_REFRACTION,
                &result,
                &diagnostic);
        expect_status(st, taiyin::TAIYIN_STATUS_OK, oracle.label, failures);
        if (st != taiyin::TAIYIN_STATUS_OK) continue;
        expect_equal(result.altitude_state, oracle.expected_state, oracle.label, failures);
    }

    const PolarCrossingOracle winter_crossings[] = {
        {"longyearbyen_winter_nautical_dawn", TAIYIN_SOLAR_VISIBILITY_EVENT_RISE, TAIYIN_SOLAR_VISIBILITY_TWILIGHT_NAUTICAL, 2460665.915852023754},
        {"longyearbyen_winter_nautical_dusk", TAIYIN_SOLAR_VISIBILITY_EVENT_SET, TAIYIN_SOLAR_VISIBILITY_TWILIGHT_NAUTICAL, 2460665.994893949479},
        {"longyearbyen_winter_astronomical_dawn", TAIYIN_SOLAR_VISIBILITY_EVENT_RISE, TAIYIN_SOLAR_VISIBILITY_TWILIGHT_ASTRONOMICAL, 2460665.775949399453},
        {"longyearbyen_winter_astronomical_dusk", TAIYIN_SOLAR_VISIBILITY_EVENT_SET, TAIYIN_SOLAR_VISIBILITY_TWILIGHT_ASTRONOMICAL, 2460666.134796859},
    };
    for (const PolarCrossingOracle& oracle : winter_crossings) {
        const taiyin::Status st = solar_visibility_search_twilight_ut(
            &longyearbyen,
            winter_start,
            winter_start + 1.0,
            oracle.event_kind,
            oracle.twilight_kind,
            &result,
            &diagnostic);
        expect_status(st, taiyin::TAIYIN_STATUS_OK, oracle.label, failures);
        if (st != taiyin::TAIYIN_STATUS_OK) continue;
        expect_crossing_against_swiss(
            result,
            oracle.swiss_jd_ut,
            0.25,
            oracle.event_kind == TAIYIN_SOLAR_VISIBILITY_EVENT_RISE
                ? TAIYIN_VISIBILITY_CROSSING_RISING
                : TAIYIN_VISIBILITY_CROSSING_SETTING,
            oracle.label,
            failures);
    }
}

void test_invalid_arguments_and_atmosphere(int* failures) {
    using namespace taiyin::runtime;

    NativeCalcContext context = make_context(-104.9903, 39.7392, 1609.0);
    const taiyin::SplitJulianDate start = split_jd(jd_ut(2024, 4, 8, 6.0));
    VisibilityAltitudeSearchResult result;
    EphemerisEvalDiagnostic diagnostic;

    expect_status(
        solar_visibility_search_rise_set_ut(
            &context,
            start,
            start + 1.0,
            99,
            TAIYIN_SOLAR_VISIBILITY_LIMB_UPPER,
            0u,
            &result,
            &diagnostic),
        taiyin::TAIYIN_ERROR_INVALID_ARGUMENT,
        "invalid solar event kind",
        failures);
    expect_status(
        solar_visibility_search_rise_set_ut(
            &context,
            start,
            start + 1.0,
            TAIYIN_SOLAR_VISIBILITY_EVENT_RISE,
            99,
            0u,
            &result,
            &diagnostic),
        taiyin::TAIYIN_ERROR_INVALID_ARGUMENT,
        "invalid solar limb kind",
        failures);
    expect_status(
        solar_visibility_search_twilight_ut(
            &context,
            start,
            start + 1.0,
            TAIYIN_SOLAR_VISIBILITY_EVENT_RISE,
            99,
            &result,
            &diagnostic),
        taiyin::TAIYIN_ERROR_INVALID_ARGUMENT,
        "invalid twilight kind",
        failures);
    expect_status(
        solar_visibility_search_rise_set_at_horizon_ut(
            &context,
            start,
            start + 1.0,
            TAIYIN_SOLAR_VISIBILITY_EVENT_RISE,
            TAIYIN_SOLAR_VISIBILITY_LIMB_UPPER,
            NAN,
            0u,
            &result,
            &diagnostic),
        taiyin::TAIYIN_ERROR_INVALID_ARGUMENT,
        "invalid horizon altitude",
        failures);

    SolarRiseSetFastResult rise_set_fast;
    expect_status(
        compute_solar_rise_set_fast_tt(
            &context,
            start,
            -104.9903,
            91.0,
            1609.0,
            TAIYIN_SOLAR_VISIBILITY_LIMB_CENTER,
            0.0,
            TAIYIN_SOLAR_VISIBILITY_FLAG_NO_REFRACTION,
            &rise_set_fast,
            &diagnostic),
        taiyin::TAIYIN_ERROR_INVALID_ARGUMENT,
        "rise/set fast rejects latitude above north pole",
        failures);
    expect_status(
        compute_solar_rise_set_fast_tt(
            &context,
            start,
            -104.9903,
            -91.0,
            1609.0,
            TAIYIN_SOLAR_VISIBILITY_LIMB_CENTER,
            0.0,
            TAIYIN_SOLAR_VISIBILITY_FLAG_NO_REFRACTION,
            &rise_set_fast,
            &diagnostic),
        taiyin::TAIYIN_ERROR_INVALID_ARGUMENT,
        "rise/set fast rejects latitude below south pole",
        failures);

    SolarTransitFastResult transit_fast;
    expect_status(
        compute_solar_transit_fast_tt(
            &context,
            start,
            -104.9903,
            91.0,
            1609.0,
            &transit_fast,
            &diagnostic),
        taiyin::TAIYIN_ERROR_INVALID_ARGUMENT,
        "transit fast rejects latitude above north pole",
        failures);
    expect_status(
        compute_solar_transit_fast_tt(
            &context,
            start,
            -104.9903,
            -91.0,
            1609.0,
            &transit_fast,
            &diagnostic),
        taiyin::TAIYIN_ERROR_INVALID_ARGUMENT,
        "transit fast rejects latitude below south pole",
        failures);

    NativeCalcContext no_atmosphere = make_context(-104.9903, 39.7392, 1609.0, false);
    expect_status(
        solar_visibility_search_rise_set_ut(
            &no_atmosphere,
            start,
            start + 1.0,
            TAIYIN_SOLAR_VISIBILITY_EVENT_RISE,
            TAIYIN_SOLAR_VISIBILITY_LIMB_UPPER,
            TAIYIN_SOLAR_VISIBILITY_FLAG_REFRACTION,
            &result,
            &diagnostic),
        taiyin::TAIYIN_ERROR_INVALID_ARGUMENT,
        "upper limb refraction requires atmosphere",
        failures);
    expect_status(
        solar_visibility_search_twilight_ut(
            &no_atmosphere,
            start,
            start + 1.0,
            TAIYIN_SOLAR_VISIBILITY_EVENT_RISE,
            TAIYIN_SOLAR_VISIBILITY_TWILIGHT_CIVIL,
            &result,
            &diagnostic),
        taiyin::TAIYIN_STATUS_OK,
        "twilight does not require atmosphere",
        failures);
}

void test_fast_rise_set_matches_precise(int* failures) {
    using namespace taiyin::runtime;

    NativeCalcContext context = make_context(-104.9903, 39.7392, 1609.0);
    EphemerisEvalDiagnostic diagnostic;
    const double start_jd_ut = jd_ut(2024, 4, 8, 6.0);
    const taiyin::SplitJulianDate start_ut = split_jd(start_jd_ut);
    const taiyin::SplitJulianDate end_ut = split_jd(start_jd_ut + 1.0);
    const double center_jd_tt = taiyin::ut1_to_tt_jd(
        start_jd_ut + 0.5, taiyin::estimated_delta_t_seconds_from_ut1_jd(start_jd_ut + 0.5));
    const taiyin::SplitJulianDate center_tt = split_jd(center_jd_tt);

    struct FastOracle {
        const char* label;
        int limb_kind;
        uint64_t flags;
        double rise_jd_ut;
        double set_jd_ut;
        double rise_tolerance_seconds;
        double set_tolerance_seconds;
    };
    const FastOracle oracles[] = {
        {"fast_upper_limb_refraction", TAIYIN_SOLAR_VISIBILITY_LIMB_UPPER, TAIYIN_SOLAR_VISIBILITY_FLAG_REFRACTION, 2460409.022335537709, 2460409.563677349128, 8.0, 10.0},
        {"fast_upper_limb_no_refraction", TAIYIN_SOLAR_VISIBILITY_LIMB_UPPER, TAIYIN_SOLAR_VISIBILITY_FLAG_NO_REFRACTION, 2460409.024388565216, 2460409.561618985143, 0.25, 0.25},
        {"fast_center_no_refraction", TAIYIN_SOLAR_VISIBILITY_LIMB_CENTER, TAIYIN_SOLAR_VISIBILITY_FLAG_NO_REFRACTION, 2460409.025327078679, 2460409.562713850470, 4.0, 200.0},
        {"fast_lower_limb_no_refraction", TAIYIN_SOLAR_VISIBILITY_LIMB_LOWER, TAIYIN_SOLAR_VISIBILITY_FLAG_NO_REFRACTION, 2460409.026337929536, 2460409.559664948378, 0.25, 0.25},
        {"fast_fixed_disc_upper_no_refraction", TAIYIN_SOLAR_VISIBILITY_LIMB_UPPER, TAIYIN_SOLAR_VISIBILITY_FLAG_FIXED_DISC_SIZE | TAIYIN_SOLAR_VISIBILITY_FLAG_NO_REFRACTION, 2460409.024387161247, 2460409.561620542314, 0.25, 0.25},
    };

    for (const FastOracle& oracle : oracles) {
        SolarRiseSetFastResult result;
        const taiyin::Status st = compute_solar_rise_set_fast_tt(
            &context,
            center_tt,
            -104.9903,
            39.7392,
            1609.0,
            oracle.limb_kind,
            0.0,
            oracle.flags,
            &result,
            &diagnostic);
        expect_status(st, taiyin::TAIYIN_STATUS_OK, oracle.label, failures);
        if (st != taiyin::TAIYIN_STATUS_OK) continue;
        expect_equal(result.altitude_state, TAIYIN_VISIBILITY_ALTITUDE_STATE_CROSSES, oracle.label, failures);
        const double dt = taiyin::estimated_delta_t_seconds_from_tt_jd(center_jd_tt);
        expect_near(
            taiyin::tt_to_ut1_jd(taiyin::split_julian_date_to_double(result.rise_jd_tt), dt),
            oracle.rise_jd_ut,
            oracle.rise_tolerance_seconds / 86400.0,
            oracle.label,
            failures);
        expect_near(
            taiyin::tt_to_ut1_jd(taiyin::split_julian_date_to_double(result.set_jd_tt), dt),
            oracle.set_jd_ut,
            oracle.set_tolerance_seconds / 86400.0,
            oracle.label,
            failures);
    }

    // Refraction mode also matches the precise path for the center limb.
    SolarRiseSetFastResult center_refr;
    const taiyin::Status center_st = compute_solar_rise_set_fast_tt(
        &context,
        center_tt,
        -104.9903,
        39.7392,
        1609.0,
        TAIYIN_SOLAR_VISIBILITY_LIMB_CENTER,
        0.0,
        TAIYIN_SOLAR_VISIBILITY_FLAG_REFRACTION,
        &center_refr,
        &diagnostic);
    expect_status(center_st, taiyin::TAIYIN_STATUS_OK, "fast_center_refraction", failures);
    if (center_st == taiyin::TAIYIN_STATUS_OK) {
        const double dt = taiyin::estimated_delta_t_seconds_from_tt_jd(center_jd_tt);
        expect_near(
            taiyin::tt_to_ut1_jd(taiyin::split_julian_date_to_double(center_refr.rise_jd_tt), dt),
            2460409.023311978672,
            8.0 / 86400.0,
            "fast_center_refraction rise",
            failures);
        expect_near(
            taiyin::tt_to_ut1_jd(taiyin::split_julian_date_to_double(center_refr.set_jd_tt), dt),
            2460409.562698507681,
            10.0 / 86400.0,
            "fast_center_refraction set",
            failures);
    }

    // Public default flags select refraction, matching the precise visibility API.
    SolarRiseSetFastResult default_result;
    SolarRiseSetFastResult explicit_refraction_result;
    const taiyin::Status default_st = compute_solar_rise_set_fast_tt(
        &context, center_tt, -104.9903, 39.7392, 1609.0,
        TAIYIN_SOLAR_VISIBILITY_LIMB_UPPER, 0.0, 0u,
        &default_result, &diagnostic);
    const taiyin::Status explicit_refraction_st = compute_solar_rise_set_fast_tt(
        &context, center_tt, -104.9903, 39.7392, 1609.0,
        TAIYIN_SOLAR_VISIBILITY_LIMB_UPPER, 0.0,
        TAIYIN_SOLAR_VISIBILITY_FLAG_REFRACTION,
        &explicit_refraction_result, &diagnostic);
    expect_status(default_st, taiyin::TAIYIN_STATUS_OK, "fast default flags", failures);
    expect_status(explicit_refraction_st, taiyin::TAIYIN_STATUS_OK, "fast explicit refraction", failures);
    if (default_st == taiyin::TAIYIN_STATUS_OK
        && explicit_refraction_st == taiyin::TAIYIN_STATUS_OK) {
        expect_near(
            taiyin::split_julian_date_to_double(default_result.rise_jd_tt),
            taiyin::split_julian_date_to_double(explicit_refraction_result.rise_jd_tt),
            0.0,
            "fast default rise matches explicit refraction",
            failures);
        expect_near(
            taiyin::split_julian_date_to_double(default_result.set_jd_tt),
            taiyin::split_julian_date_to_double(explicit_refraction_result.set_jd_tt),
            0.0,
            "fast default set matches explicit refraction",
            failures);
    }

    // High latitudes use the fallback solver; it must preserve limb and
    // refraction semantics instead of reverting to geometric center altitude.
    NativeCalcContext high_lat_context = make_context(18.9553, 69.6492, 10.0);
    const double high_lat_jd_ut = jd_ut(2024, 4, 15, 12.0);
    const double high_lat_center_jd_tt = taiyin::ut1_to_tt_jd(
        high_lat_jd_ut,
        taiyin::estimated_delta_t_seconds_from_ut1_jd(high_lat_jd_ut));
    SolarRiseSetFastResult high_lat_upper;
    SolarRiseSetFastResult high_lat_center;
    const taiyin::Status high_lat_upper_st = compute_solar_rise_set_fast_tt(
        &high_lat_context, split_jd(high_lat_center_jd_tt), 18.9553, 69.6492, 10.0,
        TAIYIN_SOLAR_VISIBILITY_LIMB_UPPER, 0.0,
        TAIYIN_SOLAR_VISIBILITY_FLAG_REFRACTION,
        &high_lat_upper, &diagnostic);
    const taiyin::Status high_lat_center_st = compute_solar_rise_set_fast_tt(
        &high_lat_context, split_jd(high_lat_center_jd_tt), 18.9553, 69.6492, 10.0,
        TAIYIN_SOLAR_VISIBILITY_LIMB_CENTER, 0.0,
        TAIYIN_SOLAR_VISIBILITY_FLAG_NO_REFRACTION,
        &high_lat_center, &diagnostic);
    expect_status(high_lat_upper_st, taiyin::TAIYIN_STATUS_OK, "fast high-latitude upper refracted", failures);
    expect_status(high_lat_center_st, taiyin::TAIYIN_STATUS_OK, "fast high-latitude geometric center", failures);
    if (high_lat_upper_st == taiyin::TAIYIN_STATUS_OK) {
        expect_equal(
            high_lat_upper.altitude_state,
            TAIYIN_SOLAR_VISIBILITY_ALTITUDE_STATE_CROSSES,
            "fast high-latitude upper refracted crosses",
            failures);
    }
    if (high_lat_center_st == taiyin::TAIYIN_STATUS_OK) {
        expect_equal(
            high_lat_center.altitude_state,
            TAIYIN_SOLAR_VISIBILITY_ALTITUDE_STATE_CROSSES,
            "fast high-latitude geometric center crosses",
            failures);
    }
    if (high_lat_upper_st == taiyin::TAIYIN_STATUS_OK
        && high_lat_center_st == taiyin::TAIYIN_STATUS_OK
        && high_lat_upper.altitude_state == TAIYIN_SOLAR_VISIBILITY_ALTITUDE_STATE_CROSSES
        && high_lat_center.altitude_state == TAIYIN_SOLAR_VISIBILITY_ALTITUDE_STATE_CROSSES) {
        expect_true(
            std::fabs(high_lat_upper.rise_jd_tt - high_lat_center.rise_jd_tt) > 30.0 / 86400.0,
            "fast high-latitude fallback distinguishes upper refracted rise",
            failures);
        expect_true(
            std::fabs(high_lat_upper.set_jd_tt - high_lat_center.set_jd_tt) > 30.0 / 86400.0,
            "fast high-latitude fallback distinguishes upper refracted set",
            failures);
    }

    // Refraction-aware Newton slopes must follow the refracted residual. This
    // shallow lower-limb crossing previously missed the precise root by about
    // 80 seconds when the true-altitude slope was used directly.
    NativeCalcContext shallow_context = make_context(0.0, -64.0, 0.0);
    const double shallow_start_jd_ut = jd_ut(2024, 11, 15, 0.0);
    VisibilityAltitudeSearchResult shallow_precise;
    const taiyin::Status shallow_precise_st = solar_visibility_search_rise_set_at_horizon_ut(
        &shallow_context,
        split_jd(shallow_start_jd_ut),
        split_jd(shallow_start_jd_ut + 1.0),
        TAIYIN_SOLAR_VISIBILITY_EVENT_RISE,
        TAIYIN_SOLAR_VISIBILITY_LIMB_LOWER,
        0.0,
        TAIYIN_SOLAR_VISIBILITY_FLAG_REFRACTION,
        &shallow_precise,
        &diagnostic);
    const double shallow_center_jd_tt = taiyin::ut1_to_tt_jd(
        shallow_start_jd_ut + 0.5,
        taiyin::estimated_delta_t_seconds_from_ut1_jd(shallow_start_jd_ut + 0.5));
    SolarRiseSetFastResult shallow_fast;
    const taiyin::Status shallow_fast_st = compute_solar_rise_set_fast_tt(
        &shallow_context,
        split_jd(shallow_center_jd_tt),
        0.0,
        -64.0,
        0.0,
        TAIYIN_SOLAR_VISIBILITY_LIMB_LOWER,
        0.0,
        TAIYIN_SOLAR_VISIBILITY_FLAG_REFRACTION,
        &shallow_fast,
        &diagnostic);
    expect_status(shallow_precise_st, taiyin::TAIYIN_STATUS_OK, "precise shallow refracted rise", failures);
    expect_status(shallow_fast_st, taiyin::TAIYIN_STATUS_OK, "fast shallow refracted rise", failures);
    if (shallow_precise_st == taiyin::TAIYIN_STATUS_OK
        && shallow_fast_st == taiyin::TAIYIN_STATUS_OK) {
        const double shallow_delta_t =
            taiyin::estimated_delta_t_seconds_from_tt_jd(shallow_center_jd_tt);
        const double shallow_fast_jd_ut = taiyin::tt_to_ut1_jd(
            taiyin::split_julian_date_to_double(shallow_fast.rise_jd_tt),
            shallow_delta_t);
        expect_near(
            shallow_fast_jd_ut,
            taiyin::split_julian_date_to_double(shallow_precise.jd_ut),
            5.0 / 86400.0,
            "fast shallow refracted rise matches precise root",
            failures);
    }

    // Argument validation.
    SolarRiseSetFastResult invalid;
    expect_status(
        compute_solar_rise_set_fast_tt(
            &context, center_tt, -104.9903, 39.7392, 1609.0,
            99, 0.0, TAIYIN_SOLAR_VISIBILITY_FLAG_NO_REFRACTION, &invalid, &diagnostic),
        taiyin::TAIYIN_ERROR_INVALID_ARGUMENT,
        "fast rejects unknown limb kind",
        failures);
    expect_status(
        compute_solar_rise_set_fast_tt(
            &context, center_tt, -104.9903, 39.7392, 1609.0,
            TAIYIN_SOLAR_VISIBILITY_LIMB_UPPER, 0.0,
            TAIYIN_SOLAR_VISIBILITY_FLAG_REFRACTION | TAIYIN_SOLAR_VISIBILITY_FLAG_NO_REFRACTION,
            &invalid, &diagnostic),
        taiyin::TAIYIN_ERROR_INVALID_ARGUMENT,
        "fast rejects refraction and no-refraction together",
        failures);
    expect_status(
        compute_solar_rise_set_fast_tt(
            &context, center_tt, -104.9903, 39.7392, 1609.0,
            TAIYIN_SOLAR_VISIBILITY_LIMB_UPPER, 0.0, 1ull << 40, &invalid, &diagnostic),
        taiyin::TAIYIN_ERROR_INVALID_ARGUMENT,
        "fast rejects unknown visibility flag bits",
        failures);

    // Refraction without atmosphere and no standard fallback is invalid.
    NativeCalcContext no_atmosphere = make_context(-104.9903, 39.7392, 1609.0, false);
    expect_status(
        compute_solar_rise_set_fast_tt(
            &no_atmosphere, center_tt, -104.9903, 39.7392, 1609.0,
            TAIYIN_SOLAR_VISIBILITY_LIMB_UPPER, 0.0,
            TAIYIN_SOLAR_VISIBILITY_FLAG_REFRACTION, &invalid, &diagnostic),
        taiyin::TAIYIN_ERROR_INVALID_ARGUMENT,
        "fast refraction requires atmosphere",
        failures);
    expect_status(
        compute_solar_rise_set_fast_tt(
            &no_atmosphere, center_tt, -104.9903, 39.7392, 1609.0,
            TAIYIN_SOLAR_VISIBILITY_LIMB_CENTER, 0.0,
            TAIYIN_SOLAR_VISIBILITY_FLAG_NO_REFRACTION, &invalid, &diagnostic),
        taiyin::TAIYIN_STATUS_OK,
        "fast geometric mode works without atmosphere",
        failures);
    expect_status(
        compute_solar_rise_set_fast_tt(
            &no_atmosphere, center_tt, -104.9903, 39.7392, 1609.0,
            TAIYIN_SOLAR_VISIBILITY_LIMB_CENTER, 0.0,
            TAIYIN_SOLAR_VISIBILITY_FLAG_NO_REFRACTION
                | TAIYIN_SOLAR_VISIBILITY_STRICT_METEOROLOGY,
            &invalid, &diagnostic),
        taiyin::TAIYIN_STATUS_OK,
        "fast geometric strict mode does not require atmosphere",
        failures);

    // Standard fallback uses the explicit fast-solver location, even when the
    // source context has no stored observer location.
    NativeCalcContext explicit_location_context = no_atmosphere;
    explicit_location_context.fields.clear(TAIYIN_NATIVE_FIELD_OBSERVER_LOCATION);
    explicit_location_context.atmosphere_policy_flags =
        TAIYIN_NATIVE_ATMOSPHERE_ALLOW_STANDARD_FALLBACK;
    SolarRiseSetFastResult explicit_location_result;
    expect_status(
        compute_solar_rise_set_fast_tt(
            &explicit_location_context, center_tt, -104.9903, 39.7392, 1609.0,
            TAIYIN_SOLAR_VISIBILITY_LIMB_UPPER, 0.0,
            TAIYIN_SOLAR_VISIBILITY_FLAG_REFRACTION,
            &explicit_location_result, &diagnostic),
        taiyin::TAIYIN_STATUS_OK,
        "fast fallback atmosphere uses explicit location",
        failures);

    // Standard fallback makes non-strict refraction succeed without atmosphere,
    // while strict meteorology still rejects it.
    NativeCalcContext fallback_context = make_context(-104.9903, 39.7392, 1609.0, false);
    fallback_context.atmosphere_policy_flags = TAIYIN_NATIVE_ATMOSPHERE_ALLOW_STANDARD_FALLBACK;
    SolarRiseSetFastResult fallback_result;
    expect_status(
        compute_solar_rise_set_fast_tt(
            &fallback_context, center_tt, -104.9903, 39.7392, 1609.0,
            TAIYIN_SOLAR_VISIBILITY_LIMB_UPPER, 0.0,
            TAIYIN_SOLAR_VISIBILITY_FLAG_REFRACTION, &fallback_result, &diagnostic),
        taiyin::TAIYIN_STATUS_OK,
        "fast refraction uses standard fallback atmosphere",
        failures);
    expect_status(
        compute_solar_rise_set_fast_tt(
            &fallback_context, center_tt, -104.9903, 39.7392, 1609.0,
            TAIYIN_SOLAR_VISIBILITY_LIMB_UPPER, 0.0,
            TAIYIN_SOLAR_VISIBILITY_FLAG_REFRACTION | TAIYIN_SOLAR_VISIBILITY_STRICT_METEOROLOGY,
            &invalid, &diagnostic),
        taiyin::TAIYIN_ERROR_INVALID_ARGUMENT,
        "fast strict meteorology rejects fallback atmosphere",
        failures);

    // Strict meteorology with a real atmosphere matches the non-strict result.
    SolarRiseSetFastResult strict_result;
    SolarRiseSetFastResult nonstrict_result;
    const taiyin::Status strict_st = compute_solar_rise_set_fast_tt(
        &context, center_tt, -104.9903, 39.7392, 1609.0,
        TAIYIN_SOLAR_VISIBILITY_LIMB_UPPER, 0.0,
        TAIYIN_SOLAR_VISIBILITY_FLAG_REFRACTION | TAIYIN_SOLAR_VISIBILITY_STRICT_METEOROLOGY,
        &strict_result, &diagnostic);
    const taiyin::Status nonstrict_st = compute_solar_rise_set_fast_tt(
        &context, center_tt, -104.9903, 39.7392, 1609.0,
        TAIYIN_SOLAR_VISIBILITY_LIMB_UPPER, 0.0,
        TAIYIN_SOLAR_VISIBILITY_FLAG_REFRACTION,
        &nonstrict_result, &diagnostic);
    expect_status(strict_st, taiyin::TAIYIN_STATUS_OK, "fast strict with atmosphere", failures);
    expect_status(nonstrict_st, taiyin::TAIYIN_STATUS_OK, "fast non-strict with atmosphere", failures);
    if (strict_st == taiyin::TAIYIN_STATUS_OK && nonstrict_st == taiyin::TAIYIN_STATUS_OK) {
        expect_near(
            taiyin::split_julian_date_to_double(strict_result.rise_jd_tt),
            taiyin::split_julian_date_to_double(nonstrict_result.rise_jd_tt),
            1.0e-9,
            "fast strict matches non-strict with same atmosphere",
            failures);
    }
}

void test_transit_oracles(int* failures) {
    using namespace taiyin::runtime;

    const TransitOracle oracles[] = {
        {"denver_upper_transit", -104.9903, 39.7392, 1609.0, jd_ut(2024, 4, 8, 6.0), TAIYIN_SOLAR_VISIBILITY_EVENT_UPPER_TRANSIT, 2460409.2927672616},
        {"denver_lower_transit", -104.9903, 39.7392, 1609.0, jd_ut(2024, 4, 8, 6.0), TAIYIN_SOLAR_VISIBILITY_EVENT_LOWER_TRANSIT, 2460408.7928612637},
        {"longyearbyen_summer_upper_transit", 15.6333, 78.2232, 10.0, jd_ut(2024, 6, 20, 22.0), TAIYIN_SOLAR_VISIBILITY_EVENT_UPPER_TRANSIT, 2460482.9579023924},
        {"longyearbyen_summer_lower_transit", 15.6333, 78.2232, 10.0, jd_ut(2024, 6, 20, 22.0), TAIYIN_SOLAR_VISIBILITY_EVENT_LOWER_TRANSIT, 2460482.4578272547},
        {"longyearbyen_winter_upper_transit", 15.6333, 78.2232, 10.0, jd_ut(2024, 12, 20, 23.0), TAIYIN_SOLAR_VISIBILITY_EVENT_UPPER_TRANSIT, 2460665.955371959},
        {"longyearbyen_winter_lower_transit", 15.6333, 78.2232, 10.0, jd_ut(2024, 12, 20, 23.0), TAIYIN_SOLAR_VISIBILITY_EVENT_LOWER_TRANSIT, 2460666.4555444503},
        {"tromso_summer_upper_transit", 18.9553, 69.6492, 10.0, jd_ut(2024, 6, 20, 22.0), TAIYIN_SOLAR_VISIBILITY_EVENT_UPPER_TRANSIT, 2460482.94867327},
        {"tromso_summer_lower_transit", 18.9553, 69.6492, 10.0, jd_ut(2024, 6, 20, 22.0), TAIYIN_SOLAR_VISIBILITY_EVENT_LOWER_TRANSIT, 2460482.448598052},
        {"tromso_winter_upper_transit", 18.9553, 69.6492, 10.0, jd_ut(2024, 12, 20, 23.0), TAIYIN_SOLAR_VISIBILITY_EVENT_UPPER_TRANSIT, 2460665.9461410358},
        {"tromso_winter_lower_transit", 18.9553, 69.6492, 10.0, jd_ut(2024, 12, 20, 23.0), TAIYIN_SOLAR_VISIBILITY_EVENT_LOWER_TRANSIT, 2460666.446313447},
    };

    EphemerisEvalDiagnostic diagnostic;
    VisibilityAltitudeSearchResult result;
    for (const TransitOracle& oracle : oracles) {
        NativeCalcContext context = make_context(oracle.longitude_deg, oracle.latitude_deg, oracle.height_m);
        const taiyin::Status st = solar_visibility_search_transit_ut(
            &context,
            split_jd(oracle.start_jd_ut),
            split_jd(oracle.start_jd_ut + 1.0),
            oracle.event_kind,
            &result,
            &diagnostic);
        expect_status(st, taiyin::TAIYIN_STATUS_OK, oracle.label, failures);
        if (st != taiyin::TAIYIN_STATUS_OK) continue;
        expect_equal(result.altitude_state, TAIYIN_VISIBILITY_ALTITUDE_STATE_CROSSES, oracle.label, failures);
        expect_near(result.jd_ut, oracle.swiss_jd_ut, 1.0 / 86400.0, oracle.label, failures);
        expect_near(result.residual_rad, 0.0, 1.0e-6, oracle.label, failures);
    }
}

void test_fast_apparent_correction_series(int* failures) {
    using namespace taiyin;
    using namespace taiyin::runtime;

    NativeCalcContext context = make_context(-104.9903, 39.7392, 1609.0);
    EphemerisEvalDiagnostic diagnostic;
    FastApparentOptions options;
    options.frame = FAST_APPARENT_TRUE_EQUATOR_OF_DATE;
    options.with_velocity = false;
    options.true_position = false;

    const SplitJulianDate center_tt = split_jd(2460409.0);

    FastApparentCorrectionConfig default_config;
    default_config.initial_half_days = 3.0 / 24.0;
    default_config.sample_step_days = 3.0 / 24.0;
    FastApparentCorrectionSeries default_series;
    expect_status(
        init_fast_correction_series(
            &context,
            TAIYIN_BODY_SUN,
            0,
            options,
            default_config,
            center_tt,
            &default_series,
            &diagnostic),
        TAIYIN_STATUS_OK,
        "series initializes default linear solar correction",
        failures);
    expect_equal(
        default_series.interpolation_kind,
        FAST_APPARENT_CORRECTION_INTERPOLATION_LINEAR,
        "series keeps default linear interpolation kind",
        failures);
    const uint64_t default_sun_identity = default_series.identity_hash;
    FastApparentCorrectionEpochSample moon_sample;
    expect_status(
        get_fast_correction(
            &context,
            TAIYIN_BODY_MOON,
            0,
            options,
            default_config,
            center_tt,
            &default_series,
            &diagnostic,
            &moon_sample),
        TAIYIN_STATUS_OK,
        "series rebuilds when body identity changes",
        failures);
    expect_true(
        default_series.identity_hash != default_sun_identity,
        "series identity changes after body rebuild",
        failures);

    FastApparentCorrectionConfig invalid_config;
    invalid_config.initial_half_days = std::numeric_limits<double>::infinity();
    FastApparentCorrectionSeries invalid_series;
    FastApparentCorrectionEpochSample invalid_sample;
    expect_status(
        get_fast_correction(
            &context,
            TAIYIN_BODY_SUN,
            0,
            options,
            invalid_config,
            center_tt,
            &invalid_series,
            &diagnostic,
            &invalid_sample),
        TAIYIN_ERROR_INVALID_ARGUMENT,
        "series rejects non-finite initial half days",
        failures);

    FastApparentCorrectionConfig config;
    config.initial_half_days = 3.0 / 24.0;
    config.sample_step_days = 1.0 / 24.0;
    config.interpolation_kind = FAST_APPARENT_CORRECTION_INTERPOLATION_CATMULL_ROM;
    FastApparentCorrectionSeries solar_series;
    FastApparentCorrectionEpochSample solar_sample;
    expect_status(
        init_fast_correction_series(
            &context,
            TAIYIN_BODY_SUN,
            0,
            options,
            config,
            center_tt,
            &solar_series,
            &diagnostic),
        TAIYIN_STATUS_OK,
        "series initializes solar correction",
        failures);
    expect_status(
        get_fast_correction(
            &context,
            TAIYIN_BODY_SUN,
            0,
            options,
            config,
            center_tt,
            &solar_series,
            &diagnostic,
            &solar_sample),
        TAIYIN_STATUS_OK,
        "series gets solar correction",
        failures);
    expect_true(solar_series.samples.size() >= 4, "series keeps enough samples for catmull-rom", failures);
    expect_true(
        solar_series.start_jd_tt <= center_tt && center_tt <= solar_series.end_jd_tt,
        "series solar covers center jd",
        failures);
    expect_near(
        solar_series.sample_step_days,
        1.0 / 24.0,
        1.0e-15,
        "series keeps configured sample step",
        failures);
    expect_equal(
        solar_series.interpolation_kind,
        FAST_APPARENT_CORRECTION_INTERPOLATION_CATMULL_ROM,
        "series keeps configured interpolation kind",
        failures);

    FastApparentCorrectionEpochSample default_lookup_sample;
    expect_status(
        get_fast_correction(
            &context,
            TAIYIN_BODY_SUN,
            0,
            options,
            default_config,
            center_tt + 0.5 / 24.0,
            &solar_series,
            &diagnostic,
            &default_lookup_sample),
        TAIYIN_STATUS_OK,
        "series lookup uses stored config after initialization",
        failures);

    FastApparentCorrectionEpochSample later_sample;
    expect_status(
        get_fast_correction(
            &context,
            TAIYIN_BODY_SUN,
            0,
            options,
            config,
            center_tt + 8.0 / 24.0,
            &solar_series,
            &diagnostic,
            &later_sample),
        TAIYIN_STATUS_OK,
        "series builds later solar correction",
        failures);
    expect_true(
        solar_series.start_jd_tt <= center_tt + 8.0 / 24.0
            && center_tt + 8.0 / 24.0 <= solar_series.end_jd_tt,
        "series later segment covers jd",
        failures);

    FastApparentCorrectionSeries pair_series;
    FastApparentCorrectionEpochSample pair_sample;
    expect_status(
        init_fast_correction_series(
            &context,
            TAIYIN_BODY_MOON,
            TAIYIN_BODY_SUN,
            options,
            config,
            center_tt + 0.25 / 24.0,
            &pair_series,
            &diagnostic),
        TAIYIN_STATUS_OK,
        "series initializes moon-sun pair correction",
        failures);
    expect_status(
        get_fast_correction(
            &context,
            TAIYIN_BODY_MOON,
            TAIYIN_BODY_SUN,
            options,
            config,
            center_tt + 0.25 / 24.0,
            &pair_series,
            &diagnostic,
            &pair_sample),
        TAIYIN_STATUS_OK,
        "series gets moon-sun pair correction",
        failures);
    expect_true(pair_series.samples.size() >= 4, "series pair sample count", failures);
}

}  // namespace

int main() {
    int failures = 0;
    if (initialize_runtime(&failures)) {
        test_fast_apparent_correction_series(&failures);
        test_denver_swiss_oracles(&failures);
        test_polar_oracles(&failures);
        test_invalid_arguments_and_atmosphere(&failures);
        test_fast_rise_set_matches_precise(&failures);
        test_transit_oracles(&failures);
    }
    if (failures != 0) {
        std::cerr << failures << " solar visibility checks failed\n";
        return 1;
    }
    return 0;
}
