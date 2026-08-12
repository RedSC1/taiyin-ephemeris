#ifndef TAIYIN_RUNTIME_PLANET_VISIBILITY_INTERNAL_H
#define TAIYIN_RUNTIME_PLANET_VISIBILITY_INTERNAL_H

#include "runtime/visibility/visibility_search_internal.h"

#include "taiyin/runtime/planet_visibility.h"

namespace taiyin {
namespace runtime {

Status planet_visibility_search_rise_set_ut(
    const NativeCalcContext* context,
    int body_id,
    const SplitJulianDate& start_jd_ut,
    const SplitJulianDate& end_jd_ut,
    int event_kind,
    int limb_kind,
    uint64_t planet_visibility_flags,
    VisibilityAltitudeSearchResult* out,
    EphemerisEvalDiagnostic* diagnostic = 0
) noexcept;

Status planet_visibility_search_rise_set_at_horizon_ut(
    const NativeCalcContext* context,
    int body_id,
    const SplitJulianDate& start_jd_ut,
    const SplitJulianDate& end_jd_ut,
    int event_kind,
    int limb_kind,
    double horizon_altitude_rad,
    uint64_t planet_visibility_flags,
    VisibilityAltitudeSearchResult* out,
    EphemerisEvalDiagnostic* diagnostic = 0
) noexcept;

Status planet_visibility_search_transit_ut(
    const NativeCalcContext* context,
    int body_id,
    const SplitJulianDate& start_jd_ut,
    const SplitJulianDate& end_jd_ut,
    int event_kind,
    uint64_t flags,
    VisibilityAltitudeSearchResult* out,
    EphemerisEvalDiagnostic* diagnostic = 0
) noexcept;

}  // namespace runtime
}  // namespace taiyin

#endif  // TAIYIN_RUNTIME_PLANET_VISIBILITY_INTERNAL_H
