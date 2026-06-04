#ifndef TAIYIN_TIME_H
#define TAIYIN_TIME_H

namespace taiyin {

const double JD_J2000 = 2451545.0;
const double DAYS_PER_JULIAN_YEAR = 365.25;
const double DAYS_PER_JULIAN_CENTURY = 36525.0;
const double DAYS_PER_JULIAN_MILLENNIUM = 365250.0;
const double SECONDS_PER_DAY = 86400.0;

struct CalendarDateTime {
    int year;
    int month;
    int day;
    int hour;
    int minute;
    double second;
};

enum TdbModel {
    FastPeriodic,
    SofaFull,
};

struct PreciseTimeScales {
    double jd_utc;
    double jd_tai;
    double jd_tt;
    double jd_ut1;
    double jd_tdb;
    double tai_minus_utc_seconds;
    double dut1_seconds;
    double delta_t_seconds;
};

struct EstimatedTimeScales {
    double jd_ut1;
    double jd_tt;
    double jd_tdb;
    double delta_t_seconds;
};

double julian_day(const CalendarDateTime& date) noexcept;
CalendarDateTime reverse_julian_day(double jd) noexcept;
double decimal_year_from_jd(double jd) noexcept;

double julian_centuries_from_j2000(double jd) noexcept;
double julian_millennia_from_j2000(double jd) noexcept;
double add_seconds_to_jd(double jd, double seconds) noexcept;
double seconds_between_jd(double jd_a, double jd_b) noexcept;

double estimated_delta_t_seconds_for_decimal_year(double year_decimal) noexcept;
double estimated_delta_t_seconds_from_ut1_jd(double jd_ut1) noexcept;
double estimated_delta_t_seconds_from_tt_jd(double jd_tt) noexcept;

bool tai_minus_utc_seconds_from_utc(
    const CalendarDateTime& datetime_utc,
    double* tai_minus_utc_seconds
) noexcept;
double utc_to_tai_jd(double jd_utc, double tai_minus_utc_seconds) noexcept;
double tai_to_tt_jd(double jd_tai) noexcept;
double utc_to_tt_jd(double jd_utc, double tai_minus_utc_seconds) noexcept;
double utc_to_ut1_jd(double jd_utc, double dut1_seconds) noexcept;
double delta_t_from_tai_minus_utc_and_dut1(double tai_minus_utc_seconds, double dut1_seconds) noexcept;
double tt_to_ut1_jd(double jd_tt, double delta_t_seconds) noexcept;
double ut1_to_tt_jd(double jd_ut1, double delta_t_seconds) noexcept;

double tdb_minus_tt_fast_seconds(double jd_tt) noexcept;
double tdb_minus_tt_sofa_seconds(double jd_tt) noexcept;
double tdb_minus_tt_sofa_seconds(
    double jd_tt,
    double ut_fraction,
    double elong_rad,
    double u_km,
    double v_km
) noexcept;
double tdb_minus_tt_seconds(double jd_tt) noexcept;
double tdb_minus_tt_seconds(double jd_tt, TdbModel model) noexcept;
double tt_to_tdb_jd(double jd_tt) noexcept;
double tt_to_tdb_jd(double jd_tt, TdbModel model) noexcept;
double tdb_to_tt_jd(double jd_tdb) noexcept;
double tdb_to_tt_jd(double jd_tdb, TdbModel model) noexcept;
double tdb_to_tt_jd(double jd_tdb, TdbModel model, int max_iterations, double tolerance_days) noexcept;

PreciseTimeScales make_precise_time_scales_from_utc(
    const CalendarDateTime& datetime_utc,
    double tai_minus_utc_seconds,
    double dut1_seconds,
    TdbModel tdb_model
) noexcept;
bool make_precise_time_scales_from_utc_with_leap_seconds(
    const CalendarDateTime& datetime_utc,
    double dut1_seconds,
    TdbModel tdb_model,
    PreciseTimeScales* out
) noexcept;
EstimatedTimeScales make_time_scales_from_ut_delta_t(
    const CalendarDateTime& datetime_ut,
    double delta_t_seconds,
    TdbModel tdb_model
) noexcept;
EstimatedTimeScales make_estimated_time_scales_from_ut(
    const CalendarDateTime& datetime_ut,
    TdbModel tdb_model
) noexcept;

}  // namespace taiyin

#endif  // TAIYIN_TIME_H
