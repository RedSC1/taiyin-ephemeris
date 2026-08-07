#ifndef TAIYIN_RUNTIME_VISIBILITY_SEARCH_INTERNAL_H
#define TAIYIN_RUNTIME_VISIBILITY_SEARCH_INTERNAL_H

#include "taiyin/runtime/native_context.h"
#include "taiyin/status.h"

#include <stdint.h>

namespace taiyin {
namespace runtime {

typedef Status (*VisibilityAltitudeResidualSampler)(
    const void* user_data,
    const SplitJulianDate& jd_ut,
    double* out_residual_rad,
    EphemerisEvalDiagnostic* diagnostic
);

const int TAIYIN_VISIBILITY_RESIDUAL_CENTER_ALTITUDE = 1;
const int TAIYIN_VISIBILITY_RESIDUAL_TRUE_UPPER_LIMB = 2;
const int TAIYIN_VISIBILITY_RESIDUAL_APPARENT_UPPER_LIMB = 3;
const int TAIYIN_VISIBILITY_RESIDUAL_TRUE_LOWER_LIMB = 4;
const int TAIYIN_VISIBILITY_RESIDUAL_APPARENT_LOWER_LIMB = 5;

const int TAIYIN_VISIBILITY_CROSSING_ANY = 0;
const int TAIYIN_VISIBILITY_CROSSING_RISING = 1;
const int TAIYIN_VISIBILITY_CROSSING_SETTING = 2;

struct VisibilityAltitudeSearchSpec {
    int body_id;
    int residual_mode;
    int crossing_direction;
    SplitJulianDate start_jd_ut;
    SplitJulianDate end_jd_ut;
    double target_altitude_rad;
    double physical_radius_km;
    double angular_radius_distance_au;
    double coarse_step_days;
    double root_tolerance_days;
    double residual_tolerance_rad;
    uint64_t observed_flags;
    VisibilityAltitudeResidualSampler residual_sampler;
    const void* residual_sampler_data;

    VisibilityAltitudeSearchSpec() noexcept;
};

struct VisibilityAltitudeSearchResult {
    int altitude_state;
    int crossing_direction;
    SplitJulianDate jd_ut;
    double residual_rad;
    double min_residual_rad;
    double max_residual_rad;
    SplitJulianDate min_residual_jd_ut;
    SplitJulianDate max_residual_jd_ut;
    int sample_count;
    int refine_count;

    VisibilityAltitudeSearchResult() noexcept;
};

Status visibility_search_altitude_interval_ut(
    const NativeCalcContext* context,
    const VisibilityAltitudeSearchSpec& spec,
    VisibilityAltitudeSearchResult* out,
    EphemerisEvalDiagnostic* diagnostic
) noexcept;

}  // namespace runtime
}  // namespace taiyin

#endif  // TAIYIN_RUNTIME_VISIBILITY_SEARCH_INTERNAL_H
