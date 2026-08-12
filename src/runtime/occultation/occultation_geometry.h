#ifndef TAIYIN_RUNTIME_OCCULTATION_GEOMETRY_H
#define TAIYIN_RUNTIME_OCCULTATION_GEOMETRY_H

#include "taiyin/vector3.h"

namespace taiyin {
namespace runtime {
namespace occultation_geometry {

struct GeodeticPoint {
    bool valid;
    double longitude_rad;
    double latitude_rad;

    GeodeticPoint() noexcept;
};

struct AxisState {
    Vector3 moon_llr;
    Vector3 target_llr;
    double gast_rad;

    AxisState() noexcept;
};

struct AxisProjection {
    GeodeticPoint center_intersection;
    GeodeticPoint nearest_observer;
    int center_line_hits_earth;

    AxisProjection() noexcept;
};

void initialize_axis(
    const Vector3& moon_llr,
    const Vector3& target_llr,
    double gast_rad,
    AxisState* out
) noexcept;

GeodeticPoint intersect_axis_with_earth(
    const Vector3& moon_llr,
    const Vector3& target_llr,
    double gast_rad
) noexcept;

GeodeticPoint intersect_axis_with_earth(const AxisState& state) noexcept;

GeodeticPoint find_nearest_observer(const AxisState& state) noexcept;

AxisProjection project_axis_to_earth(const AxisState& state) noexcept;

double surface_distance_km(
    double longitude_a_rad,
    double latitude_a_rad,
    double longitude_b_rad,
    double latitude_b_rad
) noexcept;

}  // namespace occultation_geometry
}  // namespace runtime
}  // namespace taiyin

#endif  // TAIYIN_RUNTIME_OCCULTATION_GEOMETRY_H
