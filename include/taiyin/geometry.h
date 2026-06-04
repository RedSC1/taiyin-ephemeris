#ifndef TAIYIN_GEOMETRY_H
#define TAIYIN_GEOMETRY_H

#include "vector3.h"

namespace taiyin {

struct EclipticPositionVelocity {
    double longitude_rad;
    double latitude_rad;
    double radius_au;
    double longitude_rate_rad_per_day;
    double latitude_rate_rad_per_day;
    double radius_rate_au_per_day;
};

struct EclipticPositionVelocityAcceleration {
    double longitude_rad;
    double latitude_rad;
    double radius_au;
    double longitude_rate_rad_per_day;
    double latitude_rate_rad_per_day;
    double radius_rate_au_per_day;
    double longitude_acceleration_rad_per_day2;
    double latitude_acceleration_rad_per_day2;
    double radius_acceleration_au_per_day2;
};

struct EquatorialPositionVelocityAcceleration {
    double right_ascension_rad;
    double declination_rad;
    double distance_au;
    double right_ascension_rate_rad_per_day;
    double declination_rate_rad_per_day;
    double distance_rate_au_per_day;
    double right_ascension_acceleration_rad_per_day2;
    double declination_acceleration_rad_per_day2;
    double distance_acceleration_au_per_day2;
};

bool cartesian_position_velocity_to_ecliptic(
    const Vector3& position_au,
    const Vector3& velocity_au_per_day,
    EclipticPositionVelocity* out
) noexcept;

bool cartesian_position_velocity_acceleration_to_ecliptic(
    const Vector3& position_au,
    const Vector3& velocity_au_per_day,
    const Vector3& acceleration_au_per_day2,
    EclipticPositionVelocityAcceleration* out
) noexcept;

bool cartesian_position_velocity_acceleration_to_equatorial(
    const Vector3& position_au,
    const Vector3& velocity_au_per_day,
    const Vector3& acceleration_au_per_day2,
    EquatorialPositionVelocityAcceleration* out
) noexcept;

Vector3 split_earth_from_emb_moon_position(
    const Vector3& emb_position_au,
    const Vector3& moon_geocentric_position_au,
    double earth_moon_mass_ratio
) noexcept;

Vector3 split_earth_from_emb_moon_velocity(
    const Vector3& emb_velocity_au_per_day,
    const Vector3& moon_geocentric_velocity_au_per_day,
    double earth_moon_mass_ratio
) noexcept;

Vector3 split_earth_from_emb_moon_acceleration(
    const Vector3& emb_acceleration_au_per_day2,
    const Vector3& moon_geocentric_acceleration_au_per_day2,
    double earth_moon_mass_ratio
) noexcept;

double light_time_tau_from_distance(
    double distance_au,
    double light_time_days_per_au,
    double shapiro_delay_days
) noexcept;

bool light_time_tau_dot_no_shapiro(
    double target_speed_along_direction_au_per_day,
    double observer_speed_along_direction_au_per_day,
    double light_time_days_per_au,
    double* out_tau_dot
) noexcept;

bool light_time_tau_dot_with_shapiro(
    double target_speed_along_direction_au_per_day,
    double observer_speed_along_direction_au_per_day,
    double observer_radius_speed_au_per_day,
    double target_radius_speed_au_per_day,
    double light_time_days_per_au,
    double shapiro_delay_derivative_by_radius_sum,
    double shapiro_delay_derivative_by_distance,
    double* out_tau_dot
) noexcept;

}  // namespace taiyin

#endif  // TAIYIN_GEOMETRY_H
