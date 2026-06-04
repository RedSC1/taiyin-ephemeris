#include "taiyin/corrections.h"
#include "taiyin/dispatch.h"
#include "taiyin/earth_rotation.h"
#include "taiyin/internal/builtin_loader.h"
#include "taiyin/internal/descriptor_loader.h"
#include "taiyin/internal/eop.h"
#include "taiyin/internal/ephemeris_block.h"
#include "taiyin/internal/ephemeris_cache.h"
#include "taiyin/internal/ephemeris_catalog.h"
#include "taiyin/internal/ephemeris_discovery.h"
#include "taiyin/internal/spk.h"
#include "taiyin/pipeline.h"
#include "taiyin/time.h"

#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <thread>
#include <vector>

namespace {

const char* kNasaBspRoot = std::getenv("TAIYIN_NASA_BSP_ROOT");
const char* kDe441Path = std::getenv("TAIYIN_DE441_PATH");
const double kArcsecPerRadian = 206264.8062470963552;
const double kDegPerRadian = 180.0 / 3.141592653589793238462643383279502884;
const double kArcsecPerDegree = 3600.0;
const double kHorizonsAirlessNoDiurnalAzimuthDeg = 328.608846;
const double kHorizonsAirlessNoDiurnalAltitudeDeg = -59.647355;
const double kHorizonsAirlessWithDiurnalAzimuthDeg = 328.608949;
const double kHorizonsAirlessWithDiurnalAltitudeDeg = -59.647375;
const double kHorizonsToleranceArcsec = 0.1;
const double JUPITER_SOLAR_MASS_RATIO = 9.547919384243266e-4;

bool file_exists(const char* path) {
    if (!path) {
        return false;
    }
    std::ifstream f(path, std::ios::binary);
    return static_cast<bool>(f);
}

int failures = 0;

void expect_true(bool value, const char* msg) {
    if (!value) {
        std::fprintf(stderr, "FAIL: expected true: %s\n", msg);
        ++failures;
    }
}

void expect_near(double actual, double expected, double tol, const char* msg) {
    if (std::fabs(actual - expected) > tol) {
        std::fprintf(stderr, "FAIL: %s: actual=%.17g expected=%.17g diff=%.17g tol=%.17g\n",
                     msg, actual, expected, std::fabs(actual - expected), tol);
        ++failures;
    }
}

void expect_near_arcsec(double actual_deg, double expected_deg, double tolerance_arcsec, const char* msg) {
    expect_near(actual_deg, expected_deg, tolerance_arcsec / kArcsecPerDegree, msg);
}

bool vector3_position_from_block(double jd_tdb, const void* data, taiyin::Vector3* out) {
    const taiyin::internal::CompiledEphemerisBlock* block =
        static_cast<const taiyin::internal::CompiledEphemerisBlock*>(data);
    return taiyin::internal::eval_compiled_ephemeris_block_position(jd_tdb, block, out);
}

bool vector3_velocity_from_block(double jd_tdb, const void* data, taiyin::Vector3* out) {
    const taiyin::internal::CompiledEphemerisBlock* block =
        static_cast<const taiyin::internal::CompiledEphemerisBlock*>(data);
    return taiyin::internal::eval_compiled_ephemeris_block_velocity(jd_tdb, block, out);
}

struct DescriptorEvalContext {
    const taiyin::internal::EphemerisBlockDescriptor* descriptor;
    taiyin::internal::EphemerisBlockCache* cache;
};

bool vector3_position_from_descriptor(double jd_tdb, const void* data, taiyin::Vector3* out) {
    const DescriptorEvalContext* context = static_cast<const DescriptorEvalContext*>(data);
    return context
        && context->descriptor
        && context->cache
        && taiyin::internal::eval_descriptor_position(*context->descriptor, context->cache, jd_tdb, out);
}

bool vector3_velocity_from_descriptor(double jd_tdb, const void* data, taiyin::Vector3* out) {
    const DescriptorEvalContext* context = static_cast<const DescriptorEvalContext*>(data);
    return context
        && context->descriptor
        && context->cache
        && taiyin::internal::eval_descriptor_velocity(*context->descriptor, context->cache, jd_tdb, out);
}

bool vector3_acceleration_from_descriptor(double jd_tdb, const void* data, taiyin::Vector3* out) {
    const DescriptorEvalContext* context = static_cast<const DescriptorEvalContext*>(data);
    return context
        && context->descriptor
        && context->cache
        && taiyin::internal::eval_descriptor_acceleration(*context->descriptor, context->cache, jd_tdb, out);
}

bool discover_catalog_from_data_root(taiyin::internal::EphemerisBlockCatalog* out) {
    if (!kNasaBspRoot) {
        return false;
    }
    std::vector<taiyin::internal::EphemerisDiscoverFileFn> discoverers;
    taiyin::internal::append_builtin_ephemeris_discoverers(&discoverers);
    taiyin::internal::EphemerisDiscoveryOptions options;
    return taiyin::internal::discover_ephemeris_catalog_from_directory(
        kNasaBspRoot,
        discoverers,
        options,
        out);
}

const taiyin::internal::EphemerisBlockDescriptor* find_catalog_descriptor(
    const taiyin::internal::EphemerisBlockCatalog& catalog,
    int target_id,
    int center_id,
    double jd_tdb
) {
    taiyin::internal::EphemerisBlockQuery query;
    query.target_id = target_id;
    query.center_id = center_id;
    query.frame = taiyin::internal::EphemerisFrame::IcrfJ2000Equatorial;
    query.jd_tdb = jd_tdb;
    return catalog.find_first(query);
}

taiyin::internal::EphemerisBlockDescriptor make_spk_source_descriptor(
    int target_id,
    int center_id,
    double start_jd_tdb,
    double end_jd_tdb
) {
    taiyin::internal::EphemerisBlockDescriptor descriptor;
    descriptor.route_key = taiyin::internal::EphemerisRouteKey(target_id, center_id, 2, 0);
    descriptor.source_key = taiyin::internal::EphemerisBlockKey(2, static_cast<uint64_t>(target_id * 1000 + center_id), 1, 0);
    descriptor.target_id = target_id;
    descriptor.center_id = center_id;
    descriptor.method_id = 2;
    descriptor.frame = taiyin::internal::EphemerisFrame::IcrfJ2000Equatorial;
    descriptor.format = taiyin::internal::EphemerisBlockFormat::Spk;
    descriptor.jd_tdb_start = start_jd_tdb;
    descriptor.jd_tdb_end = end_jd_tdb;
    descriptor.path = kDe441Path;
    return descriptor;
}

// Test 1: Equinox route — Mars apparent topocentric horizontal
// Uses DE441 SPK blocks, equinox frame route (Vondrak2011 + IAU2000B),
// a ground observer, and Bennett refraction.
void test_equinox_mars_topocentric() {
    using taiyin::internal::CompiledEphemerisBlock;
    using taiyin::internal::StorageEphemerisBlock;
    using taiyin::internal::compile_spk_ephemeris_block_from_file;
    using taiyin::internal::destroy_storage_ephemeris_block;
    using taiyin::internal::get_compiled_block_from_storage;
    using taiyin::internal::eval_compiled_ephemeris_block;
    using taiyin::internal::eval_compiled_ephemeris_block_acceleration;

    if (!file_exists(kDe441Path)) {
        std::printf("skipping equinox Mars topocentric test; local DE441 is absent\n");
        return;
    }

    // 2024-01-01T22:00:00Z — Mars should be visible from Greenwich in the evening
    const double jd_utc = 2460311.416667;
    const double tai_minus_utc = 37.0;  // 2024

    // Load builtin EOP table
    taiyin::internal::EarthOrientationTable eop_table;
    expect_true(taiyin::internal::load_builtin_eop_table(&eop_table), "load builtin EOP table");

    // Interpolate EOP at this jd_utc
    taiyin::internal::EarthOrientationSample eop_sample;
    expect_true(taiyin::internal::interpolate_earth_orientation(&eop_table, jd_utc, &eop_sample),
                "interpolate EOP at jd_utc");

    taiyin::internal::EarthOrientationRates eop_rates;
    taiyin::internal::EarthRotationDerivatives eop_derivs;
    expect_true(taiyin::internal::derive_earth_orientation_rates(&eop_table, jd_utc, &eop_rates, &eop_derivs),
                "derive EOP rates at jd_utc");

    std::printf("EOP: dut1=%.6f s, xp=%.6f\", yp=%.6f\", lod=%.6f ms, dx=%.6f\", dy=%.6f\"\n",
                eop_sample.dut1_seconds,
                eop_sample.xp_rad * kArcsecPerRadian,
                eop_sample.yp_rad * kArcsecPerRadian,
                eop_sample.lod_seconds * 1000.0,
                eop_sample.dx_rad * kArcsecPerRadian,
                eop_sample.dy_rad * kArcsecPerRadian);

    const double dut1_seconds = eop_sample.dut1_seconds;
    const double jd_tt = taiyin::utc_to_tt_jd(jd_utc, tai_minus_utc);
    const double jd_ut1 = taiyin::utc_to_ut1_jd(jd_utc, dut1_seconds);
    const double jd_tdb = taiyin::tt_to_tdb_jd(jd_tt, taiyin::TdbModel::SofaFull);
    const double start = jd_tdb - 2.0;
    const double end = jd_tdb + 2.0;

    // Greenwich: 0 deg longitude, 51.4773207 deg N, 67.0693m altitude
    const double longitude_rad = 0.0;
    const double latitude_rad = 51.4773207 * 3.14159265358979323846 / 180.0;
    const double height_m = 67.0693;

    const double xp_rad = eop_sample.xp_rad;
    const double yp_rad = eop_sample.yp_rad;
    const double sp_rad = taiyin::internal::sp_rad_for_jd(jd_utc);
    const double dx_rad = eop_sample.dx_rad;
    const double dy_rad = eop_sample.dy_rad;
    const double lod_seconds = eop_sample.lod_seconds;
    const double xp_rate = eop_rates.xp_rate_rad_per_day;
    const double yp_rate = eop_rates.yp_rate_rad_per_day;
    const double sp_rate = eop_rates.sp_rate_rad_per_day;
    const double dut1_rate = eop_derivs.dut1_rate_seconds_per_day;

    taiyin::internal::destroy_earth_orientation_table(&eop_table);

    // Compile SPK blocks
    StorageEphemerisBlock mars_storage;
    StorageEphemerisBlock earth_helio_storage;
    StorageEphemerisBlock earth_bary_storage;
    StorageEphemerisBlock jupiter_storage;
    expect_true(
        compile_spk_ephemeris_block_from_file(kDe441Path, 4, 10, start, end, &mars_storage),
        "compile Mars heliocentric for equinox topocentric");
    expect_true(
        compile_spk_ephemeris_block_from_file(kDe441Path, 399, 10, start, end, &earth_helio_storage),
        "compile Earth heliocentric for equinox topocentric");
    expect_true(
        compile_spk_ephemeris_block_from_file(kDe441Path, 399, 0, start, end, &earth_bary_storage),
        "compile Earth barycentric for equinox topocentric");
    expect_true(
        compile_spk_ephemeris_block_from_file(kDe441Path, 5, 10, start, end, &jupiter_storage),
        "compile Jupiter heliocentric for equinox topocentric");

    // Extract views from storage blocks
    CompiledEphemerisBlock mars_heliocentric;
    CompiledEphemerisBlock earth_heliocentric;
    CompiledEphemerisBlock earth_barycentric;
    CompiledEphemerisBlock jupiter_heliocentric;
    get_compiled_block_from_storage(&mars_storage, 0, &mars_heliocentric);
    get_compiled_block_from_storage(&earth_helio_storage, 0, &earth_heliocentric);
    get_compiled_block_from_storage(&earth_bary_storage, 0, &earth_barycentric);
    get_compiled_block_from_storage(&jupiter_storage, 0, &jupiter_heliocentric);

    // Get Earth states for aberration
    taiyin::CartesianState earth_helio;
    taiyin::CartesianState earth_bary;
    expect_true(eval_compiled_ephemeris_block(jd_tdb, &earth_heliocentric, &earth_helio), "eval Earth helio state");
    expect_true(eval_compiled_ephemeris_block(jd_tdb, &earth_barycentric, &earth_bary), "eval Earth bary state");
    taiyin::Vector3 earth_bary_accel;
    expect_true(eval_compiled_ephemeris_block_acceleration(jd_tdb, &earth_barycentric, &earth_bary_accel),
                "eval Earth barycentric acceleration");

    taiyin::CartesianState jupiter_helio;
    expect_true(eval_compiled_ephemeris_block(jd_tdb, &jupiter_heliocentric, &jupiter_helio), "eval Jupiter helio state");

    // Set up pipeline context
    taiyin::pipeline::PipelineContext ctx;
    ctx.step_data_in.resize(10, nullptr);
    ctx.step_data_out.resize(10, nullptr);

    // Step 0: Light time
    taiyin::pipeline::LightTimeStepData lt_data;
    lt_data.jd_tdb = jd_tdb;
    lt_data.observer_position_au = earth_helio.position_au;
    lt_data.observer_velocity_au_per_day = earth_helio.velocity_au_per_day;
    lt_data.target_block = &mars_heliocentric;
    lt_data.target_position_fn = vector3_position_from_block;
    lt_data.target_velocity_fn = vector3_velocity_from_block;
    lt_data.max_iterations = 16;
    lt_data.tolerance_days = 1e-15;
    ctx.step_data_in[0] = &lt_data;
    ctx.step_data_out[0] = &lt_data;

    // Step 1: Deflection (Sun)
    taiyin::pipeline::DeflectionStepData def_data;
    def_data.observer_heliocentric_position_au = earth_helio.position_au;
    def_data.observer_heliocentric_velocity_au_per_day = earth_helio.velocity_au_per_day;
    def_data.sun_heliocentric_position_au = {0.0, 0.0, 0.0};
    def_data.sun_heliocentric_velocity_au_per_day = {0.0, 0.0, 0.0};
    def_data.schwarzschild_radius_au = taiyin::TAIYIN_SOLAR_SCHWARZSCHILD_RADIUS_AU;
    def_data.deflection_limit = taiyin::TAIYIN_SOLAR_DEFLECTION_LIMIT;
    ctx.step_data_in[1] = &def_data;
    ctx.step_data_out[1] = &def_data;

    // Step 2: Deflection (Jupiter)
    taiyin::pipeline::DeflectionStepData jup_def_data;
    jup_def_data.observer_heliocentric_position_au = earth_helio.position_au;
    jup_def_data.observer_heliocentric_velocity_au_per_day = earth_helio.velocity_au_per_day;
    jup_def_data.sun_heliocentric_position_au = jupiter_helio.position_au;
    jup_def_data.sun_heliocentric_velocity_au_per_day = jupiter_helio.velocity_au_per_day;
    jup_def_data.schwarzschild_radius_au = taiyin::TAIYIN_SOLAR_SCHWARZSCHILD_RADIUS_AU * JUPITER_SOLAR_MASS_RATIO;
    jup_def_data.deflection_limit = taiyin::TAIYIN_SOLAR_DEFLECTION_LIMIT;
    ctx.step_data_in[2] = &jup_def_data;
    ctx.step_data_out[2] = &jup_def_data;

    // Step 3: Aberration
    taiyin::pipeline::AberrationStepData ab_data;
    ab_data.observer_heliocentric_position_au = earth_helio.position_au;
    ab_data.observer_heliocentric_velocity_au_per_day = earth_helio.velocity_au_per_day;
    ab_data.observer_heliocentric_acceleration_au_per_day2 = {0.0, 0.0, 0.0};
    ab_data.observer_barycentric_velocity_au_per_day = earth_bary.velocity_au_per_day;
    ab_data.observer_barycentric_acceleration_au_per_day2 = earth_bary_accel;
    ab_data.light_time_days_per_au = taiyin::TAIYIN_LIGHT_TIME_DAYS_PER_AU;
    ab_data.solar_schwarzschild_radius_au = taiyin::TAIYIN_SOLAR_SCHWARZSCHILD_RADIUS_AU;
    ctx.step_data_in[3] = &ab_data;
    ctx.step_data_out[3] = &ab_data;

    // Step 4: Frame transform
    taiyin::pipeline::FrameTransformStepData frame_data;
    frame_data.jd_ut1 = jd_ut1;
    frame_data.jd_tt = jd_tt;
    frame_data.frame_route = taiyin::dispatch::FRAME_ROUTE_EQUINOX;
    frame_data.xp_rad = xp_rad;
    frame_data.yp_rad = yp_rad;
    frame_data.sp_rad = sp_rad;
    frame_data.dx_rad = dx_rad;
    frame_data.dy_rad = dy_rad;
    frame_data.precession_model = taiyin::dispatch::PRECESSION_IAU2006;
    frame_data.nutation_model = taiyin::dispatch::NUTATION_IAU2000A;
    ctx.step_data_in[4] = &frame_data;
    ctx.step_data_out[4] = &frame_data;

    // Step 5: Observer geocentric
    taiyin::pipeline::ObserverGeocentricStepData obs_data;
    obs_data.jd_ut1 = jd_ut1;
    obs_data.jd_tt = jd_tt;
    obs_data.longitude_rad = longitude_rad;
    obs_data.latitude_rad = latitude_rad;
    obs_data.height_m = height_m;
    obs_data.xp_rad = xp_rad;
    obs_data.yp_rad = yp_rad;
    obs_data.sp_rad = sp_rad;
    obs_data.frame_route = taiyin::dispatch::FRAME_ROUTE_EQUINOX;
    obs_data.xp_rate_rad_per_day = xp_rate;
    obs_data.yp_rate_rad_per_day = yp_rate;
    obs_data.sp_rate_rad_per_day = sp_rate;
    obs_data.dut1_rate_seconds_per_day = dut1_rate;
    obs_data.lod_seconds = lod_seconds;
    obs_data.derivative_step_days = 1e-3;
    ctx.step_data_in[5] = &obs_data;
    ctx.step_data_out[5] = &obs_data;

    // Step 6: Topocentric
    taiyin::pipeline::TopocentricStepData topo_data;
    ctx.step_data_in[6] = &topo_data;
    ctx.step_data_out[6] = &topo_data;

    // Step 7: Diurnal Aberration
    taiyin::pipeline::DiurnalAberrationStepData da_data;
    ctx.step_data_in[7] = &da_data;
    ctx.step_data_out[7] = &da_data;

    // Step 8: Horizontal
    taiyin::pipeline::HorizontalStepData horiz_data;
    // Local sidereal = GAST + longitude
    const double gast = taiyin::gast_iau2000a_rad(jd_ut1, jd_tt);
    horiz_data.local_sidereal_rad = gast + longitude_rad;
    horiz_data.latitude_rad = latitude_rad;
    ctx.step_data_in[8] = &horiz_data;
    ctx.step_data_out[8] = &horiz_data;

    // Step 9: Refraction
    taiyin::pipeline::RefractionStepData refr_data;
    refr_data.refraction_model = taiyin::dispatch::REFRACTION_BENNETT;
    refr_data.pressure_mbar = 1013.25;
    refr_data.temperature_c = 10.0;
    refr_data.relative_humidity = 0.0;
    refr_data.wavelength_micrometer = 0.0;
    refr_data.max_iterations = 16;
    refr_data.tolerance = 1e-10;
    ctx.step_data_in[9] = &refr_data;
    ctx.step_data_out[9] = &refr_data;

    expect_true(
        taiyin::pipeline::run_pipeline(
            taiyin::pipeline::kEquinoxPipeline,
            taiyin::pipeline::kEquinoxPipelineStepCount,
            &ctx) == taiyin::pipeline::OK,
        "equinox pipeline via registry");

    const double azimuth_deg = refr_data.horizontal.azimuth_rad * kDegPerRadian;
    const double altitude_deg = refr_data.horizontal.altitude_rad * kDegPerRadian;

    // 1. Compare pre-diurnal topocentric coordinates (no diurnal aberration) vs raw Horizons
    auto pre_horiz = topocentric_position_to_horizontal(topo_data.topocentric_position_au, gast + longitude_rad, latitude_rad);
    const double pre_azimuth_deg = pre_horiz.azimuth_rad * kDegPerRadian;
    const double pre_altitude_deg = pre_horiz.altitude_rad * kDegPerRadian;
    const double d_az_pre_arcsec = (pre_azimuth_deg - kHorizonsAirlessNoDiurnalAzimuthDeg) * kArcsecPerDegree;
    const double d_alt_pre_arcsec = (pre_altitude_deg - kHorizonsAirlessNoDiurnalAltitudeDeg) * kArcsecPerDegree;
    std::printf("equinox Mars topocentric (pre-diurnal): az=%.6f deg, alt=%.6f deg, dAz=%+.3f\", dAlt=%+.3f\" vs Horizons (no-diurnal)\n",
                pre_azimuth_deg, pre_altitude_deg, d_az_pre_arcsec, d_alt_pre_arcsec);
    expect_near_arcsec(pre_azimuth_deg, kHorizonsAirlessNoDiurnalAzimuthDeg, kHorizonsToleranceArcsec,
                       "equinox pre-diurnal azimuth vs Horizons");
    expect_near_arcsec(pre_altitude_deg, kHorizonsAirlessNoDiurnalAltitudeDeg, kHorizonsToleranceArcsec,
                       "equinox pre-diurnal altitude vs Horizons");

    // 2. Compare post-diurnal/refracted coordinates (with diurnal aberration) vs theoretical base
    const double d_az_post_arcsec = (azimuth_deg - kHorizonsAirlessWithDiurnalAzimuthDeg) * kArcsecPerDegree;
    const double d_alt_post_arcsec = (altitude_deg - kHorizonsAirlessWithDiurnalAltitudeDeg) * kArcsecPerDegree;
    std::printf("equinox Mars topocentric (post-diurnal): az=%.6f deg, alt=%.6f deg, dAz=%+.3f\", dAlt=%+.3f\" vs target-base (with diurnal)\n",
                azimuth_deg, altitude_deg, d_az_post_arcsec, d_alt_post_arcsec);
    expect_near_arcsec(azimuth_deg, kHorizonsAirlessWithDiurnalAzimuthDeg, kHorizonsToleranceArcsec,
                       "equinox post-diurnal azimuth vs target-base");
    expect_near_arcsec(altitude_deg, kHorizonsAirlessWithDiurnalAltitudeDeg, kHorizonsToleranceArcsec,
                       "equinox post-diurnal altitude vs target-base");

    // Sanity checks: azimuth in [0, 360), altitude in valid range
    expect_true(azimuth_deg >= 0.0 && azimuth_deg < 360.0, "azimuth in range");
    expect_true(altitude_deg > -90.1 && altitude_deg <= 90.1, "altitude in valid range");

    // Verify unrefracted horizontal vs refracted: refraction should increase altitude
    const double unrefracted_alt_deg = horiz_data.horizontal.altitude_rad * kDegPerRadian;
    const double refracted_alt_deg = refr_data.horizontal.altitude_rad * kDegPerRadian;
    if (unrefracted_alt_deg > 0.0 && unrefracted_alt_deg < 85.0) {
        expect_true(refracted_alt_deg > unrefracted_alt_deg,
                    "equinox refraction increases altitude for positive alt");
    }

    destroy_storage_ephemeris_block(&mars_storage);
    destroy_storage_ephemeris_block(&earth_helio_storage);
    destroy_storage_ephemeris_block(&earth_bary_storage);
    destroy_storage_ephemeris_block(&jupiter_storage);
}

// Test 2: CIRS route — Mars apparent topocentric horizontal
void test_cirs_mars_topocentric() {
    using taiyin::internal::CompiledEphemerisBlock;
    using taiyin::internal::StorageEphemerisBlock;
    using taiyin::internal::compile_spk_ephemeris_block_from_file;
    using taiyin::internal::destroy_storage_ephemeris_block;
    using taiyin::internal::get_compiled_block_from_storage;
    using taiyin::internal::eval_compiled_ephemeris_block;
    using taiyin::internal::eval_compiled_ephemeris_block_acceleration;

    if (!file_exists(kDe441Path)) {
        std::printf("skipping CIRS Mars topocentric test; local DE441 is absent\n");
        return;
    }

    const double jd_utc = 2460311.416667;
    const double tai_minus_utc = 37.0;

    // Load EOP
    taiyin::internal::EarthOrientationTable eop_table;
    expect_true(taiyin::internal::load_builtin_eop_table(&eop_table), "CIRS load EOP");
    taiyin::internal::EarthOrientationSample eop_sample;
    expect_true(taiyin::internal::interpolate_earth_orientation(&eop_table, jd_utc, &eop_sample), "CIRS interpolate EOP");
    taiyin::internal::EarthOrientationRates eop_rates;
    taiyin::internal::EarthRotationDerivatives eop_derivs;
    expect_true(taiyin::internal::derive_earth_orientation_rates(&eop_table, jd_utc, &eop_rates, &eop_derivs), "CIRS EOP rates");

    const double dut1_seconds = eop_sample.dut1_seconds;
    const double jd_tt = taiyin::utc_to_tt_jd(jd_utc, tai_minus_utc);
    const double jd_ut1 = taiyin::utc_to_ut1_jd(jd_utc, dut1_seconds);
    const double jd_tdb = taiyin::tt_to_tdb_jd(jd_tt, taiyin::TdbModel::SofaFull);
    const double start = jd_tdb - 2.0;
    const double end = jd_tdb + 2.0;
    const double longitude_rad = 0.0;
    const double latitude_rad = 51.4773207 * 3.14159265358979323846 / 180.0;
    const double height_m = 67.0693;

    const double xp_rad = eop_sample.xp_rad;
    const double yp_rad = eop_sample.yp_rad;
    const double sp_rad = taiyin::internal::sp_rad_for_jd(jd_utc);
    const double dx_rad = eop_sample.dx_rad;
    const double dy_rad = eop_sample.dy_rad;
    const double lod_seconds = eop_sample.lod_seconds;
    const double xp_rate = eop_rates.xp_rate_rad_per_day;
    const double yp_rate = eop_rates.yp_rate_rad_per_day;
    const double sp_rate = eop_rates.sp_rate_rad_per_day;
    const double dut1_rate = eop_derivs.dut1_rate_seconds_per_day;

    taiyin::internal::destroy_earth_orientation_table(&eop_table);

    StorageEphemerisBlock mars_storage;
    StorageEphemerisBlock earth_helio_storage;
    StorageEphemerisBlock earth_bary_storage;
    StorageEphemerisBlock jupiter_storage;
    expect_true(
        compile_spk_ephemeris_block_from_file(kDe441Path, 4, 10, start, end, &mars_storage),
        "compile Mars heliocentric for CIRS topocentric");
    expect_true(
        compile_spk_ephemeris_block_from_file(kDe441Path, 399, 10, start, end, &earth_helio_storage),
        "compile Earth heliocentric for CIRS topocentric");
    expect_true(
        compile_spk_ephemeris_block_from_file(kDe441Path, 399, 0, start, end, &earth_bary_storage),
        "compile Earth barycentric for CIRS topocentric");
    expect_true(
        compile_spk_ephemeris_block_from_file(kDe441Path, 5, 10, start, end, &jupiter_storage),
        "compile Jupiter heliocentric for CIRS topocentric");

    // Extract views from storage blocks
    CompiledEphemerisBlock mars_heliocentric;
    CompiledEphemerisBlock earth_heliocentric;
    CompiledEphemerisBlock earth_barycentric;
    CompiledEphemerisBlock jupiter_heliocentric;
    get_compiled_block_from_storage(&mars_storage, 0, &mars_heliocentric);
    get_compiled_block_from_storage(&earth_helio_storage, 0, &earth_heliocentric);
    get_compiled_block_from_storage(&earth_bary_storage, 0, &earth_barycentric);
    get_compiled_block_from_storage(&jupiter_storage, 0, &jupiter_heliocentric);

    taiyin::CartesianState earth_helio;
    taiyin::CartesianState earth_bary;
    expect_true(eval_compiled_ephemeris_block(jd_tdb, &earth_heliocentric, &earth_helio), "CIRS eval Earth helio");
    expect_true(eval_compiled_ephemeris_block(jd_tdb, &earth_barycentric, &earth_bary), "CIRS eval Earth bary");
    taiyin::Vector3 earth_bary_accel;
    expect_true(eval_compiled_ephemeris_block_acceleration(jd_tdb, &earth_barycentric, &earth_bary_accel),
                "CIRS eval Earth barycentric acceleration");

    taiyin::CartesianState jupiter_helio;
    expect_true(eval_compiled_ephemeris_block(jd_tdb, &jupiter_heliocentric, &jupiter_helio), "CIRS eval Jupiter helio");

    taiyin::pipeline::PipelineContext ctx;
    ctx.step_data_in.resize(10, nullptr);
    ctx.step_data_out.resize(10, nullptr);

    // Light time
    taiyin::pipeline::LightTimeStepData lt_data;
    lt_data.jd_tdb = jd_tdb;
    lt_data.observer_position_au = earth_helio.position_au;
    lt_data.observer_velocity_au_per_day = earth_helio.velocity_au_per_day;
    lt_data.target_block = &mars_heliocentric;
    lt_data.target_position_fn = vector3_position_from_block;
    lt_data.target_velocity_fn = vector3_velocity_from_block;
    lt_data.max_iterations = 16;
    lt_data.tolerance_days = 1e-15;
    ctx.step_data_in[0] = &lt_data;
    ctx.step_data_out[0] = &lt_data;

    // Deflection (Sun)
    taiyin::pipeline::DeflectionStepData def_data;
    def_data.observer_heliocentric_position_au = earth_helio.position_au;
    def_data.observer_heliocentric_velocity_au_per_day = earth_helio.velocity_au_per_day;
    def_data.sun_heliocentric_position_au = {0.0, 0.0, 0.0};
    def_data.sun_heliocentric_velocity_au_per_day = {0.0, 0.0, 0.0};
    def_data.schwarzschild_radius_au = taiyin::TAIYIN_SOLAR_SCHWARZSCHILD_RADIUS_AU;
    def_data.deflection_limit = taiyin::TAIYIN_SOLAR_DEFLECTION_LIMIT;
    ctx.step_data_in[1] = &def_data;
    ctx.step_data_out[1] = &def_data;

    // Deflection (Jupiter)
    taiyin::pipeline::DeflectionStepData jup_def_data;
    jup_def_data.observer_heliocentric_position_au = earth_helio.position_au;
    jup_def_data.observer_heliocentric_velocity_au_per_day = earth_helio.velocity_au_per_day;
    jup_def_data.sun_heliocentric_position_au = jupiter_helio.position_au;
    jup_def_data.sun_heliocentric_velocity_au_per_day = jupiter_helio.velocity_au_per_day;
    jup_def_data.schwarzschild_radius_au = taiyin::TAIYIN_SOLAR_SCHWARZSCHILD_RADIUS_AU * JUPITER_SOLAR_MASS_RATIO;
    jup_def_data.deflection_limit = taiyin::TAIYIN_SOLAR_DEFLECTION_LIMIT;
    ctx.step_data_in[2] = &jup_def_data;
    ctx.step_data_out[2] = &jup_def_data;

    // Aberration
    taiyin::pipeline::AberrationStepData ab_data;
    ab_data.observer_heliocentric_position_au = earth_helio.position_au;
    ab_data.observer_heliocentric_velocity_au_per_day = earth_helio.velocity_au_per_day;
    ab_data.observer_heliocentric_acceleration_au_per_day2 = {0.0, 0.0, 0.0};
    ab_data.observer_barycentric_velocity_au_per_day = earth_bary.velocity_au_per_day;
    ab_data.observer_barycentric_acceleration_au_per_day2 = earth_bary_accel;
    ab_data.light_time_days_per_au = taiyin::TAIYIN_LIGHT_TIME_DAYS_PER_AU;
    ab_data.solar_schwarzschild_radius_au = taiyin::TAIYIN_SOLAR_SCHWARZSCHILD_RADIUS_AU;
    ctx.step_data_in[3] = &ab_data;
    ctx.step_data_out[3] = &ab_data;

    // Frame transform — CIRS route
    taiyin::pipeline::FrameTransformStepData frame_data;
    frame_data.jd_ut1 = jd_ut1;
    frame_data.jd_tt = jd_tt;
    frame_data.frame_route = taiyin::dispatch::FRAME_ROUTE_CIRS;
    frame_data.xp_rad = xp_rad;
    frame_data.yp_rad = yp_rad;
    frame_data.sp_rad = sp_rad;
    frame_data.dx_rad = dx_rad;
    frame_data.dy_rad = dy_rad;
    frame_data.precession_model = taiyin::dispatch::PRECESSION_IAU2006;
    frame_data.nutation_model = taiyin::dispatch::NUTATION_IAU2000A;
    ctx.step_data_in[4] = &frame_data;
    ctx.step_data_out[4] = &frame_data;

    // Observer geocentric — CIRS route
    taiyin::pipeline::ObserverGeocentricStepData obs_data;
    obs_data.jd_ut1 = jd_ut1;
    obs_data.jd_tt = jd_tt;
    obs_data.longitude_rad = longitude_rad;
    obs_data.latitude_rad = latitude_rad;
    obs_data.height_m = height_m;
    obs_data.xp_rad = xp_rad;
    obs_data.yp_rad = yp_rad;
    obs_data.sp_rad = sp_rad;
    obs_data.frame_route = taiyin::dispatch::FRAME_ROUTE_CIRS;
    obs_data.xp_rate_rad_per_day = xp_rate;
    obs_data.yp_rate_rad_per_day = yp_rate;
    obs_data.sp_rate_rad_per_day = sp_rate;
    obs_data.dut1_rate_seconds_per_day = dut1_rate;
    obs_data.lod_seconds = lod_seconds;
    obs_data.derivative_step_days = 1e-3;
    ctx.step_data_in[5] = &obs_data;
    ctx.step_data_out[5] = &obs_data;

    // Topocentric
    taiyin::pipeline::TopocentricStepData topo_data;
    ctx.step_data_in[6] = &topo_data;
    ctx.step_data_out[6] = &topo_data;

    // Diurnal Aberration
    taiyin::pipeline::DiurnalAberrationStepData da_data;
    ctx.step_data_in[7] = &da_data;
    ctx.step_data_out[7] = &da_data;

    // Horizontal — CIRS uses ERA + longitude
    taiyin::pipeline::HorizontalStepData horiz_data;
    const double era = taiyin::earth_rotation_angle_rad(jd_ut1);
    horiz_data.local_sidereal_rad = era + longitude_rad;
    horiz_data.latitude_rad = latitude_rad;
    ctx.step_data_in[8] = &horiz_data;
    ctx.step_data_out[8] = &horiz_data;

    // Refraction
    taiyin::pipeline::RefractionStepData refr_data;
    refr_data.refraction_model = taiyin::dispatch::REFRACTION_BENNETT;
    refr_data.pressure_mbar = 1013.25;
    refr_data.temperature_c = 10.0;
    refr_data.relative_humidity = 0.0;
    refr_data.wavelength_micrometer = 0.0;
    refr_data.max_iterations = 16;
    refr_data.tolerance = 1e-10;
    ctx.step_data_in[9] = &refr_data;
    ctx.step_data_out[9] = &refr_data;

    expect_true(
        taiyin::pipeline::run_pipeline(
            taiyin::pipeline::kCirsPipeline,
            taiyin::pipeline::kCirsPipelineStepCount,
            &ctx) == taiyin::pipeline::OK,
        "CIRS pipeline via registry");

    const double azimuth_deg = refr_data.horizontal.azimuth_rad * kDegPerRadian;
    const double altitude_deg = refr_data.horizontal.altitude_rad * kDegPerRadian;

    // 1. Compare pre-diurnal topocentric coordinates (no diurnal aberration) vs raw Horizons
    auto pre_horiz = topocentric_position_to_horizontal(topo_data.topocentric_position_au, era + longitude_rad, latitude_rad);
    const double pre_azimuth_deg = pre_horiz.azimuth_rad * kDegPerRadian;
    const double pre_altitude_deg = pre_horiz.altitude_rad * kDegPerRadian;
    const double d_az_pre_arcsec = (pre_azimuth_deg - kHorizonsAirlessNoDiurnalAzimuthDeg) * kArcsecPerDegree;
    const double d_alt_pre_arcsec = (pre_altitude_deg - kHorizonsAirlessNoDiurnalAltitudeDeg) * kArcsecPerDegree;
    std::printf("CIRS Mars topocentric (pre-diurnal): az=%.6f deg, alt=%.6f deg, dAz=%+.3f\", dAlt=%+.3f\" vs Horizons (no-diurnal)\n",
                pre_azimuth_deg, pre_altitude_deg, d_az_pre_arcsec, d_alt_pre_arcsec);
    expect_near_arcsec(pre_azimuth_deg, kHorizonsAirlessNoDiurnalAzimuthDeg, kHorizonsToleranceArcsec,
                       "CIRS pre-diurnal azimuth vs Horizons");
    expect_near_arcsec(pre_altitude_deg, kHorizonsAirlessNoDiurnalAltitudeDeg, kHorizonsToleranceArcsec,
                       "CIRS pre-diurnal altitude vs Horizons");

    // 2. Compare post-diurnal/refracted coordinates (with diurnal aberration) vs theoretical base
    const double d_az_post_arcsec = (azimuth_deg - kHorizonsAirlessWithDiurnalAzimuthDeg) * kArcsecPerDegree;
    const double d_alt_post_arcsec = (altitude_deg - kHorizonsAirlessWithDiurnalAltitudeDeg) * kArcsecPerDegree;
    std::printf("CIRS Mars topocentric (post-diurnal): az=%.6f deg, alt=%.6f deg, dAz=%+.3f\", dAlt=%+.3f\" vs target-base (with diurnal)\n",
                azimuth_deg, altitude_deg, d_az_post_arcsec, d_alt_post_arcsec);
    expect_near_arcsec(azimuth_deg, kHorizonsAirlessWithDiurnalAzimuthDeg, kHorizonsToleranceArcsec,
                       "CIRS post-diurnal azimuth vs target-base");
    expect_near_arcsec(altitude_deg, kHorizonsAirlessWithDiurnalAltitudeDeg, kHorizonsToleranceArcsec,
                       "CIRS altitude vs Horizons");

    // Sanity checks
    expect_true(azimuth_deg >= 0.0 && azimuth_deg < 360.0, "CIRS azimuth in range");
    expect_true(altitude_deg > -90.1 && altitude_deg <= 90.1, "CIRS altitude in valid range");

    destroy_storage_ephemeris_block(&mars_storage);
    destroy_storage_ephemeris_block(&earth_helio_storage);
    destroy_storage_ephemeris_block(&earth_bary_storage);
    destroy_storage_ephemeris_block(&jupiter_storage);
}

// Test 3: Dispatch registry basic check
void test_dispatch_registry() {
    // Refraction
    taiyin::dispatch::RefractionDispatchData ref_data;
    ref_data.altitude_rad = 0.5;  // ~28.6 deg
    ref_data.pressure_mbar = 1013.25;
    ref_data.temperature_c = 10.0;
    ref_data.relative_humidity = 0.5;
    ref_data.wavelength_micrometer = 0.55;
    ref_data.max_iterations = 16;
    ref_data.tolerance = 1e-10;

    double bennett = taiyin::dispatch::eval_refraction(taiyin::dispatch::REFRACTION_BENNETT, &ref_data);
    double sofa = taiyin::dispatch::eval_refraction(taiyin::dispatch::REFRACTION_SOFA, &ref_data);
    expect_true(bennett > 0.0, "Bennett refraction positive");
    expect_true(sofa > 0.0, "SOFA refraction positive");
    // Bennett and SOFA should be in same ballpark (< 20% difference for moderate altitudes)
    expect_near(bennett, sofa, sofa * 0.2, "Bennett vs SOFA rough agreement");

    // Precession
    taiyin::Matrix3x3 prec;
    expect_true(taiyin::dispatch::eval_precession(taiyin::dispatch::PRECESSION_VONDRAK2011, 2460310.5, nullptr, &prec),
                "Vondrak2011 precession via dispatch");
    expect_true(taiyin::dispatch::eval_precession(taiyin::dispatch::PRECESSION_IAU2006, 2460310.5, nullptr, &prec),
                "IAU2006 precession via dispatch");

    // Nutation
    taiyin::NutationAngles nut;
    expect_true(taiyin::dispatch::eval_nutation(taiyin::dispatch::NUTATION_IAU2000B, 2460310.5, nullptr, &nut),
                "IAU2000B nutation via dispatch");
    expect_true(taiyin::dispatch::eval_nutation(taiyin::dispatch::NUTATION_IAU2000A, 2460310.5, nullptr, &nut),
                "IAU2000A nutation via dispatch");

    // TDB inverse
    taiyin::dispatch::TdbInverseDispatchData tdb_inverse_data;
    tdb_inverse_data.max_iterations = 8;
    tdb_inverse_data.tolerance_days = 1e-16;
    const double jd_tt = 2460311.417467740830;
    const double jd_tdb = taiyin::tt_to_tdb_jd(jd_tt, taiyin::TdbModel::SofaFull);
    const double inverse_tt = taiyin::dispatch::eval_tdb_inverse(
        taiyin::dispatch::TDB_SOFA_FULL,
        jd_tdb,
        &tdb_inverse_data);
    expect_near(
        inverse_tt,
        taiyin::tdb_to_tt_jd(jd_tdb, taiyin::TdbModel::SofaFull, 8, 1e-16),
        0.0,
        "TDB inverse via dispatch");
    expect_near(inverse_tt, jd_tt, 1e-12, "TDB inverse round trip");

    // Frame routes
    taiyin::dispatch::FrameRouteDispatchData route_data;
    route_data.xp_rad = 0.0;
    route_data.yp_rad = 0.0;
    route_data.sp_rad = 0.0;
    route_data.dx_rad = 0.0;
    route_data.dy_rad = 0.0;
    route_data.precession_model = taiyin::dispatch::PRECESSION_VONDRAK2011;
    route_data.nutation_model = taiyin::dispatch::NUTATION_IAU2000B;

    const double jd_ut1 = 2460310.5 - 69.184 / 86400.0;
    taiyin::Matrix3x3 frame;
    expect_true(taiyin::dispatch::eval_frame_route(taiyin::dispatch::FRAME_ROUTE_EQUINOX, jd_ut1, 2460310.5, &route_data, &frame),
                "Equinox frame route via dispatch");

    route_data.precession_model = taiyin::dispatch::PRECESSION_IAU2006;
    route_data.nutation_model = taiyin::dispatch::NUTATION_IAU2000A;
    expect_true(taiyin::dispatch::eval_frame_route(taiyin::dispatch::FRAME_ROUTE_CIRS, jd_ut1, 2460310.5, &route_data, &frame),
                "CIRS frame route via dispatch");
}

void test_autonomous_mars_apparent() {
    using taiyin::internal::CompiledEphemerisBlock;
    using taiyin::internal::StorageEphemerisBlock;
    using taiyin::internal::compile_spk_ephemeris_block_from_file;
    using taiyin::internal::destroy_storage_ephemeris_block;
    using taiyin::internal::get_compiled_block_from_storage;

    if (!file_exists(kDe441Path)) {
        std::printf("skipping autonomous Mars apparent test; local DE441 is absent\n");
        return;
    }

    const double jd_utc = 2460311.416667;
    const double tai_minus_utc = 37.0;

    taiyin::internal::EarthOrientationTable eop_table;
    expect_true(taiyin::internal::load_builtin_eop_table(&eop_table), "load builtin EOP");
    taiyin::internal::EarthOrientationSample eop_sample;
    expect_true(taiyin::internal::interpolate_earth_orientation(&eop_table, jd_utc, &eop_sample), "interpolate EOP");
    taiyin::internal::EarthOrientationRates eop_rates;
    taiyin::internal::EarthRotationDerivatives eop_derivs;
    expect_true(taiyin::internal::derive_earth_orientation_rates(&eop_table, jd_utc, &eop_rates, &eop_derivs), "derive EOP rates");

    const double dut1_seconds = eop_sample.dut1_seconds;
    const double jd_tt = taiyin::utc_to_tt_jd(jd_utc, tai_minus_utc);
    const double jd_ut1 = taiyin::utc_to_ut1_jd(jd_utc, dut1_seconds);
    const double jd_tdb = taiyin::tt_to_tdb_jd(jd_tt, taiyin::TdbModel::SofaFull);
    const double start = jd_tdb - 2.0;
    const double end = jd_tdb + 2.0;

    const double longitude_rad = 0.0;
    const double latitude_rad = 51.4773207 * 3.14159265358979323846 / 180.0;
    const double height_m = 67.0693;

    const double xp_rad = eop_sample.xp_rad;
    const double yp_rad = eop_sample.yp_rad;
    const double sp_rad = taiyin::internal::sp_rad_for_jd(jd_utc);
    const double dx_rad = eop_sample.dx_rad;
    const double dy_rad = eop_sample.dy_rad;
    const double lod_seconds = eop_sample.lod_seconds;
    const double xp_rate = eop_rates.xp_rate_rad_per_day;
    const double yp_rate = eop_rates.yp_rate_rad_per_day;
    const double sp_rate = eop_rates.sp_rate_rad_per_day;
    const double dut1_rate = eop_derivs.dut1_rate_seconds_per_day;

    taiyin::internal::destroy_earth_orientation_table(&eop_table);

    StorageEphemerisBlock mars_storage;
    StorageEphemerisBlock earth_helio_storage;
    StorageEphemerisBlock earth_bary_storage;
    StorageEphemerisBlock jupiter_storage;
    expect_true(compile_spk_ephemeris_block_from_file(kDe441Path, 4, 10, start, end, &mars_storage), "compile Mars helio");
    expect_true(compile_spk_ephemeris_block_from_file(kDe441Path, 399, 10, start, end, &earth_helio_storage), "compile Earth helio");
    expect_true(compile_spk_ephemeris_block_from_file(kDe441Path, 399, 0, start, end, &earth_bary_storage), "compile Earth bary");
    expect_true(compile_spk_ephemeris_block_from_file(kDe441Path, 5, 10, start, end, &jupiter_storage), "compile Jupiter helio");

    CompiledEphemerisBlock mars_heliocentric;
    CompiledEphemerisBlock earth_heliocentric;
    CompiledEphemerisBlock earth_barycentric;
    CompiledEphemerisBlock jupiter_heliocentric;
    get_compiled_block_from_storage(&mars_storage, 0, &mars_heliocentric);
    get_compiled_block_from_storage(&earth_helio_storage, 0, &earth_heliocentric);
    get_compiled_block_from_storage(&earth_bary_storage, 0, &earth_barycentric);
    get_compiled_block_from_storage(&jupiter_storage, 0, &jupiter_heliocentric);

    // Set up pipeline context
    taiyin::pipeline::PipelineContext ctx;
    ctx.step_data_in.resize(13, nullptr);
    ctx.step_data_out.resize(13, nullptr);

    // Step 0: Earth Heliocentric state evaluation
    taiyin::pipeline::EvalBodyStepData earth_helio_step;
    earth_helio_step.body_id = 399;
    earth_helio_step.jd_tdb = jd_tdb;
    earth_helio_step.target_block = &earth_heliocentric;
    earth_helio_step.target_position_fn = nullptr;
    earth_helio_step.target_velocity_fn = nullptr;
    earth_helio_step.target_acceleration_fn = nullptr;
    ctx.step_data_in[0] = &earth_helio_step;
    ctx.step_data_out[0] = &earth_helio_step;

    // Step 1: Jupiter Heliocentric state evaluation (for deflection source)
    taiyin::pipeline::EvalBodyStepData jup_helio_step;
    jup_helio_step.body_id = 5;
    jup_helio_step.jd_tdb = jd_tdb;
    jup_helio_step.target_block = &jupiter_heliocentric;
    jup_helio_step.target_position_fn = nullptr;
    jup_helio_step.target_velocity_fn = nullptr;
    jup_helio_step.target_acceleration_fn = nullptr;
    ctx.step_data_in[1] = &jup_helio_step;
    ctx.step_data_out[1] = &jup_helio_step;

    // Step 2: Earth Barycentric state evaluation (for aberration)
    taiyin::pipeline::EvalBodyStepData earth_bary_step;
    earth_bary_step.body_id = 3990;
    earth_bary_step.jd_tdb = jd_tdb;
    earth_bary_step.target_block = &earth_barycentric;
    earth_bary_step.target_position_fn = nullptr;
    earth_bary_step.target_velocity_fn = nullptr;
    earth_bary_step.target_acceleration_fn = nullptr;
    ctx.step_data_in[2] = &earth_bary_step;
    ctx.step_data_out[2] = &earth_bary_step;

    // Step 3: Light time
    taiyin::pipeline::LightTimeStepData lt_data;
    lt_data.jd_tdb = jd_tdb;
    // Note: observer_position_au and observer_velocity_au_per_day will be wired from Step 0!
    lt_data.target_block = &mars_heliocentric;
    lt_data.target_position_fn = vector3_position_from_block;
    lt_data.target_velocity_fn = vector3_velocity_from_block;
    lt_data.max_iterations = 16;
    lt_data.tolerance_days = 1e-15;
    ctx.step_data_in[3] = &lt_data;
    ctx.step_data_out[3] = &lt_data;

    // Step 4: Deflection (Sun)
    taiyin::pipeline::DeflectionStepData def_data;
    // Note: observer_heliocentric_position_au and observer_heliocentric_velocity_au_per_day will be wired from Step 0!
    def_data.sun_heliocentric_position_au = {0.0, 0.0, 0.0};
    def_data.sun_heliocentric_velocity_au_per_day = {0.0, 0.0, 0.0};
    def_data.schwarzschild_radius_au = taiyin::TAIYIN_SOLAR_SCHWARZSCHILD_RADIUS_AU;
    def_data.deflection_limit = taiyin::TAIYIN_SOLAR_DEFLECTION_LIMIT;
    ctx.step_data_in[4] = &def_data;
    ctx.step_data_out[4] = &def_data;

    // Step 5: Deflection (Jupiter)
    taiyin::pipeline::DeflectionStepData jup_def_data;
    // Note: observer_heliocentric_position_au and observer_heliocentric_velocity_au_per_day will be wired from Step 0!
    // Note: sun_heliocentric_position_au and sun_heliocentric_velocity_au_per_day will be wired from Step 1!
    jup_def_data.schwarzschild_radius_au = taiyin::TAIYIN_SOLAR_SCHWARZSCHILD_RADIUS_AU * JUPITER_SOLAR_MASS_RATIO;
    jup_def_data.deflection_limit = taiyin::TAIYIN_SOLAR_DEFLECTION_LIMIT;
    ctx.step_data_in[5] = &jup_def_data;
    ctx.step_data_out[5] = &jup_def_data;

    // Step 6: Aberration
    taiyin::pipeline::AberrationStepData ab_data;
    // Note: observer_heliocentric_position_au, observer_heliocentric_velocity_au_per_day will be wired from Step 0!
    // Note: observer_barycentric_velocity_au_per_day, observer_barycentric_acceleration_au_per_day2 will be wired from Step 2!
    ab_data.observer_heliocentric_acceleration_au_per_day2 = {0.0, 0.0, 0.0};
    ab_data.light_time_days_per_au = taiyin::TAIYIN_LIGHT_TIME_DAYS_PER_AU;
    ab_data.solar_schwarzschild_radius_au = taiyin::TAIYIN_SOLAR_SCHWARZSCHILD_RADIUS_AU;
    ctx.step_data_in[6] = &ab_data;
    ctx.step_data_out[6] = &ab_data;

    // Step 7: Frame transform
    taiyin::pipeline::FrameTransformStepData frame_data;
    frame_data.jd_ut1 = jd_ut1;
    frame_data.jd_tt = jd_tt;
    frame_data.frame_route = taiyin::dispatch::FRAME_ROUTE_EQUINOX;
    frame_data.xp_rad = xp_rad;
    frame_data.yp_rad = yp_rad;
    frame_data.sp_rad = sp_rad;
    frame_data.dx_rad = dx_rad;
    frame_data.dy_rad = dy_rad;
    frame_data.precession_model = taiyin::dispatch::PRECESSION_IAU2006;
    frame_data.nutation_model = taiyin::dispatch::NUTATION_IAU2000A;
    ctx.step_data_in[7] = &frame_data;
    ctx.step_data_out[7] = &frame_data;

    // Step 8: Observer geocentric
    taiyin::pipeline::ObserverGeocentricStepData obs_data;
    obs_data.jd_ut1 = jd_ut1;
    obs_data.jd_tt = jd_tt;
    obs_data.longitude_rad = longitude_rad;
    obs_data.latitude_rad = latitude_rad;
    obs_data.height_m = height_m;
    obs_data.xp_rad = xp_rad;
    obs_data.yp_rad = yp_rad;
    obs_data.sp_rad = sp_rad;
    obs_data.frame_route = taiyin::dispatch::FRAME_ROUTE_EQUINOX;
    obs_data.xp_rate_rad_per_day = xp_rate;
    obs_data.yp_rate_rad_per_day = yp_rate;
    obs_data.sp_rate_rad_per_day = sp_rate;
    obs_data.dut1_rate_seconds_per_day = dut1_rate;
    obs_data.lod_seconds = lod_seconds;
    obs_data.derivative_step_days = 1e-3;
    ctx.step_data_in[8] = &obs_data;
    ctx.step_data_out[8] = &obs_data;

    // Step 9: Topocentric
    taiyin::pipeline::TopocentricStepData topo_data;
    ctx.step_data_in[9] = &topo_data;
    ctx.step_data_out[9] = &topo_data;

    // Step 10: Diurnal Aberration
    taiyin::pipeline::DiurnalAberrationStepData da_data;
    ctx.step_data_in[10] = &da_data;
    ctx.step_data_out[10] = &da_data;

    // Step 11: Horizontal
    taiyin::pipeline::HorizontalStepData horiz_data;
    const double gast_val = taiyin::gast_iau2000a_rad(jd_ut1, jd_tt);
    horiz_data.local_sidereal_rad = gast_val + longitude_rad;
    horiz_data.latitude_rad = latitude_rad;
    ctx.step_data_in[11] = &horiz_data;
    ctx.step_data_out[11] = &horiz_data;

    // Step 12: Refraction
    taiyin::pipeline::RefractionStepData refr_data;
    refr_data.refraction_model = taiyin::dispatch::REFRACTION_BENNETT;
    refr_data.pressure_mbar = 1013.25;
    refr_data.temperature_c = 10.0;
    refr_data.relative_humidity = 0.0;
    refr_data.wavelength_micrometer = 0.0;
    refr_data.max_iterations = 16;
    refr_data.tolerance = 1e-10;
    ctx.step_data_in[12] = &refr_data;
    ctx.step_data_out[12] = &refr_data;

    const int steps[] = {
        taiyin::pipeline::STEP_EVAL_BODY,
        taiyin::pipeline::STEP_EVAL_BODY,
        taiyin::pipeline::STEP_EVAL_BODY,
        taiyin::pipeline::STEP_LIGHT_TIME,
        taiyin::pipeline::STEP_DEFLECTION,
        taiyin::pipeline::STEP_DEFLECTION,
        taiyin::pipeline::STEP_ABERRATION,
        taiyin::pipeline::STEP_FRAME_TRANSFORM,
        taiyin::pipeline::STEP_OBSERVER_GEOCENTRIC,
        taiyin::pipeline::STEP_TOPOCENTRIC,
        taiyin::pipeline::STEP_DIURNAL_ABERRATION,
        taiyin::pipeline::STEP_HORIZONTAL,
        taiyin::pipeline::STEP_REFRACTION
    };

    expect_true(
        taiyin::pipeline::run_pipeline(steps, 13, &ctx) == taiyin::pipeline::OK,
        "autonomous Mars apparent pipeline run"
    );

    const double azimuth_deg = refr_data.horizontal.azimuth_rad * kDegPerRadian;
    const double altitude_deg = refr_data.horizontal.altitude_rad * kDegPerRadian;

    std::printf("autonomous Mars topocentric: az=%.6f deg, alt=%.6f deg\n", azimuth_deg, altitude_deg);
    expect_near_arcsec(azimuth_deg, kHorizonsAirlessWithDiurnalAzimuthDeg, kHorizonsToleranceArcsec,
                       "autonomous Mars apparent azimuth vs Horizons");
    expect_near_arcsec(altitude_deg, kHorizonsAirlessWithDiurnalAltitudeDeg, kHorizonsToleranceArcsec,
                       "autonomous Mars apparent altitude vs Horizons");

    destroy_storage_ephemeris_block(&mars_storage);
    destroy_storage_ephemeris_block(&earth_helio_storage);
    destroy_storage_ephemeris_block(&earth_bary_storage);
    destroy_storage_ephemeris_block(&jupiter_storage);
}

struct ApparentPipelineResult {
    double azimuth_deg;
    double altitude_deg;
    size_t cache_entries;
    size_t cache_total_bytes;
    bool contains_mars_bucket;
    bool contains_mars_source_route;

