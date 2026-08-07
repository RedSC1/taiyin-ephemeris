#include "runtime/visibility/visibility_sampling_internal.h"

#include "taiyin/angle.h"
#include "taiyin/dispatch.h"
#include "taiyin/earth_rotation.h"
#include "runtime/core/native_context_checks.h"
#include "taiyin/runtime/observed_position.h"
#include "taiyin/time.h"

#include <cmath>
#include <limits>

namespace taiyin {
namespace runtime {
namespace {

constexpr int kRefractionMaxIterations = 10;
constexpr double kRefractionTolerance = 3.0e-5;

double nan_value() noexcept {
    return std::numeric_limits<double>::quiet_NaN();
}

void clear_center_outputs(
    double* out_altitude_rad,
    double* out_azimuth_rad,
    double* out_hour_angle_rad,
    double* out_distance_au,
    double* out_apparent_ra_rad,
    double* out_apparent_dec_rad
) noexcept {
    if (out_altitude_rad) *out_altitude_rad = nan_value();
    if (out_azimuth_rad) *out_azimuth_rad = nan_value();
    if (out_hour_angle_rad) *out_hour_angle_rad = nan_value();
    if (out_distance_au) *out_distance_au = nan_value();
    if (out_apparent_ra_rad) *out_apparent_ra_rad = nan_value();
    if (out_apparent_dec_rad) *out_apparent_dec_rad = nan_value();
}

Status tt_from_ut(
    const NativeCalcContext* context,
    const SplitJulianDate& jd_ut,
    SplitJulianDate* out_jd_tt
) noexcept {
    if (!context || !out_jd_tt || !split_julian_date_is_finite(jd_ut)) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    const double delta_t = dispatch::eval_delta_t_with_ephemeris_correction(
        context->delta_t_model_id,
        context->ephemeris_family_id,
        jd_ut,
        0,
        0);
    if (!ut1_to_tt_split_jd(jd_ut, delta_t, out_jd_tt)) {
        return TAIYIN_ERROR_UNSUPPORTED;
    }
    return TAIYIN_STATUS_OK;
}

double hour_angle_rad(double sidereal_rad, double longitude_rad, double ra_rad) noexcept {
    return normalize_signed_radians(sidereal_rad + longitude_rad - ra_rad);
}

}  // namespace

Status visibility_apply_refraction_from_context(
    const NativeCalcContext* context,
    double true_altitude_rad,
    uint64_t observed_flags,
    double* out_apparent_altitude_rad
) noexcept {
    if (out_apparent_altitude_rad) *out_apparent_altitude_rad = nan_value();
    if (!context || !out_apparent_altitude_rad || !std::isfinite(true_altitude_rad)) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    const bool allow_standard_fallback = (context->atmosphere_policy_flags
        & TAIYIN_NATIVE_ATMOSPHERE_ALLOW_STANDARD_FALLBACK) != 0u
        && (observed_flags & TAIYIN_OBSERVED_STRICT_METEOROLOGY) == 0u;
    NativeAtmosphere atmosphere;
    if (!dispatch::has_refraction_model(context->refraction_model_id)
        || !native_context_resolve_refraction_atmosphere(
            *context, allow_standard_fallback, &atmosphere)) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    dispatch::RefractionDispatchData data;
    data.altitude_rad = true_altitude_rad;
    data.pressure_mbar = atmosphere.pressure_mbar;
    data.temperature_c = atmosphere.temperature_celsius;
    data.relative_humidity = atmosphere.relative_humidity;
    data.wavelength_micrometer = atmosphere.wavelength_micrometer;
    data.max_iterations = kRefractionMaxIterations;
    data.tolerance = kRefractionTolerance;
    const double refraction_rad = dispatch::eval_refraction(context->refraction_model_id, &data);
    const double apparent_altitude = true_altitude_rad + refraction_rad;
    if (!std::isfinite(refraction_rad) || !std::isfinite(apparent_altitude)) {
        return TAIYIN_EPHEMERIS_ERROR_EVAL_FAILED;
    }
    *out_apparent_altitude_rad = apparent_altitude;
    return TAIYIN_STATUS_OK;
}

Status visibility_sample_body_center_horizontal_ut(
    const NativeCalcContext* context,
    int body_id,
    const SplitJulianDate& jd_ut,
    uint64_t observed_flags,
    double* out_altitude_rad,
    double* out_azimuth_rad,
    double* out_hour_angle_rad,
    double* out_distance_au,
    double* out_apparent_ra_rad,
    double* out_apparent_dec_rad,
    EphemerisEvalDiagnostic* diagnostic
) noexcept {
    clear_center_outputs(
        out_altitude_rad,
        out_azimuth_rad,
        out_hour_angle_rad,
        out_distance_au,
        out_apparent_ra_rad,
        out_apparent_dec_rad);
    if (!context
        || !out_altitude_rad
        || !out_azimuth_rad
        || !out_hour_angle_rad
        || !out_distance_au
        || !split_julian_date_is_finite(jd_ut)) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }

