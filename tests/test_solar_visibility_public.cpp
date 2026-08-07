#include "taiyin/runtime/solar_visibility.h"

#include "taiyin/angle.h"
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

void expect_not_status(taiyin::Status actual, taiyin::Status forbidden, const char* label, int* failures) {
    if (actual == forbidden) {
        std::cerr << "FAIL: " << label << ": actual=" << actual
                  << " forbidden=" << forbidden << "\n";
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

void expect_near(
    const taiyin::SplitJulianDate& actual,
    double expected,
    double tolerance,
    const char* label,
    int* failures
) {
    expect_near(taiyin::split_julian_date_to_double(actual), expected, tolerance, label, failures);
}

void expect_near(
    const taiyin::SplitJulianDate& actual,
    const taiyin::SplitJulianDate& expected,
    double tolerance,
    const char* label,
    int* failures
) {
    expect_near(taiyin::days_between_split_jd(actual, expected), 0.0, tolerance, label, failures);
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

const taiyin::SplitJulianDate jd_ut(int year, int month, int day, double hour) {
    taiyin::SplitJulianDate out;
    taiyin::julian_day_split(
        {year, month, day, static_cast<int>(hour), 0, (hour - static_cast<int>(hour)) * 3600.0},
        &out);
    return out;
}

taiyin::runtime::NativeCalcContext make_context(double longitude_deg, double latitude_deg, double height_m) {
    taiyin::runtime::NativeCalcContext context;
    taiyin::runtime::native_context_set_geocentric_observer(
        &context,
        taiyin::TAIYIN_BODY_EARTH,
        taiyin::TAIYIN_BODY_EARTH);
    taiyin::runtime::native_context_set_observer_location(
        &context,
        taiyin::runtime::native_observer_location_degrees(longitude_deg, latitude_deg, height_m));
    taiyin::runtime::native_context_set_atmosphere(&context, taiyin::runtime::native_standard_atmosphere());
    taiyin::runtime::native_context_use_solar_deflector(&context);
    context.apparent_options.flags =
        taiyin::TAIYIN_APPARENT_SPHERICAL
        | taiyin::TAIYIN_APPARENT_LIGHT_TIME
        | taiyin::TAIYIN_APPARENT_ABERRATION
        | taiyin::TAIYIN_APPARENT_DEFLECTION;
    context.apparent_options.output_frame_id = taiyin::TAIYIN_APPARENT_FRAME_TRUE_ECLIPTIC_OF_DATE;
    return context;
}

taiyin::runtime::NativeCalcContext make_context_without_atmosphere(
    double longitude_deg,
    double latitude_deg,
    double height_m
) {
    taiyin::runtime::NativeCalcContext context;
    taiyin::runtime::native_context_set_geocentric_observer(
        &context,
        taiyin::TAIYIN_BODY_EARTH,
        taiyin::TAIYIN_BODY_EARTH);
    taiyin::runtime::native_context_set_observer_location(
        &context,
        taiyin::runtime::native_observer_location_degrees(longitude_deg, latitude_deg, height_m));
    taiyin::runtime::native_context_use_solar_deflector(&context);
    context.apparent_options.flags =
        taiyin::TAIYIN_APPARENT_SPHERICAL
        | taiyin::TAIYIN_APPARENT_LIGHT_TIME
        | taiyin::TAIYIN_APPARENT_ABERRATION
        | taiyin::TAIYIN_APPARENT_DEFLECTION;
    context.apparent_options.output_frame_id = taiyin::TAIYIN_APPARENT_FRAME_TRUE_ECLIPTIC_OF_DATE;
    return context;
}

void test_public_denver_oracles(int* failures) {
    using namespace taiyin::runtime;

    NativeCalcContext context = make_context(-104.9903, 39.7392, 1609.0);
    const taiyin::SplitJulianDate start = jd_ut(2024, 4, 8, 6.0);
    const taiyin::SplitJulianDate end = start + 1.0;
    SolarVisibilityEventResult result;

    expect_status(
        search_solar_rise_set_ut(
            &context,
            start,
            end,
            TAIYIN_SOLAR_VISIBILITY_EVENT_RISE,
            TAIYIN_SOLAR_VISIBILITY_LIMB_UPPER,
            TAIYIN_SOLAR_VISIBILITY_FLAG_REFRACTION,
            &result),
        taiyin::TAIYIN_STATUS_OK,
        "public Denver default sunrise",
        failures);
    expect_equal(result.altitude_state, TAIYIN_SOLAR_VISIBILITY_ALTITUDE_STATE_CROSSES, "public sunrise crosses", failures);
    expect_equal(result.crossing_direction, TAIYIN_SOLAR_VISIBILITY_CROSSING_RISING, "public sunrise direction", failures);
    expect_near(result.jd_ut, 2460409.022335537709, 2.0 / 86400.0, "public Denver default sunrise SwissEph", failures);

    SolarVisibilityEventResult default_result;
    expect_status(
        search_solar_rise_set_ut(
            &context,
            start,
            end,
            TAIYIN_SOLAR_VISIBILITY_EVENT_RISE,
            TAIYIN_SOLAR_VISIBILITY_LIMB_UPPER,
            0u,
            &default_result),
        taiyin::TAIYIN_STATUS_OK,
        "public Denver sunrise default flags",
        failures);
    expect_near(default_result.jd_ut, result.jd_ut, 1.0e-12, "public solar default flags use refraction", failures);

    expect_status(
        search_solar_twilight_ut(
            &context,
            start,
            end,
            TAIYIN_SOLAR_VISIBILITY_EVENT_SET,
            TAIYIN_SOLAR_VISIBILITY_TWILIGHT_NAUTICAL,
            &result),
        taiyin::TAIYIN_STATUS_OK,
        "public Denver nautical dusk",
        failures);
    expect_equal(result.altitude_state, TAIYIN_SOLAR_VISIBILITY_ALTITUDE_STATE_CROSSES, "public nautical dusk crosses", failures);
    expect_equal(result.crossing_direction, TAIYIN_SOLAR_VISIBILITY_CROSSING_SETTING, "public nautical dusk direction", failures);
    expect_near(result.jd_ut, 2460409.605691832025, 0.25 / 86400.0, "public Denver nautical dusk SwissEph", failures);

    expect_status(
        search_solar_rise_set_at_horizon_ut(
            &context,
            start,
            end,
            TAIYIN_SOLAR_VISIBILITY_EVENT_RISE,
            TAIYIN_SOLAR_VISIBILITY_LIMB_UPPER,
            taiyin::TAIYIN_DEG_TO_RAD,
            TAIYIN_SOLAR_VISIBILITY_FLAG_REFRACTION,
            &result),
        taiyin::TAIYIN_STATUS_OK,
        "public Denver sunrise at +1 degree horizon",
        failures);
    expect_equal(result.altitude_state, TAIYIN_SOLAR_VISIBILITY_ALTITUDE_STATE_CROSSES, "public custom horizon sunrise crosses", failures);
    expect_equal(result.crossing_direction, TAIYIN_SOLAR_VISIBILITY_CROSSING_RISING, "public custom horizon sunrise direction", failures);
    expect_near(result.jd_ut, 2460409.026595990174, 2.0 / 86400.0, "public Denver custom horizon SwissEph", failures);

    expect_status(
        search_solar_rise_set_ut(
            &context,
            start,
            end,
            TAIYIN_SOLAR_VISIBILITY_EVENT_SET,
            TAIYIN_SOLAR_VISIBILITY_LIMB_LOWER,
            TAIYIN_SOLAR_VISIBILITY_FLAG_NO_REFRACTION,
            &result),
        taiyin::TAIYIN_STATUS_OK,
        "public Denver lower-limb sunset",
        failures);
    expect_equal(result.altitude_state, TAIYIN_SOLAR_VISIBILITY_ALTITUDE_STATE_CROSSES, "public lower-limb sunset crosses", failures);
    expect_equal(result.crossing_direction, TAIYIN_SOLAR_VISIBILITY_CROSSING_SETTING, "public lower-limb sunset direction", failures);
    expect_near(result.jd_ut, 2460409.559664948378, 0.25 / 86400.0, "public Denver lower-limb sunset SwissEph", failures);

    expect_status(
        search_solar_rise_set_ut(
            &context,
            start,
            end,
            TAIYIN_SOLAR_VISIBILITY_EVENT_RISE,
            TAIYIN_SOLAR_VISIBILITY_LIMB_UPPER,
            TAIYIN_SOLAR_VISIBILITY_FLAG_FIXED_DISC_SIZE | TAIYIN_SOLAR_VISIBILITY_FLAG_NO_REFRACTION,
            &result),
        taiyin::TAIYIN_STATUS_OK,
        "public Denver fixed-disc sunrise",
        failures);
    expect_equal(result.altitude_state, TAIYIN_SOLAR_VISIBILITY_ALTITUDE_STATE_CROSSES, "public fixed-disc sunrise crosses", failures);
    expect_near(result.jd_ut, 2460409.024387161247, 0.25 / 86400.0, "public Denver fixed-disc sunrise SwissEph", failures);
}

void test_public_polar_state(int* failures) {
    using namespace taiyin::runtime;

    NativeCalcContext context = make_context(15.6333, 78.2232, 10.0);
    const taiyin::SplitJulianDate start = jd_ut(2024, 6, 20, 22.0);
    SolarVisibilityEventResult result;
    expect_status(
        search_solar_rise_set_ut(
            &context,
            start,
            start + 1.0,
            TAIYIN_SOLAR_VISIBILITY_EVENT_SET,
            TAIYIN_SOLAR_VISIBILITY_LIMB_UPPER,
            TAIYIN_SOLAR_VISIBILITY_FLAG_REFRACTION,
            &result),
        taiyin::TAIYIN_STATUS_OK,
        "public Longyearbyen summer sunset",
        failures);
    expect_equal(result.altitude_state, TAIYIN_SOLAR_VISIBILITY_ALTITUDE_STATE_ALWAYS_ABOVE, "public polar always above", failures);
    expect_true(result.min_residual_rad > 0.0, "public polar min positive", failures);
    expect_true(!taiyin::split_julian_date_is_finite(result.jd_ut), "public polar jd is nan", failures);
}

void test_public_flag_validation(int* failures) {
    using namespace taiyin::runtime;

    NativeCalcContext context = make_context(-104.9903, 39.7392, 1609.0);
    const taiyin::SplitJulianDate start = jd_ut(2024, 4, 8, 6.0);
    SolarVisibilityEventResult result;
    expect_status(
        search_solar_rise_set_at_horizon_ut(
            nullptr,
            start,
            start + 1.0,
            TAIYIN_SOLAR_VISIBILITY_EVENT_RISE,
            TAIYIN_SOLAR_VISIBILITY_LIMB_UPPER,
            0.0,
            0u,
            &result),
        taiyin::TAIYIN_ERROR_INVALID_ARGUMENT,
        "public solar rejects null context",
        failures);

    expect_status(
        search_solar_rise_set_ut(
            &context,
            start,
            start + 1.0,
            TAIYIN_SOLAR_VISIBILITY_EVENT_RISE,
            TAIYIN_SOLAR_VISIBILITY_LIMB_UPPER,
            TAIYIN_SOLAR_VISIBILITY_FLAG_REFRACTION | TAIYIN_SOLAR_VISIBILITY_FLAG_NO_REFRACTION,
            &result),
        taiyin::TAIYIN_ERROR_INVALID_ARGUMENT,
        "public solar rejects conflicting refraction flags",
        failures);

    expect_status(
        search_solar_rise_set_ut(
            &context,
            start,
            start + 1.0,
            TAIYIN_SOLAR_VISIBILITY_EVENT_RISE,
            TAIYIN_SOLAR_VISIBILITY_LIMB_UPPER,
            1u << 31,
            &result),
        taiyin::TAIYIN_ERROR_INVALID_ARGUMENT,
        "public solar rejects unknown visibility flags",
        failures);

    NativeCalcContext missing_atmosphere = make_context_without_atmosphere(-104.9903, 39.7392, 1609.0);
    expect_not_status(
        search_solar_rise_set_ut(
            &missing_atmosphere,
            start,
            start + 1.0,
            TAIYIN_SOLAR_VISIBILITY_EVENT_RISE,
            TAIYIN_SOLAR_VISIBILITY_LIMB_UPPER,
            0u,
            &result),
        taiyin::TAIYIN_STATUS_OK,
        "public solar default refraction requires atmosphere",
        failures);

    expect_status(
        native_context_set_atmosphere_policy_flags(
            &missing_atmosphere, TAIYIN_NATIVE_ATMOSPHERE_ALLOW_STANDARD_FALLBACK),
        taiyin::TAIYIN_STATUS_OK,
        "public solar enables standard-atmosphere fallback",
        failures);
    expect_status(
        search_solar_rise_set_ut(
            &missing_atmosphere,
            start,
            start + 1.0,
            TAIYIN_SOLAR_VISIBILITY_EVENT_RISE,
            TAIYIN_SOLAR_VISIBILITY_LIMB_CENTER,
            TAIYIN_SOLAR_VISIBILITY_STRICT_METEOROLOGY,
            &result),
        taiyin::TAIYIN_ERROR_INVALID_ARGUMENT,
        "public solar center strict mode bypasses standard atmosphere",
        failures);
}

void test_public_transit_oracles(int* failures) {
    using namespace taiyin::runtime;

    NativeCalcContext denver = make_context(-104.9903, 39.7392, 1609.0);
    const taiyin::SplitJulianDate denver_start = jd_ut(2024, 4, 8, 6.0);
    SolarVisibilityEventResult result;
    expect_status(
        search_solar_transit_ut(
            &denver,
            denver_start,
            denver_start + 1.0,
            TAIYIN_SOLAR_VISIBILITY_EVENT_UPPER_TRANSIT,
            &result),
        taiyin::TAIYIN_STATUS_OK,
        "public Denver upper transit",
        failures);
    expect_equal(result.altitude_state, TAIYIN_SOLAR_VISIBILITY_ALTITUDE_STATE_CROSSES, "public upper transit crosses", failures);
    expect_near(result.jd_ut, 2460409.292767241597, 1.0 / 86400.0, "public Denver upper transit SwissEph", failures);

    NativeCalcContext longyearbyen = make_context(15.6333, 78.2232, 10.0);
    const taiyin::SplitJulianDate longyearbyen_start = jd_ut(2024, 12, 20, 23.0);
    expect_status(
        search_solar_transit_ut(
            &longyearbyen,
            longyearbyen_start,
            longyearbyen_start + 1.0,
            TAIYIN_SOLAR_VISIBILITY_EVENT_LOWER_TRANSIT,
            &result),
        taiyin::TAIYIN_STATUS_OK,
        "public Longyearbyen lower transit",
        failures);
    expect_equal(result.altitude_state, TAIYIN_SOLAR_VISIBILITY_ALTITUDE_STATE_CROSSES, "public lower transit crosses", failures);
    expect_near(result.jd_ut, 2460666.455544441938, 1.0 / 86400.0, "public Longyearbyen lower transit SwissEph", failures);
}

}  // namespace

int main() {
    int failures = 0;
    if (initialize_runtime(&failures)) {
        test_public_denver_oracles(&failures);
        test_public_polar_state(&failures);
        test_public_flag_validation(&failures);
        test_public_transit_oracles(&failures);
    }
    if (failures != 0) {
        std::cerr << failures << " public solar visibility checks failed\n";
        return 1;
    }
    return 0;
}
