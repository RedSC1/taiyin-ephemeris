#ifndef TAIYIN_INTERNAL_BODY_DISC_RADIUS_H
#define TAIYIN_INTERNAL_BODY_DISC_RADIUS_H

#include "taiyin/body_id.h"

#include <limits>

namespace taiyin {
namespace internal {

// MeanPhysical is used for geometric limbs. ApparentDisc preserves the
// conventional radii used by the public phenomena diameter contract.
enum class BodyDiscRadiusConvention {
    MeanPhysical,
    ApparentDisc,
};

inline double body_disc_radius_km(
    int body_id,
    BodyDiscRadiusConvention convention
) noexcept {
    switch (body_id) {
    case TAIYIN_BODY_SUN:
        return convention == BodyDiscRadiusConvention::ApparentDisc ? 696000.0 : 695700.0;
    case TAIYIN_BODY_MOON:
        return convention == BodyDiscRadiusConvention::ApparentDisc ? 1737.5 : 1737.4;
    case TAIYIN_BODY_MERCURY:
        return convention == BodyDiscRadiusConvention::ApparentDisc ? 2439.4 : 2439.7;
    case TAIYIN_BODY_VENUS:
        return 6051.8;
    case TAIYIN_BODY_MARS:
        return 3389.5;
    case TAIYIN_BODY_JUPITER:
        return 69911.0;
    case TAIYIN_BODY_SATURN:
        return 58232.0;
    case TAIYIN_BODY_URANUS:
        return 25362.0;
    case TAIYIN_BODY_NEPTUNE:
        return 24622.0;
    case TAIYIN_BODY_PLUTO:
        return 1188.3;
    default:
        return std::numeric_limits<double>::quiet_NaN();
    }
}

}  // namespace internal
}  // namespace taiyin

#endif  // TAIYIN_INTERNAL_BODY_DISC_RADIUS_H
