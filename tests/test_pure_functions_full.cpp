#include "taiyin/angle.h"
#include "taiyin/coordinates.h"
#include "taiyin/earth_rotation.h"
#include "taiyin/observer.h"
#include "taiyin/time.h"
#include "taiyin/vector3.h"

#include <cmath>
#include <iostream>

// ERFA wrappers declared in internal header, forward-declare here for testing
namespace taiyin { namespace internal {
void erfa_pfw06(double jd_tt, double* gamb, double* phib, double* psib, double* epsa);
void erfa_fw2m(double gamb, double phib, double psi, double eps, double rm[3][3]);
}}

namespace {

void expect_near(double actual, double expected, double tolerance, const char* label, int* failures) {
    if (std::fabs(actual - expected) > tolerance) {
        std::cerr << "FAIL: " << label << " actual=" << actual << " expected=" << expected
                  << " diff=" << std::fabs(actual - expected) << "\n";
        ++(*failures);
    }
}

void expect_true(bool value, const char* label, int* failures) {
    if (!value) {
        std::cerr << "FAIL: " << label << "\n";
        ++(*failures);
    }
}

void expect_vector_near(
    const taiyin::Vector3& actual,
    const taiyin::Vector3& expected,
    double tolerance,
    const char* label,
    int* failures
) {
    expect_near(actual.x, expected.x, tolerance, label, failures);
    expect_near(actual.y, expected.y, tolerance, label, failures);
    expect_near(actual.z, expected.z, tolerance, label, failures);
}

void expect_matrix_near(
    const taiyin::Matrix3x3& actual,
    const double expected[3][3],
    double tolerance,
    const char* label,
    int* failures
) {
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j) {
            expect_near(actual.m[i][j], expected[i][j], tolerance, label, failures);
        }
    }
}

void expect_matrix_near(
    const taiyin::Matrix3x3& actual,
    const taiyin::Matrix3x3& expected,
    double tolerance,
    const char* label,
    int* failures
) {
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j) {
            expect_near(actual.m[i][j], expected.m[i][j], tolerance, label, failures);
        }
    }
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

taiyin::Matrix3x3 rotation_z_trajectory_matrix(
    double theta0,
    double theta_rate_rad_per_day,
    double theta_acceleration_rad_per_day2,
    double dt_days
) {
    return taiyin::rotation_z_matrix(
        theta0 + theta_rate_rad_per_day * dt_days + 0.5 * theta_acceleration_rad_per_day2 * dt_days * dt_days);
}

taiyin::Matrix3x3 matrix_first_derivative(
    const taiyin::Matrix3x3& previous,
    const taiyin::Matrix3x3& next,
    double h_days
) {
    return taiyin::matrix3x3_scale(taiyin::matrix3x3_subtract(next, previous), 1.0 / (2.0 * h_days));
}

taiyin::Matrix3x3 matrix_second_derivative(
    const taiyin::Matrix3x3& previous,
    const taiyin::Matrix3x3& current,
    const taiyin::Matrix3x3& next,
    double h_days
) {
    return taiyin::matrix3x3_scale(
        taiyin::matrix3x3_add(
            taiyin::matrix3x3_subtract(next, taiyin::matrix3x3_scale(current, 2.0)),
            previous),
        1.0 / (h_days * h_days));
}

double fourth_order_scalar_rate(double fm2, double fm1, double fp1, double fp2, double h_days) {
    return (-fp2 + 8.0 * fp1 - 8.0 * fm1 + fm2) / (12.0 * h_days);
}

double fourth_order_scalar_acceleration(double fm2, double fm1, double f0, double fp1, double fp2, double h_days) {
    return (-fp2 + 16.0 * fp1 - 30.0 * f0 + 16.0 * fm1 - fm2) / (12.0 * h_days * h_days);
}

double fourth_order_angle_rate(double fm2, double fm1, double f0, double fp1, double fp2, double h_days) {
    return (
        -taiyin::angular_difference_radians(fp2, f0)
        + 8.0 * taiyin::angular_difference_radians(fp1, f0)
        - 8.0 * taiyin::angular_difference_radians(fm1, f0)
        + taiyin::angular_difference_radians(fm2, f0)) / (12.0 * h_days);
}

