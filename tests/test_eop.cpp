#include "taiyin/internal/builtin_loader.h"
#include "taiyin/internal/eop.h"
#include "taiyin/angle.h"

#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>

namespace {

bool nearly_equal(double a, double b, double tolerance) {
    return std::fabs(a - b) <= tolerance;
}

void expect_near(double actual, double expected, double tolerance, const char* message, int* failures) {
    if (!nearly_equal(actual, expected, tolerance)) {
        std::fprintf(stderr, "FAIL: %s: actual=%.17g expected=%.17g diff=%.17g tol=%.17g\n",
                     message, actual, expected, std::fabs(actual - expected), tolerance);
        ++(*failures);
    }
}

void expect_true(bool value, const char* message, int* failures) {
    if (!value) {
        std::fprintf(stderr, "FAIL: expected true: %s\n", message);
        ++(*failures);
    }
}

void expect_false(bool value, const char* message, int* failures) {
    if (value) {
        std::fprintf(stderr, "FAIL: expected false: %s\n", message);
        ++(*failures);
    }
}

std::string sample_finals2000a_text() {
    return std::string()
        + "73 1 2 41684.00 I  0.120733 0.009786  0.136966 0.015902  I 0.8084178 0.0002710  0.0000 0.1916  P    -0.766    0.199    -0.720    0.300   .143000   .137000   .8075000   -18.637    -3.667  \n"
        + "73 1 3 41685.00 I  0.118980 0.011039  0.135656 0.013616  I 0.8056163 0.0002710  3.5563 0.1916  P    -0.751    0.199    -0.701    0.300   .141000   .134000   .8044000   -18.636    -3.571  \n"
        + "27 710 61596.00                                                                                                                                                                     \n";
}

}  // namespace

