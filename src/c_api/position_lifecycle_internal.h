#ifndef TAIYIN_C_API_POSITION_LIFECYCLE_INTERNAL_H
#define TAIYIN_C_API_POSITION_LIFECYCLE_INTERNAL_H

#include <mutex>

namespace taiyin_c_internal {

// Serializes C-owned native-position registration changes with C runtime
// initialization/reset. Calculation paths intentionally do not take this
// setup-time lock.
std::mutex& native_position_lifecycle_mutex() noexcept;

// The caller must hold native_position_lifecycle_mutex().
void clear_native_position_evaluators_locked() noexcept;

}  // namespace taiyin_c_internal

#endif  // TAIYIN_C_API_POSITION_LIFECYCLE_INTERNAL_H
