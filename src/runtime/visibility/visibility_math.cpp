#include "runtime/visibility/visibility_math_internal.h"

#include "taiyin/angle.h"
#include "taiyin/physical_constants.h"

#include <cmath>
#include <limits>

namespace taiyin {
namespace runtime {
namespace {

double nan_value() noexcept {
    return std::numeric_limits<double>::quiet_NaN();
}

double clamp_unit(double value) noexcept {
    if (value < -1.0) return -1.0;
    if (value > 1.0) return 1.0;
    return value;
}

}  // namespace

int visibility_sign(double value, double tolerance) noexcept {
    if (!std::isfinite(value) || !(tolerance >= 0.0)) return TAIYIN_VISIBILITY_SIGN_INVALID;
    if (value > tolerance) return TAIYIN_VISIBILITY_SIGN_POSITIVE;
    if (value < -tolerance) return TAIYIN_VISIBILITY_SIGN_NEGATIVE;
    return TAIYIN_VISIBILITY_SIGN_ZERO;
}

int visibility_has_crossing(double y0, double y1, double tolerance) noexcept {
    const int s0 = visibility_sign(y0, tolerance);
    const int s1 = visibility_sign(y1, tolerance);
    if (s0 == TAIYIN_VISIBILITY_SIGN_INVALID || s1 == TAIYIN_VISIBILITY_SIGN_INVALID) return 0;
    if (s0 == TAIYIN_VISIBILITY_SIGN_ZERO || s1 == TAIYIN_VISIBILITY_SIGN_ZERO) return 1;
    return s0 != s1 ? 1 : 0;
}

double visibility_linear_root_time(
    double t0,
    double y0,
    double t1,
    double y1
) noexcept {
    if (!std::isfinite(t0) || !std::isfinite(t1) || !std::isfinite(y0) || !std::isfinite(y1)) {
        return nan_value();
    }
    const double denom = y0 - y1;
    if (denom == 0.0) return nan_value();
    const double f = y0 / denom;
    return t0 + (t1 - t0) * f;
}

double visibility_quadratic_vertex_time(
    double tm,
    double ym,
    double t0,
    double y0,
    double tp,
    double yp
) noexcept {
    if (!std::isfinite(tm) || !std::isfinite(t0) || !std::isfinite(tp)
        || !std::isfinite(ym) || !std::isfinite(y0) || !std::isfinite(yp)) {
        return nan_value();
    }
    const double left_step = t0 - tm;
    const double right_step = tp - t0;
    if (!(left_step > 0.0) || !(right_step > 0.0)) return nan_value();
    const double num = left_step * left_step * (yp - y0) - right_step * right_step * (ym - y0);
    const double denom = 2.0 * (right_step * (ym - y0) + left_step * (yp - y0));
    if (denom == 0.0) return nan_value();
    return t0 - num / denom;
}

int visibility_classify_altitude_range(
    double min_residual,
    double max_residual,
    double tolerance
) noexcept {
    if (!std::isfinite(min_residual) || !std::isfinite(max_residual) || !(tolerance >= 0.0)) {
        return TAIYIN_VISIBILITY_ALTITUDE_STATE_NOT_FOUND;
    }
    if (min_residual > max_residual) return TAIYIN_VISIBILITY_ALTITUDE_STATE_NOT_FOUND;
    if (min_residual > tolerance) return TAIYIN_VISIBILITY_ALTITUDE_STATE_ALWAYS_ABOVE;
    if (max_residual < -tolerance) return TAIYIN_VISIBILITY_ALTITUDE_STATE_ALWAYS_BELOW;
    if (std::fabs(min_residual) <= tolerance || std::fabs(max_residual) <= tolerance) {
        return TAIYIN_VISIBILITY_ALTITUDE_STATE_TANGENT;
    }
    if (min_residual < -tolerance && max_residual > tolerance) {
        return TAIYIN_VISIBILITY_ALTITUDE_STATE_CROSSES;
    }
    return TAIYIN_VISIBILITY_ALTITUDE_STATE_NOT_FOUND;
}

double visibility_solar_twilight_altitude_rad(int twilight_kind) noexcept {
    switch (twilight_kind) {
    case 1:
        return -6.0 * TAIYIN_DEG_TO_RAD;
    case 2:
        return -12.0 * TAIYIN_DEG_TO_RAD;
    case 3:
        return -18.0 * TAIYIN_DEG_TO_RAD;
    default:
        return nan_value();
    }
}

double visibility_angular_radius_rad(double physical_radius_km, double distance_au) noexcept {
    if (!(physical_radius_km >= 0.0) || !(distance_au > 0.0)) return nan_value();
    return std::asin(clamp_unit(physical_radius_km / (distance_au * TAIYIN_AU_KM)));
}

}  // namespace runtime
}  // namespace taiyin
