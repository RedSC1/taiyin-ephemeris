#include "runtime/visibility/planet_visibility_internal.h"
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
    int body_id;
    int event_kind;
    int limb_kind;
    uint32_t flags;
    double swiss_jd_ut;
    double tolerance_seconds;
};

struct TransitOracle {
    const char* label;
    int body_id;
    int event_kind;
    double swiss_jd_ut;
    double tolerance_seconds;
};

void test_denver_oracles(int* failures) {
    using namespace taiyin;
    using namespace taiyin::runtime;

    NativeCalcContext context = make_context(-104.9903, 39.7392, 1609.0);
    const taiyin::SplitJulianDate start = split_jd(jd_ut(2024, 4, 8, 6.0));
    const taiyin::SplitJulianDate end = start + 1.0;
    EphemerisEvalDiagnostic diagnostic;
    VisibilityAltitudeSearchResult result;

    const RiseSetOracle rise_set_oracles[] = {
        {"denver_mercury_rise_no_refraction", TAIYIN_BODY_MERCURY, TAIYIN_PLANET_VISIBILITY_EVENT_RISE, TAIYIN_PLANET_VISIBILITY_LIMB_UPPER, 0u, 2460409.025837766007, 1.0},
        {"denver_mercury_set_no_refraction", TAIYIN_BODY_MERCURY, TAIYIN_PLANET_VISIBILITY_EVENT_SET, TAIYIN_PLANET_VISIBILITY_LIMB_UPPER, 0u, 2460409.581091097556, 1.0},
        {"denver_venus_rise_no_refraction", TAIYIN_BODY_VENUS, TAIYIN_PLANET_VISIBILITY_EVENT_RISE, TAIYIN_PLANET_VISIBILITY_LIMB_UPPER, 0u, 2460409.005294922739, 1.0},
        {"denver_venus_set_no_refraction", TAIYIN_BODY_VENUS, TAIYIN_PLANET_VISIBILITY_EVENT_SET, TAIYIN_PLANET_VISIBILITY_LIMB_UPPER, 0u, 2460409.507296646014, 1.0},
        {"denver_venus_rise_refraction", TAIYIN_BODY_VENUS, TAIYIN_PLANET_VISIBILITY_EVENT_RISE, TAIYIN_PLANET_VISIBILITY_LIMB_UPPER, TAIYIN_PLANET_VISIBILITY_FLAG_REFRACTION, 2460409.003273752518, 5.0},
        {"denver_venus_set_lower_no_refraction", TAIYIN_BODY_VENUS, TAIYIN_PLANET_VISIBILITY_EVENT_SET, TAIYIN_PLANET_VISIBILITY_LIMB_LOWER, 0u, 2460409.507286465727, 1.0},
    };

    for (const RiseSetOracle& oracle : rise_set_oracles) {
        expect_status(
            planet_visibility_search_rise_set_ut(
                &context,
                oracle.body_id,
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
            oracle.event_kind == TAIYIN_PLANET_VISIBILITY_EVENT_RISE
                ? TAIYIN_VISIBILITY_CROSSING_RISING
                : TAIYIN_VISIBILITY_CROSSING_SETTING,
            oracle.label,
            failures);
    }
}

void test_transit_oracles(int* failures) {
    using namespace taiyin;
    using namespace taiyin::runtime;

    NativeCalcContext context = make_context(-104.9903, 39.7392, 1609.0);
    const taiyin::SplitJulianDate start = split_jd(jd_ut(2024, 4, 8, 6.0));
    EphemerisEvalDiagnostic diagnostic;
    VisibilityAltitudeSearchResult result;

    const TransitOracle oracles[] = {
        {"denver_mercury_upper_transit", TAIYIN_BODY_MERCURY, TAIYIN_PLANET_VISIBILITY_EVENT_UPPER_TRANSIT, 2460409.303728295024, 2.0},
        {"denver_mercury_lower_transit", TAIYIN_BODY_MERCURY, TAIYIN_PLANET_VISIBILITY_EVENT_LOWER_TRANSIT, 2460408.805835458450, 2.0},
        {"denver_venus_upper_transit", TAIYIN_BODY_VENUS, TAIYIN_PLANET_VISIBILITY_EVENT_UPPER_TRANSIT, 2460409.256011750549, 2.0},
    };

    for (const TransitOracle& oracle : oracles) {
        expect_status(
            planet_visibility_search_transit_ut(
                &context,
                oracle.body_id,
                start,
                start + 1.0,
                oracle.event_kind,
                &result,
                &diagnostic),
            taiyin::TAIYIN_STATUS_OK,
            oracle.label,
            failures);
        expect_equal(result.altitude_state, TAIYIN_VISIBILITY_ALTITUDE_STATE_CROSSES, oracle.label, failures);
        expect_near(result.jd_ut, oracle.swiss_jd_ut, oracle.tolerance_seconds / 86400.0, oracle.label, failures);
    }
}

void test_cob_planet_route_smoke(int* failures) {
    using namespace taiyin;
    using namespace taiyin::runtime;

    NativeCalcContext context = make_context(-104.9903, 39.7392, 1609.0);
    const taiyin::SplitJulianDate start = split_jd(jd_ut(2024, 4, 8, 6.0));
    EphemerisEvalDiagnostic diagnostic;
    VisibilityAltitudeSearchResult result;

    const int bodies[] = { TAIYIN_BODY_JUPITER, TAIYIN_BODY_SATURN, TAIYIN_BODY_NEPTUNE };
    const char* labels[] = { "Jupiter", "Saturn", "Neptune" };
    for (int i = 0; i < 3; ++i) {
        expect_status(
            planet_visibility_search_rise_set_ut(
                &context,
                bodies[i],
                start,
                start + 1.0,
                TAIYIN_PLANET_VISIBILITY_EVENT_RISE,
                TAIYIN_PLANET_VISIBILITY_LIMB_UPPER,
                0u,
                &result,
                &diagnostic),
            taiyin::TAIYIN_STATUS_OK,
            labels[i],
            failures);
        expect_equal(result.altitude_state, TAIYIN_VISIBILITY_ALTITUDE_STATE_CROSSES, labels[i], failures);
        expect_true(taiyin::split_julian_date_is_finite(result.jd_ut), labels[i], failures);

        expect_status(
            planet_visibility_search_transit_ut(
                &context,
                bodies[i],
                start,
                start + 1.0,
                TAIYIN_PLANET_VISIBILITY_EVENT_UPPER_TRANSIT,
                &result,
                &diagnostic),
            taiyin::TAIYIN_STATUS_OK,
            labels[i],
            failures);
        expect_equal(result.altitude_state, TAIYIN_VISIBILITY_ALTITUDE_STATE_CROSSES, labels[i], failures);
        expect_true(taiyin::split_julian_date_is_finite(result.jd_ut), labels[i], failures);
    }
}

void test_high_latitude_no_event_oracles(int* failures) {
    using namespace taiyin;
    using namespace taiyin::runtime;

    NativeCalcContext context = make_context(15.6333, 78.2232, 10.0);
    EphemerisEvalDiagnostic diagnostic;
    VisibilityAltitudeSearchResult result;

    struct NoEventCase {
        const char* label;
        double start_jd_ut;
        int event_kind;
        int expected_state;
    };

    const NoEventCase cases[] = {
        {"Longyearbyen summer Mercury rise always above", jd_ut(2024, 6, 20, 22.0), TAIYIN_PLANET_VISIBILITY_EVENT_RISE, TAIYIN_VISIBILITY_ALTITUDE_STATE_ALWAYS_ABOVE},
        {"Longyearbyen summer Mercury set always above", jd_ut(2024, 6, 20, 22.0), TAIYIN_PLANET_VISIBILITY_EVENT_SET, TAIYIN_VISIBILITY_ALTITUDE_STATE_ALWAYS_ABOVE},
        {"Longyearbyen winter Mercury rise always below", jd_ut(2024, 12, 20, 23.0), TAIYIN_PLANET_VISIBILITY_EVENT_RISE, TAIYIN_VISIBILITY_ALTITUDE_STATE_ALWAYS_BELOW},
        {"Longyearbyen winter Mercury set always below", jd_ut(2024, 12, 20, 23.0), TAIYIN_PLANET_VISIBILITY_EVENT_SET, TAIYIN_VISIBILITY_ALTITUDE_STATE_ALWAYS_BELOW},
    };

    for (const NoEventCase& c : cases) {
        expect_status(
            planet_visibility_search_rise_set_ut(
                &context,
                TAIYIN_BODY_MERCURY,
                split_jd(c.start_jd_ut),
                split_jd(c.start_jd_ut + 1.0),
                c.event_kind,
                TAIYIN_PLANET_VISIBILITY_LIMB_UPPER,
                TAIYIN_PLANET_VISIBILITY_FLAG_REFRACTION,
                &result,
                &diagnostic),
            taiyin::TAIYIN_STATUS_OK,
            c.label,
            failures);
        expect_equal(result.altitude_state, c.expected_state, c.label, failures);
        if (c.expected_state == TAIYIN_VISIBILITY_ALTITUDE_STATE_ALWAYS_ABOVE) {
            expect_true(result.min_residual_rad > 0.0, c.label, failures);
        } else {
            expect_true(result.max_residual_rad < 0.0, c.label, failures);
        }
    }
}

void test_barycenter_ids_are_not_planet_discs(int* failures) {
    using namespace taiyin;
    using namespace taiyin::runtime;

    NativeCalcContext context = make_context(15.6333, 78.2232, 10.0);
    const taiyin::SplitJulianDate start = split_jd(jd_ut(2024, 6, 20, 22.0));
    EphemerisEvalDiagnostic diagnostic;
    VisibilityAltitudeSearchResult result;

    expect_status(
        planet_visibility_search_rise_set_ut(
            &context,
            TAIYIN_BODY_JUPITER_BARYCENTER,
            start,
            start + 1.0,
            TAIYIN_PLANET_VISIBILITY_EVENT_RISE,
            TAIYIN_PLANET_VISIBILITY_LIMB_UPPER,
            TAIYIN_PLANET_VISIBILITY_FLAG_REFRACTION,
            &result,
            &diagnostic),
        taiyin::TAIYIN_ERROR_INVALID_ARGUMENT,
        "reject Jupiter barycenter as planet disc",
        failures);

    expect_status(
        planet_visibility_search_rise_set_ut(
            &context,
            TAIYIN_BODY_MARS_BARYCENTER,
            start,
            start + 1.0,
            TAIYIN_PLANET_VISIBILITY_EVENT_SET,
            TAIYIN_PLANET_VISIBILITY_LIMB_UPPER,
            0u,
            &result,
            &diagnostic),
        taiyin::TAIYIN_ERROR_INVALID_ARGUMENT,
        "reject Mars barycenter as planet disc",
        failures);
}

void test_invalid_arguments(int* failures) {
    using namespace taiyin;
    using namespace taiyin::runtime;

    NativeCalcContext context = make_context(-104.9903, 39.7392, 1609.0);
    VisibilityAltitudeSearchResult result;
    EphemerisEvalDiagnostic diagnostic;
    const taiyin::SplitJulianDate start = split_jd(jd_ut(2024, 4, 8, 6.0));

    expect_status(
        planet_visibility_search_rise_set_ut(
            &context,
            TAIYIN_BODY_SUN,
            start,
            start + 1.0,
            TAIYIN_PLANET_VISIBILITY_EVENT_RISE,
            TAIYIN_PLANET_VISIBILITY_LIMB_UPPER,
            0u,
            &result,
            &diagnostic),
        taiyin::TAIYIN_ERROR_INVALID_ARGUMENT,
        "reject non-planet body",
        failures);
    expect_status(
        planet_visibility_search_rise_set_ut(
            &context,
            TAIYIN_BODY_JUPITER_BARYCENTER,
            start,
            start + 1.0,
            TAIYIN_PLANET_VISIBILITY_EVENT_RISE,
            TAIYIN_PLANET_VISIBILITY_LIMB_UPPER,
            0u,
            &result,
            &diagnostic),
        taiyin::TAIYIN_ERROR_INVALID_ARGUMENT,
        "reject barycenter body",
        failures);
    expect_status(
        planet_visibility_search_rise_set_at_horizon_ut(
            &context,
            TAIYIN_BODY_MERCURY,
            start,
            start + 1.0,
            TAIYIN_PLANET_VISIBILITY_EVENT_RISE,
            TAIYIN_PLANET_VISIBILITY_LIMB_UPPER,
            NAN,
            0u,
            &result,
            &diagnostic),
        taiyin::TAIYIN_ERROR_INVALID_ARGUMENT,
        "reject NaN horizon",
        failures);
}

}  // namespace

int main() {
    int failures = 0;
    if (initialize_runtime(&failures)) {
        test_denver_oracles(&failures);
        test_transit_oracles(&failures);
        test_cob_planet_route_smoke(&failures);
        test_high_latitude_no_event_oracles(&failures);
        test_barycenter_ids_are_not_planet_discs(&failures);
        test_invalid_arguments(&failures);
    }
    if (failures != 0) {
        std::cerr << failures << " planet visibility checks failed\n";
        return 1;
    }
    return 0;
}