double fourth_order_angle_acceleration(double fm2, double fm1, double f0, double fp1, double fp2, double h_days) {
    return (
        -taiyin::angular_difference_radians(fp2, f0)
        + 16.0 * taiyin::angular_difference_radians(fp1, f0)
        + 16.0 * taiyin::angular_difference_radians(fm1, f0)
        - taiyin::angular_difference_radians(fm2, f0)) / (12.0 * h_days * h_days);
}

taiyin::Vector3 vector_first_derivative(
    const taiyin::Vector3& previous,
    const taiyin::Vector3& next,
    double h_days
) {
    return taiyin::vector3_scale(taiyin::vector3_subtract(next, previous), 1.0 / (2.0 * h_days));
}

taiyin::Vector3 vector_second_derivative(
    const taiyin::Vector3& previous,
    const taiyin::Vector3& current,
    const taiyin::Vector3& next,
    double h_days
) {
    return taiyin::vector3_scale(
        taiyin::vector3_add(taiyin::vector3_subtract(next, taiyin::vector3_scale(current, 2.0)), previous),
        1.0 / (h_days * h_days));
}

double shifted_ut1(
    double jd_ut1,
    double dut1_rate_seconds_per_day,
    double lod_seconds,
    double lod_rate_seconds_per_day,
    double dt_days
) {
    const double ut1_rate_days_per_day = 1.0 + dut1_rate_seconds_per_day / taiyin::SECONDS_PER_DAY - lod_seconds / taiyin::SECONDS_PER_DAY;
    const double ut1_acceleration_days_per_day2 = -lod_rate_seconds_per_day / taiyin::SECONDS_PER_DAY;
    return jd_ut1 + ut1_rate_days_per_day * dt_days + 0.5 * ut1_acceleration_days_per_day2 * dt_days * dt_days;
}

struct MatrixOracle {
    double jd_tt;
    double values[3][3];
};

const MatrixOracle VONDRAK_ORACLES[] = {
    { 2451545.0, {
        { 1.0000000000000000, -7.0782797432736689e-8, 8.0561489398790301e-8 },
        { 7.0782797433127277e-8, 1.0000000000000000, 3.3055566297944602e-8 },
        { -8.0561489398447120e-8, -3.3055566297944596e-8, 1.0000000000000000 },
    } },
    { 2460000.0, {
        { 0.99998407267057809, -0.0051765046762749225, -0.0022490452445636257 },
        { 0.0051765048162935668, 0.99998660179260823, -0.0000057588873933755231 },
        { 0.0022490449222988648, -0.0000058833978765631700, 0.99999747087796709 },
    } },
    { 1219339.078000, {
        { 0.68473392912753224, 0.66647788221176470, 0.29486722236305385 },
        { -0.66669476463873305, 0.73625641199831485, -0.11595079385100924 },
        { -0.29437652267952261, -0.11719099075396052, 0.94847706065103425 },
    } },
};

struct CirsOracle {
    double jd_tt;
    double dpsi_rad;
    double deps_rad;
    double x_rad;
    double y_rad;
    double s_rad;
    double c2i[3][3];
};

const CirsOracle CIRS_ORACLES[] = {
    {
        2451545.0,
        -6.7544255989695129e-05, -2.7970831192374137e-05,
        -2.6946380149047226e-05, -2.8004721164746991e-05, -1.0133965177563562e-08,
        {
            { 0.99999999963694641, 9.7566522196112456e-09, 2.6946380432846096e-05 },
            { -1.0511278118007196e-08, 0.9999999996078679, 2.8004720891673316e-05 },
            { -2.694638014904723e-05, -2.8004721164746994e-05, 0.99999999924481409 },
        },
    },
    {
        2460310.5008007409,
        -2.5981687580008748e-05, 3.9112180825130383e-05,
        0.0023215120183531341, 3.2847333693040369e-05, -4.2787069519330131e-08,
        {
            { 0.99999730528734032, 4.6592781461469099e-09, -0.0023215120197585732 },
            { -8.0914745574427233e-08, 0.99999999946052631, -3.2847234362344221e-05 },
            { 0.0023215120183531341, 3.2847333693040369e-05, 0.99999730474786841 },
        },
    },
};

}  // namespace

