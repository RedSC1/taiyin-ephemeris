#include "taiyin/runtime/planet_visibility.h"

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

const double kDegToRad = 3.141592653589793238462643383279502884 / 180.0;

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

std::string repo_opm2_cob_full_root() {
    const char* root = std::getenv("TAIYIN_REPO_ROOT");
    if (root && root[0] != '\0') {
        return std::string(root) + "/data/ephemerides/opm2/cob/full";
    }
    return "../data/ephemerides/opm2/cob/full";
}

bool initialize_runtime(int* failures) {
    taiyin::runtime::EphemerisRuntimeConfig config;
    const std::string major_root = repo_opm2_major_body_root();
    const std::string cob_root = repo_opm2_cob_full_root();
    const char* source_paths[] = { major_root.c_str(), cob_root.c_str() };
    config.source_paths = source_paths;
    config.source_path_count = 2;
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
    using namespace taiyin;
    using namespace taiyin::runtime;

    NativeCalcContext context = make_context(-104.9903, 39.7392, 1609.0);
    const taiyin::SplitJulianDate start = jd_ut(2024, 4, 8, 6.0);
    PlanetVisibilityEventResult result;

    expect_status(
        search_planet_rise_set_ut(
            &context,
            TAIYIN_BODY_MERCURY,
            start,
            start + 1.0,
            TAIYIN_PLANET_VISIBILITY_EVENT_RISE,
            TAIYIN_PLANET_VISIBILITY_LIMB_UPPER,
            TAIYIN_PLANET_VISIBILITY_FLAG_NO_REFRACTION,
            &result),
        taiyin::TAIYIN_STATUS_OK,
        "public Denver Mercury rise",
        failures);
    expect_equal(result.altitude_state, TAIYIN_PLANET_VISIBILITY_ALTITUDE_STATE_CROSSES, "public Mercury rise crosses", failures);
    expect_equal(result.crossing_direction, TAIYIN_PLANET_VISIBILITY_CROSSING_RISING, "public Mercury rise direction", failures);
    expect_near(result.jd_ut, 2460409.025837766007, 1.0 / 86400.0, "public Denver Mercury rise SwissEph", failures);

    PlanetVisibilityEventResult explicit_refraction;
    PlanetVisibilityEventResult default_result;
    expect_status(
        search_planet_rise_set_ut(
            &context,
            TAIYIN_BODY_MERCURY,
            start,
            start + 1.0,
            TAIYIN_PLANET_VISIBILITY_EVENT_RISE,
            TAIYIN_PLANET_VISIBILITY_LIMB_UPPER,
            TAIYIN_PLANET_VISIBILITY_FLAG_REFRACTION,
            &explicit_refraction),
        taiyin::TAIYIN_STATUS_OK,
        "public Denver Mercury rise refraction",
        failures);
    expect_status(
        search_planet_rise_set_ut(
            &context,
            TAIYIN_BODY_MERCURY,
            start,
            start + 1.0,
            TAIYIN_PLANET_VISIBILITY_EVENT_RISE,
            TAIYIN_PLANET_VISIBILITY_LIMB_UPPER,
            0u,
            &default_result),
        taiyin::TAIYIN_STATUS_OK,
        "public Denver Mercury rise default flags",
        failures);
    expect_near(default_result.jd_ut, explicit_refraction.jd_ut, 1.0e-12, "public planet default flags use refraction", failures);

    expect_status(
        search_planet_transit_ut(
            &context,
            TAIYIN_BODY_VENUS,
            start,
            start + 1.0,
            TAIYIN_PLANET_VISIBILITY_EVENT_UPPER_TRANSIT,
            &result),
        taiyin::TAIYIN_STATUS_OK,
        "public Denver Venus upper transit",
        failures);
    expect_equal(result.altitude_state, TAIYIN_PLANET_VISIBILITY_ALTITUDE_STATE_CROSSES, "public Venus upper transit crosses", failures);
    expect_near(result.jd_ut, 2460409.256011750549, 2.0 / 86400.0, "public Denver Venus transit SwissEph", failures);

    expect_status(
        search_planet_rise_set_ut(
            &context,
            TAIYIN_BODY_VENUS,
            start,
            start + 1.0,
            TAIYIN_PLANET_VISIBILITY_EVENT_SET,
            TAIYIN_PLANET_VISIBILITY_LIMB_LOWER,
            TAIYIN_PLANET_VISIBILITY_FLAG_NO_REFRACTION,
            &result),
        taiyin::TAIYIN_STATUS_OK,
        "public Denver Venus lower-limb set",
        failures);
    expect_equal(result.altitude_state, TAIYIN_PLANET_VISIBILITY_ALTITUDE_STATE_CROSSES, "public Venus lower-limb set crosses", failures);
    expect_equal(result.crossing_direction, TAIYIN_PLANET_VISIBILITY_CROSSING_SETTING, "public Venus lower-limb set direction", failures);
    expect_near(result.jd_ut, 2460409.507286465727, 1.0 / 86400.0, "public Denver Venus lower-limb set SwissEph", failures);

    expect_status(
        search_planet_rise_set_ut(
            &context,
            TAIYIN_BODY_NEPTUNE,
            start,
            start + 1.0,
            TAIYIN_PLANET_VISIBILITY_EVENT_RISE,
            TAIYIN_PLANET_VISIBILITY_LIMB_UPPER,
            TAIYIN_PLANET_VISIBILITY_FLAG_NO_REFRACTION,
            &result),
        taiyin::TAIYIN_STATUS_OK,
        "public Denver Neptune rise smoke",
        failures);
    expect_equal(result.altitude_state, TAIYIN_PLANET_VISIBILITY_ALTITUDE_STATE_CROSSES, "public Neptune rise crosses", failures);
    expect_true(taiyin::split_julian_date_is_finite(result.jd_ut), "public Neptune rise jd finite", failures);

    expect_status(
        search_planet_transit_ut(
            &context,
            TAIYIN_BODY_JUPITER,
            start,
            start + 1.0,
            TAIYIN_PLANET_VISIBILITY_EVENT_UPPER_TRANSIT,
            &result),
        taiyin::TAIYIN_STATUS_OK,
        "public Denver Jupiter physical upper transit smoke",
        failures);
    expect_equal(result.altitude_state, TAIYIN_PLANET_VISIBILITY_ALTITUDE_STATE_CROSSES, "public Jupiter physical upper transit crosses", failures);
    expect_true(taiyin::split_julian_date_is_finite(result.jd_ut), "public Jupiter transit jd finite", failures);
}

