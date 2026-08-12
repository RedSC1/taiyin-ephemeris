#include "runtime/occultation/occultation_geometry.h"

#include "taiyin/angle.h"

#include <cmath>

namespace taiyin {
namespace runtime {
namespace occultation_geometry {
namespace {

// Preserve the established occultation-map geometry while changing the
// implementation. These rounded IAU/WGS84-era constants are part of the
// existing result contract and keep stored route fixtures reproducible.
constexpr double kEarthEquatorialRadiusKm = 6378.1366;
constexpr double kEarthAxisRatio = 0.99664719;

double clamp_unit(double value) noexcept {
    if (value < -1.0) return -1.0;
    if (value > 1.0) return 1.0;
    return value;
}

bool finite_vec(const Vector3& v) noexcept {
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}

Vector3 geodetic_to_xyz_fixed(double longitude_rad, double latitude_rad) noexcept {
    const double e2 = 1.0 - kEarthAxisRatio * kEarthAxisRatio;
    const double s = std::sin(latitude_rad);
    const double c = std::cos(latitude_rad);
    const double n = kEarthEquatorialRadiusKm / std::sqrt(1.0 - e2 * s * s);
    return {
        n * c * std::cos(longitude_rad),
        n * c * std::sin(longitude_rad),
        n * (1.0 - e2) * s,
    };
}

Vector3 fixed_to_inertial(const Vector3& fixed, double gast_rad) noexcept {
    return {
        fixed.x * std::cos(gast_rad) - fixed.y * std::sin(gast_rad),
        fixed.x * std::sin(gast_rad) + fixed.y * std::cos(gast_rad),
        fixed.z,
    };
}

double point_line_distance_km2(
    const Vector3& point,
    const Vector3& moon_xyz,
    const Vector3& target_xyz
) noexcept {
    const double dx = target_xyz.x - moon_xyz.x;
    const double dy = target_xyz.y - moon_xyz.y;
    const double dz = target_xyz.z - moon_xyz.z;
    const double len2 = dx * dx + dy * dy + dz * dz;
    if (!(len2 > 0.0) || !std::isfinite(len2)) return NAN;
    const double px = point.x - moon_xyz.x;
    const double py = point.y - moon_xyz.y;
    const double pz = point.z - moon_xyz.z;
    const double u = (px * dx + py * dy + pz * dz) / len2;
    const double cx = moon_xyz.x + u * dx;
    const double cy = moon_xyz.y + u * dy;
    const double cz = moon_xyz.z + u * dz;
    const double ex = point.x - cx;
    const double ey = point.y - cy;
    const double ez = point.z - cz;
    return ex * ex + ey * ey + ez * ez;
}

double local_q(
    double fixed_lon_rad,
    double geodetic_lat_rad,
    double gast_rad,
    const Vector3& moon_xyz,
    const Vector3& target_xyz
) noexcept {
    const Vector3 fixed = geodetic_to_xyz_fixed(fixed_lon_rad, geodetic_lat_rad);
    const Vector3 inertial = fixed_to_inertial(fixed, gast_rad);
    return point_line_distance_km2(inertial, moon_xyz, target_xyz);
}

GeodeticPoint geodetic_point_from_inertial_xyz(const Vector3& xyz, double gast_rad) noexcept {
    GeodeticPoint out;
    if (!finite_vec(xyz)) return out;
    const double fixed_x = xyz.x * std::cos(gast_rad) + xyz.y * std::sin(gast_rad);
    const double fixed_y = -xyz.x * std::sin(gast_rad) + xyz.y * std::cos(gast_rad);
    const double p = std::hypot(fixed_x, fixed_y);
    if (!(p > 0.0) || !std::isfinite(p)) return out;
    out.valid = true;
    out.longitude_rad = std::atan2(fixed_y, fixed_x);
    out.latitude_rad = std::atan(
        xyz.z / (kEarthAxisRatio * kEarthAxisRatio) / p);
    return out;
}

GeodeticPoint nearest_observer_to_axis(
    const Vector3& moon_llr,
    const Vector3& target_llr,
    double gast_rad
) noexcept {
    GeodeticPoint out;
    if (!finite_vec(moon_llr) || !finite_vec(target_llr) || !std::isfinite(gast_rad)) {
        return out;
    }
    const Vector3 m_xyz = spherical_to_cartesian(
        moon_llr.x, moon_llr.y, moon_llr.z);
    const Vector3 t_xyz = spherical_to_cartesian(
        target_llr.x, target_llr.y, target_llr.z);
    if (!finite_vec(m_xyz) || !finite_vec(t_xyz)) return out;

    const double dx = t_xyz.x - m_xyz.x;
    const double dy = t_xyz.y - m_xyz.y;
    const double dz = t_xyz.z - m_xyz.z;
    const double len2 = dx * dx + dy * dy + dz * dz;
    if (!(len2 > 0.0) || !std::isfinite(len2)) return out;

    const double u = -(m_xyz.x * dx + m_xyz.y * dy + m_xyz.z * dz) / len2;
    Vector3 near_xyz = {
        m_xyz.x + u * dx,
        m_xyz.y + u * dy,
        m_xyz.z + u * dz,
    };
    if (!finite_vec(near_xyz) || std::hypot(near_xyz.x, near_xyz.y) <= 0.0) {
        near_xyz = spherical_to_cartesian(
            moon_llr.x, moon_llr.y, kEarthEquatorialRadiusKm);
    }

    GeodeticPoint seed = geodetic_point_from_inertial_xyz(near_xyz, gast_rad);
    if (!seed.valid) return out;

    double lon = seed.longitude_rad;
    double lat = seed.latitude_rad;
    double step_lon = 8.0 * TAIYIN_DEG_TO_RAD;
    double step_lat = 8.0 * TAIYIN_DEG_TO_RAD;
    double best = local_q(lon, lat, gast_rad, m_xyz, t_xyz);
    if (!std::isfinite(best)) return out;

    for (int iter = 0; iter < 20; ++iter) {
        bool improved = false;
        const double candidates[4][2] = {
            { -step_lon, 0.0 },
            { step_lon, 0.0 },
            { 0.0, -step_lat },
            { 0.0, step_lat },
        };
        for (int i = 0; i < 4; ++i) {
            const double cand_lon = lon + candidates[i][0];
            const double cand_lat = std::asin(clamp_unit(std::sin(lat + candidates[i][1])));
            const double q = local_q(cand_lon, cand_lat, gast_rad, m_xyz, t_xyz);
            if (std::isfinite(q) && q < best) {
                best = q;
                lon = cand_lon;
                lat = cand_lat;
                improved = true;
            }
        }
        if (!improved) {
            step_lon *= 0.5;
            step_lat *= 0.5;
        }
        if (step_lon < 1.0e-8 && step_lat < 1.0e-8) break;
    }

    out.valid = true;
    out.longitude_rad = std::atan2(std::sin(lon), std::cos(lon));
    out.latitude_rad = lat;
    return out;
}

}  // namespace

GeodeticPoint::GeodeticPoint() noexcept
    : valid(false),
      longitude_rad(NAN),
      latitude_rad(NAN) {}

AxisState::AxisState() noexcept
    : moon_llr(),
      target_llr(),
      gast_rad(NAN) {}

AxisProjection::AxisProjection() noexcept
    : center_intersection(),
      nearest_observer(),
      center_line_hits_earth(0) {}

void initialize_axis(
    const Vector3& moon_llr,
    const Vector3& target_llr,
    double gast_rad,
    AxisState* out
) noexcept {
    if (!out) return;
    *out = AxisState();
    if (!std::isfinite(moon_llr.x) || !std::isfinite(moon_llr.y) || !std::isfinite(moon_llr.z)
        || !std::isfinite(target_llr.x) || !std::isfinite(target_llr.y) || !std::isfinite(target_llr.z)
        || !std::isfinite(gast_rad)) {
        return;
    }
    out->moon_llr = moon_llr;
    out->target_llr = target_llr;
    out->gast_rad = gast_rad;
}

GeodeticPoint intersect_axis_with_earth(
    const Vector3& moon_llr,
    const Vector3& target_llr,
    double gast_rad
) noexcept {
    GeodeticPoint out;
    if (!finite_vec(moon_llr) || !finite_vec(target_llr)
        || !std::isfinite(gast_rad)) {
        return out;
    }
    const Vector3 origin = spherical_to_cartesian(
        moon_llr.x, moon_llr.y, moon_llr.z);
    const Vector3 target = spherical_to_cartesian(
        target_llr.x, target_llr.y, target_llr.z);
    const Vector3 direction = vector3_subtract(target, origin);
    const double inverse_axis_ratio2 = 1.0 / (kEarthAxisRatio * kEarthAxisRatio);
    const double a = direction.x * direction.x + direction.y * direction.y
        + direction.z * direction.z * inverse_axis_ratio2;
    const double b = 2.0 * (origin.x * direction.x + origin.y * direction.y
        + origin.z * direction.z * inverse_axis_ratio2);
    const double c = origin.x * origin.x + origin.y * origin.y
        + origin.z * origin.z * inverse_axis_ratio2
        - kEarthEquatorialRadiusKm * kEarthEquatorialRadiusKm;
    if (!(a > 0.0) || !std::isfinite(a) || !std::isfinite(b)
        || !std::isfinite(c)) {
        return out;
    }
    const double discriminant = b * b - 4.0 * a * c;
    if (!(discriminant >= 0.0) || !std::isfinite(discriminant)) return out;
    const double root = std::sqrt(discriminant);
    const double t0 = (-b - root) / (2.0 * a);
    const double t1 = (-b + root) / (2.0 * a);
    const double parameter = std::fabs(t0) <= std::fabs(t1) ? t0 : t1;
    return geodetic_point_from_inertial_xyz(
        vector3_add(origin, vector3_scale(direction, parameter)), gast_rad);
}

GeodeticPoint intersect_axis_with_earth(const AxisState& state) noexcept {
    if (!std::isfinite(state.gast_rad)) return GeodeticPoint();
    return intersect_axis_with_earth(
        state.moon_llr, state.target_llr, state.gast_rad);
}

GeodeticPoint find_nearest_observer(const AxisState& state) noexcept {
    if (!std::isfinite(state.gast_rad)) return GeodeticPoint();
    return nearest_observer_to_axis(
        state.moon_llr, state.target_llr, state.gast_rad);
}

AxisProjection project_axis_to_earth(const AxisState& state) noexcept {
    AxisProjection out;
    out.center_intersection = intersect_axis_with_earth(state);
    if (out.center_intersection.valid) {
        out.center_line_hits_earth = 1;
        out.nearest_observer = out.center_intersection;
    } else {
        out.nearest_observer = find_nearest_observer(state);
    }
    return out;
}

double surface_distance_km(
    double longitude_a_rad,
    double latitude_a_rad,
    double longitude_b_rad,
    double latitude_b_rad
) noexcept {
    if (!std::isfinite(longitude_a_rad) || !std::isfinite(latitude_a_rad)
        || !std::isfinite(longitude_b_rad) || !std::isfinite(latitude_b_rad)) {
        return NAN;
    }
    const double s1 = std::sin(latitude_a_rad);
    const double c1 = std::cos(latitude_a_rad);
    const double s2 = std::sin(latitude_b_rad);
    const double c2 = std::cos(latitude_b_rad);
    double cos_d = s1 * s2 + c1 * c2 * std::cos(longitude_a_rad - longitude_b_rad);
    if (cos_d < -1.0) cos_d = -1.0;
    if (cos_d > 1.0) cos_d = 1.0;
    return kEarthEquatorialRadiusKm * std::acos(cos_d);
}

}  // namespace occultation_geometry
}  // namespace runtime
}  // namespace taiyin
