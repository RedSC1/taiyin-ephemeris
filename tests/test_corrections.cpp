#include "taiyin/corrections.h"
#include "taiyin/vector3.h"

#include <cmath>
#include <iomanip>
#include <iostream>

namespace {

bool near(double actual, double expected, double tol) {
    return std::fabs(actual - expected) <= tol;
}

bool vector_near(const taiyin::Vector3& actual, const taiyin::Vector3& expected, double tol) {
    return near(actual.x, expected.x, tol)
        && near(actual.y, expected.y, tol)
        && near(actual.z, expected.z, tol);
}

void expect_true(bool value, const char* message, int* failures) {
    if (!value) {
        std::cerr << "FAIL: " << message << "\n";
        ++(*failures);
    }
}

void expect_near(double actual, double expected, double tol, const char* message, int* failures) {
    if (!near(actual, expected, tol)) {
        std::cerr << std::setprecision(17)
                  << "FAIL: " << message << ": actual=" << actual << " expected=" << expected
                  << " diff=" << std::fabs(actual - expected) << "\n";
        ++(*failures);
    }
}

void expect_vector_near(
    const taiyin::Vector3& actual,
    const taiyin::Vector3& expected,
    double tol,
    const char* message,
    int* failures
) {
    if (!vector_near(actual, expected, tol)) {
        std::cerr << "FAIL: " << message << ": actual=("
                  << actual.x << ", " << actual.y << ", " << actual.z << ") expected=("
                  << expected.x << ", " << expected.y << ", " << expected.z << ")\n";
        ++(*failures);
    }
}

struct PolynomialTarget {
    double epoch_jd_tdb;
    taiyin::Vector3 position_au;
    taiyin::Vector3 velocity_au_per_day;
    taiyin::Vector3 acceleration_au_per_day2;
};

struct Dual2 {
    double value;
    double first;
    double second;
};

struct DualVector3 {
    Dual2 x;
    Dual2 y;
    Dual2 z;
};

Dual2 dual_constant(double value) {
    return Dual2{ value, 0.0, 0.0 };
}

Dual2 dual_variable(double value) {
    return Dual2{ value, 1.0, 0.0 };
}

Dual2 operator+(const Dual2& a, const Dual2& b) {
    return Dual2{ a.value + b.value, a.first + b.first, a.second + b.second };
}

Dual2 operator-(const Dual2& a, const Dual2& b) {
    return Dual2{ a.value - b.value, a.first - b.first, a.second - b.second };
}

Dual2 operator-(const Dual2& a) {
    return Dual2{ -a.value, -a.first, -a.second };
}

Dual2 operator*(const Dual2& a, const Dual2& b) {
    return Dual2{
        a.value * b.value,
        a.first * b.value + a.value * b.first,
        a.second * b.value + 2.0 * a.first * b.first + a.value * b.second,
    };
}

Dual2 operator*(const Dual2& a, double b) {
    return Dual2{ a.value * b, a.first * b, a.second * b };
}

Dual2 operator*(double a, const Dual2& b) {
    return b * a;
}

Dual2 reciprocal(const Dual2& a) {
    return Dual2{
        1.0 / a.value,
        -a.first / (a.value * a.value),
        2.0 * a.first * a.first / (a.value * a.value * a.value)
            - a.second / (a.value * a.value),
    };
}

Dual2 operator/(const Dual2& a, const Dual2& b) {
    return a * reciprocal(b);
}

Dual2 dual_sqrt(const Dual2& a) {
    const double root = std::sqrt(a.value);
    return Dual2{
        root,
        a.first / (2.0 * root),
        a.second / (2.0 * root) - (a.first * a.first) / (4.0 * root * root * root),
    };
}

Dual2 dual_log(const Dual2& a) {
    return Dual2{
        std::log(a.value),
        a.first / a.value,
        a.second / a.value - (a.first * a.first) / (a.value * a.value),
    };
}

DualVector3 dual_vector_add(const DualVector3& a, const DualVector3& b) {
    return DualVector3{ a.x + b.x, a.y + b.y, a.z + b.z };
}

DualVector3 dual_vector_subtract(const DualVector3& a, const DualVector3& b) {
    return DualVector3{ a.x - b.x, a.y - b.y, a.z - b.z };
}

DualVector3 dual_vector_scale(const DualVector3& a, const Dual2& scale) {
    return DualVector3{ a.x * scale, a.y * scale, a.z * scale };
}

Dual2 dual_vector_dot(const DualVector3& a, const DualVector3& b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

Dual2 dual_vector_norm(const DualVector3& a) {
    return dual_sqrt(dual_vector_dot(a, a));
}

DualVector3 dual_polynomial_position(const Dual2& jd_tdb, const PolynomialTarget& target) {
    const Dual2 dt = jd_tdb - dual_constant(target.epoch_jd_tdb);
    const Dual2 half_dt2 = dt * dt * 0.5;
    return DualVector3{
        dual_constant(target.position_au.x) + dt * target.velocity_au_per_day.x + half_dt2 * target.acceleration_au_per_day2.x,
        dual_constant(target.position_au.y) + dt * target.velocity_au_per_day.y + half_dt2 * target.acceleration_au_per_day2.y,
        dual_constant(target.position_au.z) + dt * target.velocity_au_per_day.z + half_dt2 * target.acceleration_au_per_day2.z,
    };
}

bool solve_shapiro_polynomial_state_dual(
    double jd_tdb,
    const PolynomialTarget& observer,
    const PolynomialTarget& target,
    taiyin::Vector3* position,
    taiyin::Vector3* velocity,
    taiyin::Vector3* acceleration,
    double* light_time_days,
    double* light_time_rate,
    double* light_time_acceleration
) {
    if (!position || !velocity || !acceleration || !light_time_days || !light_time_rate || !light_time_acceleration) {
        return false;
    }

    const Dual2 t = dual_variable(jd_tdb);
    const DualVector3 observer_position = dual_polynomial_position(t, observer);
    Dual2 tau = dual_constant(0.0);
    DualVector3 target_position = dual_polynomial_position(t, target);
    DualVector3 geocentric_position = dual_vector_subtract(target_position, observer_position);
    Dual2 distance = dual_vector_norm(geocentric_position);
    tau = distance * taiyin::TAIYIN_LIGHT_TIME_DAYS_PER_AU;

    const double schwarzschild_light_time_days =
        taiyin::TAIYIN_SOLAR_SCHWARZSCHILD_RADIUS_AU * taiyin::TAIYIN_LIGHT_TIME_DAYS_PER_AU;
    for (int i = 0; i < 20; ++i) {
        const Dual2 emission_time = t - tau;
        target_position = dual_polynomial_position(emission_time, target);
        geocentric_position = dual_vector_subtract(target_position, observer_position);
        distance = dual_vector_norm(geocentric_position);
        const Dual2 observer_radius = dual_vector_norm(observer_position);
        const Dual2 target_radius = dual_vector_norm(target_position);
        const Dual2 radius_sum = observer_radius + target_radius;
        const Dual2 delay = dual_log((radius_sum + distance) / (radius_sum - distance))
            * schwarzschild_light_time_days;
        tau = distance * taiyin::TAIYIN_LIGHT_TIME_DAYS_PER_AU + delay;
    }

    const Dual2 emission_time = t - tau;
    target_position = dual_polynomial_position(emission_time, target);
    geocentric_position = dual_vector_subtract(target_position, observer_position);
    *position = taiyin::Vector3{ geocentric_position.x.value, geocentric_position.y.value, geocentric_position.z.value };
    *velocity = taiyin::Vector3{ geocentric_position.x.first, geocentric_position.y.first, geocentric_position.z.first };
    *acceleration = taiyin::Vector3{ geocentric_position.x.second, geocentric_position.y.second, geocentric_position.z.second };
    *light_time_days = tau.value;
    *light_time_rate = tau.first;
    *light_time_acceleration = tau.second;
    return true;
}

taiyin::Vector3 vector_add_scaled(const taiyin::Vector3& a, const taiyin::Vector3& b, double scale) {
    return taiyin::vector3_add(a, taiyin::vector3_scale(b, scale));
}

taiyin::Vector3 constant_acceleration_position_at(
    const taiyin::Vector3& position,
    const taiyin::Vector3& velocity,
    const taiyin::Vector3& acceleration,
    double dt_days
) {
    return vector_add_scaled(
        vector_add_scaled(position, velocity, dt_days),
        acceleration,
        0.5 * dt_days * dt_days);
}

taiyin::Vector3 constant_acceleration_velocity_at(
    const taiyin::Vector3& velocity,
    const taiyin::Vector3& acceleration,
    double dt_days
) {
    return vector_add_scaled(velocity, acceleration, dt_days);
}

bool polynomial_target_position(double jd_tdb, const void* data, taiyin::Vector3* out_position_au) {
    if (!data || !out_position_au) {
        return false;
    }
    const PolynomialTarget* target = static_cast<const PolynomialTarget*>(data);
    const double dt = jd_tdb - target->epoch_jd_tdb;
    *out_position_au = vector_add_scaled(
        vector_add_scaled(target->position_au, target->velocity_au_per_day, dt),
        target->acceleration_au_per_day2,
        0.5 * dt * dt);
    return true;
}

bool polynomial_target_velocity(double jd_tdb, const void* data, taiyin::Vector3* out_velocity_au_per_day) {
    if (!data || !out_velocity_au_per_day) {
        return false;
    }
    const PolynomialTarget* target = static_cast<const PolynomialTarget*>(data);
    const double dt = jd_tdb - target->epoch_jd_tdb;
    *out_velocity_au_per_day = vector_add_scaled(
        target->velocity_au_per_day,
        target->acceleration_au_per_day2,
        dt);
    return true;
}

bool polynomial_target_acceleration(double, const void* data, taiyin::Vector3* out_acceleration_au_per_day2) {
    if (!data || !out_acceleration_au_per_day2) {
        return false;
    }
    const PolynomialTarget* target = static_cast<const PolynomialTarget*>(data);
    *out_acceleration_au_per_day2 = target->acceleration_au_per_day2;
    return true;
}

bool polynomial_state(
    double jd_tdb,
    const PolynomialTarget& target,
    taiyin::Vector3* position,
    taiyin::Vector3* velocity,
    taiyin::Vector3* acceleration
) {
    return polynomial_target_position(jd_tdb, &target, position)
        && polynomial_target_velocity(jd_tdb, &target, velocity)
        && polynomial_target_acceleration(jd_tdb, &target, acceleration);
}

bool solve_shapiro_polynomial_state(
    double jd_tdb,
    const PolynomialTarget& observer,
    const PolynomialTarget& target,
    taiyin::Vector3* position,
    taiyin::Vector3* velocity,
    taiyin::Vector3* acceleration,
    double* light_time_days,
    double* light_time_rate,
    double* light_time_acceleration
) {
    taiyin::Vector3 observer_position;
    taiyin::Vector3 observer_velocity;
    taiyin::Vector3 observer_acceleration;
    if (!polynomial_state(jd_tdb, observer, &observer_position, &observer_velocity, &observer_acceleration)) {
        return false;
    }
    return taiyin::solve_light_time_acceleration_with_shapiro(
        jd_tdb,
        observer_position,
        observer_velocity,
        observer_acceleration,
        polynomial_target_position,
        polynomial_target_velocity,
        polynomial_target_acceleration,
        &target,
        taiyin::TAIYIN_LIGHT_TIME_DAYS_PER_AU,
        taiyin::TAIYIN_SOLAR_SCHWARZSCHILD_RADIUS_AU * taiyin::TAIYIN_LIGHT_TIME_DAYS_PER_AU,
        12,
        1e-15,
        position,
        velocity,
        acceleration,
        light_time_days,
        light_time_rate,
        light_time_acceleration,
        0,
        0,
        0);
}

struct MultiShapiroDeflectorData {
    const PolynomialTarget* deflectors;
    int count;
};

bool multi_shapiro_deflector_position(double jd_tdb, const void* data, int index, taiyin::Vector3* out_position_au) {
    const MultiShapiroDeflectorData* deflectors = static_cast<const MultiShapiroDeflectorData*>(data);
    if (!deflectors || !out_position_au || index < 0 || index >= deflectors->count) {
        return false;
    }
    return polynomial_target_position(jd_tdb, &deflectors->deflectors[index], out_position_au);
}

bool multi_shapiro_deflector_velocity(double jd_tdb, const void* data, int index, taiyin::Vector3* out_velocity_au_per_day) {
    const MultiShapiroDeflectorData* deflectors = static_cast<const MultiShapiroDeflectorData*>(data);
    if (!deflectors || !out_velocity_au_per_day || index < 0 || index >= deflectors->count) {
        return false;
    }
    return polynomial_target_velocity(jd_tdb, &deflectors->deflectors[index], out_velocity_au_per_day);
}

bool multi_shapiro_deflector_acceleration(double jd_tdb, const void* data, int index, taiyin::Vector3* out_acceleration_au_per_day2) {
    const MultiShapiroDeflectorData* deflectors = static_cast<const MultiShapiroDeflectorData*>(data);
    if (!deflectors || !out_acceleration_au_per_day2 || index < 0 || index >= deflectors->count) {
        return false;
    }
    return polynomial_target_acceleration(jd_tdb, &deflectors->deflectors[index], out_acceleration_au_per_day2);
}

}  // namespace

int main() {
    int failures = 0;

    {
        const taiyin::Vector3 a = { 1.0, 2.0, 3.0 };
        const taiyin::Vector3 b = { -4.0, 5.0, -6.0 };
        expect_vector_near(taiyin::vector3_add(a, b), { -3.0, 7.0, -3.0 }, 0.0, "vector add", &failures);
        expect_vector_near(taiyin::vector3_subtract(a, b), { 5.0, -3.0, 9.0 }, 0.0, "vector subtract", &failures);
        expect_vector_near(taiyin::vector3_cross(a, b), { -27.0, -6.0, 13.0 }, 0.0, "vector cross", &failures);
        expect_near(taiyin::vector3_dot(a, b), -12.0, 0.0, "vector dot", &failures);
    }

    {
        taiyin::Vector3 unit;
        taiyin::Vector3 unit_dot;
        double norm = 0.0;
        double norm_dot = 0.0;
        expect_true(
            taiyin::unit_vector_with_derivative({ 3.0, 4.0, 0.0 }, { 0.3, 0.4, 0.0 }, &unit, &unit_dot, &norm, &norm_dot),
            "unit vector succeeds",
            &failures);
        expect_vector_near(unit, { 0.6, 0.8, 0.0 }, 1e-15, "unit vector", &failures);
        expect_vector_near(unit_dot, { 0.0, 0.0, 0.0 }, 1e-15, "unit derivative radial velocity", &failures);
        expect_near(norm, 5.0, 1e-15, "unit norm", &failures);
        expect_near(norm_dot, 0.5, 1e-15, "unit norm dot", &failures);
        expect_true(
            !taiyin::unit_vector_with_derivative({ 0.0, 0.0, 0.0 }, { 0.0, 0.0, 0.0 }, 0, 0, 0, 0),
            "zero vector rejected",
            &failures);
    }

    {
        const taiyin::Vector3 observer_position = { 1.0, 0.0, 0.0 };
        const taiyin::Vector3 target_position = { 1.5, 0.2, 0.0 };
        const taiyin::Vector3 geocentric_position = taiyin::vector3_subtract(target_position, observer_position);
        double delay = 0.0;
        double dr = 0.0;
        double dd = 0.0;
        expect_true(
            taiyin::solar_shapiro_delay_terms(
                observer_position,
                target_position,
                geocentric_position,
                taiyin::TAIYIN_SOLAR_SCHWARZSCHILD_RADIUS_AU * taiyin::TAIYIN_LIGHT_TIME_DAYS_PER_AU,
                &delay,
                &dr,
                &dd),
            "shapiro terms succeed",
            &failures);
        expect_true(delay > 0.0, "shapiro delay positive", &failures);
        expect_true(std::isfinite(dr) && std::isfinite(dd), "shapiro derivatives finite", &failures);
    }

    {
        const double jd_tdb = 1000.0;
        const double light_time_days_per_au = 0.005;
        const PolynomialTarget target = {
            jd_tdb,
            { 3.0, 4.0, 0.0 },
            { 0.0, 0.0, 0.0 },
            { 0.0, 0.0, 0.0 },
        };
        taiyin::Vector3 observed_position;
        taiyin::Vector3 retarded_target_position;
        double light_time_days = 0.0;
        expect_true(
            taiyin::solve_light_time_position(
                jd_tdb,
                { 0.0, 0.0, 0.0 },
                polynomial_target_position,
                &target,
                light_time_days_per_au,
                8,
                1e-14,
                &observed_position,
                &light_time_days,
                &retarded_target_position),
            "light-time stationary position succeeds",
            &failures);
        expect_vector_near(observed_position, { 3.0, 4.0, 0.0 }, 0.0, "light-time stationary observed", &failures);
        expect_vector_near(retarded_target_position, { 3.0, 4.0, 0.0 }, 0.0, "light-time stationary target", &failures);
        expect_near(light_time_days, 5.0 * light_time_days_per_au, 0.0, "light-time stationary tau", &failures);
        expect_true(
            !taiyin::solve_light_time_position(
                jd_tdb,
                { 0.0, 0.0, 0.0 },
                0,
                &target,
                light_time_days_per_au,
                8,
                1e-14,
                &observed_position,
                &light_time_days,
                &retarded_target_position),
            "light-time null target rejected",
            &failures);
    }

    {
        const double jd_tdb = 1000.0;
        const double light_time_days_per_au = 0.005;
        const PolynomialTarget target = {
            jd_tdb,
            { 2.0, 0.0, 0.0 },
            { 0.01, 0.0, 0.0 },
            { 0.0, 0.0, 0.0 },
        };
        const taiyin::Vector3 observer_position = { 0.5, 0.0, 0.0 };
        const taiyin::Vector3 observer_velocity = { -0.003, 0.0, 0.0 };
        const double expected_tau = light_time_days_per_au * (target.position_au.x - observer_position.x)
            / (1.0 + light_time_days_per_au * target.velocity_au_per_day.x);
        const double expected_tau_dot = light_time_days_per_au
            * (target.velocity_au_per_day.x - observer_velocity.x)
            / (1.0 + light_time_days_per_au * target.velocity_au_per_day.x);
        const double expected_position_x = target.position_au.x
            - target.velocity_au_per_day.x * expected_tau
            - observer_position.x;
        const double expected_velocity_x = target.velocity_au_per_day.x * (1.0 - expected_tau_dot)
            - observer_velocity.x;
        taiyin::Vector3 observed_position;
        taiyin::Vector3 observed_velocity;
        taiyin::Vector3 retarded_target_position;
        taiyin::Vector3 retarded_target_velocity;
        double light_time_days = 0.0;
        double light_time_rate = 0.0;
        expect_true(
            taiyin::solve_light_time_velocity(
                jd_tdb,
                observer_position,
                observer_velocity,
                polynomial_target_position,
                polynomial_target_velocity,
                &target,
                light_time_days_per_au,
                8,
                1e-14,
                &observed_position,
                &observed_velocity,
                &light_time_days,
                &light_time_rate,
                &retarded_target_position,
                &retarded_target_velocity),
            "light-time linear velocity succeeds",
            &failures);
        expect_near(light_time_days, expected_tau, 1e-14, "light-time linear tau", &failures);
        expect_near(light_time_rate, expected_tau_dot, 1e-15, "light-time linear tau dot", &failures);
        expect_vector_near(observed_position, { expected_position_x, 0.0, 0.0 }, 1e-14, "light-time linear position", &failures);
        expect_vector_near(observed_velocity, { expected_velocity_x, 0.0, 0.0 }, 1e-15, "light-time linear velocity", &failures);
        expect_vector_near(retarded_target_position, { target.position_au.x - target.velocity_au_per_day.x * expected_tau, 0.0, 0.0 }, 1e-14, "light-time linear target position", &failures);
        expect_vector_near(retarded_target_velocity, target.velocity_au_per_day, 0.0, "light-time linear target velocity", &failures);
    }

    {
        const double jd_tdb = 1000.0;
        const double light_time_days_per_au = 0.005;
        const PolynomialTarget target = {
            jd_tdb,
            { 2.0, 0.0, 0.0 },
            { 0.01, 0.0, 0.0 },
            { 0.02, 0.0, 0.0 },
        };
        const double quadratic_a = 0.5 * light_time_days_per_au * target.acceleration_au_per_day2.x;
        const double quadratic_b = -(1.0 + light_time_days_per_au * target.velocity_au_per_day.x);
        const double quadratic_c = light_time_days_per_au * target.position_au.x;
        const double expected_tau = (2.0 * quadratic_c)
            / (-quadratic_b + std::sqrt(quadratic_b * quadratic_b - 4.0 * quadratic_a * quadratic_c));
        const double retarded_velocity_x = target.velocity_au_per_day.x
            - target.acceleration_au_per_day2.x * expected_tau;
        const double expected_tau_dot = light_time_days_per_au * retarded_velocity_x
            / (1.0 + light_time_days_per_au * retarded_velocity_x);
        const double expected_tau_ddot = light_time_days_per_au
            * target.acceleration_au_per_day2.x
            * (1.0 - expected_tau_dot)
            * (1.0 - expected_tau_dot)
            / (1.0 + light_time_days_per_au * retarded_velocity_x);
        const double expected_acceleration_x = target.acceleration_au_per_day2.x
            * (1.0 - expected_tau_dot)
            * (1.0 - expected_tau_dot)
            - retarded_velocity_x * expected_tau_ddot;
        taiyin::Vector3 observed_position;
        taiyin::Vector3 observed_velocity;
        taiyin::Vector3 observed_acceleration;
        double light_time_days = 0.0;
        double light_time_rate = 0.0;
        double light_time_acceleration = 0.0;
        expect_true(
            taiyin::solve_light_time_acceleration(
                jd_tdb,
                { 0.0, 0.0, 0.0 },
                { 0.0, 0.0, 0.0 },
                { 0.0, 0.0, 0.0 },
                polynomial_target_position,
                polynomial_target_velocity,
                polynomial_target_acceleration,
                &target,
                light_time_days_per_au,
                12,
                1e-15,
                &observed_position,
                &observed_velocity,
                &observed_acceleration,
                &light_time_days,
                &light_time_rate,
                &light_time_acceleration,
                0,
                0,
                0),
            "light-time quadratic acceleration succeeds",
            &failures);
        expect_near(light_time_days, expected_tau, 1e-14, "light-time quadratic tau", &failures);
        expect_near(light_time_rate, expected_tau_dot, 1e-15, "light-time quadratic tau dot", &failures);
        expect_near(light_time_acceleration, expected_tau_ddot, 1e-15, "light-time quadratic tau ddot", &failures);
        expect_vector_near(observed_acceleration, { expected_acceleration_x, 0.0, 0.0 }, 1e-14, "light-time quadratic acceleration", &failures);
    }

    {
        const double jd_tdb = 1000.0;
        const PolynomialTarget target = {
            jd_tdb,
            { -2.0, 0.1, 0.0 },
            { 0.0, 0.0, 0.0 },
            { 0.0, 0.0, 0.0 },
        };
        taiyin::Vector3 shapiro_position;
        taiyin::Vector3 geometric_position;
        double shapiro_light_time = 0.0;
        double geometric_light_time = 0.0;
        expect_true(
            taiyin::solve_light_time_position_with_shapiro(
                jd_tdb,
                { 1.0, 0.0, 0.0 },
                polynomial_target_position,
                &target,
                taiyin::TAIYIN_LIGHT_TIME_DAYS_PER_AU,
                taiyin::TAIYIN_SOLAR_SCHWARZSCHILD_RADIUS_AU * taiyin::TAIYIN_LIGHT_TIME_DAYS_PER_AU,
                8,
                1e-15,
                &shapiro_position,
                &shapiro_light_time,
                0),
            "light-time Shapiro stationary position succeeds",
            &failures);
        expect_true(
            taiyin::solve_light_time_position(
                jd_tdb,
                { 1.0, 0.0, 0.0 },
                polynomial_target_position,
                &target,
                taiyin::TAIYIN_LIGHT_TIME_DAYS_PER_AU,
                8,
                1e-15,
                &geometric_position,
                &geometric_light_time,
                0),
            "light-time geometric comparison succeeds",
            &failures);
        expect_vector_near(shapiro_position, geometric_position, 0.0, "light-time Shapiro stationary position", &failures);
        expect_true(shapiro_light_time > geometric_light_time, "light-time Shapiro increases tau", &failures);
    }

    {
        const double jd_tdb = 1000.0;
        const PolynomialTarget observer = {
            jd_tdb,
            { 0.8, 0.3, 0.1 },
            { -0.001, 0.017, 0.0002 },
            { 0.00005, -0.00002, 0.00001 },
        };
        const PolynomialTarget target = {
            jd_tdb,
            { 1.5, 1.2, 0.4 },
            { -0.008, 0.006, 0.001 },
            { 0.00003, -0.00004, 0.00002 },
        };
        taiyin::Vector3 position;
        taiyin::Vector3 velocity;
        taiyin::Vector3 acceleration;
        taiyin::Vector3 oracle_position;
        taiyin::Vector3 oracle_velocity;
        taiyin::Vector3 oracle_acceleration;
        double light_time_days = 0.0;
        double light_time_rate = 0.0;
        double light_time_acceleration = 0.0;
        double oracle_light_time_days = 0.0;
        double oracle_light_time_rate = 0.0;
        double oracle_light_time_acceleration = 0.0;
        expect_true(
            solve_shapiro_polynomial_state(
                jd_tdb,
                observer,
                target,
                &position,
                &velocity,
                &acceleration,
                &light_time_days,
                &light_time_rate,
                &light_time_acceleration),
            "light-time Shapiro acceleration succeeds",
            &failures);
        expect_true(
            solve_shapiro_polynomial_state_dual(
                jd_tdb,
                observer,
                target,
                &oracle_position,
                &oracle_velocity,
                &oracle_acceleration,
                &oracle_light_time_days,
                &oracle_light_time_rate,
                &oracle_light_time_acceleration),
            "light-time Shapiro dual oracle succeeds",
            &failures);
        expect_vector_near(position, oracle_position, 1e-14, "light-time Shapiro position dual oracle", &failures);
        expect_vector_near(velocity, oracle_velocity, 1e-14, "light-time Shapiro velocity dual oracle", &failures);
        expect_vector_near(acceleration, oracle_acceleration, 1e-14, "light-time Shapiro acceleration dual oracle", &failures);
        expect_near(light_time_days, oracle_light_time_days, 1e-14, "light-time Shapiro tau dual oracle", &failures);
        expect_near(light_time_rate, oracle_light_time_rate, 1e-14, "light-time Shapiro tau dot dual oracle", &failures);
        expect_near(light_time_acceleration, oracle_light_time_acceleration, 1e-14, "light-time Shapiro tau ddot dual oracle", &failures);
        expect_true(std::isfinite(light_time_days), "light-time Shapiro tau finite", &failures);
        expect_true(std::isfinite(light_time_rate), "light-time Shapiro tau dot finite", &failures);
        expect_true(std::isfinite(light_time_acceleration), "light-time Shapiro tau ddot finite", &failures);
    }

    {
        const double jd_tdb = 1000.0;
        const PolynomialTarget observer = {
            jd_tdb,
            { 0.8, 0.3, 0.1 },
            { -0.001, 0.017, 0.0002 },
            { 0.00005, -0.00002, 0.00001 },
        };
        const PolynomialTarget target = {
            jd_tdb,
            { 1.5, 1.2, 0.4 },
            { -0.008, 0.006, 0.001 },
            { 0.00003, -0.00004, 0.00002 },
        };
        const PolynomialTarget deflectors[] = {
            { jd_tdb, { 0.0, 0.0, 0.0 }, { 0.0, 0.0, 0.0 }, { 0.0, 0.0, 0.0 } },
            { jd_tdb, { -0.2, 0.6, -0.1 }, { 0.0003, -0.0001, 0.0002 }, { 0.00001, 0.00002, -0.00001 } },
        };
        MultiShapiroDeflectorData deflector_data = { deflectors, 2 };
        const double single_radius[] = { taiyin::TAIYIN_SOLAR_SCHWARZSCHILD_RADIUS_AU };
        const double multi_radius[] = {
            taiyin::TAIYIN_SOLAR_SCHWARZSCHILD_RADIUS_AU,
            taiyin::TAIYIN_SOLAR_SCHWARZSCHILD_RADIUS_AU * 0.25,
        };
        taiyin::Vector3 observer_position;
        taiyin::Vector3 observer_velocity;
        taiyin::Vector3 observer_acceleration;
        expect_true(polynomial_state(jd_tdb, observer, &observer_position, &observer_velocity, &observer_acceleration), "multi Shapiro observer state", &failures);

        taiyin::Vector3 single_position;
        taiyin::Vector3 single_velocity;
        taiyin::Vector3 single_acceleration;
        taiyin::Vector3 multi_position;
        taiyin::Vector3 multi_velocity;
        taiyin::Vector3 multi_acceleration;
        double single_light_time = 0.0;
        double single_light_time_rate = 0.0;
        double single_light_time_acceleration = 0.0;
        double multi_light_time = 0.0;
        double multi_light_time_rate = 0.0;
        double multi_light_time_acceleration = 0.0;
        expect_true(
            taiyin::solve_light_time_acceleration_with_shapiro(
                jd_tdb,
                observer_position,
                observer_velocity,
                observer_acceleration,
                polynomial_target_position,
                polynomial_target_velocity,
                polynomial_target_acceleration,
                &target,
                taiyin::TAIYIN_LIGHT_TIME_DAYS_PER_AU,
                taiyin::TAIYIN_SOLAR_SCHWARZSCHILD_RADIUS_AU * taiyin::TAIYIN_LIGHT_TIME_DAYS_PER_AU,
                12,
                1e-15,
                &single_position,
                &single_velocity,
                &single_acceleration,
                &single_light_time,
                &single_light_time_rate,
                &single_light_time_acceleration,
                0,
                0,
                0),
            "single Shapiro acceleration for multi equivalence succeeds",
            &failures);
        expect_true(
            taiyin::solve_light_time_acceleration_with_multi_shapiro(
                jd_tdb,
                observer_position,
                observer_velocity,
                observer_acceleration,
                polynomial_target_position,
                polynomial_target_velocity,
                polynomial_target_acceleration,
                &target,
                1,
                single_radius,
                multi_shapiro_deflector_position,
                multi_shapiro_deflector_velocity,
                multi_shapiro_deflector_acceleration,
                &deflector_data,
                taiyin::TAIYIN_LIGHT_TIME_DAYS_PER_AU,
                12,
                1e-15,
                &multi_position,
                &multi_velocity,
                &multi_acceleration,
                &multi_light_time,
                &multi_light_time_rate,
                &multi_light_time_acceleration,
                0,
                0,
                0),
            "multi Shapiro single-deflector equivalence succeeds",
            &failures);
        expect_vector_near(multi_position, single_position, 1e-15, "multi Shapiro single position equivalence", &failures);
        expect_vector_near(multi_velocity, single_velocity, 1e-15, "multi Shapiro single velocity equivalence", &failures);
        expect_vector_near(multi_acceleration, single_acceleration, 1e-15, "multi Shapiro single acceleration equivalence", &failures);
        expect_near(multi_light_time, single_light_time, 1e-15, "multi Shapiro single tau equivalence", &failures);
        expect_near(multi_light_time_rate, single_light_time_rate, 1e-15, "multi Shapiro single tau dot equivalence", &failures);
        expect_near(multi_light_time_acceleration, single_light_time_acceleration, 1e-15, "multi Shapiro single tau ddot equivalence", &failures);

        double two_body_light_time = 0.0;
        double two_body_light_time_rate = 0.0;
        expect_true(
            taiyin::solve_light_time_velocity_with_multi_shapiro(
                jd_tdb,
                observer_position,
                observer_velocity,
                polynomial_target_position,
                polynomial_target_velocity,
                &target,
                2,
                multi_radius,
                multi_shapiro_deflector_position,
                multi_shapiro_deflector_velocity,
                &deflector_data,
                taiyin::TAIYIN_LIGHT_TIME_DAYS_PER_AU,
                12,
                1e-15,
                &multi_position,
                &multi_velocity,
                &two_body_light_time,
                &two_body_light_time_rate,
                0,
                0),
            "multi Shapiro two-deflector velocity succeeds",
            &failures);
        expect_true(two_body_light_time > single_light_time, "multi Shapiro second deflector increases tau", &failures);
        expect_true(std::isfinite(two_body_light_time_rate), "multi Shapiro two-deflector tau dot finite", &failures);
    }

    {
        const taiyin::Vector3 source_position = { 0.7, 1.2, 0.3 };
        const taiyin::Vector3 source_velocity = { 0.001, -0.002, 0.0005 };
        const taiyin::Vector3 source_acceleration = { -3e-5, 1e-5, 2e-5 };
        const taiyin::Vector3 observer_velocity = { 0.00011, -0.00023, 0.00007 };
        const taiyin::Vector3 observer_acceleration = { -2e-6, 3e-6, 1e-6 };
        taiyin::Vector3 aberrated_position;
        taiyin::Vector3 aberrated_velocity;
        taiyin::Vector3 aberrated_acceleration;
        expect_true(
            taiyin::apply_observer_velocity_aberration_acceleration(
                source_position,
                source_velocity,
                source_acceleration,
                { 0.0, 0.0, 0.0 },
                { 0.0, 0.0, 0.0 },
                taiyin::TAIYIN_LIGHT_TIME_DAYS_PER_AU,
                &aberrated_position,
                &aberrated_velocity,
                &aberrated_acceleration),
            "observer velocity aberration zero succeeds",
            &failures);
        expect_vector_near(aberrated_position, source_position, 1e-15, "observer velocity zero position", &failures);
        expect_vector_near(aberrated_velocity, source_velocity, 1e-15, "observer velocity zero velocity", &failures);
        expect_vector_near(aberrated_acceleration, source_acceleration, 1e-10, "observer velocity zero acceleration", &failures);

        taiyin::Vector3 old_position;
        taiyin::Vector3 old_velocity;
        expect_true(
            taiyin::apply_observer_velocity_aberration(
                source_position,
                source_velocity,
                observer_velocity,
                observer_acceleration,
                taiyin::TAIYIN_LIGHT_TIME_DAYS_PER_AU,
                &old_position,
                &old_velocity),
            "observer velocity old succeeds",
            &failures);
        expect_true(
            taiyin::apply_observer_velocity_aberration_acceleration(
                source_position,
                source_velocity,
                source_acceleration,
                observer_velocity,
                observer_acceleration,
                taiyin::TAIYIN_LIGHT_TIME_DAYS_PER_AU,
                &aberrated_position,
                &aberrated_velocity,
                &aberrated_acceleration),
            "observer velocity acceleration succeeds",
            &failures);
        expect_vector_near(aberrated_position, old_position, 0.0, "observer acceleration wrapper position", &failures);
        expect_vector_near(aberrated_velocity, old_velocity, 0.0, "observer acceleration wrapper velocity", &failures);

        const double h = 1e-4;
        taiyin::Vector3 previous_position;
        taiyin::Vector3 previous_velocity;
        taiyin::Vector3 next_position;
        taiyin::Vector3 next_velocity;
        expect_true(
            taiyin::apply_observer_velocity_aberration(
                constant_acceleration_position_at(source_position, source_velocity, source_acceleration, -h),
                constant_acceleration_velocity_at(source_velocity, source_acceleration, -h),
                constant_acceleration_velocity_at(observer_velocity, observer_acceleration, -h),
                observer_acceleration,
                taiyin::TAIYIN_LIGHT_TIME_DAYS_PER_AU,
                &previous_position,
                &previous_velocity),
            "observer velocity previous succeeds",
            &failures);
        expect_true(
            taiyin::apply_observer_velocity_aberration(
                constant_acceleration_position_at(source_position, source_velocity, source_acceleration, h),
                constant_acceleration_velocity_at(source_velocity, source_acceleration, h),
                constant_acceleration_velocity_at(observer_velocity, observer_acceleration, h),
                observer_acceleration,
                taiyin::TAIYIN_LIGHT_TIME_DAYS_PER_AU,
                &next_position,
                &next_velocity),
            "observer velocity next succeeds",
            &failures);
        expect_vector_near(
            aberrated_acceleration,
            taiyin::vector3_scale(taiyin::vector3_subtract(next_velocity, previous_velocity), 1.0 / (2.0 * h)),
            1e-14,
            "observer velocity acceleration finite difference",
            &failures);
    }

    {
        const taiyin::Vector3 source_position = { 0.7, 1.2, 0.3 };
        const taiyin::Vector3 source_velocity = { 0.001, -0.002, 0.0005 };
        const taiyin::Vector3 source_acceleration = { -3e-5, 1e-5, 2e-5 };
        const taiyin::Vector3 observer_heliocentric_position = { 1.0, 0.2, -0.1 };
        const taiyin::Vector3 observer_heliocentric_velocity = { -0.001, 0.017, 0.0003 };
        const taiyin::Vector3 observer_heliocentric_acceleration = { -2e-5, -1e-6, 5e-7 };
        const taiyin::Vector3 observer_barycentric_velocity = { 0.0002, 0.016, 0.0005 };
        const taiyin::Vector3 observer_barycentric_acceleration = { -1e-5, 3e-6, 2e-6 };
        taiyin::Vector3 aberrated_position;
        taiyin::Vector3 aberrated_velocity;
        taiyin::Vector3 aberrated_acceleration;
        expect_true(
            taiyin::apply_annual_aberration_acceleration(
                source_position,
                source_velocity,
                source_acceleration,
                { 1.0, 0.0, 0.0 },
                { 0.0, 0.017, 0.0 },
                { 0.0, 0.0, 0.0 },
                { 0.0, 0.0, 0.0 },
                { 0.0, 0.0, 0.0 },
                taiyin::TAIYIN_LIGHT_TIME_DAYS_PER_AU,
                taiyin::TAIYIN_SOLAR_SCHWARZSCHILD_RADIUS_AU,
                &aberrated_position,
                &aberrated_velocity,
                &aberrated_acceleration),
            "annual aberration zero velocity succeeds",
            &failures);
        expect_vector_near(aberrated_position, source_position, 1e-15, "annual zero velocity position", &failures);

        taiyin::Vector3 old_position;
        taiyin::Vector3 old_velocity;
        expect_true(
            taiyin::apply_annual_aberration(
                source_position,
                source_velocity,
                observer_heliocentric_position,
                observer_heliocentric_velocity,
                observer_barycentric_velocity,
                observer_barycentric_acceleration,
                taiyin::TAIYIN_LIGHT_TIME_DAYS_PER_AU,
                taiyin::TAIYIN_SOLAR_SCHWARZSCHILD_RADIUS_AU,
                &old_position,
                &old_velocity),
            "annual old succeeds",
            &failures);
        expect_true(
            taiyin::apply_annual_aberration_acceleration(
                source_position,
                source_velocity,
                source_acceleration,
                observer_heliocentric_position,
                observer_heliocentric_velocity,
                observer_heliocentric_acceleration,
                observer_barycentric_velocity,
                observer_barycentric_acceleration,
                taiyin::TAIYIN_LIGHT_TIME_DAYS_PER_AU,
                taiyin::TAIYIN_SOLAR_SCHWARZSCHILD_RADIUS_AU,
                &aberrated_position,
                &aberrated_velocity,
                &aberrated_acceleration),
            "annual acceleration succeeds",
            &failures);
        expect_vector_near(aberrated_position, old_position, 0.0, "annual acceleration wrapper position", &failures);
        expect_vector_near(aberrated_velocity, old_velocity, 0.0, "annual acceleration wrapper velocity", &failures);

        const double h = 1e-4;
        taiyin::Vector3 previous_position;
        taiyin::Vector3 previous_velocity;
        taiyin::Vector3 next_position;
        taiyin::Vector3 next_velocity;
        expect_true(
            taiyin::apply_annual_aberration(
                constant_acceleration_position_at(source_position, source_velocity, source_acceleration, -h),
                constant_acceleration_velocity_at(source_velocity, source_acceleration, -h),
                constant_acceleration_position_at(observer_heliocentric_position, observer_heliocentric_velocity, observer_heliocentric_acceleration, -h),
                constant_acceleration_velocity_at(observer_heliocentric_velocity, observer_heliocentric_acceleration, -h),
                constant_acceleration_velocity_at(observer_barycentric_velocity, observer_barycentric_acceleration, -h),
                observer_barycentric_acceleration,
                taiyin::TAIYIN_LIGHT_TIME_DAYS_PER_AU,
                taiyin::TAIYIN_SOLAR_SCHWARZSCHILD_RADIUS_AU,
                &previous_position,
                &previous_velocity),
            "annual previous succeeds",
            &failures);
        expect_true(
            taiyin::apply_annual_aberration(
                constant_acceleration_position_at(source_position, source_velocity, source_acceleration, h),
                constant_acceleration_velocity_at(source_velocity, source_acceleration, h),
                constant_acceleration_position_at(observer_heliocentric_position, observer_heliocentric_velocity, observer_heliocentric_acceleration, h),
                constant_acceleration_velocity_at(observer_heliocentric_velocity, observer_heliocentric_acceleration, h),
                constant_acceleration_velocity_at(observer_barycentric_velocity, observer_barycentric_acceleration, h),
                observer_barycentric_acceleration,
                taiyin::TAIYIN_LIGHT_TIME_DAYS_PER_AU,
                taiyin::TAIYIN_SOLAR_SCHWARZSCHILD_RADIUS_AU,
                &next_position,
                &next_velocity),
            "annual next succeeds",
            &failures);
        expect_vector_near(
            aberrated_acceleration,
            taiyin::vector3_scale(taiyin::vector3_subtract(next_velocity, previous_velocity), 1.0 / (2.0 * h)),
            1e-14,
            "annual acceleration finite difference",
            &failures);
    }

    {
        const taiyin::Vector3 observer_position = { 1.0, 0.0, 0.0 };
        const taiyin::Vector3 observer_velocity = { 0.0, 0.017, 0.0 };
        const taiyin::Vector3 observer_acceleration = { -1e-5, 0.0, 2e-6 };
        const taiyin::Vector3 deflector_position = { 0.0, 0.0, 0.0 };
        const taiyin::Vector3 deflector_velocity = { 0.0, 0.0, 0.0 };
        const taiyin::Vector3 deflector_acceleration = { 0.0, 0.0, 0.0 };
        const taiyin::Vector3 source_position = { 0.2, 1.0, 0.1 };
        const taiyin::Vector3 source_velocity = { 0.0, 0.001, 0.0 };
        const taiyin::Vector3 source_acceleration = { 2e-5, -3e-5, 1e-5 };
        taiyin::Vector3 apparent_position = source_position;
        taiyin::Vector3 apparent_velocity = source_velocity;
        taiyin::Vector3 apparent_acceleration = source_acceleration;
        taiyin::Vector3 deflected_acceleration;
        const double before_norm = taiyin::vector3_norm(apparent_position);
        expect_true(
            taiyin::apply_gravitational_deflection_from_body_acceleration(
                apparent_position,
                apparent_velocity,
                apparent_acceleration,
                observer_position,
                observer_velocity,
                observer_acceleration,
                deflector_position,
                deflector_velocity,
                deflector_acceleration,
                source_position,
                source_velocity,
                source_acceleration,
                taiyin::TAIYIN_SOLAR_SCHWARZSCHILD_RADIUS_AU,
                taiyin::TAIYIN_SOLAR_DEFLECTION_LIMIT,
                &apparent_position,
                &apparent_velocity,
                &deflected_acceleration),
            "gravitational deflection succeeds",
            &failures);
        expect_near(taiyin::vector3_norm(apparent_position), before_norm, 1e-15, "deflection preserves distance", &failures);

        const double h = 1e-4;
        taiyin::Vector3 previous_position;
        taiyin::Vector3 previous_velocity;
        taiyin::Vector3 next_position;
        taiyin::Vector3 next_velocity;
        expect_true(
            taiyin::apply_gravitational_deflection_from_body(
                constant_acceleration_position_at(source_position, source_velocity, source_acceleration, -h),
                constant_acceleration_velocity_at(source_velocity, source_acceleration, -h),
                constant_acceleration_position_at(observer_position, observer_velocity, observer_acceleration, -h),
                constant_acceleration_velocity_at(observer_velocity, observer_acceleration, -h),
                constant_acceleration_position_at(deflector_position, deflector_velocity, deflector_acceleration, -h),
                constant_acceleration_velocity_at(deflector_velocity, deflector_acceleration, -h),
                constant_acceleration_position_at(source_position, source_velocity, source_acceleration, -h),
                constant_acceleration_velocity_at(source_velocity, source_acceleration, -h),
                taiyin::TAIYIN_SOLAR_SCHWARZSCHILD_RADIUS_AU,
                taiyin::TAIYIN_SOLAR_DEFLECTION_LIMIT,
                &previous_position,
                &previous_velocity),
            "deflection previous succeeds",
            &failures);
        expect_true(
            taiyin::apply_gravitational_deflection_from_body(
                constant_acceleration_position_at(source_position, source_velocity, source_acceleration, h),
                constant_acceleration_velocity_at(source_velocity, source_acceleration, h),
                constant_acceleration_position_at(observer_position, observer_velocity, observer_acceleration, h),
                constant_acceleration_velocity_at(observer_velocity, observer_acceleration, h),
                constant_acceleration_position_at(deflector_position, deflector_velocity, deflector_acceleration, h),
                constant_acceleration_velocity_at(deflector_velocity, deflector_acceleration, h),
                constant_acceleration_position_at(source_position, source_velocity, source_acceleration, h),
                constant_acceleration_velocity_at(source_velocity, source_acceleration, h),
                taiyin::TAIYIN_SOLAR_SCHWARZSCHILD_RADIUS_AU,
                taiyin::TAIYIN_SOLAR_DEFLECTION_LIMIT,
                &next_position,
                &next_velocity),
            "deflection next succeeds",
            &failures);
        expect_vector_near(
            deflected_acceleration,
            taiyin::vector3_scale(taiyin::vector3_subtract(next_velocity, previous_velocity), 1.0 / (2.0 * h)),
            1e-14,
            "deflection acceleration finite difference",
            &failures);
    }

    return failures == 0 ? 0 : 1;
}
