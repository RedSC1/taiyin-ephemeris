#ifndef TAIYIN_RUNTIME_VISIBILITY_SAMPLING_INTERNAL_H
#define TAIYIN_RUNTIME_VISIBILITY_SAMPLING_INTERNAL_H

#include "taiyin/runtime/native_context.h"
#include "taiyin/status.h"

#include <stdint.h>

namespace taiyin {
namespace runtime {

Status visibility_apply_refraction_from_context(
    const NativeCalcContext* context,
    double true_altitude_rad,
    uint64_t observed_flags,
    double* out_apparent_altitude_rad
) noexcept;

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
) noexcept;

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
) noexcept;

}  // namespace runtime
}  // namespace taiyin

#endif  // TAIYIN_RUNTIME_VISIBILITY_SAMPLING_INTERNAL_H
