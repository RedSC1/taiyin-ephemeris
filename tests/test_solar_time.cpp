#include "taiyin/angle.h"
#include "taiyin/runtime/native_context.h"
#include "taiyin/runtime/runtime.h"
#include "taiyin/runtime/solar_time.h"
#include "taiyin/status.h"
#include "taiyin/time.h"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <string>

namespace {

taiyin::SplitJulianDate split_jd(double jd) {
    taiyin::SplitJulianDate out;
    taiyin::split_julian_date_from_double(jd, &out);
    return out;
}

void expect_true(bool value, const char* label, int* failures) {
    if (!value) {
        std::cerr << "FAIL: " << label << "\n";
        ++*failures;
    }
}

void expect_status(
    taiyin::Status actual,
    taiyin::Status expected,
    const char* label,
    int* failures
) {
    if (actual != expected) {
        std::cerr << "FAIL: " << label << " actual=" << actual
                  << " expected=" << expected << "\n";
        ++*failures;
    }
}

void expect_near(
    double actual,
    double expected,
    double tolerance,
    const char* label,
    int* failures
) {
    if (!std::isfinite(actual) || std::fabs(actual - expected) > tolerance) {
        std::cerr << "FAIL: " << label << " actual=" << actual
                  << " expected=" << expected << " tolerance=" << tolerance << "\n";
        ++*failures;
    }
}

std::string packaged_data_root() {
    const char* root = std::getenv("TAIYIN_REPO_ROOT");
    return root && root[0] != '\0'
        ? std::string(root) + "/data/ephemerides/opm2/major-bodies/600y"
        : "../data/ephemerides/opm2/major-bodies/600y";
}

bool initialize_runtime(int* failures) {
    const std::string path = packaged_data_root();
    const char* paths[] = { path.c_str() };
    taiyin::runtime::EphemerisRuntimeConfig config;
    config.source_paths = paths;
    config.source_path_count = 1;
    config.load_packaged_data = true;
    config.segment_cache_max_entries = 128;
    const bool ok = taiyin::runtime::initialize_global_ephemeris_runtime(config);
    expect_true(ok, "initialize packaged OPM2 runtime", failures);
    return ok;
}

}  // namespace

