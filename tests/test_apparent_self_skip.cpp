#include "taiyin/apparent_position.h"
#include "taiyin/corrections.h"
#include "taiyin/dispatch.h"
#include "taiyin/internal/ephemeris_block.h"
#include "taiyin/internal/spk.h"
#include "test_env.h"

#include <cstdio>
#include <cmath>

using namespace taiyin;
using namespace taiyin::internal;

const char* DE441_PATH = taiyin_test::getenv_path("TAIYIN_DE441_PATH");

bool expect_true(bool condition, const char* label) {
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", label);
        return false;
    }
    std::printf("PASS: %s\n", label);
    return true;
}

void test_self_deflection_skip() {
    if (!DE441_PATH) {
        std::fprintf(stderr, "skip test: set TAIYIN_DE441_PATH to run apparent self-skip test\n");
        return;
    }

    const double jd0 = 2451545.0 - 100.0;
    const double jd1 = 2451545.0 + 100.0;
    const double jd_tdb = 2451545.0;
    const double jd_tt = 2451545.0;

    StorageEphemerisBlock sun_storage, jupiter_storage, earth_storage;
    if (!compile_spk_ephemeris_block_from_file(DE441_PATH, 10, 0, jd0, jd1, &sun_storage)
        || !compile_spk_ephemeris_block_from_file(DE441_PATH, 5, 0, jd0, jd1, &jupiter_storage)
        || !compile_spk_ephemeris_block_from_file(DE441_PATH, 399, 0, jd0, jd1, &earth_storage)) {
        std::fprintf(stderr, "skip test: DE441 not available\n");
        return;
    }

    CompiledEphemerisBlock sun_block, jupiter_block, earth_block;
    get_compiled_block_from_storage(&sun_storage, 0, &sun_block);
    get_compiled_block_from_storage(&jupiter_storage, 0, &jupiter_block);
    get_compiled_block_from_storage(&earth_storage, 0, &earth_block);

    const CompiledEphemerisBlock* deflectors[] = { &sun_block, &jupiter_block };
    const int deflector_ids[] = { 10, 5 };
    const double schwarzschild[] = {
        TAIYIN_SOLAR_SCHWARZSCHILD_RADIUS_AU,
        TAIYIN_SOLAR_SCHWARZSCHILD_RADIUS_AU * 0.0009547919,
    };
    const double deflection_limit[] = {
        TAIYIN_SOLAR_DEFLECTION_LIMIT,
        TAIYIN_SOLAR_DEFLECTION_LIMIT,
    };

    const uint32_t flags = TAIYIN_APPARENT_LIGHT_TIME
        | TAIYIN_APPARENT_SHAPIRO_DELAY
        | TAIYIN_APPARENT_DEFLECTION
        | TAIYIN_APPARENT_ABERRATION
        | TAIYIN_APPARENT_SPHERICAL;

    // Test 1: Sun as target with Sun+Jupiter as deflectors
    // Sun should skip itself for deflection and Shapiro
    // Note: For Sun, we need to disable aberration since it requires Sun as solar deflector
    const uint32_t sun_flags = TAIYIN_APPARENT_LIGHT_TIME
        | TAIYIN_APPARENT_DEFLECTION
        | TAIYIN_APPARENT_SPHERICAL;

    double sun_lon_with_self = 0.0, sun_lat_with_self = 0.0, sun_dist_with_self = 0.0;
    bool sun_result = taiyin_calc_apparent_batch_flat(
        jd_tdb,
        jd_tt,
        1,
        &deflector_ids[0],  // target_ids: Sun (10)
        &deflectors[0],     // target_blocks: &sun_block
        399,                // observer_id: Earth
        &earth_block,
        0, 0, 0,
        2,
        0,
        deflector_ids,
        deflectors,
        schwarzschild,
        deflection_limit,
        sun_flags,
        TAIYIN_APPARENT_FRAME_TRUE_ECLIPTIC_OF_DATE,
        0, 0, 0, 0,
        dispatch::PRECESSION_IAU2006,
        dispatch::NUTATION_IAU2000B,
        0,
        8, 1e-13, 1e-3,
        0, 0, 0,
        &sun_lon_with_self,
        &sun_lat_with_self,
        &sun_dist_with_self,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0);

    expect_true(sun_result, "Sun apparent with self in deflectors succeeds");
    expect_true(std::isfinite(sun_lon_with_self) && std::isfinite(sun_lat_with_self), "Sun result is finite");

    // Test 2: Jupiter as target with Sun+Jupiter as deflectors
    // Jupiter should skip itself for deflection and Shapiro
    double jupiter_lon_with_self = 0.0, jupiter_lat_with_self = 0.0, jupiter_dist_with_self = 0.0;
    bool jupiter_result = taiyin_calc_apparent_batch_flat(
        jd_tdb,
        jd_tt,
        1,
        &deflector_ids[1],  // target_ids: Jupiter (5)
        &deflectors[1],     // target_blocks: &jupiter_block
        399,                // observer_id: Earth
        &earth_block,
        0, 0, 0,
        2,
        0,
        deflector_ids,
        deflectors,
        schwarzschild,
        deflection_limit,
        flags,
        TAIYIN_APPARENT_FRAME_TRUE_ECLIPTIC_OF_DATE,
        0, 0, 0, 0,
        dispatch::PRECESSION_IAU2006,
        dispatch::NUTATION_IAU2000B,
        0,
        8, 1e-13, 1e-3,
        0, 0, 0,
        &jupiter_lon_with_self,
        &jupiter_lat_with_self,
        &jupiter_dist_with_self,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0);

    expect_true(jupiter_result, "Jupiter apparent with self in deflectors succeeds");
    expect_true(std::isfinite(jupiter_lon_with_self) && std::isfinite(jupiter_lat_with_self), "Jupiter result is finite");

    // Test 3: Compute Sun with only Jupiter as deflector (no self)
    const CompiledEphemerisBlock* deflectors_no_sun[] = { &jupiter_block };
    const int deflector_ids_no_sun[] = { 5 };
    double sun_lon_no_self = 0.0, sun_lat_no_self = 0.0, sun_dist_no_self = 0.0;
    bool sun_no_self_result = taiyin_calc_apparent_batch_flat(
        jd_tdb,
        jd_tt,
        1,
        &deflector_ids[0],
        &deflectors[0],
        399,
        &earth_block,
        0, 0, 0,
        1,
        0,
        deflector_ids_no_sun,
        deflectors_no_sun,
        &schwarzschild[1],
        &deflection_limit[1],
        sun_flags,
        TAIYIN_APPARENT_FRAME_TRUE_ECLIPTIC_OF_DATE,
        0, 0, 0, 0,
        dispatch::PRECESSION_IAU2006,
        dispatch::NUTATION_IAU2000B,
        0,
        8, 1e-13, 1e-3,
        0, 0, 0,
        &sun_lon_no_self,
        &sun_lat_no_self,
        &sun_dist_no_self,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0);

    expect_true(sun_no_self_result, "Sun apparent without self in deflectors succeeds");

    // The results should be identical because self-deflection was skipped
    double dlon = std::abs(sun_lon_with_self - sun_lon_no_self) * 206264.806;  // to arcsec
    double dlat = std::abs(sun_lat_with_self - sun_lat_no_self) * 206264.806;
    std::printf("Sun with_self vs no_self: dlon=%.9f arcsec dlat=%.9f arcsec\n", dlon, dlat);
    expect_true(dlon < 1e-9 && dlat < 1e-9, "Sun self-skip makes results identical");

    // Test 4: Verify Jupiter self-skip
    const CompiledEphemerisBlock* deflectors_no_jupiter[] = { &sun_block };
    const int deflector_ids_no_jupiter[] = { 10 };
    double jupiter_lon_no_self = 0.0, jupiter_lat_no_self = 0.0, jupiter_dist_no_self = 0.0;
    bool jupiter_no_self_result = taiyin_calc_apparent_batch_flat(
        jd_tdb,
        jd_tt,
        1,
        &deflector_ids[1],
        &deflectors[1],
        399,
        &earth_block,
        0, 0, 0,
        1,
        0,
        deflector_ids_no_jupiter,
        deflectors_no_jupiter,
        &schwarzschild[0],
        &deflection_limit[0],
        flags,
        TAIYIN_APPARENT_FRAME_TRUE_ECLIPTIC_OF_DATE,
        0, 0, 0, 0,
        dispatch::PRECESSION_IAU2006,
        dispatch::NUTATION_IAU2000B,
        0,
        8, 1e-13, 1e-3,
        0, 0, 0,
        &jupiter_lon_no_self,
        &jupiter_lat_no_self,
        &jupiter_dist_no_self,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0);

    expect_true(jupiter_no_self_result, "Jupiter apparent without self in deflectors succeeds");

    double jup_dlon = std::abs(jupiter_lon_with_self - jupiter_lon_no_self) * 206264.806;
    double jup_dlat = std::abs(jupiter_lat_with_self - jupiter_lat_no_self) * 206264.806;
    std::printf("Jupiter with_self vs no_self: dlon=%.9f arcsec dlat=%.9f arcsec\n", jup_dlon, jup_dlat);
    expect_true(jup_dlon < 1e-9 && jup_dlat < 1e-9, "Jupiter self-skip makes results identical");

    destroy_storage_ephemeris_block(&earth_storage);
    destroy_storage_ephemeris_block(&jupiter_storage);
    destroy_storage_ephemeris_block(&sun_storage);
}

int main() {
    test_self_deflection_skip();
    return 0;
}
