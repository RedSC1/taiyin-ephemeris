#include "taiyin/corrections.h"

#include "taiyin/geometry.h"

#include <cmath>

namespace taiyin {
namespace {

const int DEFAULT_LIGHT_TIME_MAX_ITERATIONS = 8;
const double DEFAULT_LIGHT_TIME_TOLERANCE_DAYS = 1e-13;
const double LIGHT_TIME_DENOMINATOR_EPSILON = 1e-14;
const double CORRECTION_ACCELERATION_STEP_DAYS = 1e-4;

int normalized_light_time_max_iterations(int max_iterations) noexcept {
    return max_iterations > 0 ? max_iterations : DEFAULT_LIGHT_TIME_MAX_ITERATIONS;
}

double normalized_light_time_tolerance_days(double tolerance_days) noexcept {
    return tolerance_days > 0.0 ? tolerance_days : DEFAULT_LIGHT_TIME_TOLERANCE_DAYS;
}

bool finite_vector(const Vector3& value) noexcept {
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

Vector3 constant_acceleration_position_at(
    const Vector3& position,
    const Vector3& velocity,
    const Vector3& acceleration,
    double dt_days
) noexcept {
    return vector3_add(
        vector3_add(position, vector3_scale(velocity, dt_days)),
        vector3_scale(acceleration, 0.5 * dt_days * dt_days));
}

Vector3 constant_acceleration_velocity_at(
    const Vector3& velocity,
    const Vector3& acceleration,
    double dt_days
) noexcept {
    return vector3_add(velocity, vector3_scale(acceleration, dt_days));
}

bool valid_light_time_inputs(
    LightTimePositionFn target_position,
    double light_time_days_per_au
) noexcept {
    return target_position
        && std::isfinite(light_time_days_per_au)
        && light_time_days_per_au > 0.0;
}

bool solve_light_time_iteration(
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
) noexcept {
    if (!out_position_au || !out_light_time_days) {
        return false;
    }
    if (!valid_light_time_inputs(target_position, light_time_days_per_au)
        || !std::isfinite(jd_tdb)
        || !finite_vector(observer_position_au)) {
        return false;
    }

    Vector3 target;
    if (!target_position(jd_tdb, target_data, &target) || !finite_vector(target)) {
        return false;
    }
    Vector3 position = vector3_subtract(target, observer_position_au);
    double distance = vector3_norm(position);
    if (!std::isfinite(distance) || distance == 0.0) {
        return false;
    }

    double light_time_days = distance * light_time_days_per_au;
    const int iterations = normalized_light_time_max_iterations(max_iterations);
    const double tolerance = normalized_light_time_tolerance_days(tolerance_days);
    for (int i = 0; i < iterations; ++i) {
        const double emission_jd_tdb = jd_tdb - light_time_days;
        if (!std::isfinite(emission_jd_tdb)
            || !target_position(emission_jd_tdb, target_data, &target)
            || !finite_vector(target)) {
            return false;
        }
        position = vector3_subtract(target, observer_position_au);
        distance = vector3_norm(position);
        if (!std::isfinite(distance) || distance == 0.0) {
            return false;
        }
        const double next_light_time_days = distance * light_time_days_per_au;
        if (!std::isfinite(next_light_time_days)) {
            return false;
        }
        if (std::fabs(next_light_time_days - light_time_days) <= tolerance) {
            *out_position_au = position;
            *out_light_time_days = next_light_time_days;
            if (out_retarded_target_position_au) {
                *out_retarded_target_position_au = target;
            }
            return true;
        }
        light_time_days = next_light_time_days;
    }

    return false;
}

bool solve_light_time_position_velocity(
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
) noexcept {
    if (!out_position_au || !out_velocity_au_per_day || !out_light_time_days || !out_light_time_rate) {
        return false;
    }
    if (!target_velocity || !finite_vector(observer_velocity_au_per_day)) {
        return false;
    }

    Vector3 position;
    Vector3 target_retarded_position;
    double light_time_days = 0.0;
    if (!solve_light_time_iteration(
            jd_tdb,
            observer_position_au,
            target_position,
            target_data,
            light_time_days_per_au,
            max_iterations,
            tolerance_days,
            &position,
            &light_time_days,
            &target_retarded_position)) {
        return false;
    }

    const double emission_jd_tdb = jd_tdb - light_time_days;
    Vector3 target_retarded_velocity;
    if (!target_velocity(emission_jd_tdb, target_data, &target_retarded_velocity)
        || !finite_vector(target_retarded_velocity)) {
        return false;
    }

    const double distance = vector3_norm(position);
    if (!std::isfinite(distance) || distance == 0.0) {
        return false;
    }
    const Vector3 direction = vector3_scale(position, 1.0 / distance);
    double light_time_rate = 0.0;
    if (!light_time_tau_dot_no_shapiro(
            vector3_dot(direction, target_retarded_velocity),
            vector3_dot(direction, observer_velocity_au_per_day),
            light_time_days_per_au,
            &light_time_rate)) {
        return false;
    }

    const Vector3 velocity = vector3_subtract(
        vector3_scale(target_retarded_velocity, 1.0 - light_time_rate),
        observer_velocity_au_per_day);
    if (!finite_vector(velocity) || !std::isfinite(light_time_rate)) {
        return false;
    }

    *out_position_au = position;
    *out_velocity_au_per_day = velocity;
    *out_light_time_days = light_time_days;
    *out_light_time_rate = light_time_rate;
    if (out_retarded_target_position_au) {
        *out_retarded_target_position_au = target_retarded_position;
    }
    if (out_retarded_target_velocity_au_per_day) {
        *out_retarded_target_velocity_au_per_day = target_retarded_velocity;
    }
    return true;
}

struct ShapiroPartials {
    double delay_days;
    double radius_sum_derivative;
    double distance_derivative;
    double radius_sum_second_derivative;
    double radius_sum_distance_derivative;
    double distance_second_derivative;
};

bool shapiro_delay_partials(
    double radius_sum_au,
    double distance_au,
    double schwarzschild_light_time_days,
    ShapiroPartials* out
) noexcept {
    if (!out || radius_sum_au <= 0.0 || distance_au <= 0.0) {
        return false;
    }

    const double numerator = radius_sum_au + distance_au;
    const double denominator = radius_sum_au - distance_au;
    const double radius_difference = radius_sum_au * radius_sum_au - distance_au * distance_au;
    if (numerator <= 0.0 || denominator <= 0.0 || radius_difference == 0.0) {
        return false;
    }

    const double radius_difference2 = radius_difference * radius_difference;
    out->delay_days = schwarzschild_light_time_days * std::log(numerator / denominator);
    out->radius_sum_derivative = -2.0 * schwarzschild_light_time_days * distance_au / radius_difference;
    out->distance_derivative = 2.0 * schwarzschild_light_time_days * radius_sum_au / radius_difference;
    out->radius_sum_second_derivative = 4.0 * schwarzschild_light_time_days * radius_sum_au * distance_au / radius_difference2;
    out->radius_sum_distance_derivative = -2.0 * schwarzschild_light_time_days
        * (radius_sum_au * radius_sum_au + distance_au * distance_au)
        / radius_difference2;
    out->distance_second_derivative = out->radius_sum_second_derivative;
    return true;
}

bool solve_light_time_shapiro_iteration(
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
) noexcept {
    if (!out_position_au || !out_light_time_days) {
        return false;
    }
    if (!valid_light_time_inputs(target_heliocentric_position, light_time_days_per_au)
        || !std::isfinite(jd_tdb)
        || !std::isfinite(schwarzschild_light_time_days)
        || schwarzschild_light_time_days < 0.0
        || !finite_vector(observer_heliocentric_position_au)) {
        return false;
    }

    Vector3 target;
    if (!target_heliocentric_position(jd_tdb, target_data, &target) || !finite_vector(target)) {
        return false;
    }
    Vector3 position = vector3_subtract(target, observer_heliocentric_position_au);
    double distance = vector3_norm(position);
    if (!std::isfinite(distance) || distance == 0.0) {
        return false;
    }

    double delay = 0.0;
    if (!solar_shapiro_delay_terms(
            observer_heliocentric_position_au,
            target,
            position,
            schwarzschild_light_time_days,
            &delay,
            0,
            0)) {
        return false;
    }

    double light_time_days = light_time_tau_from_distance(distance, light_time_days_per_au, delay);
    const int iterations = normalized_light_time_max_iterations(max_iterations);
    const double tolerance = normalized_light_time_tolerance_days(tolerance_days);
    for (int i = 0; i < iterations; ++i) {
        const double emission_jd_tdb = jd_tdb - light_time_days;
        if (!std::isfinite(emission_jd_tdb)
            || !target_heliocentric_position(emission_jd_tdb, target_data, &target)
            || !finite_vector(target)) {
            return false;
        }
        position = vector3_subtract(target, observer_heliocentric_position_au);
        distance = vector3_norm(position);
        if (!std::isfinite(distance) || distance == 0.0) {
            return false;
        }
        if (!solar_shapiro_delay_terms(
                observer_heliocentric_position_au,
                target,
                position,
                schwarzschild_light_time_days,
                &delay,
                0,
                0)) {
            return false;
        }
        const double next_light_time_days = light_time_tau_from_distance(distance, light_time_days_per_au, delay);
        if (!std::isfinite(next_light_time_days)) {
            return false;
        }
        if (std::fabs(next_light_time_days - light_time_days) <= tolerance) {
            *out_position_au = position;
            *out_light_time_days = next_light_time_days;
            if (out_retarded_target_heliocentric_position_au) {
                *out_retarded_target_heliocentric_position_au = target;
            }
            return true;
        }
        light_time_days = next_light_time_days;
    }

    return false;
}

bool solve_light_time_shapiro_position_velocity(
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
) noexcept {
    if (!out_position_au || !out_velocity_au_per_day || !out_light_time_days || !out_light_time_rate) {
        return false;
    }
    if (!target_heliocentric_velocity || !finite_vector(observer_heliocentric_velocity_au_per_day)) {
        return false;
    }

    Vector3 position;
    Vector3 target_retarded_position;
    double light_time_days = 0.0;
    if (!solve_light_time_shapiro_iteration(
            jd_tdb,
            observer_heliocentric_position_au,
            target_heliocentric_position,
            target_data,
            light_time_days_per_au,
            schwarzschild_light_time_days,
            max_iterations,
            tolerance_days,
            &position,
            &light_time_days,
            &target_retarded_position)) {
        return false;
    }

    const double emission_jd_tdb = jd_tdb - light_time_days;
    Vector3 target_retarded_velocity;
    if (!target_heliocentric_velocity(emission_jd_tdb, target_data, &target_retarded_velocity)
        || !finite_vector(target_retarded_velocity)) {
        return false;
    }

    Vector3 direction;
    double distance = 0.0;
    if (!unit_vector_with_derivative(position, vector3_subtract(target_retarded_velocity, observer_heliocentric_velocity_au_per_day), &direction, 0, &distance, 0)) {
        return false;
    }
    Vector3 observer_unit;
    Vector3 target_unit;
    if (!unit_vector_with_derivative(observer_heliocentric_position_au, observer_heliocentric_velocity_au_per_day, &observer_unit, 0, 0, 0)
        || !unit_vector_with_derivative(target_retarded_position, target_retarded_velocity, &target_unit, 0, 0, 0)) {
        return false;
    }

    double delay_radius_sum_derivative = 0.0;
    double delay_distance_derivative = 0.0;
    if (!solar_shapiro_delay_terms(
            observer_heliocentric_position_au,
            target_retarded_position,
            position,
            schwarzschild_light_time_days,
            0,
            &delay_radius_sum_derivative,
            &delay_distance_derivative)) {
        return false;
    }

    const double target_speed_along_direction = vector3_dot(direction, target_retarded_velocity);
    const double observer_speed_along_direction = vector3_dot(direction, observer_heliocentric_velocity_au_per_day);
    const double observer_radius_speed = vector3_dot(observer_unit, observer_heliocentric_velocity_au_per_day);
    const double target_radius_speed = vector3_dot(target_unit, target_retarded_velocity);
    double light_time_rate = 0.0;
    if (!light_time_tau_dot_with_shapiro(
            target_speed_along_direction,
            observer_speed_along_direction,
            observer_radius_speed,
            target_radius_speed,
            light_time_days_per_au,
            delay_radius_sum_derivative,
            delay_distance_derivative,
            &light_time_rate)) {
        return false;
    }

    const Vector3 velocity = vector3_subtract(
        vector3_scale(target_retarded_velocity, 1.0 - light_time_rate),
        observer_heliocentric_velocity_au_per_day);
    if (!finite_vector(velocity) || !std::isfinite(light_time_rate) || !std::isfinite(distance)) {
        return false;
    }

    *out_position_au = position;
    *out_velocity_au_per_day = velocity;
    *out_light_time_days = light_time_days;
    *out_light_time_rate = light_time_rate;
    if (out_retarded_target_heliocentric_position_au) {
        *out_retarded_target_heliocentric_position_au = target_retarded_position;
    }
    if (out_retarded_target_heliocentric_velocity_au_per_day) {
        *out_retarded_target_heliocentric_velocity_au_per_day = target_retarded_velocity;
    }
    return true;
}

bool valid_multi_shapiro_inputs(
    LightTimePositionFn target_primary_relative_position,
    int deflector_count,
    const double* deflector_schwarzschild_radius_au,
    ShapiroDeflectorPositionFn deflector_primary_relative_position,
    double light_time_days_per_au
) noexcept {
    if (!valid_light_time_inputs(target_primary_relative_position, light_time_days_per_au)
        || deflector_count <= 0
        || !deflector_schwarzschild_radius_au
        || !deflector_primary_relative_position) {
        return false;
    }
    for (int i = 0; i < deflector_count; ++i) {
        if (!std::isfinite(deflector_schwarzschild_radius_au[i]) || deflector_schwarzschild_radius_au[i] < 0.0) {
            return false;
        }
    }
    return true;
}

bool accumulate_multi_shapiro_delay(
    double jd_tdb,
    const Vector3& observer_primary_relative_position_au,
    const Vector3& target_primary_relative_position_au,
    const Vector3& target_observer_position_au,
    int deflector_count,
    const double* deflector_schwarzschild_radius_au,
    ShapiroDeflectorPositionFn deflector_primary_relative_position,
    const void* deflector_data,
    double light_time_days_per_au,
    double* out_delay_days
) noexcept {
    if (!out_delay_days || deflector_count <= 0 || !deflector_schwarzschild_radius_au || !deflector_primary_relative_position) {
        return false;
    }
    double delay = 0.0;
    for (int i = 0; i < deflector_count; ++i) {
        Vector3 deflector_position;
        if (!deflector_primary_relative_position(jd_tdb, deflector_data, i, &deflector_position)
            || !finite_vector(deflector_position)
            || !std::isfinite(deflector_schwarzschild_radius_au[i])
            || deflector_schwarzschild_radius_au[i] < 0.0) {
            return false;
        }
        double delay_i = 0.0;
        if (!solar_shapiro_delay_terms(
                vector3_subtract(observer_primary_relative_position_au, deflector_position),
                vector3_subtract(target_primary_relative_position_au, deflector_position),
                target_observer_position_au,
                deflector_schwarzschild_radius_au[i] * light_time_days_per_au,
                &delay_i,
                0,
                0)) {
            return false;
        }
        delay += delay_i;
    }
    *out_delay_days = delay;
    return std::isfinite(delay);
}

bool solve_light_time_multi_shapiro_iteration(
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
) noexcept {
    if (!out_position_au || !out_light_time_days) {
        return false;
    }
    if (!valid_multi_shapiro_inputs(
            target_primary_relative_position,
            deflector_count,
            deflector_schwarzschild_radius_au,
            deflector_primary_relative_position,
            light_time_days_per_au)
        || !std::isfinite(jd_tdb)
        || !finite_vector(observer_primary_relative_position_au)) {
        return false;
    }

    Vector3 target;
    if (!target_primary_relative_position(jd_tdb, target_data, &target) || !finite_vector(target)) {
        return false;
    }
    Vector3 position = vector3_subtract(target, observer_primary_relative_position_au);
    double distance = vector3_norm(position);
    if (!std::isfinite(distance) || distance == 0.0) {
        return false;
    }

    double delay = 0.0;
    if (!accumulate_multi_shapiro_delay(
            jd_tdb,
            observer_primary_relative_position_au,
            target,
            position,
            deflector_count,
            deflector_schwarzschild_radius_au,
            deflector_primary_relative_position,
            deflector_data,
            light_time_days_per_au,
            &delay)) {
        return false;
    }

    double light_time_days = light_time_tau_from_distance(distance, light_time_days_per_au, delay);
    const int iterations = normalized_light_time_max_iterations(max_iterations);
    const double tolerance = normalized_light_time_tolerance_days(tolerance_days);
    for (int i = 0; i < iterations; ++i) {
        const double emission_jd_tdb = jd_tdb - light_time_days;
        if (!std::isfinite(emission_jd_tdb)
            || !target_primary_relative_position(emission_jd_tdb, target_data, &target)
            || !finite_vector(target)) {
            return false;
        }
        position = vector3_subtract(target, observer_primary_relative_position_au);
        distance = vector3_norm(position);
        if (!std::isfinite(distance) || distance == 0.0) {
            return false;
        }
        if (!accumulate_multi_shapiro_delay(
                emission_jd_tdb,
                observer_primary_relative_position_au,
                target,
                position,
                deflector_count,
                deflector_schwarzschild_radius_au,
                deflector_primary_relative_position,
                deflector_data,
                light_time_days_per_au,
                &delay)) {
            return false;
        }
        const double next_light_time_days = light_time_tau_from_distance(distance, light_time_days_per_au, delay);
        if (!std::isfinite(next_light_time_days)) {
            return false;
        }
        if (std::fabs(next_light_time_days - light_time_days) <= tolerance) {
            *out_position_au = position;
            *out_light_time_days = next_light_time_days;
            if (out_retarded_target_primary_relative_position_au) {
                *out_retarded_target_primary_relative_position_au = target;
            }
            return true;
        }
        light_time_days = next_light_time_days;
    }

    return false;
}

struct MultiShapiroVelocityTerms {
    double radius_sum_rate_term;
    double target_radius_rate_term;
    double distance_derivative_sum;
};

bool accumulate_multi_shapiro_velocity_terms(
    double jd_tdb,
    const Vector3& observer_primary_relative_position_au,
    const Vector3& observer_primary_relative_velocity_au_per_day,
    const Vector3& target_primary_relative_position_au,
    const Vector3& target_primary_relative_velocity_au_per_day,
    const Vector3& target_observer_position_au,
    int deflector_count,
    const double* deflector_schwarzschild_radius_au,
    ShapiroDeflectorPositionFn deflector_primary_relative_position,
    ShapiroDeflectorVelocityFn deflector_primary_relative_velocity,
    const void* deflector_data,
    double light_time_days_per_au,
    MultiShapiroVelocityTerms* out
) noexcept {
    if (!out || deflector_count <= 0 || !deflector_schwarzschild_radius_au || !deflector_primary_relative_position || !deflector_primary_relative_velocity) {
        return false;
    }
    MultiShapiroVelocityTerms terms;
    terms.radius_sum_rate_term = 0.0;
    terms.target_radius_rate_term = 0.0;
    terms.distance_derivative_sum = 0.0;
    for (int i = 0; i < deflector_count; ++i) {
        Vector3 deflector_position;
        Vector3 deflector_velocity;
        if (!deflector_primary_relative_position(jd_tdb, deflector_data, i, &deflector_position)
            || !deflector_primary_relative_velocity(jd_tdb, deflector_data, i, &deflector_velocity)
            || !finite_vector(deflector_position)
            || !finite_vector(deflector_velocity)) {
            return false;
        }
        const Vector3 observer_position = vector3_subtract(observer_primary_relative_position_au, deflector_position);
        const Vector3 observer_velocity = vector3_subtract(observer_primary_relative_velocity_au_per_day, deflector_velocity);
        const Vector3 target_position = vector3_subtract(target_primary_relative_position_au, deflector_position);
        const Vector3 target_velocity = vector3_subtract(target_primary_relative_velocity_au_per_day, deflector_velocity);
        Vector3 observer_unit;
        Vector3 target_unit;
        if (!unit_vector_with_derivative(observer_position, observer_velocity, &observer_unit, 0, 0, 0)
            || !unit_vector_with_derivative(target_position, target_velocity, &target_unit, 0, 0, 0)) {
            return false;
        }
        double delay_radius_sum_derivative = 0.0;
        double delay_distance_derivative = 0.0;
        if (!solar_shapiro_delay_terms(
                observer_position,
                target_position,
                target_observer_position_au,
                deflector_schwarzschild_radius_au[i] * light_time_days_per_au,
                0,
                &delay_radius_sum_derivative,
                &delay_distance_derivative)) {
            return false;
        }
        const double observer_radius_speed = vector3_dot(observer_unit, observer_velocity);
        const double target_radius_speed = vector3_dot(target_unit, target_velocity);
        terms.radius_sum_rate_term += delay_radius_sum_derivative * (observer_radius_speed + target_radius_speed);
        terms.target_radius_rate_term += delay_radius_sum_derivative * target_radius_speed;
        terms.distance_derivative_sum += delay_distance_derivative;
    }
    if (!std::isfinite(terms.radius_sum_rate_term)
        || !std::isfinite(terms.target_radius_rate_term)
        || !std::isfinite(terms.distance_derivative_sum)) {
        return false;
    }
    *out = terms;
    return true;
}

bool solve_light_time_multi_shapiro_position_velocity(
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
) noexcept {
    if (!out_position_au || !out_velocity_au_per_day || !out_light_time_days || !out_light_time_rate) {
        return false;
    }
    if (!target_primary_relative_velocity
        || !deflector_primary_relative_velocity
        || !finite_vector(observer_primary_relative_velocity_au_per_day)) {
        return false;
    }

    Vector3 position;
    Vector3 target_retarded_position;
    double light_time_days = 0.0;
    if (!solve_light_time_multi_shapiro_iteration(
            jd_tdb,
            observer_primary_relative_position_au,
            target_primary_relative_position,
            target_data,
            deflector_count,
            deflector_schwarzschild_radius_au,
            deflector_primary_relative_position,
            deflector_data,
            light_time_days_per_au,
            max_iterations,
            tolerance_days,
            &position,
            &light_time_days,
            &target_retarded_position)) {
        return false;
    }

    const double emission_jd_tdb = jd_tdb - light_time_days;
    Vector3 target_retarded_velocity;
    if (!target_primary_relative_velocity(emission_jd_tdb, target_data, &target_retarded_velocity)
        || !finite_vector(target_retarded_velocity)) {
        return false;
    }

    Vector3 direction;
    double distance = 0.0;
    if (!unit_vector_with_derivative(
            position,
            vector3_subtract(target_retarded_velocity, observer_primary_relative_velocity_au_per_day),
            &direction,
            0,
            &distance,
            0)) {
        return false;
    }

    MultiShapiroVelocityTerms terms;
    if (!accumulate_multi_shapiro_velocity_terms(
            emission_jd_tdb,
            observer_primary_relative_position_au,
            observer_primary_relative_velocity_au_per_day,
            target_retarded_position,
            target_retarded_velocity,
            position,
            deflector_count,
            deflector_schwarzschild_radius_au,
            deflector_primary_relative_position,
            deflector_primary_relative_velocity,
            deflector_data,
            light_time_days_per_au,
            &terms)) {
        return false;
    }

    const double target_speed_along_direction = vector3_dot(direction, target_retarded_velocity);
    const double observer_speed_along_direction = vector3_dot(direction, observer_primary_relative_velocity_au_per_day);
    const double distance_speed_without_tau = target_speed_along_direction - observer_speed_along_direction;
    const double numerator = light_time_days_per_au * distance_speed_without_tau
        + terms.distance_derivative_sum * distance_speed_without_tau
        + terms.radius_sum_rate_term;
    const double denominator = 1.0
        + light_time_days_per_au * target_speed_along_direction
        + terms.distance_derivative_sum * target_speed_along_direction
        + terms.target_radius_rate_term;
    if (std::fabs(denominator) <= LIGHT_TIME_DENOMINATOR_EPSILON) {
        return false;
    }
    const double light_time_rate = numerator / denominator;

    const Vector3 velocity = vector3_subtract(
        vector3_scale(target_retarded_velocity, 1.0 - light_time_rate),
        observer_primary_relative_velocity_au_per_day);
    if (!finite_vector(velocity) || !std::isfinite(light_time_rate) || !std::isfinite(distance)) {
        return false;
    }

    *out_position_au = position;
    *out_velocity_au_per_day = velocity;
    *out_light_time_days = light_time_days;
    *out_light_time_rate = light_time_rate;
    if (out_retarded_target_primary_relative_position_au) {
        *out_retarded_target_primary_relative_position_au = target_retarded_position;
    }
    if (out_retarded_target_primary_relative_velocity_au_per_day) {
        *out_retarded_target_primary_relative_velocity_au_per_day = target_retarded_velocity;
    }
    return true;
}

struct MultiShapiroAccelerationTerms {
    double distance_derivative_sum;
    double target_radius_rate_term;
    double radius_acceleration_term;
    double curvature_term;
};

bool accumulate_multi_shapiro_acceleration_terms(
    double jd_tdb,
    double alpha,
    const Vector3& observer_primary_relative_position_au,
    const Vector3& observer_primary_relative_velocity_au_per_day,
    const Vector3& observer_primary_relative_acceleration_au_per_day2,
    const Vector3& target_primary_relative_position_au,
    const Vector3& target_primary_relative_velocity_au_per_day,
    const Vector3& target_primary_relative_acceleration_au_per_day2,
    const Vector3& target_observer_position_au,
    double distance_rate_au_per_day,
    int deflector_count,
    const double* deflector_schwarzschild_radius_au,
    ShapiroDeflectorPositionFn deflector_primary_relative_position,
    ShapiroDeflectorVelocityFn deflector_primary_relative_velocity,
    ShapiroDeflectorAccelerationFn deflector_primary_relative_acceleration,
    const void* deflector_data,
    double light_time_days_per_au,
    MultiShapiroAccelerationTerms* out
) noexcept {
    if (!out
        || deflector_count <= 0
        || !deflector_schwarzschild_radius_au
        || !deflector_primary_relative_position
        || !deflector_primary_relative_velocity
        || !deflector_primary_relative_acceleration) {
        return false;
    }
    MultiShapiroAccelerationTerms terms;
    terms.distance_derivative_sum = 0.0;
    terms.target_radius_rate_term = 0.0;
    terms.radius_acceleration_term = 0.0;
    terms.curvature_term = 0.0;
    for (int i = 0; i < deflector_count; ++i) {
        Vector3 deflector_position;
        Vector3 deflector_velocity;
        Vector3 deflector_acceleration;
        if (!deflector_primary_relative_position(jd_tdb, deflector_data, i, &deflector_position)
            || !deflector_primary_relative_velocity(jd_tdb, deflector_data, i, &deflector_velocity)
            || !deflector_primary_relative_acceleration(jd_tdb, deflector_data, i, &deflector_acceleration)
            || !finite_vector(deflector_position)
            || !finite_vector(deflector_velocity)
            || !finite_vector(deflector_acceleration)) {
            return false;
        }
        const Vector3 observer_position = vector3_subtract(observer_primary_relative_position_au, deflector_position);
        const Vector3 observer_velocity = vector3_subtract(observer_primary_relative_velocity_au_per_day, deflector_velocity);
        const Vector3 observer_acceleration = vector3_subtract(observer_primary_relative_acceleration_au_per_day2, deflector_acceleration);
        const Vector3 target_position = vector3_subtract(target_primary_relative_position_au, deflector_position);
        const Vector3 target_velocity = vector3_subtract(target_primary_relative_velocity_au_per_day, deflector_velocity);
        const Vector3 target_acceleration = vector3_subtract(target_primary_relative_acceleration_au_per_day2, deflector_acceleration);

        Vector3 observer_unit;
        double observer_radius = 0.0;
        double observer_radius_speed = 0.0;
        if (!unit_vector_with_derivative(observer_position, observer_velocity, &observer_unit, 0, &observer_radius, &observer_radius_speed)) {
            return false;
        }
        Vector3 target_unit;
        double target_radius = 0.0;
        double target_radius_speed = 0.0;
        if (!unit_vector_with_derivative(target_position, target_velocity, &target_unit, 0, &target_radius, &target_radius_speed)) {
            return false;
        }
        const double radius_sum = observer_radius + target_radius;
        ShapiroPartials partials;
        if (!shapiro_delay_partials(
                radius_sum,
                vector3_norm(target_observer_position_au),
                deflector_schwarzschild_radius_au[i] * light_time_days_per_au,
                &partials)) {
            return false;
        }
        const double observer_radius_acceleration = (
            vector3_dot(observer_velocity, observer_velocity) - observer_radius_speed * observer_radius_speed) / observer_radius
            + vector3_dot(observer_unit, observer_acceleration);
        const double target_radius_acceleration_by_emission_time = (
            vector3_dot(target_velocity, target_velocity) - target_radius_speed * target_radius_speed) / target_radius
            + vector3_dot(target_unit, target_acceleration);
        const double radius_sum_rate = observer_radius_speed + target_radius_speed * alpha;
        const double radius_sum_acceleration_without_tau_ddot = observer_radius_acceleration
            + target_radius_acceleration_by_emission_time * alpha * alpha;
        terms.distance_derivative_sum += partials.distance_derivative;
        terms.target_radius_rate_term += partials.radius_sum_derivative * target_radius_speed;
        terms.radius_acceleration_term += partials.radius_sum_derivative * radius_sum_acceleration_without_tau_ddot;
        terms.curvature_term += partials.radius_sum_second_derivative * radius_sum_rate * radius_sum_rate
            + 2.0 * partials.radius_sum_distance_derivative * radius_sum_rate * distance_rate_au_per_day
            + partials.distance_second_derivative * distance_rate_au_per_day * distance_rate_au_per_day;
    }
    if (!std::isfinite(terms.distance_derivative_sum)
        || !std::isfinite(terms.target_radius_rate_term)
        || !std::isfinite(terms.radius_acceleration_term)
        || !std::isfinite(terms.curvature_term)) {
        return false;
    }
    *out = terms;
    return true;
}

}  // namespace

bool unit_vector_with_derivative(
    const Vector3& vector,
    const Vector3& vector_dot,
    Vector3* unit,
    Vector3* unit_dot,
    double* norm_out,
    double* norm_dot_out
) noexcept {
    const double norm = vector3_norm(vector);
    if (norm == 0.0) {
        return false;
    }

    const Vector3 direction = vector3_scale(vector, 1.0 / norm);
    const double norm_dot = vector3_dot(direction, vector_dot);
    if (unit) {
        *unit = direction;
    }
    if (unit_dot) {
        *unit_dot = vector3_scale(
            vector3_subtract(vector_dot, vector3_scale(direction, norm_dot)),
            1.0 / norm);
    }
    if (norm_out) {
        *norm_out = norm;
    }
    if (norm_dot_out) {
        *norm_dot_out = norm_dot;
    }
    return true;
}

bool solar_shapiro_delay_terms(
    const Vector3& observer_heliocentric_position_au,
    const Vector3& target_heliocentric_position_au,
    const Vector3& target_geocentric_position_au,
    double schwarzschild_light_time_days,
    double* delay_days,
    double* delay_derivative_by_radius_sum,
    double* delay_derivative_by_distance
) noexcept {
    const double observer_radius = vector3_norm(observer_heliocentric_position_au);
    const double target_radius = vector3_norm(target_heliocentric_position_au);
    const double distance = vector3_norm(target_geocentric_position_au);
    if (observer_radius == 0.0 || target_radius == 0.0 || distance == 0.0) {
        return false;
    }

    const double radius_sum = observer_radius + target_radius;
    const double numerator = radius_sum + distance;
    const double denominator = radius_sum - distance;
    const double radius_difference = radius_sum * radius_sum - distance * distance;
    if (numerator <= 0.0 || denominator <= 0.0 || radius_difference == 0.0) {
        return false;
    }

    if (delay_days) {
        *delay_days = schwarzschild_light_time_days * std::log(numerator / denominator);
    }
    if (delay_derivative_by_radius_sum) {
        *delay_derivative_by_radius_sum = -2.0 * schwarzschild_light_time_days * distance / radius_difference;
    }
    if (delay_derivative_by_distance) {
        *delay_derivative_by_distance = 2.0 * schwarzschild_light_time_days * radius_sum / radius_difference;
    }
    return true;
}

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
) noexcept {
    return solve_light_time_iteration(
        jd_tdb,
        observer_position_au,
        target_position,
        target_data,
        light_time_days_per_au,
        max_iterations,
        tolerance_days,
        out_position_au,
        out_light_time_days,
        out_retarded_target_position_au);
}

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
) noexcept {
    return solve_light_time_position_velocity(
        jd_tdb,
        observer_position_au,
        observer_velocity_au_per_day,
        target_position,
        target_velocity,
        target_data,
        light_time_days_per_au,
        max_iterations,
        tolerance_days,
        out_position_au,
        out_velocity_au_per_day,
        out_light_time_days,
        out_light_time_rate,
        out_retarded_target_position_au,
        out_retarded_target_velocity_au_per_day);
}

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
) noexcept {
    if (!out_position_au
        || !out_velocity_au_per_day
        || !out_acceleration_au_per_day2
        || !out_light_time_days
        || !out_light_time_rate
        || !out_light_time_acceleration) {
        return false;
    }
    if (!target_acceleration || !finite_vector(observer_acceleration_au_per_day2)) {
        return false;
    }

    Vector3 position;
    Vector3 velocity;
    Vector3 target_retarded_position;
    Vector3 target_retarded_velocity;
    double light_time_days = 0.0;
    double light_time_rate = 0.0;
    if (!solve_light_time_position_velocity(
            jd_tdb,
            observer_position_au,
            observer_velocity_au_per_day,
            target_position,
            target_velocity,
            target_data,
            light_time_days_per_au,
            max_iterations,
            tolerance_days,
            &position,
            &velocity,
            &light_time_days,
            &light_time_rate,
            &target_retarded_position,
            &target_retarded_velocity)) {
        return false;
    }

    const double emission_jd_tdb = jd_tdb - light_time_days;
    Vector3 target_retarded_acceleration;
    if (!target_acceleration(emission_jd_tdb, target_data, &target_retarded_acceleration)
        || !finite_vector(target_retarded_acceleration)) {
        return false;
    }

    const double distance = vector3_norm(position);
    if (!std::isfinite(distance) || distance == 0.0) {
        return false;
    }
    const Vector3 direction = vector3_scale(position, 1.0 / distance);
    const double target_speed_along_direction = vector3_dot(direction, target_retarded_velocity);
    const double denominator = 1.0 + light_time_days_per_au * target_speed_along_direction;
    if (std::fabs(denominator) <= LIGHT_TIME_DENOMINATOR_EPSILON) {
        return false;
    }

    const double distance_rate = vector3_dot(direction, velocity);
    const double transverse_acceleration = (
        vector3_dot(velocity, velocity) - distance_rate * distance_rate) / distance;
    const Vector3 target_acceleration_with_emission_rate = vector3_scale(
        target_retarded_acceleration,
        (1.0 - light_time_rate) * (1.0 - light_time_rate));
    const double light_time_acceleration = light_time_days_per_au * (
        vector3_dot(
            direction,
            vector3_subtract(target_acceleration_with_emission_rate, observer_acceleration_au_per_day2))
        + transverse_acceleration) / denominator;
    if (!std::isfinite(light_time_acceleration)) {
        return false;
    }

    const Vector3 acceleration = vector3_subtract(
        vector3_subtract(
            target_acceleration_with_emission_rate,
            vector3_scale(target_retarded_velocity, light_time_acceleration)),
        observer_acceleration_au_per_day2);
    if (!finite_vector(acceleration)) {
        return false;
    }

    *out_position_au = position;
    *out_velocity_au_per_day = velocity;
    *out_acceleration_au_per_day2 = acceleration;
    *out_light_time_days = light_time_days;
    *out_light_time_rate = light_time_rate;
    *out_light_time_acceleration = light_time_acceleration;
    if (out_retarded_target_position_au) {
        *out_retarded_target_position_au = target_retarded_position;
    }
    if (out_retarded_target_velocity_au_per_day) {
        *out_retarded_target_velocity_au_per_day = target_retarded_velocity;
    }
    if (out_retarded_target_acceleration_au_per_day2) {
        *out_retarded_target_acceleration_au_per_day2 = target_retarded_acceleration;
    }
    return true;
}

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
) noexcept {
    return solve_light_time_shapiro_iteration(
        jd_tdb,
        observer_heliocentric_position_au,
        target_heliocentric_position,
        target_data,
        light_time_days_per_au,
        schwarzschild_light_time_days,
        max_iterations,
        tolerance_days,
        out_position_au,
        out_light_time_days,
        out_retarded_target_heliocentric_position_au);
}

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
) noexcept {
    return solve_light_time_shapiro_position_velocity(
        jd_tdb,
        observer_heliocentric_position_au,
        observer_heliocentric_velocity_au_per_day,
        target_heliocentric_position,
        target_heliocentric_velocity,
        target_data,
        light_time_days_per_au,
        schwarzschild_light_time_days,
        max_iterations,
        tolerance_days,
        out_position_au,
        out_velocity_au_per_day,
        out_light_time_days,
        out_light_time_rate,
        out_retarded_target_heliocentric_position_au,
        out_retarded_target_heliocentric_velocity_au_per_day);
}

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
) noexcept {
    if (!out_position_au
        || !out_velocity_au_per_day
        || !out_acceleration_au_per_day2
        || !out_light_time_days
        || !out_light_time_rate
        || !out_light_time_acceleration) {
        return false;
    }
    if (!target_heliocentric_acceleration || !finite_vector(observer_heliocentric_acceleration_au_per_day2)) {
        return false;
    }

    Vector3 position;
    Vector3 velocity;
    Vector3 target_retarded_position;
    Vector3 target_retarded_velocity;
    double light_time_days = 0.0;
    double light_time_rate = 0.0;
    if (!solve_light_time_shapiro_position_velocity(
            jd_tdb,
            observer_heliocentric_position_au,
            observer_heliocentric_velocity_au_per_day,
            target_heliocentric_position,
            target_heliocentric_velocity,
            target_data,
            light_time_days_per_au,
            schwarzschild_light_time_days,
            max_iterations,
            tolerance_days,
            &position,
            &velocity,
            &light_time_days,
            &light_time_rate,
            &target_retarded_position,
            &target_retarded_velocity)) {
        return false;
    }

    const double emission_jd_tdb = jd_tdb - light_time_days;
    Vector3 target_retarded_acceleration;
    if (!target_heliocentric_acceleration(emission_jd_tdb, target_data, &target_retarded_acceleration)
        || !finite_vector(target_retarded_acceleration)) {
        return false;
    }

    Vector3 direction;
    double distance = 0.0;
    if (!unit_vector_with_derivative(position, velocity, &direction, 0, &distance, 0)) {
        return false;
    }

    Vector3 observer_unit;
    double observer_radius = 0.0;
    double observer_radius_speed = 0.0;
    if (!unit_vector_with_derivative(
            observer_heliocentric_position_au,
            observer_heliocentric_velocity_au_per_day,
            &observer_unit,
            0,
            &observer_radius,
            &observer_radius_speed)) {
        return false;
    }

    Vector3 target_unit;
    double target_radius = 0.0;
    double target_radius_speed = 0.0;
    if (!unit_vector_with_derivative(
            target_retarded_position,
            target_retarded_velocity,
            &target_unit,
            0,
            &target_radius,
            &target_radius_speed)) {
        return false;
    }

    const double radius_sum = observer_radius + target_radius;
    ShapiroPartials partials;
    if (!shapiro_delay_partials(radius_sum, distance, schwarzschild_light_time_days, &partials)) {
        return false;
    }

    const double alpha = 1.0 - light_time_rate;
    const double target_speed_along_direction = vector3_dot(direction, target_retarded_velocity);
    const double denominator = 1.0
        + (light_time_days_per_au + partials.distance_derivative) * target_speed_along_direction
        + partials.radius_sum_derivative * target_radius_speed;
    if (std::fabs(denominator) <= LIGHT_TIME_DENOMINATOR_EPSILON) {
        return false;
    }

    const Vector3 target_acceleration_with_emission_rate = vector3_scale(
        target_retarded_acceleration,
        alpha * alpha);
    const double distance_rate = vector3_dot(direction, velocity);
    const double distance_acceleration_without_tau_ddot = (
        vector3_dot(velocity, velocity) - distance_rate * distance_rate) / distance
        + vector3_dot(
            direction,
            vector3_subtract(target_acceleration_with_emission_rate, observer_heliocentric_acceleration_au_per_day2));

    const double observer_radius_acceleration = (
        vector3_dot(observer_heliocentric_velocity_au_per_day, observer_heliocentric_velocity_au_per_day)
        - observer_radius_speed * observer_radius_speed) / observer_radius
        + vector3_dot(observer_unit, observer_heliocentric_acceleration_au_per_day2);
    const double target_radius_acceleration_by_emission_time = (
        vector3_dot(target_retarded_velocity, target_retarded_velocity)
        - target_radius_speed * target_radius_speed) / target_radius
        + vector3_dot(target_unit, target_retarded_acceleration);
    const double radius_sum_rate = observer_radius_speed + target_radius_speed * alpha;
    const double radius_sum_acceleration_without_tau_ddot = observer_radius_acceleration
        + target_radius_acceleration_by_emission_time * alpha * alpha;
    const double curvature_terms = partials.radius_sum_second_derivative * radius_sum_rate * radius_sum_rate
        + 2.0 * partials.radius_sum_distance_derivative * radius_sum_rate * distance_rate
        + partials.distance_second_derivative * distance_rate * distance_rate;
    const double light_time_acceleration = (
        (light_time_days_per_au + partials.distance_derivative) * distance_acceleration_without_tau_ddot
        + partials.radius_sum_derivative * radius_sum_acceleration_without_tau_ddot
        + curvature_terms) / denominator;
    if (!std::isfinite(light_time_acceleration)) {
        return false;
    }

    const Vector3 acceleration = vector3_subtract(
        vector3_subtract(
            target_acceleration_with_emission_rate,
            vector3_scale(target_retarded_velocity, light_time_acceleration)),
        observer_heliocentric_acceleration_au_per_day2);
    if (!finite_vector(acceleration)) {
        return false;
    }

    *out_position_au = position;
    *out_velocity_au_per_day = velocity;
    *out_acceleration_au_per_day2 = acceleration;
    *out_light_time_days = light_time_days;
    *out_light_time_rate = light_time_rate;
    *out_light_time_acceleration = light_time_acceleration;
    if (out_retarded_target_heliocentric_position_au) {
        *out_retarded_target_heliocentric_position_au = target_retarded_position;
    }
    if (out_retarded_target_heliocentric_velocity_au_per_day) {
        *out_retarded_target_heliocentric_velocity_au_per_day = target_retarded_velocity;
    }
    if (out_retarded_target_heliocentric_acceleration_au_per_day2) {
        *out_retarded_target_heliocentric_acceleration_au_per_day2 = target_retarded_acceleration;
    }
    return true;
}

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
) noexcept {
    return solve_light_time_multi_shapiro_iteration(
        jd_tdb,
        observer_primary_relative_position_au,
        target_primary_relative_position,
        target_data,
        deflector_count,
        deflector_schwarzschild_radius_au,
        deflector_primary_relative_position,
        deflector_data,
        light_time_days_per_au,
        max_iterations,
        tolerance_days,
        out_position_au,
        out_light_time_days,
        out_retarded_target_primary_relative_position_au);
}

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
) noexcept {
    return solve_light_time_multi_shapiro_position_velocity(
        jd_tdb,
        observer_primary_relative_position_au,
        observer_primary_relative_velocity_au_per_day,
        target_primary_relative_position,
        target_primary_relative_velocity,
        target_data,
        deflector_count,
        deflector_schwarzschild_radius_au,
        deflector_primary_relative_position,
        deflector_primary_relative_velocity,
        deflector_data,
        light_time_days_per_au,
        max_iterations,
        tolerance_days,
        out_position_au,
        out_velocity_au_per_day,
        out_light_time_days,
        out_light_time_rate,
        out_retarded_target_primary_relative_position_au,
        out_retarded_target_primary_relative_velocity_au_per_day);
}

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
) noexcept {
    if (!out_position_au
        || !out_velocity_au_per_day
        || !out_acceleration_au_per_day2
        || !out_light_time_days
        || !out_light_time_rate
        || !out_light_time_acceleration) {
        return false;
    }
    if (!target_primary_relative_acceleration
        || !deflector_primary_relative_acceleration
        || !finite_vector(observer_primary_relative_acceleration_au_per_day2)) {
        return false;
    }

    Vector3 position;
    Vector3 velocity;
    Vector3 target_retarded_position;
    Vector3 target_retarded_velocity;
    double light_time_days = 0.0;
    double light_time_rate = 0.0;
    if (!solve_light_time_multi_shapiro_position_velocity(
            jd_tdb,
            observer_primary_relative_position_au,
            observer_primary_relative_velocity_au_per_day,
            target_primary_relative_position,
            target_primary_relative_velocity,
            target_data,
            deflector_count,
            deflector_schwarzschild_radius_au,
            deflector_primary_relative_position,
            deflector_primary_relative_velocity,
            deflector_data,
            light_time_days_per_au,
            max_iterations,
            tolerance_days,
            &position,
            &velocity,
            &light_time_days,
            &light_time_rate,
            &target_retarded_position,
            &target_retarded_velocity)) {
        return false;
    }

    const double emission_jd_tdb = jd_tdb - light_time_days;
    Vector3 target_retarded_acceleration;
    if (!target_primary_relative_acceleration(emission_jd_tdb, target_data, &target_retarded_acceleration)
        || !finite_vector(target_retarded_acceleration)) {
        return false;
    }

    Vector3 direction;
    double distance = 0.0;
    if (!unit_vector_with_derivative(position, velocity, &direction, 0, &distance, 0)) {
        return false;
    }
    const double alpha = 1.0 - light_time_rate;
    const double target_speed_along_direction = vector3_dot(direction, target_retarded_velocity);
    const double distance_rate = vector3_dot(direction, velocity);
    const Vector3 target_acceleration_with_emission_rate = vector3_scale(target_retarded_acceleration, alpha * alpha);
    const double distance_acceleration_without_tau_ddot = (
        vector3_dot(velocity, velocity) - distance_rate * distance_rate) / distance
        + vector3_dot(
            direction,
            vector3_subtract(target_acceleration_with_emission_rate, observer_primary_relative_acceleration_au_per_day2));

    MultiShapiroAccelerationTerms terms;
    if (!accumulate_multi_shapiro_acceleration_terms(
            emission_jd_tdb,
            alpha,
            observer_primary_relative_position_au,
            observer_primary_relative_velocity_au_per_day,
            observer_primary_relative_acceleration_au_per_day2,
            target_retarded_position,
            target_retarded_velocity,
            target_retarded_acceleration,
            position,
            distance_rate,
            deflector_count,
            deflector_schwarzschild_radius_au,
            deflector_primary_relative_position,
            deflector_primary_relative_velocity,
            deflector_primary_relative_acceleration,
            deflector_data,
            light_time_days_per_au,
            &terms)) {
        return false;
    }

    const double denominator = 1.0
        + (light_time_days_per_au + terms.distance_derivative_sum) * target_speed_along_direction
        + terms.target_radius_rate_term;
    if (std::fabs(denominator) <= LIGHT_TIME_DENOMINATOR_EPSILON) {
        return false;
    }
    const double light_time_acceleration = (
        (light_time_days_per_au + terms.distance_derivative_sum) * distance_acceleration_without_tau_ddot
        + terms.radius_acceleration_term
        + terms.curvature_term) / denominator;
    if (!std::isfinite(light_time_acceleration)) {
        return false;
    }

    const Vector3 acceleration = vector3_subtract(
        vector3_subtract(
            target_acceleration_with_emission_rate,
            vector3_scale(target_retarded_velocity, light_time_acceleration)),
        observer_primary_relative_acceleration_au_per_day2);
    if (!finite_vector(acceleration)) {
        return false;
    }

    *out_position_au = position;
    *out_velocity_au_per_day = velocity;
    *out_acceleration_au_per_day2 = acceleration;
    *out_light_time_days = light_time_days;
    *out_light_time_rate = light_time_rate;
    *out_light_time_acceleration = light_time_acceleration;
    if (out_retarded_target_primary_relative_position_au) {
        *out_retarded_target_primary_relative_position_au = target_retarded_position;
    }
    if (out_retarded_target_primary_relative_velocity_au_per_day) {
        *out_retarded_target_primary_relative_velocity_au_per_day = target_retarded_velocity;
    }
    if (out_retarded_target_primary_relative_acceleration_au_per_day2) {
        *out_retarded_target_primary_relative_acceleration_au_per_day2 = target_retarded_acceleration;
    }
    return true;
}

