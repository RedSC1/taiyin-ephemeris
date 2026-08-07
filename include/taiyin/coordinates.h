#ifndef TAIYIN_COORDINATES_H
#define TAIYIN_COORDINATES_H

#include "vector3.h"
#include "time.h"

namespace taiyin {

struct Matrix3x3 {
    double m[3][3];
};

struct NutationAngles {
    double dpsi_rad;
    double deps_rad;
    double mean_obliquity_rad;
    double true_obliquity_rad;
};

struct CelestialIntermediatePole {
    double x_rad;
    double y_rad;
};

typedef bool (*MatrixEvalFn)(
    const SplitJulianDate& jd,
    const void* data,
    Matrix3x3* out_matrix
);

Matrix3x3 matrix3x3_identity() noexcept;
Matrix3x3 matrix3x3_transpose(const Matrix3x3& matrix) noexcept;
Matrix3x3 matrix3x3_multiply(const Matrix3x3& a, const Matrix3x3& b) noexcept;
Vector3 matrix3x3_multiply_vector(const Matrix3x3& matrix, const Vector3& vector) noexcept;
Matrix3x3 matrix3x3_add(const Matrix3x3& a, const Matrix3x3& b) noexcept;
Matrix3x3 matrix3x3_subtract(const Matrix3x3& a, const Matrix3x3& b) noexcept;
Matrix3x3 matrix3x3_scale(const Matrix3x3& matrix, double scale) noexcept;

Matrix3x3 rotation_x_matrix(double angle_rad) noexcept;
Matrix3x3 rotation_y_matrix(double angle_rad) noexcept;
Matrix3x3 rotation_z_matrix(double angle_rad) noexcept;

double mean_obliquity_iau2006(const SplitJulianDate& jd_tt) noexcept;
bool vondrak2011_precession_matrix(const SplitJulianDate& jd_tt, Matrix3x3* out, double* out_mean_obliquity_rad = nullptr) noexcept;
bool iau2006_precession_matrix(const SplitJulianDate& jd_tt, Matrix3x3* out, double* out_mean_obliquity_rad = nullptr) noexcept;
bool iau1976_precession_matrix(const SplitJulianDate& jd_tt, Matrix3x3* out, double* out_mean_obliquity_rad = nullptr) noexcept;
bool newcomb1895_precession_matrix(const SplitJulianDate& jd_tt, Matrix3x3* out, double* out_mean_obliquity_rad = nullptr) noexcept;
bool iau2000b_nutation(const SplitJulianDate& jd_tt, NutationAngles* out) noexcept;
bool iau2000a_nutation(const SplitJulianDate& jd_tt, NutationAngles* out) noexcept;
Matrix3x3 nutation_matrix(const NutationAngles& nutation) noexcept;
bool iau2006a_cip_xy(const SplitJulianDate& jd_tt, CelestialIntermediatePole* out) noexcept;
double cio_locator_s_iau2006a_rad(const SplitJulianDate& jd_tt, double x_rad, double y_rad) noexcept;
Matrix3x3 celestial_intermediate_matrix(double x_rad, double y_rad, double s_rad) noexcept;
bool cirs_matrix_iau2006a(
    const SplitJulianDate& jd_tt,
    double celestial_pole_offset_dx_rad,
    double celestial_pole_offset_dy_rad,
    Matrix3x3* out
) noexcept;
double equation_of_origins_iau2000b_rad(const SplitJulianDate& jd_tt) noexcept;

Vector3 transform_position_with_matrix(const Vector3& position, const Matrix3x3& matrix) noexcept;
Vector3 transform_velocity_with_matrix(
    const Vector3& position,
    const Vector3& velocity,
    const Matrix3x3& matrix,
    const Matrix3x3& matrix_dot
) noexcept;
Vector3 transform_acceleration_with_matrix(
    const Vector3& position,
    const Vector3& velocity,
    const Vector3& acceleration,
    const Matrix3x3& matrix,
    const Matrix3x3& matrix_dot,
    const Matrix3x3& matrix_ddot
) noexcept;

bool matrix_derivative_central(
    MatrixEvalFn eval,
    const void* data,
    const SplitJulianDate& jd,
    double step_days,
    Matrix3x3* out_matrix_dot
) noexcept;

bool matrix_second_derivative_central(
    MatrixEvalFn eval,
    const void* data,
    const SplitJulianDate& jd,
    double step_days,
    Matrix3x3* out_matrix_ddot
) noexcept;

}  // namespace taiyin

#endif  // TAIYIN_COORDINATES_H