    ObservedPosition observed;
    const uint64_t flags = observed_flags | TAIYIN_OBSERVED_TOPOCENTRIC | TAIYIN_OBSERVED_HORIZONTAL;
    Status st = calc_observed_ut(context, jd_ut, &body_id, 1, flags, &observed, diagnostic);
    if (st != TAIYIN_STATUS_OK) return st;
    if (observed.status != TAIYIN_STATUS_OK) return observed.status;

    SplitJulianDate jd_tt;
    st = tt_from_ut(context, jd_ut, &jd_tt);
    if (st != TAIYIN_STATUS_OK) return st;
    double sidereal = 0.0;
    if (!gast_model_rad(
            context->model_context.precession_model_id,
            context->model_context.nutation_model_id,
            jd_ut,
            jd_tt,
            &sidereal)) {
        return TAIYIN_ERROR_UNSUPPORTED;
    }

    const bool use_refraction = (flags & TAIYIN_OBSERVED_REFRACTION) != 0u;
    const HorizontalCoordinates& horizontal = use_refraction
        ? observed.refracted_horizontal
        : observed.horizontal;
    const double ra = observed.apparent.longitude_rad;
    const double dec = observed.apparent.latitude_rad;
    const double hour_angle = hour_angle_rad(sidereal, context->observer_location.longitude_rad, ra);

    if (!std::isfinite(horizontal.altitude_rad)
        || !std::isfinite(horizontal.azimuth_rad)
        || !std::isfinite(horizontal.distance_au)
        || !std::isfinite(hour_angle)
        || !std::isfinite(ra)
        || !std::isfinite(dec)) {
        return TAIYIN_EPHEMERIS_ERROR_EVAL_FAILED;
    }

    *out_altitude_rad = horizontal.altitude_rad;
    *out_azimuth_rad = horizontal.azimuth_rad;
    *out_hour_angle_rad = hour_angle;
    *out_distance_au = horizontal.distance_au;
    if (out_apparent_ra_rad) *out_apparent_ra_rad = ra;
    if (out_apparent_dec_rad) *out_apparent_dec_rad = dec;
    return TAIYIN_STATUS_OK;
}

Status visibility_sample_body_center_residual_ut(
    const NativeCalcContext* context,
    int body_id,
    const SplitJulianDate& jd_ut,
    double target_altitude_rad,
    uint64_t observed_flags,
    double* out_residual_rad,
    double* out_altitude_rad,
    double* out_azimuth_rad,
    double* out_hour_angle_rad,
    double* out_distance_au,
    EphemerisEvalDiagnostic* diagnostic
) noexcept {
    if (out_residual_rad) *out_residual_rad = nan_value();
    if (out_altitude_rad) *out_altitude_rad = nan_value();
    if (out_azimuth_rad) *out_azimuth_rad = nan_value();
    if (out_hour_angle_rad) *out_hour_angle_rad = nan_value();
    if (out_distance_au) *out_distance_au = nan_value();
    if (!out_residual_rad || !std::isfinite(target_altitude_rad)) return TAIYIN_ERROR_INVALID_ARGUMENT;
    double altitude = 0.0;
    double azimuth = 0.0;
    double hour_angle = 0.0;
    double distance = 0.0;
    const Status st = visibility_sample_body_center_horizontal_ut(
        context,
        body_id,
        jd_ut,
        observed_flags,
        &altitude,
        &azimuth,
        &hour_angle,
        &distance,
        0,
        0,
        diagnostic);
    if (st != TAIYIN_STATUS_OK) return st;
    const double residual = altitude - target_altitude_rad;
    if (!std::isfinite(residual)) return TAIYIN_EPHEMERIS_ERROR_EVAL_FAILED;
    *out_residual_rad = residual;
    if (out_altitude_rad) *out_altitude_rad = altitude;
    if (out_azimuth_rad) *out_azimuth_rad = azimuth;
    if (out_hour_angle_rad) *out_hour_angle_rad = hour_angle;
    if (out_distance_au) *out_distance_au = distance;
    return TAIYIN_STATUS_OK;
}

}  // namespace runtime
}  // namespace taiyin
