#include "taiyin/angle.h"
#include "taiyin/geometry.h"
#include "taiyin/vector3.h"

#include <cmath>
#include <iostream>

namespace {


bool near(double actual, double expected, double tol) {
    return std::fabs(actual - expected) <= tol;
}

double signed_angle_difference(double a, double b) {
    double diff = std::fmod(a - b, taiyin::TAIYIN_TWO_PI);
    if (diff > taiyin::TAIYIN_PI) {
        diff -= taiyin::TAIYIN_TWO_PI;
    } else if (diff < -taiyin::TAIYIN_PI) {
        diff += taiyin::TAIYIN_TWO_PI;
    }
    return diff;
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
        std::cerr << "FAIL: " << message << ": actual=" << actual << " expected=" << expected << "\n";
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

taiyin::Vector3 state_at(
    const taiyin::Vector3& position,
    const taiyin::Vector3& velocity,
    double dt_days
) {
    return taiyin::vector3_add(position, taiyin::vector3_scale(velocity, dt_days));
}

taiyin::Vector3 quadratic_position_at(
    const taiyin::Vector3& position,
    const taiyin::Vector3& velocity,
    const taiyin::Vector3& acceleration,
    double dt_days
) {
    return taiyin::vector3_add(
        taiyin::vector3_add(position, taiyin::vector3_scale(velocity, dt_days)),
        taiyin::vector3_scale(acceleration, 0.5 * dt_days * dt_days));
}

taiyin::Vector3 quadratic_velocity_at(
    const taiyin::Vector3& velocity,
    const taiyin::Vector3& acceleration,
    double dt_days
) {
    return taiyin::vector3_add(velocity, taiyin::vector3_scale(acceleration, dt_days));
}

}  // namespace

int main() {
    int failures = 0;

    {
        const taiyin::Vector3 position = { 0.72, -1.31, 0.44 };
        const taiyin::Vector3 velocity = { 0.0021, 0.0084, -0.0017 };
        taiyin::EclipticPositionVelocity actual;
        expect_true(
            taiyin::cartesian_position_velocity_to_ecliptic(position, velocity, &actual),
            "cartesian to ecliptic succeeds",
            &failures);

        const double h = 1.0e-5;
        taiyin::EclipticPositionVelocity previous;
        taiyin::EclipticPositionVelocity next;
        expect_true(
            taiyin::cartesian_position_velocity_to_ecliptic(state_at(position, velocity, -h), velocity, &previous),
            "previous ecliptic succeeds",
            &failures);
        expect_true(
            taiyin::cartesian_position_velocity_to_ecliptic(state_at(position, velocity, h), velocity, &next),
            "next ecliptic succeeds",
            &failures);

        expect_near(
            actual.longitude_rate_rad_per_day,
            signed_angle_difference(next.longitude_rad, previous.longitude_rad) / (2.0 * h),
            1e-11,
            "longitude rate finite difference",
            &failures);
        expect_near(
            actual.latitude_rate_rad_per_day,
            (next.latitude_rad - previous.latitude_rad) / (2.0 * h),
            1e-11,
            "latitude rate finite difference",
            &failures);
        expect_near(
            actual.radius_rate_au_per_day,
            (next.radius_au - previous.radius_au) / (2.0 * h),
            1e-10,
            "radius rate finite difference",
            &failures);
    }

    {
        const taiyin::Vector3 position = { 0.72, -1.31, 0.44 };
        const taiyin::Vector3 velocity = { 0.0021, 0.0084, -0.0017 };
        const taiyin::Vector3 acceleration = { -1.4e-5, 2.7e-5, 8.0e-6 };
        taiyin::EclipticPositionVelocityAcceleration actual;
        expect_true(
            taiyin::cartesian_position_velocity_acceleration_to_ecliptic(
                position,
                velocity,
                acceleration,
                &actual),
            "cartesian pva to ecliptic succeeds",
            &failures);

        taiyin::EclipticPositionVelocity first_order;
        expect_true(
            taiyin::cartesian_position_velocity_to_ecliptic(position, velocity, &first_order),
            "cartesian pva first-order reference succeeds",
            &failures);
        expect_near(
            signed_angle_difference(actual.longitude_rad, first_order.longitude_rad),
            0.0,
            1e-15,
            "pva longitude matches pv",
            &failures);
        expect_near(actual.latitude_rad, first_order.latitude_rad, 1e-15, "pva latitude matches pv", &failures);
        expect_near(actual.radius_au, first_order.radius_au, 1e-15, "pva radius matches pv", &failures);
        expect_near(
            actual.longitude_rate_rad_per_day,
            first_order.longitude_rate_rad_per_day,
            1e-15,
            "pva longitude rate matches pv",
            &failures);
        expect_near(
            actual.latitude_rate_rad_per_day,
            first_order.latitude_rate_rad_per_day,
            1e-15,
            "pva latitude rate matches pv",
            &failures);
        expect_near(
            actual.radius_rate_au_per_day,
            first_order.radius_rate_au_per_day,
            1e-15,
            "pva radius rate matches pv",
            &failures);

        const double h = 1.0e-4;
        taiyin::EclipticPositionVelocity previous;
        taiyin::EclipticPositionVelocity next;
        expect_true(
            taiyin::cartesian_position_velocity_to_ecliptic(
                quadratic_position_at(position, velocity, acceleration, -h),
                quadratic_velocity_at(velocity, acceleration, -h),
                &previous),
            "previous pva finite-difference ecliptic succeeds",
            &failures);
        expect_true(
            taiyin::cartesian_position_velocity_to_ecliptic(
                quadratic_position_at(position, velocity, acceleration, h),
                quadratic_velocity_at(velocity, acceleration, h),
                &next),
            "next pva finite-difference ecliptic succeeds",
            &failures);
        expect_near(
            actual.longitude_acceleration_rad_per_day2,
            (next.longitude_rate_rad_per_day - previous.longitude_rate_rad_per_day) / (2.0 * h),
            1e-10,
            "longitude acceleration finite difference",
            &failures);
        expect_near(
            actual.latitude_acceleration_rad_per_day2,
            (next.latitude_rate_rad_per_day - previous.latitude_rate_rad_per_day) / (2.0 * h),
            1e-10,
            "latitude acceleration finite difference",
            &failures);
        expect_near(
            actual.radius_acceleration_au_per_day2,
            (next.radius_rate_au_per_day - previous.radius_rate_au_per_day) / (2.0 * h),
            1e-10,
            "radius acceleration finite difference",
            &failures);
    }

    {
        const taiyin::Vector3 position = { 0.72, -1.31, 0.44 };
        const taiyin::Vector3 velocity = { 0.0021, 0.0084, -0.0017 };
        const taiyin::Vector3 acceleration = { -1.4e-5, 2.7e-5, 8.0e-6 };
        taiyin::EclipticPositionVelocityAcceleration spherical;
        taiyin::EquatorialPositionVelocityAcceleration equatorial;
        expect_true(
            taiyin::cartesian_position_velocity_acceleration_to_ecliptic(position, velocity, acceleration, &spherical),
            "spherical pva reference succeeds",
            &failures);
        expect_true(
            taiyin::cartesian_position_velocity_acceleration_to_equatorial(position, velocity, acceleration, &equatorial),
            "cartesian pva to equatorial succeeds",
            &failures);
        expect_near(
            signed_angle_difference(equatorial.right_ascension_rad, spherical.longitude_rad),
            0.0,
            1e-15,
            "equatorial right ascension matches spherical longitude",
            &failures);
        expect_near(equatorial.declination_rad, spherical.latitude_rad, 1e-15, "equatorial declination matches spherical latitude", &failures);
        expect_near(equatorial.distance_au, spherical.radius_au, 1e-15, "equatorial distance matches spherical radius", &failures);
        expect_near(equatorial.right_ascension_rate_rad_per_day, spherical.longitude_rate_rad_per_day, 1e-15, "equatorial right ascension rate", &failures);
        expect_near(equatorial.declination_rate_rad_per_day, spherical.latitude_rate_rad_per_day, 1e-15, "equatorial declination rate", &failures);
        expect_near(equatorial.distance_rate_au_per_day, spherical.radius_rate_au_per_day, 1e-15, "equatorial distance rate", &failures);
        expect_near(equatorial.right_ascension_acceleration_rad_per_day2, spherical.longitude_acceleration_rad_per_day2, 1e-15, "equatorial right ascension acceleration", &failures);
        expect_near(equatorial.declination_acceleration_rad_per_day2, spherical.latitude_acceleration_rad_per_day2, 1e-15, "equatorial declination acceleration", &failures);
        expect_near(equatorial.distance_acceleration_au_per_day2, spherical.radius_acceleration_au_per_day2, 1e-15, "equatorial distance acceleration", &failures);
    }

    {
        taiyin::EclipticPositionVelocity out;
        expect_true(
            !taiyin::cartesian_position_velocity_to_ecliptic({ 0.0, 0.0, 0.0 }, { 0.0, 0.0, 0.0 }, &out),
            "zero ecliptic vector rejected",
            &failures);
        expect_true(
            !taiyin::cartesian_position_velocity_to_ecliptic({ 0.0, 0.0, 1.0 }, { 0.0, 0.0, 0.0 }, &out),
            "ecliptic pole longitude singularity rejected",
            &failures);

        taiyin::EclipticPositionVelocityAcceleration pva_out;
        expect_true(
            !taiyin::cartesian_position_velocity_acceleration_to_ecliptic(
                { 0.0, 0.0, 0.0 },
                { 0.0, 0.0, 0.0 },
                { 0.0, 0.0, 0.0 },
                &pva_out),
            "zero pva ecliptic vector rejected",
            &failures);
        expect_true(
            !taiyin::cartesian_position_velocity_acceleration_to_ecliptic(
                { 0.0, 0.0, 1.0 },
                { 0.0, 0.0, 0.0 },
                { 0.0, 0.0, 0.0 },
                &pva_out),
            "pva ecliptic pole longitude singularity rejected",
            &failures);

        taiyin::EquatorialPositionVelocityAcceleration equatorial_out;
        expect_true(
            !taiyin::cartesian_position_velocity_acceleration_to_equatorial(
                { 0.0, 0.0, 1.0 },
                { 0.0, 0.0, 0.0 },
                { 0.0, 0.0, 0.0 },
                &equatorial_out),
            "equatorial pole right ascension singularity rejected",
            &failures);
    }

    {
        const double emrat = 81.3005682;
        const double factor = 1.0 / (1.0 + emrat);
        const taiyin::Vector3 emb_position = { 1.0, 2.0, 3.0 };
        const taiyin::Vector3 moon_position = { 0.1, -0.2, 0.3 };
        const taiyin::Vector3 expected_position = {
            emb_position.x - moon_position.x * factor,
            emb_position.y - moon_position.y * factor,
            emb_position.z - moon_position.z * factor,
        };
        expect_vector_near(
            taiyin::split_earth_from_emb_moon_position(emb_position, moon_position, emrat),
            expected_position,
            0.0,
            "split earth position",
            &failures);

        const taiyin::Vector3 emb_velocity = { 0.01, -0.02, 0.03 };
        const taiyin::Vector3 moon_velocity = { -0.004, 0.005, -0.006 };
        const taiyin::Vector3 expected_velocity = {
            emb_velocity.x - moon_velocity.x * factor,
            emb_velocity.y - moon_velocity.y * factor,
            emb_velocity.z - moon_velocity.z * factor,
        };
        expect_vector_near(
            taiyin::split_earth_from_emb_moon_velocity(emb_velocity, moon_velocity, emrat),
            expected_velocity,
            0.0,
            "split earth velocity",
            &failures);

        const taiyin::Vector3 emb_acceleration = { 1e-5, 2e-5, -3e-5 };
        const taiyin::Vector3 moon_acceleration = { 4e-6, -5e-6, 6e-6 };
        const taiyin::Vector3 expected_acceleration = {
            emb_acceleration.x - moon_acceleration.x * factor,
            emb_acceleration.y - moon_acceleration.y * factor,
            emb_acceleration.z - moon_acceleration.z * factor,
        };
        expect_vector_near(
            taiyin::split_earth_from_emb_moon_acceleration(emb_acceleration, moon_acceleration, emrat),
            expected_acceleration,
            0.0,
            "split earth acceleration",
            &failures);
    }

    {
        expect_near(
            taiyin::light_time_tau_from_distance(2.0, 0.005, 1e-8),
            0.01000001,
            0.0,
            "light time tau",
            &failures);

        double tau_dot = 0.0;
        expect_true(
            taiyin::light_time_tau_dot_no_shapiro(0.01, -0.02, 0.005, &tau_dot),
            "tau dot no shapiro succeeds",
            &failures);
        expect_near(
            tau_dot,
            0.005 * (0.01 - (-0.02)) / (1.0 + 0.005 * 0.01),
            0.0,
            "tau dot no shapiro value",
            &failures);
        expect_true(
            !taiyin::light_time_tau_dot_no_shapiro(-200.0, 0.0, 0.005, &tau_dot),
            "tau dot no shapiro zero denominator rejected",
            &failures);

        expect_true(
            taiyin::light_time_tau_dot_with_shapiro(
                0.01,
                -0.02,
                0.003,
                -0.004,
                0.005,
                -2e-10,
                5e-10,
                &tau_dot),
            "tau dot with shapiro succeeds",
            &failures);
        const double expected_with_shapiro = (
            0.005 * (0.01 - (-0.02))
            + (-2e-10) * (0.003 + (-0.004))
            + 5e-10 * (0.01 - (-0.02)))
            / (
                1.0
                + 0.005 * 0.01
                + (-2e-10) * (-0.004)
                + 5e-10 * 0.01);
        expect_near(tau_dot, expected_with_shapiro, 0.0, "tau dot with shapiro value", &failures);
    }

    return failures == 0 ? 0 : 1;
}
