#include "runtime/eclipse/eclipse_time.h"

#include "taiyin/dispatch.h"
#include "taiyin/time.h"

#include <cmath>

namespace taiyin {
namespace runtime {
namespace {

void set_estimated_time_diagnostic(
    EphemerisEvalDiagnostic* diagnostic,
    const NativeCalcContext& context,
    double delta_t_seconds
) noexcept {
    if (!diagnostic) return;
    diagnostic->time_scale_route = static_cast<uint8_t>(TimeScaleRouteEstimatedDeltaT);
    diagnostic->time_scale_fallback_reason = static_cast<uint8_t>(TimeScaleFallbackNone);
    diagnostic->time_scale_flags = TAIYIN_TIME_DIAGNOSTIC_USED_DELTA_T_MODEL;
    diagnostic->tai_minus_utc_seconds = 0.0;
    diagnostic->dut1_seconds = 0.0;
    diagnostic->delta_t_seconds = delta_t_seconds;
    (void)context;
}

}  // namespace

double eclipse_delta_t_seconds_for_ut_estimated(
    const NativeCalcContext& context,
    const SplitJulianDate& jd_ut
) noexcept {
    return dispatch::eval_delta_t_with_ephemeris_correction(
        context.delta_t_model_id,
        context.ephemeris_family_id,
        jd_ut,
        nullptr,
        nullptr);
}

Status eclipse_ut_to_tt(
    const NativeCalcContext& context,
    const SplitJulianDate& jd_ut,
    SplitJulianDate* out_jd_tt,
    double* out_delta_t_seconds,
    EphemerisEvalDiagnostic* diagnostic
) noexcept {
    if (!out_jd_tt || !split_julian_date_is_finite(jd_ut)) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    const double delta_t = eclipse_delta_t_seconds_for_ut_estimated(context, jd_ut);
    if (!ut1_to_tt_split_jd(jd_ut, delta_t, out_jd_tt)) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    if (out_delta_t_seconds) *out_delta_t_seconds = delta_t;
    set_estimated_time_diagnostic(diagnostic, context, delta_t);
    return TAIYIN_STATUS_OK;
}

Status eclipse_tt_to_ut(
    const NativeCalcContext& context,
    const SplitJulianDate& jd_tt,
    SplitJulianDate* out_jd_ut,
    double* out_delta_t_seconds,
    EphemerisEvalDiagnostic* diagnostic
) noexcept {
    if (!out_jd_ut || !split_julian_date_is_finite(jd_tt)) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    double delta_t = dispatch::eval_delta_t_with_ephemeris_correction(
        context.delta_t_model_id,
        context.ephemeris_family_id,
        jd_tt,
        nullptr,
        nullptr);
    SplitJulianDate jd_ut;
    if (!tt_to_ut1_split_jd(jd_tt, delta_t, &jd_ut)) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    for (int i = 0; i < 2; ++i) {
        delta_t = eclipse_delta_t_seconds_for_ut_estimated(context, jd_ut);
        if (!tt_to_ut1_split_jd(jd_tt, delta_t, &jd_ut)) {
            return TAIYIN_ERROR_INVALID_ARGUMENT;
        }
    }
    *out_jd_ut = jd_ut;
    if (out_delta_t_seconds) *out_delta_t_seconds = delta_t;
    set_estimated_time_diagnostic(diagnostic, context, delta_t);
    return TAIYIN_STATUS_OK;
}

}  // namespace runtime
}  // namespace taiyin
