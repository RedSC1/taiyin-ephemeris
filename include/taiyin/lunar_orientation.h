#ifndef TAIYIN_LUNAR_ORIENTATION_H
#define TAIYIN_LUNAR_ORIENTATION_H

#include "taiyin/coordinates.h"
#include "taiyin/status.h"
#include "taiyin/time.h"
#include "taiyin/vector3.h"

namespace taiyin {

struct LunarLimbViewCoordinates {
    double libration_longitude_deg;
    double libration_latitude_deg;
    double position_angle_deg;

    LunarLimbViewCoordinates() noexcept;
};

// IAU 2009 lunar Mean Earth/Polar Axis orientation, as encoded by NAIF in
// pck00011.tpc for the IAU_MOON frame. The input epoch is TDB.
bool iau2009_moon_j2000_to_mean_earth_matrix(
    SplitJulianDate jd_tdb,
    Matrix3x3* out
) noexcept;

// Resolve viewing coordinates when both vectors are already expressed in the
// lunar Mean Earth/Polar Axis body-fixed frame.
Status lunar_limb_view_coordinates_body_fixed(
    const Vector3& moon_to_observer_body_fixed,
    const Vector3& apparent_limb_direction_body_fixed,
    LunarLimbViewCoordinates* out
) noexcept;

// Resolve the viewing libration and lunar-limb position angle. Both vectors
// are expressed in J2000/ICRF axes. moon_to_observer_j2000 points from the
// lunar center toward the observer. apparent_limb_direction_j2000 identifies
// the desired direction on the apparent lunar disk and is projected onto the
// observer's sky plane before the position angle is measured north through
// lunar east.
Status lunar_limb_view_coordinates_j2000(
    SplitJulianDate jd_tdb,
    const Vector3& moon_to_observer_j2000,
    const Vector3& apparent_limb_direction_j2000,
    LunarLimbViewCoordinates* out
) noexcept;

}  // namespace taiyin

#endif  // TAIYIN_LUNAR_ORIENTATION_H
