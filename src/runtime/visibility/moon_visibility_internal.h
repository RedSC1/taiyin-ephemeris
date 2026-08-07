#ifndef TAIYIN_RUNTIME_MOON_VISIBILITY_INTERNAL_H
#define TAIYIN_RUNTIME_MOON_VISIBILITY_INTERNAL_H

#include "taiyin/runtime/moon_visibility.h"

#include "runtime/visibility/visibility_search_internal.h"

namespace taiyin {
namespace runtime {

Status moon_visibility_search_rise_set_ut(
    const NativeCalcContext* context,
    const SplitJulianDate& start_jd_ut,
    const SplitJulianDate& end_jd_ut,
    int event_kind,
    int limb_kind,
    uint64_t moon_visibility_flags,
    VisibilityAltitudeSearchResult* out,
    EphemerisEvalDiagnostic* diagnostic
) noexcept;

Status moon_visibility_search_rise_set_at_horizon_ut(
    const NativeCalcContext* context,
    const SplitJulianDate& start_jd_ut,
    const SplitJulianDate& end_jd_ut,
    int event_kind,
    int limb_kind,
    double horizon_altitude_rad,
    uint64_t moon_visibility_flags,
    VisibilityAltitudeSearchResult* out,
    EphemerisEvalDiagnostic* diagnostic
) noexcept;

Status moon_visibility_search_transit_ut(
    const NativeCalcContext* context,
    const SplitJulianDate& start_jd_ut,
    const SplitJulianDate& end_jd_ut,
    int event_kind,
    VisibilityAltitudeSearchResult* out,
    EphemerisEvalDiagnostic* diagnostic
) noexcept;

}  // namespace runtime
}  // namespace taiyin

#endif  // TAIYIN_RUNTIME_MOON_VISIBILITY_INTERNAL_H