int main() {
    using namespace taiyin::internal;
    int failures = 0;

    // Test 1: Parse small sample
    {
        const std::string sample_text = sample_finals2000a_text();
        const size_t max_count = 100;
        EarthOrientationSample parsed[100];
        size_t count = 0;
        expect_true(
            parse_finals2000a_table(sample_text.c_str(), static_cast<int>(sample_text.size()), parsed, max_count, &count),
            "parse small sample succeeds",
            &failures);
        assert(count == 2);

        expect_near(parsed[0].jd_utc, 2441684.5, 0.0, "sample 0 jd_utc", &failures);
        expect_near(parsed[0].dut1_seconds, 0.8084178, 0.0, "sample 0 dut1", &failures);
        expect_near(parsed[0].xp_rad, 0.120733 * taiyin::TAIYIN_ARCSEC_TO_RAD, 1e-20, "sample 0 xp", &failures);
        expect_near(parsed[0].yp_rad, 0.136966 * taiyin::TAIYIN_ARCSEC_TO_RAD, 1e-20, "sample 0 yp", &failures);
        expect_near(parsed[0].lod_seconds, 0.0, 0.0, "sample 0 lod", &failures);
        expect_near(parsed[0].dx_rad, 0.143 * taiyin::TAIYIN_ARCSEC_TO_RAD / 1000.0, 1e-20, "sample 0 dx", &failures);
        expect_near(parsed[0].dy_rad, 0.137 * taiyin::TAIYIN_ARCSEC_TO_RAD / 1000.0, 1e-20, "sample 0 dy", &failures);

        expect_near(parsed[1].jd_utc, 2441685.5, 0.0, "sample 1 jd_utc", &failures);
        expect_near(parsed[1].dut1_seconds, 0.8056163, 0.0, "sample 1 dut1", &failures);
        expect_near(parsed[1].lod_seconds, 3.5563e-3, 1e-15, "sample 1 lod", &failures);
        expect_near(parsed[1].dx_rad, 0.141 * taiyin::TAIYIN_ARCSEC_TO_RAD / 1000.0, 1e-20, "sample 1 dx", &failures);
        expect_near(parsed[1].dy_rad, 0.134 * taiyin::TAIYIN_ARCSEC_TO_RAD / 1000.0, 1e-20, "sample 1 dy", &failures);
    }

    // Test 2: Interpolation at midpoint
    {
        EarthOrientationSample samples[2];
        samples[0].jd_utc = 1000.0;
        samples[0].dut1_seconds = 1.0;
        samples[0].xp_rad = 0.1;
        samples[0].yp_rad = -0.05;
        samples[0].sp_rad = sp_rad_for_jd(1000.0);
        samples[0].lod_seconds = 0.01;
        samples[0].dx_rad = 0.0;
        samples[0].dy_rad = 0.0;

        samples[1].jd_utc = 1002.0;
        samples[1].dut1_seconds = 5.0;
        samples[1].xp_rad = 0.3;
        samples[1].yp_rad = 0.01;
        samples[1].sp_rad = sp_rad_for_jd(1002.0);
        samples[1].lod_seconds = 0.03;
        samples[1].dx_rad = 0.0;
        samples[1].dy_rad = 0.0;

        EarthOrientationTable table;
        table.samples = samples;
        table.count = 2;

        EarthOrientationSample interpolated;
        expect_true(
            interpolate_earth_orientation(&table, 1001.0, &interpolated),
            "midpoint interpolation succeeds",
            &failures);
        expect_near(interpolated.jd_utc, 1001.0, 0.0, "midpoint jd", &failures);
        expect_near(interpolated.dut1_seconds, 3.0, 1e-15, "midpoint dut1", &failures);
        expect_near(interpolated.xp_rad, 0.2, 1e-15, "midpoint xp", &failures);
        expect_near(interpolated.yp_rad, -0.02, 1e-15, "midpoint yp", &failures);
        expect_near(interpolated.lod_seconds, 0.02, 1e-15, "midpoint lod", &failures);
    }

    // Test 3: Boundary behavior
    {
        EarthOrientationSample samples[2];
        samples[0].jd_utc = 1000.0;
        samples[0].dut1_seconds = 1.0;
        samples[0].xp_rad = 0.0;
        samples[0].yp_rad = 0.0;
        samples[0].sp_rad = 0.0;
        samples[0].lod_seconds = 0.0;
        samples[0].dx_rad = 0.0;
        samples[0].dy_rad = 0.0;

        samples[1].jd_utc = 1001.0;
        samples[1].dut1_seconds = 2.0;
        samples[1].xp_rad = 0.0;
        samples[1].yp_rad = 0.0;
        samples[1].sp_rad = 0.0;
        samples[1].lod_seconds = 0.0;
        samples[1].dx_rad = 0.0;
        samples[1].dy_rad = 0.0;

        EarthOrientationTable table;
        table.samples = samples;
        table.count = 2;

        EarthOrientationSample out;
        expect_false(
            interpolate_earth_orientation(&table, 999.5, &out),
            "out of range low returns false",
            &failures);
        expect_false(
            interpolate_earth_orientation(&table, 1001.5, &out),
            "out of range high returns false",
            &failures);
        expect_true(
            interpolate_earth_orientation(&table, 1000.0, &out),
            "exact start returns true",
            &failures);
        expect_near(out.dut1_seconds, 1.0, 0.0, "exact start value", &failures);
    }

    // Test 4: Rate derivation with handcrafted table
    {
        EarthOrientationSample samples[3];
        samples[0].jd_utc = 1000.0;
        samples[0].dut1_seconds = 1.0;
        samples[0].xp_rad = 10.0e-6;
        samples[0].yp_rad = -6.0e-6;
        samples[0].sp_rad = 1.0e-9;
        samples[0].lod_seconds = 0.01;
        samples[0].dx_rad = 0.1e-6;
        samples[0].dy_rad = -0.2e-6;

        samples[1].jd_utc = 1002.0;
        samples[1].dut1_seconds = 5.0;
        samples[1].xp_rad = 18.0e-6;
        samples[1].yp_rad = -2.0e-6;
        samples[1].sp_rad = 5.0e-9;
        samples[1].lod_seconds = 0.05;
        samples[1].dx_rad = 0.7e-6;
        samples[1].dy_rad = 0.2e-6;

        samples[2].jd_utc = 1005.0;
        samples[2].dut1_seconds = 14.0;
        samples[2].xp_rad = 33.0e-6;
        samples[2].yp_rad = 7.0e-6;
        samples[2].sp_rad = 11.0e-9;
        samples[2].lod_seconds = 0.11;
        samples[2].dx_rad = 1.6e-6;
        samples[2].dy_rad = 1.1e-6;

        EarthOrientationTable table;
        table.samples = samples;
        table.count = 3;

        EarthOrientationRates rates;
        EarthRotationDerivatives derivs;
        expect_true(
            derive_earth_orientation_rates(&table, 1001.0, &rates, &derivs),
            "rate derivation at midpoint succeeds",
            &failures);
        expect_near(rates.xp_rate_rad_per_day, 4.0e-6, 1e-20, "xp_rate at 1001", &failures);
        expect_near(rates.yp_rate_rad_per_day, 2.0e-6, 1e-20, "yp_rate at 1001", &failures);
        expect_near(rates.sp_rate_rad_per_day, 2.0e-9, 1e-23, "sp_rate at 1001", &failures);
        expect_near(rates.dx_rate_rad_per_day, 0.3e-6, 1e-20, "dx_rate at 1001", &failures);
        expect_near(rates.dy_rate_rad_per_day, 0.2e-6, 1e-20, "dy_rate at 1001", &failures);
        expect_near(derivs.dut1_rate_seconds_per_day, 2.0, 0.0, "dut1_rate at 1001", &failures);
        expect_near(derivs.lod_seconds, 0.03, 1e-15, "lod at 1001", &failures);
        expect_near(derivs.lod_rate_seconds_per_day, 0.02, 1e-15, "lod_rate at 1001", &failures);

        // At second sample (jd=1002), should use pair (1,2) with dt=3
        expect_true(
            derive_earth_orientation_rates(&table, 1002.0, &rates, &derivs),
            "rate derivation at sample 1 succeeds",
            &failures);
        expect_near(rates.xp_rate_rad_per_day, 5.0e-6, 1e-20, "xp_rate at 1002", &failures);
        expect_near(rates.yp_rate_rad_per_day, 3.0e-6, 1e-20, "yp_rate at 1002", &failures);
        expect_near(derivs.dut1_rate_seconds_per_day, 3.0, 0.0, "dut1_rate at 1002", &failures);
        expect_near(derivs.lod_rate_seconds_per_day, 0.02, 1e-15, "lod_rate at 1002", &failures);

        // At last sample (jd=1005)
        expect_true(
            derive_earth_orientation_rates(&table, 1005.0, &rates, &derivs),
            "rate derivation at last sample succeeds",
            &failures);
        expect_near(rates.xp_rate_rad_per_day, 5.0e-6, 1e-20, "xp_rate at 1005", &failures);
        expect_near(derivs.lod_seconds, 0.11, 0.0, "lod at 1005", &failures);
    }

    // Test 5: Builtin table
    {
        EarthOrientationTable table;
        expect_true(
            load_builtin_eop_table(&table),
            "builtin EOP table loads",
            &failures);
        assert(table.count > 19000);
        expect_near(table.samples[0].jd_utc, 2441684.5, 0.0, "builtin first jd", &failures);
        expect_near(table.samples[0].dut1_seconds, 0.8084178, 0.0, "builtin first dut1", &failures);
        expect_near(table.samples[0].xp_rad, 0.120733 * taiyin::TAIYIN_ARCSEC_TO_RAD, 1e-20, "builtin first xp", &failures);
        expect_near(table.samples[0].yp_rad, 0.136966 * taiyin::TAIYIN_ARCSEC_TO_RAD, 1e-20, "builtin first yp", &failures);

        destroy_earth_orientation_table(&table);
    }

    // Test 6: Builtin rate derivation
    {
        EarthOrientationTable table;
        expect_true(load_builtin_eop_table(&table), "builtin table loads for rate test", &failures);

        const double test_jd = 2460310.75;
        EarthOrientationRates rates;
        EarthRotationDerivatives derivs;
        expect_true(
            derive_earth_orientation_rates(&table, test_jd, &rates, &derivs),
            "builtin rate derivation succeeds",
            &failures);

        // Manually find bracketing pair and verify
        size_t upper = 0;
        while (upper < table.count && table.samples[upper].jd_utc < test_jd) {
            ++upper;
        }
        expect_true(upper > 0 && upper < table.count, "found bracketing pair", &failures);
        const EarthOrientationSample& sa = table.samples[upper - 1];
        const EarthOrientationSample& sb = table.samples[upper];
        const double dt = sb.jd_utc - sa.jd_utc;
        expect_near(rates.xp_rate_rad_per_day, (sb.xp_rad - sa.xp_rad) / dt, 0.0, "builtin xp_rate manual check", &failures);
        expect_near(rates.yp_rate_rad_per_day, (sb.yp_rad - sa.yp_rad) / dt, 0.0, "builtin yp_rate manual check", &failures);
        expect_near(derivs.dut1_rate_seconds_per_day, (sb.dut1_seconds - sa.dut1_seconds) / dt, 0.0, "builtin dut1_rate manual check", &failures);

        // LOD should match interpolated value
        EarthOrientationSample interp;
        expect_true(interpolate_earth_orientation(&table, test_jd, &interp), "interpolate for lod check", &failures);
        expect_near(derivs.lod_seconds, interp.lod_seconds, 0.0, "builtin lod matches interpolation", &failures);

        destroy_earth_orientation_table(&table);
    }

    // Test 7: Edge cases
    {
        EarthOrientationSample samples[1];
        samples[0].jd_utc = 1000.0;
        samples[0].dut1_seconds = 1.0;
        samples[0].xp_rad = 0.0;
        samples[0].yp_rad = 0.0;
        samples[0].sp_rad = 0.0;
        samples[0].lod_seconds = 0.0;
        samples[0].dx_rad = 0.0;
        samples[0].dy_rad = 0.0;

        EarthOrientationTable one_table;
        one_table.samples = samples;
        one_table.count = 1;

        EarthOrientationRates rates;
        EarthRotationDerivatives derivs;
        expect_false(
            derive_earth_orientation_rates(&one_table, 1000.0, &rates, &derivs),
            "1-sample rate returns false",
            &failures);

        EarthOrientationTable empty_table;
        empty_table.samples = samples;
        empty_table.count = 0;

        EarthOrientationSample out;
        expect_false(
            interpolate_earth_orientation(&empty_table, 1000.0, &out),
            "empty table interpolation returns false",
            &failures);

        expect_false(
            interpolate_earth_orientation(0, 1000.0, &out),
            "null table returns false",
            &failures);
        expect_false(
            interpolate_earth_orientation(&one_table, 1000.0, 0),
            "null out returns false",
            &failures);
        expect_false(
            derive_earth_orientation_rates(0, 1000.0, &rates, &derivs),
            "null table rate returns false",
            &failures);
        expect_false(
            derive_earth_orientation_rates(&one_table, 1000.0, 0, &derivs),
            "null rates returns false",
            &failures);
        expect_false(
            derive_earth_orientation_rates(&one_table, 1000.0, &rates, 0),
            "null derivs returns false",
            &failures);

        EarthOrientationSample parsed[10];
        size_t count = 0;
        expect_false(
            parse_finals2000a_table("no usable rows\n", 15, parsed, 10, &count),
            "unparseable text returns false",
            &failures);
    }

    return failures == 0 ? 0 : 1;
}