void test_public_custom_horizon(int* failures) {
    using namespace taiyin;
    using namespace taiyin::runtime;

    NativeCalcContext context = make_context(-104.9903, 39.7392, 1609.0);
    const taiyin::SplitJulianDate start = jd_ut(2024, 4, 8, 6.0);
    const taiyin::SplitJulianDate end = start + 1.0;
    const double high_horizon_rad = 1.0 * kDegToRad;

    PlanetVisibilityEventResult base_rise;
    PlanetVisibilityEventResult custom_rise;
    expect_status(
        search_planet_rise_set_ut(
            &context,
            TAIYIN_BODY_MERCURY,
            start,
            end,
            TAIYIN_PLANET_VISIBILITY_EVENT_RISE,
            TAIYIN_PLANET_VISIBILITY_LIMB_CENTER,
            TAIYIN_PLANET_VISIBILITY_FLAG_NO_REFRACTION,
            &base_rise),
        taiyin::TAIYIN_STATUS_OK,
        "public Denver Mercury center rise baseline",
        failures);
    expect_status(
        search_planet_rise_set_at_horizon_ut(
            &context,
            TAIYIN_BODY_MERCURY,
            start,
            end,
            TAIYIN_PLANET_VISIBILITY_EVENT_RISE,
            TAIYIN_PLANET_VISIBILITY_LIMB_CENTER,
            high_horizon_rad,
            TAIYIN_PLANET_VISIBILITY_FLAG_NO_REFRACTION,
            &custom_rise),
        taiyin::TAIYIN_STATUS_OK,
        "public Denver Mercury custom-horizon rise",
        failures);
    expect_equal(custom_rise.altitude_state, TAIYIN_PLANET_VISIBILITY_ALTITUDE_STATE_CROSSES, "public Mercury custom-horizon rise crosses", failures);
    expect_equal(custom_rise.crossing_direction, TAIYIN_PLANET_VISIBILITY_CROSSING_RISING, "public Mercury custom-horizon rise direction", failures);
    expect_true(taiyin::split_julian_date_is_finite(custom_rise.jd_ut), "public Mercury custom-horizon rise jd finite", failures);
    expect_true(custom_rise.jd_ut > base_rise.jd_ut, "public Mercury higher horizon delays rise", failures);
    expect_near(custom_rise.residual_rad, 0.0, 1.0e-8, "public Mercury custom-horizon rise residual", failures);

    PlanetVisibilityEventResult mercury_refracted_upper;
    expect_status(
        search_planet_rise_set_at_horizon_ut(
            &context,
            TAIYIN_BODY_MERCURY,
            start,
            end,
            TAIYIN_PLANET_VISIBILITY_EVENT_RISE,
            TAIYIN_PLANET_VISIBILITY_LIMB_UPPER,
            high_horizon_rad,
            0u,
            &mercury_refracted_upper),
        taiyin::TAIYIN_STATUS_OK,
        "public Denver Mercury custom-horizon upper-limb default rise",
        failures);
    expect_equal(mercury_refracted_upper.altitude_state, TAIYIN_PLANET_VISIBILITY_ALTITUDE_STATE_CROSSES, "public Mercury custom-horizon upper-limb rise crosses", failures);
    expect_near(mercury_refracted_upper.jd_ut, 2460409.028094371781, 2.0 / 86400.0, "public Denver Mercury custom-horizon upper refraction SwissEph", failures);

    PlanetVisibilityEventResult base_set;
    PlanetVisibilityEventResult custom_set;
    expect_status(
        search_planet_rise_set_ut(
            &context,
            TAIYIN_BODY_VENUS,
            start,
            end,
            TAIYIN_PLANET_VISIBILITY_EVENT_SET,
            TAIYIN_PLANET_VISIBILITY_LIMB_CENTER,
            TAIYIN_PLANET_VISIBILITY_FLAG_NO_REFRACTION,
            &base_set),
        taiyin::TAIYIN_STATUS_OK,
        "public Denver Venus center set baseline",
        failures);
    expect_status(
        search_planet_rise_set_at_horizon_ut(
            &context,
            TAIYIN_BODY_VENUS,
            start,
            end,
            TAIYIN_PLANET_VISIBILITY_EVENT_SET,
            TAIYIN_PLANET_VISIBILITY_LIMB_CENTER,
            high_horizon_rad,
            TAIYIN_PLANET_VISIBILITY_FLAG_NO_REFRACTION,
            &custom_set),
        taiyin::TAIYIN_STATUS_OK,
        "public Denver Venus custom-horizon set",
        failures);
    expect_equal(custom_set.altitude_state, TAIYIN_PLANET_VISIBILITY_ALTITUDE_STATE_CROSSES, "public Venus custom-horizon set crosses", failures);
    expect_equal(custom_set.crossing_direction, TAIYIN_PLANET_VISIBILITY_CROSSING_SETTING, "public Venus custom-horizon set direction", failures);
    expect_true(taiyin::split_julian_date_is_finite(custom_set.jd_ut), "public Venus custom-horizon set jd finite", failures);
    expect_true(custom_set.jd_ut < base_set.jd_ut, "public Venus higher horizon advances set", failures);
    expect_near(custom_set.residual_rad, 0.0, 1.0e-8, "public Venus custom-horizon set residual", failures);
    expect_near(custom_set.jd_ut, 2460409.503673634492, 1.0 / 86400.0, "public Denver Venus custom-horizon center no-refraction SwissEph", failures);
}

