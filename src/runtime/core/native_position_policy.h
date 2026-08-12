#ifndef TAIYIN_RUNTIME_CORE_NATIVE_POSITION_POLICY_H
#define TAIYIN_RUNTIME_CORE_NATIVE_POSITION_POLICY_H

#include "taiyin/body_id.h"
#include "taiyin/runtime/native_position.h"
#include "taiyin/status.h"

namespace taiyin {
namespace runtime {

inline bool native_position_status_allows_barycenter_approx(
    Status status
) noexcept {
    return status == TAIYIN_EPHEMERIS_ERROR_NO_ROUTE
        || status == TAIYIN_EPHEMERIS_ERROR_COVERAGE_GAP
        || status == TAIYIN_EPHEMERIS_ERROR_COMPOSITE_MISSING_COMPONENT
        || status == TAIYIN_EPHEMERIS_ERROR_COMPOSITE_COVERAGE_GAP;
}

inline int native_position_barycenter_approx_target(int target_id) noexcept {
    switch (target_id) {
    case TAIYIN_BODY_MARS:
        return TAIYIN_BODY_MARS_BARYCENTER;
    case TAIYIN_BODY_JUPITER:
        return TAIYIN_BODY_JUPITER_BARYCENTER;
    case TAIYIN_BODY_SATURN:
        return TAIYIN_BODY_SATURN_BARYCENTER;
    case TAIYIN_BODY_URANUS:
        return TAIYIN_BODY_URANUS_BARYCENTER;
    case TAIYIN_BODY_NEPTUNE:
        return TAIYIN_BODY_NEPTUNE_BARYCENTER;
    case TAIYIN_BODY_PLUTO:
        return TAIYIN_BODY_PLUTO_BARYCENTER;
    default:
        return 0;
    }
}

inline bool native_position_should_try_barycenter_approx(
    int target_id,
    uint32_t position_flags,
    Status status
) noexcept {
    return (position_flags & TAIYIN_NATIVE_POSITION_ALLOW_BARYCENTER_APPROX) != 0u
        && native_position_status_allows_barycenter_approx(status)
        && native_position_barycenter_approx_target(target_id) != 0;
}

}  // namespace runtime
}  // namespace taiyin

#endif  // TAIYIN_RUNTIME_CORE_NATIVE_POSITION_POLICY_H
