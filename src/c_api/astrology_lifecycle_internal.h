#ifndef TAIYIN_C_API_ASTROLOGY_LIFECYCLE_INTERNAL_H
#define TAIYIN_C_API_ASTROLOGY_LIFECYCLE_INTERNAL_H

#include <mutex>

namespace taiyin_c_internal {

// Serializes C-owned astrology-model registration changes with C runtime
// initialization/reset. Calculation paths intentionally do not take this
// setup-time lock. The complete lock order is this mutex -> a C bridge-map
// mutex -> the matching native model-registry mutex. Native C++ callers only
// acquire the final mutex.
std::mutex& astrology_model_lifecycle_mutex() noexcept;

// The caller must hold astrology_model_lifecycle_mutex(). These functions
// clear only callbacks registered through the corresponding C API. House cleanup
// returns false when a native model still references a C-owned fallback bridge.
void clear_c_ayanamsha_models_locked() noexcept;
bool clear_c_house_system_models_locked() noexcept;

}  // namespace taiyin_c_internal

#endif  // TAIYIN_C_API_ASTROLOGY_LIFECYCLE_INTERNAL_H
