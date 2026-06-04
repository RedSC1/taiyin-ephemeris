#include "taiyin/angle.h"
#include "taiyin/interpolation.h"
#include "taiyin/time.h"

#include <cmath>
#include <iomanip>
#include <iostream>

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
        double tai_minus_utc = 0.0;
        expect_true(!taiyin::tai_minus_utc_seconds_from_utc({ 1971, 12, 31, 23, 59, 59.0 }, &tai_minus_utc), "pre-leap rejected", &failures);
        expect_true(taiyin::tai_minus_utc_seconds_from_utc({ 1972, 1, 1, 0, 0, 0.0 }, &tai_minus_utc), "1972 leap found", &failures);
        expect_near(tai_minus_utc, 10.0, 0.0, "1972 tai-utc", &failures);
        expect_true(taiyin::tai_minus_utc_seconds_from_utc({ 2024, 4, 8, 18, 17, 20.0 }, &tai_minus_utc), "2024 leap found", &failures);
        expect_near(tai_minus_utc, 37.0, 0.0, "2024 tai-utc", &failures);
        expect_true(!taiyin::tai_minus_utc_seconds_from_utc({ 2024, 4, 8, 18, 17, 20.0 }, 0), "null leap rejected", &failures);

        const taiyin::CalendarDateTime sample = { 2024, 4, 8, 18, 17, 20.0 };
        const double sample_jd = taiyin::julian_day(sample);
        expect_near(taiyin::utc_to_tai_jd(sample_jd, 37.0), taiyin::add_seconds_to_jd(sample_jd, 37.0), 0.0, "utc to tai", &failures);
        expect_near(taiyin::tai_to_tt_jd(taiyin::utc_to_tai_jd(sample_jd, 37.0)), taiyin::add_seconds_to_jd(sample_jd, 69.184), 1e-12, "tai to tt", &failures);
        expect_near(taiyin::utc_to_tt_jd(sample_jd, 37.0), taiyin::add_seconds_to_jd(sample_jd, 69.184), 1e-12, "utc to tt", &failures);
        expect_near(taiyin::utc_to_ut1_jd(sample_jd, -0.1), taiyin::add_seconds_to_jd(sample_jd, -0.1), 0.0, "utc to ut1", &failures);
        expect_near(taiyin::delta_t_from_tai_minus_utc_and_dut1(37.0, -0.1), 69.284, 1e-12, "delta t from tai dut1", &failures);
        const double manual_tt_jd = taiyin::ut1_to_tt_jd(2460409.5, 69.17035296181177);
        expect_near(manual_tt_jd, taiyin::add_seconds_to_jd(2460409.5, 69.17035296181177), 0.0, "ut1 to tt", &failures);
        expect_near(taiyin::tt_to_ut1_jd(manual_tt_jd, 69.17035296181177), 2460409.5, 1e-12, "tt to ut1", &failures);

        const taiyin::PreciseTimeScales precise = taiyin::make_precise_time_scales_from_utc(sample, 37.0, -0.1, taiyin::TdbModel::FastPeriodic);
        expect_near(precise.jd_utc, sample_jd, 1e-12, "precise utc", &failures);
        expect_near(precise.jd_tai, taiyin::add_seconds_to_jd(sample_jd, 37.0), 1e-12, "precise tai", &failures);
        expect_near(precise.jd_tt, taiyin::add_seconds_to_jd(sample_jd, 69.184), 1e-12, "precise tt", &failures);
        expect_near(precise.jd_ut1, taiyin::add_seconds_to_jd(sample_jd, -0.1), 1e-12, "precise ut1", &failures);
        expect_near(precise.delta_t_seconds, 69.284, 1e-12, "precise delta t", &failures);

        taiyin::PreciseTimeScales precise_with_leap = {};
        expect_true(taiyin::make_precise_time_scales_from_utc_with_leap_seconds(sample, -0.1, taiyin::TdbModel::FastPeriodic, &precise_with_leap), "precise leap succeeds", &failures);
        expect_near(precise_with_leap.tai_minus_utc_seconds, 37.0, 0.0, "precise leap tai-utc", &failures);
        expect_true(!taiyin::make_precise_time_scales_from_utc_with_leap_seconds({ 1971, 12, 31, 23, 59, 59.0 }, -0.1, taiyin::TdbModel::FastPeriodic, &precise_with_leap), "pre-leap precise rejected", &failures);
        expect_true(!taiyin::make_precise_time_scales_from_utc_with_leap_seconds(sample, -0.1, taiyin::TdbModel::FastPeriodic, 0), "precise null rejected", &failures);
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
        const taiyin::EstimatedTimeScales manual = taiyin::make_time_scales_from_ut_delta_t({ 2024, 4, 8, 18, 17, 20.0 }, 69.17035296181177, taiyin::TdbModel::FastPeriodic);
        expect_near(manual.jd_ut1, 2460409.262037037, 1e-12, "manual estimated ut1", &failures);
        expect_near(manual.jd_tt, taiyin::ut1_to_tt_jd(manual.jd_ut1, 69.17035296181177), 1e-12, "manual estimated tt", &failures);
        expect_near(manual.jd_tdb, taiyin::tt_to_tdb_jd(manual.jd_tt), 0.0, "manual estimated tdb", &failures);
        expect_near(manual.delta_t_seconds, 69.17035296181177, 0.0, "manual estimated delta t", &failures);
        const taiyin::EstimatedTimeScales estimated = taiyin::make_estimated_time_scales_from_ut({ 2024, 4, 8, 18, 17, 20.0 }, taiyin::TdbModel::FastPeriodic);
        expect_near(estimated.jd_tt, taiyin::ut1_to_tt_jd(estimated.jd_ut1, estimated.delta_t_seconds), 0.0, "estimated tt", &failures);
        expect_near(estimated.jd_tdb, taiyin::tt_to_tdb_jd(estimated.jd_tt), 0.0, "estimated tdb", &failures);
        expect_near(estimated.delta_t_seconds, taiyin::estimated_delta_t_seconds_from_ut1_jd(estimated.jd_ut1), 0.0, "estimated aggregate delta t", &failures);
    }

    return failures == 0 ? 0 : 1;
}