    ApparentPipelineResult()
        : azimuth_deg(0.0),
          altitude_deg(0.0),
          cache_entries(0),
          cache_total_bytes(0),
          contains_mars_bucket(false),
          contains_mars_source_route(false) {}
};

bool eval_catalog_cache_mars_apparent(
    const taiyin::internal::EphemerisBlockCatalog& catalog,
    double jd_utc,
    taiyin::internal::EphemerisBlockCache* cache,
    ApparentPipelineResult* out
) {
    if (!cache || !out) {
        return false;
    }

    const double tai_minus_utc = 37.0;

    taiyin::internal::EarthOrientationTable eop_table;
    if (!taiyin::internal::load_builtin_eop_table(&eop_table)) {
        return false;
    }
    taiyin::internal::EarthOrientationSample eop_sample;
    if (!taiyin::internal::interpolate_earth_orientation(&eop_table, jd_utc, &eop_sample)) {
        taiyin::internal::destroy_earth_orientation_table(&eop_table);
        return false;
    }
    taiyin::internal::EarthOrientationRates eop_rates;
    taiyin::internal::EarthRotationDerivatives eop_derivs;
    if (!taiyin::internal::derive_earth_orientation_rates(&eop_table, jd_utc, &eop_rates, &eop_derivs)) {
        taiyin::internal::destroy_earth_orientation_table(&eop_table);
        return false;
    }

    const double dut1_seconds = eop_sample.dut1_seconds;
    const double jd_tt = taiyin::utc_to_tt_jd(jd_utc, tai_minus_utc);
    const double jd_ut1 = taiyin::utc_to_ut1_jd(jd_utc, dut1_seconds);
    const double jd_tdb = taiyin::tt_to_tdb_jd(jd_tt, taiyin::TdbModel::SofaFull);
    const double descriptor_start = jd_tdb - 2.0;
    const double descriptor_end = jd_tdb + 2.0;

    const double longitude_rad = 0.0;
    const double latitude_rad = 51.4773207 * 3.14159265358979323846 / 180.0;
    const double height_m = 67.0693;

    const double xp_rad = eop_sample.xp_rad;
    const double yp_rad = eop_sample.yp_rad;
    const double sp_rad = taiyin::internal::sp_rad_for_jd(jd_utc);
    const double dx_rad = eop_sample.dx_rad;
    const double dy_rad = eop_sample.dy_rad;
    const double lod_seconds = eop_sample.lod_seconds;
    const double xp_rate = eop_rates.xp_rate_rad_per_day;
    const double yp_rate = eop_rates.yp_rate_rad_per_day;
    const double sp_rate = eop_rates.sp_rate_rad_per_day;
    const double dut1_rate = eop_derivs.dut1_rate_seconds_per_day;

    taiyin::internal::destroy_earth_orientation_table(&eop_table);

    const taiyin::internal::EphemerisBlockDescriptor* mars_descriptor =
        find_catalog_descriptor(catalog, 4, 10, jd_tdb);
    const taiyin::internal::EphemerisBlockDescriptor* jupiter_descriptor =
        find_catalog_descriptor(catalog, 5, 10, jd_tdb);
    if (!mars_descriptor || !jupiter_descriptor) {
        return false;
    }

    taiyin::internal::EphemerisBlockDescriptor earth_helio_descriptor =
        make_spk_source_descriptor(399, 10, descriptor_start, descriptor_end);
    taiyin::internal::EphemerisBlockDescriptor earth_bary_descriptor =
        make_spk_source_descriptor(399, 0, descriptor_start, descriptor_end);

    DescriptorEvalContext mars_context = { mars_descriptor, cache };
    DescriptorEvalContext earth_helio_context = { &earth_helio_descriptor, cache };
    DescriptorEvalContext earth_bary_context = { &earth_bary_descriptor, cache };
    DescriptorEvalContext jupiter_context = { jupiter_descriptor, cache };

    taiyin::pipeline::PipelineContext ctx;
    ctx.step_data_in.resize(13, nullptr);
    ctx.step_data_out.resize(13, nullptr);

    taiyin::pipeline::EvalBodyStepData earth_helio_step;
    earth_helio_step.body_id = 399;
    earth_helio_step.jd_tdb = jd_tdb;
    earth_helio_step.target_block = &earth_helio_context;
    earth_helio_step.target_position_fn = vector3_position_from_descriptor;
    earth_helio_step.target_velocity_fn = vector3_velocity_from_descriptor;
    earth_helio_step.target_acceleration_fn = nullptr;
    ctx.step_data_in[0] = &earth_helio_step;
    ctx.step_data_out[0] = &earth_helio_step;

    taiyin::pipeline::EvalBodyStepData jup_helio_step;
    jup_helio_step.body_id = 5;
    jup_helio_step.jd_tdb = jd_tdb;
    jup_helio_step.target_block = &jupiter_context;
    jup_helio_step.target_position_fn = vector3_position_from_descriptor;
    jup_helio_step.target_velocity_fn = vector3_velocity_from_descriptor;
    jup_helio_step.target_acceleration_fn = nullptr;
    ctx.step_data_in[1] = &jup_helio_step;
    ctx.step_data_out[1] = &jup_helio_step;

    taiyin::pipeline::EvalBodyStepData earth_bary_step;
    earth_bary_step.body_id = 3990;
    earth_bary_step.jd_tdb = jd_tdb;
    earth_bary_step.target_block = &earth_bary_context;
    earth_bary_step.target_position_fn = vector3_position_from_descriptor;
    earth_bary_step.target_velocity_fn = vector3_velocity_from_descriptor;
    earth_bary_step.target_acceleration_fn = vector3_acceleration_from_descriptor;
    ctx.step_data_in[2] = &earth_bary_step;
    ctx.step_data_out[2] = &earth_bary_step;

    taiyin::pipeline::LightTimeStepData lt_data;
    lt_data.jd_tdb = jd_tdb;
    lt_data.target_block = &mars_context;
    lt_data.target_position_fn = vector3_position_from_descriptor;
    lt_data.target_velocity_fn = vector3_velocity_from_descriptor;
    lt_data.max_iterations = 16;
    lt_data.tolerance_days = 1e-15;
    ctx.step_data_in[3] = &lt_data;
    ctx.step_data_out[3] = &lt_data;

    taiyin::pipeline::DeflectionStepData def_data;
    def_data.sun_heliocentric_position_au = {0.0, 0.0, 0.0};
    def_data.sun_heliocentric_velocity_au_per_day = {0.0, 0.0, 0.0};
    def_data.schwarzschild_radius_au = taiyin::TAIYIN_SOLAR_SCHWARZSCHILD_RADIUS_AU;
    def_data.deflection_limit = taiyin::TAIYIN_SOLAR_DEFLECTION_LIMIT;
    ctx.step_data_in[4] = &def_data;
    ctx.step_data_out[4] = &def_data;

    taiyin::pipeline::DeflectionStepData jup_def_data;
    jup_def_data.schwarzschild_radius_au = taiyin::TAIYIN_SOLAR_SCHWARZSCHILD_RADIUS_AU * JUPITER_SOLAR_MASS_RATIO;
    jup_def_data.deflection_limit = taiyin::TAIYIN_SOLAR_DEFLECTION_LIMIT;
    ctx.step_data_in[5] = &jup_def_data;
    ctx.step_data_out[5] = &jup_def_data;

    taiyin::pipeline::AberrationStepData ab_data;
    ab_data.observer_heliocentric_acceleration_au_per_day2 = {0.0, 0.0, 0.0};
    ab_data.light_time_days_per_au = taiyin::TAIYIN_LIGHT_TIME_DAYS_PER_AU;
    ab_data.solar_schwarzschild_radius_au = taiyin::TAIYIN_SOLAR_SCHWARZSCHILD_RADIUS_AU;
    ctx.step_data_in[6] = &ab_data;
    ctx.step_data_out[6] = &ab_data;

    taiyin::pipeline::FrameTransformStepData frame_data;
    frame_data.jd_ut1 = jd_ut1;
    frame_data.jd_tt = jd_tt;
    frame_data.frame_route = taiyin::dispatch::FRAME_ROUTE_EQUINOX;
    frame_data.xp_rad = xp_rad;
    frame_data.yp_rad = yp_rad;
    frame_data.sp_rad = sp_rad;
    frame_data.dx_rad = dx_rad;
    frame_data.dy_rad = dy_rad;
    frame_data.precession_model = taiyin::dispatch::PRECESSION_IAU2006;
    frame_data.nutation_model = taiyin::dispatch::NUTATION_IAU2000A;
    ctx.step_data_in[7] = &frame_data;
    ctx.step_data_out[7] = &frame_data;

    taiyin::pipeline::ObserverGeocentricStepData obs_data;
    obs_data.jd_ut1 = jd_ut1;
    obs_data.jd_tt = jd_tt;
    obs_data.longitude_rad = longitude_rad;
    obs_data.latitude_rad = latitude_rad;
    obs_data.height_m = height_m;
    obs_data.xp_rad = xp_rad;
    obs_data.yp_rad = yp_rad;
    obs_data.sp_rad = sp_rad;
    obs_data.frame_route = taiyin::dispatch::FRAME_ROUTE_EQUINOX;
    obs_data.xp_rate_rad_per_day = xp_rate;
    obs_data.yp_rate_rad_per_day = yp_rate;
    obs_data.sp_rate_rad_per_day = sp_rate;
    obs_data.dut1_rate_seconds_per_day = dut1_rate;
    obs_data.lod_seconds = lod_seconds;
    obs_data.derivative_step_days = 1e-3;
    ctx.step_data_in[8] = &obs_data;
    ctx.step_data_out[8] = &obs_data;

    taiyin::pipeline::TopocentricStepData topo_data;
    ctx.step_data_in[9] = &topo_data;
    ctx.step_data_out[9] = &topo_data;

    taiyin::pipeline::DiurnalAberrationStepData da_data;
    ctx.step_data_in[10] = &da_data;
    ctx.step_data_out[10] = &da_data;

    taiyin::pipeline::HorizontalStepData horiz_data;
    const double gast_val = taiyin::gast_iau2000a_rad(jd_ut1, jd_tt);
    horiz_data.local_sidereal_rad = gast_val + longitude_rad;
    horiz_data.latitude_rad = latitude_rad;
    ctx.step_data_in[11] = &horiz_data;
    ctx.step_data_out[11] = &horiz_data;

    taiyin::pipeline::RefractionStepData refr_data;
    refr_data.refraction_model = taiyin::dispatch::REFRACTION_BENNETT;
    refr_data.pressure_mbar = 1013.25;
    refr_data.temperature_c = 10.0;
    refr_data.relative_humidity = 0.0;
    refr_data.wavelength_micrometer = 0.0;
    refr_data.max_iterations = 16;
    refr_data.tolerance = 1e-10;
    ctx.step_data_in[12] = &refr_data;
    ctx.step_data_out[12] = &refr_data;

    const int steps[] = {
        taiyin::pipeline::STEP_EVAL_BODY,
        taiyin::pipeline::STEP_EVAL_BODY,
        taiyin::pipeline::STEP_EVAL_BODY,
        taiyin::pipeline::STEP_LIGHT_TIME,
        taiyin::pipeline::STEP_DEFLECTION,
        taiyin::pipeline::STEP_DEFLECTION,
        taiyin::pipeline::STEP_ABERRATION,
        taiyin::pipeline::STEP_FRAME_TRANSFORM,
        taiyin::pipeline::STEP_OBSERVER_GEOCENTRIC,
        taiyin::pipeline::STEP_TOPOCENTRIC,
        taiyin::pipeline::STEP_DIURNAL_ABERRATION,
        taiyin::pipeline::STEP_HORIZONTAL,
        taiyin::pipeline::STEP_REFRACTION
    };

    if (taiyin::pipeline::run_pipeline(steps, 13, &ctx) != taiyin::pipeline::OK) {
        return false;
    }

    taiyin::internal::EphemerisBlockDescriptor mars_bucket;
    if (!taiyin::internal::make_cache_bucket_descriptor_for_jd(*mars_descriptor, jd_tdb, &mars_bucket)) {
        return false;
    }

    out->azimuth_deg = refr_data.horizontal.azimuth_rad * kDegPerRadian;
    out->altitude_deg = refr_data.horizontal.altitude_rad * kDegPerRadian;
    out->cache_entries = cache->entry_count();
    out->cache_total_bytes = cache->total_bytes();
    out->contains_mars_bucket = cache->contains(mars_bucket.route_key);
    out->contains_mars_source_route = cache->contains(mars_descriptor->route_key);
    return true;
}

bool discover_or_skip_apparent_catalog(taiyin::internal::EphemerisBlockCatalog* catalog, const char* label) {
    if (!file_exists(kDe441Path)) {
        std::printf("skipping %s; local DE441 is absent\n", label);
        return false;
    }
    if (!discover_catalog_from_data_root(catalog)) {
        std::printf("skipping %s; local data root is absent\n", label);
        return false;
    }
    return true;
}

void test_catalog_cache_mars_apparent() {
    taiyin::internal::EphemerisBlockCatalog catalog;
    if (!discover_or_skip_apparent_catalog(&catalog, "catalog/cache Mars apparent test")) {
        return;
    }

    taiyin::internal::EphemerisBlockCache cache(32 * 1024 * 1024);
    ApparentPipelineResult result;
    expect_true(
        eval_catalog_cache_mars_apparent(catalog, 2460311.416667, &cache, &result),
        "catalog/cache Mars apparent pipeline run");

    std::printf("catalog/cache Mars topocentric: az=%.6f deg, alt=%.6f deg, cache_entries=%zu\n",
                result.azimuth_deg, result.altitude_deg, result.cache_entries);
    expect_near_arcsec(result.azimuth_deg, kHorizonsAirlessWithDiurnalAzimuthDeg, kHorizonsToleranceArcsec,
                       "catalog/cache Mars apparent azimuth vs Horizons");
    expect_near_arcsec(result.altitude_deg, kHorizonsAirlessWithDiurnalAltitudeDeg, kHorizonsToleranceArcsec,
                       "catalog/cache Mars apparent altitude vs Horizons");
    expect_true(result.contains_mars_bucket, "catalog/cache contains Mars lazy bucket");
    expect_true(!result.contains_mars_source_route, "catalog/cache does not cache source Mars route");
    expect_true(result.cache_entries >= 4, "catalog/cache loaded source descriptors into lazy buckets");
}

void test_catalog_cache_mars_apparent_eviction() {
    taiyin::internal::EphemerisBlockCatalog catalog;
    if (!discover_or_skip_apparent_catalog(&catalog, "catalog/cache Mars apparent eviction test")) {
        return;
    }

    const double jd_utc_cases[] = {
        2460311.416667,
        2460314.416667,
        2460317.416667,
    };

    taiyin::internal::EphemerisBlockCache reference_cache(32 * 1024 * 1024);
    taiyin::internal::EphemerisBlockCache eviction_cache(1);
    for (size_t i = 0; i < sizeof(jd_utc_cases) / sizeof(jd_utc_cases[0]); ++i) {
        ApparentPipelineResult reference;
        ApparentPipelineResult evicted;
        expect_true(
            eval_catalog_cache_mars_apparent(catalog, jd_utc_cases[i], &reference_cache, &reference),
            "reference catalog/cache Mars apparent pipeline run");
        expect_true(
            eval_catalog_cache_mars_apparent(catalog, jd_utc_cases[i], &eviction_cache, &evicted),
            "evicting catalog/cache Mars apparent pipeline run");

        std::printf(
            "catalog/cache eviction Mars[%zu]: az=%.6f deg, alt=%.6f deg, entries=%zu, bytes=%zu\n",
            i,
            evicted.azimuth_deg,
            evicted.altitude_deg,
            evicted.cache_entries,
            evicted.cache_total_bytes);

        expect_near(evicted.azimuth_deg, reference.azimuth_deg, 1e-10,
                    "evicting cache azimuth matches reference cache");
        expect_near(evicted.altitude_deg, reference.altitude_deg, 1e-10,
                    "evicting cache altitude matches reference cache");
        expect_true(evicted.cache_entries == 1, "one-byte cache keeps only one oversized block after eviction");
        expect_true(evicted.cache_total_bytes > eviction_cache.max_bytes(), "one-byte cache exercised oversized insertion path");
        expect_true(evicted.contains_mars_bucket, "evicting cache ends with Mars lazy bucket");
        expect_true(!evicted.contains_mars_source_route, "evicting cache never stores source route");
    }
}

bool eval_catalog_cache_mars_apparent_with_retries(
    const taiyin::internal::EphemerisBlockCatalog& catalog,
    double jd_utc,
    taiyin::internal::EphemerisBlockCache* cache,
    ApparentPipelineResult* out
) {
    const int max_attempts = 32;
    for (int attempt = 0; attempt < max_attempts; ++attempt) {
        if (eval_catalog_cache_mars_apparent(catalog, jd_utc, cache, out)) {
            return true;
        }
        std::this_thread::yield();
    }
    return false;
}

void test_catalog_cache_mars_apparent_concurrent_stress() {
    taiyin::internal::EphemerisBlockCatalog catalog;
    if (!discover_or_skip_apparent_catalog(&catalog, "catalog/cache Mars apparent concurrent stress test")) {
        return;
    }

    const double jd_utc_cases[] = {
        2460311.416667,
        2460314.416667,
        2460317.416667,
    };
    const size_t case_count = sizeof(jd_utc_cases) / sizeof(jd_utc_cases[0]);

    taiyin::internal::EphemerisBlockCache reference_cache(32 * 1024 * 1024);
    ApparentPipelineResult reference[case_count];
    for (size_t i = 0; i < case_count; ++i) {
        expect_true(
            eval_catalog_cache_mars_apparent(catalog, jd_utc_cases[i], &reference_cache, &reference[i]),
            "concurrent stress reference apparent pipeline run");
    }

    taiyin::internal::EphemerisBlockCache shared_cache(192 * 1024);
    const int thread_count = 4;
    const int iterations_per_thread = 4;
    std::vector<int> thread_failures(static_cast<size_t>(thread_count), 0);
    std::vector<std::thread> threads;
    threads.reserve(static_cast<size_t>(thread_count));

    for (int thread_index = 0; thread_index < thread_count; ++thread_index) {
        threads.push_back(std::thread([&, thread_index]() {
            int local_failures = 0;
            for (int iteration = 0; iteration < iterations_per_thread; ++iteration) {
                const size_t case_index = static_cast<size_t>((thread_index + iteration) % static_cast<int>(case_count));
                ApparentPipelineResult actual;
                if (!eval_catalog_cache_mars_apparent_with_retries(
                        catalog,
                        jd_utc_cases[case_index],
                        &shared_cache,
                        &actual)) {
                    ++local_failures;
                    continue;
                }
                if (std::fabs(actual.azimuth_deg - reference[case_index].azimuth_deg) > 1e-10
                    || std::fabs(actual.altitude_deg - reference[case_index].altitude_deg) > 1e-10
                    || actual.contains_mars_source_route) {
                    ++local_failures;
                }
            }
            thread_failures[static_cast<size_t>(thread_index)] = local_failures;
        }));
    }

    for (size_t i = 0; i < threads.size(); ++i) {
        threads[i].join();
    }

    int total_thread_failures = 0;
    for (size_t i = 0; i < thread_failures.size(); ++i) {
        total_thread_failures += thread_failures[i];
    }

    std::printf(
        "catalog/cache concurrent stress: threads=%d iterations=%d entries=%zu bytes=%zu failures=%d\n",
        thread_count,
        iterations_per_thread,
        shared_cache.entry_count(),
        shared_cache.total_bytes(),
        total_thread_failures);
    expect_true(total_thread_failures == 0, "catalog/cache concurrent apparent stress completed without mismatches");
    expect_true(shared_cache.entry_count() > 0, "concurrent stress leaves cache populated");
    expect_true(shared_cache.total_bytes() <= shared_cache.max_bytes(), "concurrent stress cache respects non-oversized budget");
}

}  // namespace

int main() {
    test_dispatch_registry();
    test_equinox_mars_topocentric();
    test_cirs_mars_topocentric();
    test_autonomous_mars_apparent();
    test_catalog_cache_mars_apparent();
    test_catalog_cache_mars_apparent_eviction();
    test_catalog_cache_mars_apparent_concurrent_stress();
    return failures == 0 ? 0 : 1;
}
