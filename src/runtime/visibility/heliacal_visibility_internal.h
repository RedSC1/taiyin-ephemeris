#ifndef TAIYIN_RUNTIME_HELIACAL_VISIBILITY_INTERNAL_H
#define TAIYIN_RUNTIME_HELIACAL_VISIBILITY_INTERNAL_H

#include "taiyin/runtime/heliacal_visibility.h"

namespace taiyin {
namespace runtime {

bool valid_heliacal_visibility_flags(uint64_t flags) noexcept;
bool valid_heliacal_body_target(int body_id) noexcept;

bool heliacal_visibility_eval_belokrylov_2011(
    const HeliacalVisibilityModelInput* input,
    HeliacalVisibilityResult* out
) noexcept;

bool heliacal_visibility_eval_schaefer_1993(
    const HeliacalVisibilityModelInput* input,
    HeliacalVisibilityResult* out
) noexcept;

}  // namespace runtime
}  // namespace taiyin

#endif  // TAIYIN_RUNTIME_HELIACAL_VISIBILITY_INTERNAL_H
