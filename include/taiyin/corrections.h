#ifndef TAIYIN_CORRECTIONS_H
#define TAIYIN_CORRECTIONS_H

#include "vector3.h"

namespace taiyin {

constexpr double TAIYIN_LIGHT_TIME_DAYS_PER_AU = 0.00577551833109;
constexpr double TAIYIN_SOLAR_SCHWARZSCHILD_RADIUS_AU = 1.97412574336e-8;
constexpr double TAIYIN_SOLAR_DEFLECTION_LIMIT = 1e-6;

typedef bool (*LightTimePositionFn)(
    double jd_tdb,
    const void* data,
    Vector3* out_position_au
);

typedef bool (*LightTimeVelocityFn)(
    double jd_tdb,
    const void* data,
    Vector3* out_velocity_au_per_day
);

typedef bool (*LightTimeAccelerationFn)(
    double jd_tdb,
    const void* data,
    Vector3* out_acceleration_au_per_day2
);

typedef bool (*ShapiroDeflectorPositionFn)(
    double jd_tdb,
    const void* data,
    int deflector_index,
    Vector3* out_position_au
);

typedef bool (*ShapiroDeflectorVelocityFn)(
    double jd_tdb,
    const void* data,
    int deflector_index,
    Vector3* out_velocity_au_per_day
);

typedef bool (*ShapiroDeflectorAccelerationFn)(
    double jd_tdb,
    const void* data,
    int deflector_index,
    Vector3* out_acceleration_au_per_day2
);

bool unit_vector_with_derivative(
    const Vector3& vector,
    const Vector3& vector_dot,
    Vector3* unit,
    Vector3* unit_dot,
    double* norm_out,
    double* norm_dot_out
) noexcept;

bool solar_shapiro_delay_terms(
    const Vector3& observer_heliocentric_position_au,
    const Vector3& target_heliocentric_position_au,
    const Vector3& target_geocentric_position_au,
    double schwarzschild_light_time_days,
    double* delay_days,
    double* delay_derivative_by_radius_sum,
    double* delay_derivative_by_distance
) noexcept;

bool solve_light_time_position(
    double jd_tdb,
    const Vector3& observer_position_au,
    LightTimePositionFn target_position,
    const void* target_data,
    double light_time_days_per_au,
    int max_iterations,
    double tolerance_days,
    Vector3* out_position_au,
    double* out_light_time_days,
    Vector3* out_retarded_target_position_au
) noexcept;

bool solve_light_time_velocity(
    double jd_tdb,
    const Vector3& observer_position_au,
    const Vector3& observer_velocity_au_per_day,
    LightTimePositionFn target_position,
    LightTimeVelocityFn target_velocity,
    const void* target_data,
    double light_time_days_per_au,
    int max_iterations,
    double tolerance_days,
    Vector3* out_position_au,
    Vector3* out_velocity_au_per_day,
    double* out_light_time_days,
    double* out_light_time_rate,
    Vector3* out_retarded_target_position_au,
    Vector3* out_retarded_target_velocity_au_per_day
) noexcept;

bool solve_light_time_acceleration(
    double jd_tdb,
    const Vector3& observer_position_au,
    const Vector3& observer_velocity_au_per_day,
    const Vector3& observer_acceleration_au_per_day2,
    LightTimePositionFn target_position,
    LightTimeVelocityFn target_velocity,
    LightTimeAccelerationFn target_acceleration,
    const void* target_data,
    double light_time_days_per_au,
    int max_iterations,
    double tolerance_days,
    Vector3* out_position_au,
    Vector3* out_velocity_au_per_day,
    Vector3* out_acceleration_au_per_day2,
    double* out_light_time_days,
    double* out_light_time_rate,
    double* out_light_time_acceleration,
    Vector3* out_retarded_target_position_au,
    Vector3* out_retarded_target_velocity_au_per_day,
    Vector3* out_retarded_target_acceleration_au_per_day2
) noexcept;

bool solve_light_time_position_with_shapiro(
    double jd_tdb,
    const Vector3& observer_heliocentric_position_au,
    LightTimePositionFn target_heliocentric_position,
    const void* target_data,
    double light_time_days_per_au,
    double schwarzschild_light_time_days,
    int max_iterations,
    double tolerance_days,
    Vector3* out_position_au,
    double* out_light_time_days,
    Vector3* out_retarded_target_heliocentric_position_au
) noexcept;

bool solve_light_time_velocity_with_shapiro(
    double jd_tdb,
    const Vector3& observer_heliocentric_position_au,
    const Vector3& observer_heliocentric_velocity_au_per_day,
    LightTimePositionFn target_heliocentric_position,
    LightTimeVelocityFn target_heliocentric_velocity,
    const void* target_data,
    double light_time_days_per_au,
    double schwarzschild_light_time_days,
    int max_iterations,
    double tolerance_days,
    Vector3* out_position_au,
    Vector3* out_velocity_au_per_day,
    double* out_light_time_days,
    double* out_light_time_rate,
    Vector3* out_retarded_target_heliocentric_position_au,
    Vector3* out_retarded_target_heliocentric_velocity_au_per_day
) noexcept;

bool solve_light_time_acceleration_with_shapiro(
    double jd_tdb,
    const Vector3& observer_heliocentric_position_au,
    const Vector3& observer_heliocentric_velocity_au_per_day,
    const Vector3& observer_heliocentric_acceleration_au_per_day2,
    LightTimePositionFn target_heliocentric_position,
    LightTimeVelocityFn target_heliocentric_velocity,
    LightTimeAccelerationFn target_heliocentric_acceleration,
    const void* target_data,
    double light_time_days_per_au,
    double schwarzschild_light_time_days,
    int max_iterations,
    double tolerance_days,
    Vector3* out_position_au,
    Vector3* out_velocity_au_per_day,
    Vector3* out_acceleration_au_per_day2,
    double* out_light_time_days,
    double* out_light_time_rate,
    double* out_light_time_acceleration,
    Vector3* out_retarded_target_heliocentric_position_au,
    Vector3* out_retarded_target_heliocentric_velocity_au_per_day,
    Vector3* out_retarded_target_heliocentric_acceleration_au_per_day2
) noexcept;

bool solve_light_time_position_with_multi_shapiro(
    double jd_tdb,
    const Vector3& observer_primary_relative_position_au,
    LightTimePositionFn target_primary_relative_position,
    const void* target_data,
    int deflector_count,
    const double* deflector_schwarzschild_radius_au,
    ShapiroDeflectorPositionFn deflector_primary_relative_position,
    const void* deflector_data,
    double light_time_days_per_au,
    int max_iterations,
    double tolerance_days,
    Vector3* out_position_au,
    double* out_light_time_days,
    Vector3* out_retarded_target_primary_relative_position_au
) noexcept;

bool solve_light_time_velocity_with_multi_shapiro(
    double jd_tdb,
    const Vector3& observer_primary_relative_position_au,
    const Vector3& observer_primary_relative_velocity_au_per_day,
    LightTimePositionFn target_primary_relative_position,
    LightTimeVelocityFn target_primary_relative_velocity,
    const void* target_data,
    int deflector_count,
    const double* deflector_schwarzschild_radius_au,
    ShapiroDeflectorPositionFn deflector_primary_relative_position,
    ShapiroDeflectorVelocityFn deflector_primary_relative_velocity,
    const void* deflector_data,
    double light_time_days_per_au,
    int max_iterations,
    double tolerance_days,
    Vector3* out_position_au,
    Vector3* out_velocity_au_per_day,
    double* out_light_time_days,
    double* out_light_time_rate,
    Vector3* out_retarded_target_primary_relative_position_au,
    Vector3* out_retarded_target_primary_relative_velocity_au_per_day
) noexcept;

bool solve_light_time_acceleration_with_multi_shapiro(
    double jd_tdb,
    const Vector3& observer_primary_relative_position_au,
    const Vector3& observer_primary_relative_velocity_au_per_day,
    const Vector3& observer_primary_relative_acceleration_au_per_day2,
    LightTimePositionFn target_primary_relative_position,
    LightTimeVelocityFn target_primary_relative_velocity,
    LightTimeAccelerationFn target_primary_relative_acceleration,
    const void* target_data,
    int deflector_count,
    const double* deflector_schwarzschild_radius_au,
    ShapiroDeflectorPositionFn deflector_primary_relative_position,
    ShapiroDeflectorVelocityFn deflector_primary_relative_velocity,
    ShapiroDeflectorAccelerationFn deflector_primary_relative_acceleration,
    const void* deflector_data,
    double light_time_days_per_au,
    int max_iterations,
    double tolerance_days,
    Vector3* out_position_au,
    Vector3* out_velocity_au_per_day,
    Vector3* out_acceleration_au_per_day2,
    double* out_light_time_days,
    double* out_light_time_rate,
    double* out_light_time_acceleration,
    Vector3* out_retarded_target_primary_relative_position_au,
    Vector3* out_retarded_target_primary_relative_velocity_au_per_day,
    Vector3* out_retarded_target_primary_relative_acceleration_au_per_day2
) noexcept;

bool apply_observer_velocity_aberration(
    const Vector3& source_topocentric_position_au,
    const Vector3& source_topocentric_velocity_au_per_day,
    const Vector3& observer_velocity_au_per_day,
    const Vector3& observer_acceleration_au_per_day2,
    double light_time_days_per_au,
    Vector3* out_position_au,
    Vector3* out_velocity_au_per_day
) noexcept;

bool apply_observer_velocity_aberration_acceleration(
    const Vector3& source_topocentric_position_au,
    const Vector3& source_topocentric_velocity_au_per_day,
    const Vector3& source_topocentric_acceleration_au_per_day2,
    const Vector3& observer_velocity_au_per_day,
    const Vector3& observer_acceleration_au_per_day2,
    double light_time_days_per_au,
    Vector3* out_position_au,
    Vector3* out_velocity_au_per_day,
    Vector3* out_acceleration_au_per_day2
) noexcept;

bool apply_annual_aberration(
    const Vector3& source_geocentric_position_au,
    const Vector3& source_geocentric_velocity_au_per_day,
    const Vector3& observer_heliocentric_position_au,
    const Vector3& observer_heliocentric_velocity_au_per_day,
    const Vector3& observer_barycentric_velocity_au_per_day,
    const Vector3& observer_barycentric_acceleration_au_per_day2,
    double light_time_days_per_au,
    double solar_schwarzschild_radius_au,
    Vector3* out_position_au,
    Vector3* out_velocity_au_per_day
) noexcept;

bool apply_annual_aberration_acceleration(
    const Vector3& source_geocentric_position_au,
    const Vector3& source_geocentric_velocity_au_per_day,
    const Vector3& source_geocentric_acceleration_au_per_day2,
    const Vector3& observer_heliocentric_position_au,
    const Vector3& observer_heliocentric_velocity_au_per_day,
    const Vector3& observer_heliocentric_acceleration_au_per_day2,
    const Vector3& observer_barycentric_velocity_au_per_day,
    const Vector3& observer_barycentric_acceleration_au_per_day2,
    double light_time_days_per_au,
    double solar_schwarzschild_radius_au,
    Vector3* out_position_au,
    Vector3* out_velocity_au_per_day,
    Vector3* out_acceleration_au_per_day2
) noexcept;

bool apply_gravitational_deflection_from_body(
    const Vector3& apparent_position_au,
    const Vector3& apparent_velocity_au_per_day,
    const Vector3& observer_heliocentric_position_au,
    const Vector3& observer_heliocentric_velocity_au_per_day,
    const Vector3& deflector_heliocentric_position_au,
    const Vector3& deflector_heliocentric_velocity_au_per_day,
    const Vector3& source_geocentric_position_au,
    const Vector3& source_geocentric_velocity_au_per_day,
    double schwarzschild_radius_au,
    double deflection_limit,
    Vector3* out_position_au,
    Vector3* out_velocity_au_per_day
) noexcept;

bool apply_gravitational_deflection_from_body_acceleration(
    const Vector3& apparent_position_au,
    const Vector3& apparent_velocity_au_per_day,
    const Vector3& apparent_acceleration_au_per_day2,
    const Vector3& observer_heliocentric_position_au,
    const Vector3& observer_heliocentric_velocity_au_per_day,
    const Vector3& observer_heliocentric_acceleration_au_per_day2,
    const Vector3& deflector_heliocentric_position_au,
    const Vector3& deflector_heliocentric_velocity_au_per_day,
    const Vector3& deflector_heliocentric_acceleration_au_per_day2,
    const Vector3& source_geocentric_position_au,
    const Vector3& source_geocentric_velocity_au_per_day,
    const Vector3& source_geocentric_acceleration_au_per_day2,
    double schwarzschild_radius_au,
    double deflection_limit,
    Vector3* out_position_au,
    Vector3* out_velocity_au_per_day,
    Vector3* out_acceleration_au_per_day2
) noexcept;

}  // namespace taiyin

#endif  // TAIYIN_CORRECTIONS_H
