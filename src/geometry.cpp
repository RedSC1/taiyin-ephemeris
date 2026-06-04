#include "taiyin/geometry.h"

#include "taiyin/angle.h"

#include <cmath>

namespace taiyin {
namespace {

const double DENOMINATOR_EPSILON = 1.0e-15;

Vector3 split_earth_from_emb_moon_vector(
    const Vector3& emb,
    const Vector3& moon_geocentric,
    double earth_moon_mass_ratio
) noexcept {
    const double factor = 1.0 / (1.0 + earth_moon_mass_ratio);
    return vector3_subtract(emb, vector3_scale(moon_geocentric, factor));
}

}  // namespace

bool cartesian_position_velocity_to_ecliptic(
    const Vector3& position_au,
    const Vector3& velocity_au_per_day,
    EclipticPositionVelocity* out
) noexcept {
    if (!out) {
        return false;
    }

    const double xy2 = position_au.x * position_au.x + position_au.y * position_au.y;
    const double r2 = xy2 + position_au.z * position_au.z;
    if (xy2 == 0.0 || r2 == 0.0) {
        return false;
    }

    const double xy = std::sqrt(xy2);
    const double r = std::sqrt(r2);
    const double xy_dot = (position_au.x * velocity_au_per_day.x
        + position_au.y * velocity_au_per_day.y) / xy;

    out->longitude_rad = normalize_radians(std::atan2(position_au.y, position_au.x));
    out->latitude_rad = std::atan2(position_au.z, xy);
    out->radius_au = r;
    out->longitude_rate_rad_per_day = (position_au.x * velocity_au_per_day.y
        - position_au.y * velocity_au_per_day.x) / xy2;
    out->latitude_rate_rad_per_day = (xy * velocity_au_per_day.z
        - position_au.z * xy_dot) / r2;
    out->radius_rate_au_per_day = vector3_dot(position_au, velocity_au_per_day) / r;
    return true;
}

bool cartesian_position_velocity_acceleration_to_ecliptic(
    const Vector3& position_au,
    const Vector3& velocity_au_per_day,
    const Vector3& acceleration_au_per_day2,
    EclipticPositionVelocityAcceleration* out
) noexcept {
    if (!out) {
        return false;
    }

    const double xy2 = position_au.x * position_au.x + position_au.y * position_au.y;
    const double r2 = xy2 + position_au.z * position_au.z;
    if (xy2 == 0.0 || r2 == 0.0) {
        return false;
    }

    const double xy = std::sqrt(xy2);
    const double r = std::sqrt(r2);
    const double xy_dot_numerator = position_au.x * velocity_au_per_day.x
        + position_au.y * velocity_au_per_day.y;
    const double xy_dot = xy_dot_numerator / xy;
    const double xy_ddot_numerator = velocity_au_per_day.x * velocity_au_per_day.x
        + velocity_au_per_day.y * velocity_au_per_day.y
        + position_au.x * acceleration_au_per_day2.x
        + position_au.y * acceleration_au_per_day2.y;
    const double xy_ddot = xy_ddot_numerator / xy
        - (xy_dot_numerator * xy_dot_numerator) / (xy * xy * xy);

    const double longitude_numerator = position_au.x * velocity_au_per_day.y
        - position_au.y * velocity_au_per_day.x;
    const double longitude_numerator_dot = position_au.x * acceleration_au_per_day2.y
        - position_au.y * acceleration_au_per_day2.x;
    const double xy2_dot = 2.0 * xy_dot_numerator;

    const double latitude_numerator = xy * velocity_au_per_day.z - position_au.z * xy_dot;
    const double latitude_numerator_dot = xy * acceleration_au_per_day2.z - position_au.z * xy_ddot;
    const double r2_dot = 2.0 * vector3_dot(position_au, velocity_au_per_day);
    const double radius_dot_numerator = vector3_dot(position_au, velocity_au_per_day);
    const double radius_ddot_numerator = vector3_dot(velocity_au_per_day, velocity_au_per_day)
        + vector3_dot(position_au, acceleration_au_per_day2);

    out->longitude_rad = normalize_radians(std::atan2(position_au.y, position_au.x));
    out->latitude_rad = std::atan2(position_au.z, xy);
    out->radius_au = r;
    out->longitude_rate_rad_per_day = longitude_numerator / xy2;
    out->latitude_rate_rad_per_day = latitude_numerator / r2;
    out->radius_rate_au_per_day = radius_dot_numerator / r;
    out->longitude_acceleration_rad_per_day2 = (longitude_numerator_dot * xy2
        - longitude_numerator * xy2_dot) / (xy2 * xy2);
    out->latitude_acceleration_rad_per_day2 = (latitude_numerator_dot * r2
        - latitude_numerator * r2_dot) / (r2 * r2);
    out->radius_acceleration_au_per_day2 = radius_ddot_numerator / r
        - (radius_dot_numerator * radius_dot_numerator) / (r * r * r);
    return true;
}

bool cartesian_position_velocity_acceleration_to_equatorial(
    const Vector3& position_au,
    const Vector3& velocity_au_per_day,
    const Vector3& acceleration_au_per_day2,
    EquatorialPositionVelocityAcceleration* out
) noexcept {
    if (!out) {
        return false;
    }

    EclipticPositionVelocityAcceleration spherical;
    if (!cartesian_position_velocity_acceleration_to_ecliptic(
            position_au,
            velocity_au_per_day,
            acceleration_au_per_day2,
            &spherical)) {
        return false;
    }

    out->right_ascension_rad = spherical.longitude_rad;
    out->declination_rad = spherical.latitude_rad;
    out->distance_au = spherical.radius_au;
    out->right_ascension_rate_rad_per_day = spherical.longitude_rate_rad_per_day;
    out->declination_rate_rad_per_day = spherical.latitude_rate_rad_per_day;
    out->distance_rate_au_per_day = spherical.radius_rate_au_per_day;
    out->right_ascension_acceleration_rad_per_day2 = spherical.longitude_acceleration_rad_per_day2;
    out->declination_acceleration_rad_per_day2 = spherical.latitude_acceleration_rad_per_day2;
    out->distance_acceleration_au_per_day2 = spherical.radius_acceleration_au_per_day2;
    return true;
}

Vector3 split_earth_from_emb_moon_position(
    const Vector3& emb_position_au,
    const Vector3& moon_geocentric_position_au,
    double earth_moon_mass_ratio
) noexcept {
    return split_earth_from_emb_moon_vector(
        emb_position_au,
        moon_geocentric_position_au,
        earth_moon_mass_ratio);
}

Vector3 split_earth_from_emb_moon_velocity(
    const Vector3& emb_velocity_au_per_day,
    const Vector3& moon_geocentric_velocity_au_per_day,
    double earth_moon_mass_ratio
) noexcept {
    return split_earth_from_emb_moon_vector(
        emb_velocity_au_per_day,
        moon_geocentric_velocity_au_per_day,
        earth_moon_mass_ratio);
}

Vector3 split_earth_from_emb_moon_acceleration(
    const Vector3& emb_acceleration_au_per_day2,
    const Vector3& moon_geocentric_acceleration_au_per_day2,
    double earth_moon_mass_ratio
) noexcept {
    return split_earth_from_emb_moon_vector(
        emb_acceleration_au_per_day2,
        moon_geocentric_acceleration_au_per_day2,
        earth_moon_mass_ratio);
}

double light_time_tau_from_distance(
    double distance_au,
    double light_time_days_per_au,
    double shapiro_delay_days
) noexcept {
    return distance_au * light_time_days_per_au + shapiro_delay_days;
}

bool light_time_tau_dot_no_shapiro(
    double target_speed_along_direction_au_per_day,
    double observer_speed_along_direction_au_per_day,
    double light_time_days_per_au,
    double* out_tau_dot
) noexcept {
    if (!out_tau_dot) {
        return false;
    }

    const double distance_speed_without_tau = target_speed_along_direction_au_per_day
        - observer_speed_along_direction_au_per_day;
    const double denominator = 1.0
        + light_time_days_per_au * target_speed_along_direction_au_per_day;
    if (std::fabs(denominator) <= DENOMINATOR_EPSILON) {
        return false;
    }

    *out_tau_dot = light_time_days_per_au * distance_speed_without_tau / denominator;
    return true;
}

bool light_time_tau_dot_with_shapiro(
    double target_speed_along_direction_au_per_day,
    double observer_speed_along_direction_au_per_day,
    double observer_radius_speed_au_per_day,
    double target_radius_speed_au_per_day,
    double light_time_days_per_au,
    double shapiro_delay_derivative_by_radius_sum,
    double shapiro_delay_derivative_by_distance,
    double* out_tau_dot
) noexcept {
    if (!out_tau_dot) {
        return false;
    }

    const double distance_speed_without_tau = target_speed_along_direction_au_per_day
        - observer_speed_along_direction_au_per_day;
    const double numerator = light_time_days_per_au * distance_speed_without_tau
        + shapiro_delay_derivative_by_radius_sum
            * (observer_radius_speed_au_per_day + target_radius_speed_au_per_day)
        + shapiro_delay_derivative_by_distance * distance_speed_without_tau;
    const double denominator = 1.0
        + light_time_days_per_au * target_speed_along_direction_au_per_day
        + shapiro_delay_derivative_by_radius_sum * target_radius_speed_au_per_day
        + shapiro_delay_derivative_by_distance * target_speed_along_direction_au_per_day;
    if (std::fabs(denominator) <= DENOMINATOR_EPSILON) {
        return false;
    }

    *out_tau_dot = numerator / denominator;
    return true;
}

}  // namespace taiyin
