#include "taiyin/angle.h"
#include "taiyin/body_id.h"
#include "taiyin/dispatch.h"
#include "taiyin/internal/eop.h"
#include "taiyin/runtime/major_body_apparent.h"
#include "taiyin/runtime/native_context.h"
#include "taiyin/runtime/native_position.h"
#include "taiyin/runtime/observed_position.h"
#include "taiyin/runtime/runtime.h"
#include "taiyin/status.h"
#include "taiyin/time.h"

#include <cmath>
#include <cstdlib>
#include <atomic>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

namespace {

const taiyin::SplitJulianDate JD_UT(2460310, 0.5);
const taiyin::CalendarDateTime UTC_SAMPLE = { 2024, 4, 8, 18, 17, 20.0 };
const int TEST_TDB_MODEL_ID = 19001;
const int TEST_PRECESSION_MODEL_ID = 19002;

double test_tdb_zero(
    const taiyin::SplitJulianDate&,
    const void*
) {
    return 0.0;
}

double test_tdb_two_minutes(
    const taiyin::SplitJulianDate&,
    const void*
) {
    return 120.0;
}

bool test_identity_precession(
    const taiyin::SplitJulianDate&,
    const void*,
    taiyin::Matrix3x3* out,
    double* out_mean_obliquity_rad
) {
    if (!out) return false;
    *out = taiyin::matrix3x3_identity();
    if (out_mean_obliquity_rad) {
        *out_mean_obliquity_rad = 23.4 * taiyin::TAIYIN_DEG_TO_RAD;
    }
    return true;
}

bool test_rotated_precession(
    const taiyin::SplitJulianDate&,
    const void*,
    taiyin::Matrix3x3* out,
    double* out_mean_obliquity_rad
) {
    if (!out) return false;
    *out = taiyin::rotation_z_matrix(0.125);
    if (out_mean_obliquity_rad) {
        *out_mean_obliquity_rad = 23.4 * taiyin::TAIYIN_DEG_TO_RAD;
    }
    return true;
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

double spherical_angle_delta(const taiyin::runtime::ObservedPosition& lhs, const taiyin::runtime::ObservedPosition& rhs) {
    const double lon_delta = taiyin::angular_difference_radians(lhs.apparent.longitude_rad, rhs.apparent.longitude_rad);
    const double lat_delta = lhs.apparent.latitude_rad - rhs.apparent.latitude_rad;
    return std::sqrt(lon_delta * lon_delta + lat_delta * lat_delta);
}

double spherical_array_angle_delta(const double lhs[6], const double rhs[6]) {
    const double lon_delta = taiyin::angular_difference_radians(lhs[0], rhs[0]);
    const double lat_delta = lhs[1] - rhs[1];
    return std::sqrt(lon_delta * lon_delta + lat_delta * lat_delta);
}

double horizontal_angle_delta(const taiyin::runtime::ObservedPosition& lhs, const taiyin::runtime::ObservedPosition& rhs) {
    const double az_delta = taiyin::angular_difference_radians(lhs.horizontal.azimuth_rad, rhs.horizontal.azimuth_rad);
    const double alt_delta = lhs.horizontal.altitude_rad - rhs.horizontal.altitude_rad;
    return std::sqrt(az_delta * az_delta + alt_delta * alt_delta);
}

std::string repo_opm2_major_body_root() {
    const char* root = std::getenv("TAIYIN_REPO_ROOT");
    if (root && root[0] != '\0') {
        return std::string(root) + "/data/ephemerides/opm2/major-bodies/600y";
    }
    return "../data/ephemerides/opm2/major-bodies/600y";
}

bool initialize_packaged_runtime(int* failures) {
    taiyin::runtime::EphemerisRuntimeConfig config;
    const std::string data_root = repo_opm2_major_body_root();
    const char* source_paths[] = { data_root.c_str() };
    config.source_paths = source_paths;
    config.source_path_count = 1;
    config.load_packaged_data = false;
    config.segment_cache_max_entries = 256;
    const bool ok = taiyin::runtime::initialize_global_ephemeris_runtime(config);
    expect_true(ok, "initialize OPM2 runtime", failures);
    expect_true(taiyin::runtime::global_ephemeris_catalog_size() > 0, "OPM2 runtime catalog not empty", failures);
    return ok;
}

taiyin::internal::EarthOrientationSample make_eop_sample(
    double jd_utc,
    double dut1_seconds,
    double xp_arcsec,
    double yp_arcsec,
    double lod_seconds,
    double dx_mas,
    double dy_mas
) {
    taiyin::internal::EarthOrientationSample sample = {};
    sample.jd_utc = jd_utc;
    sample.dut1_seconds = dut1_seconds;
    sample.xp_rad = xp_arcsec * taiyin::TAIYIN_DEG_TO_RAD / 3600.0;
    sample.yp_rad = yp_arcsec * taiyin::TAIYIN_DEG_TO_RAD / 3600.0;
    sample.sp_rad = taiyin::internal::sp_rad_for_jd(jd_utc);
    sample.lod_seconds = lod_seconds;
    sample.dx_rad = dx_mas * taiyin::TAIYIN_DEG_TO_RAD / 3600000.0;
    sample.dy_rad = dy_mas * taiyin::TAIYIN_DEG_TO_RAD / 3600000.0;
    return sample;
}

void make_eop_table_around(
    double jd_utc,
    double dut1_seconds,
    double xp_arcsec,
    double yp_arcsec,
    double lod_seconds,
    double dx_mas,
    double dy_mas,
    taiyin::internal::EarthOrientationSample samples[3],
    taiyin::internal::EarthOrientationTable* table
) {
    samples[0] = make_eop_sample(jd_utc - 1.0, dut1_seconds - 0.02, xp_arcsec - 0.01, yp_arcsec + 0.01, lod_seconds, dx_mas - 0.2, dy_mas + 0.2);
    samples[1] = make_eop_sample(jd_utc, dut1_seconds, xp_arcsec, yp_arcsec, lod_seconds, dx_mas, dy_mas);
    samples[2] = make_eop_sample(jd_utc + 1.0, dut1_seconds + 0.02, xp_arcsec + 0.01, yp_arcsec - 0.01, lod_seconds, dx_mas + 0.2, dy_mas - 0.2);
    table->samples = samples;
    table->count = 3;
}

taiyin::runtime::NativeCalcContext make_geocentric_context() {
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

taiyin::runtime::NativeCalcContext make_observed_utc_context() {
    taiyin::runtime::NativeCalcContext context = make_geocentric_context();
    taiyin::runtime::native_context_set_observer_location(
        &context,
        taiyin::runtime::native_observer_location_degrees(116.4074, 39.9042, 43.5));
    return context;
}

void expect_position_vector(const double values[6], bool expect_speed, const char* label, int* failures) {
    for (int i = 0; i < (expect_speed ? 6 : 3); ++i) {
        if (!std::isfinite(values[i])) {
            std::cerr << "FAIL: finite " << label << "[" << i << "]\n";
            ++(*failures);
        }
    }
    expect_true(values[2] > 0.0, "spherical distance is positive", failures);
}

void expect_state_vector(const taiyin::CartesianState& state, const char* label, int* failures) {
    const double values[] = {
        state.position_au.x,
        state.position_au.y,
        state.position_au.z,
        state.velocity_au_per_day.x,
        state.velocity_au_per_day.y,
        state.velocity_au_per_day.z,
        state.acceleration_au_per_day2.x,
        state.acceleration_au_per_day2.y,
        state.acceleration_au_per_day2.z,
    };
    for (size_t i = 0; i < sizeof(values) / sizeof(values[0]); ++i) {
        if (!std::isfinite(values[i])) {
            std::cerr << "FAIL: finite " << label << " state[" << i << "]\n";
            ++(*failures);
        }
    }
    const double acceleration_norm =
        std::sqrt(state.acceleration_au_per_day2.x * state.acceleration_au_per_day2.x
            + state.acceleration_au_per_day2.y * state.acceleration_au_per_day2.y
            + state.acceleration_au_per_day2.z * state.acceleration_au_per_day2.z);
    expect_true(acceleration_norm > 0.0, "state acceleration is nonzero", failures);
}

void test_default_apparent_jupiter_matches_swiss(int* failures) {
    using namespace taiyin;
    using namespace taiyin::runtime;

    // 2003-08-28 03:00 China Standard Time = 2003-08-27 19:00 UT.
    // Swiss Ephemeris 2.10.03, SEFLG_SWIEPH | SEFLG_SPEED:
    // longitude 150.0868595396218 deg, latitude 0.8261369468319945 deg.
    const SplitJulianDate jd_ut(2452879, 0.2916666666666665);
    const double swiss_longitude_rad = 150.0868595396218 * TAIYIN_DEG_TO_RAD;
    const double swiss_latitude_rad = 0.8261369468319945 * TAIYIN_DEG_TO_RAD;
    const double tolerance_rad = 0.2 / 3600.0 * TAIYIN_DEG_TO_RAD;

    NativeCalcContext context;
    expect_true(
        (context.apparent_options.flags & TAIYIN_APPARENT_ABERRATION) != 0u,
        "default context enables annual aberration",
        failures);
    expect_true(
        (context.apparent_options.flags & TAIYIN_APPARENT_DEFLECTION) != 0u,
        "default context enables solar deflection",
        failures);
    expect_true(
        context.apparent_options.deflector_count == 1
            && context.apparent_options.solar_deflector_index == 0
            && context.apparent_options.deflectors
            && context.apparent_options.deflectors[0].body_id == TAIYIN_BODY_SUN,
        "default context contains only the solar deflector",
        failures);

    double apparent[6] = {};
    double no_aberration[6] = {};
    double no_deflection[6] = {};
    EphemerisEvalDiagnostic diagnostic;
    expect_status(
        calc_position_ut(
            &context, TAIYIN_BODY_JUPITER_BARYCENTER, jd_ut,
            TAIYIN_NATIVE_POSITION_RADIANS | TAIYIN_NATIVE_POSITION_SPEED,
            apparent,
            &diagnostic),
        TAIYIN_STATUS_OK,
        "default apparent Jupiter position and speed with solar deflection",
        failures);
    expect_position_vector(
        apparent,
        true,
        "default apparent Jupiter with solar deflection",
        failures);
    expect_near(
        std::fabs(angular_difference_radians(apparent[0], swiss_longitude_rad)),
        0.0,
        tolerance_rad,
        "default Jupiter longitude matches Swiss apparent position",
        failures);
    expect_near(
        apparent[1],
        swiss_latitude_rad,
        tolerance_rad,
        "default Jupiter latitude matches Swiss apparent position",
        failures);

    expect_status(
        calc_position_ut(
            &context, TAIYIN_BODY_JUPITER_BARYCENTER, jd_ut,
            TAIYIN_NATIVE_POSITION_RADIANS | TAIYIN_NATIVE_POSITION_NO_ABERR,
            no_aberration, &diagnostic),
        TAIYIN_STATUS_OK,
        "Jupiter position without annual aberration",
        failures);
    expect_status(
        calc_position_ut(
            &context, TAIYIN_BODY_JUPITER_BARYCENTER, jd_ut,
            TAIYIN_NATIVE_POSITION_RADIANS | TAIYIN_NATIVE_POSITION_NO_GDEFL,
            no_deflection, &diagnostic),
        TAIYIN_STATUS_OK,
        "Jupiter position without solar deflection",
        failures);
    expect_true(
        std::fabs(angular_difference_radians(apparent[0], no_aberration[0]))
            > 15.0 / 3600.0 * TAIYIN_DEG_TO_RAD,
        "NO_ABERR removes the default annual aberration",
        failures);
    expect_true(
        std::fabs(angular_difference_radians(apparent[0], no_deflection[0]))
            > 0.01 / 3600.0 * TAIYIN_DEG_TO_RAD,
        "NO_GDEFL removes the default solar deflection",
        failures);
}

void test_solar_target_shapiro_skips_self_deflector(int* failures) {
    using namespace taiyin;
    using namespace taiyin::runtime;

    NativeCalcContext context = make_geocentric_context();
    expect_status(
        native_context_enable_shapiro_delay(&context, 0),
        TAIYIN_STATUS_OK,
        "enable Shapiro delay for solar target",
        failures);

    double solar_position[6] = {};
    CartesianState solar_state;
    EphemerisEvalDiagnostic diagnostic;
    expect_status(
        calc_position_ut(
            &context,
            TAIYIN_BODY_SUN,
            JD_UT,
            TAIYIN_NATIVE_POSITION_RADIANS | TAIYIN_NATIVE_POSITION_SPEED,
            solar_position,
            &diagnostic),
        TAIYIN_STATUS_OK,
        "solar apparent position with Shapiro self-deflector exclusion",
        failures);
    expect_position_vector(
        solar_position,
        true,
        "solar apparent position with Shapiro",
        failures);
    expect_status(
        calc_state_ut(
            &context,
            TAIYIN_BODY_SUN,
            JD_UT,
            0,
            &solar_state,
            &diagnostic),
        TAIYIN_STATUS_OK,
        "solar apparent state with Shapiro self-deflector exclusion",
        failures);
    expect_state_vector(
        solar_state,
        "solar apparent state with Shapiro",
        failures);
}

void test_native_position_batch(int* failures) {
    using namespace taiyin;
    using namespace taiyin::runtime;

    NativeCalcContext context = make_geocentric_context();
    const uint32_t flags = TAIYIN_NATIVE_POSITION_SPEED | TAIYIN_NATIVE_POSITION_RADIANS;
    double mercury[6] = {};
    EphemerisEvalDiagnostic diagnostic;
    expect_status(
        calc_position_ut(&context, TAIYIN_BODY_MERCURY_BARYCENTER, JD_UT, flags, mercury, &diagnostic),
        TAIYIN_STATUS_OK,
        "calc Mercury native position",
        failures);
    expect_position_vector(mercury, true, "Mercury", failures);
    expect_true(mercury[0] >= 0.0 && mercury[0] < TAIYIN_TWO_PI, "Mercury longitude normalized", failures);

    double mercury_xyz[6] = {};
    CartesianState mercury_state;
    expect_status(
        calc_position_ut(
            &context,
            TAIYIN_BODY_MERCURY_BARYCENTER,
            JD_UT,
            flags | TAIYIN_NATIVE_POSITION_XYZ,
            mercury_xyz,
            &diagnostic),
        TAIYIN_STATUS_OK,
        "calc Mercury XYZ position",
        failures);
    expect_status(
        calc_state_ut(&context, TAIYIN_BODY_MERCURY_BARYCENTER, JD_UT, 0, &mercury_state, &diagnostic),
        TAIYIN_STATUS_OK,
        "calc Mercury native state",
        failures);
    expect_state_vector(mercury_state, "Mercury", failures);
    expect_near(mercury_state.position_au.x, mercury_xyz[0], 0.0, "state position x matches XYZ", failures);
    expect_near(mercury_state.position_au.y, mercury_xyz[1], 0.0, "state position y matches XYZ", failures);
    expect_near(mercury_state.position_au.z, mercury_xyz[2], 0.0, "state position z matches XYZ", failures);
    expect_near(mercury_state.velocity_au_per_day.x, mercury_xyz[3], 0.0, "state velocity x matches XYZ", failures);
    expect_near(mercury_state.velocity_au_per_day.y, mercury_xyz[4], 0.0, "state velocity y matches XYZ", failures);
    expect_near(mercury_state.velocity_au_per_day.z, mercury_xyz[5], 0.0, "state velocity z matches XYZ", failures);

    const int targets[] = {
        TAIYIN_BODY_SUN,
        TAIYIN_BODY_MOON,
        TAIYIN_BODY_MERCURY_BARYCENTER,
        TAIYIN_BODY_VENUS_BARYCENTER,
    };
    double batch[sizeof(targets) / sizeof(targets[0])][6] = {};
    EphemerisEvalDiagnostic diagnostics[sizeof(targets) / sizeof(targets[0])];
    expect_status(
        calc_positions_ut(
            &context,
            targets,
            sizeof(targets) / sizeof(targets[0]),
            JD_UT,
            flags,
            &batch[0][0],
            diagnostics),
        TAIYIN_STATUS_OK,
        "calc native position batch",
        failures);
    for (size_t i = 0; i < sizeof(targets) / sizeof(targets[0]); ++i) {
        expect_position_vector(batch[i], true, "batch body", failures);
        expect_status(diagnostics[i].status, TAIYIN_STATUS_OK, "native batch diagnostic", failures);
    }
}

void test_native_context_apparent_matrix_cache(int* failures) {
    using namespace taiyin;
    using namespace taiyin::runtime;

    NativeCalcContext context = make_geocentric_context();
    expect_true(!context.apparent_matrix_cache.valid, "matrix cache starts empty", failures);

    double position[6] = {};
    EphemerisEvalDiagnostic diagnostic;
    expect_status(
        calc_position_ut(
            &context,
            TAIYIN_BODY_MERCURY_BARYCENTER,
            JD_UT,
            TAIYIN_NATIVE_POSITION_RADIANS,
            position,
            &diagnostic),
        TAIYIN_STATUS_OK,
        "populate apparent matrix cache",
        failures);
    expect_true(context.apparent_matrix_cache.valid, "matrix cache populated", failures);
    expect_true(context.time_scale_cache.ut1_valid, "UT1 time-scale cache populated", failures);
    expect_true(context.time_scale_cache.tt_valid, "TT-to-TDB cache populated", failures);
    expect_true(context.ephemeris_state_cache.valid, "epoch state cache populated", failures);
    expect_true(
        context.ephemeris_state_cache.component_entry_count > 0u,
        "epoch state cache stores source-aware receive-epoch states",
        failures);
    expect_true(
        context.apparent_matrix_cache.derivative_flags == 0u,
        "position matrix cache needs no derivatives",
        failures);
    expect_true(
        context.apparent_matrix_cache.output_frame_id
            == TAIYIN_APPARENT_FRAME_TRUE_ECLIPTIC_OF_DATE,
        "matrix cache records output frame",
        failures);
    const SplitJulianDate first_jd_tt = context.apparent_matrix_cache.jd_tt;
    const uint64_t first_state_cache_hits = context.ephemeris_state_cache.hit_count;

    expect_status(
        calc_position_ut(
            &context,
            TAIYIN_BODY_VENUS_BARYCENTER,
            JD_UT,
            TAIYIN_NATIVE_POSITION_RADIANS,
            position,
            &diagnostic),
        TAIYIN_STATUS_OK,
        "reuse apparent matrix cache for another target",
        failures);
    expect_true(
        context.apparent_matrix_cache.jd_tt == first_jd_tt,
        "same epoch keeps matrix cache key",
        failures);
    expect_true(
        context.ephemeris_state_cache.hit_count > first_state_cache_hits,
        "same epoch reuses observer or solar-deflector states",
        failures);
    expect_true(
        context.time_scale_cache.jd_ut1 == JD_UT,
        "same UT1 input keeps cached time-scale key",
        failures);
    NativeCalcContext fresh_context = make_geocentric_context();
    double fresh_position[6] = {};
    expect_status(
        calc_position_ut(
            &fresh_context,
            TAIYIN_BODY_VENUS_BARYCENTER,
            JD_UT,
            TAIYIN_NATIVE_POSITION_RADIANS,
            fresh_position,
            &diagnostic),
        TAIYIN_STATUS_OK,
        "fresh-context reference for cached position",
        failures);
    for (int i = 0; i < 3; ++i) {
        expect_near(
            position[i],
            fresh_position[i],
            1.0e-15,
            "cached and uncached positions agree",
            failures);
    }

    for (size_t i = 0; i < TAIYIN_NATIVE_EPHEMERIS_STATE_CACHE_CAPACITY; ++i) {
        if (context.ephemeris_state_cache.component_entries[i].valid) {
            context.ephemeris_state_cache.component_entries[i]
                .source_key.source_id ^= UINT64_C(0x8000000000000000);
        }
    }
    const uint64_t misses_before_source_change =
        context.ephemeris_state_cache.miss_count;
    expect_status(
        calc_position_ut(
            &context,
            TAIYIN_BODY_VENUS_BARYCENTER,
            JD_UT,
            TAIYIN_NATIVE_POSITION_RADIANS,
            position,
            &diagnostic),
        TAIYIN_STATUS_OK,
        "source-key mismatch refreshes same-epoch raw states",
        failures);
    expect_true(
        context.ephemeris_state_cache.miss_count
            > misses_before_source_change,
        "same JD does not reuse a different source key",
        failures);

    expect_status(
        calc_position_ut(
            &context,
            TAIYIN_BODY_VENUS_BARYCENTER,
            JD_UT,
            TAIYIN_NATIVE_POSITION_RADIANS | TAIYIN_NATIVE_POSITION_SPEED,
            position,
            &diagnostic),
        TAIYIN_STATUS_OK,
        "refresh matrix cache for velocity derivatives",
        failures);
    expect_true(
        context.apparent_matrix_cache.derivative_flags == TAIYIN_APPARENT_VELOCITY,
        "speed request updates matrix derivative key",
        failures);

    expect_status(
        calc_position_ut(
            &context,
            TAIYIN_BODY_VENUS_BARYCENTER,
            JD_UT,
            TAIYIN_NATIVE_POSITION_RADIANS | TAIYIN_NATIVE_POSITION_NONUT,
            position,
            &diagnostic),
        TAIYIN_STATUS_OK,
        "refresh matrix cache for mean ecliptic frame",
        failures);
    expect_true(
        context.apparent_matrix_cache.output_frame_id
            == TAIYIN_APPARENT_FRAME_MEAN_ECLIPTIC_OF_DATE,
        "NONUT updates matrix frame key",
        failures);

    const SplitJulianDate next_jd(JD_UT.day_number, JD_UT.day_fraction + 0.25);
    expect_status(
        calc_position_ut(
            &context,
            TAIYIN_BODY_VENUS_BARYCENTER,
            next_jd,
            TAIYIN_NATIVE_POSITION_RADIANS,
            position,
            &diagnostic),
        TAIYIN_STATUS_OK,
        "refresh matrix cache for a new epoch",
        failures);
    expect_true(
        context.apparent_matrix_cache.jd_tt != first_jd_tt,
        "new epoch replaces matrix cache key",
        failures);
    expect_true(
        context.ephemeris_state_cache.jd_tdb
            == context.time_scale_cache.ut1_jd_tdb,
        "new epoch replaces state-cache key",
        failures);
}

void test_context_epoch_cache_invalidated_by_runtime_clear(int* failures) {
    using namespace taiyin;
    using namespace taiyin::runtime;

    NativeCalcContext context = make_geocentric_context();
    double position[6] = {};
    EphemerisEvalDiagnostic diagnostic;
    expect_status(
        calc_position_ut(
            &context,
            TAIYIN_BODY_MERCURY_BARYCENTER,
            JD_UT,
            TAIYIN_NATIVE_POSITION_RADIANS,
            position,
            &diagnostic),
        TAIYIN_STATUS_OK,
        "populate epoch cache before global clear",
        failures);
    const uint64_t generation_before_clear =
        context.ephemeris_state_cache.runtime_generation;

    clear_global_ephemeris_cache();
    expect_true(
        global_ephemeris_cache_entry_count() == 0u,
        "global clear empties segment cache",
        failures);
    expect_status(
        calc_position_ut(
            &context,
            TAIYIN_BODY_MERCURY_BARYCENTER,
            JD_UT,
            TAIYIN_NATIVE_POSITION_RADIANS,
            position,
            &diagnostic),
        TAIYIN_STATUS_OK,
        "same-context evaluation after global clear",
        failures);
    expect_true(
        context.ephemeris_state_cache.runtime_generation
            != generation_before_clear,
        "global clear invalidates the context epoch generation",
        failures);
    expect_true(
        global_ephemeris_cache_entry_count() > 0u,
        "post-clear context evaluation remaps a segment",
        failures);
}

void test_context_caches_invalidated_by_dispatch_replacement(int* failures) {
    using namespace taiyin;
    using namespace taiyin::runtime;

    NativeCalcContext time_context = make_geocentric_context();
    time_context.model_context.tdb_model_id = TEST_TDB_MODEL_ID;
    dispatch::register_tdb_model(TEST_TDB_MODEL_ID, test_tdb_zero);
    double position[6] = {};
    EphemerisEvalDiagnostic diagnostic;
    expect_status(
        calc_position_tt(
            &time_context,
            TAIYIN_BODY_MERCURY_BARYCENTER,
            JD_UT,
            TAIYIN_NATIVE_POSITION_RADIANS,
            position,
            &diagnostic),
        TAIYIN_STATUS_OK,
        "populate time cache with first TDB callback",
        failures);
    const SplitJulianDate first_jd_tdb = time_context.time_scale_cache.jd_tdb;
    const uint64_t first_time_generation =
        time_context.time_scale_cache.tt_dispatch_generation;

    dispatch::register_tdb_model(TEST_TDB_MODEL_ID, test_tdb_two_minutes);
    expect_status(
        calc_position_tt(
            &time_context,
            TAIYIN_BODY_MERCURY_BARYCENTER,
            JD_UT,
            TAIYIN_NATIVE_POSITION_RADIANS,
            position,
            &diagnostic),
        TAIYIN_STATUS_OK,
        "replace TDB callback under cached model ID",
        failures);
    expect_true(
        time_context.time_scale_cache.tt_dispatch_generation
            != first_time_generation,
        "TDB replacement invalidates time cache generation",
        failures);
    expect_near(
        days_between_split_jd(
            first_jd_tdb, time_context.time_scale_cache.jd_tdb),
        120.0 / 86400.0,
        1.0e-15,
        "TDB replacement refreshes cached scale",
        failures);

    NativeCalcContext matrix_context = make_geocentric_context();
    matrix_context.model_context.precession_model_id =
        TEST_PRECESSION_MODEL_ID;
    dispatch::register_precession_model(
        TEST_PRECESSION_MODEL_ID, test_identity_precession);
    double first_position[6] = {};
    double second_position[6] = {};
    expect_status(
        calc_position_tt(
            &matrix_context,
            TAIYIN_BODY_MERCURY_BARYCENTER,
            JD_UT,
            TAIYIN_NATIVE_POSITION_RADIANS,
            first_position,
            &diagnostic),
        TAIYIN_STATUS_OK,
        "populate matrix cache with first precession callback",
        failures);
    const uint64_t first_matrix_generation =
        matrix_context.apparent_matrix_cache.dispatch_generation;

    dispatch::register_precession_model(
        TEST_PRECESSION_MODEL_ID, test_rotated_precession);
    expect_status(
        calc_position_tt(
            &matrix_context,
            TAIYIN_BODY_MERCURY_BARYCENTER,
            JD_UT,
            TAIYIN_NATIVE_POSITION_RADIANS,
            second_position,
            &diagnostic),
        TAIYIN_STATUS_OK,
        "replace precession callback under cached model ID",
        failures);
    expect_true(
        matrix_context.apparent_matrix_cache.dispatch_generation
            != first_matrix_generation,
        "precession replacement invalidates matrix cache generation",
        failures);
    expect_true(
        spherical_array_angle_delta(first_position, second_position) > 0.01,
        "precession replacement recomputes apparent matrix",
        failures);
}

void test_shared_native_context_concurrent_reads(int* failures) {
    using namespace taiyin;
    using namespace taiyin::runtime;

    struct ConcurrentCase {
        int target_id;
        SplitJulianDate jd_ut;
        uint32_t flags;
        double expected[6];
    };
    ConcurrentCase cases[] = {
        { TAIYIN_BODY_MERCURY_BARYCENTER, JD_UT,
          TAIYIN_NATIVE_POSITION_RADIANS, {} },
        { TAIYIN_BODY_VENUS_BARYCENTER,
          SplitJulianDate(JD_UT.day_number, JD_UT.day_fraction + 0.125),
          TAIYIN_NATIVE_POSITION_RADIANS | TAIYIN_NATIVE_POSITION_SPEED, {} },
        { TAIYIN_BODY_MARS_BARYCENTER,
          SplitJulianDate(JD_UT.day_number, JD_UT.day_fraction + 0.25),
          TAIYIN_NATIVE_POSITION_RADIANS, {} },
        { TAIYIN_BODY_JUPITER_BARYCENTER,
          SplitJulianDate(JD_UT.day_number, JD_UT.day_fraction + 0.375),
          TAIYIN_NATIVE_POSITION_RADIANS | TAIYIN_NATIVE_POSITION_SPEED, {} },
    };

    EphemerisEvalDiagnostic diagnostic;
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        NativeCalcContext reference = make_geocentric_context();
        expect_status(
            calc_position_ut(
                &reference,
                cases[i].target_id,
                cases[i].jd_ut,
                cases[i].flags,
                cases[i].expected,
                &diagnostic),
            TAIYIN_STATUS_OK,
            "concurrent shared-context reference",
            failures);
    }

    NativeCalcContext shared = make_geocentric_context();
    std::atomic<int> concurrent_failures(0);
    std::vector<std::thread> workers;
    const int worker_count = 8;
    const int iterations_per_worker = 64;
    for (int worker = 0; worker < worker_count; ++worker) {
        workers.push_back(std::thread([&, worker]() {
            for (int iteration = 0; iteration < iterations_per_worker; ++iteration) {
                const size_t index = static_cast<size_t>(
                    (worker + iteration) %
                    static_cast<int>(sizeof(cases) / sizeof(cases[0])));
                double actual[6] = {};
                EphemerisEvalDiagnostic thread_diagnostic;
                const Status status = calc_position_ut(
                    &shared,
                    cases[index].target_id,
                    cases[index].jd_ut,
                    cases[index].flags,
                    actual,
                    &thread_diagnostic);
                const int value_count =
                    (cases[index].flags & TAIYIN_NATIVE_POSITION_SPEED) != 0u
                    ? 6 : 3;
                bool matches = status == TAIYIN_STATUS_OK;
                for (int component = 0; matches && component < value_count; ++component) {
                    matches = std::fabs(
                        actual[component] - cases[index].expected[component])
                        <= 1.0e-14;
                }
                if (!matches) {
                    concurrent_failures.fetch_add(1, std::memory_order_relaxed);
                }
            }
        }));
    }
    for (size_t i = 0; i < workers.size(); ++i) {
        workers[i].join();
    }
    expect_true(
        concurrent_failures.load(std::memory_order_relaxed) == 0,
        "immutable native context supports concurrent calculations",
        failures);
}

void test_major_body_apparent_batch(int* failures) {
    using namespace taiyin;
    using namespace taiyin::runtime;

    const int bodies[] = {
        major_body_id_for_mask_bit(TAIYIN_MAJOR_BODY_SUN),
        major_body_id_for_mask_bit(TAIYIN_MAJOR_BODY_MOON),
        major_body_id_for_mask_bit(TAIYIN_MAJOR_BODY_MERCURY),
    };

    ApparentOptions options = get_global_apparent_options();
    options.flags =
        TAIYIN_APPARENT_SPHERICAL
        | TAIYIN_APPARENT_LIGHT_TIME
        | TAIYIN_APPARENT_ABERRATION
        | TAIYIN_APPARENT_DEFLECTION;
    options.output_frame_id = TAIYIN_APPARENT_FRAME_TRUE_ECLIPTIC_OF_DATE;
    ApparentDeflector solar_deflector = *native_solar_deflector();
    options.deflectors = &solar_deflector;
    options.deflector_count = 1;
    options.solar_deflector_index = 0;

    MajorBodyApparentBatchRequest request;
    request.jd_tdb = JD_UT;
    request.jd_tt = JD_UT;
    request.observer_id = TAIYIN_BODY_EARTH;
    request.center_id = TAIYIN_BODY_EARTH;
    request.body_ids = bodies;
    request.body_count = sizeof(bodies) / sizeof(bodies[0]);
    request.options = &options;

    MajorBodyApparentBatchResult result;
    EphemerisEvalDiagnostic diagnostic;
    expect_status(
        eval_global_major_body_apparent_batch(request, &result, &diagnostic),
        TAIYIN_STATUS_OK,
        "eval global major body apparent batch",
        failures);
    expect_status(result.status, TAIYIN_STATUS_OK, "major body result status", failures);
    expect_true(result.body_count == request.body_count, "major body result count", failures);
    for (size_t i = 0; i < result.body_count; ++i) {
        expect_status(result.bodies[i].status, TAIYIN_STATUS_OK, "major body status", failures);
        expect_true(major_body_name_for_id(result.bodies[i].body_id)[0] != '\0', "major body name", failures);
        expect_true(std::isfinite(result.bodies[i].longitude_rad), "major body longitude", failures);
        expect_true(std::isfinite(result.bodies[i].latitude_rad), "major body latitude", failures);
        expect_true(result.bodies[i].distance_au > 0.0, "major body distance", failures);
    }
}

void test_observed_utc_requires_time_tables(int* failures) {
    using namespace taiyin;
    using namespace taiyin::runtime;

    const int body = TAIYIN_BODY_SUN;
    ObservedPosition observed;
    EphemerisEvalDiagnostic diagnostic;

    NativeCalcContext missing_tables = make_geocentric_context();
    expect_true(set_global_earth_orientation_table(nullptr), "clear global EOP", failures);
    expect_status(
        calc_observed_utc(
            &missing_tables,
            UTC_SAMPLE,
            &body,
            1,
            0,
            &observed,
            &diagnostic),
        TAIYIN_TIME_ERROR_EOP_OUT_OF_RANGE,
        "observed UTC requires global EOP",
        failures);
    expect_status(
        diagnostic.status,
        TAIYIN_TIME_ERROR_EOP_OUT_OF_RANGE,
        "missing table diagnostic",
        failures);
    expect_true(
        diagnostic.time_scale_fallback_reason == TimeScaleFallbackNullEopTable,
        "missing table fallback reason",
        failures);

    internal::EarthOrientationSample samples[3];
    internal::EarthOrientationTable eop_table;
    make_eop_table_around(julian_day(UTC_SAMPLE), -0.1, 0.08, 0.25, 0.001, 0.4, -0.2, samples, &eop_table);

    NativeCalcContext global_tables = make_geocentric_context();
    expect_true(set_global_earth_orientation_table(&eop_table), "set global EOP", failures);
    expect_status(
        calc_observed_utc(
            &global_tables,
            UTC_SAMPLE,
            &body,
            1,
            0,
            &observed,
            &diagnostic),
        TAIYIN_STATUS_OK,
        "observed UTC uses global leap seconds and EOP",
        failures);
}

void test_observed_utc_requires_eop_coverage(int* failures) {
    using namespace taiyin;
    using namespace taiyin::runtime;

    internal::EarthOrientationSample stale_samples[3];
    internal::EarthOrientationTable stale_eop;
    make_eop_table_around(julian_day(UTC_SAMPLE) - 30.0, -0.1, 0.08, 0.25, 0.001, 0.4, -0.2, stale_samples, &stale_eop);

    expect_true(set_global_earth_orientation_table(&stale_eop), "set stale global EOP", failures);
    NativeCalcContext context = make_observed_utc_context();
    const int body = TAIYIN_BODY_SUN;
    ObservedPosition observed;
    EphemerisEvalDiagnostic diagnostic;
    expect_status(
        calc_observed_utc(
            &context,
            UTC_SAMPLE,
            &body,
            1,
            0,
            &observed,
            &diagnostic),
        TAIYIN_TIME_ERROR_EOP_OUT_OF_RANGE,
        "observed UTC requires EOP coverage at UTC",
        failures);
    expect_status(
        diagnostic.status,
        TAIYIN_TIME_ERROR_EOP_OUT_OF_RANGE,
        "stale EOP diagnostic",
        failures);
    expect_true(
        diagnostic.time_scale_fallback_reason == TimeScaleFallbackEopOutOfRange,
        "stale EOP fallback reason",
        failures);
}

void test_utc_position_apis_propagate_time_failures(int* failures) {
    using namespace taiyin;
    using namespace taiyin::runtime;

    NativeCalcContext context = make_geocentric_context();
    expect_true(set_global_earth_orientation_table(nullptr), "clear global EOP", failures);

    const int body_ids[] = {TAIYIN_BODY_SUN, TAIYIN_BODY_MARS_BARYCENTER};
    double position[6];
    double positions[12];
    CartesianState state;
    EphemerisEvalDiagnostic diagnostic;
    EphemerisEvalDiagnostic diagnostics[2];

    expect_status(
        calc_position_utc(
            &context, body_ids[0], UTC_SAMPLE, 0, position, &diagnostic),
        TAIYIN_TIME_ERROR_EOP_OUT_OF_RANGE,
        "position UTC propagates missing EOP",
        failures);
    expect_true(
        diagnostic.time_scale_fallback_reason == TimeScaleFallbackNullEopTable,
        "position UTC missing EOP reason",
        failures);

    expect_status(
        calc_positions_utc(
            &context, body_ids, 2, UTC_SAMPLE, 0, positions, diagnostics),
        TAIYIN_TIME_ERROR_EOP_OUT_OF_RANGE,
        "position UTC batch propagates missing EOP",
        failures);
    expect_true(
        diagnostics[0].time_scale_fallback_reason == TimeScaleFallbackNullEopTable
            && diagnostics[1].time_scale_fallback_reason == TimeScaleFallbackNullEopTable,
        "position UTC batch missing EOP reasons",
        failures);

    expect_status(
        calc_state_utc(
            &context, body_ids[0], UTC_SAMPLE, 0, &state, &diagnostic),
        TAIYIN_TIME_ERROR_EOP_OUT_OF_RANGE,
        "state UTC propagates missing EOP",
        failures);
    expect_true(
        diagnostic.time_scale_fallback_reason == TimeScaleFallbackNullEopTable,
        "state UTC missing EOP reason",
        failures);

    expect_status(
        native_context_set_allow_utc_out_of_range_estimate(&context, true),
        TAIYIN_STATUS_OK,
        "enable UTC out-of-range estimate",
        failures);
    expect_status(
        calc_position_utc(
            &context, body_ids[0], UTC_SAMPLE, 0, position, &diagnostic),
        TAIYIN_STATUS_OK,
        "position UTC estimates when EOP is missing",
        failures);
    expect_true(
        diagnostic.time_scale_route == TimeScaleRouteEstimatedDeltaT
            && diagnostic.time_scale_fallback_reason
                == TimeScaleFallbackNullEopTable
            && (diagnostic.time_scale_flags
                & TAIYIN_TIME_DIAGNOSTIC_USED_DELTA_T_MODEL) != 0,
        "position UTC estimated-route diagnostic",
        failures);
    expect_status(
        native_context_set_allow_utc_out_of_range_estimate(&context, false),
        TAIYIN_STATUS_OK,
        "disable UTC out-of-range estimate",
        failures);

    const CalendarDateTime before_leap_seconds = {1900, 1, 1, 0, 0, 0.0};
    internal::EarthOrientationSample samples[3];
    internal::EarthOrientationTable eop_table;
    make_eop_table_around(
        julian_day(before_leap_seconds),
        0.0,
        0.0,
        0.0,
        0.0,
        0.0,
        0.0,
        samples,
        &eop_table);
    expect_true(set_global_earth_orientation_table(&eop_table), "set historical EOP", failures);
    expect_status(
        calc_position_utc(
            &context,
            body_ids[0],
            before_leap_seconds,
            0,
            position,
            &diagnostic),
        TAIYIN_TIME_ERROR_LEAP_SECOND_UNAVAILABLE,
        "position UTC propagates missing leap seconds",
        failures);
    expect_true(
        diagnostic.time_scale_fallback_reason
            == TimeScaleFallbackLeapSecondUnavailable,
        "position UTC missing leap-second reason",
        failures);
}

void test_observed_utc_geocentric_matches_native_utc(int* failures) {
    using namespace taiyin;
    using namespace taiyin::runtime;

    internal::EarthOrientationSample samples[3];
    internal::EarthOrientationTable eop_table;
    make_eop_table_around(julian_day(UTC_SAMPLE), -0.101, 0.075, 0.240, 0.0012, 0.35, -0.15, samples, &eop_table);

    expect_true(set_global_earth_orientation_table(&eop_table), "set global EOP", failures);
    NativeCalcContext context = make_observed_utc_context();
    const int bodies[] = {
        TAIYIN_BODY_SUN,
        TAIYIN_BODY_MOON,
        TAIYIN_BODY_MERCURY_BARYCENTER,
    };
    ObservedPosition observed[sizeof(bodies) / sizeof(bodies[0])];
    EphemerisEvalDiagnostic observed_diagnostics[sizeof(bodies) / sizeof(bodies[0])];
    expect_status(
        calc_observed_utc(
            &context,
            UTC_SAMPLE,
            bodies,
            sizeof(bodies) / sizeof(bodies[0]),
            TAIYIN_OBSERVED_SPEED,
            observed,
            observed_diagnostics),
        TAIYIN_STATUS_OK,
        "observed UTC geocentric batch",
        failures);

    double native_position[6] = {};
    EphemerisEvalDiagnostic native_diagnostic;
    for (size_t i = 0; i < sizeof(bodies) / sizeof(bodies[0]); ++i) {
        expect_status(observed[i].status, TAIYIN_STATUS_OK, "observed UTC body status", failures);
        expect_status(observed_diagnostics[i].status, TAIYIN_STATUS_OK, "observed UTC diagnostic", failures);
        expect_status(
            calc_position_utc(
                &context,
                bodies[i],
                UTC_SAMPLE,
                TAIYIN_NATIVE_POSITION_EQUATORIAL
                    | TAIYIN_NATIVE_POSITION_SPEED
                    | TAIYIN_NATIVE_POSITION_RADIANS,
                native_position,
                &native_diagnostic),
            TAIYIN_STATUS_OK,
            "native UTC equatorial position",
            failures);
        expect_near(observed[i].apparent.longitude_rad, native_position[0], 1.0e-14, "observed/native UTC longitude", failures);
        expect_near(observed[i].apparent.latitude_rad, native_position[1], 1.0e-14, "observed/native UTC latitude", failures);
        expect_near(observed[i].apparent.distance_au, native_position[2], 1.0e-14, "observed/native UTC distance", failures);
    }
}

void test_observed_utc_topocentric_uses_eop(int* failures) {
    using namespace taiyin;
    using namespace taiyin::runtime;

    internal::EarthOrientationSample base_samples[3];
    internal::EarthOrientationTable base_eop;
    make_eop_table_around(julian_day(UTC_SAMPLE), -0.101, 0.075, 0.240, 0.0012, 0.35, -0.15, base_samples, &base_eop);

    internal::EarthOrientationSample shifted_samples[3];
    internal::EarthOrientationTable shifted_eop;
    make_eop_table_around(julian_day(UTC_SAMPLE), 0.550, 0.700, -0.500, 0.0040, 8.00, -7.00, shifted_samples, &shifted_eop);

    NativeCalcContext base_context = make_observed_utc_context();
    NativeCalcContext shifted_context = make_observed_utc_context();
    const int body = TAIYIN_BODY_MOON;
    ObservedPosition base_observed;
    ObservedPosition shifted_observed;
    EphemerisEvalDiagnostic diagnostic;
    const uint64_t flags = TAIYIN_OBSERVED_TOPOCENTRIC
        | TAIYIN_OBSERVED_HORIZONTAL
        | TAIYIN_OBSERVED_SPEED;

    expect_true(set_global_earth_orientation_table(&base_eop), "set base global EOP", failures);
    expect_status(
        calc_observed_utc(
            &base_context,
            UTC_SAMPLE,
            &body,
            1,
            flags,
            &base_observed,
            &diagnostic),
        TAIYIN_STATUS_OK,
        "base EOP observed UTC topocentric",
        failures);
    expect_true(set_global_earth_orientation_table(&shifted_eop), "set shifted global EOP", failures);
    expect_status(
        calc_observed_utc(
            &shifted_context,
            UTC_SAMPLE,
            &body,
            1,
            flags,
            &shifted_observed,
            &diagnostic),
        TAIYIN_STATUS_OK,
        "shifted EOP observed UTC topocentric",
        failures);

    expect_true(std::isfinite(base_observed.horizontal.azimuth_rad), "base horizontal azimuth finite", failures);
    expect_true(std::isfinite(base_observed.horizontal.altitude_rad), "base horizontal altitude finite", failures);
    expect_true(std::isfinite(shifted_observed.horizontal.azimuth_rad), "shifted horizontal azimuth finite", failures);
    expect_true(std::isfinite(shifted_observed.horizontal.altitude_rad), "shifted horizontal altitude finite", failures);

    const double azimuth_delta = std::fabs(angular_difference_radians(
        shifted_observed.horizontal.azimuth_rad,
        base_observed.horizontal.azimuth_rad));
    const double altitude_delta = std::fabs(shifted_observed.horizontal.altitude_rad - base_observed.horizontal.altitude_rad);
    expect_true(
        azimuth_delta > 1.0e-7 || altitude_delta > 1.0e-7,
        "EOP changes topocentric horizontal output",
        failures);
}

void test_observed_utc_topocentric_uses_dut1_and_polar_motion(int* failures) {
    using namespace taiyin;
    using namespace taiyin::runtime;

    internal::EarthOrientationSample base_samples[3];
    internal::EarthOrientationTable base_eop;
    make_eop_table_around(julian_day(UTC_SAMPLE), -0.101, 0.075, 0.240, 0.0012, 0.35, -0.15, base_samples, &base_eop);

    internal::EarthOrientationSample dut1_samples[3];
    internal::EarthOrientationTable dut1_eop;
    make_eop_table_around(julian_day(UTC_SAMPLE), 0.850, 0.075, 0.240, 0.0012, 0.35, -0.15, dut1_samples, &dut1_eop);

    internal::EarthOrientationSample polar_samples[3];
    internal::EarthOrientationTable polar_eop;
    make_eop_table_around(julian_day(UTC_SAMPLE), -0.101, 8.000, -6.000, 0.0012, 0.35, -0.15, polar_samples, &polar_eop);

    NativeCalcContext base_context = make_observed_utc_context();
    NativeCalcContext dut1_context = make_observed_utc_context();
    NativeCalcContext polar_context = make_observed_utc_context();
    const int body = TAIYIN_BODY_MOON;
    ObservedPosition base_observed;
    ObservedPosition dut1_observed;
    ObservedPosition polar_observed;
    EphemerisEvalDiagnostic diagnostic;
    const uint64_t flags = TAIYIN_OBSERVED_TOPOCENTRIC
        | TAIYIN_OBSERVED_HORIZONTAL
        | TAIYIN_OBSERVED_SPEED;

    expect_true(set_global_earth_orientation_table(&base_eop), "set base global EOP", failures);
    expect_status(
        calc_observed_utc(&base_context, UTC_SAMPLE, &body, 1, flags, &base_observed, &diagnostic),
        TAIYIN_STATUS_OK,
        "base observed UTC topocentric",
        failures);
    expect_true(set_global_earth_orientation_table(&dut1_eop), "set DUT1 global EOP", failures);
    expect_status(
        calc_observed_utc(&dut1_context, UTC_SAMPLE, &body, 1, flags, &dut1_observed, &diagnostic),
        TAIYIN_STATUS_OK,
        "DUT1-shifted observed UTC topocentric",
        failures);
    expect_true(set_global_earth_orientation_table(&polar_eop), "set polar global EOP", failures);
    expect_status(
        calc_observed_utc(&polar_context, UTC_SAMPLE, &body, 1, flags, &polar_observed, &diagnostic),
        TAIYIN_STATUS_OK,
        "polar-motion-shifted observed UTC topocentric",
        failures);

    expect_true(
        horizontal_angle_delta(dut1_observed, base_observed) > 1.0e-8,
        "DUT1 changes topocentric horizontal output",
        failures);
    expect_true(
        horizontal_angle_delta(polar_observed, base_observed) > 1.0e-8,
        "polar motion changes topocentric horizontal output",
        failures);
}

void test_observed_flag_contract_and_barycenter_mask(int* failures) {
    using namespace taiyin;
    using namespace taiyin::runtime;

    NativeCalcContext context = make_geocentric_context();
    const int body_id = TAIYIN_BODY_MARS;
    ObservedPosition observed;
    EphemerisEvalDiagnostic diagnostic;
    expect_status(
        calc_observed_ut(
            &context,
            JD_UT,
            &body_id,
            1,
            TAIYIN_OBSERVED_ALLOW_BARYCENTER_APPROX,
            &observed,
            &diagnostic),
        TAIYIN_STATUS_OK,
        "observed Mars permits barycenter approximation",
        failures);
    expect_true(
        observed.body_id == TAIYIN_BODY_MARS
            && observed.apparent.body_mask_bit == TAIYIN_MAJOR_BODY_MARS
            && observed.diagnostic.component_target_id
                == TAIYIN_BODY_MARS_BARYCENTER,
        "observed Mars preserves the major-body mask after barycenter fallback",
        failures);

    const uint32_t unsupported_flags[] = {
        TAIYIN_NATIVE_POSITION_XYZ,
        TAIYIN_NATIVE_POSITION_EQUATORIAL,
        TAIYIN_NATIVE_POSITION_RADIANS,
        TAIYIN_NATIVE_POSITION_NONUT,
    };
    for (size_t i = 0;
         i < sizeof(unsupported_flags) / sizeof(unsupported_flags[0]);
         ++i) {
        expect_status(
            calc_observed_ut(
                &context,
                JD_UT,
                &body_id,
                1,
                unsupported_flags[i],
                &observed,
                &diagnostic),
            TAIYIN_ERROR_UNSUPPORTED,
            "observed rejects unsupported native representation flag",
            failures);
    }
}

void test_calc_position_utc_cirs_uses_eop_cpo(int* failures) {
    using namespace taiyin;
    using namespace taiyin::runtime;

    internal::EarthOrientationSample base_samples[3];
    internal::EarthOrientationTable base_eop;
    make_eop_table_around(julian_day(UTC_SAMPLE), -0.101, 0.075, 0.240, 0.0012, 0.0, 0.0, base_samples, &base_eop);

    internal::EarthOrientationSample cpo_samples[3];
    internal::EarthOrientationTable cpo_eop;
    make_eop_table_around(julian_day(UTC_SAMPLE), -0.101, 0.075, 0.240, 0.0012, 120.0, -90.0, cpo_samples, &cpo_eop);

    NativeCalcContext base_context = make_geocentric_context();
    NativeCalcContext cpo_context = make_geocentric_context();

    base_context.model_context.precession_model_id = dispatch::PRECESSION_IAU2006;
    base_context.model_context.nutation_model_id = dispatch::NUTATION_IAU2000A;
    base_context.model_context.frame_route_id = dispatch::FRAME_ROUTE_CIRS;
    base_context.apparent_options.output_frame_id = TAIYIN_APPARENT_FRAME_CIRS;
    cpo_context.model_context = base_context.model_context;
    cpo_context.apparent_options.output_frame_id = TAIYIN_APPARENT_FRAME_CIRS;

    double base_cirs[6] = {};
    double cpo_cirs[6] = {};
    EphemerisEvalDiagnostic diagnostic;
    const uint32_t flags = TAIYIN_NATIVE_POSITION_TRUEPOS | TAIYIN_NATIVE_POSITION_RADIANS;
    expect_true(set_global_earth_orientation_table(&base_eop), "set base CPO table", failures);
    expect_status(
        calc_position_utc(&base_context, TAIYIN_BODY_MARS_BARYCENTER, UTC_SAMPLE, flags, base_cirs, &diagnostic),
        TAIYIN_STATUS_OK,
        "base CIRS position",
        failures);
    expect_true(set_global_earth_orientation_table(&cpo_eop), "set shifted CPO table", failures);
    expect_status(
        calc_position_utc(&cpo_context, TAIYIN_BODY_MARS_BARYCENTER, UTC_SAMPLE, flags, cpo_cirs, &diagnostic),
        TAIYIN_STATUS_OK,
        "CPO-shifted CIRS position",
        failures);
    expect_true(
        spherical_array_angle_delta(base_cirs, cpo_cirs) > 1.0e-8,
        "EOP CPO changes CIRS output",
        failures);

    base_context.apparent_options.output_frame_id = TAIYIN_APPARENT_FRAME_TRUE_EQUATOR_OF_DATE;
    cpo_context.apparent_options.output_frame_id = TAIYIN_APPARENT_FRAME_TRUE_EQUATOR_OF_DATE;
    double base_equator[6] = {};
    double cpo_equator[6] = {};
    expect_true(set_global_earth_orientation_table(&base_eop), "restore base CPO table", failures);
    expect_status(
        calc_position_utc(&base_context, TAIYIN_BODY_MARS_BARYCENTER, UTC_SAMPLE, flags, base_equator, &diagnostic),
        TAIYIN_STATUS_OK,
        "base true-equator position",
        failures);
    expect_true(set_global_earth_orientation_table(&cpo_eop), "restore shifted CPO table", failures);
    expect_status(
        calc_position_utc(&cpo_context, TAIYIN_BODY_MARS_BARYCENTER, UTC_SAMPLE, flags, cpo_equator, &diagnostic),
        TAIYIN_STATUS_OK,
        "CPO-shifted true-equator position",
        failures);
    expect_near(base_equator[0], cpo_equator[0], 1.0e-14, "CPO does not affect equinox-route longitude", failures);
    expect_near(base_equator[1], cpo_equator[1], 1.0e-14, "CPO does not affect equinox-route latitude", failures);
    expect_near(base_equator[2], cpo_equator[2], 1.0e-14, "CPO does not affect equinox-route distance", failures);
}

void test_calc_position_utc_matches_manual_precise_scales_and_cpo(int* failures) {
    using namespace taiyin;
    using namespace taiyin::runtime;

    internal::EarthOrientationSample samples[3];
    internal::EarthOrientationTable eop_table;
    make_eop_table_around(julian_day(UTC_SAMPLE), -0.123, 0.091, -0.211, 0.0015, 3.50, -2.75, samples, &eop_table);

    NativeCalcContext utc_context = make_geocentric_context();
    expect_true(set_global_earth_orientation_table(&eop_table), "set manual global EOP", failures);
    utc_context.model_context.precession_model_id = dispatch::PRECESSION_IAU2006;
    utc_context.model_context.nutation_model_id = dispatch::NUTATION_IAU2000A;
    utc_context.model_context.frame_route_id = dispatch::FRAME_ROUTE_CIRS;
    utc_context.apparent_options.output_frame_id = TAIYIN_APPARENT_FRAME_CIRS;

    TimeScaleOptions options;
    options.allow_utc_out_of_range_estimate = false;
    options.tdb_model_id = utc_context.model_context.tdb_model_id;
    options.delta_t_model_id = utc_context.delta_t_model_id;
    options.ephemeris_family_id = utc_context.ephemeris_family_id;
    options.leap_second_table = builtin_leap_second_table();
    PreciseTimeScales scales;
    TimeScaleDiagnostic time_diagnostic;
    expect_true(
        make_time_scales_from_utc(UTC_SAMPLE, &eop_table, &options, &scales, &time_diagnostic),
        "manual UTC time scales",
        failures);

    internal::EarthOrientationSample eop;
    internal::EarthOrientationRates rates;
    internal::EarthRotationDerivatives derivatives;
    const SplitJulianDate jd_utc = scales.jd_utc;
    const SplitJulianDate jd_ut1 = scales.jd_ut1;
    const SplitJulianDate jd_tt = scales.jd_tt;
    expect_true(internal::interpolate_earth_orientation(&eop_table, jd_utc, &eop), "manual interpolate EOP", failures);
    expect_true(internal::derive_earth_orientation_rates(&eop_table, jd_utc, &rates, &derivatives), "manual derive EOP rates", failures);

    NativeCalcContext manual_context = utc_context;
    expect_status(
        native_context_set_celestial_pole_offset(
            &manual_context,
            eop.dx_rad,
            eop.dy_rad,
            rates.dx_rate_rad_per_day,
            rates.dy_rate_rad_per_day),
        TAIYIN_STATUS_OK,
        "manual set CPO",
        failures);

    double utc_position[6] = {};
    double manual_position[6] = {};
    EphemerisEvalDiagnostic diagnostic;
    const uint32_t flags = TAIYIN_NATIVE_POSITION_TRUEPOS | TAIYIN_NATIVE_POSITION_RADIANS;
    expect_status(
        calc_position_utc(&utc_context, TAIYIN_BODY_MARS_BARYCENTER, UTC_SAMPLE, flags, utc_position, &diagnostic),
        TAIYIN_STATUS_OK,
        "UTC CIRS position",
        failures);
    expect_status(
        calc_position_tdb(
            &manual_context,
            TAIYIN_BODY_MARS_BARYCENTER,
            scales.jd_tdb,
            jd_tt,
            flags,
            manual_position,
            &diagnostic),
        TAIYIN_STATUS_OK,
        "manual TDB CIRS position",
        failures);
    for (int i = 0; i < 3; ++i) {
        expect_near(utc_position[i], manual_position[i], 1.0e-14, "UTC/manual CIRS position", failures);
    }

    double default_tt_position[6] = {};
    double fallback_tt_position[6] = {};
    expect_status(
        calc_position_tdb(
            &manual_context,
            TAIYIN_BODY_MARS_BARYCENTER,
            scales.jd_tdb,
            SplitJulianDate(),
            flags,
            default_tt_position,
            &diagnostic),
        TAIYIN_STATUS_OK,
        "default split TT falls back to TDB",
        failures);
    expect_status(
        calc_position_tdb(
            &manual_context,
            TAIYIN_BODY_MARS_BARYCENTER,
            scales.jd_tdb,
            scales.jd_tdb,
            flags,
            fallback_tt_position,
            &diagnostic),
        TAIYIN_STATUS_OK,
        "explicit TDB-as-TT fallback position",
        failures);
    for (int i = 0; i < 6; ++i) {
        expect_near(
            default_tt_position[i], fallback_tt_position[i], 0.0,
            "default split TT fallback matches explicit TDB", failures);
    }
}

void test_precise_topocentric_observer_matches_erfa_baked_oracle(int* failures) {
    using namespace taiyin;
    using namespace taiyin::runtime;

    internal::EarthOrientationSample samples[3];
    internal::EarthOrientationTable eop_table;
    make_eop_table_around(julian_day(UTC_SAMPLE), -0.123, 0.091, -0.211, 0.0015, 3.50, -2.75, samples, &eop_table);

    NativeCalcContext context = make_geocentric_context();
    expect_true(set_global_earth_orientation_table(&eop_table), "set ERFA oracle global EOP", failures);

    TimeScaleOptions options;
    options.allow_utc_out_of_range_estimate = false;
    options.tdb_model_id = context.model_context.tdb_model_id;
    options.delta_t_model_id = context.delta_t_model_id;
    options.ephemeris_family_id = context.ephemeris_family_id;
    options.leap_second_table = builtin_leap_second_table();
    PreciseTimeScales scales;
    TimeScaleDiagnostic time_diagnostic;
    expect_true(
        make_time_scales_from_utc(UTC_SAMPLE, &eop_table, &options, &scales, &time_diagnostic),
        "ERFA oracle time scales",
        failures);
    const SplitJulianDate jd_utc = scales.jd_utc;
    const SplitJulianDate jd_ut1 = scales.jd_ut1;
    const SplitJulianDate jd_tt = scales.jd_tt;

    const NativeObserverLocation beijing = native_observer_location_degrees(116.4074, 39.9042, 43.5);
    expect_status(
        native_context_set_precise_topocentric_observer(&context, beijing, jd_utc, jd_tt),
        TAIYIN_STATUS_OK,
        "set precise topocentric observer for ERFA oracle",
        failures);

    Vector3 cirs_position;
    expect_true(
        observer_geocentric_cirs_position_au(
            beijing.longitude_rad,
            beijing.latitude_rad,
            beijing.height_m,
            jd_ut1,
            samples[1].xp_rad,
            samples[1].yp_rad,
            samples[1].sp_rad,
            &cirs_position),
        "CIRS observer position for ERFA oracle",
        failures);

    // Baked from pyerfa 2.0.1.5 / ERFA for UTC 2024-04-08T18:17:20,
    // Beijing WGS84 geodetic coordinates, DUT1=-0.123s, xp=0.091",
    // yp=-0.211", dX=3.50mas, dY=-2.75mas. Runtime test does not
    // import pyerfa; these constants pin the external ERFA convention.
    const double expected_cirs_au[3] = {
        -2.19463120427051411e-05,
        -2.43110310721646453e-05,
        2.72053528343107097e-05,
    };
    const double expected_icrf_au[3] = {
        -2.18823743115243841e-05,
        -2.43099743605155673e-05,
        2.72577495033667530e-05,
    };

    expect_near(cirs_position.x, expected_cirs_au[0], 5.0e-15, "ERFA CIRS observer x", failures);
    expect_near(cirs_position.y, expected_cirs_au[1], 5.0e-15, "ERFA CIRS observer y", failures);
    expect_near(cirs_position.z, expected_cirs_au[2], 5.0e-15, "ERFA CIRS observer z", failures);
    expect_near(context.apparent_options.observer_offset.position_au.x, expected_icrf_au[0], 5.0e-15, "ERFA ICRF observer x", failures);
    expect_near(context.apparent_options.observer_offset.position_au.y, expected_icrf_au[1], 5.0e-15, "ERFA ICRF observer y", failures);
    expect_near(context.apparent_options.observer_offset.position_au.z, expected_icrf_au[2], 5.0e-15, "ERFA ICRF observer z", failures);
}

void test_horizontal_formula_matches_erfa_baked_oracle(int* failures) {
    using namespace taiyin;

    const double latitude_rad = 39.9042 * TAIYIN_DEG_TO_RAD;
    const double hour_angle_rad = 1.234567890123;
    const double declination_rad = -0.456789012345;
    const Vector3 topocentric_equatorial = {
        std::cos(declination_rad),
        0.0,
        std::sin(declination_rad),
    };

    const HorizontalCoordinates horizontal = topocentric_position_to_horizontal(
        topocentric_equatorial,
        hour_angle_rad,
        latitude_rad);

    // Baked from pyerfa.erfa.hd2ae(hour_angle, declination, latitude).
    expect_near(horizontal.azimuth_rad, 4.15481671102492811e+00, 1.0e-15, "ERFA hd2ae azimuth", failures);
    expect_near(horizontal.altitude_rad, -5.58314600230030919e-02, 1.0e-15, "ERFA hd2ae altitude", failures);
    expect_near(horizontal.distance_au, 1.0, 1.0e-15, "horizontal unit distance", failures);
}

void test_observed_utc_refraction_requires_atmosphere_fields(int* failures) {
    using namespace taiyin;
    using namespace taiyin::runtime;

    internal::EarthOrientationSample samples[3];
    internal::EarthOrientationTable eop_table;
    make_eop_table_around(julian_day(UTC_SAMPLE), -0.101, 0.075, 0.240, 0.0012, 0.35, -0.15, samples, &eop_table);

    expect_true(set_global_earth_orientation_table(&eop_table), "set refraction global EOP", failures);
    NativeCalcContext context = make_observed_utc_context();
    const int body = TAIYIN_BODY_MOON;
    ObservedPosition observed;
    EphemerisEvalDiagnostic diagnostic;
    const uint64_t flags = TAIYIN_OBSERVED_TOPOCENTRIC
        | TAIYIN_OBSERVED_HORIZONTAL
        | TAIYIN_OBSERVED_REFRACTION;

    expect_status(
        calc_observed_utc(&context, UTC_SAMPLE, &body, 1, flags, &observed, &diagnostic),
        TAIYIN_ERROR_INVALID_ARGUMENT,
        "refraction requires atmosphere fields",
        failures);

    expect_status(
        native_context_set_atmosphere_pressure_temperature(&context, 1010.0, 10.0),
        TAIYIN_STATUS_OK,
        "set refraction pressure/temperature",
        failures);
    expect_status(
        calc_observed_utc(&context, UTC_SAMPLE, &body, 1, flags, &observed, &diagnostic),
        TAIYIN_STATUS_OK,
        "refraction works with required atmosphere fields",
        failures);
    expect_true(std::isfinite(observed.refracted_horizontal.altitude_rad), "refracted altitude finite", failures);
}

void test_non_earth_topocentric_is_unsupported(int* failures) {
    using namespace taiyin;
    using namespace taiyin::runtime;

    NativeCalcContext context = make_geocentric_context();
    expect_status(
        native_context_set_geocentric_observer(
            &context, TAIYIN_BODY_MARS_BARYCENTER, TAIYIN_BODY_SUN),
        TAIYIN_STATUS_OK,
        "set non-Earth observer for topocentric boundary",
        failures);

    const CartesianState zero_offset = {};
    expect_status(
        native_context_set_topocentric_observer_offset(&context, zero_offset),
        TAIYIN_ERROR_UNSUPPORTED,
        "Cartesian topocentric offset rejects non-Earth observer",
        failures);

    const NativeObserverLocation surface =
        native_observer_location_degrees(0.0, 0.0, 0.0);
    expect_status(
        native_context_set_simple_topocentric_observer(
            &context, surface, JD_UT, JD_UT),
        TAIYIN_ERROR_UNSUPPORTED,
        "simple topocentric observer rejects non-Earth observer",
        failures);
    expect_status(
        native_context_set_precise_topocentric_observer(
            &context, surface, JD_UT, JD_UT),
        TAIYIN_ERROR_UNSUPPORTED,
        "precise topocentric observer rejects non-Earth observer",
        failures);

    EphemerisEvalDiagnostic diagnostic;
    double position[6] = {};
    expect_status(
        calc_position_ut(
            &context,
            TAIYIN_BODY_SUN,
            JD_UT,
            TAIYIN_NATIVE_POSITION_XYZ | TAIYIN_NATIVE_POSITION_TOPOCENTRIC,
            position,
            &diagnostic),
        TAIYIN_ERROR_UNSUPPORTED,
        "native topocentric position rejects non-Earth observer",
        failures);

    const int body_id = TAIYIN_BODY_SUN;
    ObservedPosition observed;
    expect_status(
        calc_observed_ut(
            &context,
            JD_UT,
            &body_id,
            1,
            TAIYIN_OBSERVED_TOPOCENTRIC,
            &observed,
            &diagnostic),
        TAIYIN_ERROR_UNSUPPORTED,
        "observed topocentric position rejects non-Earth observer",
        failures);
}

void test_native_position_many_deflectors(int* failures) {
    using namespace taiyin;
    using namespace taiyin::runtime;

    NativeCalcContext context = make_geocentric_context();
    ApparentDeflector deflectors[5];
    const int body_ids[5] = {
        TAIYIN_BODY_SUN,
        TAIYIN_BODY_JUPITER_BARYCENTER,
        TAIYIN_BODY_SATURN_BARYCENTER,
        TAIYIN_BODY_URANUS_BARYCENTER,
        TAIYIN_BODY_NEPTUNE_BARYCENTER,
    };
    for (size_t i = 0; i < 5u; ++i) {
        deflectors[i] = *native_solar_deflector();
        deflectors[i].body_id = body_ids[i];
        if (i > 0u) {
            deflectors[i].schwarzschild_radius_au *= 1.0e-6;
        }
    }
    expect_status(
        native_context_set_deflectors(&context, deflectors, 5u, 0),
        TAIYIN_STATUS_OK,
        "set more deflectors than the native inline capacity",
        failures);

    double position[6] = {};
    EphemerisEvalDiagnostic diagnostic;
    expect_status(
        calc_position_ut(
            &context,
            TAIYIN_BODY_MARS_BARYCENTER,
            JD_UT,
            TAIYIN_NATIVE_POSITION_RADIANS,
            position,
            &diagnostic),
        TAIYIN_STATUS_OK,
        "native position supports overflow deflector storage",
        failures);
    expect_position_vector(position, false, "many-deflector position", failures);
}

}  // namespace

int main() {
    int failures = 0;
    if (initialize_packaged_runtime(&failures)) {
        test_default_apparent_jupiter_matches_swiss(&failures);
        test_solar_target_shapiro_skips_self_deflector(&failures);
        test_native_position_batch(&failures);
        test_native_context_apparent_matrix_cache(&failures);
        test_context_epoch_cache_invalidated_by_runtime_clear(&failures);
        test_context_caches_invalidated_by_dispatch_replacement(&failures);
        test_shared_native_context_concurrent_reads(&failures);
        test_major_body_apparent_batch(&failures);
        test_observed_utc_requires_time_tables(&failures);
        test_observed_utc_requires_eop_coverage(&failures);
        test_utc_position_apis_propagate_time_failures(&failures);
        test_observed_utc_geocentric_matches_native_utc(&failures);
        test_observed_utc_topocentric_uses_eop(&failures);
        test_observed_utc_topocentric_uses_dut1_and_polar_motion(&failures);
        test_observed_flag_contract_and_barycenter_mask(&failures);
        test_calc_position_utc_cirs_uses_eop_cpo(&failures);
        test_calc_position_utc_matches_manual_precise_scales_and_cpo(&failures);
        test_precise_topocentric_observer_matches_erfa_baked_oracle(&failures);
        test_horizontal_formula_matches_erfa_baked_oracle(&failures);
        test_observed_utc_refraction_requires_atmosphere_fields(&failures);
        test_non_earth_topocentric_is_unsupported(&failures);
        test_native_position_many_deflectors(&failures);
    }
    return failures == 0 ? 0 : 1;
}