void test_public_flag_validation(int* failures) {
    using namespace taiyin;
    using namespace taiyin::runtime;

    NativeCalcContext context = make_context(-104.9903, 39.7392, 1609.0);
    const taiyin::SplitJulianDate start = jd_ut(2024, 4, 8, 6.0);
    PlanetVisibilityEventResult result;
    expect_status(
        search_planet_rise_set_at_horizon_ut(
            nullptr,
            TAIYIN_BODY_MERCURY,
            start,
            start + 1.0,
            TAIYIN_PLANET_VISIBILITY_EVENT_RISE,
            TAIYIN_PLANET_VISIBILITY_LIMB_UPPER,
            0.0,
            0u,
            &result),
        taiyin::TAIYIN_ERROR_INVALID_ARGUMENT,
        "public planet rejects null context",
        failures);

    expect_status(
        search_planet_rise_set_ut(
            &context,
            TAIYIN_BODY_MERCURY,
            start,
            start + 1.0,
            TAIYIN_PLANET_VISIBILITY_EVENT_RISE,
            TAIYIN_PLANET_VISIBILITY_LIMB_UPPER,
            TAIYIN_PLANET_VISIBILITY_FLAG_REFRACTION | TAIYIN_PLANET_VISIBILITY_FLAG_NO_REFRACTION,
            &result),
        taiyin::TAIYIN_ERROR_INVALID_ARGUMENT,
        "public planet rejects conflicting refraction flags",
        failures);

    expect_status(
        search_planet_rise_set_ut(
            &context,
            TAIYIN_BODY_MERCURY,
            start,
            start + 1.0,
            TAIYIN_PLANET_VISIBILITY_EVENT_RISE,
            TAIYIN_PLANET_VISIBILITY_LIMB_UPPER,
            1u << 31,
            &result),
        taiyin::TAIYIN_ERROR_INVALID_ARGUMENT,
        "public planet rejects unknown visibility flags",
        failures);

    NativeCalcContext missing_atmosphere = make_context_without_atmosphere(-104.9903, 39.7392, 1609.0);
    expect_not_status(
        search_planet_rise_set_ut(
            &missing_atmosphere,
            TAIYIN_BODY_MERCURY,
            start,
            start + 1.0,
            TAIYIN_PLANET_VISIBILITY_EVENT_RISE,
            TAIYIN_PLANET_VISIBILITY_LIMB_UPPER,
            0u,
            &result),
        taiyin::TAIYIN_STATUS_OK,
        "public planet default refraction requires atmosphere",
        failures);

    expect_status(
        native_context_set_atmosphere_policy_flags(
            &missing_atmosphere, TAIYIN_NATIVE_ATMOSPHERE_ALLOW_STANDARD_FALLBACK),
        taiyin::TAIYIN_STATUS_OK,
        "public planet enables standard-atmosphere fallback",
        failures);
    expect_status(
        search_planet_rise_set_ut(
            &missing_atmosphere,
            TAIYIN_BODY_MERCURY,
            start,
            start + 1.0,
            TAIYIN_PLANET_VISIBILITY_EVENT_RISE,
            TAIYIN_PLANET_VISIBILITY_LIMB_CENTER,
            0u,
            &result),
        taiyin::TAIYIN_STATUS_OK,
        "public planet center uses standard-atmosphere fallback",
        failures);
    expect_status(
        search_planet_rise_set_ut(
            &missing_atmosphere,
            TAIYIN_BODY_MERCURY,
            start,
            start + 1.0,
            TAIYIN_PLANET_VISIBILITY_EVENT_RISE,
            TAIYIN_PLANET_VISIBILITY_LIMB_CENTER,
            TAIYIN_PLANET_VISIBILITY_STRICT_METEOROLOGY,
            &result),
        taiyin::TAIYIN_ERROR_INVALID_ARGUMENT,
        "public planet center strict mode bypasses standard atmosphere",
        failures);
}

