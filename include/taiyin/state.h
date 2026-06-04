#ifndef TAIYIN_STATE_H
#define TAIYIN_STATE_H

#include "vector3.h"

namespace taiyin {

struct CartesianState {
    Vector3 position_au;
    Vector3 velocity_au_per_day;
    Vector3 acceleration_au_per_day2;
};

CartesianState cartesian_state_add(const CartesianState& a, const CartesianState& b) noexcept;
CartesianState cartesian_state_subtract(const CartesianState& a, const CartesianState& b) noexcept;
CartesianState cartesian_state_scale(const CartesianState& state, double scale) noexcept;
CartesianState cartesian_state_negate(const CartesianState& state) noexcept;

}  // namespace taiyin

#endif  // TAIYIN_STATE_H