int main() {
    int failures = 0;

    for (int i = 0; i < static_cast<int>(sizeof(VONDRAK_ORACLES) / sizeof(VONDRAK_ORACLES[0])); ++i) {
        taiyin::Matrix3x3 actual;
        expect_true(taiyin::vondrak2011_precession_matrix(VONDRAK_ORACLES[i].jd_tt, &actual), "vondrak succeeds", &failures);
        expect_matrix_near(actual, VONDRAK_ORACLES[i].values, 1e-12, "vondrak matrix oracle", &failures);
    }

    // IAU2006 precession vs ERFA pfw06+fw2m
    {
        const double test_jds[] = { 2451545.0, 2460310.5008007409, 1219339.078000 };
        for (int i = 0; i < 3; ++i) {
            double gamb, phib, psib, epsa;
            taiyin::internal::erfa_pfw06(test_jds[i], &gamb, &phib, &psib, &epsa);
            double erfa_rm[3][3];
            taiyin::internal::erfa_fw2m(gamb, phib, psib, epsa, erfa_rm);

            taiyin::Matrix3x3 ours;
            expect_true(taiyin::iau2006_precession_matrix(test_jds[i], &ours), "iau2006 precession succeeds", &failures);

            for (int r = 0; r < 3; ++r) {
                for (int c = 0; c < 3; ++c) {
                    double diff = std::fabs(ours.m[r][c] - erfa_rm[r][c]);
                    if (diff > 1e-14) {
                        std::fprintf(stderr, "FAIL: IAU2006 precession vs ERFA at JD=%.3f [%d][%d]: ours=%.17g erfa=%.17g diff=%.3e\n",
                                     test_jds[i], r, c, ours.m[r][c], erfa_rm[r][c], diff);
                        ++failures;
                    }
                }
            }
        }
    }

    for (int i = 0; i < static_cast<int>(sizeof(CIRS_ORACLES) / sizeof(CIRS_ORACLES[0])); ++i) {
        taiyin::NutationAngles nutation;
        expect_true(taiyin::iau2000a_nutation(CIRS_ORACLES[i].jd_tt, &nutation), "iau2000a succeeds", &failures);
        expect_near(nutation.dpsi_rad, CIRS_ORACLES[i].dpsi_rad, 2e-18, "iau2000a dpsi", &failures);
        expect_near(nutation.deps_rad, CIRS_ORACLES[i].deps_rad, 2e-18, "iau2000a deps", &failures);

        taiyin::CelestialIntermediatePole cip;
        expect_true(taiyin::iau2006a_cip_xy(CIRS_ORACLES[i].jd_tt, &cip), "cip xy succeeds", &failures);
        expect_near(cip.x_rad, CIRS_ORACLES[i].x_rad, 2e-18, "cip x", &failures);
        expect_near(cip.y_rad, CIRS_ORACLES[i].y_rad, 2e-18, "cip y", &failures);
        const double s = taiyin::cio_locator_s_iau2006a_rad(CIRS_ORACLES[i].jd_tt, cip.x_rad, cip.y_rad);
        expect_near(s, CIRS_ORACLES[i].s_rad, 2e-18, "cio s", &failures);
        taiyin::Matrix3x3 c2i;
        expect_true(taiyin::cirs_matrix_iau2006a(CIRS_ORACLES[i].jd_tt, 0.0, 0.0, &c2i), "cirs matrix succeeds", &failures);
        expect_matrix_near(c2i, CIRS_ORACLES[i].c2i, 2e-16, "cirs matrix oracle", &failures);
    }

    {
        const double theta0 = 0.7;
        const double theta_rate = 0.013;
        const double theta_acceleration = -2.1e-5;
        const double h = 1.0e-4;
        const taiyin::Vector3 position = { 0.8, -1.2, 0.35 };
        const taiyin::Vector3 velocity = { 0.002, 0.004, -0.001 };
        const taiyin::Vector3 acceleration = { -3.0e-5, 1.0e-5, 2.0e-5 };

        const taiyin::Matrix3x3 previous_matrix = rotation_z_trajectory_matrix(theta0, theta_rate, theta_acceleration, -h);
        const taiyin::Matrix3x3 matrix = rotation_z_trajectory_matrix(theta0, theta_rate, theta_acceleration, 0.0);
        const taiyin::Matrix3x3 next_matrix = rotation_z_trajectory_matrix(theta0, theta_rate, theta_acceleration, h);
        const taiyin::Matrix3x3 matrix_dot = matrix_first_derivative(previous_matrix, next_matrix, h);
        const taiyin::Matrix3x3 matrix_ddot = matrix_second_derivative(previous_matrix, matrix, next_matrix, h);

        const taiyin::Vector3 previous_transformed = taiyin::transform_position_with_matrix(
            quadratic_position_at(position, velocity, acceleration, -h),
            previous_matrix);
        const taiyin::Vector3 transformed = taiyin::transform_position_with_matrix(position, matrix);
        const taiyin::Vector3 next_transformed = taiyin::transform_position_with_matrix(
            quadratic_position_at(position, velocity, acceleration, h),
            next_matrix);

        expect_vector_near(
            taiyin::transform_velocity_with_matrix(position, velocity, matrix, matrix_dot),
            vector_first_derivative(previous_transformed, next_transformed, h),
            2e-10,
            "matrix transform velocity finite difference",
            &failures);
        expect_vector_near(
            taiyin::transform_acceleration_with_matrix(position, velocity, acceleration, matrix, matrix_dot, matrix_ddot),
            vector_second_derivative(previous_transformed, transformed, next_transformed, h),
            2e-8,
            "matrix transform acceleration finite difference",
            &failures);
    }

    {
        const double jd_tt = 2460310.5008007409;
        const double jd_ut1 = 2460310.5;
        const double dut1_rate = 1.3e-4;
        const double lod = 8.0e-4;
        const double lod_rate = -2.0e-5;
        const double h = 0.02;

        const double eq_m2 = taiyin::equation_of_equinoxes_rad(jd_tt - 2.0 * h);
        const double eq_m1 = taiyin::equation_of_equinoxes_rad(jd_tt - h);
        const double eq_0 = taiyin::equation_of_equinoxes_rad(jd_tt);
        const double eq_p1 = taiyin::equation_of_equinoxes_rad(jd_tt + h);
        const double eq_p2 = taiyin::equation_of_equinoxes_rad(jd_tt + 2.0 * h);
        expect_near(
            taiyin::equation_of_equinoxes_rate_rad_per_day(jd_tt, h),
            fourth_order_scalar_rate(eq_m2, eq_m1, eq_p1, eq_p2, h),
            5e-12,
            "equation of equinoxes rate finite difference",
            &failures);
        expect_near(
            taiyin::equation_of_equinoxes_acceleration_rad_per_day2(jd_tt, h),
            fourth_order_scalar_acceleration(eq_m2, eq_m1, eq_0, eq_p1, eq_p2, h),
            1e-12,
            "equation of equinoxes acceleration finite difference",
            &failures);

        const double gast_m2 = taiyin::gast_rad(
            shifted_ut1(jd_ut1, dut1_rate, lod, lod_rate, -2.0 * h),
            jd_tt - 2.0 * h);
        const double gast_m1 = taiyin::gast_rad(
            shifted_ut1(jd_ut1, dut1_rate, lod, lod_rate, -h),
            jd_tt - h);
        const double gast_0 = taiyin::gast_rad(jd_ut1, jd_tt);
        const double gast_p1 = taiyin::gast_rad(
            shifted_ut1(jd_ut1, dut1_rate, lod, lod_rate, h),
            jd_tt + h);
        const double gast_p2 = taiyin::gast_rad(
            shifted_ut1(jd_ut1, dut1_rate, lod, lod_rate, 2.0 * h),
            jd_tt + 2.0 * h);
        expect_near(
            taiyin::gast_rate_rad_per_day(jd_tt, dut1_rate, lod, h),
            fourth_order_angle_rate(gast_m2, gast_m1, gast_0, gast_p1, gast_p2, h),
            1e-7,
            "GAST rate finite difference",
            &failures);
        expect_near(
            taiyin::gast_acceleration_rad_per_day2(jd_tt, lod_rate, h),
            fourth_order_angle_acceleration(gast_m2, gast_m1, gast_0, gast_p1, gast_p2, h),
            1e-7,
            "GAST acceleration finite difference",
            &failures);
    }

    {
        const taiyin::Vector3 ecef_equator = taiyin::geodetic_to_ecef_m(0.0, 0.0, 0.0);
        expect_vector_near(ecef_equator, { 6378137.0, 0.0, 0.0 }, 1e-9, "geodetic equator", &failures);

        expect_vector_near(taiyin::local_east_itrf(0.0, 0.0), { 0.0, 1.0, 0.0 }, 1e-15, "local east equator Greenwich", &failures);
        expect_vector_near(taiyin::local_north_itrf(0.0, 0.0), { 0.0, 0.0, 1.0 }, 1e-15, "local north equator Greenwich", &failures);
        expect_vector_near(taiyin::local_up_itrf(0.0, 0.0), { 1.0, 0.0, 0.0 }, 1e-15, "local up equator Greenwich", &failures);
        const taiyin::Vector3 east = taiyin::local_east_itrf(taiyin::deg_to_rad(121.5), taiyin::deg_to_rad(31.2));
        const taiyin::Vector3 north = taiyin::local_north_itrf(taiyin::deg_to_rad(121.5), taiyin::deg_to_rad(31.2));
        const taiyin::Vector3 up = taiyin::local_up_itrf(taiyin::deg_to_rad(121.5), taiyin::deg_to_rad(31.2));
        expect_near(taiyin::vector3_norm(east), 1.0, 1e-15, "local east unit", &failures);
        expect_near(taiyin::vector3_norm(north), 1.0, 1e-15, "local north unit", &failures);
        expect_near(taiyin::vector3_norm(up), 1.0, 1e-15, "local up unit", &failures);
        expect_near(taiyin::vector3_dot(east, north), 0.0, 1e-15, "local east north orthogonal", &failures);
        expect_near(taiyin::vector3_dot(east, up), 0.0, 1e-15, "local east up orthogonal", &failures);
        expect_near(taiyin::vector3_dot(north, up), 0.0, 1e-15, "local north up orthogonal", &failures);

        const taiyin::HorizontalCoordinates zenith = taiyin::topocentric_position_to_horizontal({ 1.0, 0.0, 0.0 }, 0.0, 0.0);
        expect_near(zenith.azimuth_rad, 0.0, 1e-15, "zenith azimuth", &failures);
        expect_near(zenith.altitude_rad, taiyin::TAIYIN_PI / 2.0, 1e-15, "zenith altitude", &failures);

        const taiyin::Vector3 topocentric_position = { 1.0, 2.0, 3.0 };
        const taiyin::Vector3 topocentric_velocity = { 0.01, 0.02, 0.03 };
        const double sidereal = 0.4;
        const double sidereal_rate = 6.3;
        const double latitude = 0.5;
        const double h = 1.0e-5;
        taiyin::HorizontalRates rates;
        expect_true(
            taiyin::topocentric_velocity_to_horizontal_rates(
                topocentric_position,
                topocentric_velocity,
                sidereal,
                sidereal_rate,
                latitude,
                &rates),
            "horizontal rates succeeds",
            &failures);
        const taiyin::HorizontalCoordinates previous_horizontal = taiyin::topocentric_position_to_horizontal(
            taiyin::vector3_add(topocentric_position, taiyin::vector3_scale(topocentric_velocity, -h)),
            sidereal - sidereal_rate * h,
            latitude);
        const taiyin::HorizontalCoordinates next_horizontal = taiyin::topocentric_position_to_horizontal(
            taiyin::vector3_add(topocentric_position, taiyin::vector3_scale(topocentric_velocity, h)),
            sidereal + sidereal_rate * h,
            latitude);
        expect_near(
            rates.azimuth_rate_rad_per_day,
            taiyin::angular_difference_radians(next_horizontal.azimuth_rad, previous_horizontal.azimuth_rad) / (2.0 * h),
            1e-8,
            "horizontal azimuth rate finite difference",
            &failures);
        expect_near(
            rates.altitude_rate_rad_per_day,
            (next_horizontal.altitude_rad - previous_horizontal.altitude_rad) / (2.0 * h),
            1e-8,
            "horizontal altitude rate finite difference",
            &failures);
        expect_near(
            rates.distance_rate_au_per_day,
            (next_horizontal.distance_au - previous_horizontal.distance_au) / (2.0 * h),
            2e-10,
            "horizontal distance rate finite difference",
            &failures);
    }

    {
        const double longitude = taiyin::deg_to_rad(121.5);
        const double latitude = taiyin::deg_to_rad(31.2);
        const double height_m = 42.0;
        const double jd_ut1 = 2460310.5;
        const double jd_tt = 2460310.5008007409;
        const double xp = 0.12 / 3600.0 * taiyin::deg_to_rad(1.0);
        const double yp = -0.27 / 3600.0 * taiyin::deg_to_rad(1.0);
        const double sp = -4.7e-11;
        const double xp_rate = 1.0e-9;
        const double yp_rate = -2.0e-9;
        const double sp_rate = 3.0e-12;
        const double dut1_rate = 2.0e-4;
        const double lod = 7.0e-4;
        const double lod_rate = -1.0e-5;
        const double h = 1.0e-3;

        const taiyin::Vector3 simple_previous = taiyin::observer_geocentric_simple_position_au(
            longitude, latitude, height_m, jd_ut1 - h, jd_tt - h);
        const taiyin::Vector3 simple_position = taiyin::observer_geocentric_simple_position_au(
            longitude, latitude, height_m, jd_ut1, jd_tt);
        const taiyin::Vector3 simple_next = taiyin::observer_geocentric_simple_position_au(
            longitude, latitude, height_m, jd_ut1 + h, jd_tt + h);
        taiyin::Vector3 simple_velocity;
        taiyin::Vector3 simple_acceleration;
        expect_true(
            taiyin::observer_geocentric_simple_velocity_au_per_day(longitude, latitude, height_m, jd_ut1, jd_tt, &simple_velocity),
            "simple observer velocity succeeds",
            &failures);
        expect_true(
            taiyin::observer_geocentric_simple_acceleration_au_per_day2(longitude, latitude, height_m, jd_ut1, jd_tt, &simple_acceleration),
            "simple observer acceleration succeeds",
            &failures);
        expect_vector_near(
            simple_velocity,
            vector_first_derivative(simple_previous, simple_next, h),
            2e-9,
            "simple observer velocity finite difference",
            &failures);
        expect_vector_near(
            simple_acceleration,
            vector_second_derivative(simple_previous, simple_position, simple_next, h),
            2e-8,
            "simple observer acceleration finite difference",
            &failures);

        taiyin::Vector3 true_position;
        taiyin::Vector3 true_velocity;
        taiyin::Vector3 true_acceleration;
        taiyin::Vector3 true_previous;
        taiyin::Vector3 true_next;
        expect_true(
            taiyin::observer_geocentric_true_equator_of_date_position_au(
                longitude, latitude, height_m, shifted_ut1(jd_ut1, dut1_rate, lod, lod_rate, -h), jd_tt - h,
                xp - xp_rate * h, yp - yp_rate * h, sp - sp_rate * h, &true_previous),
            "true observer previous succeeds",
            &failures);
        expect_true(
            taiyin::observer_geocentric_true_equator_of_date_position_au(
                longitude, latitude, height_m, jd_ut1, jd_tt, xp, yp, sp, &true_position),
            "true observer position succeeds",
            &failures);
        expect_true(
            taiyin::observer_geocentric_true_equator_of_date_position_au(
                longitude, latitude, height_m, shifted_ut1(jd_ut1, dut1_rate, lod, lod_rate, h), jd_tt + h,
                xp + xp_rate * h, yp + yp_rate * h, sp + sp_rate * h, &true_next),
            "true observer next succeeds",
            &failures);
        expect_true(
            taiyin::observer_geocentric_true_equator_of_date_velocity_au_per_day(
                longitude, latitude, height_m, jd_ut1, jd_tt, xp, yp, sp, xp_rate, yp_rate, sp_rate,
                dut1_rate, lod, &true_velocity),
            "true observer velocity succeeds",
            &failures);
        expect_true(
            taiyin::observer_geocentric_true_equator_of_date_acceleration_au_per_day2(
                longitude, latitude, height_m, jd_ut1, jd_tt, xp, yp, sp, xp_rate, yp_rate, sp_rate,
                dut1_rate, lod, lod_rate, &true_acceleration),
            "true observer acceleration succeeds",
            &failures);
        expect_vector_near(
            true_velocity,
            vector_first_derivative(true_previous, true_next, h),
            2e-9,
            "true observer velocity finite difference",
            &failures);
        expect_vector_near(
            true_acceleration,
            vector_second_derivative(true_previous, true_position, true_next, h),
            2e-8,
            "true observer acceleration finite difference",
            &failures);

        taiyin::Vector3 cirs_position;
        taiyin::Vector3 cirs_velocity;
        taiyin::Vector3 cirs_acceleration;
        taiyin::Vector3 cirs_previous;
        taiyin::Vector3 cirs_next;
        expect_true(
            taiyin::observer_geocentric_cirs_position_au(
                longitude, latitude, height_m, shifted_ut1(jd_ut1, dut1_rate, lod, lod_rate, -h),
                xp - xp_rate * h, yp - yp_rate * h, sp - sp_rate * h, &cirs_previous),
            "cirs observer previous succeeds",
            &failures);
        expect_true(
            taiyin::observer_geocentric_cirs_position_au(
                longitude, latitude, height_m, jd_ut1, xp, yp, sp, &cirs_position),
            "cirs observer position succeeds",
            &failures);
        expect_true(
            taiyin::observer_geocentric_cirs_position_au(
                longitude, latitude, height_m, shifted_ut1(jd_ut1, dut1_rate, lod, lod_rate, h),
                xp + xp_rate * h, yp + yp_rate * h, sp + sp_rate * h, &cirs_next),
            "cirs observer next succeeds",
            &failures);
        expect_true(
            taiyin::observer_geocentric_cirs_velocity_au_per_day(
                longitude, latitude, height_m, jd_ut1, xp, yp, sp, xp_rate, yp_rate, sp_rate,
                dut1_rate, lod, &cirs_velocity),
            "cirs observer velocity succeeds",
            &failures);
        expect_true(
            taiyin::observer_geocentric_cirs_acceleration_au_per_day2(
                longitude, latitude, height_m, jd_ut1, xp, yp, sp, xp_rate, yp_rate, sp_rate,
                dut1_rate, lod, lod_rate, &cirs_acceleration),
            "cirs observer acceleration succeeds",
            &failures);
        expect_vector_near(
            cirs_velocity,
            vector_first_derivative(cirs_previous, cirs_next, h),
            2e-9,
            "cirs observer velocity finite difference",
            &failures);
        expect_vector_near(
            cirs_acceleration,
            vector_second_derivative(cirs_previous, cirs_position, cirs_next, h),
            2e-8,
            "cirs observer acceleration finite difference",
            &failures);
    }

    {
        struct RefractionOracle {
            taiyin::RefractionModel model;
            double altitude_deg;
            double refraction_rad;
        };
        const RefractionOracle oracles[] = {
            { taiyin::RefractionModel::Bennett, -0.75, 1.05406021000800949e-02 },
            { taiyin::RefractionModel::Bennett, 10.0, 1.57303058186004539e-03 },
            { taiyin::RefractionModel::Skyfield, 10.0, 1.55424545213107943e-03 },
            { taiyin::RefractionModel::Hybrid, 15.0, 1.05132761341894705e-03 },
            { taiyin::RefractionModel::AuerStandish, 10.0, 1.53160573791977749e-03 },
            { taiyin::RefractionModel::AuerStandish, 45.0, 2.80126797441743452e-04 },
        };
        for (int i = 0; i < static_cast<int>(sizeof(oracles) / sizeof(oracles[0])); ++i) {
            expect_near(
                taiyin::atmospheric_refraction_rad(taiyin::deg_to_rad(oracles[i].altitude_deg), 1010.0, 10.0, 0.0, 0.55, oracles[i].model),
                oracles[i].refraction_rad,
                2e-12,
                "refraction oracle",
                &failures);
        }

        const double sofa_unrefracted_altitude = 6.23813761152731683e-02;
        const double sofa_refracted_altitude = 6.55630118490333480e-02;
        expect_near(
            taiyin::atmospheric_refraction_sofa_rad(sofa_unrefracted_altitude, 1010.0, 10.0, 0.0, 0.55),
            sofa_refracted_altitude - sofa_unrefracted_altitude,
            1e-14,
            "sofa refraction oracle",
            &failures);
    }

    return failures == 0 ? 0 : 1;
}
