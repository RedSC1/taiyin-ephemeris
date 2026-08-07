#include "runtime/eclipse/eclipse_time.h"

#include "taiyin/dispatch.h"
#include "taiyin/runtime/runtime.h"
#include "taiyin/time.h"

#include <cmath>

namespace taiyin {
namespace runtime {
namespace {

void set_time_diagnostic(
    EphemerisEvalDiagnostic* diagnostic,
    Status status,
    const SplitJulianDate& jd
) noexcept {
    if (!diagnostic) return;
    diagnostic->status = status;
    diagnostic->jd_tdb = jd;
}

void copy_time_diagnostic(EphemerisEvalDiagnostic* diagnostic, const TimeScaleDiagnostic& source) noexcept {
    if (!diagnostic) return;
    diagnostic->time_scale_route = static_cast<uint8_t>(source.route);
    diagnostic->time_scale_fallback_reason = static_cast<uint8_t>(source.fallback_reason);
    diagnostic->time_scale_flags = 0;
    if (source.used_leap_seconds) diagnostic->time_scale_flags |= TAIYIN_TIME_DIAGNOSTIC_USED_LEAP_SECONDS;
    if (source.used_eop) diagnostic->time_scale_flags |= TAIYIN_TIME_DIAGNOSTIC_USED_EOP;
    if (source.used_delta_t_model) diagnostic->time_scale_flags |= TAIYIN_TIME_DIAGNOSTIC_USED_DELTA_T_MODEL;
    diagnostic->tai_minus_utc_seconds = source.tai_minus_utc_seconds;
    diagnostic->dut1_seconds = source.dut1_seconds;
    diagnostic->delta_t_seconds = source.delta_t_seconds;
}

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

Status status_from_time_diagnostic(const TimeScaleDiagnostic& diagnostic) noexcept {
    switch (diagnostic.fallback_reason) {
    case TimeScaleFallbackNullEopTable:
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    case TimeScaleFallbackEopOutOfRange:
        return TAIYIN_TIME_ERROR_EOP_OUT_OF_RANGE;
    case TimeScaleFallbackLeapSecondUnavailable:
        return TAIYIN_TIME_ERROR_LEAP_SECOND_UNAVAILABLE;
    case TimeScaleFallbackNone:
    default:
        return TAIYIN_ERROR_UNSUPPORTED;
    }
}

TimeScaleOptions eclipse_time_options(const NativeCalcContext& context) noexcept {
    TimeScaleOptions options;
    options.policy = context.time_scale_policy;
    options.tdb_model_id = context.model_context.tdb_model_id;
    options.delta_t_model_id = context.delta_t_model_id;
    options.ephemeris_family_id = context.ephemeris_family_id;
    options.leap_second_table = builtin_leap_second_table();
    return options;
}

Status make_eclipse_time_scales_from_jd_utc(
    const NativeCalcContext& context,
    const SplitJulianDate& jd_utc,
    PreciseTimeScales* out,
    EphemerisEvalDiagnostic* diagnostic
) noexcept {
    TimeScaleDiagnostic time_diagnostic;
    CalendarDateTime datetime_utc;
    if (!reverse_julian_day_split(jd_utc, &datetime_utc)) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    const TimeScaleOptions options = eclipse_time_options(context);
    if (!make_time_scales_from_utc(
            datetime_utc,
            global_earth_orientation_table(),
            &options,
            out,
            &time_diagnostic)) {
        copy_time_diagnostic(diagnostic, time_diagnostic);
        const Status status = status_from_time_diagnostic(time_diagnostic);
        set_time_diagnostic(diagnostic, status, jd_utc);
        return status;
    }
    copy_time_diagnostic(diagnostic, time_diagnostic);
    return TAIYIN_STATUS_OK;
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
    if (context.time_scale_policy == TimeScaleEstimated) {
        const double delta_t = eclipse_delta_t_seconds_for_ut_estimated(context, jd_ut);
        if (!ut1_to_tt_split_jd(jd_ut, delta_t, out_jd_tt)) {
            return TAIYIN_ERROR_INVALID_ARGUMENT;
        }
        if (out_delta_t_seconds) *out_delta_t_seconds = delta_t;
        set_estimated_time_diagnostic(diagnostic, context, delta_t);
        return TAIYIN_STATUS_OK;
    }

    PreciseTimeScales scales;
    const Status status = make_eclipse_time_scales_from_jd_utc(context, jd_ut, &scales, diagnostic);
    if (status != TAIYIN_STATUS_OK) return status;
    *out_jd_tt = scales.jd_tt;
    if (out_delta_t_seconds) *out_delta_t_seconds = scales.delta_t_seconds;
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
    if (context.time_scale_policy == TimeScaleEstimated) {
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

    SplitJulianDate jd_utc;
    if (!tt_to_ut1_split_jd(
            jd_tt, estimated_delta_t_seconds_from_tt_jd(jd_tt), &jd_utc)) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    PreciseTimeScales scales;
    Status status = TAIYIN_STATUS_OK;
    for (int i = 0; i < 3; ++i) {
        status = make_eclipse_time_scales_from_jd_utc(context, jd_utc, &scales, diagnostic);
        if (status != TAIYIN_STATUS_OK) return status;
        jd_utc = jd_utc + (jd_tt - scales.jd_tt);
    }
    status = make_eclipse_time_scales_from_jd_utc(context, jd_utc, &scales, diagnostic);
    if (status != TAIYIN_STATUS_OK) return status;
    *out_jd_ut = scales.jd_utc;
    if (out_delta_t_seconds) *out_delta_t_seconds = scales.delta_t_seconds;
    return TAIYIN_STATUS_OK;
}

}  // namespace runtime
}  // namespace taiyin
