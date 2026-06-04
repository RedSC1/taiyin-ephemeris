#include "taiyin/apparent_position.h"

#include "taiyin/coordinates.h"
#include "taiyin/corrections.h"
#include "taiyin/dispatch.h"
#include "taiyin/geometry.h"
#include "taiyin/vector3.h"

#include <cmath>
#include <vector>

namespace taiyin {
namespace {

const uint32_t UNSUPPORTED_APPARENT_FLAGS = 0u;

struct MatrixEvalData {
    int output_frame_id;
    int precession_model_id;
    int nutation_model_id;
    int obliquity_model_id;
};

struct RelativeToDeflectorEvalData {
    const internal::CompiledEphemerisBlock* target_block;
    const internal::CompiledEphemerisBlock* deflector_block;
};

struct MultiDeflectorRelativeEvalData {
    const internal::CompiledEphemerisBlock* primary_block;
    const internal::CompiledEphemerisBlock* const* deflector_blocks;
    int deflector_count;
    int primary_deflector_index;
    int target_id;
    const int* deflector_ids;
};

bool need_velocity(uint32_t flags) noexcept {
    return (flags & (TAIYIN_APPARENT_VELOCITY | TAIYIN_APPARENT_ACCELERATION)) != 0u;
}

bool need_acceleration(uint32_t flags) noexcept {
    return (flags & TAIYIN_APPARENT_ACCELERATION) != 0u;
}

bool finite_vector(const Vector3& value) noexcept {
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

Vector3 zero_vector() noexcept {
    return Vector3{ 0.0, 0.0, 0.0 };
}

Vector3 vector_from_array(const double values[3]) noexcept {
    return values ? Vector3{ values[0], values[1], values[2] } : zero_vector();
}

void set_identity_array(double out[9]) noexcept {
    if (!out) {
        return;
    }
    for (int i = 0; i < 9; ++i) {
        out[i] = 0.0;
    }
    out[0] = 1.0;
    out[4] = 1.0;
    out[8] = 1.0;
}

void set_zero_array9(double out[9]) noexcept {
    if (!out) {
        return;
    }
    for (int i = 0; i < 9; ++i) {
        out[i] = 0.0;
    }
}

void copy_matrix_to_array(const Matrix3x3& matrix, double out[9]) noexcept {
    if (!out) {
        return;
    }
    for (int row = 0; row < 3; ++row) {
        for (int col = 0; col < 3; ++col) {
            out[row * 3 + col] = matrix.m[row][col];
        }
    }
}

Matrix3x3 matrix_from_array(const double values[9]) noexcept {
    Matrix3x3 matrix = matrix3x3_identity();
    if (!values) {
        return matrix;
    }
    for (int row = 0; row < 3; ++row) {
        for (int col = 0; col < 3; ++col) {
            matrix.m[row][col] = values[row * 3 + col];
        }
    }
    return matrix;
}

void copy_vector_to_array(const Vector3& vector, double out[3]) noexcept {
    if (!out) {
        return;
    }
    out[0] = vector.x;
    out[1] = vector.y;
    out[2] = vector.z;
}

bool valid_block_for_position(const internal::CompiledEphemerisBlock* block) noexcept {
    return block && block->data && block->position;
}

bool valid_block_for_velocity(const internal::CompiledEphemerisBlock* block) noexcept {
    return valid_block_for_position(block) && block->velocity;
}

bool valid_block_for_acceleration(const internal::CompiledEphemerisBlock* block) noexcept {
    return valid_block_for_velocity(block) && block->acceleration;
}

bool known_output_frame(int output_frame_id) noexcept {
    return output_frame_id == TAIYIN_APPARENT_FRAME_ICRF
        || output_frame_id == TAIYIN_APPARENT_FRAME_TRUE_EQUATOR_OF_DATE
        || output_frame_id == TAIYIN_APPARENT_FRAME_TRUE_ECLIPTIC_OF_DATE;
}

bool build_output_matrix(
    double jd_tt,
    int output_frame_id,
    int precession_model_id,
    int nutation_model_id,
    int obliquity_model_id,
    Matrix3x3* out_precession,
    Matrix3x3* out_nutation,
    Matrix3x3* out_output,
    NutationAngles* out_nutation_angles
) noexcept {
    if (!std::isfinite(jd_tt) || !out_output || !known_output_frame(output_frame_id)) {
        return false;
    }
    if (obliquity_model_id != 0) {
        return false;
    }

    if (out_precession) {
        *out_precession = matrix3x3_identity();
    }
    if (out_nutation) {
        *out_nutation = matrix3x3_identity();
    }
    *out_output = matrix3x3_identity();
    if (out_nutation_angles) {
        out_nutation_angles->dpsi_rad = 0.0;
        out_nutation_angles->deps_rad = 0.0;
        out_nutation_angles->mean_obliquity_rad = 0.0;
        out_nutation_angles->true_obliquity_rad = 0.0;
    }

    if (output_frame_id == TAIYIN_APPARENT_FRAME_ICRF) {
        return true;
    }

    Matrix3x3 precession = matrix3x3_identity();
    double mean_obliquity = 0.0;
    if (!dispatch::eval_precession(precession_model_id, jd_tt, 0, &precession, &mean_obliquity)) {
        return false;
    }

    NutationAngles nutation;
    nutation.dpsi_rad = 0.0;
    nutation.deps_rad = 0.0;
    nutation.mean_obliquity_rad = mean_obliquity;
    nutation.true_obliquity_rad = mean_obliquity;
    if (!dispatch::eval_nutation(nutation_model_id, jd_tt, 0, &nutation)) {
        return false;
    }
    nutation.mean_obliquity_rad = mean_obliquity;
    nutation.true_obliquity_rad = mean_obliquity + nutation.deps_rad;

    const Matrix3x3 nutation_mat = nutation_matrix(nutation);
    const Matrix3x3 output = output_frame_id == TAIYIN_APPARENT_FRAME_TRUE_EQUATOR_OF_DATE
        ? icrf_to_true_equator_of_date_matrix(precession, nutation)
        : icrf_to_true_ecliptic_of_date_matrix(precession, nutation);

    if (out_precession) {
        *out_precession = precession;
    }
    if (out_nutation) {
        *out_nutation = nutation_mat;
    }
    *out_output = output;
    if (out_nutation_angles) {
        *out_nutation_angles = nutation;
    }
    return true;
}

bool eval_output_matrix(double jd_tt, const void* data, Matrix3x3* out_matrix) noexcept {
    const MatrixEvalData* eval_data = static_cast<const MatrixEvalData*>(data);
    if (!eval_data || !out_matrix) {
        return false;
    }
    return build_output_matrix(
        jd_tt,
        eval_data->output_frame_id,
        eval_data->precession_model_id,
        eval_data->nutation_model_id,
        eval_data->obliquity_model_id,
        0,
        0,
        out_matrix,
        0);
}

bool eval_relative_position(double jd_tdb, const void* data, Vector3* out) noexcept {
    const RelativeToDeflectorEvalData* eval_data = static_cast<const RelativeToDeflectorEvalData*>(data);
    if (!eval_data || !out) {
        return false;
    }
    Vector3 target;
    Vector3 deflector;
    if (!internal::eval_compiled_ephemeris_block_position(jd_tdb, eval_data->target_block, &target)
        || !internal::eval_compiled_ephemeris_block_position(jd_tdb, eval_data->deflector_block, &deflector)) {
        return false;
    }
    *out = vector3_subtract(target, deflector);
    return finite_vector(*out);
}

bool eval_relative_velocity(double jd_tdb, const void* data, Vector3* out) noexcept {
    const RelativeToDeflectorEvalData* eval_data = static_cast<const RelativeToDeflectorEvalData*>(data);
    if (!eval_data || !out) {
        return false;
    }
    Vector3 target;
    Vector3 deflector;
    if (!internal::eval_compiled_ephemeris_block_velocity(jd_tdb, eval_data->target_block, &target)
        || !internal::eval_compiled_ephemeris_block_velocity(jd_tdb, eval_data->deflector_block, &deflector)) {
        return false;
    }
    *out = vector3_subtract(target, deflector);
    return finite_vector(*out);
}

bool eval_relative_acceleration(double jd_tdb, const void* data, Vector3* out) noexcept {
    const RelativeToDeflectorEvalData* eval_data = static_cast<const RelativeToDeflectorEvalData*>(data);
    if (!eval_data || !out) {
        return false;
    }
    Vector3 target;
    Vector3 deflector;
    if (!internal::eval_compiled_ephemeris_block_acceleration(jd_tdb, eval_data->target_block, &target)
        || !internal::eval_compiled_ephemeris_block_acceleration(jd_tdb, eval_data->deflector_block, &deflector)) {
        return false;
    }
    *out = vector3_subtract(target, deflector);
    return finite_vector(*out);
}

bool eval_deflector_relative_position(double jd_tdb, const void* data, int deflector_index, Vector3* out) noexcept {
    const MultiDeflectorRelativeEvalData* eval_data = static_cast<const MultiDeflectorRelativeEvalData*>(data);
    if (!eval_data || !out || deflector_index < 0 || deflector_index >= eval_data->deflector_count) {
        return false;
    }
    if (deflector_index == eval_data->primary_deflector_index) {
        *out = zero_vector();
        return true;
    }
    if (eval_data->deflector_ids && eval_data->deflector_ids[deflector_index] == eval_data->target_id) {
        *out = zero_vector();
        return true;
    }
    Vector3 deflector;
    Vector3 primary;
    if (!internal::eval_compiled_ephemeris_block_position(jd_tdb, eval_data->deflector_blocks[deflector_index], &deflector)
        || !internal::eval_compiled_ephemeris_block_position(jd_tdb, eval_data->primary_block, &primary)) {
        return false;
    }
    *out = vector3_subtract(deflector, primary);
    return finite_vector(*out);
}

bool eval_deflector_relative_velocity(double jd_tdb, const void* data, int deflector_index, Vector3* out) noexcept {
    const MultiDeflectorRelativeEvalData* eval_data = static_cast<const MultiDeflectorRelativeEvalData*>(data);
    if (!eval_data || !out || deflector_index < 0 || deflector_index >= eval_data->deflector_count) {
        return false;
    }
    if (deflector_index == eval_data->primary_deflector_index) {
        *out = zero_vector();
        return true;
    }
    if (eval_data->deflector_ids && eval_data->deflector_ids[deflector_index] == eval_data->target_id) {
        *out = zero_vector();
        return true;
    }
    Vector3 deflector;
    Vector3 primary;
    if (!internal::eval_compiled_ephemeris_block_velocity(jd_tdb, eval_data->deflector_blocks[deflector_index], &deflector)
        || !internal::eval_compiled_ephemeris_block_velocity(jd_tdb, eval_data->primary_block, &primary)) {
        return false;
    }
    *out = vector3_subtract(deflector, primary);
    return finite_vector(*out);
}

bool eval_deflector_relative_acceleration(double jd_tdb, const void* data, int deflector_index, Vector3* out) noexcept {
    const MultiDeflectorRelativeEvalData* eval_data = static_cast<const MultiDeflectorRelativeEvalData*>(data);
    if (!eval_data || !out || deflector_index < 0 || deflector_index >= eval_data->deflector_count) {
        return false;
    }
    if (deflector_index == eval_data->primary_deflector_index) {
        *out = zero_vector();
        return true;
    }
    if (eval_data->deflector_ids && eval_data->deflector_ids[deflector_index] == eval_data->target_id) {
        *out = zero_vector();
        return true;
    }
    Vector3 deflector;
    Vector3 primary;
    if (!internal::eval_compiled_ephemeris_block_acceleration(jd_tdb, eval_data->deflector_blocks[deflector_index], &deflector)
        || !internal::eval_compiled_ephemeris_block_acceleration(jd_tdb, eval_data->primary_block, &primary)) {
        return false;
    }
    *out = vector3_subtract(deflector, primary);
    return finite_vector(*out);
}

bool eval_observer_state(
    double jd_tdb,
    const internal::CompiledEphemerisBlock* observer_block,
    uint32_t flags,
    CartesianState* out
) noexcept {
    if (!out || !valid_block_for_position(observer_block)) {
        return false;
    }
    *out = CartesianState();
    if (!internal::eval_compiled_ephemeris_block_position(jd_tdb, observer_block, &out->position_au)) {
        return false;
    }
    if (need_velocity(flags)
        && !internal::eval_compiled_ephemeris_block_velocity(jd_tdb, observer_block, &out->velocity_au_per_day)) {
        return false;
    }
    if (need_acceleration(flags)
        && !internal::eval_compiled_ephemeris_block_acceleration(jd_tdb, observer_block, &out->acceleration_au_per_day2)) {
        return false;
    }
    if (!finite_vector(out->position_au)
        || (need_velocity(flags) && !finite_vector(out->velocity_au_per_day))
        || (need_acceleration(flags) && !finite_vector(out->acceleration_au_per_day2))) {
        return false;
    }
    return true;
}

void apply_observer_offset(
    uint32_t flags,
    const double observer_offset_pos_au[3],
    const double observer_offset_vel_au_per_day[3],
    const double observer_offset_acc_au_per_day2[3],
    CartesianState* observer
) noexcept {
    if (!observer || (flags & TAIYIN_APPARENT_TOPOCENTRIC) == 0u) {
        return;
    }
    observer->position_au = vector3_add(observer->position_au, vector_from_array(observer_offset_pos_au));
    if (need_velocity(flags)) {
        observer->velocity_au_per_day = vector3_add(observer->velocity_au_per_day, vector_from_array(observer_offset_vel_au_per_day));
    }
    if (need_acceleration(flags)) {
        observer->acceleration_au_per_day2 = vector3_add(observer->acceleration_au_per_day2, vector_from_array(observer_offset_acc_au_per_day2));
    }
}

bool valid_deflector_inputs(
    int deflector_count,
    int solar_deflector_index,
    const internal::CompiledEphemerisBlock* const* deflector_blocks,
    const double* deflector_schwarzschild_radius_au,
    const double* deflector_limit,
    uint32_t flags
) noexcept {
    if (deflector_count < 0) {
        return false;
    }
    if (solar_deflector_index < -1 || solar_deflector_index >= deflector_count) {
        return false;
    }
    if (deflector_count > 0 && !deflector_blocks) {
        return false;
    }
    if ((flags & TAIYIN_APPARENT_DEFLECTION) != 0u) {
        if (deflector_count <= 0 || !deflector_blocks || !deflector_schwarzschild_radius_au || !deflector_limit) {
            return false;
        }
        for (int i = 0; i < deflector_count; ++i) {
            if (!valid_block_for_position(deflector_blocks[i])
                || (need_velocity(flags) && !valid_block_for_velocity(deflector_blocks[i]))
                || (need_acceleration(flags) && !valid_block_for_acceleration(deflector_blocks[i]))
                || !std::isfinite(deflector_schwarzschild_radius_au[i])
                || !std::isfinite(deflector_limit[i])) {
                return false;
            }
        }
    }
    if ((flags & TAIYIN_APPARENT_SHAPIRO_DELAY) != 0u) {
        if (deflector_count <= 0
            || solar_deflector_index < 0
            || !deflector_blocks
            || !deflector_schwarzschild_radius_au) {
            return false;
        }
        for (int i = 0; i < deflector_count; ++i) {
            if (!valid_block_for_position(deflector_blocks[i])
                || (need_velocity(flags) && !valid_block_for_velocity(deflector_blocks[i]))
                || (need_acceleration(flags) && !valid_block_for_acceleration(deflector_blocks[i]))
                || !std::isfinite(deflector_schwarzschild_radius_au[i])
                || deflector_schwarzschild_radius_au[i] < 0.0) {
                return false;
            }
        }
    }
    if ((flags & TAIYIN_APPARENT_ABERRATION) != 0u) {
        if (deflector_count <= 0
            || solar_deflector_index < 0
            || !deflector_blocks
            || !deflector_schwarzschild_radius_au
            || !valid_block_for_position(deflector_blocks[solar_deflector_index])
            || !std::isfinite(deflector_schwarzschild_radius_au[solar_deflector_index])) {
            return false;
        }
        if (need_velocity(flags) && !valid_block_for_velocity(deflector_blocks[solar_deflector_index])) {
            return false;
        }
        if (need_acceleration(flags) && !valid_block_for_acceleration(deflector_blocks[solar_deflector_index])) {
            return false;
        }
    }
    return true;
}

bool compute_spherical_outputs(
    uint32_t flags,
    const Vector3& position,
    const Vector3& velocity,
    const Vector3& acceleration,
    double* out_lon_rad,
    double* out_lat_rad,
    double* out_distance_au,
    double* out_lon_rate_rad_per_day,
    double* out_lat_rate_rad_per_day,
    double* out_distance_rate_au_per_day,
    double* out_lon_acc_rad_per_day2,
    double* out_lat_acc_rad_per_day2,
    double* out_distance_acc_au_per_day2
) noexcept {
    if ((flags & TAIYIN_APPARENT_SPHERICAL) == 0u) {
        return true;
    }
    if (need_acceleration(flags)) {
        EclipticPositionVelocityAcceleration spherical;
        if (!cartesian_position_velocity_acceleration_to_ecliptic(position, velocity, acceleration, &spherical)) {
            return false;
        }
        if (out_lon_rad) *out_lon_rad = spherical.longitude_rad;
        if (out_lat_rad) *out_lat_rad = spherical.latitude_rad;
        if (out_distance_au) *out_distance_au = spherical.radius_au;
        if (out_lon_rate_rad_per_day) *out_lon_rate_rad_per_day = spherical.longitude_rate_rad_per_day;
        if (out_lat_rate_rad_per_day) *out_lat_rate_rad_per_day = spherical.latitude_rate_rad_per_day;
        if (out_distance_rate_au_per_day) *out_distance_rate_au_per_day = spherical.radius_rate_au_per_day;
        if (out_lon_acc_rad_per_day2) *out_lon_acc_rad_per_day2 = spherical.longitude_acceleration_rad_per_day2;
        if (out_lat_acc_rad_per_day2) *out_lat_acc_rad_per_day2 = spherical.latitude_acceleration_rad_per_day2;
        if (out_distance_acc_au_per_day2) *out_distance_acc_au_per_day2 = spherical.radius_acceleration_au_per_day2;
        return true;
    }
    if (need_velocity(flags)) {
        EclipticPositionVelocity spherical;
        if (!cartesian_position_velocity_to_ecliptic(position, velocity, &spherical)) {
            return false;
        }
        if (out_lon_rad) *out_lon_rad = spherical.longitude_rad;
        if (out_lat_rad) *out_lat_rad = spherical.latitude_rad;
        if (out_distance_au) *out_distance_au = spherical.radius_au;
        if (out_lon_rate_rad_per_day) *out_lon_rate_rad_per_day = spherical.longitude_rate_rad_per_day;
        if (out_lat_rate_rad_per_day) *out_lat_rate_rad_per_day = spherical.latitude_rate_rad_per_day;
        if (out_distance_rate_au_per_day) *out_distance_rate_au_per_day = spherical.radius_rate_au_per_day;
        return true;
    }
    cartesian_to_spherical(position, out_lon_rad, out_lat_rad, out_distance_au);
    return true;
}

}  // namespace

bool taiyin_calc_apparent_matrices_flat(
    double jd_tt,
    uint32_t flags,
    int output_frame_id,
    int precession_model_id,
    int nutation_model_id,
    int obliquity_model_id,
    double matrix_derivative_step_days,
    double out_precession_matrix[9],
    double out_nutation_matrix[9],
    double out_output_matrix[9],
    double out_output_matrix_dot[9],
    double out_output_matrix_ddot[9],
    double* out_mean_obliquity_rad,
    double* out_true_obliquity_rad,
    double* out_nutation_dpsi_rad,
    double* out_nutation_deps_rad
) noexcept {
    set_identity_array(out_precession_matrix);
    set_identity_array(out_nutation_matrix);
    set_identity_array(out_output_matrix);
    set_zero_array9(out_output_matrix_dot);
    set_zero_array9(out_output_matrix_ddot);
    if (out_mean_obliquity_rad) *out_mean_obliquity_rad = 0.0;
    if (out_true_obliquity_rad) *out_true_obliquity_rad = 0.0;
    if (out_nutation_dpsi_rad) *out_nutation_dpsi_rad = 0.0;
    if (out_nutation_deps_rad) *out_nutation_deps_rad = 0.0;

    if (!out_output_matrix) {
        return false;
    }
    if (need_velocity(flags) && !out_output_matrix_dot) {
        return false;
    }
    if (need_acceleration(flags) && !out_output_matrix_ddot) {
        return false;
    }

    Matrix3x3 precession = matrix3x3_identity();
    Matrix3x3 nut_matrix = matrix3x3_identity();
    Matrix3x3 output_matrix = matrix3x3_identity();
    NutationAngles nutation;
    if (!build_output_matrix(
            jd_tt,
            output_frame_id,
            precession_model_id,
            nutation_model_id,
            obliquity_model_id,
            &precession,
            &nut_matrix,
            &output_matrix,
            &nutation)) {
        return false;
    }

    copy_matrix_to_array(precession, out_precession_matrix);
    copy_matrix_to_array(nut_matrix, out_nutation_matrix);
    copy_matrix_to_array(output_matrix, out_output_matrix);
    if (out_mean_obliquity_rad) *out_mean_obliquity_rad = nutation.mean_obliquity_rad;
    if (out_true_obliquity_rad) *out_true_obliquity_rad = nutation.true_obliquity_rad;
    if (out_nutation_dpsi_rad) *out_nutation_dpsi_rad = nutation.dpsi_rad;
    if (out_nutation_deps_rad) *out_nutation_deps_rad = nutation.deps_rad;

    if (output_frame_id == TAIYIN_APPARENT_FRAME_ICRF) {
        return true;
    }
    if ((need_velocity(flags) || need_acceleration(flags)) && matrix_derivative_step_days <= 0.0) {
        return false;
    }

    MatrixEvalData eval_data;
    eval_data.output_frame_id = output_frame_id;
    eval_data.precession_model_id = precession_model_id;
    eval_data.nutation_model_id = nutation_model_id;
    eval_data.obliquity_model_id = obliquity_model_id;

    if (need_velocity(flags)) {
        Matrix3x3 matrix_dot;
        if (!matrix_derivative_central(&eval_output_matrix, &eval_data, jd_tt, matrix_derivative_step_days, &matrix_dot)) {
            return false;
        }
        copy_matrix_to_array(matrix_dot, out_output_matrix_dot);
    }
    if (need_acceleration(flags)) {
        Matrix3x3 matrix_ddot;
        if (!matrix_second_derivative_central(&eval_output_matrix, &eval_data, jd_tt, matrix_derivative_step_days, &matrix_ddot)) {
            return false;
        }
        copy_matrix_to_array(matrix_ddot, out_output_matrix_ddot);
    }
    return true;
}

bool taiyin_calc_apparent_with_matrix_flat(
    double jd_tdb,
    int target_id,
    const internal::CompiledEphemerisBlock* target_block,
    int observer_id,
    const internal::CompiledEphemerisBlock* observer_block,
    const double observer_offset_pos_au[3],
    const double observer_offset_vel_au_per_day[3],
    const double observer_offset_acc_au_per_day2[3],
    int deflector_count,
    int solar_deflector_index,
    const int* deflector_ids,
    const internal::CompiledEphemerisBlock* const* deflector_blocks,
    const double* deflector_schwarzschild_radius_au,
    const double* deflector_limit,
    uint32_t flags,
    int light_time_method_id,
    int shapiro_delay_model_id,
    int aberration_model_id,
    int deflection_model_id,
    int max_light_time_iterations,
    double light_time_tolerance_days,
    const double output_matrix[9],
    const double output_matrix_dot[9],
    const double output_matrix_ddot[9],
    double out_geometric_pos_au[3],
    double out_geometric_vel_au_per_day[3],
    double out_geometric_acc_au_per_day2[3],
    double out_astrometric_pos_au[3],
    double out_astrometric_vel_au_per_day[3],
    double out_astrometric_acc_au_per_day2[3],
    double out_deflected_pos_au[3],
    double out_deflected_vel_au_per_day[3],
    double out_deflected_acc_au_per_day2[3],
    double out_aberrated_pos_au[3],
    double out_aberrated_vel_au_per_day[3],
    double out_aberrated_acc_au_per_day2[3],
    double out_apparent_pos_au[3],
    double out_apparent_vel_au_per_day[3],
    double out_apparent_acc_au_per_day2[3],
    double* out_lon_rad,
    double* out_lat_rad,
    double* out_distance_au,
    double* out_lon_rate_rad_per_day,
    double* out_lat_rate_rad_per_day,
    double* out_distance_rate_au_per_day,
    double* out_lon_acc_rad_per_day2,
    double* out_lat_acc_rad_per_day2,
    double* out_distance_acc_au_per_day2,
    double* out_light_time_days,
    double* out_light_time_rate,
    double* out_light_time_acceleration,
    int* out_light_time_iterations
) noexcept {
    uint32_t observer_eval_flags = flags;
    if (deflector_count > 0 && !deflector_ids) {
        return false;
    }
    if ((flags & TAIYIN_APPARENT_ABERRATION) != 0u) {
        observer_eval_flags |= TAIYIN_APPARENT_VELOCITY;
        if (need_velocity(flags)) {
            observer_eval_flags |= TAIYIN_APPARENT_ACCELERATION;
        }
    }

    if (!std::isfinite(jd_tdb)
        || !valid_block_for_position(target_block)
        || !valid_block_for_position(observer_block)) {
        return false;
    }
    if (need_velocity(flags) && !valid_block_for_velocity(target_block)) {
        return false;
    }
    if (need_velocity(observer_eval_flags) && !valid_block_for_velocity(observer_block)) {
        return false;
    }
    if (need_acceleration(flags) && !valid_block_for_acceleration(target_block)) {
        return false;
    }
    if (need_acceleration(observer_eval_flags) && !valid_block_for_acceleration(observer_block)) {
        return false;
    }
    if ((flags & UNSUPPORTED_APPARENT_FLAGS) != 0u) {
        return false;
    }
    if (light_time_method_id != 0 || shapiro_delay_model_id != 0 || aberration_model_id != 0 || deflection_model_id != 0) {
        return false;
    }
    if (!valid_deflector_inputs(
            deflector_count,
            solar_deflector_index,
            deflector_blocks,
            deflector_schwarzschild_radius_au,
            deflector_limit,
            flags)) {
        return false;
    }
    if ((flags & TAIYIN_APPARENT_USE_MATRIX) != 0u) {
        if (!output_matrix) {
            return false;
        }
        if (need_velocity(flags) && !output_matrix_dot) {
            return false;
        }
        if (need_acceleration(flags) && !output_matrix_ddot) {
            return false;
        }
    }

    CartesianState observer;
    if (!eval_observer_state(jd_tdb, observer_block, observer_eval_flags, &observer)) {
        return false;
    }
    apply_observer_offset(
        observer_eval_flags,
        observer_offset_pos_au,
        observer_offset_vel_au_per_day,
        observer_offset_acc_au_per_day2,
        &observer);

    CartesianState target;
    target = CartesianState();
    if (!internal::eval_compiled_ephemeris_block_position(jd_tdb, target_block, &target.position_au)) {
        return false;
    }
    if (need_velocity(flags) && !internal::eval_compiled_ephemeris_block_velocity(jd_tdb, target_block, &target.velocity_au_per_day)) {
        return false;
    }
    if (need_acceleration(flags) && !internal::eval_compiled_ephemeris_block_acceleration(jd_tdb, target_block, &target.acceleration_au_per_day2)) {
        return false;
    }
    if (!finite_vector(target.position_au)
        || (need_velocity(flags) && !finite_vector(target.velocity_au_per_day))
        || (need_acceleration(flags) && !finite_vector(target.acceleration_au_per_day2))) {
        return false;
    }

    CartesianState geometric;
    geometric.position_au = vector3_subtract(target.position_au, observer.position_au);
    geometric.velocity_au_per_day = need_velocity(flags)
        ? vector3_subtract(target.velocity_au_per_day, observer.velocity_au_per_day)
        : zero_vector();
    geometric.acceleration_au_per_day2 = need_acceleration(flags)
        ? vector3_subtract(target.acceleration_au_per_day2, observer.acceleration_au_per_day2)
        : zero_vector();
    if (!finite_vector(geometric.position_au) || vector3_norm(geometric.position_au) == 0.0) {
        return false;
    }
    copy_vector_to_array(geometric.position_au, out_geometric_pos_au);
    if (need_velocity(flags)) copy_vector_to_array(geometric.velocity_au_per_day, out_geometric_vel_au_per_day);
    if (need_acceleration(flags)) copy_vector_to_array(geometric.acceleration_au_per_day2, out_geometric_acc_au_per_day2);

    CartesianState astrometric = geometric;
    double light_time_days = 0.0;
    double light_time_rate = 0.0;
    double light_time_acceleration = 0.0;
    if ((flags & TAIYIN_APPARENT_SHAPIRO_DELAY) != 0u
        && (flags & TAIYIN_APPARENT_LIGHT_TIME) == 0u) {
        return false;
    }
    if ((flags & TAIYIN_APPARENT_LIGHT_TIME) != 0u) {
        if ((flags & TAIYIN_APPARENT_SHAPIRO_DELAY) != 0u) {
            const internal::CompiledEphemerisBlock* solar_block = deflector_blocks[solar_deflector_index];
            CartesianState solar;
            solar = CartesianState();
            if (!internal::eval_compiled_ephemeris_block_position(jd_tdb, solar_block, &solar.position_au)) {
                return false;
            }
            if (need_velocity(flags)
                && !internal::eval_compiled_ephemeris_block_velocity(jd_tdb, solar_block, &solar.velocity_au_per_day)) {
                return false;
            }
            if (need_acceleration(flags)
                && !internal::eval_compiled_ephemeris_block_acceleration(jd_tdb, solar_block, &solar.acceleration_au_per_day2)) {
                return false;
            }
            if (!finite_vector(solar.position_au)
                || (need_velocity(flags) && !finite_vector(solar.velocity_au_per_day))
                || (need_acceleration(flags) && !finite_vector(solar.acceleration_au_per_day2))) {
                return false;
            }

            RelativeToDeflectorEvalData eval_data;
            eval_data.target_block = target_block;
            eval_data.deflector_block = solar_block;
            MultiDeflectorRelativeEvalData multi_eval_data;
            multi_eval_data.primary_block = solar_block;
            multi_eval_data.deflector_blocks = deflector_blocks;
            multi_eval_data.deflector_count = deflector_count;
            multi_eval_data.primary_deflector_index = solar_deflector_index;
            multi_eval_data.target_id = target_id;
            multi_eval_data.deflector_ids = deflector_ids;
            const Vector3 observer_heliocentric_position = vector3_subtract(observer.position_au, solar.position_au);
            const Vector3 observer_heliocentric_velocity = need_velocity(flags)
                ? vector3_subtract(observer.velocity_au_per_day, solar.velocity_au_per_day)
                : zero_vector();
            const Vector3 observer_heliocentric_acceleration = need_acceleration(flags)
                ? vector3_subtract(observer.acceleration_au_per_day2, solar.acceleration_au_per_day2)
                : zero_vector();
            std::vector<double> shapiro_schwarzschild_radius;
            const double* shapiro_schwarzschild_radius_data = deflector_schwarzschild_radius_au;
            if (deflector_count > 0) {
                shapiro_schwarzschild_radius.assign(
                    deflector_schwarzschild_radius_au,
                    deflector_schwarzschild_radius_au + deflector_count);
                for (int i = 0; i < deflector_count; ++i) {
                    if (deflector_ids[i] == target_id) {
                        shapiro_schwarzschild_radius[i] = 0.0;
                    }
                }
                shapiro_schwarzschild_radius_data = shapiro_schwarzschild_radius.data();
            }

            if (need_acceleration(flags)) {
                Vector3 retarded_position;
                Vector3 retarded_velocity;
                Vector3 retarded_acceleration;
                if (!solve_light_time_acceleration_with_multi_shapiro(
                        jd_tdb,
                        observer_heliocentric_position,
                        observer_heliocentric_velocity,
                        observer_heliocentric_acceleration,
                        &eval_relative_position,
                        &eval_relative_velocity,
                        &eval_relative_acceleration,
                        &eval_data,
                        deflector_count,
                        shapiro_schwarzschild_radius_data,
                        &eval_deflector_relative_position,
                        &eval_deflector_relative_velocity,
                        &eval_deflector_relative_acceleration,
                        &multi_eval_data,
                        TAIYIN_LIGHT_TIME_DAYS_PER_AU,
                        max_light_time_iterations,
                        light_time_tolerance_days,
                        &astrometric.position_au,
                        &astrometric.velocity_au_per_day,
                        &astrometric.acceleration_au_per_day2,
                        &light_time_days,
                        &light_time_rate,
                        &light_time_acceleration,
                        &retarded_position,
                        &retarded_velocity,
                        &retarded_acceleration)) {
                    return false;
                }
            } else if (need_velocity(flags)) {
                Vector3 retarded_position;
                Vector3 retarded_velocity;
                if (!solve_light_time_velocity_with_multi_shapiro(
                        jd_tdb,
                        observer_heliocentric_position,
                        observer_heliocentric_velocity,
                        &eval_relative_position,
                        &eval_relative_velocity,
                        &eval_data,
                        deflector_count,
                        shapiro_schwarzschild_radius_data,
                        &eval_deflector_relative_position,
                        &eval_deflector_relative_velocity,
                        &multi_eval_data,
                        TAIYIN_LIGHT_TIME_DAYS_PER_AU,
                        max_light_time_iterations,
                        light_time_tolerance_days,
                        &astrometric.position_au,
                        &astrometric.velocity_au_per_day,
                        &light_time_days,
                        &light_time_rate,
                        &retarded_position,
                        &retarded_velocity)) {
                    return false;
                }
            } else {
                Vector3 retarded_position;
                if (!solve_light_time_position_with_multi_shapiro(
                        jd_tdb,
                        observer_heliocentric_position,
                        &eval_relative_position,
                        &eval_data,
                        deflector_count,
                        shapiro_schwarzschild_radius_data,
                        &eval_deflector_relative_position,
                        &multi_eval_data,
                        TAIYIN_LIGHT_TIME_DAYS_PER_AU,
                        max_light_time_iterations,
                        light_time_tolerance_days,
                        &astrometric.position_au,
                        &light_time_days,
                        &retarded_position)) {
                    return false;
                }
            }
        } else if (need_acceleration(flags)) {
            Vector3 retarded_position;
            Vector3 retarded_velocity;
            Vector3 retarded_acceleration;
            if (!solve_light_time_acceleration(
                    jd_tdb,
                    observer.position_au,
                    observer.velocity_au_per_day,
                    observer.acceleration_au_per_day2,
                    target_block->position,
                    target_block->velocity,
                    target_block->acceleration,
                    target_block->data,
                    TAIYIN_LIGHT_TIME_DAYS_PER_AU,
                    max_light_time_iterations,
                    light_time_tolerance_days,
                    &astrometric.position_au,
                    &astrometric.velocity_au_per_day,
                    &astrometric.acceleration_au_per_day2,
                    &light_time_days,
                    &light_time_rate,
                    &light_time_acceleration,
                    &retarded_position,
                    &retarded_velocity,
                    &retarded_acceleration)) {
                return false;
            }
        } else if (need_velocity(flags)) {
            Vector3 retarded_position;
            Vector3 retarded_velocity;
            if (!solve_light_time_velocity(
                    jd_tdb,
                    observer.position_au,
                    observer.velocity_au_per_day,
                    target_block->position,
                    target_block->velocity,
                    target_block->data,
                    TAIYIN_LIGHT_TIME_DAYS_PER_AU,
                    max_light_time_iterations,
                    light_time_tolerance_days,
                    &astrometric.position_au,
                    &astrometric.velocity_au_per_day,
                    &light_time_days,
                    &light_time_rate,
                    &retarded_position,
                    &retarded_velocity)) {
                return false;
            }
        } else {
            Vector3 retarded_position;
            if (!solve_light_time_position(
                    jd_tdb,
                    observer.position_au,
                    target_block->position,
                    target_block->data,
                    TAIYIN_LIGHT_TIME_DAYS_PER_AU,
                    max_light_time_iterations,
                    light_time_tolerance_days,
                    &astrometric.position_au,
                    &light_time_days,
                    &retarded_position)) {
                return false;
            }
        }
    }
    copy_vector_to_array(astrometric.position_au, out_astrometric_pos_au);
    if (need_velocity(flags)) copy_vector_to_array(astrometric.velocity_au_per_day, out_astrometric_vel_au_per_day);
    if (need_acceleration(flags)) copy_vector_to_array(astrometric.acceleration_au_per_day2, out_astrometric_acc_au_per_day2);
    if (out_light_time_days) *out_light_time_days = light_time_days;
    if (need_velocity(flags) && out_light_time_rate) *out_light_time_rate = light_time_rate;
    if (need_acceleration(flags) && out_light_time_acceleration) *out_light_time_acceleration = light_time_acceleration;
    if (out_light_time_iterations) *out_light_time_iterations = (flags & TAIYIN_APPARENT_LIGHT_TIME) != 0u ? -1 : 0;

    CartesianState deflected = astrometric;
    if ((flags & TAIYIN_APPARENT_DEFLECTION) != 0u) {
        const Vector3 source_velocity = need_velocity(flags) ? astrometric.velocity_au_per_day : zero_vector();
        const Vector3 source_acceleration = need_acceleration(flags) ? astrometric.acceleration_au_per_day2 : zero_vector();
        for (int i = 0; i < deflector_count; ++i) {
            if (deflector_ids[i] == target_id) {
                continue;
            }
            const internal::CompiledEphemerisBlock* deflector_block = deflector_blocks[i];
            CartesianState deflector;
            deflector = CartesianState();
            if (!internal::eval_compiled_ephemeris_block_position(jd_tdb, deflector_block, &deflector.position_au)) {
                return false;
            }
            if (need_velocity(flags)
                && !internal::eval_compiled_ephemeris_block_velocity(jd_tdb, deflector_block, &deflector.velocity_au_per_day)) {
                return false;
            }
            if (need_acceleration(flags)
                && !internal::eval_compiled_ephemeris_block_acceleration(jd_tdb, deflector_block, &deflector.acceleration_au_per_day2)) {
                return false;
            }
            if (!finite_vector(deflector.position_au)
                || (need_velocity(flags) && !finite_vector(deflector.velocity_au_per_day))
                || (need_acceleration(flags) && !finite_vector(deflector.acceleration_au_per_day2))) {
                return false;
            }

            Vector3 next_position;
            Vector3 next_velocity;
            Vector3 next_acceleration;
            const Vector3 deflected_velocity = need_velocity(flags) ? deflected.velocity_au_per_day : zero_vector();
            const Vector3 deflected_acceleration = need_acceleration(flags) ? deflected.acceleration_au_per_day2 : zero_vector();
            const Vector3 observer_velocity = need_velocity(flags) ? observer.velocity_au_per_day : zero_vector();
            const Vector3 observer_acceleration = need_acceleration(flags) ? observer.acceleration_au_per_day2 : zero_vector();
            const Vector3 deflector_velocity = need_velocity(flags) ? deflector.velocity_au_per_day : zero_vector();
            const Vector3 deflector_acceleration = need_acceleration(flags) ? deflector.acceleration_au_per_day2 : zero_vector();
            if (need_acceleration(flags)) {
                if (!apply_gravitational_deflection_from_body_acceleration(
                        deflected.position_au,
                        deflected_velocity,
                        deflected_acceleration,
                        observer.position_au,
                        observer_velocity,
                        observer_acceleration,
                        deflector.position_au,
                        deflector_velocity,
                        deflector_acceleration,
                        astrometric.position_au,
                        source_velocity,
                        source_acceleration,
                        deflector_schwarzschild_radius_au[i],
                        deflector_limit[i],
                        &next_position,
                        &next_velocity,
                        &next_acceleration)) {
                    return false;
                }
                deflected.acceleration_au_per_day2 = next_acceleration;
            } else {
                if (!apply_gravitational_deflection_from_body(
                        deflected.position_au,
                        deflected_velocity,
                        observer.position_au,
                        observer_velocity,
                        deflector.position_au,
                        deflector_velocity,
                        astrometric.position_au,
                        source_velocity,
                        deflector_schwarzschild_radius_au[i],
                        deflector_limit[i],
                        &next_position,
                        &next_velocity)) {
                    return false;
                }
            }
            deflected.position_au = next_position;
            deflected.velocity_au_per_day = next_velocity;
        }
        if (!finite_vector(deflected.position_au)
            || (need_velocity(flags) && !finite_vector(deflected.velocity_au_per_day))
            || (need_acceleration(flags) && !finite_vector(deflected.acceleration_au_per_day2))) {
            return false;
        }
    }
    copy_vector_to_array(deflected.position_au, out_deflected_pos_au);
    if (need_velocity(flags)) copy_vector_to_array(deflected.velocity_au_per_day, out_deflected_vel_au_per_day);
    if (need_acceleration(flags)) copy_vector_to_array(deflected.acceleration_au_per_day2, out_deflected_acc_au_per_day2);

    CartesianState aberrated = deflected;
    if ((flags & TAIYIN_APPARENT_ABERRATION) != 0u) {
        const internal::CompiledEphemerisBlock* solar_block = deflector_blocks[solar_deflector_index];
        CartesianState solar;
        solar = CartesianState();
        if (!internal::eval_compiled_ephemeris_block_position(jd_tdb, solar_block, &solar.position_au)) {
            return false;
        }
        if (need_velocity(flags)
            && !internal::eval_compiled_ephemeris_block_velocity(jd_tdb, solar_block, &solar.velocity_au_per_day)) {
            return false;
        }
        if (need_acceleration(flags)
            && !internal::eval_compiled_ephemeris_block_acceleration(jd_tdb, solar_block, &solar.acceleration_au_per_day2)) {
            return false;
        }
        if (!finite_vector(solar.position_au)
            || (need_velocity(flags) && !finite_vector(solar.velocity_au_per_day))
            || (need_acceleration(flags) && !finite_vector(solar.acceleration_au_per_day2))) {
            return false;
        }

        const Vector3 observer_heliocentric_position = vector3_subtract(observer.position_au, solar.position_au);
        const Vector3 observer_heliocentric_velocity = need_velocity(flags)
            ? vector3_subtract(observer.velocity_au_per_day, solar.velocity_au_per_day)
            : zero_vector();
        const Vector3 observer_heliocentric_acceleration = need_acceleration(flags)
            ? vector3_subtract(observer.acceleration_au_per_day2, solar.acceleration_au_per_day2)
            : zero_vector();
        const Vector3 source_velocity = need_velocity(flags) ? deflected.velocity_au_per_day : zero_vector();
        const Vector3 source_acceleration = need_acceleration(flags) ? deflected.acceleration_au_per_day2 : zero_vector();
        const Vector3 observer_barycentric_acceleration = need_velocity(flags)
            ? observer.acceleration_au_per_day2
            : zero_vector();

        if (need_acceleration(flags)) {
            if (!apply_annual_aberration_acceleration(
                    deflected.position_au,
                    source_velocity,
                    source_acceleration,
                    observer_heliocentric_position,
                    observer_heliocentric_velocity,
                    observer_heliocentric_acceleration,
                    observer.velocity_au_per_day,
                    observer_barycentric_acceleration,
                    TAIYIN_LIGHT_TIME_DAYS_PER_AU,
                    deflector_schwarzschild_radius_au[solar_deflector_index],
                    &aberrated.position_au,
                    &aberrated.velocity_au_per_day,
                    &aberrated.acceleration_au_per_day2)) {
                return false;
            }
        } else {
            if (!apply_annual_aberration(
                    deflected.position_au,
                    source_velocity,
                    observer_heliocentric_position,
                    observer_heliocentric_velocity,
                    observer.velocity_au_per_day,
                    observer_barycentric_acceleration,
                    TAIYIN_LIGHT_TIME_DAYS_PER_AU,
                    deflector_schwarzschild_radius_au[solar_deflector_index],
                    &aberrated.position_au,
                    &aberrated.velocity_au_per_day)) {
                return false;
            }
        }
        if (!finite_vector(aberrated.position_au)
            || (need_velocity(flags) && !finite_vector(aberrated.velocity_au_per_day))
            || (need_acceleration(flags) && !finite_vector(aberrated.acceleration_au_per_day2))) {
            return false;
        }
    }
    copy_vector_to_array(aberrated.position_au, out_aberrated_pos_au);
    if (need_velocity(flags)) copy_vector_to_array(aberrated.velocity_au_per_day, out_aberrated_vel_au_per_day);
    if (need_acceleration(flags)) copy_vector_to_array(aberrated.acceleration_au_per_day2, out_aberrated_acc_au_per_day2);

    CartesianState apparent = aberrated;
    if ((flags & TAIYIN_APPARENT_USE_MATRIX) != 0u) {
        const Matrix3x3 matrix = matrix_from_array(output_matrix);
        apparent.position_au = transform_position_with_matrix(apparent.position_au, matrix);
        if (need_velocity(flags)) {
            const Matrix3x3 matrix_dot = matrix_from_array(output_matrix_dot);
            apparent.velocity_au_per_day = transform_velocity_with_matrix(
                aberrated.position_au,
                aberrated.velocity_au_per_day,
                matrix,
                matrix_dot);
        }
        if (need_acceleration(flags)) {
            const Matrix3x3 matrix_dot = matrix_from_array(output_matrix_dot);
            const Matrix3x3 matrix_ddot = matrix_from_array(output_matrix_ddot);
            apparent.acceleration_au_per_day2 = transform_acceleration_with_matrix(
                aberrated.position_au,
                aberrated.velocity_au_per_day,
                aberrated.acceleration_au_per_day2,
                matrix,
                matrix_dot,
                matrix_ddot);
        }
    }
    copy_vector_to_array(apparent.position_au, out_apparent_pos_au);
    if (need_velocity(flags)) copy_vector_to_array(apparent.velocity_au_per_day, out_apparent_vel_au_per_day);
    if (need_acceleration(flags)) copy_vector_to_array(apparent.acceleration_au_per_day2, out_apparent_acc_au_per_day2);

    return compute_spherical_outputs(
        flags,
        apparent.position_au,
        apparent.velocity_au_per_day,
        apparent.acceleration_au_per_day2,
        out_lon_rad,
        out_lat_rad,
        out_distance_au,
        out_lon_rate_rad_per_day,
        out_lat_rate_rad_per_day,
        out_distance_rate_au_per_day,
        out_lon_acc_rad_per_day2,
        out_lat_acc_rad_per_day2,
        out_distance_acc_au_per_day2);
}

bool taiyin_calc_apparent_batch_with_matrix_flat(
    double jd_tdb,
    int target_count,
    const int* target_ids,
    const internal::CompiledEphemerisBlock* const* target_blocks,
    int observer_id,
    const internal::CompiledEphemerisBlock* observer_block,
    const double observer_offset_pos_au[3],
    const double observer_offset_vel_au_per_day[3],
    const double observer_offset_acc_au_per_day2[3],
    int deflector_count,
    int solar_deflector_index,
    const int* deflector_ids,
    const internal::CompiledEphemerisBlock* const* deflector_blocks,
    const double* deflector_schwarzschild_radius_au,
    const double* deflector_limit,
    uint32_t flags,
    int light_time_method_id,
    int shapiro_delay_model_id,
    int aberration_model_id,
    int deflection_model_id,
    int max_light_time_iterations,
    double light_time_tolerance_days,
    const double output_matrix[9],
    const double output_matrix_dot[9],
    const double output_matrix_ddot[9],
    double* out_apparent_pos_au,
    double* out_apparent_vel_au_per_day,
    double* out_apparent_acc_au_per_day2,
    double* out_lon_rad,
    double* out_lat_rad,
    double* out_distance_au,
    double* out_lon_rate_rad_per_day,
    double* out_lat_rate_rad_per_day,
    double* out_distance_rate_au_per_day,
    double* out_lon_acc_rad_per_day2,
    double* out_lat_acc_rad_per_day2,
    double* out_distance_acc_au_per_day2,
    double* out_light_time_days,
    double* out_light_time_rate,
    double* out_light_time_acceleration,
    int* out_light_time_iterations
) noexcept {
    if (target_count < 0) {
        return false;
    }
    if (target_count == 0) {
        return true;
    }
    if (!target_blocks || (target_count > 0 && !target_ids)) {
        return false;
    }
    if (deflector_count > 0 && !deflector_ids) {
        return false;
    }

    for (int i = 0; i < target_count; ++i) {
        const int current_target_id = target_ids[i];
        double geometric_pos[3];
        double geometric_vel[3];
        double geometric_acc[3];
        double astrometric_pos[3];
        double astrometric_vel[3];
        double astrometric_acc[3];
        double deflected_pos[3];
        double deflected_vel[3];
        double deflected_acc[3];
        double aberrated_pos[3];
        double aberrated_vel[3];
        double aberrated_acc[3];
        double* apparent_pos = out_apparent_pos_au ? out_apparent_pos_au + i * 3 : 0;
        double* apparent_vel = out_apparent_vel_au_per_day ? out_apparent_vel_au_per_day + i * 3 : 0;
        double* apparent_acc = out_apparent_acc_au_per_day2 ? out_apparent_acc_au_per_day2 + i * 3 : 0;
        if (!taiyin_calc_apparent_with_matrix_flat(
                jd_tdb,
                current_target_id,
                target_blocks[i],
                observer_id,
                observer_block,
                observer_offset_pos_au,
                observer_offset_vel_au_per_day,
                observer_offset_acc_au_per_day2,
                deflector_count,
                solar_deflector_index,
                deflector_ids,
                deflector_blocks,
                deflector_schwarzschild_radius_au,
                deflector_limit,
                flags,
                light_time_method_id,
                shapiro_delay_model_id,
                aberration_model_id,
                deflection_model_id,
                max_light_time_iterations,
                light_time_tolerance_days,
                output_matrix,
                output_matrix_dot,
                output_matrix_ddot,
                geometric_pos,
                need_velocity(flags) ? geometric_vel : 0,
                need_acceleration(flags) ? geometric_acc : 0,
                astrometric_pos,
                need_velocity(flags) ? astrometric_vel : 0,
                need_acceleration(flags) ? astrometric_acc : 0,
                deflected_pos,
                need_velocity(flags) ? deflected_vel : 0,
                need_acceleration(flags) ? deflected_acc : 0,
                aberrated_pos,
                need_velocity(flags) ? aberrated_vel : 0,
                need_acceleration(flags) ? aberrated_acc : 0,
                apparent_pos,
                apparent_vel,
                apparent_acc,
                out_lon_rad ? out_lon_rad + i : 0,
                out_lat_rad ? out_lat_rad + i : 0,
                out_distance_au ? out_distance_au + i : 0,
                out_lon_rate_rad_per_day ? out_lon_rate_rad_per_day + i : 0,
                out_lat_rate_rad_per_day ? out_lat_rate_rad_per_day + i : 0,
                out_distance_rate_au_per_day ? out_distance_rate_au_per_day + i : 0,
                out_lon_acc_rad_per_day2 ? out_lon_acc_rad_per_day2 + i : 0,
                out_lat_acc_rad_per_day2 ? out_lat_acc_rad_per_day2 + i : 0,
                out_distance_acc_au_per_day2 ? out_distance_acc_au_per_day2 + i : 0,
                out_light_time_days ? out_light_time_days + i : 0,
                out_light_time_rate ? out_light_time_rate + i : 0,
                out_light_time_acceleration ? out_light_time_acceleration + i : 0,
                out_light_time_iterations ? out_light_time_iterations + i : 0)) {
            return false;
        }
    }
    return true;
}

bool taiyin_calc_apparent_batch_flat(
    double jd_tdb,
    double jd_tt,
    int target_count,
    const int* target_ids,
    const internal::CompiledEphemerisBlock* const* target_blocks,
    int observer_id,
    const internal::CompiledEphemerisBlock* observer_block,
    const double observer_offset_pos_au[3],
    const double observer_offset_vel_au_per_day[3],
    const double observer_offset_acc_au_per_day2[3],
    int deflector_count,
    int solar_deflector_index,
    const int* deflector_ids,
    const internal::CompiledEphemerisBlock* const* deflector_blocks,
    const double* deflector_schwarzschild_radius_au,
    const double* deflector_limit,
    uint32_t flags,
    int output_frame_id,
    int light_time_method_id,
    int shapiro_delay_model_id,
    int aberration_model_id,
    int deflection_model_id,
    int precession_model_id,
    int nutation_model_id,
    int obliquity_model_id,
    int max_light_time_iterations,
    double light_time_tolerance_days,
    double matrix_derivative_step_days,
    double* out_apparent_pos_au,
    double* out_apparent_vel_au_per_day,
    double* out_apparent_acc_au_per_day2,
    double* out_lon_rad,
    double* out_lat_rad,
    double* out_distance_au,
    double* out_lon_rate_rad_per_day,
    double* out_lat_rate_rad_per_day,
    double* out_distance_rate_au_per_day,
    double* out_lon_acc_rad_per_day2,
    double* out_lat_acc_rad_per_day2,
    double* out_distance_acc_au_per_day2,
    double* out_light_time_days,
    double* out_light_time_rate,
    double* out_light_time_acceleration,
    int* out_light_time_iterations
) noexcept {
    if (!known_output_frame(output_frame_id)) {
        return false;
    }

    double output_matrix[9];
    double output_matrix_dot[9];
    double output_matrix_ddot[9];
    set_identity_array(output_matrix);
    set_zero_array9(output_matrix_dot);
    set_zero_array9(output_matrix_ddot);

    uint32_t core_flags = flags & ~TAIYIN_APPARENT_USE_MATRIX;
    if (output_frame_id != TAIYIN_APPARENT_FRAME_ICRF) {
        if (!taiyin_calc_apparent_matrices_flat(
                jd_tt,
                flags,
                output_frame_id,
                precession_model_id,
                nutation_model_id,
                obliquity_model_id,
                matrix_derivative_step_days,
                0,
                0,
                output_matrix,
                output_matrix_dot,
                output_matrix_ddot,
                0,
                0,
                0,
                0)) {
            return false;
        }
        core_flags |= TAIYIN_APPARENT_USE_MATRIX;
    } else if (obliquity_model_id != 0) {
        return false;
    }

    return taiyin_calc_apparent_batch_with_matrix_flat(
        jd_tdb,
        target_count,
        target_ids,
        target_blocks,
        observer_id,
        observer_block,
        observer_offset_pos_au,
        observer_offset_vel_au_per_day,
        observer_offset_acc_au_per_day2,
        deflector_count,
        solar_deflector_index,
        deflector_ids,
        deflector_blocks,
        deflector_schwarzschild_radius_au,
        deflector_limit,
        core_flags,
        light_time_method_id,
        shapiro_delay_model_id,
        aberration_model_id,
        deflection_model_id,
        max_light_time_iterations,
        light_time_tolerance_days,
        output_matrix,
        output_matrix_dot,
        output_matrix_ddot,
        out_apparent_pos_au,
        out_apparent_vel_au_per_day,
        out_apparent_acc_au_per_day2,
        out_lon_rad,
        out_lat_rad,
        out_distance_au,
        out_lon_rate_rad_per_day,
        out_lat_rate_rad_per_day,
        out_distance_rate_au_per_day,
        out_lon_acc_rad_per_day2,
        out_lat_acc_rad_per_day2,
        out_distance_acc_au_per_day2,
        out_light_time_days,
        out_light_time_rate,
        out_light_time_acceleration,
        out_light_time_iterations);
}

bool taiyin_calc_apparent_flat(
    double jd_tdb,
    double jd_tt,
    int target_id,
    const internal::CompiledEphemerisBlock* target_block,
    int observer_id,
    const internal::CompiledEphemerisBlock* observer_block,
    const double observer_offset_pos_au[3],
    const double observer_offset_vel_au_per_day[3],
    const double observer_offset_acc_au_per_day2[3],
    int deflector_count,
    int solar_deflector_index,
    const int* deflector_ids,
    const internal::CompiledEphemerisBlock* const* deflector_blocks,
    const double* deflector_schwarzschild_radius_au,
    const double* deflector_limit,
    uint32_t flags,
    int output_frame_id,
    int light_time_method_id,
    int shapiro_delay_model_id,
    int aberration_model_id,
    int deflection_model_id,
    int precession_model_id,
    int nutation_model_id,
    int obliquity_model_id,
    int max_light_time_iterations,
    double light_time_tolerance_days,
    double matrix_derivative_step_days,
    double out_geometric_pos_au[3],
    double out_geometric_vel_au_per_day[3],
    double out_geometric_acc_au_per_day2[3],
    double out_astrometric_pos_au[3],
    double out_astrometric_vel_au_per_day[3],
    double out_astrometric_acc_au_per_day2[3],
    double out_deflected_pos_au[3],
    double out_deflected_vel_au_per_day[3],
    double out_deflected_acc_au_per_day2[3],
    double out_aberrated_pos_au[3],
    double out_aberrated_vel_au_per_day[3],
    double out_aberrated_acc_au_per_day2[3],
    double out_apparent_pos_au[3],
    double out_apparent_vel_au_per_day[3],
    double out_apparent_acc_au_per_day2[3],
    double* out_lon_rad,
    double* out_lat_rad,
    double* out_distance_au,
    double* out_lon_rate_rad_per_day,
    double* out_lat_rate_rad_per_day,
    double* out_distance_rate_au_per_day,
    double* out_lon_acc_rad_per_day2,
    double* out_lat_acc_rad_per_day2,
    double* out_distance_acc_au_per_day2,
    double* out_light_time_days,
    double* out_light_time_rate,
    double* out_light_time_acceleration,
    int* out_light_time_iterations
) noexcept {
    if (!known_output_frame(output_frame_id)) {
        return false;
    }

    double output_matrix[9];
    double output_matrix_dot[9];
    double output_matrix_ddot[9];
    set_identity_array(output_matrix);
    set_zero_array9(output_matrix_dot);
    set_zero_array9(output_matrix_ddot);

    uint32_t core_flags = flags & ~TAIYIN_APPARENT_USE_MATRIX;
    if (output_frame_id != TAIYIN_APPARENT_FRAME_ICRF) {
        if (!taiyin_calc_apparent_matrices_flat(
                jd_tt,
                flags,
                output_frame_id,
                precession_model_id,
                nutation_model_id,
                obliquity_model_id,
                matrix_derivative_step_days,
                0,
                0,
                output_matrix,
                output_matrix_dot,
                output_matrix_ddot,
                0,
                0,
                0,
                0)) {
            return false;
        }
        core_flags |= TAIYIN_APPARENT_USE_MATRIX;
    } else if (obliquity_model_id != 0) {
        return false;
    }

    return taiyin_calc_apparent_with_matrix_flat(
        jd_tdb,
        target_id,
        target_block,
        observer_id,
        observer_block,
        observer_offset_pos_au,
        observer_offset_vel_au_per_day,
        observer_offset_acc_au_per_day2,
        deflector_count,
        solar_deflector_index,
        deflector_ids,
        deflector_blocks,
        deflector_schwarzschild_radius_au,
        deflector_limit,
        core_flags,
        light_time_method_id,
        shapiro_delay_model_id,
        aberration_model_id,
        deflection_model_id,
        max_light_time_iterations,
        light_time_tolerance_days,
        output_matrix,
        output_matrix_dot,
        output_matrix_ddot,
        out_geometric_pos_au,
        out_geometric_vel_au_per_day,
        out_geometric_acc_au_per_day2,
        out_astrometric_pos_au,
        out_astrometric_vel_au_per_day,
        out_astrometric_acc_au_per_day2,
        out_deflected_pos_au,
        out_deflected_vel_au_per_day,
        out_deflected_acc_au_per_day2,
        out_aberrated_pos_au,
        out_aberrated_vel_au_per_day,
        out_aberrated_acc_au_per_day2,
        out_apparent_pos_au,
        out_apparent_vel_au_per_day,
        out_apparent_acc_au_per_day2,
        out_lon_rad,
        out_lat_rad,
        out_distance_au,
        out_lon_rate_rad_per_day,
        out_lat_rate_rad_per_day,
        out_distance_rate_au_per_day,
        out_lon_acc_rad_per_day2,
        out_lat_acc_rad_per_day2,
        out_distance_acc_au_per_day2,
        out_light_time_days,
        out_light_time_rate,
        out_light_time_acceleration,
        out_light_time_iterations);
}

}  // namespace taiyin
