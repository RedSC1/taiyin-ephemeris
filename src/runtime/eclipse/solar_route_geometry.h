#ifndef TAIYIN_RUNTIME_SOLAR_ROUTE_GEOMETRY_H
#define TAIYIN_RUNTIME_SOLAR_ROUTE_GEOMETRY_H

#include "taiyin/vector3.h"

namespace taiyin {
namespace runtime {
namespace solar_route_geometry {

struct Frame {
    double right_ascension_offset_rad;
    double pole_rotation_rad;
    double gast_rad;
};

struct SurfacePoint {
    bool valid;
    double longitude_rad;
    double latitude_rad;
    double distance_to_fundamental_plane;
};

struct ProjectedIntersection {
    int count;
    double ax;
    double ay;
    double bx;
    double by;
    double distance_a;
    double distance_b;
};

struct ShadowRadii {
    double penumbra;
    double core;
    double absolute_core;
    double scale_factor;
};

struct LimitPoint {
    bool valid;
    double longitude_rad;
    double latitude_rad;
    double x;
    double y;
};

struct HorizonPoint {
    bool found;
    double longitude_rad;
    double latitude_rad;
};

struct LimitState {
    bool initialized;
    bool was_valid;
};

struct LimitSample {
    bool valid;
    double longitude_rad;
    double latitude_rad;
    bool endpoint_valid;
    double endpoint_longitude_rad;
    double endpoint_latitude_rad;
    double endpoint_time_offset_days;
    bool endpoint_entering;
};

Vector3 rotate_spherical_x(const Vector3& spherical, double angle_rad) noexcept;

ProjectedIntersection intersect_projected_ellipse_circle(
    double earth_radius,
    double earth_axis_ratio,
    double shadow_radius,
    double shadow_x,
    double shadow_y
) noexcept;

ProjectedIntersection intersect_projected_ellipse_line(
    double origin_x,
    double origin_y,
    double direction_x,
    double direction_y,
    double earth_radius,
    double earth_axis_ratio
) noexcept;

SurfacePoint fundamental_to_geodetic(
    double x,
    double y,
    double z,
    const Frame& frame,
    bool ellipsoid
) noexcept;

SurfacePoint shadow_axis_to_geodetic(
    double x,
    double y,
    const Frame& frame,
    bool ellipsoid
) noexcept;

ShadowRadii shadow_radii_at_plane(
    double moon_plane_distance,
    double penumbral_moon_radius,
    double umbral_moon_radius,
    double sun_radius_ratio,
    double penumbral_slope,
    double umbral_slope,
    double sun_moon_distance
) noexcept;

LimitPoint compute_shadow_limit(
    double shadow_x,
    double shadow_y,
    double shadow_z,
    double velocity_x_per_day,
    double velocity_y_per_day,
    int side,
    double shadow_radius,
    const Frame& frame,
    double moon_radius_ratio,
    double earth_axis_ratio
) noexcept;

HorizonPoint find_horizon_curve_point(
    double shadow_x,
    double shadow_y,
    double velocity_x_per_day,
    double velocity_y_per_day,
    bool second_root,
    double shadow_radius,
    const Frame& frame,
    double projected_axis_ratio
) noexcept;

LimitSample sample_shadow_limit(
    double shadow_x,
    double shadow_y,
    double shadow_z,
    double velocity_x_per_day,
    double velocity_y_per_day,
    int side,
    double shadow_radius,
    const Frame& frame,
    double moon_radius_ratio,
    double earth_axis_ratio,
    double projected_axis_ratio,
    LimitState* state
) noexcept;

}  // namespace solar_route_geometry
}  // namespace runtime
}  // namespace taiyin

#endif  // TAIYIN_RUNTIME_SOLAR_ROUTE_GEOMETRY_H
