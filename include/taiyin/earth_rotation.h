#ifndef TAIYIN_EARTH_ROTATION_H
#define TAIYIN_EARTH_ROTATION_H

namespace taiyin {

double earth_rotation_angle_rad(double jd_ut1) noexcept;
double gmst_minus_era_rad(double jd_tt) noexcept;
double gmst_minus_era_rate_rad_per_day(double jd_tt) noexcept;
double gmst_minus_era_acceleration_rad_per_day2(double jd_tt) noexcept;
double gmst_rad(double jd_ut1, double jd_tt) noexcept;
double gmst_rate_rad_per_day(double jd_tt, double dut1_rate_seconds_per_day, double lod_seconds) noexcept;
double gmst_acceleration_rad_per_day2(double jd_tt, double lod_rate_seconds_per_day) noexcept;
double equation_of_equinoxes_rad(double jd_tt) noexcept;
double equation_of_equinoxes_iau2000a_rad(double jd_tt) noexcept;
double equation_of_equinoxes_rate_rad_per_day(double jd_tt, double step_days) noexcept;
double equation_of_equinoxes_acceleration_rad_per_day2(double jd_tt, double step_days) noexcept;
double gast_rad(double jd_ut1, double jd_tt) noexcept;
double gast_iau2000a_rad(double jd_ut1, double jd_tt) noexcept;
double gast_rate_rad_per_day(
    double jd_tt,
    double dut1_rate_seconds_per_day,
    double lod_seconds,
    double equation_step_days
) noexcept;
double gast_acceleration_rad_per_day2(
    double jd_tt,
    double lod_rate_seconds_per_day,
    double equation_step_days
) noexcept;

}  // namespace taiyin

#endif  // TAIYIN_EARTH_ROTATION_H
