#ifndef TAIYIN_EARTH_ROTATION_H
#define TAIYIN_EARTH_ROTATION_H

#include "time.h"

namespace taiyin {

double earth_rotation_angle_rad(const SplitJulianDate& jd_ut1) noexcept;
double gmst_minus_era_rad(const SplitJulianDate& jd_tt) noexcept;
double gmst_minus_era_rate_rad_per_day(const SplitJulianDate& jd_tt) noexcept;
double gmst_minus_era_acceleration_rad_per_day2(const SplitJulianDate& jd_tt) noexcept;
double gmst_rad(const SplitJulianDate& jd_ut1, const SplitJulianDate& jd_tt) noexcept;
double gmst_rate_rad_per_day(const SplitJulianDate& jd_tt, double dut1_rate_seconds_per_day, double lod_seconds) noexcept;
double gmst_acceleration_rad_per_day2(const SplitJulianDate& jd_tt, double lod_rate_seconds_per_day) noexcept;
bool equation_of_equinoxes_model_rad(
    int precession_model_id,
    int nutation_model_id,
    const SplitJulianDate& jd_tt,
    double* out
) noexcept;
double equation_of_equinoxes_iau2000b_rad(const SplitJulianDate& jd_tt) noexcept;
double equation_of_equinoxes_iau2000a_rad(const SplitJulianDate& jd_tt) noexcept;
bool equation_of_equinoxes_rate_model_rad_per_day(
    int precession_model_id,
    int nutation_model_id,
    const SplitJulianDate& jd_tt,
    double step_days,
    double* out
) noexcept;
double equation_of_equinoxes_rate_iau2000b_rad_per_day(const SplitJulianDate& jd_tt, double step_days) noexcept;
double equation_of_equinoxes_acceleration_iau2000b_rad_per_day2(const SplitJulianDate& jd_tt, double step_days) noexcept;
bool gast_model_rad(
    int precession_model_id,
    int nutation_model_id,
    const SplitJulianDate& jd_ut1,
    const SplitJulianDate& jd_tt,
    double* out
) noexcept;
double gast_iau2000b_rad(const SplitJulianDate& jd_ut1, const SplitJulianDate& jd_tt) noexcept;
double gast_iau2000a_rad(const SplitJulianDate& jd_ut1, const SplitJulianDate& jd_tt) noexcept;
bool gast_rate_model_rad_per_day(
    int precession_model_id,
    int nutation_model_id,
    const SplitJulianDate& jd_tt,
    double dut1_rate_seconds_per_day,
    double lod_seconds,
    double equation_step_days,
    double* out
) noexcept;
double gast_rate_iau2000b_rad_per_day(
    const SplitJulianDate& jd_tt,
    double dut1_rate_seconds_per_day,
    double lod_seconds,
    double equation_step_days
) noexcept;
double gast_acceleration_iau2000b_rad_per_day2(
    const SplitJulianDate& jd_tt,
    double lod_rate_seconds_per_day,
    double equation_step_days
) noexcept;

}  // namespace taiyin

#endif  // TAIYIN_EARTH_ROTATION_H
