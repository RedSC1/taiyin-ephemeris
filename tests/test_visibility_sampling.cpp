#include "runtime/visibility/visibility_sampling_internal.h"

#include "taiyin/body_id.h"
#include "taiyin/dispatch.h"
#include "taiyin/runtime/native_context.h"
#include "taiyin/runtime/observed_position.h"
#include "taiyin/runtime/runtime.h"
#include "taiyin/status.h"
#include "taiyin/time.h"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <string>

namespace {

constexpr int kCustomRefractionModelId = taiyin::dispatch::REFRACTION_CUSTOM_START + 37;
constexpr double kCustomRefractionOffsetRad = 0.0123456789;

double custom_refraction_offset(const void* data) {
    const taiyin::dispatch::RefractionDispatchData* refraction = static_cast<const taiyin::dispatch::RefractionDispatchData*>(data);
    if (!refraction || !std::isfinite(refraction->altitude_rad)) {
        return std::numeric_limits<double>::quiet_NaN();
    }
    return kCustomRefractionOffsetRad;
}

void expect_true(bool value, const char* label, int* failures) {
    if (!value) {
        std::cerr << "FAIL: expected true: " << label << "\n";
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

taiyin::runtime::NativeCalcContext make_context(bool atmosphere) {
    taiyin::runtime::NativeCalcContext context;
    taiyin::runtime::native_context_set_geocentric_observer(
        &context,
        taiyin::TAIYIN_BODY_EARTH,
        taiyin::TAIYIN_BODY_EARTH);
    taiyin::runtime::native_context_set_observer_location(
        &context,
        taiyin::runtime::native_observer_location_degrees(-104.9903, 39.7392, 1609.0));
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

const taiyin::SplitJulianDate jd_ut(int year, int month, int day, double hour) {
    taiyin::SplitJulianDate out;
    taiyin::split_julian_date_from_double(
        taiyin::julian_day({year, month, day, static_cast<int>(hour), 0, (hour - static_cast<int>(hour)) * 3600.0}),
        &out);
    return out;
}

void test_generic_center_sampling(int* failures) {
    using namespace taiyin;
    using namespace taiyin::runtime;

    NativeCalcContext context = make_context(false);
    EphemerisEvalDiagnostic diagnostic;
    const SplitJulianDate t = jd_ut(2024, 4, 8, 18.0);
    for (int body : {TAIYIN_BODY_SUN, TAIYIN_BODY_MOON}) {
        double altitude = 0.0;
        double azimuth = 0.0;
        double hour_angle = 0.0;
        double distance = 0.0;
        double ra = 0.0;
        double dec = 0.0;
        expect_status(
            visibility_sample_body_center_horizontal_ut(
                &context, body, t, 0, &altitude, &azimuth, &hour_angle, &distance, &ra, &dec, &diagnostic),
            TAIYIN_STATUS_OK,
            "generic center horizontal sample",
            failures);
        expect_true(std::isfinite(altitude), "finite altitude", failures);
        expect_true(std::isfinite(azimuth), "finite azimuth", failures);
        expect_true(std::isfinite(hour_angle), "finite hour angle", failures);
        expect_true(distance > 0.0, "positive distance", failures);
        expect_true(std::isfinite(ra), "finite apparent RA", failures);
        expect_true(std::isfinite(dec), "finite apparent Dec", failures);
    }
}

void test_center_residual(int* failures) {
    using namespace taiyin;
    using namespace taiyin::runtime;

    NativeCalcContext context = make_context(false);
    EphemerisEvalDiagnostic diagnostic;
    const SplitJulianDate t = jd_ut(2024, 4, 8, 18.0);
    double residual = 0.0;
    double altitude = 0.0;
    expect_status(
        visibility_sample_body_center_residual_ut(
            &context, TAIYIN_BODY_SUN, t, 0.1, 0, &residual, &altitude, 0, 0, 0, &diagnostic),
        TAIYIN_STATUS_OK,
        "center residual sample",
        failures);
    expect_near(residual, altitude - 0.1, 1.0e-15, "center residual equals altitude minus target", failures);

    residual = 1.0;
    altitude = 2.0;
    double azimuth = 3.0;
    double hour_angle = 4.0;
    double distance = 5.0;
    expect_status(
        visibility_sample_body_center_residual_ut(
            &context, TAIYIN_BODY_SUN, t, NAN, 0,
            &residual, &altitude, &azimuth, &hour_angle, &distance, &diagnostic),
        TAIYIN_ERROR_INVALID_ARGUMENT,
        "center residual rejects non-finite target altitude",
        failures);
    expect_true(
        std::isnan(residual) && std::isnan(altitude) && std::isnan(azimuth)
            && std::isnan(hour_angle) && std::isnan(distance),
        "failed center residual clears every requested output",
        failures);
}

void test_hour_angle_altitude_consistency(int* failures) {
    using namespace taiyin;
    using namespace taiyin::runtime;

    NativeCalcContext context = make_context(false);
    EphemerisEvalDiagnostic diagnostic;
    const SplitJulianDate t = jd_ut(2024, 4, 8, 18.0);
    double altitude = 0.0;
    double azimuth = 0.0;
    double hour_angle = 0.0;
    double distance = 0.0;
    double ra = 0.0;
    double dec = 0.0;
    expect_status(
        visibility_sample_body_center_horizontal_ut(
            &context, TAIYIN_BODY_SUN, t, 0, &altitude, &azimuth, &hour_angle, &distance, &ra, &dec, &diagnostic),
        TAIYIN_STATUS_OK,
        "sample for hour angle consistency",
        failures);
    const double sin_alt = std::sin(context.observer_location.latitude_rad) * std::sin(dec)
        + std::cos(context.observer_location.latitude_rad) * std::cos(dec) * std::cos(hour_angle);
    const double reconstructed_altitude = std::asin(std::fmax(-1.0, std::fmin(1.0, sin_alt)));
    expect_near(reconstructed_altitude, altitude, 1.0e-12, "hour angle reconstructs altitude", failures);
}

void test_refraction_requires_atmosphere(int* failures) {
    using namespace taiyin;
    using namespace taiyin::runtime;

    EphemerisEvalDiagnostic diagnostic;
    const SplitJulianDate t = jd_ut(2024, 4, 8, 18.0);
    double altitude = 0.0;
    double azimuth = 0.0;
    double hour_angle = 0.0;
    double distance = 0.0;

    NativeCalcContext missing = make_context(false);
    expect_status(
        visibility_sample_body_center_horizontal_ut(
            &missing, TAIYIN_BODY_SUN, t, TAIYIN_OBSERVED_REFRACTION, &altitude, &azimuth, &hour_angle, &distance, 0, 0, &diagnostic),
        TAIYIN_ERROR_INVALID_ARGUMENT,
        "refraction requires atmosphere",
        failures);

    NativeCalcContext standard = make_context(false);
    expect_status(
        native_context_set_atmosphere_policy_flags(
            &standard, TAIYIN_NATIVE_ATMOSPHERE_ALLOW_STANDARD_FALLBACK),
        TAIYIN_STATUS_OK,
        "enable standard-atmosphere fallback",
        failures);
    expect_status(
        visibility_sample_body_center_horizontal_ut(
            &standard, TAIYIN_BODY_SUN, t, TAIYIN_OBSERVED_REFRACTION,
            &altitude, &azimuth, &hour_angle, &distance, 0, 0, &diagnostic),
        TAIYIN_STATUS_OK,
        "refraction can use opted-in standard atmosphere",
        failures);
    expect_status(
        visibility_sample_body_center_horizontal_ut(
            &standard, TAIYIN_BODY_SUN, t,
            TAIYIN_OBSERVED_REFRACTION | TAIYIN_OBSERVED_STRICT_METEOROLOGY,
            &altitude, &azimuth, &hour_angle, &distance, 0, 0, &diagnostic),
        TAIYIN_ERROR_INVALID_ARGUMENT,
        "strict refraction bypasses standard atmosphere fallback",
        failures);

    NativeCalcContext sofa_standard = make_context(false);
    expect_status(
        native_context_set_atmosphere_policy_flags(
            &sofa_standard, TAIYIN_NATIVE_ATMOSPHERE_ALLOW_STANDARD_FALLBACK),
        TAIYIN_STATUS_OK,
        "enable SOFA standard-atmosphere fallback",
        failures);
    expect_status(
        native_context_set_refraction_model(&sofa_standard, dispatch::REFRACTION_SOFA),
        TAIYIN_STATUS_OK,
        "select SOFA refraction",
        failures);
    const double sofa_true_altitude = 0.1;
    double sofa_apparent_altitude = 0.0;
    expect_status(
        visibility_apply_refraction_from_context(
            &sofa_standard, sofa_true_altitude, 0u, &sofa_apparent_altitude),
        TAIYIN_STATUS_OK,
        "SOFA standard-atmosphere fallback succeeds",
        failures);
    const double height_m = sofa_standard.observer_location.height_m;
    const double expected_pressure_mbar = 1013.25 * std::pow(
        1.0 - 0.0065 * height_m / 288.15, 5.255);
    const double expected_temperature_celsius = 15.0 - 0.0065 * height_m;
    expect_near(
        sofa_apparent_altitude,
        sofa_true_altitude + atmospheric_refraction_sofa_rad(
            sofa_true_altitude,
            expected_pressure_mbar,
            expected_temperature_celsius,
            0.40,
            0.55),
        1.0e-15,
        "SOFA standard fallback uses 40 percent humidity",
        failures);

    NativeCalcContext with_atmosphere = make_context(true);
    expect_status(
        visibility_sample_body_center_horizontal_ut(
            &with_atmosphere, TAIYIN_BODY_SUN, t, TAIYIN_OBSERVED_REFRACTION, &altitude, &azimuth, &hour_angle, &distance, 0, 0, &diagnostic),
        TAIYIN_STATUS_OK,
        "refraction with atmosphere succeeds",
        failures);
}

void test_refraction_helper_uses_context_dispatch(int* failures) {
    using namespace taiyin;
    using namespace taiyin::runtime;

    dispatch::register_refraction_model(kCustomRefractionModelId, custom_refraction_offset);
    NativeCalcContext context = make_context(false);
    native_context_set_refraction_model(&context, kCustomRefractionModelId);
    const double true_altitude = 0.1;
    double apparent_altitude = 0.0;
    expect_status(
        visibility_apply_refraction_from_context(&context, true_altitude, 0u, &apparent_altitude),
        TAIYIN_STATUS_OK,
        "custom context refraction without atmosphere succeeds",
        failures);
    expect_near(
        apparent_altitude,
        true_altitude + kCustomRefractionOffsetRad,
        1.0e-15,
        "custom context refraction without atmosphere is used",
        failures);
}

}  // namespace

int main() {
    int failures = 0;
    if (initialize_runtime(&failures)) {
        test_generic_center_sampling(&failures);
        test_center_residual(&failures);
        test_hour_angle_altitude_consistency(&failures);
        test_refraction_requires_atmosphere(&failures);
        test_refraction_helper_uses_context_dispatch(&failures);
    }
    if (failures != 0) {
        std::cerr << failures << " visibility sampling checks failed\n";
        return 1;
    }
    return 0;
}
