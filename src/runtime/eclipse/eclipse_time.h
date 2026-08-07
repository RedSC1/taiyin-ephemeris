#ifndef TAIYIN_RUNTIME_ECLIPSE_TIME_H
#define TAIYIN_RUNTIME_ECLIPSE_TIME_H

#include "taiyin/runtime/native_context.h"
#include "taiyin/runtime/ephemeris_engine.h"
#include "taiyin/status.h"

namespace taiyin {
namespace runtime {

Status eclipse_ut_to_tt(
    const NativeCalcContext& context,
    const SplitJulianDate& jd_ut,
    SplitJulianDate* out_jd_tt,
    double* out_delta_t_seconds,
    EphemerisEvalDiagnostic* diagnostic
) noexcept;

Status eclipse_tt_to_ut(
    const NativeCalcContext& context,
    const SplitJulianDate& jd_tt,
    SplitJulianDate* out_jd_ut,
    double* out_delta_t_seconds,
    EphemerisEvalDiagnostic* diagnostic
) noexcept;

double eclipse_delta_t_seconds_for_ut_estimated(
    const NativeCalcContext& context,
    const SplitJulianDate& jd_ut
) noexcept;

}  // namespace runtime
}  // namespace taiyin

#endif  // TAIYIN_RUNTIME_ECLIPSE_TIME_H
