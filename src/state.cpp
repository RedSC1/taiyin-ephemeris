#include "taiyin/state.h"

namespace taiyin {

CartesianState cartesian_state_add(const CartesianState& a, const CartesianState& b) noexcept {
    CartesianState out;
    out.position_au = vector3_add(a.position_au, b.position_au);
    out.velocity_au_per_day = vector3_add(a.velocity_au_per_day, b.velocity_au_per_day);
    out.acceleration_au_per_day2 = vector3_add(a.acceleration_au_per_day2, b.acceleration_au_per_day2);
    return out;
}

CartesianState cartesian_state_subtract(const CartesianState& a, const CartesianState& b) noexcept {
    CartesianState out;
    out.position_au = vector3_subtract(a.position_au, b.position_au);
    out.velocity_au_per_day = vector3_subtract(a.velocity_au_per_day, b.velocity_au_per_day);
    out.acceleration_au_per_day2 = vector3_subtract(a.acceleration_au_per_day2, b.acceleration_au_per_day2);
    return out;
}

CartesianState cartesian_state_scale(const CartesianState& state, double scale) noexcept {
    CartesianState out;
    out.position_au = vector3_scale(state.position_au, scale);
    out.velocity_au_per_day = vector3_scale(state.velocity_au_per_day, scale);
    out.acceleration_au_per_day2 = vector3_scale(state.acceleration_au_per_day2, scale);
    return out;
}

CartesianState cartesian_state_negate(const CartesianState& state) noexcept {
    return cartesian_state_scale(state, -1.0);
}

}  // namespace taiyin
