#include "taiyin/earth_rotation.h"

#include "taiyin/angle.h"
#include "taiyin/coordinates.h"
#include "taiyin/physical_constants.h"

#include <cmath>

namespace taiyin {
namespace {

}  // namespace

double earth_rotation_angle_rad(double jd_ut1) noexcept {
    return normalize_radians(
        TAIYIN_TWO_PI * (0.7790572732640 + 1.00273781191135448 * (jd_ut1 - JD_J2000)));
}

double gmst_minus_era_rad(double jd_tt) noexcept {
    const double t = (jd_tt - JD_J2000) / DAYS_PER_JULIAN_CENTURY;
    const double t2 = t * t;
    const double t3 = t2 * t;
    const double t4 = t3 * t;
    const double t5 = t4 * t;
    const double polynomial_arcsec = 0.014506
        + 4612.156534 * t
        + 1.3915817 * t2
        - 0.00000044 * t3
        - 0.000029956 * t4
        - 0.0000000368 * t5;
    return polynomial_arcsec * TAIYIN_ARCSEC_TO_RAD;
}

double gmst_minus_era_rate_rad_per_day(double jd_tt) noexcept {
    const double t = (jd_tt - JD_J2000) / DAYS_PER_JULIAN_CENTURY;
    const double t2 = t * t;
    const double t3 = t2 * t;
    const double t4 = t3 * t;
    const double polynomial_rate_arcsec_per_century = 4612.156534
        + 2.0 * 1.3915817 * t
        + 3.0 * -0.00000044 * t2
        + 4.0 * -0.000029956 * t3
        + 5.0 * -0.0000000368 * t4;
    return polynomial_rate_arcsec_per_century * TAIYIN_ARCSEC_TO_RAD / DAYS_PER_JULIAN_CENTURY;
}

double gmst_minus_era_acceleration_rad_per_day2(double jd_tt) noexcept {
    const double t = (jd_tt - JD_J2000) / DAYS_PER_JULIAN_CENTURY;
    const double t2 = t * t;
    const double t3 = t2 * t;
    const double polynomial_acceleration_arcsec_per_century2 = 2.0 * 1.3915817
        + 6.0 * -0.00000044 * t
        + 12.0 * -0.000029956 * t2
        + 20.0 * -0.0000000368 * t3;
    return polynomial_acceleration_arcsec_per_century2 * TAIYIN_ARCSEC_TO_RAD / (DAYS_PER_JULIAN_CENTURY * DAYS_PER_JULIAN_CENTURY);
}

double gmst_rad(double jd_ut1, double jd_tt) noexcept {
    return normalize_radians(earth_rotation_angle_rad(jd_ut1) + gmst_minus_era_rad(jd_tt));
}

double gmst_rate_rad_per_day(double jd_tt, double dut1_rate_seconds_per_day, double lod_seconds) noexcept {
    const double ut1_rate_days_per_day = 1.0 + dut1_rate_seconds_per_day / SECONDS_PER_DAY - lod_seconds / SECONDS_PER_DAY;
    return TAIYIN_EARTH_ROTATION_RATE_RAD_PER_DAY * ut1_rate_days_per_day + gmst_minus_era_rate_rad_per_day(jd_tt);
}

double gmst_acceleration_rad_per_day2(double jd_tt, double lod_rate_seconds_per_day) noexcept {
    return -TAIYIN_EARTH_ROTATION_RATE_RAD_PER_DAY * lod_rate_seconds_per_day / SECONDS_PER_DAY
        + gmst_minus_era_acceleration_rad_per_day2(jd_tt);
}

double equation_of_equinoxes_rad(double jd_tt) noexcept {
    NutationAngles nutation;
    if (!iau2000b_nutation(jd_tt, &nutation)) {
        return 0.0;
    }
    return nutation.dpsi_rad * std::cos(nutation.true_obliquity_rad);
}

double equation_of_equinoxes_iau2000a_rad(double jd_tt) noexcept {
    NutationAngles nutation;
    if (!iau2000a_nutation(jd_tt, &nutation)) {
        return 0.0;
    }
    return nutation.dpsi_rad * std::cos(nutation.true_obliquity_rad);
}

double equation_of_equinoxes_rate_rad_per_day(double jd_tt, double step_days) noexcept {
    if (step_days <= 0.0) {
        return 0.0;
    }
    return (equation_of_equinoxes_rad(jd_tt + step_days) - equation_of_equinoxes_rad(jd_tt - step_days))
        / (2.0 * step_days);
}

double equation_of_equinoxes_acceleration_rad_per_day2(double jd_tt, double step_days) noexcept {
    if (step_days <= 0.0) {
        return 0.0;
    }
    return (equation_of_equinoxes_rad(jd_tt + step_days)
        - 2.0 * equation_of_equinoxes_rad(jd_tt)
        + equation_of_equinoxes_rad(jd_tt - step_days)) / (step_days * step_days);
}

double gast_rad(double jd_ut1, double jd_tt) noexcept {
    return normalize_radians(gmst_rad(jd_ut1, jd_tt) + equation_of_equinoxes_rad(jd_tt));
}

double gast_iau2000a_rad(double jd_ut1, double jd_tt) noexcept {
    return normalize_radians(gmst_rad(jd_ut1, jd_tt) + equation_of_equinoxes_iau2000a_rad(jd_tt));
}

double gast_rate_rad_per_day(
    double jd_tt,
    double dut1_rate_seconds_per_day,
    double lod_seconds,
    double equation_step_days
) noexcept {
    return gmst_rate_rad_per_day(jd_tt, dut1_rate_seconds_per_day, lod_seconds)
        + equation_of_equinoxes_rate_rad_per_day(jd_tt, equation_step_days);
}

double gast_acceleration_rad_per_day2(
    double jd_tt,
    double lod_rate_seconds_per_day,
    double equation_step_days
) noexcept {
    return gmst_acceleration_rad_per_day2(jd_tt, lod_rate_seconds_per_day)
        + equation_of_equinoxes_acceleration_rad_per_day2(jd_tt, equation_step_days);
}

}  // namespace taiyin
