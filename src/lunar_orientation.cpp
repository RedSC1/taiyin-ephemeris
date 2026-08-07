#include "taiyin/lunar_orientation.h"

#include "taiyin/physical_constants.h"
#include "taiyin/time.h"

#include <cmath>

namespace taiyin {
namespace {

const double kDegreesToRadians = M_PI / 180.0;
const double kRadiansToDegrees = 180.0 / M_PI;

const double kPhaseAngleBaseDeg[13] = {
    125.045, 250.089, 260.008, 176.625, 357.529, 311.589, 134.963,
    276.617, 34.226, 15.134, 119.743, 239.961, 25.053
};

const double kPhaseAngleRateDegPerCentury[13] = {
    -1935.5364525, -3871.0729050, 475263.3328725, 487269.6299850,
    35999.0509575, 964468.4993100, 477198.8693250, 12006.3007650,
    63863.5132425, -5806.6093575, 131.8406400, 6003.1503825,
    473327.7964200
};

const double kPoleRaPeriodicDeg[13] = {
    -3.8787, -0.1204, 0.0700, -0.0172, 0.0, 0.0072, 0.0,
    0.0, 0.0, -0.0052, 0.0, 0.0, 0.0043
};

const double kPoleDecPeriodicDeg[13] = {
    1.5419, 0.0239, -0.0278, 0.0068, 0.0, -0.0029, 0.0009,
    0.0, 0.0, 0.0008, 0.0, 0.0, -0.0009
};

const double kPrimeMeridianPeriodicDeg[13] = {
    3.5610, 0.1208, -0.0642, 0.0158, 0.0252, -0.0066, -0.0047,
    -0.0046, 0.0028, 0.0052, 0.0040, 0.0019, -0.0044
};

bool finite_vector(const Vector3& value) noexcept {
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

bool normalize_vector(const Vector3& value, Vector3* out) noexcept {
    if (!out || !finite_vector(value)) return false;
    const double norm = vector3_norm(value);
    if (!std::isfinite(norm) || !(norm > 0.0)) return false;
    *out = vector3_scale(value, 1.0 / norm);
    return true;
}

double normalize_angle_degrees(double value) noexcept {
    value = std::fmod(value, 360.0);
    if (value < 0.0) value += 360.0;
    return value;
}

}  // namespace

LunarLimbViewCoordinates::LunarLimbViewCoordinates() noexcept
    : libration_longitude_deg(NAN),
      libration_latitude_deg(NAN),
      position_angle_deg(NAN) {}

bool iau2009_moon_j2000_to_mean_earth_matrix(
    SplitJulianDate jd_tdb,
    Matrix3x3* out
) noexcept {
    if (!out || !split_julian_date_is_finite(jd_tdb)) return false;
    const double days = jd_tdb - SplitJulianDate(2451545, 0.0);
    const double centuries = days / DAYS_PER_JULIAN_CENTURY;

    double pole_ra_deg = 269.9949 + 0.0031 * centuries;
    double pole_dec_deg = 66.5392 + 0.0130 * centuries;
    double prime_meridian_deg = 38.3213 + 13.17635815 * days
        - 1.4e-12 * days * days;
    for (size_t i = 0; i < 13; ++i) {
        const double phase_rad = (
            kPhaseAngleBaseDeg[i] + kPhaseAngleRateDegPerCentury[i] * centuries)
            * kDegreesToRadians;
        pole_ra_deg += kPoleRaPeriodicDeg[i] * std::sin(phase_rad);
        pole_dec_deg += kPoleDecPeriodicDeg[i] * std::cos(phase_rad);
        prime_meridian_deg += kPrimeMeridianPeriodicDeg[i] * std::sin(phase_rad);
    }

    const Matrix3x3 first = rotation_z_matrix((pole_ra_deg + 90.0) * kDegreesToRadians);
    const Matrix3x3 second = rotation_x_matrix((90.0 - pole_dec_deg) * kDegreesToRadians);
    const Matrix3x3 third = rotation_z_matrix(prime_meridian_deg * kDegreesToRadians);
    *out = matrix3x3_multiply(third, matrix3x3_multiply(second, first));
    return true;
}

Status lunar_limb_view_coordinates_j2000(
    SplitJulianDate jd_tdb,
    const Vector3& moon_to_observer_j2000,
    const Vector3& apparent_limb_direction_j2000,
    LunarLimbViewCoordinates* out
) noexcept {
    if (out) *out = LunarLimbViewCoordinates();
    if (!out || !split_julian_date_is_finite(jd_tdb)) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    Matrix3x3 j2000_to_moon;
    if (!iau2009_moon_j2000_to_mean_earth_matrix(jd_tdb, &j2000_to_moon)) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }

    return lunar_limb_view_coordinates_body_fixed(
        matrix3x3_multiply_vector(j2000_to_moon, moon_to_observer_j2000),
        matrix3x3_multiply_vector(j2000_to_moon, apparent_limb_direction_j2000),
        out);
}

Status lunar_limb_view_coordinates_body_fixed(
    const Vector3& moon_to_observer_body_fixed,
    const Vector3& apparent_limb_direction_body_fixed,
    LunarLimbViewCoordinates* out
) noexcept {
    if (out) *out = LunarLimbViewCoordinates();
    if (!out) return TAIYIN_ERROR_INVALID_ARGUMENT;

    Vector3 observer_unit;
    Vector3 limb_direction_unit;
    if (!normalize_vector(moon_to_observer_body_fixed, &observer_unit)
        || !normalize_vector(apparent_limb_direction_body_fixed, &limb_direction_unit)) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }

    const double observer_z = observer_unit.z < -1.0 ? -1.0
        : (observer_unit.z > 1.0 ? 1.0 : observer_unit.z);
    out->libration_longitude_deg = std::atan2(observer_unit.y, observer_unit.x)
        * kRadiansToDegrees;
    out->libration_latitude_deg = std::asin(observer_z) * kRadiansToDegrees;

    const Vector3 projected_limb = vector3_subtract(
        limb_direction_unit,
        vector3_scale(observer_unit, vector3_dot(limb_direction_unit, observer_unit)));
    Vector3 projected_limb_unit;
    if (!normalize_vector(projected_limb, &projected_limb_unit)) {
        *out = LunarLimbViewCoordinates();
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }

    const Vector3 lunar_north = { 0.0, 0.0, 1.0 };
    const Vector3 projected_north = vector3_subtract(
        lunar_north,
        vector3_scale(observer_unit, vector3_dot(lunar_north, observer_unit)));
    Vector3 north_unit;
    if (!normalize_vector(projected_north, &north_unit)) {
        *out = LunarLimbViewCoordinates();
        return TAIYIN_ERROR_UNSUPPORTED;
    }
    Vector3 east_unit;
    if (!normalize_vector(vector3_cross(north_unit, observer_unit), &east_unit)) {
        *out = LunarLimbViewCoordinates();
        return TAIYIN_ERROR_UNSUPPORTED;
    }
    out->position_angle_deg = normalize_angle_degrees(std::atan2(
        vector3_dot(projected_limb_unit, east_unit),
        vector3_dot(projected_limb_unit, north_unit)) * kRadiansToDegrees);
    return TAIYIN_STATUS_OK;
}

}  // namespace taiyin
