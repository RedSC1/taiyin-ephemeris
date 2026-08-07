#ifndef TAIYIN_RUNTIME_LUNAR_LIMB_H
#define TAIYIN_RUNTIME_LUNAR_LIMB_H

#include "taiyin/lunar_orientation.h"
#include "taiyin/runtime/native_context.h"
#include "taiyin/status.h"
#include "taiyin/vector3.h"

namespace taiyin {
struct Tll1LunarLimbModel;
namespace runtime {

struct PreparedLunarLimbQuery {
    const Tll1LunarLimbModel* model;
    Matrix3x3 apparent_to_lunar_body;
    bool valid;

    PreparedLunarLimbQuery() noexcept;
};

Status apparent_limb_direction_toward_target(
    const Vector3& observer_to_moon,
    const Vector3& observer_to_target,
    bool away_from_target,
    Vector3* out
) noexcept;

// Evaluate the physical lunar silhouette radius selected by the context's
// TLL1 model. The model is direction-dependent and replaces, rather than
// augments, the scalar eclipse Moon-radius model.
Status eval_lunar_limb_radius_m(
    const NativeCalcContext* context,
    SplitJulianDate jd_tdb,
    const Vector3& moon_to_observer_j2000,
    const Vector3& apparent_limb_direction_j2000,
    double* out_radius_m,
    LunarLimbViewCoordinates* out_view = nullptr
) noexcept;

Status prepare_lunar_limb_query(
    const NativeCalcContext* context,
    SplitJulianDate jd_tt,
    bool true_position,
    int output_frame_id,
    PreparedLunarLimbQuery* out
) noexcept;

Status eval_prepared_lunar_limb_radius_m(
    const PreparedLunarLimbQuery* query,
    const Vector3& moon_to_observer,
    const Vector3& apparent_limb_direction,
    double* out_radius_m,
    LunarLimbViewCoordinates* out_view = nullptr
) noexcept;

// Convert vectors from an apparent output frame back to J2000 and evaluate the
// globally loaded TLL1 model. This keeps eclipse solvers independent of the lunar
// orientation and interpolation implementation.
Status eval_lunar_limb_radius_from_apparent_frame_m(
    const NativeCalcContext* context,
    SplitJulianDate jd_tt,
    bool true_position,
    int output_frame_id,
    const Vector3& moon_to_observer,
    const Vector3& apparent_limb_direction,
    double* out_radius_m,
    LunarLimbViewCoordinates* out_view = nullptr
) noexcept;

}  // namespace runtime
}  // namespace taiyin

#endif  // TAIYIN_RUNTIME_LUNAR_LIMB_H
