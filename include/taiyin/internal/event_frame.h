#ifndef TAIYIN_INTERNAL_EVENT_FRAME_H
#define TAIYIN_INTERNAL_EVENT_FRAME_H

#include "taiyin/apparent_position.h"
#include "taiyin/coordinates.h"
#include "taiyin/dispatch.h"

namespace taiyin {
namespace internal {

bool resolve_event_frame_models(
    int precession_model_id,
    int nutation_model_id,
    dispatch::PrecessionModelEntry* precession,
    dispatch::NutationModelEntry* nutation
) noexcept;

bool eval_event_output_frame_matrix(
    const SplitJulianDate& jd_tt,
    int output_frame_id,
    const dispatch::PrecessionModelEntry& precession,
    const dispatch::NutationModelEntry& nutation,
    Matrix3x3* out_precession,
    NutationAngles* out_nutation,
    Matrix3x3* out_output_matrix
) noexcept;

}  // namespace internal
}  // namespace taiyin

#endif  // TAIYIN_INTERNAL_EVENT_FRAME_H
