#include "runtime/visibility/visibility_angle_search_internal.h"
#include "runtime/visibility/visibility_math_internal.h"
#include "runtime/visibility/visibility_sampling_internal.h"
#include "runtime/visibility/visibility_search_internal.h"

#include "taiyin/angle.h"
#include "taiyin/body_id.h"
#include "taiyin/runtime/native_context.h"
#include "taiyin/runtime/observed_position.h"
#include "taiyin/runtime/runtime.h"
#include "taiyin/status.h"
#include "taiyin/time.h"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <string>

namespace {

constexpr double kSunRadiusKm = 695700.0;

struct SyntheticResidualData {
    double center_jd_ut;
    double scale;
};

struct SyntheticAngleData {
    double fail_after_jd_ut;
    double fail_reference_min_rad;
};

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

double scalar_jd(const taiyin::SplitJulianDate& jd) {
    return taiyin::split_julian_date_to_double(jd);
}

void expect_near(
    const taiyin::SplitJulianDate& actual,
    double expected,
    double tolerance,
    const char* label,
    int* failures
) {
    expect_near(scalar_jd(actual), expected, tolerance, label, failures);
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

taiyin::Status synthetic_parabola_residual(
    const void* user_data,
    const taiyin::SplitJulianDate& jd_ut,
    double* out_residual_rad,
    taiyin::runtime::EphemerisEvalDiagnostic* diagnostic
) noexcept {
    if (!user_data || !out_residual_rad) return taiyin::TAIYIN_ERROR_INVALID_ARGUMENT;
    const SyntheticResidualData* data = static_cast<const SyntheticResidualData*>(user_data);
    const double dt = scalar_jd(jd_ut) - data->center_jd_ut;
    *out_residual_rad = data->scale * dt * dt;
    (void)diagnostic;
    return taiyin::TAIYIN_STATUS_OK;
}

taiyin::Status synthetic_angle_sample(
    const void* user_data,
    const taiyin::SplitJulianDate& jd_ut,
    double reference_angle_rad,
    bool has_reference,
    double* out_angle_rad,
    taiyin::runtime::EphemerisEvalDiagnostic* diagnostic
) noexcept {
    if (!user_data || !out_angle_rad) return taiyin::TAIYIN_ERROR_INVALID_ARGUMENT;
    const SyntheticAngleData* data = static_cast<const SyntheticAngleData*>(user_data);
    const double jd_ut_value = scalar_jd(jd_ut);
    if (jd_ut_value >= data->fail_after_jd_ut
        && (!has_reference || reference_angle_rad >= data->fail_reference_min_rad)) {
        *out_angle_rad = std::nan("");
    } else {
        *out_angle_rad = jd_ut_value - 1000.0;
    }
    (void)reference_angle_rad;
    (void)has_reference;
    (void)diagnostic;
    return taiyin::TAIYIN_STATUS_OK;
}

taiyin::runtime::VisibilityAltitudeSearchSpec synthetic_spec(
    const SyntheticResidualData* data
) {
    taiyin::runtime::VisibilityAltitudeSearchSpec spec;
    spec.crossing_direction = taiyin::runtime::TAIYIN_VISIBILITY_CROSSING_ANY;
    spec.start_jd_ut = split_jd(1000.0);
    spec.end_jd_ut = split_jd(1001.0);
    spec.target_altitude_rad = 0.0;
    spec.coarse_step_days = 0.25;
    spec.root_tolerance_days = 1.0e-12;
    spec.residual_tolerance_rad = 1.0e-12;
    spec.residual_sampler = synthetic_parabola_residual;
    spec.residual_sampler_data = data;
    return spec;
}

taiyin::runtime::VisibilityAngleTargetSearchSpec synthetic_angle_spec(
    const SyntheticAngleData* data,
    double base_target_rad
) {
    taiyin::runtime::VisibilityAngleTargetSearchSpec spec;
    spec.start_jd_ut = split_jd(1000.0);
    spec.end_jd_ut = split_jd(1001.0);
    spec.base_target_rad = base_target_rad;
    spec.coarse_step_days = 0.25;
    spec.root_tolerance_days = 1.0e-12;
    spec.residual_tolerance_rad = 1.0e-12;
    spec.sample = synthetic_angle_sample;
    spec.user_data = data;
    return spec;
}

double sample_center_altitude(
    const taiyin::runtime::NativeCalcContext* context,
    int body_id,
    double jd_ut,
    int* failures,
    const char* label
) {
    double altitude = 0.0;
    double azimuth = 0.0;
    double hour_angle = 0.0;
    double distance = 0.0;
    taiyin::runtime::EphemerisEvalDiagnostic diagnostic;
    const taiyin::Status st = taiyin::runtime::visibility_sample_body_center_horizontal_ut(
        context,
        body_id,
        split_jd(jd_ut),
        0u,
        &altitude,
        &azimuth,
        &hour_angle,
        &distance,
        0,
        0,
        &diagnostic);
    expect_status(st, taiyin::TAIYIN_STATUS_OK, label, failures);
    (void)azimuth;
    (void)hour_angle;
    (void)distance;
    return altitude;
}

double maximize_center_altitude_time(
    const taiyin::runtime::NativeCalcContext* context,
    int body_id,
    double guess_jd_ut,
    int* failures,
    const char* label
) {
    double left = guess_jd_ut - 0.08;
    double right = guess_jd_ut + 0.08;
    for (int i = 0; i < 70; ++i) {
        const double m1 = left + (right - left) / 3.0;
        const double m2 = right - (right - left) / 3.0;
        const double y1 = sample_center_altitude(context, body_id, m1, failures, label);
        const double y2 = sample_center_altitude(context, body_id, m2, failures, label);
        if (y1 < y2) left = m1;
        else right = m2;
    }
    return 0.5 * (left + right);
}

taiyin::runtime::VisibilityAltitudeSearchSpec solar_spec(
    int residual_mode,
    int crossing_direction,
    double start_jd_ut,
    uint64_t observed_flags
) {
    taiyin::runtime::VisibilityAltitudeSearchSpec spec;
    spec.body_id = taiyin::TAIYIN_BODY_SUN;
    spec.residual_mode = residual_mode;
    spec.crossing_direction = crossing_direction;
    spec.start_jd_ut = split_jd(start_jd_ut);
    spec.end_jd_ut = split_jd(start_jd_ut + 1.0);
    spec.target_altitude_rad = 0.0;
    spec.physical_radius_km = kSunRadiusKm;
    spec.coarse_step_days = 2.0 / 24.0;
    spec.root_tolerance_days = 1.0e-10;
    spec.residual_tolerance_rad = 1.0e-10;
    spec.observed_flags = observed_flags;
    return spec;
}

void test_denver_solar_rise_oracles(int* failures) {
    using namespace taiyin::runtime;

    NativeCalcContext context = make_context(-104.9903, 39.7392, 1609.0);
    const double start = jd_ut(2024, 4, 8, 6.0);

    EphemerisEvalDiagnostic diagnostic;
    VisibilityAltitudeSearchResult result;

    VisibilityAltitudeSearchSpec no_refraction = solar_spec(
        TAIYIN_VISIBILITY_RESIDUAL_TRUE_UPPER_LIMB,
        TAIYIN_VISIBILITY_CROSSING_RISING,
        start,
        0u);
    expect_status(
        visibility_search_altitude_interval_ut(&context, no_refraction, &result, &diagnostic),
        taiyin::TAIYIN_STATUS_OK,
        "Denver no-refraction sunrise search",
        failures);
    expect_equal(result.altitude_state, TAIYIN_VISIBILITY_ALTITUDE_STATE_CROSSES, "Denver no-refraction crosses", failures);
    expect_near((scalar_jd(result.jd_ut) - 2460409.024388575461) * 86400.0, 0.034963, 0.25, "Denver no-refraction sunrise oracle seconds", failures);

    VisibilityAltitudeSearchSpec disc_center = solar_spec(
        TAIYIN_VISIBILITY_RESIDUAL_CENTER_ALTITUDE,
        TAIYIN_VISIBILITY_CROSSING_RISING,
        start,
        TAIYIN_OBSERVED_REFRACTION);
    expect_status(
        visibility_search_altitude_interval_ut(&context, disc_center, &result, &diagnostic),
        taiyin::TAIYIN_STATUS_OK,
        "Denver disc-center refraction sunrise search",
        failures);
    expect_equal(result.altitude_state, TAIYIN_VISIBILITY_ALTITUDE_STATE_CROSSES, "Denver disc-center crosses", failures);
    expect_near((scalar_jd(result.jd_ut) - 2460409.023311988916) * 86400.0, -1.336022, 0.25, "Denver disc-center sunrise oracle seconds", failures);

    VisibilityAltitudeSearchSpec default_upper_limb = solar_spec(
        TAIYIN_VISIBILITY_RESIDUAL_APPARENT_UPPER_LIMB,
        TAIYIN_VISIBILITY_CROSSING_RISING,
        start,
        0u);
    expect_status(
        visibility_search_altitude_interval_ut(&context, default_upper_limb, &result, &diagnostic),
        taiyin::TAIYIN_STATUS_OK,
        "Denver default upper-limb sunrise search",
        failures);
    expect_equal(result.altitude_state, TAIYIN_VISIBILITY_ALTITUDE_STATE_CROSSES, "Denver default upper-limb crosses", failures);
    expect_near((scalar_jd(result.jd_ut) - 2460409.022335547954) * 86400.0, -1.301381, 0.25, "Denver default upper-limb sunrise oracle seconds", failures);
}

void test_polar_classification(int* failures) {
    using namespace taiyin::runtime;

    EphemerisEvalDiagnostic diagnostic;
    VisibilityAltitudeSearchResult result;

    NativeCalcContext longyearbyen = make_context(15.6333, 78.2232, 10.0);
    VisibilityAltitudeSearchSpec summer = solar_spec(
        TAIYIN_VISIBILITY_RESIDUAL_APPARENT_UPPER_LIMB,
        TAIYIN_VISIBILITY_CROSSING_RISING,
        jd_ut(2024, 6, 20, 22.0),
        0u);
    expect_status(
        visibility_search_altitude_interval_ut(&longyearbyen, summer, &result, &diagnostic),
        taiyin::TAIYIN_STATUS_OK,
        "Longyearbyen summer search",
        failures);
    expect_equal(result.altitude_state, TAIYIN_VISIBILITY_ALTITUDE_STATE_ALWAYS_ABOVE, "Longyearbyen summer always above", failures);
    expect_true(result.min_residual_rad > 0.0, "Longyearbyen summer min positive", failures);

    VisibilityAltitudeSearchSpec winter = solar_spec(
        TAIYIN_VISIBILITY_RESIDUAL_APPARENT_UPPER_LIMB,
        TAIYIN_VISIBILITY_CROSSING_RISING,
        jd_ut(2024, 12, 20, 23.0),
        0u);
    expect_status(
        visibility_search_altitude_interval_ut(&longyearbyen, winter, &result, &diagnostic),
        taiyin::TAIYIN_STATUS_OK,
        "Longyearbyen winter search",
        failures);
    expect_equal(result.altitude_state, TAIYIN_VISIBILITY_ALTITUDE_STATE_ALWAYS_BELOW, "Longyearbyen winter always below", failures);
    expect_true(result.max_residual_rad < 0.0, "Longyearbyen winter max negative", failures);
}

void test_synthetic_tangent_classification(int* failures) {
    using namespace taiyin::runtime;

    EphemerisEvalDiagnostic diagnostic;
    VisibilityAltitudeSearchResult result;

    const SyntheticResidualData lower_tangent = {1000.55, 1.0};
    VisibilityAltitudeSearchSpec lower_spec = synthetic_spec(&lower_tangent);
    expect_status(
        visibility_search_altitude_interval_ut(0, lower_spec, &result, &diagnostic),
        taiyin::TAIYIN_STATUS_OK,
        "synthetic lower tangent search",
        failures);
    expect_equal(result.altitude_state, TAIYIN_VISIBILITY_ALTITUDE_STATE_TANGENT, "synthetic lower tangent state", failures);
    expect_equal(result.crossing_direction, TAIYIN_VISIBILITY_CROSSING_ANY, "synthetic lower tangent direction", failures);
    expect_near(result.jd_ut, 1000.55, 1.0e-12, "synthetic lower tangent jd", failures);
    expect_near(result.residual_rad, 0.0, 1.0e-24, "synthetic lower tangent residual", failures);
    expect_near(result.min_residual_rad, 0.0, 0.0, "synthetic lower tangent min", failures);
    expect_true(result.max_residual_rad > 0.0, "synthetic lower tangent max positive", failures);

    const SyntheticResidualData upper_tangent = {1000.55, -1.0};
    VisibilityAltitudeSearchSpec upper_spec = synthetic_spec(&upper_tangent);
    expect_status(
        visibility_search_altitude_interval_ut(0, upper_spec, &result, &diagnostic),
        taiyin::TAIYIN_STATUS_OK,
        "synthetic upper tangent search",
        failures);
    expect_equal(result.altitude_state, TAIYIN_VISIBILITY_ALTITUDE_STATE_TANGENT, "synthetic upper tangent state", failures);
    expect_equal(result.crossing_direction, TAIYIN_VISIBILITY_CROSSING_ANY, "synthetic upper tangent direction", failures);
    expect_near(result.jd_ut, 1000.55, 1.0e-12, "synthetic upper tangent jd", failures);
    expect_near(result.residual_rad, -0.0, 1.0e-24, "synthetic upper tangent residual", failures);
    expect_true(result.min_residual_rad < 0.0, "synthetic upper tangent min negative", failures);
    expect_near(result.max_residual_rad, -0.0, 0.0, "synthetic upper tangent max", failures);

    const SyntheticResidualData clamped_final_step_tangent = {1000.85, 1.0};
    VisibilityAltitudeSearchSpec clamped_final_step_spec = synthetic_spec(&clamped_final_step_tangent);
    clamped_final_step_spec.end_jd_ut = split_jd(1000.9);
    expect_status(
        visibility_search_altitude_interval_ut(0, clamped_final_step_spec, &result, &diagnostic),
        taiyin::TAIYIN_STATUS_OK,
        "synthetic clamped final-step tangent search",
        failures);
    expect_equal(
        result.altitude_state,
        TAIYIN_VISIBILITY_ALTITUDE_STATE_TANGENT,
        "synthetic clamped final-step tangent state",
        failures);
    expect_near(result.jd_ut, 1000.85, 1.0e-12, "synthetic clamped final-step tangent jd", failures);
    expect_near(result.residual_rad, 0.0, 1.0e-24, "synthetic clamped final-step tangent residual", failures);
}

void test_synthetic_angle_rejects_nonfinite_samples(int* failures) {
    using namespace taiyin::runtime;

    EphemerisEvalDiagnostic diagnostic;
    VisibilityAltitudeSearchResult result;

    const SyntheticAngleData initial_sample_nan = {1000.0, -1.0};
    expect_status(
        visibility_search_continuous_angle_target_ut(
            synthetic_angle_spec(&initial_sample_nan, 0.5),
            &result,
            &diagnostic),
        taiyin::TAIYIN_EPHEMERIS_ERROR_EVAL_FAILED,
        "synthetic angle initial sample rejects NaN",
        failures);

    const SyntheticAngleData refine_sample_nan = {1000.1, 0.1};
    expect_status(
        visibility_search_continuous_angle_target_ut(
            synthetic_angle_spec(&refine_sample_nan, 0.125),
            &result,
            &diagnostic),
        taiyin::TAIYIN_EPHEMERIS_ERROR_EVAL_FAILED,
        "synthetic angle refine sample rejects NaN",
        failures);
}

void test_real_body_tangent_classification(int* failures) {
    using namespace taiyin::runtime;

    NativeCalcContext context = make_context(-104.9903, 39.7392, 1609.0);
    EphemerisEvalDiagnostic diagnostic;
    VisibilityAltitudeSearchResult result;

    struct RealTangentCase {
        const char* label;
        int body_id;
        double transit_guess_jd_ut;
        double step_days;
    };
    const RealTangentCase cases[] = {
        {"real Sun altitude tangent", taiyin::TAIYIN_BODY_SUN, 2460409.2927672616, 2.0 / 24.0},
        {"real Moon altitude tangent", taiyin::TAIYIN_BODY_MOON, 2460409.2934041186, 1.0 / 24.0},
    };
    for (const RealTangentCase& test_case : cases) {
        const double max_t = maximize_center_altitude_time(
            &context,
            test_case.body_id,
            test_case.transit_guess_jd_ut,
            failures,
            test_case.label);
        const double target_altitude = sample_center_altitude(
            &context,
            test_case.body_id,
            max_t,
            failures,
            test_case.label);

        VisibilityAltitudeSearchSpec spec;
        spec.body_id = test_case.body_id;
        spec.residual_mode = TAIYIN_VISIBILITY_RESIDUAL_CENTER_ALTITUDE;
        spec.crossing_direction = TAIYIN_VISIBILITY_CROSSING_ANY;
        spec.start_jd_ut = split_jd(max_t - test_case.step_days);
        spec.end_jd_ut = split_jd(max_t + test_case.step_days);
        spec.target_altitude_rad = target_altitude;
        spec.coarse_step_days = test_case.step_days;
        spec.root_tolerance_days = 1.0e-12;
        spec.residual_tolerance_rad = 1.0e-10;
        expect_status(
            visibility_search_altitude_interval_ut(&context, spec, &result, &diagnostic),
            taiyin::TAIYIN_STATUS_OK,
            test_case.label,
            failures);
        expect_equal(result.altitude_state, TAIYIN_VISIBILITY_ALTITUDE_STATE_TANGENT, test_case.label, failures);
        expect_true(std::fabs(scalar_jd(result.jd_ut) - max_t) < 1.0e-5, test_case.label, failures);
        expect_true(std::fabs(result.residual_rad) < 1.0e-8, test_case.label, failures);
    }
}

}  // namespace

int main() {
    int failures = 0;
    test_synthetic_tangent_classification(&failures);
    test_synthetic_angle_rejects_nonfinite_samples(&failures);
    if (initialize_runtime(&failures)) {
        test_denver_solar_rise_oracles(&failures);
        test_polar_classification(&failures);
        test_real_body_tangent_classification(&failures);
    }
    if (failures != 0) {
        std::cerr << failures << " visibility search checks failed\n";
        return 1;
    }
    return 0;
}
