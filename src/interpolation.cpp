#include "taiyin/interpolation.h"

#include <cmath>

namespace taiyin {

double linear_interpolate(double x0, double x1, double y0, double y1, double x) noexcept {
    if (x1 == x0) {
        return y0;
    }
    const double u = (x - x0) / (x1 - x0);
    return y0 + (y1 - y0) * u;
}

double cubic_polynomial_interpolate(double a0, double a1, double a2, double a3, double x) noexcept {
    return ((a3 * x + a2) * x + a1) * x + a0;
}

bool catmull_rom_interpolate(
    double t0,
    double t1,
    double t2,
    double t3,
    double p0,
    double p1,
    double p2,
    double p3,
    double t,
    double* out
) noexcept {
    if (!out || t2 == t1 || t2 == t0 || t3 == t1) {
        return false;
    }

    const double dt1 = t2 - t1;
    const double x = (t - t1) / dt1;
    const double m1 = ((p2 - p0) / (t2 - t0)) * dt1;
    const double m2 = ((p3 - p1) / (t3 - t1)) * dt1;
    const double x2 = x * x;
    const double x3 = x2 * x;
    *out = (2.0 * x3 - 3.0 * x2 + 1.0) * p1
        + (x3 - 2.0 * x2 + x) * m1
        + (-2.0 * x3 + 3.0 * x2) * p2
        + (x3 - x2) * m2;
    return std::isfinite(*out);
}

}  // namespace taiyin
