#ifndef TAIYIN_RUNTIME_NATIVE_CONTEXT_CHECKS_H
#define TAIYIN_RUNTIME_NATIVE_CONTEXT_CHECKS_H

#include "taiyin/observer.h"
#include "taiyin/runtime/native_context.h"

#include <cstdint>

namespace taiyin {
namespace runtime {

bool native_observer_location_is_finite(const NativeObserverLocation& location) noexcept;
bool native_context_has_observer_location(const NativeCalcContext& context) noexcept;
bool native_context_observer_degrees(
    const NativeCalcContext& context,
    double* out_longitude_deg,
    double* out_latitude_deg,
    double* out_height_m
) noexcept;
Status native_context_copy_geocentric_with_observer(
    const NativeCalcContext& context,
    NativeCalcContext* out
) noexcept;
bool native_cartesian_state_is_finite(const CartesianState& state) noexcept;

uint32_t native_required_atmosphere_fields_for_refraction_model(int refraction_model_id) noexcept;
bool native_context_has_atmosphere_fields(
    const NativeCalcContext& context,
    uint32_t required_fields
) noexcept;
bool native_context_resolve_refraction_atmosphere(
    const NativeCalcContext& context,
    bool allow_standard_fallback,
    NativeAtmosphere* out
) noexcept;
bool native_refraction_model_from_id(int refraction_model_id, RefractionModel* out) noexcept;

}  // namespace runtime
}  // namespace taiyin

#endif  // TAIYIN_RUNTIME_NATIVE_CONTEXT_CHECKS_H
