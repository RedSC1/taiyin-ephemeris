#include "taiyin/vector3.h"

#include "taiyin/angle.h"

#include <cmath>

namespace taiyin {

Vector3 vector3_add(const Vector3& a, const Vector3& b) noexcept {
    return Vector3{ a.x + b.x, a.y + b.y, a.z + b.z };
}

Vector3 vector3_subtract(const Vector3& a, const Vector3& b) noexcept {
    return Vector3{ a.x - b.x, a.y - b.y, a.z - b.z };
}

Vector3 vector3_scale(const Vector3& v, double scale) noexcept {
    return Vector3{ v.x * scale, v.y * scale, v.z * scale };
}

Vector3 vector3_negate(const Vector3& v) noexcept {
    return vector3_scale(v, -1.0);
}

double vector3_dot(const Vector3& a, const Vector3& b) noexcept {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

Vector3 vector3_cross(const Vector3& a, const Vector3& b) noexcept {
    return Vector3{
        a.y * b.z - a.z * b.y,
        a.z * b.x - a.x * b.z,
        a.x * b.y - a.y * b.x,
    };
}

double vector3_norm(const Vector3& v) noexcept {
    return std::sqrt(vector3_dot(v, v));
}

Vector3 vector3_normalize(const Vector3& v) noexcept {
    const double norm = vector3_norm(v);
    if (norm == 0.0) {
        return Vector3{ 0.0, 0.0, 0.0 };
    }
    return vector3_scale(v, 1.0 / norm);
}

Vector3 spherical_to_cartesian(double lon_rad, double lat_rad, double radius) noexcept {
    const double cos_lat = std::cos(lat_rad);
    return Vector3{
        radius * cos_lat * std::cos(lon_rad),
        radius * cos_lat * std::sin(lon_rad),
        radius * std::sin(lat_rad),
    };
}

void cartesian_to_spherical(
    const Vector3& v,
    double* lon_rad,
    double* lat_rad,
    double* radius
) noexcept {
    const double r = vector3_norm(v);
    if (radius) {
        *radius = r;
    }
    if (lon_rad) {
        *lon_rad = normalize_radians(std::atan2(v.y, v.x));
    }
    if (lat_rad) {
        *lat_rad = r == 0.0 ? 0.0 : std::asin(v.z / r);
    }
}

Vector3 rotate_x(const Vector3& v, double angle_rad) noexcept {
    const double c = std::cos(angle_rad);
    const double s = std::sin(angle_rad);
    return Vector3{ v.x, c * v.y - s * v.z, s * v.y + c * v.z };
}

Vector3 rotate_y(const Vector3& v, double angle_rad) noexcept {
    const double c = std::cos(angle_rad);
    const double s = std::sin(angle_rad);
    return Vector3{ c * v.x + s * v.z, v.y, -s * v.x + c * v.z };
}

Vector3 rotate_z(const Vector3& v, double angle_rad) noexcept {
    const double c = std::cos(angle_rad);
    const double s = std::sin(angle_rad);
    return Vector3{ c * v.x - s * v.y, s * v.x + c * v.y, v.z };
}

}  // namespace taiyin
