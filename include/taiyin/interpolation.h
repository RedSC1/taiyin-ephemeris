#ifndef TAIYIN_INTERPOLATION_H
#define TAIYIN_INTERPOLATION_H

namespace taiyin {

double linear_interpolate(double x0, double x1, double y0, double y1, double x) noexcept;
double cubic_polynomial_interpolate(double a0, double a1, double a2, double a3, double x) noexcept;
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
) noexcept;

}  // namespace taiyin

#endif  // TAIYIN_INTERPOLATION_H
