#ifndef TAIYIN_RUNTIME_SOLAR_SHADOW_GEOMETRY_H
#define TAIYIN_RUNTIME_SOLAR_SHADOW_GEOMETRY_H

#include "taiyin/vector3.h"

namespace taiyin {
namespace runtime {

// A straight generator of a solar-shadow cone expressed in the Besselian
// shadow frame. The parameterization is point(t) = origin + t * direction.
struct SolarShadowGenerator {
    Vector3 origin;
    Vector3 direction;
};

struct SolarGeneratorEarthIntersection {
    int count;
    double discriminant;
    double quadratic_a;
    double vertex_parameter;
    double normalized_discriminant;
    double generator_direction_norm;
    double parameter[2];
    Vector3 point_shadow_frame[2];
    Vector3 point_equatorial_frame[2];

    SolarGeneratorEarthIntersection() noexcept;
};

struct SolarConeEarthPoint {
    bool valid;
    double generator_angle_rad;
    double generator_parameter;
    double line_discriminant;
    double distance_to_parameter_one;
    Vector3 point_shadow_frame;
    Vector3 point_equatorial_frame;
    double longitude_rad;
    double latitude_rad;

    SolarConeEarthPoint() noexcept;
};

struct SolarConeEarthTangency {
    bool valid;
    double generator_angle_rad;
    double normalized_discriminant;
    double vertex_parameter;

    SolarConeEarthTangency() noexcept;
};

// Build one generator of a circular cone. axis_x/axis_y locate the shadow
// axis in the fundamental plane, moon_z is the Moon-plane coordinate,
// moon_radius is the occluder radius there, and fundamental_radius is the
// cone radius at z=0. All distances use the same units (normally equatorial
// Earth radii).
SolarShadowGenerator make_solar_circular_cone_generator(
    double axis_x,
    double axis_y,
    double moon_z,
    double moon_radius,
    double fundamental_radius,
    double generator_angle_rad
) noexcept;

// Intersect an infinite generator line with an oblate Earth. The shadow frame
// is rotated about x by frame_rotation_rad before applying
// x^2 + y^2 + z^2 / axis_ratio^2 = 1. Both roots are returned in ascending
// generator-parameter order.
bool intersect_solar_generator_with_oblate_earth(
    const SolarShadowGenerator& generator,
    double frame_rotation_rad,
    double earth_axis_ratio,
    SolarGeneratorEarthIntersection* out
) noexcept;

// Select the line/ellipsoid intersection closest to the generator origin and
// convert it to geodetic longitude/latitude. longitude_rotation_rad is the
// shadow-frame longitude offset (Besselian J - GAST).
bool select_solar_generator_earth_point(
    const SolarGeneratorEarthIntersection& intersection,
    double generator_angle_rad,
    double longitude_rotation_rad,
    double earth_axis_ratio,
    SolarConeEarthPoint* out
) noexcept;

bool intersect_solar_circular_cone_generator_with_oblate_earth(
    double axis_x,
    double axis_y,
    double moon_z,
    double moon_radius,
    double fundamental_radius,
    double generator_angle_rad,
    double frame_rotation_rad,
    double longitude_rotation_rad,
    double earth_axis_ratio,
    SolarConeEarthPoint* out
) noexcept;

// Intersect the center axis with Earth. This is the zero-radius specialization
// used for the center line and the global central-contact discriminant.
bool intersect_solar_shadow_axis_with_oblate_earth(
    double axis_x,
    double axis_y,
    double frame_rotation_rad,
    double longitude_rotation_rad,
    double earth_axis_ratio,
    SolarConeEarthPoint* out
) noexcept;

// Find the cone generator closest to tangency with the oblate Earth. The
// returned normalized discriminant is negative before contact, zero at
// tangency, and positive while at least one forward generator intersects.
bool maximize_solar_circular_cone_earth_discriminant(
    double axis_x,
    double axis_y,
    double moon_z,
    double moon_radius,
    double fundamental_radius,
    double frame_rotation_rad,
    double earth_axis_ratio,
    SolarConeEarthTangency* out
) noexcept;

}  // namespace runtime
}  // namespace taiyin

#endif  // TAIYIN_RUNTIME_SOLAR_SHADOW_GEOMETRY_H
