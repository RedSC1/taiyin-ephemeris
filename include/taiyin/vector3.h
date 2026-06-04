#ifndef TAIYIN_VECTOR3_H
#define TAIYIN_VECTOR3_H

namespace taiyin {

struct Vector3 {
    double x;
    double y;
    double z;
};

Vector3 vector3_add(const Vector3& a, const Vector3& b) noexcept;
Vector3 vector3_subtract(const Vector3& a, const Vector3& b) noexcept;
Vector3 vector3_scale(const Vector3& v, double scale) noexcept;
Vector3 vector3_negate(const Vector3& v) noexcept;

double vector3_dot(const Vector3& a, const Vector3& b) noexcept;
Vector3 vector3_cross(const Vector3& a, const Vector3& b) noexcept;
double vector3_norm(const Vector3& v) noexcept;
Vector3 vector3_normalize(const Vector3& v) noexcept;

Vector3 spherical_to_cartesian(double lon_rad, double lat_rad, double radius) noexcept;
void cartesian_to_spherical(
    const Vector3& v,
    double* lon_rad,
    double* lat_rad,
    double* radius
) noexcept;

Vector3 rotate_x(const Vector3& v, double angle_rad) noexcept;
Vector3 rotate_y(const Vector3& v, double angle_rad) noexcept;
Vector3 rotate_z(const Vector3& v, double angle_rad) noexcept;

}  // namespace taiyin

#endif
