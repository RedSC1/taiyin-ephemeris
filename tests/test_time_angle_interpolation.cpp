#include "taiyin/angle.h"
#include "taiyin/interpolation.h"
#include "taiyin/time.h"

#include "taiyin/internal/eop.h"
#include "taiyin/dispatch.h"

#include <cmath>
#include <iomanip>
#include <iostream>
#include <limits>

namespace {

bool near(double actual, double expected, double tolerance) {
    return std::fabs(actual - expected) <= tolerance;
}

void expect_near(double actual, double expected, double tolerance, const char* label, int* failures) {
    if (!near(actual, expected, tolerance)) {
        std::cerr << std::setprecision(17)
                  << "FAIL: " << label << " actual=" << actual << " expected=" << expected
                  << " diff=" << std::fabs(actual - expected) << "\n";
        ++(*failures);
    }
}

taiyin::SplitJulianDate split_from_double(double jd) {
    taiyin::SplitJulianDate result = {0, NAN};
    taiyin::split_julian_date_from_double(jd, &result);
    return result;
}

taiyin::SplitJulianDate add_split_seconds(
    const taiyin::SplitJulianDate& jd,
    double seconds
) {
    taiyin::SplitJulianDate result = {0, NAN};
    taiyin::add_seconds_to_split_jd(jd, seconds, &result);
    return result;
}

taiyin::SplitJulianDate split_tt_to_tdb(
    const taiyin::SplitJulianDate& jd_tt,
    taiyin::TdbModel model = taiyin::TdbModel::FastPeriodic
) {
    taiyin::SplitJulianDate result = {0, NAN};
    taiyin::tt_to_tdb_split_jd(jd_tt, model, &result);
    return result;
}

void expect_split_near(
    const taiyin::SplitJulianDate& actual,
    const taiyin::SplitJulianDate& expected,
    double tolerance_days,
    const char* label,
    int* failures
) {
    const double difference_days = std::fabs(
        taiyin::days_between_split_jd(expected, actual));
    if (!(difference_days <= tolerance_days)) {
        std::cerr << std::setprecision(17)
                  << "FAIL: " << label
                  << " actual=(" << actual.day_number << ", "
                  << actual.day_fraction << ") expected=("
                  << expected.day_number << ", " << expected.day_fraction
                  << ") diff_days=" << difference_days << "\n";
        ++(*failures);
    }
}

void expect_split_near(
    const taiyin::SplitJulianDate& actual,
    double expected,
    double tolerance_days,
    const char* label,
    int* failures
) {
    expect_split_near(
        actual,
        split_from_double(expected),
        tolerance_days,
        label,
        failures);
}

void expect_true(bool value, const char* label, int* failures) {
    if (!value) {
        std::cerr << "FAIL: " << label << "\n";
        ++(*failures);
    }
}

void expect_calendar_near(
    const taiyin::CalendarDateTime& actual,
    const taiyin::CalendarDateTime& expected,
    int* failures
) {
    expect_near(actual.year, expected.year, 0.0, "calendar year", failures);
    expect_near(actual.month, expected.month, 0.0, "calendar month", failures);
    expect_near(actual.day, expected.day, 0.0, "calendar day", failures);
    expect_near(actual.hour, expected.hour, 0.0, "calendar hour", failures);
    expect_near(actual.minute, expected.minute, 0.0, "calendar minute", failures);
    expect_near(actual.second, expected.second, 1e-5, "calendar second", failures);
}

}  // namespace

