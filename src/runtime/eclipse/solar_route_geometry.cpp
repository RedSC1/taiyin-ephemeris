#include "runtime/eclipse/solar_route_geometry.h"

#include "runtime/eclipse/solar_shadow_geometry.h"

#include "taiyin/geodetic_constants.h"
#include "taiyin/math_solvers.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace taiyin {
namespace runtime {
namespace solar_route_geometry {
namespace {

constexpr double kTwoPi = 2.0 * M_PI;

double normalize_signed(double value) noexcept {
    value = std::fmod(value + M_PI, kTwoPi);
    if (value < 0.0) value += kTwoPi;
    return value - M_PI;
}

struct SurfaceVelocity {
    double relative_x;
    double relative_y;
};

SurfaceVelocity surface_relative_velocity(
    double x,
    double y,
    double pole_rotation_rad,
    double velocity_x_per_day,
    double velocity_y_per_day
) noexcept {
    const double z2 = std::max(0.0, 1.0 - x * x - y * y);
    const double z = std::sqrt(z2);
    const double surface_x = kTwoPi * (
        std::sin(pole_rotation_rad) * z
        - std::cos(pole_rotation_rad) * y);
    const double surface_y = kTwoPi * x * std::cos(pole_rotation_rad);
    return {velocity_x_per_day - surface_x, velocity_y_per_day - surface_y};
}

}  // namespace

Vector3 rotate_spherical_x(const Vector3& spherical, double angle_rad) noexcept {
    const Vector3 cartesian = spherical_to_cartesian(
        spherical.x, spherical.y, spherical.z);
    double longitude = NAN;
    double latitude = NAN;
    double radius = NAN;
    cartesian_to_spherical(
        rotate_x(cartesian, angle_rad), &longitude, &latitude, &radius);
    return {longitude, latitude, radius};
}

ProjectedIntersection intersect_projected_ellipse_circle(
    double earth_radius,
    double earth_axis_ratio,
    double shadow_radius,
    double shadow_x,
    double shadow_y
) noexcept {
    ProjectedIntersection out{};
    if (!(earth_radius > 0.0) || !(earth_axis_ratio > 0.0)
        || !(shadow_radius >= 0.0) || !std::isfinite(shadow_x)
        || !std::isfinite(shadow_y)) {
        return out;
    }

    out.count = intersect_ellipse_circle(
        earth_radius,
        earth_axis_ratio,
        shadow_radius,
        shadow_x,
        shadow_y,
        &out.ax,
        &out.ay,
        &out.bx,
        &out.by);
    return out;
}

ProjectedIntersection intersect_projected_ellipse_line(
    double origin_x,
    double origin_y,
    double direction_x,
    double direction_y,
    double earth_radius,
    double earth_axis_ratio
) noexcept {
    ProjectedIntersection out{};
    const double inverse_b2 = 1.0
        / (earth_radius * earth_radius * earth_axis_ratio * earth_axis_ratio);
    const double inverse_a2 = 1.0 / (earth_radius * earth_radius);
    const double a = direction_x * direction_x * inverse_a2
        + direction_y * direction_y * inverse_b2;
    const double b = 2.0 * (origin_x * direction_x * inverse_a2
        + origin_y * direction_y * inverse_b2);
    const double c = origin_x * origin_x * inverse_a2
        + origin_y * origin_y * inverse_b2 - 1.0;
    if (!(a > 0.0) || !std::isfinite(a) || !std::isfinite(b)
        || !std::isfinite(c)) {
        return out;
    }
    const double discriminant = b * b - 4.0 * a * c;
    if (discriminant < 0.0 || !std::isfinite(discriminant)) return out;
    const double root = std::sqrt(std::max(0.0, discriminant));
    const double t0 = (-b + root) / (2.0 * a);
    const double t1 = (-b - root) / (2.0 * a);
    out.count = discriminant == 0.0 ? 1 : 2;
    out.ax = origin_x + direction_x * t0;
    out.ay = origin_y + direction_y * t0;
    out.bx = origin_x + direction_x * t1;
    out.by = origin_y + direction_y * t1;
    const double direction_norm = std::hypot(direction_x, direction_y);
    out.distance_a = direction_norm * std::fabs(t0);
    out.distance_b = direction_norm * std::fabs(t1);
    return out;
}

SurfacePoint fundamental_to_geodetic(
    double x,
    double y,
    double z,
    const Frame& frame,
    bool ellipsoid
) noexcept {
    SurfacePoint out{};
    const Vector3 equatorial = rotate_x(
        Vector3{x, y, z}, frame.pole_rotation_rad);
    const double horizontal = std::hypot(equatorial.x, equatorial.y);
    if (!(horizontal > 0.0) || !std::isfinite(equatorial.z)) return out;
    out.valid = true;
    out.longitude_rad = normalize_signed(
        std::atan2(equatorial.y, equatorial.x)
        + frame.right_ascension_offset_rad - frame.gast_rad);
    if (ellipsoid) {
        const double ratio = TAIYIN_WGS84_B_KM / TAIYIN_WGS84_A_KM;
        out.latitude_rad = std::atan2(
            equatorial.z / (ratio * ratio), horizontal);
    } else {
        out.latitude_rad = std::atan2(equatorial.z, horizontal);
    }
    return out;
}

SurfacePoint shadow_axis_to_geodetic(
    double x,
    double y,
    const Frame& frame,
    bool ellipsoid
) noexcept {
    SurfacePoint out{};
    const double ratio = ellipsoid
        ? TAIYIN_WGS84_B_KM / TAIYIN_WGS84_A_KM
        : 1.0;
    SolarConeEarthPoint point;
    if (!intersect_solar_shadow_axis_with_oblate_earth(
            x,
            y,
            frame.pole_rotation_rad,
            frame.right_ascension_offset_rad - frame.gast_rad,
            ratio,
            &point)
        || !point.valid) {
        return out;
    }
    out.valid = true;
    out.longitude_rad = point.longitude_rad;
    out.latitude_rad = point.latitude_rad;
    out.distance_to_fundamental_plane = point.distance_to_parameter_one;
    return out;
}

ShadowRadii shadow_radii_at_plane(
    double moon_plane_distance,
    double penumbral_moon_radius,
    double umbral_moon_radius,
    double sun_radius_ratio,
    double penumbral_slope,
    double umbral_slope,
    double sun_moon_distance
) noexcept {
    ShadowRadii out{};
    out.penumbra = penumbral_moon_radius
        + penumbral_slope * moon_plane_distance;
    out.core = umbral_moon_radius - umbral_slope * moon_plane_distance;
    out.absolute_core = std::fabs(out.core);
    out.scale_factor = umbral_moon_radius / moon_plane_distance
        / sun_radius_ratio * (sun_moon_distance + moon_plane_distance);
    return out;
}

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
) noexcept {
    LimitPoint out{};
    if (side == 0 || !std::isfinite(shadow_radius)
        || !(std::hypot(velocity_x_per_day, velocity_y_per_day) > 0.0)) {
        return out;
    }
    const double side_sign = static_cast<double>(side);
    double generator_angle = std::atan2(
        side_sign * velocity_x_per_day,
        -side_sign * velocity_y_per_day);
    for (int iteration = 0; iteration < 5; ++iteration) {
        const double nx = std::cos(generator_angle);
        const double ny = std::sin(generator_angle);
        const double candidate_x = shadow_x + shadow_radius * nx;
        const double candidate_y = shadow_y + shadow_radius * ny;
        const SurfaceVelocity relative = surface_relative_velocity(
            candidate_x,
            candidate_y,
            frame.pole_rotation_rad,
            velocity_x_per_day,
            velocity_y_per_day);
        if (!(std::hypot(relative.relative_x, relative.relative_y) > 0.0)) {
            return out;
        }
        generator_angle = std::atan2(
            side_sign * relative.relative_x,
            -side_sign * relative.relative_y);
    }
    // Preserve the projected generator point even when the generator misses
    // the ellipsoid.  sample_shadow_limit() uses it to interpolate the
    // transition between a valid limit and the horizon.
    out.x = shadow_x + shadow_radius * std::cos(generator_angle);
    out.y = shadow_y + shadow_radius * std::sin(generator_angle);
    SolarConeEarthPoint earth_point;
    if (!intersect_solar_circular_cone_generator_with_oblate_earth(
            shadow_x,
            shadow_y,
            shadow_z,
            moon_radius_ratio,
            shadow_radius,
            generator_angle,
            frame.pole_rotation_rad,
            frame.right_ascension_offset_rad - frame.gast_rad,
            earth_axis_ratio,
            &earth_point)
        || !earth_point.valid) {
        return out;
    }
    out.valid = true;
    out.longitude_rad = earth_point.longitude_rad;
    out.latitude_rad = earth_point.latitude_rad;
    return out;
}

HorizonPoint find_horizon_curve_point(
    double shadow_x,
    double shadow_y,
    double velocity_x_per_day,
    double velocity_y_per_day,
    bool second_root,
    double shadow_radius,
    const Frame& frame,
    double projected_axis_ratio
) noexcept {
    HorizonPoint out{};
    double x = shadow_x;
    double y = shadow_y;
    ProjectedIntersection intersection{};
    for (int iteration = 0; iteration < 3; ++iteration) {
        const SurfaceVelocity relative = surface_relative_velocity(
            x,
            y,
            frame.pole_rotation_rad,
            velocity_x_per_day,
            velocity_y_per_day);
        intersection = intersect_projected_ellipse_line(
            shadow_x,
            shadow_y,
            relative.relative_y,
            -relative.relative_x,
            1.0,
            projected_axis_ratio);
        if (intersection.count == 0) return out;
        if (second_root) {
            x = intersection.ax;
            y = intersection.ay;
        } else {
            x = intersection.bx;
            y = intersection.by;
        }
    }
    const double distance = second_root
        ? intersection.distance_a
        : intersection.distance_b;
    if (distance > shadow_radius) return out;
    const SurfacePoint point = fundamental_to_geodetic(x, y, 0.0, frame, true);
    if (!point.valid) return out;
    out.found = true;
    out.longitude_rad = point.longitude_rad;
    out.latitude_rad = point.latitude_rad;
    return out;
}

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
) noexcept {
    LimitSample out{};
    const LimitPoint point = compute_shadow_limit(
        shadow_x,
        shadow_y,
        shadow_z,
        velocity_x_per_day,
        velocity_y_per_day,
        side,
        shadow_radius,
        frame,
        moon_radius_ratio,
        earth_axis_ratio);
    if (state) {
        const bool changed = state->initialized && state->was_valid != point.valid;
        if (changed) {
            const ProjectedIntersection crossing = intersect_projected_ellipse_line(
                point.x,
                point.y,
                velocity_x_per_day,
                velocity_y_per_day,
                1.0,
                projected_axis_ratio);
            const double speed = std::hypot(
                velocity_x_per_day, velocity_y_per_day);
            if (crossing.count > 0 && speed > 1.0e-14) {
                const bool entering = point.valid;
                const double distance = entering
                    ? crossing.distance_b
                    : crossing.distance_a;
                const double endpoint_x = entering ? crossing.bx : crossing.ax;
                const double endpoint_y = entering ? crossing.by : crossing.ay;
                Frame endpoint_frame = frame;
                endpoint_frame.gast_rad -= distance / speed * kTwoPi;
                const SurfacePoint endpoint = fundamental_to_geodetic(
                    endpoint_x, endpoint_y, 0.0, endpoint_frame, true);
                if (endpoint.valid) {
                    out.endpoint_valid = true;
                    out.endpoint_longitude_rad = endpoint.longitude_rad;
                    out.endpoint_latitude_rad = endpoint.latitude_rad;
                    out.endpoint_time_offset_days = -distance / speed;
                    out.endpoint_entering = entering;
                }
            }
        }
        state->initialized = true;
        state->was_valid = point.valid;
    }
    if (point.valid) {
        out.valid = true;
        out.longitude_rad = point.longitude_rad;
        out.latitude_rad = point.latitude_rad;
    }
    return out;
}

}  // namespace solar_route_geometry
}  // namespace runtime
}  // namespace taiyin
