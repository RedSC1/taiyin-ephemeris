#include "taiyin/angle.h"
#include "taiyin/coordinates.h"
#include "taiyin/earth_rotation.h"
#include "taiyin/vector3.h"

#include <cmath>
#include <iostream>

namespace {


bool near(double actual, double expected, double tolerance) {
    return std::fabs(actual - expected) <= tolerance;
}

void expect_true(bool value, const char* message, int* failures) {
    if (!value) {
        std::cerr << "FAIL: " << message << "\n";
        ++(*failures);
    }
}

void expect_near(double actual, double expected, double tolerance, const char* message, int* failures) {
    if (!near(actual, expected, tolerance)) {
        std::cerr << "FAIL: " << message << ": actual=" << actual << " expected=" << expected << "\n";
        ++(*failures);
    }
}

void expect_vector_near(
    const taiyin::Vector3& actual,
    const taiyin::Vector3& expected,
    double tolerance,
    const char* message,
    int* failures
) {
    expect_near(actual.x, expected.x, tolerance, message, failures);
    expect_near(actual.y, expected.y, tolerance, message, failures);
    expect_near(actual.z, expected.z, tolerance, message, failures);
}

void expect_matrix_near(
    const taiyin::Matrix3x3& actual,
    const taiyin::Matrix3x3& expected,
    double tolerance,
    const char* message,
    int* failures
) {
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j) {
            expect_near(actual.m[i][j], expected.m[i][j], tolerance, message, failures);
        }
    }
}

void expect_nutation_near(
    const taiyin::NutationAngles& actual,
    const double expected[4],
    double tolerance,
    int* failures
) {
    expect_near(actual.dpsi_rad, expected[0], tolerance, "nutation dpsi", failures);
    expect_near(actual.deps_rad, expected[1], tolerance, "nutation deps", failures);
    expect_near(actual.mean_obliquity_rad, expected[2], tolerance, "mean obliquity", failures);
    expect_near(actual.true_obliquity_rad, expected[3], tolerance, "true obliquity", failures);
}

struct RotationZEvalData {
    double epoch_jd;
    double theta0_rad;
    double theta_rate_rad_per_day;
    double theta_acceleration_rad_per_day2;
};

bool rotation_z_eval(double jd, const void* data, taiyin::Matrix3x3* out_matrix) {
    if (!data || !out_matrix) {
        return false;
    }
    const RotationZEvalData* rotation = static_cast<const RotationZEvalData*>(data);
    const double dt = jd - rotation->epoch_jd;
    *out_matrix = taiyin::rotation_z_matrix(
        rotation->theta0_rad
        + rotation->theta_rate_rad_per_day * dt
        + 0.5 * rotation->theta_acceleration_rad_per_day2 * dt * dt);
    return true;
}

}  // namespace

