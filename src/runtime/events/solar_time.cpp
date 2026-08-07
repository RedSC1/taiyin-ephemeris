#include "taiyin/runtime/solar_time.h"

#include "taiyin/angle.h"
#include "taiyin/body_id.h"
#include "taiyin/dispatch.h"
#include "taiyin/earth_rotation.h"
#include "taiyin/time.h"

#include <cmath>

namespace taiyin {
namespace runtime {
namespace {

constexpr double kConversionToleranceDays = 1.0e-13;
constexpr int kConversionIterations = 12;

SplitJulianDate invalid_jd() noexcept {
    return SplitJulianDate(0, NAN);
}

bool valid_longitude(double longitude_rad) noexcept {
    return std::isfinite(longitude_rad)
        && longitude_rad >= -TAIYIN_PI
        && longitude_rad <= TAIYIN_PI;
}

Status fail_solar_time(
    EphemerisEvalDiagnostic* diagnostic,
    Status status
) noexcept {
    if (diagnostic) diagnostic->status = status;
    return status;
}

bool resolve_ut_from_tt(
    const NativeCalcContext& context,
    SplitJulianDate jd_tt,
    SplitJulianDate* out_jd_ut
) noexcept {
    if (!out_jd_ut || !split_julian_date_is_finite(jd_tt)) return false;
    SplitJulianDate jd_ut = jd_tt;
    for (int i = 0; i < 8; ++i) {
        const double delta_t = dispatch::eval_delta_t_with_ephemeris_correction(
            context.delta_t_model_id,
            context.ephemeris_family_id,
            jd_ut,
            nullptr,
            nullptr);
        if (!std::isfinite(delta_t)) return false;
        const SplitJulianDate next = jd_tt - delta_t / SECONDS_PER_DAY;
        if (!split_julian_date_is_finite(next)) return false;
        if (std::fabs(next - jd_ut) <= kConversionToleranceDays) {
            *out_jd_ut = next;
            return true;
        }
        jd_ut = next;
    }
    return false;
}

Status calc_equation_of_time_impl(
    const NativeCalcContext* context,
    SplitJulianDate jd_ut,
    SplitJulianDate jd_tt,
    double delta_t_seconds,
    EquationOfTimeResult* out,
    EphemerisEvalDiagnostic* diagnostic
) noexcept {
    if (out) *out = EquationOfTimeResult();
    if (diagnostic) *diagnostic = EphemerisEvalDiagnostic();
    if (!context || !out || !split_julian_date_is_finite(jd_ut) || !split_julian_date_is_finite(jd_tt)
        || !std::isfinite(delta_t_seconds)) {
        return fail_solar_time(diagnostic, TAIYIN_ERROR_INVALID_ARGUMENT);
    }

    NativeCalcContext solar_context = *context;
    Status status = native_context_set_geocentric_observer(
        &solar_context, TAIYIN_BODY_EARTH, TAIYIN_BODY_EARTH);
    if (status != TAIYIN_STATUS_OK) return status;
    double sun[6];
    status = calc_position_ut_delta_t(
        &solar_context,
        TAIYIN_BODY_SUN,
        jd_ut,
        delta_t_seconds,
        TAIYIN_NATIVE_POSITION_EQUATORIAL | TAIYIN_NATIVE_POSITION_RADIANS,
        sun,
        diagnostic);
    if (status != TAIYIN_STATUS_OK) return status;
    if (!std::isfinite(sun[0])) {
        return fail_solar_time(diagnostic, TAIYIN_EPHEMERIS_ERROR_EVAL_FAILED);
    }

    double gast = 0.0;
    if (!gast_model_rad(
            context->model_context.precession_model_id,
            context->model_context.nutation_model_id,
            jd_ut,
            jd_tt,
            &gast)) {
        return fail_solar_time(diagnostic, TAIYIN_ERROR_UNSUPPORTED);
    }

    SplitJulianDate normalized_ut;
    if (!normalize_split_julian_date(jd_ut.day_number, jd_ut.day_fraction, &normalized_ut)) {
        return fail_solar_time(diagnostic, TAIYIN_ERROR_INVALID_ARGUMENT);
    }
    const double mean_solar_fraction = normalized_ut.day_fraction + 0.5
        - std::floor(normalized_ut.day_fraction + 0.5);
    const double mean_solar_angle = normalize_radians(TAIYIN_TWO_PI * mean_solar_fraction);
    const double apparent_solar_angle = normalize_radians(gast - sun[0] + TAIYIN_PI);
    const double equation_angle = normalize_signed_radians(
        apparent_solar_angle - mean_solar_angle);

    out->jd_ut = jd_ut;
    out->jd_tt = jd_tt;
    out->equation_days = equation_angle / TAIYIN_TWO_PI;
    out->equation_seconds = out->equation_days * SECONDS_PER_DAY;
    out->apparent_sun_right_ascension_rad = normalize_radians(sun[0]);
    out->gast_rad = gast;
    return TAIYIN_STATUS_OK;
}

}  // namespace

EquationOfTimeResult::EquationOfTimeResult() noexcept
    : jd_ut(invalid_jd()),
      jd_tt(invalid_jd()),
      equation_days(NAN),
      equation_seconds(NAN),
      apparent_sun_right_ascension_rad(NAN),
      gast_rad(NAN) {}

Status calc_equation_of_time_ut(
    const NativeCalcContext* context,
    SplitJulianDate jd_ut,
    EquationOfTimeResult* out,
    EphemerisEvalDiagnostic* diagnostic
) noexcept {
    if (out) *out = EquationOfTimeResult();
    if (diagnostic) *diagnostic = EphemerisEvalDiagnostic();
    if (!context || !out || !split_julian_date_is_finite(jd_ut)) {
        return fail_solar_time(diagnostic, TAIYIN_ERROR_INVALID_ARGUMENT);
    }
    const double delta_t = dispatch::eval_delta_t_with_ephemeris_correction(
        context->delta_t_model_id,
        context->ephemeris_family_id,
        jd_ut,
        nullptr,
        nullptr);
    if (!std::isfinite(delta_t)) {
        return fail_solar_time(diagnostic, TAIYIN_ERROR_UNSUPPORTED);
    }
    SplitJulianDate jd_tt;
    if (!ut1_to_tt_split_jd(jd_ut, delta_t, &jd_tt)) {
        return fail_solar_time(diagnostic, TAIYIN_ERROR_UNSUPPORTED);
    }
    return calc_equation_of_time_impl(
        context,
        jd_ut,
        jd_tt,
        delta_t,
        out,
        diagnostic);
}

Status calc_equation_of_time_tt(
    const NativeCalcContext* context,
    SplitJulianDate jd_tt,
    EquationOfTimeResult* out,
    EphemerisEvalDiagnostic* diagnostic
) noexcept {
    if (out) *out = EquationOfTimeResult();
    if (diagnostic) *diagnostic = EphemerisEvalDiagnostic();
    if (!context || !out || !split_julian_date_is_finite(jd_tt)) {
        return fail_solar_time(diagnostic, TAIYIN_ERROR_INVALID_ARGUMENT);
    }
    SplitJulianDate jd_ut = invalid_jd();
    if (!resolve_ut_from_tt(*context, jd_tt, &jd_ut)) {
        return fail_solar_time(diagnostic, TAIYIN_ERROR_UNSUPPORTED);
    }
    return calc_equation_of_time_impl(
        context,
        jd_ut,
        jd_tt,
        (jd_tt - jd_ut) * SECONDS_PER_DAY,
        out,
        diagnostic);
}

Status local_mean_to_apparent_solar_time(
    const NativeCalcContext* context,
    SplitJulianDate jd_local_mean,
    double longitude_rad,
    SplitJulianDate* out_jd_local_apparent,
    EphemerisEvalDiagnostic* diagnostic
) noexcept {
    if (out_jd_local_apparent) *out_jd_local_apparent = invalid_jd();
    if (diagnostic) *diagnostic = EphemerisEvalDiagnostic();
    if (!context || !out_jd_local_apparent || !split_julian_date_is_finite(jd_local_mean)
        || !valid_longitude(longitude_rad)) {
        return fail_solar_time(diagnostic, TAIYIN_ERROR_INVALID_ARGUMENT);
    }
    const SplitJulianDate jd_ut = jd_local_mean - longitude_rad / TAIYIN_TWO_PI;
    if (!split_julian_date_is_finite(jd_ut)) {
        return fail_solar_time(diagnostic, TAIYIN_ERROR_INVALID_ARGUMENT);
    }
    EquationOfTimeResult equation;
    const Status status = calc_equation_of_time_ut(
        context, jd_ut, &equation, diagnostic);
    if (status != TAIYIN_STATUS_OK) return status;
    *out_jd_local_apparent = jd_local_mean + equation.equation_days;
    if (equation.equation_days != 0.0
        && *out_jd_local_apparent == jd_local_mean) {
        *out_jd_local_apparent = invalid_jd();
        return fail_solar_time(diagnostic, TAIYIN_ERROR_INVALID_ARGUMENT);
    }
    return split_julian_date_is_finite(*out_jd_local_apparent)
        ? TAIYIN_STATUS_OK
        : TAIYIN_ERROR_INVALID_ARGUMENT;
}

Status local_apparent_to_mean_solar_time(
    const NativeCalcContext* context,
    SplitJulianDate jd_local_apparent,
    double longitude_rad,
    SplitJulianDate* out_jd_local_mean,
    EphemerisEvalDiagnostic* diagnostic
) noexcept {
    if (out_jd_local_mean) *out_jd_local_mean = invalid_jd();
    if (diagnostic) *diagnostic = EphemerisEvalDiagnostic();
    if (!context || !out_jd_local_mean || !split_julian_date_is_finite(jd_local_apparent)
        || !valid_longitude(longitude_rad)) {
        return fail_solar_time(diagnostic, TAIYIN_ERROR_INVALID_ARGUMENT);
    }
    SplitJulianDate jd_local_mean = jd_local_apparent;
    for (int i = 0; i < kConversionIterations; ++i) {
        const SplitJulianDate jd_ut = jd_local_mean - longitude_rad / TAIYIN_TWO_PI;
        if (!split_julian_date_is_finite(jd_ut)) {
            return fail_solar_time(diagnostic, TAIYIN_ERROR_INVALID_ARGUMENT);
        }
        EquationOfTimeResult equation;
        const Status status = calc_equation_of_time_ut(
            context, jd_ut, &equation, diagnostic);
        if (status != TAIYIN_STATUS_OK) return status;
        const SplitJulianDate next = jd_local_apparent - equation.equation_days;
        if (!split_julian_date_is_finite(next)) {
            return fail_solar_time(diagnostic, TAIYIN_ERROR_INVALID_ARGUMENT);
        }
        if (std::fabs(next - jd_local_mean) <= kConversionToleranceDays) {
            *out_jd_local_mean = next;
            return TAIYIN_STATUS_OK;
        }
        jd_local_mean = next;
    }
    return fail_solar_time(diagnostic, TAIYIN_EPHEMERIS_ERROR_EVAL_FAILED);
}

}  // namespace runtime
}  // namespace taiyin
