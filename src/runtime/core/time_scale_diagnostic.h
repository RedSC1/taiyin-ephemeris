#ifndef TAIYIN_RUNTIME_CORE_TIME_SCALE_DIAGNOSTIC_H
#define TAIYIN_RUNTIME_CORE_TIME_SCALE_DIAGNOSTIC_H

#include "taiyin/runtime/ephemeris_engine.h"
#include "taiyin/time.h"

namespace taiyin {
namespace runtime {

inline Status precise_time_failure_status(
    const TimeScaleDiagnostic& diagnostic
) noexcept {
    switch (diagnostic.fallback_reason) {
    case TimeScaleFallbackNullEopTable:
    case TimeScaleFallbackEopOutOfRange:
        return TAIYIN_TIME_ERROR_EOP_OUT_OF_RANGE;
    case TimeScaleFallbackLeapSecondUnavailable:
        return TAIYIN_TIME_ERROR_LEAP_SECOND_UNAVAILABLE;
    case TimeScaleFallbackNone:
    default:
        return TAIYIN_ERROR_INTERNAL;
    }
}

inline void copy_time_scale_diagnostic(
    EphemerisEvalDiagnostic* diagnostic,
    const TimeScaleDiagnostic& source
) noexcept {
    if (!diagnostic) return;
    diagnostic->time_scale_route = static_cast<uint8_t>(source.route);
    diagnostic->time_scale_fallback_reason =
        static_cast<uint8_t>(source.fallback_reason);
    diagnostic->time_scale_flags = 0;
    if (source.used_leap_seconds) {
        diagnostic->time_scale_flags |=
            TAIYIN_TIME_DIAGNOSTIC_USED_LEAP_SECONDS;
    }
    if (source.used_eop) {
        diagnostic->time_scale_flags |= TAIYIN_TIME_DIAGNOSTIC_USED_EOP;
    }
    if (source.used_delta_t_model) {
        diagnostic->time_scale_flags |=
            TAIYIN_TIME_DIAGNOSTIC_USED_DELTA_T_MODEL;
    }
    diagnostic->tai_minus_utc_seconds = source.tai_minus_utc_seconds;
    diagnostic->dut1_seconds = source.dut1_seconds;
    diagnostic->delta_t_seconds = source.delta_t_seconds;
}

}  // namespace runtime
}  // namespace taiyin

#endif  // TAIYIN_RUNTIME_CORE_TIME_SCALE_DIAGNOSTIC_H