bool apply_observer_velocity_aberration(
    const Vector3& source_topocentric_position_au,
    const Vector3& source_topocentric_velocity_au_per_day,
    const Vector3& observer_velocity_au_per_day,
    const Vector3& observer_acceleration_au_per_day2,
    double light_time_days_per_au,
    Vector3* out_position_au,
    Vector3* out_velocity_au_per_day
) noexcept {
    if (!out_position_au || !out_velocity_au_per_day) {
        return false;
    }

    const double distance = vector3_norm(source_topocentric_position_au);
    if (distance == 0.0) {
        return false;
    }

    const Vector3 u = vector3_scale(source_topocentric_position_au, 1.0 / distance);
    const double distance_speed = vector3_dot(u, source_topocentric_velocity_au_per_day);
    const Vector3 u_dot = vector3_scale(
        vector3_subtract(source_topocentric_velocity_au_per_day, vector3_scale(u, distance_speed)),
        1.0 / distance);

    const Vector3 beta = vector3_scale(observer_velocity_au_per_day, light_time_days_per_au);
    const Vector3 beta_dot = vector3_scale(observer_acceleration_au_per_day2, light_time_days_per_au);
    const double beta2 = vector3_dot(beta, beta);
    if (beta2 >= 1.0) {
        return false;
    }

    const double inv_gamma = std::sqrt(1.0 - beta2);
    if (inv_gamma == 0.0) {
        return false;
    }
    const double inv_gamma_dot = -vector3_dot(beta, beta_dot) / inv_gamma;
    const double u_dot_beta = vector3_dot(u, beta);
    const double u_dot_beta_dot = vector3_dot(u_dot, beta) + vector3_dot(u, beta_dot);
    const double q = 1.0 + inv_gamma;
    if (q == 0.0) {
        return false;
    }

    const double w1 = 1.0 + u_dot_beta / q;
    const double w1_dot = (u_dot_beta_dot * q - u_dot_beta * inv_gamma_dot) / (q * q);
    const Vector3 aberrated = vector3_add(vector3_scale(u, inv_gamma), vector3_scale(beta, w1));
    const Vector3 aberrated_dot = vector3_add(
        vector3_add(vector3_scale(u_dot, inv_gamma), vector3_scale(u, inv_gamma_dot)),
        vector3_add(vector3_scale(beta_dot, w1), vector3_scale(beta, w1_dot)));

    Vector3 direction;
    Vector3 direction_dot;
    if (!unit_vector_with_derivative(aberrated, aberrated_dot, &direction, &direction_dot, 0, 0)) {
        return false;
    }

    *out_position_au = vector3_scale(direction, distance);
    *out_velocity_au_per_day = vector3_add(
        vector3_scale(direction, distance_speed),
        vector3_scale(direction_dot, distance));
    return true;
}

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
) noexcept {
    if (!out_position_au || !out_velocity_au_per_day || !out_acceleration_au_per_day2) {
        return false;
    }

    Vector3 previous_position;
    Vector3 previous_velocity;
    Vector3 next_position;
    Vector3 next_velocity;
    const double h = CORRECTION_ACCELERATION_STEP_DAYS;
    if (!apply_observer_velocity_aberration(
            source_topocentric_position_au,
            source_topocentric_velocity_au_per_day,
            observer_velocity_au_per_day,
            observer_acceleration_au_per_day2,
            light_time_days_per_au,
            out_position_au,
            out_velocity_au_per_day)
        || !apply_observer_velocity_aberration(
            constant_acceleration_position_at(
                source_topocentric_position_au,
                source_topocentric_velocity_au_per_day,
                source_topocentric_acceleration_au_per_day2,
                -h),
            constant_acceleration_velocity_at(
                source_topocentric_velocity_au_per_day,
                source_topocentric_acceleration_au_per_day2,
                -h),
            constant_acceleration_velocity_at(
                observer_velocity_au_per_day,
                observer_acceleration_au_per_day2,
                -h),
            observer_acceleration_au_per_day2,
            light_time_days_per_au,
            &previous_position,
            &previous_velocity)
        || !apply_observer_velocity_aberration(
            constant_acceleration_position_at(
                source_topocentric_position_au,
                source_topocentric_velocity_au_per_day,
                source_topocentric_acceleration_au_per_day2,
                h),
            constant_acceleration_velocity_at(
                source_topocentric_velocity_au_per_day,
                source_topocentric_acceleration_au_per_day2,
                h),
            constant_acceleration_velocity_at(
                observer_velocity_au_per_day,
                observer_acceleration_au_per_day2,
                h),
            observer_acceleration_au_per_day2,
            light_time_days_per_au,
            &next_position,
            &next_velocity)) {
        return false;
    }

    (void)previous_position;
    (void)next_position;
    *out_acceleration_au_per_day2 = vector3_scale(
        vector3_subtract(next_velocity, previous_velocity),
        1.0 / (2.0 * h));
    return finite_vector(*out_acceleration_au_per_day2);
}

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
) noexcept {
    if (!out_position_au || !out_velocity_au_per_day) {
        return false;
    }

    const double distance = vector3_norm(source_geocentric_position_au);
    if (distance == 0.0) {
        return false;
    }

    const Vector3 u = vector3_scale(source_geocentric_position_au, 1.0 / distance);
    const double distance_speed = vector3_dot(u, source_geocentric_velocity_au_per_day);
    const Vector3 u_dot = vector3_scale(
        vector3_subtract(source_geocentric_velocity_au_per_day, vector3_scale(u, distance_speed)),
        1.0 / distance);

    double observer_distance = 0.0;
    double observer_distance_dot = 0.0;
    if (!unit_vector_with_derivative(
            observer_heliocentric_position_au,
            observer_heliocentric_velocity_au_per_day,
            0,
            0,
            &observer_distance,
            &observer_distance_dot)) {
        return false;
    }

    const Vector3 beta = vector3_scale(observer_barycentric_velocity_au_per_day, light_time_days_per_au);
    const Vector3 beta_dot = vector3_scale(observer_barycentric_acceleration_au_per_day2, light_time_days_per_au);
    const double beta2 = vector3_dot(beta, beta);
    if (beta2 >= 1.0) {
        return false;
    }

    const double inv_gamma = std::sqrt(1.0 - beta2);
    if (inv_gamma == 0.0) {
        return false;
    }
    const double inv_gamma_dot = -vector3_dot(beta, beta_dot) / inv_gamma;
    const double u_dot_beta = vector3_dot(u, beta);
    const double u_dot_beta_dot = vector3_dot(u_dot, beta) + vector3_dot(u, beta_dot);
    const double q = 1.0 + inv_gamma;
    if (q == 0.0 || observer_distance == 0.0) {
        return false;
    }
    const double w1 = 1.0 + u_dot_beta / q;
    const double w1_dot = (u_dot_beta_dot * q - u_dot_beta * inv_gamma_dot) / (q * q);
    const double w2 = solar_schwarzschild_radius_au / observer_distance;
    const double w2_dot = -w2 * observer_distance_dot / observer_distance;
    const Vector3 potential_vector = vector3_subtract(beta, vector3_scale(u, u_dot_beta));
    const Vector3 potential_vector_dot = vector3_subtract(
        beta_dot,
        vector3_add(vector3_scale(u_dot, u_dot_beta), vector3_scale(u, u_dot_beta_dot)));

    const Vector3 aberrated = vector3_add(
        vector3_add(vector3_scale(u, inv_gamma), vector3_scale(beta, w1)),
        vector3_scale(potential_vector, w2));
    const Vector3 aberrated_dot = vector3_add(
        vector3_add(
            vector3_add(vector3_scale(u_dot, inv_gamma), vector3_scale(u, inv_gamma_dot)),
            vector3_add(vector3_scale(beta_dot, w1), vector3_scale(beta, w1_dot))),
        vector3_add(vector3_scale(potential_vector_dot, w2), vector3_scale(potential_vector, w2_dot)));

    Vector3 direction;
    Vector3 direction_dot;
    if (!unit_vector_with_derivative(aberrated, aberrated_dot, &direction, &direction_dot, 0, 0)) {
        return false;
    }

    *out_position_au = vector3_scale(direction, distance);
    *out_velocity_au_per_day = vector3_add(
        vector3_scale(direction, distance_speed),
        vector3_scale(direction_dot, distance));
    return true;
}

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
) noexcept {
    if (!out_position_au || !out_velocity_au_per_day || !out_acceleration_au_per_day2) {
        return false;
    }

    Vector3 previous_position;
    Vector3 previous_velocity;
    Vector3 next_position;
    Vector3 next_velocity;
    const double h = CORRECTION_ACCELERATION_STEP_DAYS;
    if (!apply_annual_aberration(
            source_geocentric_position_au,
            source_geocentric_velocity_au_per_day,
            observer_heliocentric_position_au,
            observer_heliocentric_velocity_au_per_day,
            observer_barycentric_velocity_au_per_day,
            observer_barycentric_acceleration_au_per_day2,
            light_time_days_per_au,
            solar_schwarzschild_radius_au,
            out_position_au,
            out_velocity_au_per_day)
        || !apply_annual_aberration(
            constant_acceleration_position_at(
                source_geocentric_position_au,
                source_geocentric_velocity_au_per_day,
                source_geocentric_acceleration_au_per_day2,
                -h),
            constant_acceleration_velocity_at(
                source_geocentric_velocity_au_per_day,
                source_geocentric_acceleration_au_per_day2,
                -h),
            constant_acceleration_position_at(
                observer_heliocentric_position_au,
                observer_heliocentric_velocity_au_per_day,
                observer_heliocentric_acceleration_au_per_day2,
                -h),
            constant_acceleration_velocity_at(
                observer_heliocentric_velocity_au_per_day,
                observer_heliocentric_acceleration_au_per_day2,
                -h),
            constant_acceleration_velocity_at(
                observer_barycentric_velocity_au_per_day,
                observer_barycentric_acceleration_au_per_day2,
                -h),
            observer_barycentric_acceleration_au_per_day2,
            light_time_days_per_au,
            solar_schwarzschild_radius_au,
            &previous_position,
            &previous_velocity)
        || !apply_annual_aberration(
            constant_acceleration_position_at(
                source_geocentric_position_au,
                source_geocentric_velocity_au_per_day,
                source_geocentric_acceleration_au_per_day2,
                h),
            constant_acceleration_velocity_at(
                source_geocentric_velocity_au_per_day,
                source_geocentric_acceleration_au_per_day2,
                h),
            constant_acceleration_position_at(
                observer_heliocentric_position_au,
                observer_heliocentric_velocity_au_per_day,
                observer_heliocentric_acceleration_au_per_day2,
                h),
            constant_acceleration_velocity_at(
                observer_heliocentric_velocity_au_per_day,
                observer_heliocentric_acceleration_au_per_day2,
                h),
            constant_acceleration_velocity_at(
                observer_barycentric_velocity_au_per_day,
                observer_barycentric_acceleration_au_per_day2,
                h),
            observer_barycentric_acceleration_au_per_day2,
            light_time_days_per_au,
            solar_schwarzschild_radius_au,
            &next_position,
            &next_velocity)) {
        return false;
    }

    (void)previous_position;
    (void)next_position;
    *out_acceleration_au_per_day2 = vector3_scale(
        vector3_subtract(next_velocity, previous_velocity),
        1.0 / (2.0 * h));
    return finite_vector(*out_acceleration_au_per_day2);
}

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
) noexcept {
    if (!out_position_au || !out_velocity_au_per_day) {
        return false;
    }

    Vector3 p;
    Vector3 p_dot;
    double distance = 0.0;
    double distance_dot = 0.0;
    if (!unit_vector_with_derivative(
            apparent_position_au,
            apparent_velocity_au_per_day,
            &p,
            &p_dot,
            &distance,
            &distance_dot)) {
        return false;
    }

    const Vector3 observer_from_deflector_position_au = vector3_subtract(
        observer_heliocentric_position_au,
        deflector_heliocentric_position_au);
    const Vector3 observer_from_deflector_velocity_au_per_day = vector3_subtract(
        observer_heliocentric_velocity_au_per_day,
        deflector_heliocentric_velocity_au_per_day);
    Vector3 e;
    Vector3 e_dot;
    double em = 0.0;
    double em_dot = 0.0;
    if (!unit_vector_with_derivative(
            observer_from_deflector_position_au,
            observer_from_deflector_velocity_au_per_day,
            &e,
            &e_dot,
            &em,
            &em_dot)) {
        return false;
    }

    const Vector3 source_from_deflector_position_au = vector3_add(
        observer_from_deflector_position_au,
        source_geocentric_position_au);
    const Vector3 source_from_deflector_velocity_au_per_day = vector3_add(
        observer_from_deflector_velocity_au_per_day,
        source_geocentric_velocity_au_per_day);
    Vector3 q;
    Vector3 q_dot;
    if (!unit_vector_with_derivative(source_from_deflector_position_au, source_from_deflector_velocity_au_per_day, &q, &q_dot, 0, 0)) {
        return false;
    }

    const double p_dot_e = vector3_dot(p, e);
    const double p_dot_e_dot = vector3_dot(p_dot, e) + vector3_dot(p, e_dot);
    const double p_dot_q = vector3_dot(p, q);
    const double p_dot_q_dot = vector3_dot(p_dot, q) + vector3_dot(p, q_dot);
    const double q_dot_e = vector3_dot(q, e);
    const double q_dot_e_dot = vector3_dot(q_dot, e) + vector3_dot(q, e_dot);
    const double raw_denominator = 1.0 + q_dot_e;
    const bool limited = raw_denominator < deflection_limit;
    const double denominator = limited ? deflection_limit : raw_denominator;
    const double denominator_dot = limited ? 0.0 : q_dot_e_dot;
    if (em == 0.0 || denominator == 0.0) {
        return false;
    }

    const Vector3 numerator = vector3_subtract(vector3_scale(e, p_dot_q), vector3_scale(q, p_dot_e));
    const Vector3 numerator_dot = vector3_subtract(
        vector3_add(vector3_scale(e_dot, p_dot_q), vector3_scale(e, p_dot_q_dot)),
        vector3_add(vector3_scale(q_dot, p_dot_e), vector3_scale(q, p_dot_e_dot)));
    const double scale = schwarzschild_radius_au / (em * denominator);
    const double scale_dot = -scale * (em_dot / em + denominator_dot / denominator);
    const Vector3 deflected_raw = vector3_add(p, vector3_scale(numerator, scale));
    const Vector3 deflected_raw_dot = vector3_add(
        p_dot,
        vector3_add(vector3_scale(numerator_dot, scale), vector3_scale(numerator, scale_dot)));

    Vector3 direction;
    Vector3 direction_dot;
    if (!unit_vector_with_derivative(deflected_raw, deflected_raw_dot, &direction, &direction_dot, 0, 0)) {
        return false;
    }

    *out_position_au = vector3_scale(direction, distance);
    *out_velocity_au_per_day = vector3_add(
        vector3_scale(direction, distance_dot),
        vector3_scale(direction_dot, distance));
    return true;
}

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
) noexcept {
    if (!out_position_au || !out_velocity_au_per_day || !out_acceleration_au_per_day2) {
        return false;
    }

    Vector3 previous_position;
    Vector3 previous_velocity;
    Vector3 next_position;
    Vector3 next_velocity;
    const double h = CORRECTION_ACCELERATION_STEP_DAYS;
    if (!apply_gravitational_deflection_from_body(
            apparent_position_au,
            apparent_velocity_au_per_day,
            observer_heliocentric_position_au,
            observer_heliocentric_velocity_au_per_day,
            deflector_heliocentric_position_au,
            deflector_heliocentric_velocity_au_per_day,
            source_geocentric_position_au,
            source_geocentric_velocity_au_per_day,
            schwarzschild_radius_au,
            deflection_limit,
            out_position_au,
            out_velocity_au_per_day)
        || !apply_gravitational_deflection_from_body(
            constant_acceleration_position_at(apparent_position_au, apparent_velocity_au_per_day, apparent_acceleration_au_per_day2, -h),
            constant_acceleration_velocity_at(apparent_velocity_au_per_day, apparent_acceleration_au_per_day2, -h),
            constant_acceleration_position_at(
                observer_heliocentric_position_au,
                observer_heliocentric_velocity_au_per_day,
                observer_heliocentric_acceleration_au_per_day2,
                -h),
            constant_acceleration_velocity_at(
                observer_heliocentric_velocity_au_per_day,
                observer_heliocentric_acceleration_au_per_day2,
                -h),
            constant_acceleration_position_at(
                deflector_heliocentric_position_au,
                deflector_heliocentric_velocity_au_per_day,
                deflector_heliocentric_acceleration_au_per_day2,
                -h),
            constant_acceleration_velocity_at(
                deflector_heliocentric_velocity_au_per_day,
                deflector_heliocentric_acceleration_au_per_day2,
                -h),
            constant_acceleration_position_at(
                source_geocentric_position_au,
                source_geocentric_velocity_au_per_day,
                source_geocentric_acceleration_au_per_day2,
                -h),
            constant_acceleration_velocity_at(
                source_geocentric_velocity_au_per_day,
                source_geocentric_acceleration_au_per_day2,
                -h),
            schwarzschild_radius_au,
            deflection_limit,
            &previous_position,
            &previous_velocity)
        || !apply_gravitational_deflection_from_body(
            constant_acceleration_position_at(apparent_position_au, apparent_velocity_au_per_day, apparent_acceleration_au_per_day2, h),
            constant_acceleration_velocity_at(apparent_velocity_au_per_day, apparent_acceleration_au_per_day2, h),
            constant_acceleration_position_at(
                observer_heliocentric_position_au,
                observer_heliocentric_velocity_au_per_day,
                observer_heliocentric_acceleration_au_per_day2,
                h),
            constant_acceleration_velocity_at(
                observer_heliocentric_velocity_au_per_day,
                observer_heliocentric_acceleration_au_per_day2,
                h),
            constant_acceleration_position_at(
                deflector_heliocentric_position_au,
                deflector_heliocentric_velocity_au_per_day,
                deflector_heliocentric_acceleration_au_per_day2,
                h),
            constant_acceleration_velocity_at(
                deflector_heliocentric_velocity_au_per_day,
                deflector_heliocentric_acceleration_au_per_day2,
                h),
            constant_acceleration_position_at(
                source_geocentric_position_au,
                source_geocentric_velocity_au_per_day,
                source_geocentric_acceleration_au_per_day2,
                h),
            constant_acceleration_velocity_at(
                source_geocentric_velocity_au_per_day,
                source_geocentric_acceleration_au_per_day2,
                h),
            schwarzschild_radius_au,
            deflection_limit,
            &next_position,
            &next_velocity)) {
        return false;
    }

    (void)previous_position;
    (void)next_position;
    *out_acceleration_au_per_day2 = vector3_scale(
        vector3_subtract(next_velocity, previous_velocity),
        1.0 / (2.0 * h));
    return finite_vector(*out_acceleration_au_per_day2);
}

}  // namespace taiyin
