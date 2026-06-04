#include "taiyin/angle.h"
#include "taiyin/apparent_position.h"
#include "taiyin/corrections.h"
#include "taiyin/dispatch.h"
#include "taiyin/geometry.h"
#include "taiyin/internal/ephemeris_block.h"
#include "taiyin/internal/spk.h"
#include "taiyin/time.h"
#include "taiyin/vector3.h"
#include "test_env.h"

#include <cassert>
#include <cmath>
#include <cstdio>
#include <fstream>

namespace {

const char* kDe441Path = taiyin_test::getenv_path("TAIYIN_DE441_PATH");
const double kSecondsPerDay = taiyin::SECONDS_PER_DAY;
const double kDegPerRadian = taiyin::TAIYIN_RAD_TO_DEG;
const double kArcsecPerDegree = 3600.0;

bool file_exists(const char* path) {
    if (!path) {
        return false;
    }
    std::ifstream file(path, std::ios::binary);
    return static_cast<bool>(file);
}

double tt_to_tdb_jd_fast(double jd_tt) {
    const double t = (jd_tt - taiyin::JD_J2000) / taiyin::DAYS_PER_JULIAN_CENTURY;
    const double tdb_minus_tt_seconds =
        0.001657 * std::sin(628.3076 * t + 6.2401)
        + 0.000022 * std::sin(575.3385 * t + 4.2970)
        + 0.000014 * std::sin(1256.6152 * t + 6.1969)
        + 0.000005 * std::sin(606.9777 * t + 4.0212)
        + 0.000005 * std::sin(52.9691 * t + 0.4444)
        + 0.000002 * std::sin(21.3299 * t + 5.5431)
        + 0.000010 * t * std::sin(628.3076 * t + 4.2490);
    return jd_tt + tdb_minus_tt_seconds / kSecondsPerDay;
}

double signed_degree_difference(double actual, double expected) {
    double diff = std::fmod(actual - expected + 180.0, 360.0);
    if (diff < 0.0) {
        diff += 360.0;
    }
    return diff - 180.0;
}

void expect_true(bool value, const char* label) {
    if (!value) {
        std::fprintf(stderr, "expected true: %s\n", label);
        assert(false);
    }
}

void expect_near(double actual, double expected, double tolerance, const char* label) {
    if (!(std::fabs(actual - expected) <= tolerance)) {
        std::fprintf(
            stderr,
            "%s mismatch: actual %.17g expected %.17g diff %.17g tolerance %.17g\n",
            label,
            actual,
            expected,
            std::fabs(actual - expected),
            tolerance);
        assert(false);
    }
}

void expect_array3_near(const double actual[3], const double expected[3], double tolerance, const char* label) {
    for (int i = 0; i < 3; ++i) {
        if (!(std::fabs(actual[i] - expected[i]) <= tolerance)) {
            std::fprintf(
                stderr,
                "%s[%d] mismatch: actual %.17g expected %.17g diff %.17g tolerance %.17g\n",
                label,
                i,
                actual[i],
                expected[i],
                std::fabs(actual[i] - expected[i]),
                tolerance);
            assert(false);
        }
    }
}

void expect_angle_arcsec_less(double actual_deg, double expected_deg, double limit_arcsec, const char* label) {
    const double diff_arcsec = signed_degree_difference(actual_deg, expected_deg) * kArcsecPerDegree;
    if (!(std::fabs(diff_arcsec) < limit_arcsec)) {
        std::fprintf(
            stderr,
            "%s too large: actual %.17g deg expected %.17g deg diff %.17g arcsec limit %.17g arcsec\n",
            label,
            actual_deg,
            expected_deg,
            diff_arcsec,
            limit_arcsec);
        assert(false);
    }
}

bool equatorial_ra_dec_degrees(const taiyin::Vector3& position, double* out_ra_deg, double* out_dec_deg) {
    if (!out_ra_deg || !out_dec_deg) {
        return false;
    }
    taiyin::EquatorialPositionVelocityAcceleration equatorial;
    const taiyin::Vector3 zero = { 0.0, 0.0, 0.0 };
    if (!taiyin::cartesian_position_velocity_acceleration_to_equatorial(position, zero, zero, &equatorial)) {
        return false;
    }
    *out_ra_deg = taiyin::normalize_degrees(equatorial.right_ascension_rad * kDegPerRadian);
    *out_dec_deg = equatorial.declination_rad * kDegPerRadian;
    return true;
}

void vector_to_array(const taiyin::Vector3& vector, double out[3]) {
    out[0] = vector.x;
    out[1] = vector.y;
    out[2] = vector.z;
}

taiyin::Vector3 array_to_vector(const double values[3]) {
    return taiyin::Vector3{ values[0], values[1], values[2] };
}

void check_de441_mars_horizons_astrometric_oracle() {
    using taiyin::CartesianState;
    using taiyin::internal::CompiledEphemerisBlock;
    using taiyin::internal::StorageEphemerisBlock;
    using taiyin::internal::compile_spk_ephemeris_block_from_file;
    using taiyin::internal::destroy_storage_ephemeris_block;
    using taiyin::internal::eval_compiled_ephemeris_block;
    using taiyin::internal::get_compiled_block_from_storage;

    if (!file_exists(kDe441Path)) {
        std::printf("skipping Mars Horizons astrometric flat apparent oracle; local DE441 is absent\n");
        return;
    }

    // NASA/Horizons fixture definition:
    // - target: Mars barycenter (NAIF 4)
    // - observer: geocenter, represented here by DE441 Earth center (NAIF 399)
    // - ephemeris source: local NASA DE441 BSP
    // - epoch: 2024-01-01T00:00:00Z, jd_tt below from the legacy Horizons oracle route
    // - correction layer: astrometric only, i.e. one-way light-time; no annual aberration,
    //   no gravitational deflection, and no Shapiro delay in this flat API test.
    // - output frame for this oracle: ICRF equatorial RA/Dec.
    const double jd_tt = 2460310.50080074091;
    const double jd_tdb = tt_to_tdb_jd_fast(jd_tt);
    const double start = jd_tdb - 2.0;
    const double end = jd_tdb + 2.0;
    const double nasa_astrometric_ra_deg = 266.695537368;
    const double nasa_astrometric_dec_deg = -23.952038314;

    StorageEphemerisBlock mars_storage;
    StorageEphemerisBlock earth_storage;
    expect_true(
        compile_spk_ephemeris_block_from_file(kDe441Path, 4, 10, start, end, &mars_storage),
        "compile Mars heliocentric DE441 block for flat apparent oracle");
    expect_true(
        compile_spk_ephemeris_block_from_file(kDe441Path, 399, 10, start, end, &earth_storage),
        "compile Earth heliocentric DE441 block for flat apparent oracle");

    CompiledEphemerisBlock mars_block;
    CompiledEphemerisBlock earth_block;
    expect_true(get_compiled_block_from_storage(&mars_storage, 0, &mars_block), "get Mars compiled block");
    expect_true(get_compiled_block_from_storage(&earth_storage, 0, &earth_block), "get Earth compiled block");

    CartesianState mars_geometric_state;
    CartesianState earth_geometric_state;
    expect_true(eval_compiled_ephemeris_block(jd_tdb, &mars_block, &mars_geometric_state), "eval Mars geometric state");
    expect_true(eval_compiled_ephemeris_block(jd_tdb, &earth_block, &earth_geometric_state), "eval Earth geometric state");
    const taiyin::Vector3 expected_geometric = taiyin::vector3_subtract(
        mars_geometric_state.position_au,
        earth_geometric_state.position_au);
    double expected_geometric_array[3];
    vector_to_array(expected_geometric, expected_geometric_array);

    double geometric_pos[3];
    double astrometric_pos[3];
    double apparent_pos[3];
    double light_time_days = 0.0;
    int light_time_iterations = 0;
    expect_true(
        taiyin::taiyin_calc_apparent_with_matrix_flat(
            jd_tdb,
            4,
            &mars_block,
            399,
            &earth_block,
            0,
            0,
            0,
            0,
            -1,
            0,
            0,
            0,
            0,
            taiyin::TAIYIN_APPARENT_LIGHT_TIME,
            0,
            0,
            0,
            0,
            8,
            1.0e-14,
            0,
            0,
            0,
            geometric_pos,
            0,
            0,
            astrometric_pos,
            0,
            0,
            0,
            0,
            0,
            0,
            0,
            0,
            apparent_pos,
            0,
            0,
            0,
            0,
            0,
            0,
            0,
            0,
            0,
            0,
            0,
            &light_time_days,
            0,
            0,
            &light_time_iterations),
        "flat apparent Mars astrometric ICRF succeeds");

    expect_array3_near(geometric_pos, expected_geometric_array, 1.0e-14, "flat geometric Mars-Earth vector");
    expect_array3_near(apparent_pos, astrometric_pos, 1.0e-15, "flat apparent equals astrometric before corrections");
    expect_true(light_time_days > 0.0, "flat Mars light-time positive");
    expect_true(light_time_iterations == -1, "flat Mars light-time iteration count reserved");

    double astrometric_ra_deg = 0.0;
    double astrometric_dec_deg = 0.0;
    expect_true(
        equatorial_ra_dec_degrees(array_to_vector(astrometric_pos), &astrometric_ra_deg, &astrometric_dec_deg),
        "convert flat Mars astrometric vector to RA/Dec");
    expect_angle_arcsec_less(astrometric_ra_deg, nasa_astrometric_ra_deg, 0.02, "flat Mars Horizons astrometric RA");
    expect_angle_arcsec_less(astrometric_dec_deg, nasa_astrometric_dec_deg, 0.02, "flat Mars Horizons astrometric Dec");

    std::printf(
        "flat Mars Horizons astrometric diff: dRA %.6f arcsec dDec %.6f arcsec light_time %.12f days iterations %d\n",
        signed_degree_difference(astrometric_ra_deg, nasa_astrometric_ra_deg) * kArcsecPerDegree,
        (astrometric_dec_deg - nasa_astrometric_dec_deg) * kArcsecPerDegree,
        light_time_days,
        light_time_iterations);

    destroy_storage_ephemeris_block(&mars_storage);
    destroy_storage_ephemeris_block(&earth_storage);
}

bool position_from_block(double jd_tdb, const void* data, taiyin::Vector3* out) {
    const taiyin::internal::CompiledEphemerisBlock* block =
        static_cast<const taiyin::internal::CompiledEphemerisBlock*>(data);
    return taiyin::internal::eval_compiled_ephemeris_block_position(jd_tdb, block, out);
}

bool velocity_from_block(double jd_tdb, const void* data, taiyin::Vector3* out) {
    const taiyin::internal::CompiledEphemerisBlock* block =
        static_cast<const taiyin::internal::CompiledEphemerisBlock*>(data);
    return taiyin::internal::eval_compiled_ephemeris_block_velocity(jd_tdb, block, out);
}

struct RelativeBlockData {
    const taiyin::internal::CompiledEphemerisBlock* target;
    const taiyin::internal::CompiledEphemerisBlock* deflector;
};

bool relative_position_from_blocks(double jd_tdb, const void* data, taiyin::Vector3* out) {
    const RelativeBlockData* blocks = static_cast<const RelativeBlockData*>(data);
    if (!blocks || !out) {
        return false;
    }
    taiyin::Vector3 target;
    taiyin::Vector3 deflector;
    if (!taiyin::internal::eval_compiled_ephemeris_block_position(jd_tdb, blocks->target, &target)
        || !taiyin::internal::eval_compiled_ephemeris_block_position(jd_tdb, blocks->deflector, &deflector)) {
        return false;
    }
    *out = taiyin::vector3_subtract(target, deflector);
    return true;
}

bool relative_velocity_from_blocks(double jd_tdb, const void* data, taiyin::Vector3* out) {
    const RelativeBlockData* blocks = static_cast<const RelativeBlockData*>(data);
    if (!blocks || !out) {
        return false;
    }
    taiyin::Vector3 target;
    taiyin::Vector3 deflector;
    if (!taiyin::internal::eval_compiled_ephemeris_block_velocity(jd_tdb, blocks->target, &target)
        || !taiyin::internal::eval_compiled_ephemeris_block_velocity(jd_tdb, blocks->deflector, &deflector)) {
        return false;
    }
    *out = taiyin::vector3_subtract(target, deflector);
    return true;
}

struct MultiRelativeBlockData {
    const taiyin::internal::CompiledEphemerisBlock* primary;
    const taiyin::internal::CompiledEphemerisBlock* const* deflectors;
    int count;
    int primary_index;
};

bool deflector_relative_position_from_blocks(double jd_tdb, const void* data, int index, taiyin::Vector3* out) {
    const MultiRelativeBlockData* blocks = static_cast<const MultiRelativeBlockData*>(data);
    if (!blocks || !out || index < 0 || index >= blocks->count) {
        return false;
    }
    if (index == blocks->primary_index) {
        *out = taiyin::Vector3{ 0.0, 0.0, 0.0 };
        return true;
    }
    taiyin::Vector3 deflector;
    taiyin::Vector3 primary;
    if (!taiyin::internal::eval_compiled_ephemeris_block_position(jd_tdb, blocks->deflectors[index], &deflector)
        || !taiyin::internal::eval_compiled_ephemeris_block_position(jd_tdb, blocks->primary, &primary)) {
        return false;
    }
    *out = taiyin::vector3_subtract(deflector, primary);
    return true;
}

bool deflector_relative_velocity_from_blocks(double jd_tdb, const void* data, int index, taiyin::Vector3* out) {
    const MultiRelativeBlockData* blocks = static_cast<const MultiRelativeBlockData*>(data);
    if (!blocks || !out || index < 0 || index >= blocks->count) {
        return false;
    }
    if (index == blocks->primary_index) {
        *out = taiyin::Vector3{ 0.0, 0.0, 0.0 };
        return true;
    }
    taiyin::Vector3 deflector;
    taiyin::Vector3 primary;
    if (!taiyin::internal::eval_compiled_ephemeris_block_velocity(jd_tdb, blocks->deflectors[index], &deflector)
        || !taiyin::internal::eval_compiled_ephemeris_block_velocity(jd_tdb, blocks->primary, &primary)) {
        return false;
    }
    *out = taiyin::vector3_subtract(deflector, primary);
    return true;
}

void check_de441_mars_aberration_matches_correction_helper() {
    using taiyin::CartesianState;
    using taiyin::internal::CompiledEphemerisBlock;
    using taiyin::internal::StorageEphemerisBlock;
    using taiyin::internal::compile_spk_ephemeris_block_from_file;
    using taiyin::internal::destroy_storage_ephemeris_block;
    using taiyin::internal::eval_compiled_ephemeris_block;
    using taiyin::internal::get_compiled_block_from_storage;

    if (!file_exists(kDe441Path)) {
        std::printf("skipping Mars flat apparent aberration oracle; local DE441 is absent\n");
        return;
    }

    // Same epoch as the Horizons astrometric fixture above.  These blocks are
    // all compiled against SSB so the flat function can derive the observer
    // heliocentric vector from observer_block - deflector_blocks[solar_index].
    const double jd_tt = 2460310.50080074091;
    const double jd_tdb = tt_to_tdb_jd_fast(jd_tt);
    const double start = jd_tdb - 2.0;
    const double end = jd_tdb + 2.0;

    StorageEphemerisBlock mars_storage;
    StorageEphemerisBlock earth_storage;
    StorageEphemerisBlock sun_storage;
    expect_true(
        compile_spk_ephemeris_block_from_file(kDe441Path, 4, 0, start, end, &mars_storage),
        "compile Mars SSB DE441 block for aberration oracle");
    expect_true(
        compile_spk_ephemeris_block_from_file(kDe441Path, 399, 0, start, end, &earth_storage),
        "compile Earth SSB DE441 block for aberration oracle");
    expect_true(
        compile_spk_ephemeris_block_from_file(kDe441Path, 10, 0, start, end, &sun_storage),
        "compile Sun SSB DE441 block for aberration oracle");

    CompiledEphemerisBlock mars_block;
    CompiledEphemerisBlock earth_block;
    CompiledEphemerisBlock sun_block;
    expect_true(get_compiled_block_from_storage(&mars_storage, 0, &mars_block), "get Mars SSB block");
    expect_true(get_compiled_block_from_storage(&earth_storage, 0, &earth_block), "get Earth SSB block");
    expect_true(get_compiled_block_from_storage(&sun_storage, 0, &sun_block), "get Sun SSB block");

    CartesianState earth_state;
    CartesianState sun_state;
    expect_true(eval_compiled_ephemeris_block(jd_tdb, &earth_block, &earth_state), "eval Earth SSB state for aberration");
    expect_true(eval_compiled_ephemeris_block(jd_tdb, &sun_block, &sun_state), "eval Sun SSB state for aberration");

    taiyin::Vector3 manual_astrometric_position;
    taiyin::Vector3 manual_astrometric_velocity;
    taiyin::Vector3 retarded_mars_position;
    taiyin::Vector3 retarded_mars_velocity;
    double manual_light_time = 0.0;
    double manual_light_time_rate = 0.0;
    expect_true(
        taiyin::solve_light_time_velocity(
            jd_tdb,
            earth_state.position_au,
            earth_state.velocity_au_per_day,
            &position_from_block,
            &velocity_from_block,
            &mars_block,
            taiyin::TAIYIN_LIGHT_TIME_DAYS_PER_AU,
            8,
            1.0e-14,
            &manual_astrometric_position,
            &manual_astrometric_velocity,
            &manual_light_time,
            &manual_light_time_rate,
            &retarded_mars_position,
            &retarded_mars_velocity),
        "manual Mars light-time velocity for aberration oracle");

    const taiyin::Vector3 observer_heliocentric_position = taiyin::vector3_subtract(
        earth_state.position_au,
        sun_state.position_au);
    const taiyin::Vector3 observer_heliocentric_velocity = taiyin::vector3_subtract(
        earth_state.velocity_au_per_day,
        sun_state.velocity_au_per_day);
    taiyin::Vector3 manual_aberrated_position;
    taiyin::Vector3 manual_aberrated_velocity;
    expect_true(
        taiyin::apply_annual_aberration(
            manual_astrometric_position,
            manual_astrometric_velocity,
            observer_heliocentric_position,
            observer_heliocentric_velocity,
            earth_state.velocity_au_per_day,
            earth_state.acceleration_au_per_day2,
            taiyin::TAIYIN_LIGHT_TIME_DAYS_PER_AU,
            taiyin::TAIYIN_SOLAR_SCHWARZSCHILD_RADIUS_AU,
            &manual_aberrated_position,
            &manual_aberrated_velocity),
        "manual Mars annual aberration oracle");

    const CompiledEphemerisBlock* deflectors[] = { &sun_block };
    const int deflector_ids[] = { 10 };
    const double schwarzschild_radius[] = { taiyin::TAIYIN_SOLAR_SCHWARZSCHILD_RADIUS_AU };
    const double deflection_limit[] = { taiyin::TAIYIN_SOLAR_DEFLECTION_LIMIT };
    double flat_astrometric_position[3];
    double flat_astrometric_velocity[3];
    double flat_aberrated_position[3];
    double flat_aberrated_velocity[3];
    double flat_apparent_position[3];
    double flat_apparent_velocity[3];
    double flat_light_time = 0.0;
    double flat_light_time_rate = 0.0;
    expect_true(
        taiyin::taiyin_calc_apparent_with_matrix_flat(
            jd_tdb,
            4,
            &mars_block,
            399,
            &earth_block,
            0,
            0,
            0,
            1,
            0,
            deflector_ids,
            deflectors,
            schwarzschild_radius,
            deflection_limit,
            taiyin::TAIYIN_APPARENT_LIGHT_TIME | taiyin::TAIYIN_APPARENT_ABERRATION | taiyin::TAIYIN_APPARENT_VELOCITY,
            0,
            0,
            0,
            0,
            8,
            1.0e-14,
            0,
            0,
            0,
            0,
            0,
            0,
            flat_astrometric_position,
            flat_astrometric_velocity,
            0,
            0,
            0,
            0,
            flat_aberrated_position,
            flat_aberrated_velocity,
            0,
            flat_apparent_position,
            flat_apparent_velocity,
            0,
            0,
            0,
            0,
            0,
            0,
            0,
            0,
            0,
            0,
            &flat_light_time,
            &flat_light_time_rate,
            0,
            0),
        "flat Mars annual aberration path succeeds");

    double expected_astrometric_position[3];
    double expected_astrometric_velocity[3];
    double expected_aberrated_position[3];
    double expected_aberrated_velocity[3];
    vector_to_array(manual_astrometric_position, expected_astrometric_position);
    vector_to_array(manual_astrometric_velocity, expected_astrometric_velocity);
    vector_to_array(manual_aberrated_position, expected_aberrated_position);
    vector_to_array(manual_aberrated_velocity, expected_aberrated_velocity);
    expect_array3_near(flat_astrometric_position, expected_astrometric_position, 1.0e-14, "flat aberration astrometric position");
    expect_array3_near(flat_astrometric_velocity, expected_astrometric_velocity, 1.0e-14, "flat aberration astrometric velocity");
    expect_array3_near(flat_aberrated_position, expected_aberrated_position, 1.0e-15, "flat aberrated position");
    expect_array3_near(flat_aberrated_velocity, expected_aberrated_velocity, 1.0e-15, "flat aberrated velocity");
    expect_array3_near(flat_apparent_position, expected_aberrated_position, 1.0e-15, "flat apparent equals aberrated without matrix");
    expect_array3_near(flat_apparent_velocity, expected_aberrated_velocity, 1.0e-15, "flat apparent velocity equals aberrated without matrix");
    expect_near(flat_light_time, manual_light_time, 1.0e-15, "flat aberration light-time");
    expect_near(flat_light_time_rate, manual_light_time_rate, 1.0e-15, "flat aberration light-time rate");

    std::printf(
        "flat Mars annual aberration delta %.6f arcsec light_time %.12f days\n",
        std::sqrt(
            std::pow(flat_aberrated_position[0] - flat_astrometric_position[0], 2.0)
            + std::pow(flat_aberrated_position[1] - flat_astrometric_position[1], 2.0)
            + std::pow(flat_aberrated_position[2] - flat_astrometric_position[2], 2.0))
            / taiyin::vector3_norm(array_to_vector(flat_astrometric_position)) * kArcsecPerDegree * taiyin::TAIYIN_RAD_TO_DEG,
        flat_light_time);

    destroy_storage_ephemeris_block(&mars_storage);
    destroy_storage_ephemeris_block(&earth_storage);
    destroy_storage_ephemeris_block(&sun_storage);
}

void check_de441_mars_shapiro_matches_correction_helper() {
    using taiyin::CartesianState;
    using taiyin::internal::CompiledEphemerisBlock;
    using taiyin::internal::StorageEphemerisBlock;
    using taiyin::internal::compile_spk_ephemeris_block_from_file;
    using taiyin::internal::destroy_storage_ephemeris_block;
    using taiyin::internal::eval_compiled_ephemeris_block;
    using taiyin::internal::get_compiled_block_from_storage;

    if (!file_exists(kDe441Path)) {
        std::printf("skipping Mars flat apparent Shapiro oracle; local DE441 is absent\n");
        return;
    }

    const double jd_tt = 2460310.50080074091;
    const double jd_tdb = tt_to_tdb_jd_fast(jd_tt);
    const double start = jd_tdb - 2.0;
    const double end = jd_tdb + 2.0;

    StorageEphemerisBlock mars_storage;
    StorageEphemerisBlock earth_storage;
    StorageEphemerisBlock sun_storage;
    expect_true(
        compile_spk_ephemeris_block_from_file(kDe441Path, 4, 0, start, end, &mars_storage),
        "compile Mars SSB DE441 block for Shapiro oracle");
    expect_true(
        compile_spk_ephemeris_block_from_file(kDe441Path, 399, 0, start, end, &earth_storage),
        "compile Earth SSB DE441 block for Shapiro oracle");
    expect_true(
        compile_spk_ephemeris_block_from_file(kDe441Path, 10, 0, start, end, &sun_storage),
        "compile Sun SSB DE441 block for Shapiro oracle");

    CompiledEphemerisBlock mars_block;
    CompiledEphemerisBlock earth_block;
    CompiledEphemerisBlock sun_block;
    expect_true(get_compiled_block_from_storage(&mars_storage, 0, &mars_block), "get Mars SSB Shapiro block");
    expect_true(get_compiled_block_from_storage(&earth_storage, 0, &earth_block), "get Earth SSB Shapiro block");
    expect_true(get_compiled_block_from_storage(&sun_storage, 0, &sun_block), "get Sun SSB Shapiro block");

    CartesianState earth_state;
    CartesianState sun_state;
    expect_true(eval_compiled_ephemeris_block(jd_tdb, &earth_block, &earth_state), "eval Earth SSB state for Shapiro");
    expect_true(eval_compiled_ephemeris_block(jd_tdb, &sun_block, &sun_state), "eval Sun SSB state for Shapiro");

    const taiyin::Vector3 observer_heliocentric_position = taiyin::vector3_subtract(
        earth_state.position_au,
        sun_state.position_au);
    const taiyin::Vector3 observer_heliocentric_velocity = taiyin::vector3_subtract(
        earth_state.velocity_au_per_day,
        sun_state.velocity_au_per_day);
    RelativeBlockData relative_blocks = { &mars_block, &sun_block };
    taiyin::Vector3 manual_astrometric_position;
    taiyin::Vector3 manual_astrometric_velocity;
    taiyin::Vector3 retarded_mars_position;
    taiyin::Vector3 retarded_mars_velocity;
    double manual_light_time = 0.0;
    double manual_light_time_rate = 0.0;
    expect_true(
        taiyin::solve_light_time_velocity_with_shapiro(
            jd_tdb,
            observer_heliocentric_position,
            observer_heliocentric_velocity,
            &relative_position_from_blocks,
            &relative_velocity_from_blocks,
            &relative_blocks,
            taiyin::TAIYIN_LIGHT_TIME_DAYS_PER_AU,
            taiyin::TAIYIN_SOLAR_SCHWARZSCHILD_RADIUS_AU * taiyin::TAIYIN_LIGHT_TIME_DAYS_PER_AU,
            8,
            1.0e-14,
            &manual_astrometric_position,
            &manual_astrometric_velocity,
            &manual_light_time,
            &manual_light_time_rate,
            &retarded_mars_position,
            &retarded_mars_velocity),
        "manual Mars Shapiro light-time velocity oracle");

    const CompiledEphemerisBlock* deflectors[] = { &sun_block };
    const int deflector_ids[] = { 10 };
    const double schwarzschild_radius[] = { taiyin::TAIYIN_SOLAR_SCHWARZSCHILD_RADIUS_AU };
    const double deflection_limit[] = { taiyin::TAIYIN_SOLAR_DEFLECTION_LIMIT };
    double flat_astrometric_position[3];
    double flat_astrometric_velocity[3];
    double flat_apparent_position[3];
    double flat_apparent_velocity[3];
    double flat_light_time = 0.0;
    double flat_light_time_rate = 0.0;
    expect_true(
        taiyin::taiyin_calc_apparent_with_matrix_flat(
            jd_tdb,
            4,
            &mars_block,
            399,
            &earth_block,
            0,
            0,
            0,
            1,
            0,
            deflector_ids,
            deflectors,
            schwarzschild_radius,
            deflection_limit,
            taiyin::TAIYIN_APPARENT_LIGHT_TIME | taiyin::TAIYIN_APPARENT_SHAPIRO_DELAY | taiyin::TAIYIN_APPARENT_VELOCITY,
            0,
            0,
            0,
            0,
            8,
            1.0e-14,
            0,
            0,
            0,
            0,
            0,
            0,
            flat_astrometric_position,
            flat_astrometric_velocity,
            0,
            0,
            0,
            0,
            0,
            0,
            0,
            flat_apparent_position,
            flat_apparent_velocity,
            0,
            0,
            0,
            0,
            0,
            0,
            0,
            0,
            0,
            0,
            &flat_light_time,
            &flat_light_time_rate,
            0,
            0),
        "flat Mars Shapiro light-time path succeeds");

    double expected_astrometric_position[3];
    double expected_astrometric_velocity[3];
    vector_to_array(manual_astrometric_position, expected_astrometric_position);
    vector_to_array(manual_astrometric_velocity, expected_astrometric_velocity);
    expect_array3_near(flat_astrometric_position, expected_astrometric_position, 1.0e-14, "flat Shapiro astrometric position");
    expect_array3_near(flat_astrometric_velocity, expected_astrometric_velocity, 1.0e-14, "flat Shapiro astrometric velocity");
    expect_array3_near(flat_apparent_position, expected_astrometric_position, 1.0e-15, "flat Shapiro apparent position without later corrections");
    expect_array3_near(flat_apparent_velocity, expected_astrometric_velocity, 1.0e-15, "flat Shapiro apparent velocity without later corrections");
    expect_near(flat_light_time, manual_light_time, 1.0e-15, "flat Shapiro light-time");
    expect_near(flat_light_time_rate, manual_light_time_rate, 1.0e-15, "flat Shapiro light-time rate");

    std::printf(
        "flat Mars Shapiro light_time %.12f days rate %.12e\n",
        flat_light_time,
        flat_light_time_rate);

    destroy_storage_ephemeris_block(&mars_storage);
    destroy_storage_ephemeris_block(&earth_storage);
    destroy_storage_ephemeris_block(&sun_storage);
}

void check_de441_mars_multi_shapiro_matches_correction_helper() {
    using taiyin::CartesianState;
    using taiyin::internal::CompiledEphemerisBlock;
    using taiyin::internal::StorageEphemerisBlock;
    using taiyin::internal::compile_spk_ephemeris_block_from_file;
    using taiyin::internal::destroy_storage_ephemeris_block;
    using taiyin::internal::eval_compiled_ephemeris_block;
    using taiyin::internal::get_compiled_block_from_storage;

    if (!file_exists(kDe441Path)) {
        std::printf("skipping Mars flat apparent multi-Shapiro oracle; local DE441 is absent\n");
        return;
    }

    const double jd_tt = 2460310.50080074091;
    const double jd_tdb = tt_to_tdb_jd_fast(jd_tt);
    const double start = jd_tdb - 2.0;
    const double end = jd_tdb + 2.0;

    StorageEphemerisBlock mars_storage;
    StorageEphemerisBlock earth_storage;
    StorageEphemerisBlock sun_storage;
    StorageEphemerisBlock jupiter_storage;
    expect_true(compile_spk_ephemeris_block_from_file(kDe441Path, 4, 0, start, end, &mars_storage), "compile Mars SSB DE441 block for multi-Shapiro oracle");
    expect_true(compile_spk_ephemeris_block_from_file(kDe441Path, 399, 0, start, end, &earth_storage), "compile Earth SSB DE441 block for multi-Shapiro oracle");
    expect_true(compile_spk_ephemeris_block_from_file(kDe441Path, 10, 0, start, end, &sun_storage), "compile Sun SSB DE441 block for multi-Shapiro oracle");
    expect_true(compile_spk_ephemeris_block_from_file(kDe441Path, 5, 0, start, end, &jupiter_storage), "compile Jupiter SSB DE441 block for multi-Shapiro oracle");

    CompiledEphemerisBlock mars_block;
    CompiledEphemerisBlock earth_block;
    CompiledEphemerisBlock sun_block;
    CompiledEphemerisBlock jupiter_block;
    expect_true(get_compiled_block_from_storage(&mars_storage, 0, &mars_block), "get Mars SSB multi-Shapiro block");
    expect_true(get_compiled_block_from_storage(&earth_storage, 0, &earth_block), "get Earth SSB multi-Shapiro block");
    expect_true(get_compiled_block_from_storage(&sun_storage, 0, &sun_block), "get Sun SSB multi-Shapiro block");
    expect_true(get_compiled_block_from_storage(&jupiter_storage, 0, &jupiter_block), "get Jupiter SSB multi-Shapiro block");

    CartesianState earth_state;
    CartesianState sun_state;
    expect_true(eval_compiled_ephemeris_block(jd_tdb, &earth_block, &earth_state), "eval Earth SSB state for multi-Shapiro");
    expect_true(eval_compiled_ephemeris_block(jd_tdb, &sun_block, &sun_state), "eval Sun SSB state for multi-Shapiro");

    const taiyin::Vector3 observer_heliocentric_position = taiyin::vector3_subtract(earth_state.position_au, sun_state.position_au);
    const taiyin::Vector3 observer_heliocentric_velocity = taiyin::vector3_subtract(earth_state.velocity_au_per_day, sun_state.velocity_au_per_day);
    RelativeBlockData relative_blocks = { &mars_block, &sun_block };
    const CompiledEphemerisBlock* deflectors[] = { &sun_block, &jupiter_block };
    const int deflector_ids[] = { 10, 5 };
    MultiRelativeBlockData multi_blocks = { &sun_block, deflectors, 2, 0 };
    const double jupiter_schwarzschild_radius = taiyin::TAIYIN_SOLAR_SCHWARZSCHILD_RADIUS_AU * 0.0009547919;
    const double schwarzschild_radius[] = {
        taiyin::TAIYIN_SOLAR_SCHWARZSCHILD_RADIUS_AU,
        jupiter_schwarzschild_radius,
    };
    taiyin::Vector3 manual_astrometric_position;
    taiyin::Vector3 manual_astrometric_velocity;
    taiyin::Vector3 retarded_mars_position;
    taiyin::Vector3 retarded_mars_velocity;
    double manual_light_time = 0.0;
    double manual_light_time_rate = 0.0;
    expect_true(
        taiyin::solve_light_time_velocity_with_multi_shapiro(
            jd_tdb,
            observer_heliocentric_position,
            observer_heliocentric_velocity,
            &relative_position_from_blocks,
            &relative_velocity_from_blocks,
            &relative_blocks,
            2,
            schwarzschild_radius,
            &deflector_relative_position_from_blocks,
            &deflector_relative_velocity_from_blocks,
            &multi_blocks,
            taiyin::TAIYIN_LIGHT_TIME_DAYS_PER_AU,
            8,
            1.0e-14,
            &manual_astrometric_position,
            &manual_astrometric_velocity,
            &manual_light_time,
            &manual_light_time_rate,
            &retarded_mars_position,
            &retarded_mars_velocity),
        "manual Mars Sun+Jupiter Shapiro light-time velocity oracle");

    const double deflection_limit[] = { taiyin::TAIYIN_SOLAR_DEFLECTION_LIMIT, taiyin::TAIYIN_SOLAR_DEFLECTION_LIMIT };
    double flat_astrometric_position[3];
    double flat_astrometric_velocity[3];
    double flat_light_time = 0.0;
    double flat_light_time_rate = 0.0;
    expect_true(
        taiyin::taiyin_calc_apparent_with_matrix_flat(
            jd_tdb,
            4,
            &mars_block,
            399,
            &earth_block,
            0,
            0,
            0,
            2,
            0,
            deflector_ids,
            deflectors,
            schwarzschild_radius,
            deflection_limit,
            taiyin::TAIYIN_APPARENT_LIGHT_TIME | taiyin::TAIYIN_APPARENT_SHAPIRO_DELAY | taiyin::TAIYIN_APPARENT_VELOCITY,
            0,
            0,
            0,
            0,
            8,
            1.0e-14,
            0,
            0,
            0,
            0,
            0,
            0,
            flat_astrometric_position,
            flat_astrometric_velocity,
            0,
            0,
            0,
            0,
            0,
            0,
            0,
            0,
            0,
            0,
            0,
            0,
            0,
            0,
            0,
            0,
            0,
            0,
            0,
            &flat_light_time,
            &flat_light_time_rate,
            0,
            0),
        "flat Mars Sun+Jupiter Shapiro light-time path succeeds");

    double expected_astrometric_position[3];
    double expected_astrometric_velocity[3];
    vector_to_array(manual_astrometric_position, expected_astrometric_position);
    vector_to_array(manual_astrometric_velocity, expected_astrometric_velocity);
    expect_array3_near(flat_astrometric_position, expected_astrometric_position, 1.0e-14, "flat multi-Shapiro astrometric position");
    expect_array3_near(flat_astrometric_velocity, expected_astrometric_velocity, 1.0e-14, "flat multi-Shapiro astrometric velocity");
    expect_near(flat_light_time, manual_light_time, 1.0e-15, "flat multi-Shapiro light-time");
    expect_near(flat_light_time_rate, manual_light_time_rate, 1.0e-15, "flat multi-Shapiro light-time rate");

    std::printf(
        "flat Mars Sun+Jupiter Shapiro light_time %.12f days rate %.12e\n",
        flat_light_time,
        flat_light_time_rate);

    destroy_storage_ephemeris_block(&mars_storage);
    destroy_storage_ephemeris_block(&earth_storage);
    destroy_storage_ephemeris_block(&sun_storage);
    destroy_storage_ephemeris_block(&jupiter_storage);
}

void check_de441_mars_deflection_aberration_horizons_oracle() {
    using taiyin::CartesianState;
    using taiyin::internal::CompiledEphemerisBlock;
    using taiyin::internal::StorageEphemerisBlock;
    using taiyin::internal::compile_spk_ephemeris_block_from_file;
    using taiyin::internal::destroy_storage_ephemeris_block;
    using taiyin::internal::eval_compiled_ephemeris_block;
    using taiyin::internal::get_compiled_block_from_storage;

    if (!file_exists(kDe441Path)) {
        std::printf("skipping Mars flat apparent deflection oracle; local DE441 is absent\n");
        return;
    }

    // NASA/Horizons fixture definition:
    // - target: Mars barycenter (NAIF 4), observer: geocenter/Earth center (NAIF 399)
    // - source: local NASA DE441 BSP, all flat API blocks compiled wrt SSB
    // - correction layer: one-way light-time + solar Shapiro delay + solar gravitational deflection + annual aberration
    // - output frame: true equator/equinox of date RA/Dec via Vondrak 2011 precession + IAU2000B nutation,
    //   matching the legacy strict Horizons oracle route in test_spk.cpp.
    const double jd_tt = 2460310.50080074091;
    const double jd_tdb = tt_to_tdb_jd_fast(jd_tt);
    const double start = jd_tdb - 2.0;
    const double end = jd_tdb + 2.0;
    const double nasa_apparent_ra_deg = 267.054418704;
    const double nasa_apparent_dec_deg = -23.961388660;

    StorageEphemerisBlock mars_storage;
    StorageEphemerisBlock earth_storage;
    StorageEphemerisBlock sun_storage;
    expect_true(
        compile_spk_ephemeris_block_from_file(kDe441Path, 4, 0, start, end, &mars_storage),
        "compile Mars SSB DE441 block for deflection oracle");
    expect_true(
        compile_spk_ephemeris_block_from_file(kDe441Path, 399, 0, start, end, &earth_storage),
        "compile Earth SSB DE441 block for deflection oracle");
    expect_true(
        compile_spk_ephemeris_block_from_file(kDe441Path, 10, 0, start, end, &sun_storage),
        "compile Sun SSB DE441 block for deflection oracle");

    CompiledEphemerisBlock mars_block;
    CompiledEphemerisBlock earth_block;
    CompiledEphemerisBlock sun_block;
    expect_true(get_compiled_block_from_storage(&mars_storage, 0, &mars_block), "get Mars SSB deflection block");
    expect_true(get_compiled_block_from_storage(&earth_storage, 0, &earth_block), "get Earth SSB deflection block");
    expect_true(get_compiled_block_from_storage(&sun_storage, 0, &sun_block), "get Sun SSB deflection block");

    CartesianState earth_state;
    CartesianState sun_state;
    expect_true(eval_compiled_ephemeris_block(jd_tdb, &earth_block, &earth_state), "eval Earth SSB state for deflection");
    expect_true(eval_compiled_ephemeris_block(jd_tdb, &sun_block, &sun_state), "eval Sun SSB state for deflection");

    const taiyin::Vector3 observer_heliocentric_position = taiyin::vector3_subtract(
        earth_state.position_au,
        sun_state.position_au);
    const taiyin::Vector3 observer_heliocentric_velocity = taiyin::vector3_subtract(
        earth_state.velocity_au_per_day,
        sun_state.velocity_au_per_day);
    RelativeBlockData relative_blocks = { &mars_block, &sun_block };
    taiyin::Vector3 manual_astrometric_position;
    taiyin::Vector3 manual_astrometric_velocity;
    taiyin::Vector3 retarded_mars_position;
    taiyin::Vector3 retarded_mars_velocity;
    double manual_light_time = 0.0;
    double manual_light_time_rate = 0.0;
    expect_true(
        taiyin::solve_light_time_velocity_with_shapiro(
            jd_tdb,
            observer_heliocentric_position,
            observer_heliocentric_velocity,
            &relative_position_from_blocks,
            &relative_velocity_from_blocks,
            &relative_blocks,
            taiyin::TAIYIN_LIGHT_TIME_DAYS_PER_AU,
            taiyin::TAIYIN_SOLAR_SCHWARZSCHILD_RADIUS_AU * taiyin::TAIYIN_LIGHT_TIME_DAYS_PER_AU,
            8,
            1.0e-14,
            &manual_astrometric_position,
            &manual_astrometric_velocity,
            &manual_light_time,
            &manual_light_time_rate,
            &retarded_mars_position,
            &retarded_mars_velocity),
        "manual Mars Shapiro light-time velocity for deflection oracle");

    taiyin::Vector3 manual_deflected_position;
    taiyin::Vector3 manual_deflected_velocity;
    expect_true(
        taiyin::apply_gravitational_deflection_from_body(
            manual_astrometric_position,
            manual_astrometric_velocity,
            earth_state.position_au,
            earth_state.velocity_au_per_day,
            sun_state.position_au,
            sun_state.velocity_au_per_day,
            manual_astrometric_position,
            manual_astrometric_velocity,
            taiyin::TAIYIN_SOLAR_SCHWARZSCHILD_RADIUS_AU,
            taiyin::TAIYIN_SOLAR_DEFLECTION_LIMIT,
            &manual_deflected_position,
            &manual_deflected_velocity),
        "manual Mars solar deflection oracle");

    taiyin::Vector3 manual_aberrated_position;
    taiyin::Vector3 manual_aberrated_velocity;
    expect_true(
        taiyin::apply_annual_aberration(
            manual_deflected_position,
            manual_deflected_velocity,
            observer_heliocentric_position,
            observer_heliocentric_velocity,
            earth_state.velocity_au_per_day,
            earth_state.acceleration_au_per_day2,
            taiyin::TAIYIN_LIGHT_TIME_DAYS_PER_AU,
            taiyin::TAIYIN_SOLAR_SCHWARZSCHILD_RADIUS_AU,
            &manual_aberrated_position,
            &manual_aberrated_velocity),
        "manual Mars deflected annual aberration oracle");

    const CompiledEphemerisBlock* deflectors[] = { &sun_block };
    const int deflector_ids[] = { 10 };
    const double schwarzschild_radius[] = { taiyin::TAIYIN_SOLAR_SCHWARZSCHILD_RADIUS_AU };
    const double deflection_limit[] = { taiyin::TAIYIN_SOLAR_DEFLECTION_LIMIT };
    double output_matrix[9];
    double output_matrix_dot[9];
    expect_true(
        taiyin::taiyin_calc_apparent_matrices_flat(
            jd_tt,
            taiyin::TAIYIN_APPARENT_VELOCITY,
            taiyin::TAIYIN_APPARENT_FRAME_TRUE_EQUATOR_OF_DATE,
            taiyin::dispatch::PRECESSION_VONDRAK2011,
            taiyin::dispatch::NUTATION_IAU2000B,
            0,
            0.5,
            0,
            0,
            output_matrix,
            output_matrix_dot,
            0,
            0,
            0,
            0,
            0),
        "prepare Mars true equator matrix for apparent oracle");

    double flat_astrometric_position[3];
    double flat_deflected_position[3];
    double flat_deflected_velocity[3];
    double flat_aberrated_position[3];
    double flat_aberrated_velocity[3];
    double flat_apparent_position[3];
    double flat_apparent_velocity[3];
    expect_true(
        taiyin::taiyin_calc_apparent_with_matrix_flat(
            jd_tdb,
            4,
            &mars_block,
            399,
            &earth_block,
            0,
            0,
            0,
            1,
            0,
            deflector_ids,
            deflectors,
            schwarzschild_radius,
            deflection_limit,
            taiyin::TAIYIN_APPARENT_LIGHT_TIME
                | taiyin::TAIYIN_APPARENT_SHAPIRO_DELAY
                | taiyin::TAIYIN_APPARENT_DEFLECTION
                | taiyin::TAIYIN_APPARENT_ABERRATION
                | taiyin::TAIYIN_APPARENT_USE_MATRIX
                | taiyin::TAIYIN_APPARENT_VELOCITY,
            0,
            0,
            0,
            0,
            8,
            1.0e-14,
            output_matrix,
            output_matrix_dot,
            0,
            0,
            0,
            0,
            flat_astrometric_position,
            0,
            0,
            flat_deflected_position,
            flat_deflected_velocity,
            0,
            flat_aberrated_position,
            flat_aberrated_velocity,
            0,
            flat_apparent_position,
            flat_apparent_velocity,
            0,
            0,
            0,
            0,
            0,
            0,
            0,
            0,
            0,
            0,
            0,
            0,
            0,
            0),
        "flat Mars deflection + aberration path succeeds");

    double expected_astrometric_position[3];
    double expected_deflected_position[3];
    double expected_deflected_velocity[3];
    double expected_aberrated_position[3];
    double expected_aberrated_velocity[3];
    vector_to_array(manual_astrometric_position, expected_astrometric_position);
    vector_to_array(manual_deflected_position, expected_deflected_position);
    vector_to_array(manual_deflected_velocity, expected_deflected_velocity);
    vector_to_array(manual_aberrated_position, expected_aberrated_position);
    vector_to_array(manual_aberrated_velocity, expected_aberrated_velocity);
    expect_array3_near(flat_astrometric_position, expected_astrometric_position, 1.0e-14, "flat deflection astrometric position");
    expect_array3_near(flat_deflected_position, expected_deflected_position, 1.0e-15, "flat deflected position");
    expect_array3_near(flat_deflected_velocity, expected_deflected_velocity, 1.0e-15, "flat deflected velocity");
    expect_array3_near(flat_aberrated_position, expected_aberrated_position, 1.0e-15, "flat deflected-aberrated position");
    expect_array3_near(flat_aberrated_velocity, expected_aberrated_velocity, 1.0e-15, "flat deflected-aberrated velocity");

    double apparent_ra_deg = 0.0;
    double apparent_dec_deg = 0.0;
    expect_true(
        equatorial_ra_dec_degrees(array_to_vector(flat_apparent_position), &apparent_ra_deg, &apparent_dec_deg),
        "convert flat Mars apparent vector to RA/Dec");
    // The full apparent path intentionally keeps a wider budget than the astrometric-only
    // oracle because it includes Shapiro delay, gravitational deflection, annual aberration,
    // and date-frame model conventions. With the SOFA/Swiss denominator convention for solar
    // deflection, the current Mars fixture is about 0.058 arcsec in RA, so 0.08 arcsec catches
    // regressions without making the test dependent on last-bit model agreement with Horizons.
    const double kApparentHorizonsToleranceArcsec = 0.08;
    expect_angle_arcsec_less(
        apparent_ra_deg,
        nasa_apparent_ra_deg,
        kApparentHorizonsToleranceArcsec,
        "flat Mars Horizons apparent RA");
    expect_angle_arcsec_less(
        apparent_dec_deg,
        nasa_apparent_dec_deg,
        kApparentHorizonsToleranceArcsec,
        "flat Mars Horizons apparent Dec");

    std::printf(
        "flat Mars Horizons apparent diff: dRA %.6f arcsec dDec %.6f arcsec\n",
        signed_degree_difference(apparent_ra_deg, nasa_apparent_ra_deg) * kArcsecPerDegree,
        (apparent_dec_deg - nasa_apparent_dec_deg) * kArcsecPerDegree);

    destroy_storage_ephemeris_block(&mars_storage);
    destroy_storage_ephemeris_block(&earth_storage);
    destroy_storage_ephemeris_block(&sun_storage);
}

void check_de441_mars_date_frame_paths_match() {
    using taiyin::internal::CompiledEphemerisBlock;
    using taiyin::internal::StorageEphemerisBlock;
    using taiyin::internal::compile_spk_ephemeris_block_from_file;
    using taiyin::internal::destroy_storage_ephemeris_block;
    using taiyin::internal::get_compiled_block_from_storage;

    if (!file_exists(kDe441Path)) {
        std::printf("skipping Mars flat apparent date-frame oracle; local DE441 is absent\n");
        return;
    }

    const double jd_tt = 2460310.50080074091;
    const double jd_tdb = tt_to_tdb_jd_fast(jd_tt);
    const double start = jd_tdb - 2.0;
    const double end = jd_tdb + 2.0;

    StorageEphemerisBlock mars_storage;
    StorageEphemerisBlock earth_storage;
    expect_true(
        compile_spk_ephemeris_block_from_file(kDe441Path, 4, 10, start, end, &mars_storage),
        "compile Mars heliocentric DE441 block for date-frame oracle");
    expect_true(
        compile_spk_ephemeris_block_from_file(kDe441Path, 399, 10, start, end, &earth_storage),
        "compile Earth heliocentric DE441 block for date-frame oracle");

    CompiledEphemerisBlock mars_block;
    CompiledEphemerisBlock earth_block;
    expect_true(get_compiled_block_from_storage(&mars_storage, 0, &mars_block), "get Mars date-frame block");
    expect_true(get_compiled_block_from_storage(&earth_storage, 0, &earth_block), "get Earth date-frame block");

    double output_matrix[9];
    expect_true(
        taiyin::taiyin_calc_apparent_matrices_flat(
            jd_tt,
            taiyin::TAIYIN_APPARENT_SPHERICAL,
            taiyin::TAIYIN_APPARENT_FRAME_TRUE_ECLIPTIC_OF_DATE,
            taiyin::dispatch::PRECESSION_IAU2006,
            taiyin::dispatch::NUTATION_IAU2000B,
            0,
            0.5,
            0,
            0,
            output_matrix,
            0,
            0,
            0,
            0,
            0,
            0),
        "prepare Mars true ecliptic matrix");

    double manual_pos[3];
    double manual_lon = 0.0;
    double manual_lat = 0.0;
    double manual_distance = 0.0;
    expect_true(
        taiyin::taiyin_calc_apparent_with_matrix_flat(
            jd_tdb,
            4,
            &mars_block,
            399,
            &earth_block,
            0,
            0,
            0,
            0,
            -1,
            0,
            0,
            0,
            0,
            taiyin::TAIYIN_APPARENT_LIGHT_TIME | taiyin::TAIYIN_APPARENT_USE_MATRIX | taiyin::TAIYIN_APPARENT_SPHERICAL,
            0,
            0,
            0,
            0,
            8,
            1.0e-14,
            output_matrix,
            0,
            0,
            0,
            0,
            0,
            0,
            0,
            0,
            0,
            0,
            0,
            0,
            0,
            0,
            manual_pos,
            0,
            0,
            &manual_lon,
            &manual_lat,
            &manual_distance,
            0,
            0,
            0,
            0,
            0,
            0,
            0,
            0,
            0,
            0),
        "manual Mars true ecliptic flat path succeeds");

    double convenience_pos[3];
    double convenience_lon = 0.0;
    double convenience_lat = 0.0;
    double convenience_distance = 0.0;
    expect_true(
        taiyin::taiyin_calc_apparent_flat(
            jd_tdb,
            jd_tt,
            4,
            &mars_block,
            399,
            &earth_block,
            0,
            0,
            0,
            0,
            -1,
            0,
            0,
            0,
            0,
            taiyin::TAIYIN_APPARENT_LIGHT_TIME | taiyin::TAIYIN_APPARENT_SPHERICAL,
            taiyin::TAIYIN_APPARENT_FRAME_TRUE_ECLIPTIC_OF_DATE,
            0,
            0,
            0,
            0,
            taiyin::dispatch::PRECESSION_IAU2006,
            taiyin::dispatch::NUTATION_IAU2000B,
            0,
            8,
            1.0e-14,
            0.5,
            0,
            0,
            0,
            0,
            0,
            0,
            0,
            0,
            0,
            0,
            0,
            0,
            convenience_pos,
            0,
            0,
            &convenience_lon,
            &convenience_lat,
            &convenience_distance,
            0,
            0,
            0,
            0,
            0,
            0,
            0,
            0,
            0,
            0),
        "convenience Mars true ecliptic flat path succeeds");

    expect_array3_near(convenience_pos, manual_pos, 1.0e-15, "Mars true ecliptic vector path match");
    expect_near(convenience_lon, manual_lon, 1.0e-15, "Mars true ecliptic longitude path match");
    expect_near(convenience_lat, manual_lat, 1.0e-15, "Mars true ecliptic latitude path match");
    expect_near(convenience_distance, manual_distance, 1.0e-15, "Mars true ecliptic distance path match");

    std::printf(
        "flat Mars true ecliptic astrometric lon %.12f deg lat %.12f deg distance %.12f au\n",
        convenience_lon * kDegPerRadian,
        convenience_lat * kDegPerRadian,
        convenience_distance);

    destroy_storage_ephemeris_block(&mars_storage);
    destroy_storage_ephemeris_block(&earth_storage);
}

}  // namespace

int main() {
    check_de441_mars_horizons_astrometric_oracle();
    check_de441_mars_aberration_matches_correction_helper();
    check_de441_mars_shapiro_matches_correction_helper();
    check_de441_mars_multi_shapiro_matches_correction_helper();
    check_de441_mars_deflection_aberration_horizons_oracle();
    check_de441_mars_date_frame_paths_match();
    return 0;
}