int main() {
    int failures = 0;

    {
        expect_near(taiyin::deg_to_rad(180.0), taiyin::TAIYIN_PI, 0.0, "deg to rad", &failures);
        expect_near(taiyin::rad_to_deg(taiyin::TAIYIN_PI), 180.0, 0.0, "rad to deg", &failures);
        expect_near(taiyin::normalize_degrees(-30.0), 330.0, 0.0, "normalize degrees", &failures);
        expect_near(taiyin::normalize_radians(-taiyin::TAIYIN_PI / 2.0), 1.5 * taiyin::TAIYIN_PI, 1e-15, "normalize radians", &failures);
        expect_near(taiyin::normalize_signed_degrees(190.0), -170.0, 0.0, "normalize signed degrees", &failures);
        expect_near(taiyin::normalize_signed_radians(1.5 * taiyin::TAIYIN_PI), -taiyin::TAIYIN_PI / 2.0, 1e-15, "normalize signed radians", &failures);
        expect_near(taiyin::angular_difference_degrees(10.0, 350.0), 20.0, 0.0, "angular difference degrees", &failures);
        expect_near(taiyin::angular_difference_radians(taiyin::TAIYIN_PI / 18.0, 35.0 * taiyin::TAIYIN_PI / 18.0), taiyin::TAIYIN_PI / 9.0, 1e-15, "angular difference radians", &failures);
    }

    {
        expect_near(taiyin::linear_interpolate(0.0, 10.0, 20.0, 40.0, 2.5), 25.0, 0.0, "linear interpolation", &failures);
        expect_near(taiyin::linear_interpolate(1.0, 1.0, 5.0, 9.0, 1.0), 5.0, 0.0, "linear interpolation zero span", &failures);
        expect_near(taiyin::cubic_polynomial_interpolate(1.0, 2.0, 3.0, 4.0, 2.0), 49.0, 0.0, "cubic polynomial", &failures);
        double catmull = 0.0;
        expect_true(taiyin::catmull_rom_interpolate(0.0, 1.0, 2.0, 3.0, 0.0, 1.0, 4.0, 9.0, 1.5, &catmull), "catmull succeeds", &failures);
        expect_near(catmull, 2.25, 0.0, "catmull quadratic fixture", &failures);
        expect_true(!taiyin::catmull_rom_interpolate(0.0, 1.0, 1.0, 3.0, 0.0, 1.0, 4.0, 9.0, 1.0, &catmull), "catmull bad span rejected", &failures);
    }

    {
        const taiyin::CalendarDateTime j2000 = { 2000, 1, 1, 12, 0, 0.0 };
        expect_near(taiyin::julian_day(j2000), taiyin::JD_J2000, 1e-9, "J2000 JD", &failures);
        expect_calendar_near(taiyin::reverse_julian_day(taiyin::JD_J2000), j2000, &failures);

        const taiyin::CalendarDateTime sample = { 2024, 4, 8, 18, 17, 20.0 };
        const double sample_jd = taiyin::julian_day(sample);
        taiyin::SplitJulianDate sample_split = {};
        expect_true(
            taiyin::julian_day_split(sample, &sample_split),
            "time-scale sample split conversion",
            &failures);
        expect_near(sample_jd, 2460409.262037037, 1e-9, "sample JD", &failures);
        expect_calendar_near(taiyin::reverse_julian_day(sample_jd), sample, &failures);
        expect_near(taiyin::decimal_year_from_jd(sample_jd), 2024.2698416312485, 1e-12, "decimal year from jd", &failures);

        const taiyin::CalendarDateTime old_sample = { 1990, 4, 20, 6, 0, 0.0 };
        const double old_sample_jd = taiyin::julian_day(old_sample);
        expect_near(old_sample_jd, 2448001.75, 1e-9, "old sample JD", &failures);
        expect_calendar_near(taiyin::reverse_julian_day(old_sample_jd), old_sample, &failures);

        expect_near(taiyin::julian_centuries_from_j2000(taiyin::JD_J2000 + taiyin::DAYS_PER_JULIAN_CENTURY), 1.0, 1e-15, "julian century", &failures);
        expect_near(taiyin::julian_millennia_from_j2000(taiyin::JD_J2000 + taiyin::DAYS_PER_JULIAN_MILLENNIUM), 1.0, 1e-15, "julian millennium", &failures);
        expect_near(taiyin::add_seconds_to_jd(2451545.0, 43.2), 2451545.0005, 1e-12, "add seconds", &failures);
        expect_near(taiyin::seconds_between_jd(2451545.0, 2451545.0005), 43.2, 1e-5, "seconds between", &failures);
    }

    {
        taiyin::SplitJulianDate normalized = {};
        expect_true(
            taiyin::normalize_split_julian_date(
                2451545, 1.25, &normalized),
            "split normalize succeeds",
            &failures);
        expect_true(
            normalized.day_number == 2451546,
            "split normalize day carry",
            &failures);
        expect_near(
            normalized.day_fraction,
            0.25,
            0.0,
            "split normalize fraction",
            &failures);
        int comparison = 0;
        expect_true(
            taiyin::compare_split_julian_date(
                {2451545, 1.25}, {2451546, 0.25}, &comparison)
                && comparison == 0,
            "split compare normalizes operands",
            &failures);
        expect_near(
            taiyin::days_between_split_jd(
                {2451545, 0.25}, {2451546, 0.25}),
            taiyin::days_between_split_jd(
                {2451544, 1.25}, {2451545, 1.25}),
            0.0,
            "split difference preserves equivalent noncanonical dates",
            &failures);
        expect_true(
            !taiyin::split_julian_date_is_finite({
                std::numeric_limits<int64_t>::max(), 1.0}),
            "split validity rejects positive day overflow",
            &failures);
        expect_true(
            !taiyin::split_julian_date_is_finite({
                std::numeric_limits<int64_t>::min(), -1.0}),
            "split validity rejects negative day overflow",
            &failures);
        expect_true(
            !taiyin::split_julian_date_is_finite({0, NAN}),
            "split validity rejects non-finite fraction",
            &failures);

        const taiyin::CalendarDateTime calendar = {
            2024, 4, 8, 18, 17, 20.000000001,
        };
        taiyin::SplitJulianDate utc = {};
        taiyin::CalendarDateTime roundtrip = {};
        expect_true(
            taiyin::julian_day_split(calendar, &utc),
            "split calendar conversion succeeds",
            &failures);
        expect_true(
            taiyin::reverse_julian_day_split(utc, &roundtrip),
            "split calendar reverse succeeds",
            &failures);
        expect_near(
            roundtrip.second,
            calendar.second,
            1.0e-10,
            "split calendar preserves sub-microsecond second",
            &failures);

        const taiyin::SplitJulianDate first = {2451545, 0.25};
        taiyin::SplitJulianDate second = {};
        taiyin::SplitJulianDate first_tt = {};
        taiyin::SplitJulianDate second_tt = {};
        taiyin::SplitJulianDate first_ut1 = {};
        taiyin::SplitJulianDate second_ut1 = {};
        expect_true(
            taiyin::add_seconds_to_split_jd(first, 1.0e-9, &second),
            "split nanosecond addition succeeds",
            &failures);
        expect_near(
            taiyin::seconds_between_split_jd(first, second),
            1.0e-9,
            5.0e-12,
            "split nanosecond survives",
            &failures);
        taiyin::SplitJulianDate next_century = {};
        expect_true(
            taiyin::add_days_to_split_jd(
                taiyin::SPLIT_JD_J2000,
                taiyin::DAYS_PER_JULIAN_CENTURY,
                &next_century),
            "split day addition succeeds",
            &failures);
        expect_near(
            taiyin::days_between_split_jd(
                taiyin::SPLIT_JD_J2000, next_century),
            taiyin::DAYS_PER_JULIAN_CENTURY,
            0.0,
            "split day difference",
            &failures);
        expect_near(
            taiyin::julian_centuries_from_j2000(next_century),
            1.0,
            0.0,
            "split Julian century",
            &failures);

        taiyin::SplitJulianDate sample_split = {};
        expect_true(
            taiyin::julian_day_split(
                {2024, 4, 8, 18, 17, 20.0}, &sample_split),
            "split decimal-year sample converts",
            &failures);
        expect_near(
            taiyin::decimal_year_from_jd(sample_split),
            taiyin::decimal_year_from_jd(
                taiyin::split_julian_date_to_double(sample_split)),
            1.0e-10,
            "split decimal year matches legacy near present",
            &failures);

        const taiyin::SplitJulianDate near_max_a = {
            std::numeric_limits<int64_t>::max() - 1, 0.25,
        };
        const taiyin::SplitJulianDate near_max_b = {
            std::numeric_limits<int64_t>::max(), 0.25,
        };
        expect_near(
            taiyin::seconds_between_split_jd(near_max_a, near_max_b),
            86400.0,
            0.0,
            "split difference preserves adjacent extreme days",
            &failures);
        expect_true(
            taiyin::utc_to_tt_split_jd(first, 37.0, &first_tt)
                && taiyin::utc_to_tt_split_jd(second, 37.0, &second_tt)
                && taiyin::utc_to_ut1_split_jd(first, -0.1, &first_ut1)
                && taiyin::utc_to_ut1_split_jd(second, -0.1, &second_ut1),
            "split linear conversions succeed",
            &failures);
        expect_near(
            taiyin::seconds_between_split_jd(first_tt, second_tt),
            1.0e-9,
            5.0e-12,
            "split TT preserves nanosecond",
            &failures);
        expect_near(
            taiyin::seconds_between_split_jd(first_ut1, second_ut1),
            1.0e-9,
            5.0e-12,
            "split UT1 preserves nanosecond",
            &failures);

        taiyin::SplitJulianDate first_tdb = {};
        taiyin::SplitJulianDate second_tdb = {};
        taiyin::SplitJulianDate roundtrip_tt = {};
        expect_true(
            taiyin::tt_to_tdb_split_jd(
                first_tt, taiyin::TdbModel::SofaFull, &first_tdb)
                && taiyin::tt_to_tdb_split_jd(
                    second_tt, taiyin::TdbModel::SofaFull, &second_tdb)
                && taiyin::tdb_to_tt_split_jd(
                    first_tdb,
                    taiyin::TdbModel::SofaFull,
                    &roundtrip_tt),
            "split TDB conversions succeed",
            &failures);
        expect_near(
            taiyin::seconds_between_split_jd(first_tdb, second_tdb),
            1.0e-9,
            5.0e-12,
            "split TDB preserves nanosecond",
            &failures);
        expect_near(
            taiyin::seconds_between_split_jd(first_tt, roundtrip_tt),
            0.0,
            5.0e-12,
            "split TDB roundtrip",
            &failures);

        const taiyin::TdbModel boundary_models[] = {
            taiyin::TdbModel::FastPeriodic,
            taiyin::TdbModel::SofaFull,
        };
        for (const taiyin::TdbModel model : boundary_models) {
            const taiyin::SplitJulianDate boundary_tt = {2460000, 0.0};
            taiyin::SplitJulianDate boundary_tdb = {};
            taiyin::SplitJulianDate boundary_roundtrip_tt = {};
            expect_true(
                taiyin::tt_to_tdb_split_jd(
                    boundary_tt, model, &boundary_tdb)
                    && taiyin::tdb_to_tt_split_jd(
                        boundary_tdb, model, &boundary_roundtrip_tt),
                "split TDB integer-day roundtrip succeeds",
                &failures);
            expect_near(
                taiyin::seconds_between_split_jd(
                    boundary_tt, boundary_roundtrip_tt),
                0.0,
                5.0e-12,
                "split TDB integer-day roundtrip",
                &failures);
        }
    }

    {
        double tai_minus_utc = 0.0;
        expect_true(!taiyin::tai_minus_utc_seconds_from_utc({ 1971, 12, 31, 23, 59, 59.0 }, &tai_minus_utc), "pre-leap rejected", &failures);
        expect_true(taiyin::tai_minus_utc_seconds_from_utc({ 1972, 1, 1, 0, 0, 0.0 }, &tai_minus_utc), "1972 leap found", &failures);
        expect_near(tai_minus_utc, 10.0, 0.0, "1972 tai-utc", &failures);
        expect_true(taiyin::tai_minus_utc_seconds_from_utc({ 2024, 4, 8, 18, 17, 20.0 }, &tai_minus_utc), "2024 leap found", &failures);
        expect_near(tai_minus_utc, 37.0, 0.0, "2024 tai-utc", &failures);
        expect_true(!taiyin::tai_minus_utc_seconds_from_utc({ 2024, 4, 8, 18, 17, 20.0 }, 0), "null leap rejected", &failures);

        const taiyin::CalendarDateTime sample = { 2024, 4, 8, 18, 17, 20.0 };
        const double sample_jd = taiyin::julian_day(sample);
        taiyin::SplitJulianDate sample_split = {};
        expect_true(
            taiyin::julian_day_split(sample, &sample_split),
            "time-scale sample split conversion",
            &failures);
        expect_near(taiyin::utc_to_tai_jd(sample_jd, 37.0), taiyin::add_seconds_to_jd(sample_jd, 37.0), 0.0, "utc to tai", &failures);
        expect_near(taiyin::tai_to_tt_jd(taiyin::utc_to_tai_jd(sample_jd, 37.0)), taiyin::add_seconds_to_jd(sample_jd, 69.184), 1e-12, "tai to tt", &failures);
        expect_near(taiyin::utc_to_tt_jd(sample_jd, 37.0), taiyin::add_seconds_to_jd(sample_jd, 69.184), 1e-12, "utc to tt", &failures);
        expect_near(taiyin::utc_to_ut1_jd(sample_jd, -0.1), taiyin::add_seconds_to_jd(sample_jd, -0.1), 0.0, "utc to ut1", &failures);
        expect_near(taiyin::delta_t_from_tai_minus_utc_and_dut1(37.0, -0.1), 69.284, 1e-12, "delta t from tai dut1", &failures);
        const double manual_tt_jd = taiyin::ut1_to_tt_jd(2460409.5, 69.17035296181177);
        expect_near(manual_tt_jd, taiyin::add_seconds_to_jd(2460409.5, 69.17035296181177), 0.0, "ut1 to tt", &failures);
        expect_near(taiyin::tt_to_ut1_jd(manual_tt_jd, 69.17035296181177), 2460409.5, 1e-12, "tt to ut1", &failures);

        const taiyin::PreciseTimeScales precise = taiyin::make_precise_time_scales_from_utc(sample, 37.0, -0.1, taiyin::TdbModel::FastPeriodic);
        expect_split_near(precise.jd_utc, sample_split, 0.0, "precise utc", &failures);
        expect_split_near(precise.jd_tai, add_split_seconds(sample_split, 37.0), 0.0, "precise tai", &failures);
        expect_split_near(
            precise.jd_tt,
            add_split_seconds(sample_split, 69.184),
            1.0e-16,
            "precise tt",
            &failures);
        expect_split_near(precise.jd_ut1, add_split_seconds(sample_split, -0.1), 0.0, "precise ut1", &failures);
        expect_near(precise.delta_t_seconds, 69.284, 1e-12, "precise delta t", &failures);

        taiyin::PreciseTimeScales precise_with_leap = {};
        expect_true(taiyin::make_precise_time_scales_from_utc_with_leap_seconds(sample, -0.1, taiyin::TdbModel::FastPeriodic, &precise_with_leap), "precise leap succeeds", &failures);
        expect_near(precise_with_leap.tai_minus_utc_seconds, 37.0, 0.0, "precise leap tai-utc", &failures);
        expect_true(!taiyin::make_precise_time_scales_from_utc_with_leap_seconds({ 1971, 12, 31, 23, 59, 59.0 }, -0.1, taiyin::TdbModel::FastPeriodic, &precise_with_leap), "pre-leap precise rejected", &failures);
        expect_true(!taiyin::make_precise_time_scales_from_utc_with_leap_seconds(sample, -0.1, taiyin::TdbModel::FastPeriodic, 0), "precise null rejected", &failures);

        taiyin::internal::EarthOrientationSample eop_samples[2];
        eop_samples[0].jd_utc = sample_jd - 1.0;
        eop_samples[0].dut1_seconds = -0.2;
        eop_samples[0].xp_rad = 0.0;
        eop_samples[0].yp_rad = 0.0;
        eop_samples[0].sp_rad = 0.0;
        eop_samples[0].lod_seconds = 0.0;
        eop_samples[0].dx_rad = 0.0;
        eop_samples[0].dy_rad = 0.0;
        eop_samples[1] = eop_samples[0];
        eop_samples[1].jd_utc = sample_jd + 1.0;
        eop_samples[1].dut1_seconds = 0.0;
        taiyin::internal::EarthOrientationTable eop_table = { eop_samples, 2 };

        taiyin::PreciseTimeScales routed = {};
        taiyin::TimeScaleDiagnostic time_diag = {};
        expect_true(taiyin::make_time_scales_from_utc(sample, &eop_table, 0, &routed, &time_diag), "auto routed precise succeeds", &failures);
        expect_split_near(routed.jd_utc, sample_split, 0.0, "routed precise utc", &failures);
        expect_near(routed.tai_minus_utc_seconds, 37.0, 0.0, "routed precise tai-utc", &failures);
        expect_near(routed.dut1_seconds, -0.1, 2e-11, "routed precise interpolated dut1", &failures);
        expect_near(routed.delta_t_seconds, 69.284, 2e-11, "routed precise delta t", &failures);
        expect_true(time_diag.route == taiyin::TimeScaleRoute::TimeScaleRoutePreciseUtcEop, "routed precise route", &failures);
        expect_true(time_diag.used_leap_seconds, "routed precise uses leap seconds", &failures);
        expect_true(time_diag.used_eop, "routed precise uses eop", &failures);
        expect_true(!time_diag.used_delta_t_model, "routed precise skips delta t model", &failures);

        taiyin::TimeScaleOptions estimated_options = taiyin::default_time_scale_options();
        expect_true(estimated_options.tdb_model_id == taiyin::dispatch::TDB_FAST_PERIODIC, "default tdb model", &failures);
        expect_true(estimated_options.delta_t_model_id == taiyin::dispatch::DELTA_T_ESTIMATED_DEFAULT, "default delta t model", &failures);
        expect_true(estimated_options.ephemeris_family_id == taiyin::dispatch::EPHEMERIS_FAMILY_UNKNOWN, "default ephemeris family", &failures);
        estimated_options.policy = taiyin::TimeScalePolicy::TimeScaleEstimated;
        expect_true(taiyin::make_time_scales_from_utc(sample, &eop_table, &estimated_options, &routed, &time_diag), "estimated route succeeds", &failures);
        expect_true(time_diag.route == taiyin::TimeScaleRoute::TimeScaleRouteEstimatedDeltaT, "estimated route id", &failures);
        expect_true(time_diag.used_delta_t_model, "estimated route uses delta t model", &failures);
        expect_true(!time_diag.used_eop, "estimated route skips eop", &failures);
        expect_split_near(routed.jd_ut1, sample_split, 0.0, "estimated treats input as ut1", &failures);
        expect_split_near(routed.jd_tt, add_split_seconds(routed.jd_ut1, routed.delta_t_seconds), 0.0, "estimated routed tt", &failures);
        expect_split_near(routed.jd_tdb, split_tt_to_tdb(routed.jd_tt), 0.0, "estimated routed default tdb", &failures);

        taiyin::TimeScaleOptions precise_options = taiyin::default_time_scale_options();
        precise_options.policy = taiyin::TimeScalePolicy::TimeScalePrecise;
        expect_true(!taiyin::make_time_scales_from_utc(sample, 0, &precise_options, &routed, &time_diag), "precise route requires eop", &failures);
        expect_true(time_diag.fallback_reason == taiyin::TimeScaleFallbackReason::TimeScaleFallbackNullEopTable, "precise missing eop reason", &failures);

        expect_true(taiyin::make_time_scales_from_utc(sample, 0, 0, &routed, &time_diag), "auto falls back without eop", &failures);
        expect_true(time_diag.route == taiyin::TimeScaleRoute::TimeScaleRouteEstimatedDeltaT, "auto fallback route", &failures);
        expect_true(time_diag.fallback_reason == taiyin::TimeScaleFallbackReason::TimeScaleFallbackNullEopTable, "auto fallback reason", &failures);
        expect_split_near(routed.jd_tdb, split_tt_to_tdb(routed.jd_tt, taiyin::TdbModel::FastPeriodic), 0.0, "auto fallback default fast tdb", &failures);

        taiyin::LeapSecondEntry custom_leaps[] = {
            { 2000, 1, 1, 40.0 },
        };
        const taiyin::LeapSecondTable custom_leap_table = { custom_leaps, 1 };
        taiyin::TimeScaleOptions custom_options = taiyin::default_time_scale_options();
        custom_options.leap_second_table = &custom_leap_table;
        custom_options.tdb_model_id = taiyin::dispatch::TDB_SOFA_FULL;
        expect_true(taiyin::make_time_scales_from_utc(sample, &eop_table, &custom_options, &routed, &time_diag), "custom leap and tdb route succeeds", &failures);
        expect_near(routed.tai_minus_utc_seconds, 40.0, 0.0, "custom leap tai-utc", &failures);
        expect_near(routed.delta_t_seconds, 72.284, 2e-11, "custom leap delta t", &failures);
        expect_split_near(routed.jd_tdb, split_tt_to_tdb(routed.jd_tt, taiyin::TdbModel::SofaFull), 5e-5, "custom tdb sofa route", &failures);
        expect_true(time_diag.tdb_model_id == taiyin::dispatch::TDB_SOFA_FULL, "custom diagnostic tdb id", &failures);
        expect_true(!taiyin::tai_minus_utc_seconds_from_table(0, sample, &tai_minus_utc), "null leap table rejected", &failures);
        expect_true(taiyin::tai_minus_utc_seconds_from_table(taiyin::builtin_leap_second_table(), sample, &tai_minus_utc), "builtin leap table explicit lookup", &failures);
        expect_near(tai_minus_utc, 37.0, 0.0, "builtin explicit tai-utc", &failures);

        expect_true(!taiyin::make_time_scales_from_utc(sample, &eop_table, 0, 0, &time_diag), "routed null output rejected", &failures);
    }

    {
        expect_near(taiyin::tdb_minus_tt_fast_seconds(2460000.0), 0.0012807796353021415, 1e-12, "tdb fast 2460000", &failures);
        expect_near(taiyin::tdb_minus_tt_fast_seconds(2440000.0), 0.0010610135981240078, 1e-12, "tdb fast 2440000", &failures);
        expect_near(taiyin::tdb_minus_tt_fast_seconds(taiyin::JD_J2000), -0.00009575743486095212, 1e-12, "tdb fast j2000", &failures);
        expect_near(taiyin::tdb_minus_tt_seconds(2460000.0), taiyin::tdb_minus_tt_fast_seconds(2460000.0), 0.0, "tdb default dispatch", &failures);
        expect_near(taiyin::tdb_minus_tt_seconds(2460000.0, taiyin::TdbModel::FastPeriodic), taiyin::tdb_minus_tt_fast_seconds(2460000.0), 0.0, "tdb fast dispatch", &failures);
        expect_near(taiyin::tdb_minus_tt_sofa_seconds(taiyin::JD_J2000), -0.00009930719894379447, 1e-15, "tdb sofa j2000", &failures);
        expect_near(taiyin::tdb_minus_tt_sofa_seconds(2460000.0), 0.0012746805125203914, 1e-15, "tdb sofa 2460000", &failures);
        expect_near(taiyin::tdb_minus_tt_sofa_seconds(2440000.0), 0.0010590942554813256, 1e-15, "tdb sofa 2440000", &failures);
        expect_near(taiyin::tdb_minus_tt_seconds(2460000.0, taiyin::TdbModel::SofaFull), taiyin::tdb_minus_tt_sofa_seconds(2460000.0), 0.0, "tdb dispatch sofa", &failures);
        expect_near(taiyin::tdb_minus_tt_sofa_seconds(2460000.0, 0.37, 2.1, 3900.0, 5000.0), 0.0012729016334655484, 1e-15, "tdb sofa topocentric 1", &failures);
        expect_near(taiyin::tdb_minus_tt_sofa_seconds(2440000.0, 0.91, -1.2, 4100.0, -3800.0), 0.0010580478309073467, 1e-15, "tdb sofa topocentric 2", &failures);
        const double tdb_jd = taiyin::tt_to_tdb_jd(2460000.0);
        expect_near((tdb_jd - 2460000.0) * taiyin::SECONDS_PER_DAY, taiyin::tdb_minus_tt_fast_seconds(2460000.0), 5e-5, "tt to tdb", &failures);
        expect_near(taiyin::tdb_to_tt_jd(tdb_jd), 2460000.0, 1e-12, "tdb to tt", &failures);
        expect_near(taiyin::tdb_to_tt_jd(taiyin::tt_to_tdb_jd(taiyin::JD_J2000)), taiyin::JD_J2000, 1e-12, "tdb to tt j2000", &failures);
        const double sofa_tdb_jd = taiyin::tt_to_tdb_jd(2460000.0, taiyin::TdbModel::SofaFull);
        expect_near((sofa_tdb_jd - 2460000.0) * taiyin::SECONDS_PER_DAY, taiyin::tdb_minus_tt_sofa_seconds(2460000.0), 5e-5, "tt to tdb sofa", &failures);
        expect_near(taiyin::tdb_to_tt_jd(sofa_tdb_jd, taiyin::TdbModel::SofaFull), 2460000.0, 1e-12, "tdb to tt sofa", &failures);
    }

    {
        struct DeltaTYearOracle {
            double year_decimal;
            double delta_t_seconds;
        };
        const DeltaTYearOracle delta_t_year_oracles[] = {
            { -1000.0, 25427.68 },
            { -720.0, 20371.848 },
            { -719.5, 20363.7843227998 },
            { -100.0, 11557.668 },
            { 0.0, 10441.312575999998 },
            { 399.999, 6535.125452533171 },
            { 400.0, 6535.116 },
            { 1000.0, 1650.393 },
            { 1150.0, 1056.647 },
            { 1500.0, 292.343 },
            { 1600.0, 109.127 },
            { 1800.0, 18.367 },
            { 1850.0, 9.338 },
            { 1900.0, -1.977 },
            { 1952.999, 30.00175459878804 },
            { 1953.0, 30.0 },
            { 1953.25, 30.049765625 },
            { 1961.5, 33.486875 },
            { 1972.5, 42.765625 },
            { 2000.0, 63.83 },
            { 2016.5, 68.35 },
            { 2024.25, 69.171171875 },
            { 2049.5, 71.329375 },
            { 2050.0, 71.44 },
            { 2050.5, 72.56600000000005 },
            { 2100.0, 191.95999999999998 },
            { 2200.0, 442.08 },
        };
        for (int i = 0; i < static_cast<int>(sizeof(delta_t_year_oracles) / sizeof(delta_t_year_oracles[0])); ++i) {
            expect_near(
                taiyin::estimated_delta_t_seconds_for_decimal_year(delta_t_year_oracles[i].year_decimal),
                delta_t_year_oracles[i].delta_t_seconds,
                1e-10,
                "delta t year oracle",
                &failures);
        }
        expect_near(taiyin::estimated_delta_t_seconds_from_ut1_jd(taiyin::JD_J2000), 63.83042335736016, 1e-12, "delta t j2000", &failures);
        expect_near(taiyin::estimated_delta_t_seconds_from_ut1_jd(2460409.5), 69.17035296181177, 1e-12, "delta t 2024", &failures);
        expect_near(taiyin::estimated_delta_t_seconds_from_ut1_jd(2460409.262037037), 69.17037911418967, 1e-12, "delta t 2024 sample", &failures);
        expect_near(taiyin::estimated_delta_t_seconds_from_ut1_jd(2448001.75), 57.06055072295038, 1e-12, "delta t 1990", &failures);
        expect_near(taiyin::estimated_delta_t_seconds_from_ut1_jd(2086302.5), 1650.4617878426973, 1e-12, "delta t 1000", &failures);
        expect_near(taiyin::estimated_delta_t_seconds_from_tt_jd(taiyin::JD_J2000), 63.830422732032133, 1e-12, "delta t from tt j2000", &failures);
        expect_near(taiyin::estimated_delta_t_seconds_from_tt_jd(2460409.262837778), 69.17037911417232, 1e-12, "delta t from tt 2024", &failures);
        taiyin::SplitJulianDate manual_ut1 = {};
        expect_true(
            taiyin::julian_day_split(
                {2024, 4, 8, 18, 17, 20.0}, &manual_ut1),
            "manual estimated split conversion",
            &failures);
        const taiyin::EstimatedTimeScales manual = taiyin::make_time_scales_from_ut_delta_t({ 2024, 4, 8, 18, 17, 20.0 }, 69.17035296181177, taiyin::TdbModel::FastPeriodic);
        expect_split_near(manual.jd_ut1, manual_ut1, 0.0, "manual estimated ut1", &failures);
        expect_split_near(manual.jd_tt, add_split_seconds(manual.jd_ut1, 69.17035296181177), 1e-12, "manual estimated tt", &failures);
        expect_split_near(manual.jd_tdb, split_tt_to_tdb(manual.jd_tt), 0.0, "manual estimated tdb", &failures);
        expect_near(manual.delta_t_seconds, 69.17035296181177, 0.0, "manual estimated delta t", &failures);
        const taiyin::EstimatedTimeScales estimated = taiyin::make_estimated_time_scales_from_ut({ 2024, 4, 8, 18, 17, 20.0 }, taiyin::TdbModel::FastPeriodic);
        expect_split_near(estimated.jd_tt, add_split_seconds(estimated.jd_ut1, estimated.delta_t_seconds), 0.0, "estimated tt", &failures);
        expect_split_near(estimated.jd_tdb, split_tt_to_tdb(estimated.jd_tt), 0.0, "estimated tdb", &failures);
        expect_near(estimated.delta_t_seconds, taiyin::estimated_delta_t_seconds_from_ut1_jd(estimated.jd_ut1), 0.0, "estimated aggregate delta t", &failures);
    }

    return failures == 0 ? 0 : 1;
}
