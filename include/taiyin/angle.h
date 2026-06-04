#ifndef TAIYIN_ANGLE_H
#define TAIYIN_ANGLE_H

namespace taiyin {

const double TAIYIN_PI = 3.141592653589793238462643383279502884;
const double TAIYIN_TWO_PI = 2.0 * TAIYIN_PI;
const double TAIYIN_DEG_TO_RAD = TAIYIN_PI / 180.0;
const double TAIYIN_RAD_TO_DEG = 180.0 / TAIYIN_PI;
const double TAIYIN_ARCSEC_TO_RAD = TAIYIN_DEG_TO_RAD / 3600.0;
const double TAIYIN_RAD_TO_ARCSEC = 3600.0 * TAIYIN_RAD_TO_DEG;
const double TAIYIN_MAS_TO_RAD = TAIYIN_ARCSEC_TO_RAD / 1000.0;

double deg_to_rad(double deg) noexcept;
double rad_to_deg(double rad) noexcept;

double normalize_degrees(double deg) noexcept;
double normalize_radians(double rad) noexcept;

double normalize_signed_degrees(double deg) noexcept;
double normalize_signed_radians(double rad) noexcept;

double angular_difference_degrees(double a_deg, double b_deg) noexcept;
double angular_difference_radians(double a_rad, double b_rad) noexcept;

}  // namespace taiyin

#endif  // TAIYIN_ANGLE_H
