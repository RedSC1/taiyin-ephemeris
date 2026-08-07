#ifndef TAIYIN_RUNTIME_VISIBILITY_ANGLE_SEARCH_INTERNAL_H
#define TAIYIN_RUNTIME_VISIBILITY_ANGLE_SEARCH_INTERNAL_H

#include "runtime/visibility/visibility_search_internal.h"

namespace taiyin {
namespace runtime {

typedef Status (*VisibilityAngleSampleFn)(
    const void* user_data,
    const SplitJulianDate& jd_ut,
    double reference_angle_rad,
    bool has_reference,
    double* out_angle_rad,
    EphemerisEvalDiagnostic* diagnostic);

struct VisibilityAngleTargetSearchSpec {
    SplitJulianDate start_jd_ut;
    SplitJulianDate end_jd_ut;
    double base_target_rad;
    double coarse_step_days;
    double root_tolerance_days;
    double residual_tolerance_rad;
    VisibilityAngleSampleFn sample;
    const void* user_data;

    VisibilityAngleTargetSearchSpec() noexcept;
};

Status visibility_search_continuous_angle_target_ut(
    const VisibilityAngleTargetSearchSpec& spec,
    VisibilityAltitudeSearchResult* out,
    EphemerisEvalDiagnostic* diagnostic
) noexcept;

}  // namespace runtime
}  // namespace taiyin

#endif  // TAIYIN_RUNTIME_VISIBILITY_ANGLE_SEARCH_INTERNAL_H