void test_public_high_latitude_no_event_oracles(int* failures) {
    using namespace taiyin;
    using namespace taiyin::runtime;

    NativeCalcContext context = make_context(15.6333, 78.2232, 10.0);
    PlanetVisibilityEventResult result;

    struct NoEventCase {
        const char* label;
        taiyin::SplitJulianDate start_jd_ut;
        int event_kind;
        int expected_state;
    };

    const NoEventCase cases[] = {
        {"public Longyearbyen summer Mercury rise always above", jd_ut(2024, 6, 20, 22.0), TAIYIN_PLANET_VISIBILITY_EVENT_RISE, TAIYIN_PLANET_VISIBILITY_ALTITUDE_STATE_ALWAYS_ABOVE},
        {"public Longyearbyen summer Mercury set always above", jd_ut(2024, 6, 20, 22.0), TAIYIN_PLANET_VISIBILITY_EVENT_SET, TAIYIN_PLANET_VISIBILITY_ALTITUDE_STATE_ALWAYS_ABOVE},
        {"public Longyearbyen winter Mercury rise always below", jd_ut(2024, 12, 20, 23.0), TAIYIN_PLANET_VISIBILITY_EVENT_RISE, TAIYIN_PLANET_VISIBILITY_ALTITUDE_STATE_ALWAYS_BELOW},
        {"public Longyearbyen winter Mercury set always below", jd_ut(2024, 12, 20, 23.0), TAIYIN_PLANET_VISIBILITY_EVENT_SET, TAIYIN_PLANET_VISIBILITY_ALTITUDE_STATE_ALWAYS_BELOW},
    };

    for (const NoEventCase& c : cases) {
        expect_status(
            search_planet_rise_set_ut(
                &context,
                TAIYIN_BODY_MERCURY,
                c.start_jd_ut,
                c.start_jd_ut + 1.0,
                c.event_kind,
                TAIYIN_PLANET_VISIBILITY_LIMB_UPPER,
                TAIYIN_PLANET_VISIBILITY_FLAG_REFRACTION,
                &result),
            taiyin::TAIYIN_STATUS_OK,
            c.label,
            failures);
        expect_equal(result.altitude_state, c.expected_state, c.label, failures);
        expect_true(!taiyin::split_julian_date_is_finite(result.jd_ut), c.label, failures);
        if (c.expected_state == TAIYIN_PLANET_VISIBILITY_ALTITUDE_STATE_ALWAYS_ABOVE) {
            expect_true(result.min_residual_rad > 0.0, c.label, failures);
        } else {
            expect_true(result.max_residual_rad < 0.0, c.label, failures);
        }
    }
}

