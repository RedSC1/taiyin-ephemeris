#ifndef TAIYIN_TIME_H
#define TAIYIN_TIME_H

#include <cstddef>
#include <cstdint>

namespace taiyin {

namespace internal {
struct EarthOrientationTable;
}

const double JD_J2000 = 2451545.0;
const double DAYS_PER_JULIAN_YEAR = 365.25;
const double DAYS_PER_TROPICAL_YEAR = 365.2422;
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

class SplitJulianDate {
public:
    int64_t day_number;
    double day_fraction;

    SplitJulianDate() noexcept : day_number(0), day_fraction(0.0) {}
    SplitJulianDate(int64_t day, double fraction) noexcept
        : day_number(day), day_fraction(fraction) {}

    SplitJulianDate& operator+=(double days) noexcept;
    SplitJulianDate& operator-=(double days) noexcept;
};

const SplitJulianDate SPLIT_JD_J2000 = {2451545, 0.0};

struct LeapSecondEntry {
    int year;
    int month;
    int day;
    double tai_minus_utc_seconds;
};

struct LeapSecondTable {
    const LeapSecondEntry* entries;
    size_t count;
};

enum TdbModel {
    FastPeriodic,
    SofaFull,
};

enum TimeScalePolicy {
    TimeScaleAuto,
    TimeScalePrecise,
    TimeScaleEstimated,
};

enum TimeScaleRoute {
    TimeScaleRouteNone,
    TimeScaleRoutePreciseUtcEop,
    TimeScaleRouteEstimatedDeltaT,
};

enum TimeScaleFallbackReason {
    TimeScaleFallbackNone,
    TimeScaleFallbackNullEopTable,
    TimeScaleFallbackEopOutOfRange,
    TimeScaleFallbackLeapSecondUnavailable,
};

struct PreciseTimeScales {
    SplitJulianDate jd_utc;
    SplitJulianDate jd_tai;
    SplitJulianDate jd_tt;
    SplitJulianDate jd_ut1;
    SplitJulianDate jd_tdb;
    double tai_minus_utc_seconds;
    double dut1_seconds;
    double delta_t_seconds;
};

struct TimeScaleOptions {
    TimeScalePolicy policy;
    int tdb_model_id;
    int delta_t_model_id;
    int ephemeris_family_id;
    const LeapSecondTable* leap_second_table;
};

struct TimeScaleDiagnostic {
    TimeScaleRoute route;
    TimeScaleFallbackReason fallback_reason;
    bool used_leap_seconds;
    bool used_eop;
    bool used_delta_t_model;
    int tdb_model_id;
    int delta_t_model_id;
    int ephemeris_family_id;
    double tai_minus_utc_seconds;
    double dut1_seconds;
    double delta_t_seconds;
};

struct EstimatedTimeScales {
    SplitJulianDate jd_ut1;
    SplitJulianDate jd_tt;
    SplitJulianDate jd_tdb;
    double delta_t_seconds;
};

double julian_day(const CalendarDateTime& date) noexcept;
CalendarDateTime reverse_julian_day(double jd) noexcept;
bool normalize_split_julian_date(
    int64_t day_number,
    double day_fraction,
    SplitJulianDate* out
) noexcept;
bool split_julian_date_from_double(
    double jd,
    SplitJulianDate* out
) noexcept;
double split_julian_date_to_double(const SplitJulianDate& jd) noexcept;
bool compare_split_julian_date(
    const SplitJulianDate& jd_a,
    const SplitJulianDate& jd_b,
    int* out_comparison
) noexcept;
bool split_julian_date_is_finite(const SplitJulianDate& jd) noexcept;
bool operator==(const SplitJulianDate& lhs, const SplitJulianDate& rhs) noexcept;
bool operator!=(const SplitJulianDate& lhs, const SplitJulianDate& rhs) noexcept;
bool operator<(const SplitJulianDate& lhs, const SplitJulianDate& rhs) noexcept;
bool operator<=(const SplitJulianDate& lhs, const SplitJulianDate& rhs) noexcept;
bool operator>(const SplitJulianDate& lhs, const SplitJulianDate& rhs) noexcept;
bool operator>=(const SplitJulianDate& lhs, const SplitJulianDate& rhs) noexcept;
bool julian_day_split(
    const CalendarDateTime& date,
    SplitJulianDate* out
) noexcept;
bool reverse_julian_day_split(
    const SplitJulianDate& jd,
    CalendarDateTime* out
) noexcept;
double decimal_year_from_jd(double jd) noexcept;
double decimal_year_from_jd(const SplitJulianDate& jd) noexcept;

double julian_centuries_from_j2000(double jd) noexcept;
double julian_centuries_from_j2000(const SplitJulianDate& jd) noexcept;
double julian_millennia_from_j2000(double jd) noexcept;
double julian_millennia_from_j2000(const SplitJulianDate& jd) noexcept;
double add_seconds_to_jd(double jd, double seconds) noexcept;
double seconds_between_jd(double jd_a, double jd_b) noexcept;
bool add_days_to_split_jd(
    const SplitJulianDate& jd,
    double days,
    SplitJulianDate* out
) noexcept;
bool add_seconds_to_split_jd(
    const SplitJulianDate& jd,
    double seconds,
    SplitJulianDate* out
) noexcept;
double days_between_split_jd(
    const SplitJulianDate& jd_a,
    const SplitJulianDate& jd_b
) noexcept;
double days_between_split_jd_and_double(
    const SplitJulianDate& jd_a,
    double jd_b
) noexcept;
double seconds_between_split_jd(
    const SplitJulianDate& jd_a,
    const SplitJulianDate& jd_b
) noexcept;
SplitJulianDate operator+(const SplitJulianDate& jd, double days) noexcept;
SplitJulianDate operator+(double days, const SplitJulianDate& jd) noexcept;
SplitJulianDate operator-(const SplitJulianDate& jd, double days) noexcept;
double operator-(const SplitJulianDate& lhs, const SplitJulianDate& rhs) noexcept;

double estimated_delta_t_seconds_for_decimal_year(double year_decimal) noexcept;
double estimated_delta_t_seconds_from_ut1_jd(double jd_ut1) noexcept;
double estimated_delta_t_seconds_from_tt_jd(double jd_tt) noexcept;
double estimated_delta_t_seconds_from_ut1_jd(
    const SplitJulianDate& jd_ut1
) noexcept;
double estimated_delta_t_seconds_from_tt_jd(
    const SplitJulianDate& jd_tt
) noexcept;

const LeapSecondTable* builtin_leap_second_table() noexcept;
bool tai_minus_utc_seconds_from_table(
    const LeapSecondTable* table,
    const CalendarDateTime& datetime_utc,
    double* tai_minus_utc_seconds
) noexcept;
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
bool utc_to_tai_split_jd(
    const SplitJulianDate& jd_utc,
    double tai_minus_utc_seconds,
    SplitJulianDate* out
) noexcept;
bool tai_to_tt_split_jd(
    const SplitJulianDate& jd_tai,
    SplitJulianDate* out
) noexcept;
bool utc_to_tt_split_jd(
    const SplitJulianDate& jd_utc,
    double tai_minus_utc_seconds,
    SplitJulianDate* out
) noexcept;
bool utc_to_ut1_split_jd(
    const SplitJulianDate& jd_utc,
    double dut1_seconds,
    SplitJulianDate* out
) noexcept;
bool tt_to_ut1_split_jd(
    const SplitJulianDate& jd_tt,
    double delta_t_seconds,
    SplitJulianDate* out
) noexcept;
bool ut1_to_tt_split_jd(
    const SplitJulianDate& jd_ut1,
    double delta_t_seconds,
    SplitJulianDate* out
) noexcept;

double tdb_minus_tt_fast_seconds(double jd_tt) noexcept;
double tdb_minus_tt_sofa_seconds(double jd_tt) noexcept;
double tdb_minus_tt_fast_seconds(const SplitJulianDate& jd_tt) noexcept;
double tdb_minus_tt_sofa_seconds(const SplitJulianDate& jd_tt) noexcept;
double tdb_minus_tt_sofa_seconds(
    double jd_tt,
    double ut_fraction,
    double elong_rad,
    double u_km,
    double v_km
) noexcept;
double tdb_minus_tt_seconds(double jd_tt) noexcept;
double tdb_minus_tt_seconds(double jd_tt, TdbModel model) noexcept;
double tdb_minus_tt_seconds(const SplitJulianDate& jd_tt) noexcept;
double tdb_minus_tt_seconds(
    const SplitJulianDate& jd_tt,
    TdbModel model
) noexcept;
double tt_to_tdb_jd(double jd_tt) noexcept;
double tt_to_tdb_jd(double jd_tt, TdbModel model) noexcept;
double tdb_to_tt_jd(double jd_tdb) noexcept;
double tdb_to_tt_jd(double jd_tdb, TdbModel model) noexcept;
double tdb_to_tt_jd(double jd_tdb, TdbModel model, int max_iterations, double tolerance_days) noexcept;
bool tt_to_tdb_split_jd(
    const SplitJulianDate& jd_tt,
    TdbModel model,
    SplitJulianDate* out
) noexcept;
bool tdb_to_tt_split_jd(
    const SplitJulianDate& jd_tdb,
    TdbModel model,
    SplitJulianDate* out
) noexcept;
bool tdb_to_tt_split_jd(
    const SplitJulianDate& jd_tdb,
    TdbModel model,
    int max_iterations,
    double tolerance_days,
    SplitJulianDate* out
) noexcept;

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
TimeScaleOptions default_time_scale_options() noexcept;
bool make_time_scales_from_utc(
    const CalendarDateTime& datetime_utc,
    const internal::EarthOrientationTable* eop_table,
    const TimeScaleOptions* options,
    PreciseTimeScales* out,
    TimeScaleDiagnostic* diagnostic
) noexcept;

}  // namespace taiyin

#endif  // TAIYIN_TIME_H