int main() {
    int failures = 0;

    {
        const taiyin::Matrix3x3 identity = taiyin::matrix3x3_identity();
        const taiyin::Vector3 vector = { 1.0, -2.0, 3.0 };
        expect_vector_near(
            taiyin::matrix3x3_multiply_vector(identity, vector),
            vector,
            0.0,
            "identity multiply vector",
            &failures);

        const taiyin::Matrix3x3 rz = taiyin::rotation_z_matrix(taiyin::TAIYIN_PI / 2.0);
        const taiyin::Matrix3x3 rx = taiyin::rotation_x_matrix(taiyin::TAIYIN_PI / 2.0);
        const taiyin::Matrix3x3 ry = taiyin::rotation_y_matrix(taiyin::TAIYIN_PI / 2.0);
        expect_vector_near(
            taiyin::matrix3x3_multiply_vector(rx, { 0.0, 1.0, 0.0 }),
            { 0.0, 0.0, -1.0 },
            1e-15,
            "rotation x 90",
            &failures);
        expect_vector_near(
            taiyin::matrix3x3_multiply_vector(ry, { 0.0, 0.0, 1.0 }),
            { -1.0, 0.0, 0.0 },
            1e-15,
            "rotation y 90",
            &failures);
        expect_vector_near(
            taiyin::matrix3x3_multiply_vector(rz, { 1.0, 0.0, 0.0 }),
            { 0.0, -1.0, 0.0 },
            1e-15,
            "rotation z 90",
            &failures);
        expect_matrix_near(
            taiyin::matrix3x3_multiply(rz, taiyin::matrix3x3_transpose(rz)),
            identity,
            1e-15,
            "rotation inverse",
            &failures);
    }

    {
        const RotationZEvalData data = { 2460310.5, 0.7, 0.013, -2.1e-5 };
        const double h = 1e-4;
        taiyin::Matrix3x3 matrix_dot;
        taiyin::Matrix3x3 matrix_ddot;
        expect_true(
            taiyin::matrix_derivative_central(rotation_z_eval, &data, data.epoch_jd, h, &matrix_dot),
            "matrix derivative central succeeds",
            &failures);
        expect_true(
            taiyin::matrix_second_derivative_central(rotation_z_eval, &data, data.epoch_jd, h, &matrix_ddot),
            "matrix second derivative central succeeds",
            &failures);

        const double sin_theta = std::sin(data.theta0_rad);
        const double cos_theta = std::cos(data.theta0_rad);
        taiyin::Matrix3x3 expected_dot = {};
        expected_dot.m[0][0] = -sin_theta * data.theta_rate_rad_per_day;
        expected_dot.m[0][1] = cos_theta * data.theta_rate_rad_per_day;
        expected_dot.m[1][0] = -cos_theta * data.theta_rate_rad_per_day;
        expected_dot.m[1][1] = -sin_theta * data.theta_rate_rad_per_day;

        taiyin::Matrix3x3 expected_ddot = {};
        expected_ddot.m[0][0] = -sin_theta * data.theta_acceleration_rad_per_day2
            - cos_theta * data.theta_rate_rad_per_day * data.theta_rate_rad_per_day;
        expected_ddot.m[0][1] = cos_theta * data.theta_acceleration_rad_per_day2
            - sin_theta * data.theta_rate_rad_per_day * data.theta_rate_rad_per_day;
        expected_ddot.m[1][0] = -cos_theta * data.theta_acceleration_rad_per_day2
            + sin_theta * data.theta_rate_rad_per_day * data.theta_rate_rad_per_day;
        expected_ddot.m[1][1] = -sin_theta * data.theta_acceleration_rad_per_day2
            - cos_theta * data.theta_rate_rad_per_day * data.theta_rate_rad_per_day;

        expect_matrix_near(matrix_dot, expected_dot, 5e-8, "matrix central first derivative oracle", &failures);
        expect_matrix_near(matrix_ddot, expected_ddot, 1e-7, "matrix central second derivative oracle", &failures);
        expect_true(!taiyin::matrix_derivative_central(0, &data, data.epoch_jd, h, &matrix_dot), "matrix derivative null eval rejected", &failures);
        expect_true(!taiyin::matrix_second_derivative_central(rotation_z_eval, &data, data.epoch_jd, 0.0, &matrix_ddot), "matrix second derivative bad step rejected", &failures);
    }

    {
        struct NutationOracle {
            double jd_tt;
            double values[4];
        };
        const NutationOracle oracles[] = {
            { 2451545.0, { -0.000067542612539922361, -0.000027970923310985653, 0.40909260060058289, 0.40906462967727192 } },
            { 2460000.0, { -0.000044811878657808338, 0.000037607177053908570, 0.40904003706375935, 0.40907764424081328 } },
            { 2415020.5, { 0.000084518702696893369, -0.000011103153586824906, 0.40931965795344111, 0.40930855479985429 } },
        };
        for (int i = 0; i < static_cast<int>(sizeof(oracles) / sizeof(oracles[0])); ++i) {
            taiyin::NutationAngles actual;
            expect_true(taiyin::iau2000b_nutation(oracles[i].jd_tt, &actual), "iau2000b succeeds", &failures);
            expect_nutation_near(actual, oracles[i].values, 1e-14, &failures);
            expect_near(
                taiyin::mean_obliquity_iau2006(oracles[i].jd_tt),
                oracles[i].values[2],
                1e-14,
                "mean obliquity oracle",
                &failures);
        }
        expect_true(!taiyin::iau2000b_nutation(2451545.0, 0), "iau2000b null out rejected", &failures);
    }

    {
        const taiyin::NutationAngles zero_nutation = { 0.0, 0.0, 0.40909260060058289, 0.40909260060058289 };
        expect_matrix_near(
            taiyin::nutation_matrix(zero_nutation),
            taiyin::matrix3x3_identity(),
            1e-15,
            "zero nutation matrix",
            &failures);

        taiyin::NutationAngles nutation;
        expect_true(taiyin::iau2000b_nutation(2451545.0, &nutation), "iau2000b for matrix succeeds", &failures);
        const taiyin::Matrix3x3 matrix = taiyin::nutation_matrix(nutation);
        expect_matrix_near(
            taiyin::matrix3x3_multiply(matrix, taiyin::matrix3x3_transpose(matrix)),
            taiyin::matrix3x3_identity(),
            1e-14,
            "nutation matrix orthonormal",
            &failures);
    }

    {
        const double jd_tt = 2460000.0;
        taiyin::Matrix3x3 precession;
        taiyin::NutationAngles nutation;
        expect_true(taiyin::vondrak2011_precession_matrix(jd_tt, &precession), "reference precession succeeds", &failures);
        expect_true(taiyin::iau2000b_nutation(jd_tt, &nutation), "reference nutation succeeds", &failures);

        expect_matrix_near(
            taiyin::icrf_to_j2000_mean_equatorial_matrix(),
            taiyin::frame_bias_matrix(),
            0.0,
            "icrf to j2000 mean equatorial",
            &failures);
        expect_matrix_near(
            taiyin::icrf_to_j2000_ecliptic_matrix(),
            taiyin::j2000_ecliptic_matrix(),
            0.0,
            "icrf to j2000 ecliptic",
            &failures);
        expect_matrix_near(
            taiyin::icrf_to_true_equator_of_date_matrix(precession, nutation),
            taiyin::true_equator_of_date_matrix(precession, nutation),
            0.0,
            "icrf to true equator of date",
            &failures);
        expect_matrix_near(
            taiyin::icrf_to_cirs_iau2000b_matrix(jd_tt, precession, nutation),
            taiyin::matrix3x3_multiply(
                taiyin::rotation_z_matrix(-taiyin::equation_of_origins_iau2000b_rad(jd_tt)),
                taiyin::true_equator_of_date_matrix(precession, nutation)),
            1e-15,
            "icrf to cirs iau2000b",
            &failures);
        expect_matrix_near(
            taiyin::icrf_to_true_ecliptic_of_date_matrix(precession, nutation),
            taiyin::true_ecliptic_of_date_matrix(precession, nutation),
            0.0,
            "icrf to true ecliptic of date",
            &failures);

        const taiyin::Vector3 icrf_vector = { 0.2, -0.4, 0.7 };
        const taiyin::Matrix3x3 from = taiyin::icrf_to_j2000_ecliptic_matrix();
        const taiyin::Matrix3x3 to = taiyin::icrf_to_true_equator_of_date_matrix(precession, nutation);
        const taiyin::Matrix3x3 ecliptic_to_true_equator = taiyin::reference_plane_transform_matrix(from, to);
        expect_vector_near(
            taiyin::matrix3x3_multiply_vector(
                ecliptic_to_true_equator,
                taiyin::matrix3x3_multiply_vector(from, icrf_vector)),
            taiyin::matrix3x3_multiply_vector(to, icrf_vector),
            1e-15,
            "reference plane transform direction",
            &failures);
        expect_matrix_near(
            taiyin::matrix3x3_multiply(
                taiyin::reference_plane_transform_matrix(to, from),
                ecliptic_to_true_equator),
            taiyin::matrix3x3_identity(),
            1e-14,
            "reference plane transform roundtrip",
            &failures);
    }

    {
        struct EarthRotationOracle {
            double jd_ut1;
            double jd_tt;
            double era_rad;
            double gmst_rad;
        };
        const EarthRotationOracle erfa_oracles[] = {
            { 2451545.0, 2451545.0, 4.894961212823756, 4.8949612831508285 },
            { 2460000.0, 2460000.0008, 5.826127456378991, 5.831303984358957 },
            { 2440000.0, 2440000.0007, 1.0745425392021062, 1.0674755105895786 },
            { 2460409.26203588, 2460409.262837778, 1.9463758662917954, 1.9518029776572094 },
            { 2448001.749997685, 2448001.750661852, 5.204380593691198, 5.20221157346126 },
            { 2457754.5000046296, 2457754.500800741, 1.7561815779592962, 1.7599832590147506 },
        };
        for (int i = 0; i < static_cast<int>(sizeof(erfa_oracles) / sizeof(erfa_oracles[0])); ++i) {
            expect_near(
                taiyin::earth_rotation_angle_rad(erfa_oracles[i].jd_ut1),
                erfa_oracles[i].era_rad,
                1e-9,
                "ERA ERFA oracle",
                &failures);
            expect_near(
                taiyin::gmst_rad(erfa_oracles[i].jd_ut1, erfa_oracles[i].jd_tt),
                erfa_oracles[i].gmst_rad,
                1e-9,
                "GMST ERFA oracle",
                &failures);
            expect_near(
                taiyin::gmst_rad(erfa_oracles[i].jd_ut1, erfa_oracles[i].jd_tt),
                taiyin::normalize_radians(
                    taiyin::earth_rotation_angle_rad(erfa_oracles[i].jd_ut1)
                    + taiyin::gmst_minus_era_rad(erfa_oracles[i].jd_tt)),
                1e-15,
                "GMST composition",
                &failures);
            expect_near(
                taiyin::gmst_minus_era_rad(erfa_oracles[i].jd_tt),
                taiyin::normalize_signed_radians(erfa_oracles[i].gmst_rad - erfa_oracles[i].era_rad),
                1e-9,
                "GMST minus ERA oracle",
                &failures);
        }
    }

    return failures == 0 ? 0 : 1;
}