void test_public_rejects_barycenter_id(int* failures) {
    using namespace taiyin;
    using namespace taiyin::runtime;

    NativeCalcContext context = make_context(15.6333, 78.2232, 10.0);
    const taiyin::SplitJulianDate start = jd_ut(2024, 6, 20, 22.0);
    PlanetVisibilityEventResult result;
    expect_status(
        search_planet_rise_set_ut(
            &context,
            TAIYIN_BODY_JUPITER_BARYCENTER,
            start,
            start + 1.0,
            TAIYIN_PLANET_VISIBILITY_EVENT_RISE,
            TAIYIN_PLANET_VISIBILITY_LIMB_UPPER,
            TAIYIN_PLANET_VISIBILITY_FLAG_REFRACTION,
            &result),
        taiyin::TAIYIN_ERROR_INVALID_ARGUMENT,
        "public rejects Jupiter barycenter planet disc",
        failures);
}

}  // namespace

int main() {
    int failures = 0;
    if (initialize_runtime(&failures)) {
        test_public_denver_oracles(&failures);
        test_public_custom_horizon(&failures);
        test_public_flag_validation(&failures);
        test_public_high_latitude_no_event_oracles(&failures);
        test_public_rejects_barycenter_id(&failures);
    }
    if (failures != 0) {
        std::cerr << failures << " public planet visibility checks failed\n";
        return 1;
    }
    return 0;
}
