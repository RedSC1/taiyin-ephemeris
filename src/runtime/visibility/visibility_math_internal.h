#ifndef TAIYIN_RUNTIME_VISIBILITY_MATH_INTERNAL_H
#define TAIYIN_RUNTIME_VISIBILITY_MATH_INTERNAL_H

namespace taiyin {
namespace runtime {

const int TAIYIN_VISIBILITY_ALTITUDE_STATE_NOT_FOUND = 0;
const int TAIYIN_VISIBILITY_ALTITUDE_STATE_CROSSES = 1;
const int TAIYIN_VISIBILITY_ALTITUDE_STATE_ALWAYS_ABOVE = 2;
const int TAIYIN_VISIBILITY_ALTITUDE_STATE_ALWAYS_BELOW = 3;
const int TAIYIN_VISIBILITY_ALTITUDE_STATE_TANGENT = 4;

const int TAIYIN_VISIBILITY_SIGN_NEGATIVE = -1;
const int TAIYIN_VISIBILITY_SIGN_ZERO = 0;
const int TAIYIN_VISIBILITY_SIGN_POSITIVE = 1;
const int TAIYIN_VISIBILITY_SIGN_INVALID = 2;

int visibility_sign(double value, double tolerance) noexcept;
int visibility_has_crossing(double y0, double y1, double tolerance) noexcept;

double visibility_linear_root_time(
    double t0,
    double y0,
    double t1,
    double y1
) noexcept;

double visibility_quadratic_vertex_time(
    double tm,
    double ym,
    double t0,
    double y0,
    double tp,
    double yp
) noexcept;

int visibility_classify_altitude_range(
    double min_residual,
    double max_residual,
    double tolerance
) noexcept;

double visibility_solar_twilight_altitude_rad(int twilight_kind) noexcept;
double visibility_angular_radius_rad(double physical_radius_km, double distance_au) noexcept;

}  // namespace runtime
}  // namespace taiyin

#endif  // TAIYIN_RUNTIME_VISIBILITY_MATH_INTERNAL_H
