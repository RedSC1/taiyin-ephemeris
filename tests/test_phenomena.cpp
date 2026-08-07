#include "taiyin/body_id.h"
#include "taiyin/angle.h"
#include "taiyin/runtime/native_context.h"
#include "taiyin/runtime/native_position.h"
#include "taiyin/runtime/phenomena.h"
#include "taiyin/runtime/runtime.h"
#include "taiyin/status.h"
#include "taiyin/time.h"

#include "runtime/events/phenomena_internal.h"

#include <cmath>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <string>

namespace {

taiyin::SplitJulianDate split_jd(double jd) {
    taiyin::SplitJulianDate out;
    if (!taiyin::split_julian_date_from_double(jd, &out)) {
        out.day_fraction = NAN;
    }
    return out;
}

void expect_true(bool value, const char* label, int* failures) {
    if (!value) {
        std::cerr << "FAIL: expected true: " << label << "\n";
        ++(*failures);
    }
}

void expect_status(taiyin::Status actual, taiyin::Status expected, const char* label, int* failures) {
    if (actual != expected) {
        std::cerr << "FAIL: " << label << ": actual=" << actual
                  << " expected=" << expected << "\n";
        ++(*failures);
    }
}

void expect_near(double actual, double expected, double tolerance, const char* label, int* failures) {
    const double diff = std::fabs(actual - expected);
    if (!(diff <= tolerance)) {
        std::cerr << "FAIL: " << label << ": actual=" << actual
                  << " expected=" << expected
                  << " diff=" << diff
                  << " tolerance=" << tolerance << "\n";
        ++(*failures);
    }
}

double magnitude_tolerance_for_body(int body_id) {
    if (body_id == taiyin::TAIYIN_BODY_MOON) {
        return 0.10;
    }
    return 0.08;
}

bool print_swiss_pheno_diffs() {
    const char* value = std::getenv("TAIYIN_PRINT_SWISS_PHENO_DIFFS");
    return value && value[0] != '\0' && value[0] != '0';
}

void expect_nan(double value, const char* label, int* failures) {
    if (!std::isnan(value)) {
        std::cerr << "FAIL: expected nan: " << label << ": actual=" << value << "\n";
        ++(*failures);
    }
}

void test_formula_sanity(int* failures) {
    namespace internal = taiyin::runtime::internal;

    expect_near(
        internal::phenomena_sun_apparent_magnitude(1.0),
        -26.74,
        1.0e-15,
        "Sun magnitude at 1 AU",
        failures);
    expect_near(
        internal::phenomena_sun_apparent_magnitude(0.5),
        -28.245149978319905,
        1.0e-14,
        "Sun magnitude at 0.5 AU",
        failures);
    expect_nan(
        internal::phenomena_sun_apparent_magnitude(0.0),
        "Sun magnitude rejects zero distance",
        failures);

    expect_near(
        internal::phenomena_moon_apparent_magnitude(0.0, 1.0, 1.0, true),
        0.21,
        1.0e-15,
        "Moon magnitude full phase",
        failures);
    expect_near(
        internal::phenomena_moon_apparent_magnitude(90.0, 1.0, 1.0, true),
        2.9217350397,
        1.0e-12,
        "Moon magnitude before full branch",
        failures);
    expect_near(
        internal::phenomena_moon_apparent_magnitude(90.0, 1.0, 1.0, false),
        2.9369448390,
        1.0e-12,
        "Moon magnitude after full branch",
        failures);
    expect_near(
        internal::phenomena_moon_apparent_magnitude(160.0, 1.0, 1.0, true),
        7.549032875296764,
        1.0e-12,
        "Moon magnitude crescent branch",
        failures);
    expect_nan(
        internal::phenomena_moon_apparent_magnitude(180.0, 1.0, 1.0, true),
        "Moon magnitude rejects zero crescent width",
        failures);

    expect_near(
        internal::phenomena_mars_magnitude_correction('R', 0.0),
        0.024,
        1.0e-15,
        "Mars rotation correction 0 deg",
        failures);
    expect_near(
        internal::phenomena_mars_magnitude_correction('R', 10.0),
        0.034,
        1.0e-15,
        "Mars rotation correction 10 deg",
        failures);
    expect_near(
        internal::phenomena_mars_magnitude_correction('R', 350.0),
        0.022,
        1.0e-15,
        "Mars rotation correction 350 deg",
        failures);
    expect_near(
        internal::phenomena_mars_magnitude_correction('R', 360.0),
        0.024,
        1.0e-15,
        "Mars rotation correction wraps 360 deg",
        failures);
    expect_near(
        internal::phenomena_mars_magnitude_correction('O', 0.0),
        -0.030,
        1.0e-15,
        "Mars orbit correction 0 deg",
        failures);
    expect_near(
        internal::phenomena_mars_magnitude_correction('O', 350.0),
        0.019,
        1.0e-15,
        "Mars orbit correction 350 deg",
        failures);
    expect_true(
        std::isfinite(internal::phenomena_mars_magnitude_correction('R', 5.0)),
        "Mars correction interpolates between grid points",
        failures);
    expect_nan(
        internal::phenomena_mars_magnitude_correction('R', NAN),
        "Mars correction rejects nan angle",
        failures);

    expect_near(
        internal::phenomena_hg_phase_function(0.0, -0.55, 0.15),
        -0.55,
        1.0e-15,
        "H-G phase at zero phase",
        failures);
    expect_near(
        internal::phenomena_hg_phase_function(30.0, -0.55, 0.15),
        0.749093559584227,
        1.0e-12,
        "H-G phase at 30 deg",
        failures);
    expect_nan(
        internal::phenomena_hg_phase_function(120.0, -0.55, 0.15),
        "H-G phase rejects 120 deg boundary",
        failures);

    expect_nan(
        internal::phenomena_neptune_phase_magnitude_term(split_jd(2448008.5), 2.0),
        "Neptune pre-2000 large phase unsupported",
        failures);
    expect_true(
        std::isfinite(internal::phenomena_neptune_phase_magnitude_term(
            split_jd(2460409.25), 2.0)),
        "Neptune post-2000 large phase supported",
        failures);
}

std::string repo_opm2_major_body_root() {
    const char* root = std::getenv("TAIYIN_REPO_ROOT");
    if (root && root[0] != '\0') {
        return std::string(root) + "/data/ephemerides/opm2/major-bodies/600y";
    }
    return "../data/ephemerides/opm2/major-bodies/600y";
}

std::string repo_opm2_cob_full_root() {
    const char* root = std::getenv("TAIYIN_REPO_ROOT");
    if (root && root[0] != '\0') {
        return std::string(root) + "/data/ephemerides/opm2/cob/full";
    }
    return "../data/ephemerides/opm2/cob/full";
}

std::string repo_opm2_uranus_cob_root() {
    const char* root = std::getenv("TAIYIN_REPO_ROOT");
    if (root && root[0] != '\0') {
        return std::string(root) + "/data/ephemerides/opm2/cob/slices/uranus_1600_2200";
    }
    return "../data/ephemerides/opm2/cob/slices/uranus_1600_2200";
}

bool initialize_runtime(int* failures) {
    taiyin::runtime::EphemerisRuntimeConfig config;
    const std::string major_root = repo_opm2_major_body_root();
    const std::string cob_root = repo_opm2_cob_full_root();
    const std::string uranus_cob_root = repo_opm2_uranus_cob_root();
    const char* source_paths[] = { major_root.c_str(), cob_root.c_str(), uranus_cob_root.c_str() };
    config.source_paths = source_paths;
    config.source_path_count = 3;
    config.load_packaged_data = false;
    config.segment_cache_max_entries = 256;
    const bool ok = taiyin::runtime::initialize_global_ephemeris_runtime(config);
    expect_true(ok, "initialize OPM2 runtime", failures);
    return ok;
}

double jd_ut(int year, int month, int day, double hour) {
    return taiyin::julian_day({year, month, day, static_cast<int>(hour), 0, (hour - static_cast<int>(hour)) * 3600.0});
}

taiyin::runtime::NativeCalcContext make_context() {
    taiyin::runtime::NativeCalcContext context;
    taiyin::runtime::native_context_set_geocentric_observer(
        &context,
        taiyin::TAIYIN_BODY_EARTH,
        taiyin::TAIYIN_BODY_EARTH);
    taiyin::runtime::native_context_use_solar_deflector(&context);
    context.apparent_options.flags =
        taiyin::TAIYIN_APPARENT_SPHERICAL
        | taiyin::TAIYIN_APPARENT_LIGHT_TIME
        | taiyin::TAIYIN_APPARENT_ABERRATION
        | taiyin::TAIYIN_APPARENT_DEFLECTION;
    context.apparent_options.output_frame_id = taiyin::TAIYIN_APPARENT_FRAME_TRUE_ECLIPTIC_OF_DATE;
    return context;
}

void test_geometric_identities(int* failures) {
    taiyin::runtime::NativeCalcContext context = make_context();
    taiyin::runtime::BodyPhenomena ph;
    taiyin::runtime::EphemerisEvalDiagnostic diagnostic;
    const double jd = jd_ut(2024, 4, 8, 18.0);
    expect_status(
        taiyin::runtime::calc_body_phenomena_ut(
            &context,
            taiyin::TAIYIN_BODY_MOON,
            split_jd(jd),
            0u,
            &ph,
            &diagnostic),
        taiyin::TAIYIN_STATUS_OK,
        "Moon phenomena status",
        failures);
    expect_true(diagnostic.target_id == taiyin::TAIYIN_BODY_MOON, "Moon success diagnostic target", failures);
    expect_true(std::isfinite(ph.phase_angle_rad), "Moon phase angle finite", failures);
    expect_true(std::isfinite(ph.illuminated_fraction), "Moon illuminated fraction finite", failures);
    expect_true(std::isfinite(ph.solar_elongation_rad), "Moon elongation finite", failures);
    expect_true(std::isfinite(ph.apparent_diameter_rad), "Moon diameter finite", failures);
    expect_true(std::isfinite(ph.apparent_magnitude), "Moon apparent magnitude finite", failures);
    expect_true(std::isfinite(ph.horizontal_parallax_rad), "Moon horizontal parallax finite", failures);
    expect_near(
        ph.illuminated_fraction,
        0.5 * (1.0 + std::cos(ph.phase_angle_rad)),
        1.0e-15,
        "Moon illuminated fraction identity",
        failures);

    taiyin::runtime::BodyPhenomena sun;
    expect_status(
        taiyin::runtime::calc_body_phenomena_ut(
            &context,
            taiyin::TAIYIN_BODY_SUN,
            split_jd(jd),
            0u,
            &sun,
            &diagnostic),
        taiyin::TAIYIN_STATUS_OK,
        "Sun phenomena status",
        failures);
    expect_true(diagnostic.target_id == taiyin::TAIYIN_BODY_SUN, "Sun success diagnostic target", failures);
    expect_near(sun.phase_angle_rad, 0.0, 0.0, "Sun phase angle convention", failures);
    expect_near(sun.illuminated_fraction, 1.0, 0.0, "Sun illuminated fraction convention", failures);
    expect_near(sun.solar_elongation_rad, 0.0, 0.0, "Sun elongation convention", failures);
    expect_true(std::isfinite(sun.apparent_diameter_rad), "Sun diameter finite", failures);
    expect_true(std::isfinite(sun.apparent_magnitude), "Sun apparent magnitude finite", failures);
    expect_true(std::isnan(sun.horizontal_parallax_rad), "Sun horizontal parallax is nan", failures);

    taiyin::runtime::BodyPhenomena pluto;
    expect_status(
        taiyin::runtime::calc_body_phenomena_ut(
            &context,
            taiyin::TAIYIN_BODY_PLUTO,
            split_jd(jd),
            0u,
            &pluto,
            &diagnostic),
        taiyin::TAIYIN_STATUS_OK,
        "Pluto phenomena status",
        failures);
    expect_true(diagnostic.target_id == taiyin::TAIYIN_BODY_PLUTO, "Pluto success diagnostic target", failures);
    expect_true(std::isfinite(pluto.apparent_magnitude), "Pluto apparent magnitude finite", failures);
}

void test_moon_horizontal_parallax_stays_geocentric(int* failures) {
    taiyin::runtime::NativeCalcContext geocentric = make_context();
    taiyin::runtime::NativeCalcContext topocentric = make_context();
    const double jd = jd_ut(2024, 4, 8, 18.0);
    const taiyin::runtime::NativeObserverLocation denver =
        taiyin::runtime::native_observer_location_degrees(-104.9903, 39.7392, 1609.3);
    expect_status(
        taiyin::runtime::native_context_set_simple_topocentric_observer(
            &topocentric,
            denver,
            split_jd(jd),
            split_jd(jd)),
        taiyin::TAIYIN_STATUS_OK,
        "set topocentric observer for phenomena",
        failures);

    taiyin::runtime::BodyPhenomena geo_ph;
    taiyin::runtime::BodyPhenomena topo_ph;
    taiyin::runtime::EphemerisEvalDiagnostic diagnostic;
    expect_status(
        taiyin::runtime::calc_body_phenomena_ut(
            &geocentric,
            taiyin::TAIYIN_BODY_MOON,
            split_jd(jd),
            0u,
            &geo_ph,
            &diagnostic),
        taiyin::TAIYIN_STATUS_OK,
        "geocentric Moon phenomena status",
        failures);
    expect_status(
        taiyin::runtime::calc_body_phenomena_ut(
            &topocentric,
            taiyin::TAIYIN_BODY_MOON,
            split_jd(jd),
            taiyin::runtime::TAIYIN_NATIVE_POSITION_TOPOCENTRIC,
            &topo_ph,
            &diagnostic),
        taiyin::TAIYIN_STATUS_OK,
        "topocentric Moon phenomena status",
        failures);
    expect_near(
        topo_ph.horizontal_parallax_rad,
        geo_ph.horizontal_parallax_rad,
        1.0e-15,
        "Moon horizontal parallax remains geocentric",
        failures);
    expect_true(
        std::fabs(topo_ph.apparent_diameter_rad - geo_ph.apparent_diameter_rad) > 1.0e-12,
        "topocentric Moon apparent diameter still uses observer distance",
        failures);
}

void test_barycenter_approximation_flag(int* failures) {
    taiyin::runtime::NativeCalcContext context = make_context();
    taiyin::runtime::BodyPhenomena mars;
    taiyin::runtime::EphemerisEvalDiagnostic diagnostic;
    const double jd = jd_ut(2024, 4, 8, 18.0);

    const taiyin::Status strict_status = taiyin::runtime::calc_body_phenomena_ut(
        &context,
        taiyin::TAIYIN_BODY_MARS,
        split_jd(jd),
        0u,
        &mars,
        &diagnostic);
    expect_true(
        strict_status != taiyin::TAIYIN_STATUS_OK,
        "Mars body without approximation flag is strict",
        failures);

    expect_status(
        taiyin::runtime::calc_body_phenomena_ut(
            &context,
            taiyin::TAIYIN_BODY_MARS,
            split_jd(jd),
            taiyin::runtime::TAIYIN_NATIVE_POSITION_ALLOW_BARYCENTER_APPROX,
            &mars,
            &diagnostic),
        taiyin::TAIYIN_STATUS_OK,
        "Mars phenomena allows explicit barycenter approximation",
        failures);
    expect_true(diagnostic.target_id == taiyin::TAIYIN_BODY_MARS, "Mars approximation diagnostic target", failures);
    expect_true(
        diagnostic.component_target_id == taiyin::TAIYIN_BODY_MARS_BARYCENTER,
        "Mars approximation diagnostic component target",
        failures);
    expect_true(std::isfinite(mars.phase_angle_rad), "Mars approximation phase angle finite", failures);
    expect_true(std::isfinite(mars.apparent_magnitude), "Mars approximation magnitude finite", failures);
}

void test_native_barycenter_approximation_flag(int* failures) {
    taiyin::runtime::NativeCalcContext context = make_context();
    taiyin::runtime::EphemerisEvalDiagnostic diagnostic;
    double out[6] = {};
    const double jd = jd_ut(2024, 4, 8, 18.0);
    const uint32_t position_flags =
        taiyin::runtime::TAIYIN_NATIVE_POSITION_XYZ
        | taiyin::runtime::TAIYIN_NATIVE_POSITION_TRUEPOS;

    const taiyin::Status mars_strict_status = taiyin::runtime::calc_position_ut(
        &context,
        taiyin::TAIYIN_BODY_MARS,
        split_jd(jd),
        position_flags,
        out,
        &diagnostic);
    expect_true(
        mars_strict_status != taiyin::TAIYIN_STATUS_OK,
        "native Mars body without approximation flag is strict",
        failures);

    expect_status(
        taiyin::runtime::calc_position_ut(
            &context,
            taiyin::TAIYIN_BODY_MARS,
            split_jd(jd),
            position_flags | taiyin::runtime::TAIYIN_NATIVE_POSITION_ALLOW_BARYCENTER_APPROX,
            out,
            &diagnostic),
        taiyin::TAIYIN_STATUS_OK,
        "native Mars position allows explicit barycenter approximation",
        failures);
    expect_true(diagnostic.target_id == taiyin::TAIYIN_BODY_MARS, "native Mars approximation target", failures);
    expect_true(
        diagnostic.component_target_id == taiyin::TAIYIN_BODY_MARS_BARYCENTER,
        "native Mars approximation component target",
        failures);
    expect_true(std::isfinite(out[0]) && std::isfinite(out[1]) && std::isfinite(out[2]),
        "native Mars approximation xyz finite",
        failures);

    expect_status(
        taiyin::runtime::calc_position_ut(
            &context,
            taiyin::TAIYIN_BODY_MARS_BARYCENTER,
            split_jd(jd),
            position_flags | taiyin::runtime::TAIYIN_NATIVE_POSITION_ALLOW_BARYCENTER_APPROX,
            out,
            &diagnostic),
        taiyin::TAIYIN_STATUS_OK,
        "native Mars barycenter ignores body approximation flag",
        failures);
    expect_true(
        diagnostic.target_id == taiyin::TAIYIN_BODY_MARS_BARYCENTER,
        "native Mars barycenter remains diagnostic target",
        failures);
    expect_true(
        diagnostic.component_target_id != taiyin::TAIYIN_BODY_MARS_BARYCENTER,
        "native Mars barycenter is not reported as approximation component",
        failures);

    expect_status(
        taiyin::runtime::calc_position_ut(
            &context,
            taiyin::TAIYIN_BODY_MOON,
            split_jd(jd),
            position_flags | taiyin::runtime::TAIYIN_NATIVE_POSITION_ALLOW_BARYCENTER_APPROX,
            out,
            &diagnostic),
        taiyin::TAIYIN_STATUS_OK,
        "native Moon ignores major-planet approximation flag",
        failures);
    expect_true(
        diagnostic.target_id == taiyin::TAIYIN_BODY_MOON,
        "native Moon remains diagnostic target",
        failures);

    const int target_ids[] = {taiyin::TAIYIN_BODY_MOON, taiyin::TAIYIN_BODY_MARS};
    double batch_out[12] = {};
    taiyin::runtime::EphemerisEvalDiagnostic batch_diagnostics[2];
    const taiyin::Status batch_strict_status = taiyin::runtime::calc_positions_ut(
        &context,
        target_ids,
        2,
        split_jd(jd),
        position_flags,
        batch_out,
        batch_diagnostics);
    expect_true(
        batch_strict_status != taiyin::TAIYIN_STATUS_OK,
        "native batch without approximation flag keeps Mars strict",
        failures);
    expect_status(
        taiyin::runtime::calc_positions_ut(
            &context,
            target_ids,
            2,
            split_jd(jd),
            position_flags | taiyin::runtime::TAIYIN_NATIVE_POSITION_ALLOW_BARYCENTER_APPROX,
            batch_out,
            batch_diagnostics),
        taiyin::TAIYIN_STATUS_OK,
        "native batch allows explicit Mars barycenter approximation",
        failures);
    expect_true(
        batch_diagnostics[0].target_id == taiyin::TAIYIN_BODY_MOON,
        "native batch Moon target unchanged",
        failures);
    expect_true(
        batch_diagnostics[1].target_id == taiyin::TAIYIN_BODY_MARS,
        "native batch Mars requested target preserved",
        failures);
    expect_true(
        batch_diagnostics[1].component_target_id == taiyin::TAIYIN_BODY_MARS_BARYCENTER,
        "native batch Mars approximation component target",
        failures);
    expect_true(
        std::isfinite(batch_out[0]) && std::isfinite(batch_out[1]) && std::isfinite(batch_out[2])
            && std::isfinite(batch_out[6]) && std::isfinite(batch_out[7]) && std::isfinite(batch_out[8]),
        "native batch approximation xyz finite",
        failures);
}

void test_documented_example_smoke(int* failures) {
    taiyin::runtime::NativeCalcContext context;
    taiyin::runtime::native_context_set_geocentric_observer(
        &context,
        taiyin::TAIYIN_BODY_EARTH,
        taiyin::TAIYIN_BODY_EARTH);
    taiyin::runtime::native_context_use_solar_deflector(&context);
    context.apparent_options.flags =
        taiyin::TAIYIN_APPARENT_LIGHT_TIME
        | taiyin::TAIYIN_APPARENT_ABERRATION
        | taiyin::TAIYIN_APPARENT_DEFLECTION;

    const double jd = jd_ut(2024, 4, 8, 18.0);
    taiyin::runtime::BodyPhenomena moon;
    taiyin::runtime::EphemerisEvalDiagnostic diagnostic;
    expect_status(
        taiyin::runtime::calc_body_phenomena_ut(
            &context,
            taiyin::TAIYIN_BODY_MOON,
            split_jd(jd),
            0u,
            &moon,
            &diagnostic),
        taiyin::TAIYIN_STATUS_OK,
        "documented phenomena example computes Moon",
        failures);
    expect_true(std::isfinite(moon.phase_angle_rad), "documented example phase finite", failures);
    expect_true(std::isfinite(moon.apparent_magnitude), "documented example magnitude finite", failures);
    expect_true(std::isfinite(moon.horizontal_parallax_rad), "documented example parallax finite", failures);
}

void test_swiss_pheno_geometry_oracles(int* failures) {
    struct OracleCase {
        const char* label;
        int body_id;
        double jd_ut;
        double phase_angle_deg;
        double illuminated_fraction;
        double elongation_deg;
        double apparent_diameter_deg;
        double apparent_magnitude;
        double horizontal_parallax_deg;
    };

    const double jd = 2460409.25;
    const OracleCase cases[] = {
        {"Moon", taiyin::TAIYIN_BODY_MOON, 2460409.250000000000, 179.612142848809810, 0.000011456097256, 0.386834397148720, 0.553402655756206, 7.295320378379385, 1.015785084167904},
        {"Moon first quarter", taiyin::TAIYIN_BODY_MOON, 2460416.291666666511, 89.952673424033108, 0.500413002240195, 89.896332647082019, 0.503260123339875, -10.048877989411316, 0.923829244641875},
        {"Moon full", taiyin::TAIYIN_BODY_MOON, 2460424.458333333489, 1.689946471592007, 0.999782525091863, 178.305528599979482, 0.497921253280936, -12.571892878552536, 0.913937269724489},
        {"Mercury", taiyin::TAIYIN_BODY_MERCURY, 2460409.250000000000, 164.657514093546013, 0.017819256786599, 6.116141904354143, 0.003079340885665, 4.793237806890438, NAN},
        {"Mercury elongation", taiyin::TAIYIN_BODY_MERCURY, 2460558.500000000000, 96.043910676655841, 0.447354689980927, 18.052751640813902, 0.002018780752677, -0.210681821560262, NAN},
        {"Venus", taiyin::TAIYIN_BODY_VENUS, 2460409.250000000000, 20.920335020532050, 0.967038902255189, 15.031958281489359, 0.002815008223313, -3.876241387723340, NAN},
        {"Venus inferior", taiyin::TAIYIN_BODY_VENUS, 2460757.500000000000, 168.316049790068178, 0.010360211339916, 8.415061818239845, 0.016519414968197, -4.219544864402351, NAN},
        {"Venus superior", taiyin::TAIYIN_BODY_VENUS, 2461046.500000000000, 0.954117924625311, 0.999930675169327, 0.706322633366453, 0.002709550894599, -3.908780372820892, NAN},
        {"Jupiter", taiyin::TAIYIN_BODY_JUPITER, 2460409.250000000000, 5.684839696196555, 0.997540907428766, 29.666196900579639, 0.009151534010785, -2.043341696741464, NAN},
        {"Jupiter opposition", taiyin::TAIYIN_BODY_JUPITER, 2460651.500000000000, 0.239971815328670, 0.999995614545770, 178.789948901753718, 0.013095181603900, -2.809808748864398, NAN},
        {"Saturn", taiyin::TAIYIN_BODY_SATURN, 2460409.250000000000, 3.386167746030388, 0.999127058240912, 34.967939475840687, 0.004242762854635, 1.063586835429949, NAN},
        {"Saturn ring plane", taiyin::TAIYIN_BODY_SATURN, 2460757.500000000000, 0.967754581680960, 0.999928679415939, 9.405346573354775, 0.004212948773439, 1.136625009462885, NAN},
        {"Saturn ring open", taiyin::TAIYIN_BODY_SATURN, 2463353.500000000000, 4.080340159666968, 0.998732628527487, 39.539975806809004, 0.004563869289417, 0.093955763778696, NAN},
        {"Uranus", taiyin::TAIYIN_BODY_URANUS, 2460409.250000000000, 1.545946644012275, 0.999818005862944, 31.785216225417727, 0.000950368985861, 5.864036588050928, NAN},
        {"Uranus faint", taiyin::TAIYIN_BODY_URANUS, 2454533.500000000000, 0.056819870998857, 0.999999754135857, 1.084167391768102, 0.000921208390367, 5.976225835399326, NAN},
        {"Neptune", taiyin::TAIYIN_BODY_NEPTUNE, 2460409.250000000000, 0.690655568471832, 0.999963674423075, 21.230138668386400, 0.000611703106599, 7.823496028613119, NAN},
        {"Neptune 1990", taiyin::TAIYIN_BODY_NEPTUNE, 2448008.500000000000, 1.774103902874245, 0.999760327532403, 111.934772129577101, 0.000632532941859, 7.826176113634231, NAN},
    };
    // Current Taiyin-vs-Swiss sanity diffs at this JD are small but not bit-identical:
    // across these multi-date cases, phase <= 37 arcsec, elongation <= 0.08 arcsec,
    // diameter <= 180 mas, planet magnitude <= 0.05 mag, Moon magnitude <= 0.09 mag,
    // and Moon horizontal parallax <= 0.8 mas.
    // Set TAIYIN_PRINT_SWISS_PHENO_DIFFS=1 to print the per-body diff table.
    const double phase_tolerance_rad = 0.012 * taiyin::TAIYIN_DEG_TO_RAD;
    const double illumination_tolerance = 1.0e-4;
    const double elongation_tolerance_rad = 2.5e-5 * taiyin::TAIYIN_DEG_TO_RAD;
    const double diameter_tolerance_rad = 6.0e-5 * taiyin::TAIYIN_DEG_TO_RAD;

    taiyin::runtime::NativeCalcContext context = make_context();
    taiyin::runtime::EphemerisEvalDiagnostic diagnostic;
    const bool print_diffs = print_swiss_pheno_diffs();
    if (print_diffs) {
        std::cout
            << "Swiss pheno sanity diffs for fixed multi-date oracles\n"
            << std::left << std::setw(22) << "Body"
            << ' ' << std::right << std::setw(14) << "dPhase(\")"
            << ' ' << std::setw(15) << "dIllum(ppm)"
            << ' ' << std::setw(14) << "dElong(\")"
            << ' ' << std::setw(15) << "dDiam(mas)"
            << ' ' << std::setw(13) << "dMag"
            << ' ' << std::setw(13) << "dHP(mas)"
            << "\n";
    }
    for (const OracleCase& c : cases) {
        taiyin::runtime::BodyPhenomena ph;
        expect_status(
            taiyin::runtime::calc_body_phenomena_ut(
                &context,
                c.body_id,
                split_jd(c.jd_ut),
                0u,
                &ph,
                &diagnostic),
            taiyin::TAIYIN_STATUS_OK,
            c.label,
            failures);
        expect_near(
            ph.phase_angle_rad,
            c.phase_angle_deg * taiyin::TAIYIN_DEG_TO_RAD,
            phase_tolerance_rad,
            c.label,
            failures);
        expect_near(
            ph.illuminated_fraction,
            c.illuminated_fraction,
            illumination_tolerance,
            c.label,
            failures);
        expect_near(
            ph.solar_elongation_rad,
            c.elongation_deg * taiyin::TAIYIN_DEG_TO_RAD,
            elongation_tolerance_rad,
            c.label,
            failures);
        expect_near(
            ph.apparent_diameter_rad,
            c.apparent_diameter_deg * taiyin::TAIYIN_DEG_TO_RAD,
            diameter_tolerance_rad,
            c.label,
            failures);
        expect_near(
            ph.apparent_magnitude,
            c.apparent_magnitude,
            magnitude_tolerance_for_body(c.body_id),
            c.label,
            failures);
        if (print_diffs) {
            const double phase_diff_arcsec =
                (ph.phase_angle_rad - c.phase_angle_deg * taiyin::TAIYIN_DEG_TO_RAD)
                * taiyin::TAIYIN_RAD_TO_DEG * 3600.0;
            const double illum_diff_ppm =
                (ph.illuminated_fraction - c.illuminated_fraction) * 1.0e6;
            const double elong_diff_arcsec =
                (ph.solar_elongation_rad - c.elongation_deg * taiyin::TAIYIN_DEG_TO_RAD)
                * taiyin::TAIYIN_RAD_TO_DEG * 3600.0;
            const double diameter_diff_mas =
                (ph.apparent_diameter_rad - c.apparent_diameter_deg * taiyin::TAIYIN_DEG_TO_RAD)
                * taiyin::TAIYIN_RAD_TO_DEG * 3600000.0;
            const double magnitude_diff = ph.apparent_magnitude - c.apparent_magnitude;
            const double parallax_diff_mas = std::isfinite(c.horizontal_parallax_deg)
                ? (ph.horizontal_parallax_rad - c.horizontal_parallax_deg * taiyin::TAIYIN_DEG_TO_RAD)
                    * taiyin::TAIYIN_RAD_TO_DEG * 3600000.0
                : NAN;
            std::cout << std::left << std::setw(22) << c.label
                      << ' ' << std::right << std::setw(14) << std::setprecision(6) << phase_diff_arcsec
                      << ' ' << std::setw(15) << illum_diff_ppm
                      << ' ' << std::setw(14) << elong_diff_arcsec
                      << ' ' << std::setw(15) << diameter_diff_mas
                      << ' ' << std::setw(13) << magnitude_diff
                      << ' ' << std::setw(13) << parallax_diff_mas
                      << "\n";
        }
        if (std::isfinite(c.horizontal_parallax_deg)) {
            expect_near(
                ph.horizontal_parallax_rad,
                c.horizontal_parallax_deg * taiyin::TAIYIN_DEG_TO_RAD,
                1.0e-6 * taiyin::TAIYIN_DEG_TO_RAD,
                c.label,
                failures);
        } else {
            expect_true(std::isnan(ph.horizontal_parallax_rad), c.label, failures);
        }
    }

    taiyin::runtime::BodyPhenomena sun;
    expect_status(
        taiyin::runtime::calc_body_phenomena_ut(
            &context,
            taiyin::TAIYIN_BODY_SUN,
            split_jd(jd),
            0u,
            &sun,
            &diagnostic),
        taiyin::TAIYIN_STATUS_OK,
        "Sun SwissEph diameter oracle",
        failures);
    expect_near(
        sun.apparent_diameter_rad,
        0.532335603543526 * taiyin::TAIYIN_DEG_TO_RAD,
        2.0e-6 * taiyin::TAIYIN_DEG_TO_RAD,
        "Sun apparent diameter SwissEph",
        failures);
    if (print_diffs) {
        const double sun_diameter_diff_mas =
            (sun.apparent_diameter_rad - 0.532335603543526 * taiyin::TAIYIN_DEG_TO_RAD)
            * taiyin::TAIYIN_RAD_TO_DEG * 3600000.0;
        const double sun_magnitude_diff = sun.apparent_magnitude - -26.856737455602502;
        std::cout << std::left << std::setw(22) << "Sun"
                  << ' ' << std::right << std::setw(14) << std::setprecision(6) << 0.0
                  << ' ' << std::setw(15) << NAN
                  << ' ' << std::setw(14) << 0.0
                  << ' ' << std::setw(15) << sun_diameter_diff_mas
                  << ' ' << std::setw(13) << sun_magnitude_diff
                  << ' ' << std::setw(13) << NAN
                  << "\n";
    }
    expect_near(
        sun.apparent_magnitude,
        -26.856737455602502,
        0.13,
        "Sun apparent magnitude SwissEph",
        failures);
}

void test_invalid_inputs(int* failures) {
    taiyin::runtime::NativeCalcContext context = make_context();
    taiyin::runtime::BodyPhenomena ph;
    taiyin::runtime::EphemerisEvalDiagnostic diagnostic;
    const double jd = jd_ut(2024, 4, 8, 18.0);
    expect_status(
        taiyin::runtime::calc_body_phenomena_ut(
            nullptr,
            taiyin::TAIYIN_BODY_MOON,
            split_jd(jd),
            0u,
            &ph,
            &diagnostic),
        taiyin::TAIYIN_ERROR_INVALID_ARGUMENT,
        "phenomena rejects null context",
        failures);
    expect_status(
        taiyin::runtime::calc_body_phenomena_ut(
            &context,
            taiyin::TAIYIN_BODY_MOON,
            split_jd(jd),
            0u,
            nullptr,
            &diagnostic),
        taiyin::TAIYIN_ERROR_INVALID_ARGUMENT,
        "phenomena rejects null output",
        failures);
    expect_status(
        taiyin::runtime::calc_body_phenomena_ut(
            &context,
            taiyin::TAIYIN_BODY_MOON,
            split_jd(NAN),
            0u,
            &ph,
            &diagnostic),
        taiyin::TAIYIN_ERROR_INVALID_ARGUMENT,
        "phenomena rejects non-finite jd",
        failures);
    expect_status(
        taiyin::runtime::calc_body_phenomena_ut(
            &context,
            taiyin::TAIYIN_BODY_JUPITER_BARYCENTER,
            split_jd(jd),
            0u,
            &ph,
            &diagnostic),
        taiyin::TAIYIN_ERROR_UNSUPPORTED,
        "barycenter phenomena unsupported",
        failures);
    expect_status(
        taiyin::runtime::calc_body_phenomena_ut(
            &context,
            taiyin::TAIYIN_BODY_MOON,
            split_jd(jd),
            1ull << 40,
            &ph,
            &diagnostic),
        taiyin::TAIYIN_ERROR_UNSUPPORTED,
        "phenomena rejects reserved high flags",
        failures);
}

}  // namespace

int main() {
    int failures = 0;
    test_formula_sanity(&failures);
    if (initialize_runtime(&failures)) {
        test_geometric_identities(&failures);
        test_moon_horizontal_parallax_stays_geocentric(&failures);
        test_barycenter_approximation_flag(&failures);
        test_native_barycenter_approximation_flag(&failures);
        test_documented_example_smoke(&failures);
        test_swiss_pheno_geometry_oracles(&failures);
        test_invalid_inputs(&failures);
    }
    if (failures != 0) {
        std::cerr << failures << " phenomena checks failed\n";
        return 1;
    }
    return 0;
}
