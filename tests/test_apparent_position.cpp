#include "taiyin/angle.h"
#include "taiyin/apparent_position.h"
#include "taiyin/coordinates.h"
#include "taiyin/dispatch.h"
#include "taiyin/geometry.h"
#include "taiyin/vector3.h"

#include <cmath>
#include <iostream>

namespace {

using namespace taiyin;


struct LinearBlockData {
    Vector3 position0_au;
    Vector3 velocity_au_per_day;
    Vector3 acceleration_au_per_day2;
    double epoch_jd_tdb;
};

bool eval_linear_position(double jd_tdb, const void* data, Vector3* out) {
    if (!data || !out) {
        return false;
    }
    const LinearBlockData* block = static_cast<const LinearBlockData*>(data);
    const double dt = jd_tdb - block->epoch_jd_tdb;
    *out = vector3_add(
        vector3_add(block->position0_au, vector3_scale(block->velocity_au_per_day, dt)),
        vector3_scale(block->acceleration_au_per_day2, 0.5 * dt * dt));
    return true;
}

bool eval_linear_velocity(double jd_tdb, const void* data, Vector3* out) {
    if (!data || !out) {
        return false;
    }
    const LinearBlockData* block = static_cast<const LinearBlockData*>(data);
    const double dt = jd_tdb - block->epoch_jd_tdb;
    *out = vector3_add(block->velocity_au_per_day, vector3_scale(block->acceleration_au_per_day2, dt));
    return true;
}

bool eval_linear_acceleration(double, const void* data, Vector3* out) {
    if (!data || !out) {
        return false;
    }
    const LinearBlockData* block = static_cast<const LinearBlockData*>(data);
    *out = block->acceleration_au_per_day2;
    return true;
}

internal::CompiledEphemerisBlock make_linear_block(const LinearBlockData* data) {
    internal::CompiledEphemerisBlock block;
    block.data = data;
    block.bytes = sizeof(LinearBlockData);
    block.position = &eval_linear_position;
    block.velocity = &eval_linear_velocity;
    block.acceleration = &eval_linear_acceleration;
    block.format = internal::EphemerisBlockFormat::Kepler;
    return block;
}

void matrix_to_array(const Matrix3x3& matrix, double out[9]) {
    for (int row = 0; row < 3; ++row) {
        for (int col = 0; col < 3; ++col) {
            out[row * 3 + col] = matrix.m[row][col];
        }
    }
}

void identity_array(double out[9]) {
    for (int i = 0; i < 9; ++i) {
        out[i] = 0.0;
    }
    out[0] = 1.0;
    out[4] = 1.0;
    out[8] = 1.0;
}

void zero_array(double out[9]) {
    for (int i = 0; i < 9; ++i) {
        out[i] = 0.0;
    }
}

bool near(double actual, double expected, double tolerance) {
    return std::fabs(actual - expected) <= tolerance;
}

void expect_true(bool value, const char* label, int* failures) {
    if (!value) {
        std::cerr << "FAIL: expected true: " << label << "\n";
        ++(*failures);
    }
}

void expect_false(bool value, const char* label, int* failures) {
    if (value) {
        std::cerr << "FAIL: expected false: " << label << "\n";
        ++(*failures);
    }
}

void expect_near(double actual, double expected, double tolerance, const char* label, int* failures) {
    if (!near(actual, expected, tolerance)) {
        std::cerr << "FAIL: " << label << ": actual=" << actual << " expected=" << expected
                  << " tolerance=" << tolerance << "\n";
        ++(*failures);
    }
}

void expect_array3_near(const double actual[3], const double expected[3], double tolerance, const char* label, int* failures) {
    for (int i = 0; i < 3; ++i) {
        if (!near(actual[i], expected[i], tolerance)) {
            std::cerr << "FAIL: " << label << "[" << i << "]: actual=" << actual[i]
                      << " expected=" << expected[i] << " tolerance=" << tolerance << "\n";
            ++(*failures);
        }
    }
}

bool call_core(
    double jd_tdb,
    const internal::CompiledEphemerisBlock* target_block,
    const internal::CompiledEphemerisBlock* observer_block,
    uint32_t flags,
    const double matrix[9],
    const double matrix_dot[9],
    const double matrix_ddot[9],
    double out_geometric_pos[3],
    double out_geometric_vel[3],
    double out_geometric_acc[3],
    double out_astrometric_pos[3],
    double out_astrometric_vel[3],
    double out_astrometric_acc[3],
    double out_apparent_pos[3],
    double out_apparent_vel[3],
    double out_apparent_acc[3],
    double* out_lon,
    double* out_lat,
    double* out_distance,
    double* out_lon_rate,
    double* out_lat_rate,
    double* out_distance_rate,
    double* out_lon_acc,
    double* out_lat_acc,
    double* out_distance_acc,
    double* out_light_time,
    double* out_light_time_rate = 0,
    double* out_light_time_acc = 0
) {
    return taiyin_calc_apparent_with_matrix_flat(
        jd_tdb,
        0,  // target_id
        target_block,
        0,  // observer_id
        observer_block,
        0,
        0,
        0,
        0,
        -1,
        0,  // deflector_ids
        0,
        0,
        0,
        flags,
        0,
        0,
        0,
        0,
        8,
        1.0e-13,
        matrix,
        matrix_dot,
        matrix_ddot,
        out_geometric_pos,
        out_geometric_vel,
        out_geometric_acc,
        out_astrometric_pos,
        out_astrometric_vel,
        out_astrometric_acc,
        0,
        0,
        0,
        0,
        0,
        0,
        out_apparent_pos,
        out_apparent_vel,
        out_apparent_acc,
        out_lon,
        out_lat,
        out_distance,
        out_lon_rate,
        out_lat_rate,
        out_distance_rate,
        out_lon_acc,
        out_lat_acc,
        out_distance_acc,
        out_light_time,
        out_light_time_rate,
        out_light_time_acc,
        0);
}

bool call_convenience(
    double jd_tdb,
    double jd_tt,
    const internal::CompiledEphemerisBlock* target_block,
    const internal::CompiledEphemerisBlock* observer_block,
    uint32_t flags,
    int output_frame_id,
    double out_apparent_pos[3],
    double out_apparent_vel[3],
    double out_apparent_acc[3],
    double* out_lon,
    double* out_lat,
    double* out_distance,
    double* out_lon_rate,
    double* out_lat_rate,
    double* out_distance_rate,
    double* out_lon_acc,
    double* out_lat_acc,
    double* out_distance_acc
) {
    return taiyin_calc_apparent_flat(
        jd_tdb,
        jd_tt,
        0,  // target_id
        target_block,
        0,  // observer_id
        observer_block,
        0,
        0,
        0,
        0,
        -1,
        0,  // deflector_ids
        0,
        0,
        0,
        flags,
        output_frame_id,
        0,
        0,
        0,
        0,
        dispatch::PRECESSION_IAU2006,
        dispatch::NUTATION_IAU2000B,
        0,
        8,
        1.0e-13,
        0.5,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        out_apparent_pos,
        out_apparent_vel,
        out_apparent_acc,
        out_lon,
        out_lat,
        out_distance,
        out_lon_rate,
        out_lat_rate,
        out_distance_rate,
        out_lon_acc,
        out_lat_acc,
        out_distance_acc,
        0,
        0,
        0,
        0);
}

void test_geometric_icrf(int* failures) {
    LinearBlockData target = { { 2.0, 3.0, 4.0 }, { 0.2, 0.3, 0.4 }, { 0.02, 0.03, 0.04 }, 100.0 };
    LinearBlockData observer = { { 1.0, 1.0, 1.0 }, { 0.1, 0.1, 0.1 }, { 0.01, 0.01, 0.01 }, 100.0 };
    const internal::CompiledEphemerisBlock target_block = make_linear_block(&target);
    const internal::CompiledEphemerisBlock observer_block = make_linear_block(&observer);

    double geometric[3];
    double geometric_vel[3];
    double geometric_acc[3];
    double astrometric[3];
    double apparent[3];
    double light_time = -1.0;
    expect_true(
        call_core(
            100.0,
            &target_block,
            &observer_block,
            TAIYIN_APPARENT_VELOCITY | TAIYIN_APPARENT_ACCELERATION,
            0,
            0,
            0,
            geometric,
            geometric_vel,
            geometric_acc,
            astrometric,
            0,
            0,
            apparent,
            0,
            0,
            0,
            0,
            0,
            0,
            0,
            0,
            0,
            0,
            0,
            &light_time,
            0,
            0),
        "geometric ICRF succeeds",
        failures);

    const double expected_pos[3] = { 1.0, 2.0, 3.0 };
    const double expected_vel[3] = { 0.1, 0.2, 0.3 };
    const double expected_acc[3] = { 0.01, 0.02, 0.03 };
    expect_array3_near(geometric, expected_pos, 1.0e-15, "geometric", failures);
    expect_array3_near(geometric_vel, expected_vel, 1.0e-15, "geometric velocity", failures);
    expect_array3_near(geometric_acc, expected_acc, 1.0e-15, "geometric acceleration", failures);
    expect_array3_near(astrometric, expected_pos, 1.0e-15, "astrometric", failures);
    expect_array3_near(apparent, expected_pos, 1.0e-15, "apparent", failures);
    expect_near(light_time, 0.0, 1.0e-15, "geometric light time", failures);
}

void test_spherical_projection_and_rates(int* failures) {
    LinearBlockData target = { { 1.0, 0.0, 0.0 }, { 0.0, 1.0, 0.0 }, { -1.0, 0.0, 0.0 }, 100.0 };
    LinearBlockData observer = { { 0.0, 0.0, 0.0 }, { 0.0, 0.0, 0.0 }, { 0.0, 0.0, 0.0 }, 100.0 };
    const internal::CompiledEphemerisBlock target_block = make_linear_block(&target);
    const internal::CompiledEphemerisBlock observer_block = make_linear_block(&observer);

    double apparent[3];
    double apparent_vel[3];
    double apparent_acc[3];
    double lon = 0.0;
    double lat = 0.0;
    double distance = 0.0;
    double lon_rate = 0.0;
    double lat_rate = 0.0;
    double distance_rate = 0.0;
    double lon_acc = 0.0;
    double lat_acc = 0.0;
    double distance_acc = 0.0;
    expect_true(
        call_core(
            100.0,
            &target_block,
            &observer_block,
            TAIYIN_APPARENT_SPHERICAL | TAIYIN_APPARENT_VELOCITY | TAIYIN_APPARENT_ACCELERATION,
            0,
            0,
            0,
            0,
            0,
            0,
            0,
            0,
            0,
            apparent,
            apparent_vel,
            apparent_acc,
            &lon,
            &lat,
            &distance,
            &lon_rate,
            &lat_rate,
            &distance_rate,
            &lon_acc,
            &lat_acc,
            &distance_acc,
            0,
            0,
            0),
        "spherical projection succeeds",
        failures);

    const Vector3 position = { 1.0, 0.0, 0.0 };
    const Vector3 velocity = { 0.0, 1.0, 0.0 };
    const Vector3 acceleration = { -1.0, 0.0, 0.0 };
    EclipticPositionVelocityAcceleration expected;
    expect_true(
        cartesian_position_velocity_acceleration_to_ecliptic(position, velocity, acceleration, &expected),
        "reference spherical succeeds",
        failures);
    expect_near(lon, expected.longitude_rad, 1.0e-15, "spherical longitude", failures);
    expect_near(lat, expected.latitude_rad, 1.0e-15, "spherical latitude", failures);
    expect_near(distance, expected.radius_au, 1.0e-15, "spherical distance", failures);
    expect_near(lon_rate, expected.longitude_rate_rad_per_day, 1.0e-15, "spherical longitude rate", failures);
    expect_near(lat_rate, expected.latitude_rate_rad_per_day, 1.0e-15, "spherical latitude rate", failures);
    expect_near(distance_rate, expected.radius_rate_au_per_day, 1.0e-15, "spherical distance rate", failures);
    expect_near(lon_acc, expected.longitude_acceleration_rad_per_day2, 1.0e-15, "spherical longitude acceleration", failures);
    expect_near(lat_acc, expected.latitude_acceleration_rad_per_day2, 1.0e-15, "spherical latitude acceleration", failures);
    expect_near(distance_acc, expected.radius_acceleration_au_per_day2, 1.0e-15, "spherical distance acceleration", failures);
}

void test_manual_matrix_path(int* failures) {
    LinearBlockData target = { { 1.0, 0.0, 0.0 }, { 0.5, 0.0, 0.0 }, { 0.25, 0.0, 0.0 }, 100.0 };
    LinearBlockData observer = { { 0.0, 0.0, 0.0 }, { 0.0, 0.0, 0.0 }, { 0.0, 0.0, 0.0 }, 100.0 };
    const internal::CompiledEphemerisBlock target_block = make_linear_block(&target);
    const internal::CompiledEphemerisBlock observer_block = make_linear_block(&observer);

    double matrix[9];
    double matrix_dot[9];
    double matrix_ddot[9];
    matrix_to_array(rotation_z_matrix(taiyin::TAIYIN_PI / 2.0), matrix);
    zero_array(matrix_dot);
    zero_array(matrix_ddot);
    matrix_dot[0] = 2.0;
    matrix_ddot[0] = 3.0;

    double apparent[3];
    double apparent_vel[3];
    double apparent_acc[3];
    expect_true(
        call_core(
            100.0,
            &target_block,
            &observer_block,
            TAIYIN_APPARENT_USE_MATRIX | TAIYIN_APPARENT_VELOCITY | TAIYIN_APPARENT_ACCELERATION,
            matrix,
            matrix_dot,
            matrix_ddot,
            0,
            0,
            0,
            0,
            0,
            0,
            apparent,
            apparent_vel,
            apparent_acc,
            0,
            0,
            0,
            0,
            0,
            0,
            0,
            0,
            0,
            0,
            0,
            0),
        "manual matrix path succeeds",
        failures);

    const double expected_pos[3] = { 0.0, -1.0, 0.0 };
    const double expected_vel[3] = { 2.0, -0.5, 0.0 };
    const double expected_acc[3] = { 5.0, -0.25, 0.0 };
    expect_array3_near(apparent, expected_pos, 1.0e-14, "manual matrix apparent", failures);
    expect_array3_near(apparent_vel, expected_vel, 1.0e-14, "manual matrix velocity", failures);
    expect_array3_near(apparent_acc, expected_acc, 1.0e-14, "manual matrix acceleration", failures);
}

void test_convenience_matches_manual_matrix(int* failures) {
    LinearBlockData target = { { 1.2, -0.7, 0.4 }, { 0.001, 0.002, -0.003 }, { 0.0, 0.0, 0.0 }, 2451545.0 };
    LinearBlockData observer = { { 0.1, 0.2, -0.3 }, { -0.001, 0.004, 0.002 }, { 0.0, 0.0, 0.0 }, 2451545.0 };
    const internal::CompiledEphemerisBlock target_block = make_linear_block(&target);
    const internal::CompiledEphemerisBlock observer_block = make_linear_block(&observer);

    double output_matrix[9];
    double output_matrix_dot[9];
    expect_true(
        taiyin_calc_apparent_matrices_flat(
            2451545.0,
            TAIYIN_APPARENT_SPHERICAL | TAIYIN_APPARENT_VELOCITY,
            TAIYIN_APPARENT_FRAME_TRUE_ECLIPTIC_OF_DATE,
            dispatch::PRECESSION_IAU2006,
            dispatch::NUTATION_IAU2000B,
            0,
            0.5,
            0,
            0,
            output_matrix,
            output_matrix_dot,
            0,
            0,
            0,
            0,
            0),
        "prepare true ecliptic matrix succeeds",
        failures);

    double manual[3];
    double manual_vel[3];
    double manual_lon = 0.0;
    double manual_lat = 0.0;
    double manual_distance = 0.0;
    double manual_lon_rate = 0.0;
    expect_true(
        call_core(
            2451545.0,
            &target_block,
            &observer_block,
            TAIYIN_APPARENT_USE_MATRIX | TAIYIN_APPARENT_SPHERICAL | TAIYIN_APPARENT_VELOCITY,
            output_matrix,
            output_matrix_dot,
            0,
            0,
            0,
            0,
            0,
            0,
            0,
            manual,
            manual_vel,
            0,
            &manual_lon,
            &manual_lat,
            &manual_distance,
            &manual_lon_rate,
            0,
            0,
            0,
            0,
            0,
            0,
            0,
            0),
        "manual true ecliptic apparent succeeds",
        failures);

    double convenience[3];
    double convenience_vel[3];
    double convenience_lon = 0.0;
    double convenience_lat = 0.0;
    double convenience_distance = 0.0;
    double convenience_lon_rate = 0.0;
    expect_true(
        call_convenience(
            2451545.0,
            2451545.0,
            &target_block,
            &observer_block,
            TAIYIN_APPARENT_SPHERICAL | TAIYIN_APPARENT_VELOCITY,
            TAIYIN_APPARENT_FRAME_TRUE_ECLIPTIC_OF_DATE,
            convenience,
            convenience_vel,
            0,
            &convenience_lon,
            &convenience_lat,
            &convenience_distance,
            &convenience_lon_rate,
            0,
            0,
            0,
            0,
            0),
        "convenience true ecliptic apparent succeeds",
        failures);

    expect_array3_near(convenience, manual, 1.0e-15, "convenience vector", failures);
    expect_array3_near(convenience_vel, manual_vel, 1.0e-15, "convenience velocity", failures);
    expect_near(convenience_lon, manual_lon, 1.0e-15, "convenience longitude", failures);
    expect_near(convenience_lat, manual_lat, 1.0e-15, "convenience latitude", failures);
    expect_near(convenience_distance, manual_distance, 1.0e-15, "convenience distance", failures);
    expect_near(convenience_lon_rate, manual_lon_rate, 1.0e-15, "convenience longitude rate", failures);
}

void test_light_time_paths(int* failures) {
    LinearBlockData target = { { 2.0, 0.0, 0.0 }, { 0.1, 0.0, 0.0 }, { 0.001, 0.0, 0.0 }, 100.0 };
    LinearBlockData observer = { { 0.0, 0.0, 0.0 }, { 0.0, 0.01, 0.0 }, { 0.0, 0.0, 0.0001 }, 100.0 };
    const internal::CompiledEphemerisBlock target_block = make_linear_block(&target);
    const internal::CompiledEphemerisBlock observer_block = make_linear_block(&observer);

    double geometric[3];
    double astrometric[3];
    double astrometric_vel[3];
    double astrometric_acc[3];
    double apparent[3];
    double light_time = 0.0;
    double light_time_rate = 0.0;
    double light_time_acc = 0.0;
    expect_true(
        call_core(
            100.0,
            &target_block,
            &observer_block,
            TAIYIN_APPARENT_LIGHT_TIME | TAIYIN_APPARENT_VELOCITY | TAIYIN_APPARENT_ACCELERATION,
            0,
            0,
            0,
            geometric,
            0,
            0,
            astrometric,
            astrometric_vel,
            astrometric_acc,
            apparent,
            0,
            0,
            0,
            0,
            0,
            0,
            0,
            0,
            0,
            0,
            0,
            &light_time,
            &light_time_rate,
            &light_time_acc),
        "light-time apparent succeeds",
        failures);

    expect_near(geometric[0], 2.0, 1.0e-15, "light-time geometric x", failures);
    expect_true(light_time > 0.0, "light time positive", failures);
    expect_true(std::isfinite(light_time_rate), "light time rate finite", failures);
    expect_true(std::isfinite(light_time_acc), "light time acceleration finite", failures);
    expect_true(astrometric[0] < geometric[0], "retarded target is behind geometric target", failures);
    expect_true(std::isfinite(astrometric_vel[0]), "light-time velocity finite", failures);
    expect_true(std::isfinite(astrometric_acc[0]), "light-time acceleration vector finite", failures);
    expect_array3_near(apparent, astrometric, 1.0e-15, "light-time apparent equals astrometric", failures);
}

void test_topocentric_offset(int* failures) {
    LinearBlockData target = { { 2.0, 0.0, 0.0 }, { 0.2, 0.0, 0.0 }, { 0.02, 0.0, 0.0 }, 100.0 };
    LinearBlockData observer = { { 1.0, 0.0, 0.0 }, { 0.1, 0.0, 0.0 }, { 0.01, 0.0, 0.0 }, 100.0 };
    const internal::CompiledEphemerisBlock target_block = make_linear_block(&target);
    const internal::CompiledEphemerisBlock observer_block = make_linear_block(&observer);
    const double offset_pos[3] = { 0.25, 0.0, 0.0 };
    const double offset_vel[3] = { 0.025, 0.0, 0.0 };
    const double offset_acc[3] = { 0.0025, 0.0, 0.0 };

    double geometric[3];
    double geometric_vel[3];
    double geometric_acc[3];
    expect_true(
        taiyin_calc_apparent_with_matrix_flat(
            100.0,
            0,  // target_id
            &target_block,
            0,  // observer_id
            &observer_block,
            offset_pos,
            offset_vel,
            offset_acc,
            0,
            -1,
            0,  // deflector_ids
            0,
            0,
            0,
            TAIYIN_APPARENT_TOPOCENTRIC | TAIYIN_APPARENT_VELOCITY | TAIYIN_APPARENT_ACCELERATION,
            0,
            0,
            0,
            0,
            8,
            1.0e-13,
            0,
            0,
            0,
            geometric,
            geometric_vel,
            geometric_acc,
            0,
            0,
            0,
            0,
            0,
            0,
            0,
            0,
            0,
            0,
            0,
            0,
            0,
            0,
            0,
            0,
            0,
            0,
            0,
            0,
            0,
            0,
            0,
            0,
            0),
        "topocentric offset succeeds",
        failures);

    const double expected_pos[3] = { 0.75, 0.0, 0.0 };
    const double expected_vel[3] = { 0.075, 0.0, 0.0 };
    const double expected_acc[3] = { 0.0075, 0.0, 0.0 };
    expect_array3_near(geometric, expected_pos, 1.0e-15, "topocentric position", failures);
    expect_array3_near(geometric_vel, expected_vel, 1.0e-15, "topocentric velocity", failures);
    expect_array3_near(geometric_acc, expected_acc, 1.0e-15, "topocentric acceleration", failures);
}

void test_batch_manual_matrix_matches_single(int* failures) {
    LinearBlockData target0 = { { 1.0, 0.2, -0.1 }, { 0.01, -0.02, 0.03 }, { 0.001, 0.002, -0.001 }, 100.0 };
    LinearBlockData target1 = { { -0.3, 1.1, 0.4 }, { -0.02, 0.01, 0.005 }, { 0.0, -0.001, 0.002 }, 100.0 };
    LinearBlockData target2 = { { 0.8, -0.5, 1.3 }, { 0.004, 0.006, -0.008 }, { -0.001, 0.0, 0.001 }, 100.0 };
    LinearBlockData observer = { { 0.1, -0.2, 0.3 }, { 0.001, 0.002, -0.001 }, { 0.0001, -0.0002, 0.0003 }, 100.0 };
    const internal::CompiledEphemerisBlock target_block0 = make_linear_block(&target0);
    const internal::CompiledEphemerisBlock target_block1 = make_linear_block(&target1);
    const internal::CompiledEphemerisBlock target_block2 = make_linear_block(&target2);
    const internal::CompiledEphemerisBlock observer_block = make_linear_block(&observer);
    const internal::CompiledEphemerisBlock* targets[] = { &target_block0, &target_block1, &target_block2 };

    double matrix[9];
    double matrix_dot[9];
    double matrix_ddot[9];
    matrix_to_array(rotation_z_matrix(taiyin::TAIYIN_PI / 4.0), matrix);
    zero_array(matrix_dot);
    zero_array(matrix_ddot);
    matrix_dot[0] = 0.01;
    matrix_dot[4] = -0.02;
    matrix_ddot[0] = 0.03;
    matrix_ddot[4] = 0.04;

    const uint32_t flags = TAIYIN_APPARENT_USE_MATRIX
        | TAIYIN_APPARENT_SPHERICAL
        | TAIYIN_APPARENT_VELOCITY
        | TAIYIN_APPARENT_ACCELERATION;
    double batch_pos[9];
    double batch_vel[9];
    double batch_acc[9];
    double batch_lon[3];
    double batch_lat[3];
    double batch_distance[3];
    double batch_lon_rate[3];
    double batch_lat_rate[3];
    double batch_distance_rate[3];
    double batch_lon_acc[3];
    double batch_lat_acc[3];
    double batch_distance_acc[3];
    expect_true(
        taiyin_calc_apparent_batch_with_matrix_flat(
            100.0,
            3,
            (const int[]){ 0, 0, 0 },
            targets,
            0,
            &observer_block,
            0,
            0,
            0,
            0,
            -1,
            0,  // deflector_ids
            0,
            0,
            0,
            flags,
            0,
            0,
            0,
            0,
            8,
            1.0e-13,
            matrix,
            matrix_dot,
            matrix_ddot,
            batch_pos,
            batch_vel,
            batch_acc,
            batch_lon,
            batch_lat,
            batch_distance,
            batch_lon_rate,
            batch_lat_rate,
            batch_distance_rate,
            batch_lon_acc,
            batch_lat_acc,
            batch_distance_acc,
            0,
            0,
            0,
            0),
        "batch manual matrix succeeds",
        failures);

    for (int i = 0; i < 3; ++i) {
        double single_pos[3];
        double single_vel[3];
        double single_acc[3];
        double single_lon = 0.0;
        double single_lat = 0.0;
        double single_distance = 0.0;
        double single_lon_rate = 0.0;
        double single_lat_rate = 0.0;
        double single_distance_rate = 0.0;
        double single_lon_acc = 0.0;
        double single_lat_acc = 0.0;
        double single_distance_acc = 0.0;
        expect_true(
            call_core(
                100.0,
                targets[i],
                &observer_block,
                flags,
                matrix,
                matrix_dot,
                matrix_ddot,
                0,
                0,
                0,
                0,
                0,
                0,
                single_pos,
                single_vel,
                single_acc,
                &single_lon,
                &single_lat,
                &single_distance,
                &single_lon_rate,
                &single_lat_rate,
                &single_distance_rate,
                &single_lon_acc,
                &single_lat_acc,
                &single_distance_acc,
                0,
                0,
                0),
            "single manual matrix for batch comparison succeeds",
            failures);
        expect_array3_near(batch_pos + i * 3, single_pos, 1.0e-15, "batch apparent position", failures);
        expect_array3_near(batch_vel + i * 3, single_vel, 1.0e-15, "batch apparent velocity", failures);
        expect_array3_near(batch_acc + i * 3, single_acc, 1.0e-15, "batch apparent acceleration", failures);
        expect_near(batch_lon[i], single_lon, 1.0e-15, "batch longitude", failures);
        expect_near(batch_lat[i], single_lat, 1.0e-15, "batch latitude", failures);
        expect_near(batch_distance[i], single_distance, 1.0e-15, "batch distance", failures);
        expect_near(batch_lon_rate[i], single_lon_rate, 1.0e-15, "batch longitude rate", failures);
        expect_near(batch_lat_rate[i], single_lat_rate, 1.0e-15, "batch latitude rate", failures);
        expect_near(batch_distance_rate[i], single_distance_rate, 1.0e-15, "batch distance rate", failures);
        expect_near(batch_lon_acc[i], single_lon_acc, 1.0e-15, "batch longitude acceleration", failures);
        expect_near(batch_lat_acc[i], single_lat_acc, 1.0e-15, "batch latitude acceleration", failures);
        expect_near(batch_distance_acc[i], single_distance_acc, 1.0e-15, "batch distance acceleration", failures);
    }
}

void test_batch_convenience_and_light_time(int* failures) {
    LinearBlockData target0 = { { 2.0, 0.0, 0.0 }, { 0.10, 0.01, 0.0 }, { 0.001, 0.0, 0.0 }, 100.0 };
    LinearBlockData target1 = { { 1.0, 1.5, 0.2 }, { -0.03, 0.02, 0.01 }, { 0.0, -0.001, 0.0005 }, 100.0 };
    LinearBlockData observer = { { 0.1, -0.2, 0.3 }, { 0.0, 0.01, 0.0 }, { 0.0, 0.0, 0.0001 }, 100.0 };
    const internal::CompiledEphemerisBlock target_block0 = make_linear_block(&target0);
    const internal::CompiledEphemerisBlock target_block1 = make_linear_block(&target1);
    const internal::CompiledEphemerisBlock observer_block = make_linear_block(&observer);
    const internal::CompiledEphemerisBlock* targets[] = { &target_block0, &target_block1 };

    const uint32_t flags = TAIYIN_APPARENT_LIGHT_TIME | TAIYIN_APPARENT_SPHERICAL | TAIYIN_APPARENT_VELOCITY;
    double matrix[9];
    double matrix_dot[9];
    expect_true(
        taiyin_calc_apparent_matrices_flat(
            100.0,
            flags,
            TAIYIN_APPARENT_FRAME_TRUE_ECLIPTIC_OF_DATE,
            dispatch::PRECESSION_IAU2006,
            dispatch::NUTATION_IAU2000B,
            0,
            0.5,
            0,
            0,
            matrix,
            matrix_dot,
            0,
            0,
            0,
            0,
            0),
        "batch matrix preparation succeeds",
        failures);

    double with_matrix_pos[6];
    double with_matrix_vel[6];
    double with_matrix_lon[2];
    double with_matrix_lat[2];
    double with_matrix_distance[2];
    double with_matrix_lon_rate[2];
    double with_matrix_light_time[2];
    double with_matrix_light_time_rate[2];
    int with_matrix_iterations[2];
    expect_true(
        taiyin_calc_apparent_batch_with_matrix_flat(
            100.0,
            2,
            (const int[]){ 0, 0 },
            targets,
            0,
            &observer_block,
            0,
            0,
            0,
            0,
            -1,
            0,  // deflector_ids
            0,
            0,
            0,
            flags | TAIYIN_APPARENT_USE_MATRIX,
            0,
            0,
            0,
            0,
            8,
            1.0e-13,
            matrix,
            matrix_dot,
            0,
            with_matrix_pos,
            with_matrix_vel,
            0,
            with_matrix_lon,
            with_matrix_lat,
            with_matrix_distance,
            with_matrix_lon_rate,
            0,
            0,
            0,
            0,
            0,
            with_matrix_light_time,
            with_matrix_light_time_rate,
            0,
            with_matrix_iterations),
        "batch with matrix light-time succeeds",
        failures);

    double convenience_pos[6];
    double convenience_vel[6];
    double convenience_lon[2];
    double convenience_lat[2];
    double convenience_distance[2];
    double convenience_lon_rate[2];
    double convenience_light_time[2];
    double convenience_light_time_rate[2];
    int convenience_iterations[2];
    expect_true(
        taiyin_calc_apparent_batch_flat(
            100.0,
            100.0,
            2,
            (const int[]){ 0, 0 },
            targets,
            0,
            &observer_block,
            0,
            0,
            0,
            0,
            -1,
            0,  // deflector_ids
            0,
            0,
            0,
            flags,
            TAIYIN_APPARENT_FRAME_TRUE_ECLIPTIC_OF_DATE,
            0,
            0,
            0,
            0,
            dispatch::PRECESSION_IAU2006,
            dispatch::NUTATION_IAU2000B,
            0,
            8,
            1.0e-13,
            0.5,
            convenience_pos,
            convenience_vel,
            0,
            convenience_lon,
            convenience_lat,
            convenience_distance,
            convenience_lon_rate,
            0,
            0,
            0,
            0,
            0,
            convenience_light_time,
            convenience_light_time_rate,
            0,
            convenience_iterations),
        "batch convenience light-time succeeds",
        failures);

    for (int i = 0; i < 2; ++i) {
        expect_array3_near(convenience_pos + i * 3, with_matrix_pos + i * 3, 1.0e-15, "batch convenience position", failures);
        expect_array3_near(convenience_vel + i * 3, with_matrix_vel + i * 3, 1.0e-15, "batch convenience velocity", failures);
        expect_near(convenience_lon[i], with_matrix_lon[i], 1.0e-15, "batch convenience longitude", failures);
        expect_near(convenience_lat[i], with_matrix_lat[i], 1.0e-15, "batch convenience latitude", failures);
        expect_near(convenience_distance[i], with_matrix_distance[i], 1.0e-15, "batch convenience distance", failures);
        expect_near(convenience_lon_rate[i], with_matrix_lon_rate[i], 1.0e-15, "batch convenience longitude rate", failures);
        expect_near(convenience_light_time[i], with_matrix_light_time[i], 1.0e-15, "batch convenience light-time", failures);
        expect_near(convenience_light_time_rate[i], with_matrix_light_time_rate[i], 1.0e-15, "batch convenience light-time rate", failures);
        expect_true(convenience_iterations[i] == -1, "batch light-time iteration count reserved", failures);
    }
}

void test_validation(int* failures) {
    LinearBlockData target = { { 2.0, 0.0, 0.0 }, { 0.0, 0.0, 0.0 }, { 0.0, 0.0, 0.0 }, 100.0 };
    LinearBlockData observer = { { 0.0, 0.0, 0.0 }, { 0.0, 0.0, 0.0 }, { 0.0, 0.0, 0.0 }, 100.0 };
    const internal::CompiledEphemerisBlock target_block = make_linear_block(&target);
    const internal::CompiledEphemerisBlock observer_block = make_linear_block(&observer);
    double apparent[3];

    expect_false(
        call_core(100.0, 0, &observer_block, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, apparent, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0),
        "null target rejected",
        failures);
    expect_false(
        call_core(
            100.0,
            &target_block,
            &observer_block,
            TAIYIN_APPARENT_USE_MATRIX,
            0,
            0,
            0,
            0,
            0,
            0,
            0,
            0,
            0,
            apparent,
            0,
            0,
            0,
            0,
            0,
            0,
            0,
            0,
            0,
            0,
            0,
            0),
        "matrix flag with null matrix rejected",
        failures);
    expect_false(
        call_core(
            100.0,
            &target_block,
            &observer_block,
            TAIYIN_APPARENT_ABERRATION,
            0,
            0,
            0,
            0,
            0,
            0,
            0,
            0,
            0,
            apparent,
            0,
            0,
            0,
            0,
            0,
            0,
            0,
            0,
            0,
            0,
            0,
            0),
        "reserved aberration flag rejected",
        failures);
    expect_false(
        call_core(
            100.0,
            &target_block,
            &observer_block,
            TAIYIN_APPARENT_DEFLECTION,
            0,
            0,
            0,
            0,
            0,
            0,
            0,
            0,
            0,
            apparent,
            0,
            0,
            0,
            0,
            0,
            0,
            0,
            0,
            0,
            0,
            0,
            0),
        "reserved deflection flag rejected",
        failures);
    expect_false(
        call_core(
            100.0,
            &target_block,
            &observer_block,
            TAIYIN_APPARENT_SHAPIRO_DELAY,
            0,
            0,
            0,
            0,
            0,
            0,
            0,
            0,
            0,
            apparent,
            0,
            0,
            0,
            0,
            0,
            0,
            0,
            0,
            0,
            0,
            0,
            0),
        "reserved shapiro flag rejected",
        failures);
    expect_false(
        call_convenience(
            100.0,
            100.0,
            &target_block,
            &observer_block,
            0,
            99,
            apparent,
            0,
            0,
            0,
            0,
            0,
            0,
            0,
            0,
            0,
            0,
            0),
        "unknown output frame rejected",
        failures);

    const internal::CompiledEphemerisBlock* targets[] = { &target_block };
    const int target_ids[] = { 0 };
    expect_false(
        taiyin_calc_apparent_batch_with_matrix_flat(
            100.0,
            -1,
            0,
            targets,
            0,
            &observer_block,
            0,
            0,
            0,
            0,
            -1,
            0,
            0,
            0,
            0,
            0,
            0,
            0,
            0,
            0,
            8,
            1.0e-13,
            0,
            0,
            0,
            apparent,
            0,
            0,
            0,
            0,
            0,
            0,
            0,
            0,
            0,
            0,
            0,
            0,
            0,
            0,
            0),
        "negative batch target count rejected",
        failures);
    expect_true(
        taiyin_calc_apparent_batch_with_matrix_flat(
            100.0,
            0,
            0,
            0,
            0,
            &observer_block,
            0,
            0,
            0,
            0,
            -1,
            0,
            0,
            0,
            0,
            0,
            0,
            0,
            0,
            0,
            8,
            1.0e-13,
            0,
            0,
            0,
            0,
            0,
            0,
            0,
            0,
            0,
            0,
            0,
            0,
            0,
            0,
            0,
            0,
            0,
            0,
            0),
        "zero batch target count succeeds",
        failures);
    expect_false(
        taiyin_calc_apparent_batch_with_matrix_flat(
            100.0,
            1,
            target_ids,
            0,
            0,
            &observer_block,
            0,
            0,
            0,
            0,
            -1,
            0,
            0,
            0,
            0,
            0,
            0,
            0,
            0,
            0,
            8,
            1.0e-13,
            0,
            0,
            0,
            apparent,
            0,
            0,
            0,
            0,
            0,
            0,
            0,
            0,
            0,
            0,
            0,
            0,
            0,
            0,
            0),
        "null batch target array rejected",
        failures);
}

}  // namespace

int main() {
    int failures = 0;
    test_geometric_icrf(&failures);
    test_spherical_projection_and_rates(&failures);
    test_manual_matrix_path(&failures);
    test_convenience_matches_manual_matrix(&failures);
    test_light_time_paths(&failures);
    test_topocentric_offset(&failures);
    test_batch_manual_matrix_matches_single(&failures);
    test_batch_convenience_and_light_time(&failures);
    test_validation(&failures);

    if (failures == 0) {
        std::cout << "test_apparent_position: ALL TESTS PASSED\n";
        return 0;
    }
    std::cerr << failures << " test_apparent_position failure(s)\n";
    return 1;
}