int main() {
    int failures = 0;
    if (!initialize_runtime(&failures)) return 1;

    taiyin::runtime::NativeCalcContext context;
    expect_status(
        taiyin::runtime::native_context_set_geocentric_observer(
            &context, taiyin::TAIYIN_BODY_EARTH, taiyin::TAIYIN_BODY_EARTH),
        taiyin::TAIYIN_STATUS_OK,
        "set geocentric observer",
        &failures);

    const taiyin::SplitJulianDate jd_ut = split_jd(2460311.0);
    taiyin::runtime::EquationOfTimeResult equation;
    taiyin::runtime::EphemerisEvalDiagnostic diagnostic;
    expect_status(
        taiyin::runtime::calc_equation_of_time_ut(
            &context, jd_ut, &equation, &diagnostic),
        taiyin::TAIYIN_STATUS_OK,
        "calculate equation of time",
        &failures);
    expect_true(
        equation.equation_seconds < -150.0 && equation.equation_seconds > -250.0,
        "January equation of time has expected sign and scale",
        &failures);
    // pyswisseph 2.10.03 time_equ(), Moshier fallback, at the same UT epoch.
    // The loose cross-ephemeris allowance is separate from the tight internal
    // UT/TT and conversion consistency checks below.
    expect_near(
        equation.equation_seconds,
        -198.9342282623329,
        2.0,
        "equation of time vs Swiss oracle",
        &failures);
    const double swiss_oracle_cases[][2] = {
        {2451545.0, -197.11531440430917},
        {2460409.0, -102.17101941988405},
        {2460676.5, -206.5203796885362},
    };
    for (size_t i = 0;
         i < sizeof(swiss_oracle_cases) / sizeof(swiss_oracle_cases[0]);
         ++i) {
        taiyin::runtime::EquationOfTimeResult oracle_equation;
        expect_status(
            taiyin::runtime::calc_equation_of_time_ut(
                &context, split_jd(swiss_oracle_cases[i][0]),
                &oracle_equation, &diagnostic),
            taiyin::TAIYIN_STATUS_OK,
            "calculate equation-of-time oracle case",
            &failures);
        expect_near(
            oracle_equation.equation_seconds,
            swiss_oracle_cases[i][1],
            2.0,
            "equation of time multi-epoch Swiss oracle",
            &failures);
    }
    expect_near(
        equation.equation_days * taiyin::SECONDS_PER_DAY,
        equation.equation_seconds,
        1.0e-12,
        "equation-of-time units agree",
        &failures);

    taiyin::runtime::EquationOfTimeResult equation_tt;
    expect_status(
        taiyin::runtime::calc_equation_of_time_tt(
            &context, equation.jd_tt, &equation_tt, &diagnostic),
        taiyin::TAIYIN_STATUS_OK,
        "calculate equation of time from TT",
        &failures);
    expect_near(
        equation_tt.equation_seconds,
        equation.equation_seconds,
        2.0e-5,
        "UT and TT equation-of-time paths agree",
        &failures);

    const double longitude = 116.3833 * taiyin::TAIYIN_DEG_TO_RAD;
    taiyin::runtime::NativeCalcContext topocentric_context = context;
    expect_status(
        taiyin::runtime::native_context_set_simple_topocentric_observer(
            &topocentric_context,
            taiyin::runtime::native_observer_location_degrees(
                116.3833, 39.9167, 50.0),
            jd_ut,
            equation.jd_tt),
        taiyin::TAIYIN_STATUS_OK,
        "install topocentric observer",
        &failures);
    taiyin::runtime::EquationOfTimeResult topocentric_equation;
    expect_status(
        taiyin::runtime::calc_equation_of_time_ut(
            &topocentric_context, jd_ut, &topocentric_equation, &diagnostic),
        taiyin::TAIYIN_STATUS_OK,
        "calculate global equation of time from topocentric context",
        &failures);
    expect_near(
        topocentric_equation.equation_seconds,
        equation.equation_seconds,
        1.0e-10,
        "equation of time clears topocentric state",
        &failures);

    const taiyin::SplitJulianDate jd_local_mean =
        jd_ut + longitude / taiyin::TAIYIN_TWO_PI;
    taiyin::SplitJulianDate jd_local_apparent;
    expect_status(
        taiyin::runtime::local_mean_to_apparent_solar_time(
            &context, jd_local_mean, longitude, &jd_local_apparent, &diagnostic),
        taiyin::TAIYIN_STATUS_OK,
        "convert local mean to apparent solar time",
        &failures);
    expect_near(
        jd_local_apparent - jd_local_mean,
        equation.equation_days,
        1.0e-9,
        "LMT to LAT applies equation of time",
        &failures);
    taiyin::SplitJulianDate round_trip_local_mean;
    expect_status(
        taiyin::runtime::local_apparent_to_mean_solar_time(
            &context, jd_local_apparent, longitude, &round_trip_local_mean, &diagnostic),
        taiyin::TAIYIN_STATUS_OK,
        "convert local apparent to mean solar time",
        &failures);
    expect_near(
        round_trip_local_mean - jd_local_mean,
        0.0,
        1.0e-9,
        "LMT/LAT conversion round trip",
        &failures);

    expect_status(
        taiyin::runtime::calc_equation_of_time_ut(
            nullptr, jd_ut, &equation, &diagnostic),
        taiyin::TAIYIN_ERROR_INVALID_ARGUMENT,
        "reject null equation-of-time context",
        &failures);
    expect_status(
        taiyin::runtime::local_mean_to_apparent_solar_time(
            &context, jd_local_mean, NAN, &jd_local_apparent, &diagnostic),
        taiyin::TAIYIN_ERROR_INVALID_ARGUMENT,
        "reject non-finite solar-time longitude",
        &failures);
    expect_status(
        taiyin::runtime::local_mean_to_apparent_solar_time(
            &context, jd_local_mean, 1.1 * taiyin::TAIYIN_PI,
            &jd_local_apparent, &diagnostic),
        taiyin::TAIYIN_ERROR_INVALID_ARGUMENT,
        "reject out-of-range solar-time longitude",
        &failures);
    if (failures != 0) {
        std::cerr << failures << " solar-time test(s) failed\n";
        return 1;
    }
    std::cout << "solar-time tests passed\n";
    return 0;
}
