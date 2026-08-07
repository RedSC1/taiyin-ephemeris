#include "taiyin/angle.h"

#include <cmath>

namespace taiyin {

double deg_to_rad(double deg) noexcept {
    return deg * TAIYIN_DEG_TO_RAD;
}

double rad_to_deg(double rad) noexcept {
    return rad * TAIYIN_RAD_TO_DEG;
}

double normalize_degrees(double deg) noexcept {
    double result = std::fmod(deg, 360.0);
    if (result < 0.0) {
        result += 360.0;
    }
    return result;
}

double normalize_radians(double rad) noexcept {
    double result = std::fmod(rad, TAIYIN_TWO_PI);
    if (result < 0.0) {
        result += TAIYIN_TWO_PI;
    }
    return result;
}

double normalize_signed_degrees(double deg) noexcept {
    return normalize_degrees(deg + 180.0) - 180.0;
}

double normalize_signed_radians(double rad) noexcept {
    return normalize_radians(rad + TAIYIN_PI) - TAIYIN_PI;
}

double angular_difference_degrees(double a_deg, double b_deg) noexcept {
    return normalize_signed_degrees(a_deg - b_deg);
}

double angular_difference_radians(double a_rad, double b_rad) noexcept {
    return normalize_signed_radians(a_rad - b_rad);
}

}  // namespace taiyin
