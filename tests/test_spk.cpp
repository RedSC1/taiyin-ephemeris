#include "taiyin/angle.h"
#include "taiyin/corrections.h"
#include "taiyin/coordinates.h"
#include "taiyin/geometry.h"
#include "taiyin/internal/ephemeris_file_loader.h"
#include "taiyin/time.h"
#include "taiyin/internal/spk.h"
#include "taiyin/dispatch.h"
#include "test_env.h"

#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <vector>

namespace {

const char* kDe441Path = taiyin_test::getenv_path("TAIYIN_DE441_PATH");
const char* kVenusOpm4Path = taiyin_test::getenv_path("TAIYIN_VENUS_OPM4_PATH");
const char* kMarsOpm4Path = taiyin_test::getenv_path("TAIYIN_MARS_OPM4_PATH");
const char* kChironSpkPath = taiyin_test::getenv_path("TAIYIN_CHIRON_SPK_PATH");
const char* kChironOpm4Path = taiyin_test::getenv_path("TAIYIN_CHIRON_OPM4_PATH");
const char* kJupiterSatellitesSpkPath = taiyin_test::getenv_path("TAIYIN_JUPITER_SATELLITES_SPK_PATH");
const char* kMainBeltAsteroidsSpkPath = taiyin_test::getenv_path("TAIYIN_MAIN_BELT_ASTEROIDS_SPK_PATH");
const char* kNearEarthAsteroidsSpkPath = taiyin_test::getenv_path("TAIYIN_NEAR_EARTH_ASTEROIDS_SPK_PATH");
const char* kCeresOpm4Path = taiyin_test::getenv_path("TAIYIN_CERES_OPM4_PATH");
const char* kErosOpm4Path = taiyin_test::getenv_path("TAIYIN_EROS_OPM4_PATH");
const char* kJupiterCobOpm4Path = taiyin_test::getenv_path("TAIYIN_JUPITER_COB_OPM4_PATH");
const double kSecondsPerDay = taiyin::SECONDS_PER_DAY;
const double kArcsecPerRadian = taiyin::TAIYIN_RAD_TO_ARCSEC;
const double kDegPerRadian = taiyin::TAIYIN_RAD_TO_DEG;
const double kArcsecPerDegree = 3600.0;

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

bool file_exists(const char* path) {
    if (!path) {
        return false;
    }
    std::ifstream file(path, std::ios::binary);
    return static_cast<bool>(file);
}

double abs_max3(double x, double y, double z) {
    return std::fmax(std::fabs(x), std::fmax(std::fabs(y), std::fabs(z)));
}

double max_position_diff_au(const taiyin::CartesianState& a, const taiyin::CartesianState& b) {
    return abs_max3(
        a.position_au.x - b.position_au.x,
        a.position_au.y - b.position_au.y,
        a.position_au.z - b.position_au.z);
}

double position_diff_norm_au(const taiyin::CartesianState& a, const taiyin::CartesianState& b) {
    const double dx = a.position_au.x - b.position_au.x;
    const double dy = a.position_au.y - b.position_au.y;
    const double dz = a.position_au.z - b.position_au.z;
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

double position_norm_au(const taiyin::CartesianState& state) {
    const double x = state.position_au.x;
    const double y = state.position_au.y;
    const double z = state.position_au.z;
    return std::sqrt(x * x + y * y + z * z);
}

double angular_position_diff_arcsec(const taiyin::CartesianState& a, const taiyin::CartesianState& b) {
    const double ax = a.position_au.x;
    const double ay = a.position_au.y;
    const double az = a.position_au.z;
    const double bx = b.position_au.x;
    const double by = b.position_au.y;
    const double bz = b.position_au.z;
    const double an = std::sqrt(ax * ax + ay * ay + az * az);
    const double bn = std::sqrt(bx * bx + by * by + bz * bz);
    if (an == 0.0 || bn == 0.0) {
        return 0.0;
    }

    const double cx = ay * bz - az * by;
    const double cy = az * bx - ax * bz;
    const double cz = ax * by - ay * bx;
    const double cross_norm = std::sqrt(cx * cx + cy * cy + cz * cz);
    const double dot = ax * bx + ay * by + az * bz;
    return std::atan2(cross_norm, dot) * kArcsecPerRadian;
}

double max_velocity_diff_au_per_day(const taiyin::CartesianState& a, const taiyin::CartesianState& b) {
    return abs_max3(
        a.velocity_au_per_day.x - b.velocity_au_per_day.x,
        a.velocity_au_per_day.y - b.velocity_au_per_day.y,
        a.velocity_au_per_day.z - b.velocity_au_per_day.z);
}

double max_vector_diff(const taiyin::Vector3& a, const taiyin::Vector3& b) {
    return abs_max3(a.x - b.x, a.y - b.y, a.z - b.z);
}

double signed_degree_difference(double actual, double expected) {
    double diff = std::fmod(actual - expected + 180.0, 360.0);
    if (diff < 0.0) {
        diff += 360.0;
    }
    return diff - 180.0;
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

void add_state(const taiyin::CartesianState& lhs, const taiyin::CartesianState& rhs, taiyin::CartesianState* out) {
    out->position_au.x = lhs.position_au.x + rhs.position_au.x;
    out->position_au.y = lhs.position_au.y + rhs.position_au.y;
    out->position_au.z = lhs.position_au.z + rhs.position_au.z;
    out->velocity_au_per_day.x = lhs.velocity_au_per_day.x + rhs.velocity_au_per_day.x;
    out->velocity_au_per_day.y = lhs.velocity_au_per_day.y + rhs.velocity_au_per_day.y;
    out->velocity_au_per_day.z = lhs.velocity_au_per_day.z + rhs.velocity_au_per_day.z;
}

void subtract_state(const taiyin::CartesianState& lhs, const taiyin::CartesianState& rhs, taiyin::CartesianState* out) {
    out->position_au.x = lhs.position_au.x - rhs.position_au.x;
    out->position_au.y = lhs.position_au.y - rhs.position_au.y;
    out->position_au.z = lhs.position_au.z - rhs.position_au.z;
    out->velocity_au_per_day.x = lhs.velocity_au_per_day.x - rhs.velocity_au_per_day.x;
    out->velocity_au_per_day.y = lhs.velocity_au_per_day.y - rhs.velocity_au_per_day.y;
    out->velocity_au_per_day.z = lhs.velocity_au_per_day.z - rhs.velocity_au_per_day.z;
}

void expect_true(bool value, const char* label) {
    if (!value) {
        std::fprintf(stderr, "expected true: %s\n", label);
        assert(false);
    }
}

void expect_less(double actual, double limit, const char* label) {
    if (!(actual < limit)) {
        std::fprintf(stderr, "%s too large: actual %.17g limit %.17g\n", label, actual, limit);
        assert(false);
    }
}

void compare_spk_to_opm4(
    const char* label,
    const char* spk_path,
    const char* opm4_path,
    int target_id,
    int center_id,
    double jd_tdb,
    double position_limit_au,
    double velocity_limit_au_per_day
) {
    using taiyin::CartesianState;
    using taiyin::internal::CompiledEphemerisBlock;
    using taiyin::internal::StorageEphemerisBlock;
    using taiyin::internal::compile_opm4_ephemeris_block_from_file;
    using taiyin::internal::compile_spk_ephemeris_block_from_file;
    using taiyin::internal::destroy_storage_ephemeris_block;
    using taiyin::internal::get_compiled_block_from_storage;
    using taiyin::internal::EphemerisBlockFormat;
    using taiyin::internal::eval_compiled_ephemeris_block;
    using taiyin::internal::eval_compiled_ephemeris_block_acceleration;
    using taiyin::internal::eval_compiled_ephemeris_block_position;
    using taiyin::internal::eval_compiled_ephemeris_block_velocity;

    if (!file_exists(spk_path) || !file_exists(opm4_path)) {
        std::printf("skipping %s SPK/OPM4 comparison; local files are absent\n", label);
        return;
    }

    StorageEphemerisBlock spk_storage;
    const double jd_tdb_start = jd_tdb - 1.0;
    const double jd_tdb_end = jd_tdb + 1.0;
    expect_true(
        compile_spk_ephemeris_block_from_file(
            spk_path,
            target_id,
            center_id,
            jd_tdb_start,
            jd_tdb_end,
            &spk_storage),
        "compile spk ephemeris block");
    expect_true(spk_storage.format == EphemerisBlockFormat::Spk, "compiled block is SPK");
    CompiledEphemerisBlock spk_block;
    get_compiled_block_from_storage(&spk_storage, 0, &spk_block);
    expect_true(spk_block.position != 0, "SPK exposes position component");
    expect_true(spk_block.velocity != 0, "SPK exposes velocity component");
    expect_true(spk_block.acceleration != 0, "SPK exposes acceleration component");

    StorageEphemerisBlock opm4_storage;
    expect_true(compile_opm4_ephemeris_block_from_file(opm4_path, jd_tdb_start, jd_tdb_end, &opm4_storage), "compile opm4 block");
    CompiledEphemerisBlock block;
    get_compiled_block_from_storage(&opm4_storage, 0, &block);

    CartesianState spk_state;
    CartesianState opm4_state;
    expect_true(eval_compiled_ephemeris_block(jd_tdb, &spk_block, &spk_state), "eval spk relative state");
    expect_true(eval_compiled_ephemeris_block(jd_tdb, &block, &opm4_state), "eval opm4");
    taiyin::Vector3 component;
    expect_true(eval_compiled_ephemeris_block_position(jd_tdb, &spk_block, &component), "eval SPK position component");
    expect_true(eval_compiled_ephemeris_block_velocity(jd_tdb, &spk_block, &component), "eval SPK velocity component");
    expect_true(eval_compiled_ephemeris_block_acceleration(jd_tdb, &spk_block, &component), "eval SPK acceleration component");
    expect_true(std::isfinite(component.x) && std::isfinite(component.y) && std::isfinite(component.z), "SPK acceleration finite");
    expect_true(abs_max3(component.x, component.y, component.z) > 0.0, "SPK acceleration nonzero");

    const double h = 1e-3;
    taiyin::Vector3 velocity_before;
    taiyin::Vector3 velocity_after;
    taiyin::Vector3 finite_difference_acceleration;
    expect_true(eval_compiled_ephemeris_block_velocity(jd_tdb - h, &spk_block, &velocity_before), "eval SPK velocity before acceleration check");
    expect_true(eval_compiled_ephemeris_block_velocity(jd_tdb + h, &spk_block, &velocity_after), "eval SPK velocity after acceleration check");
    finite_difference_acceleration.x = (velocity_after.x - velocity_before.x) / (2.0 * h);
    finite_difference_acceleration.y = (velocity_after.y - velocity_before.y) / (2.0 * h);
    finite_difference_acceleration.z = (velocity_after.z - velocity_before.z) / (2.0 * h);
    expect_less(max_vector_diff(component, finite_difference_acceleration), 1e-10, "SPK acceleration finite-difference check");

    const double position_diff = max_position_diff_au(spk_state, opm4_state);
    const double position_diff_norm = position_diff_norm_au(spk_state, opm4_state);
    const double velocity_diff = max_velocity_diff_au_per_day(spk_state, opm4_state);
    const double range_au = position_norm_au(spk_state);
    const double position_error_arcsec_at_1au = position_diff_norm * kArcsecPerRadian;
    const double angular_arcsec = angular_position_diff_arcsec(spk_state, opm4_state);
    std::printf(
        "%s SPK/OPM4 max diff: position %.17g au, norm %.17g au, velocity %.17g au/day, angular %.17g arcsec, position-error %.17g arcsec@1au, range %.17g au\n",
        label,
        position_diff,
        position_diff_norm,
        velocity_diff,
        angular_arcsec,
        position_error_arcsec_at_1au,
        range_au);
    expect_less(position_diff, position_limit_au, "spk/opm4 position diff au");
    expect_less(velocity_diff, velocity_limit_au_per_day, "spk/opm4 velocity diff au/day");

    destroy_storage_ephemeris_block(&opm4_storage);
    destroy_storage_ephemeris_block(&spk_storage);
}

void check_spk_ephemeris_block_range_gate() {
    using taiyin::CartesianState;
    using taiyin::internal::CompiledEphemerisBlock;
    using taiyin::internal::StorageEphemerisBlock;
    using taiyin::internal::compile_spk_ephemeris_block_from_file;
    using taiyin::internal::destroy_storage_ephemeris_block;
    using taiyin::internal::get_compiled_block_from_storage;
    using taiyin::internal::eval_compiled_ephemeris_block;

    if (!file_exists(kDe441Path)) {
        std::printf("skipping SPK block range gate check; local DE441 is absent\n");
        return;
    }

    const double jd = 2451600.0;
    StorageEphemerisBlock storage;
    expect_true(
        compile_spk_ephemeris_block_from_file(
            kDe441Path,
            2,
            10,
            tt_to_tdb_jd_fast(jd - 1.0),
            tt_to_tdb_jd_fast(jd + 1.0),
            &storage),
        "compile range-limited SPK block");
    CompiledEphemerisBlock block;
    get_compiled_block_from_storage(&storage, 0, &block);

    CartesianState state;
    expect_true(eval_compiled_ephemeris_block(tt_to_tdb_jd_fast(jd), &block, &state), "eval inside SPK block range");
    expect_true(!eval_compiled_ephemeris_block(tt_to_tdb_jd_fast(jd + 2.0), &block, &state), "reject outside SPK block range");
    destroy_storage_ephemeris_block(&storage);

    expect_true(
        !compile_spk_ephemeris_block_from_file(
            kDe441Path,
            2,
            10,
            9000000.0,
            9000001.0,
            &storage),
        "reject uncovered SPK compile range");
}

void check_chiron_type21_reference() {
    using taiyin::CartesianState;
    using taiyin::internal::CompiledEphemerisBlock;
    using taiyin::internal::StorageEphemerisBlock;
    using taiyin::internal::SpkEphemerisData;
    using taiyin::internal::calc_spk_state;
    using taiyin::internal::compile_spk_ephemeris_block_from_file;
    using taiyin::internal::compile_spk_ephemeris_data_from_file;
    using taiyin::internal::compile_spk_ephemeris_data_from_source;
    using taiyin::internal::destroy_storage_ephemeris_block;
    using taiyin::internal::get_compiled_block_from_storage;
    using taiyin::internal::eval_compiled_ephemeris_block_acceleration;
    using taiyin::internal::read_file_bytes;
    using taiyin::internal::SpkByteSource;
    using taiyin::internal::spk_ephemeris_data_destroy;

    if (!file_exists(kChironSpkPath)) {
        std::printf("skipping Chiron Type 21 reference check; local BSP is absent\n");
        return;
    }

    const double jd = 2451600.0;
    SpkEphemerisData* data = 0;
    expect_true(
        compile_spk_ephemeris_data_from_file(
            kChironSpkPath,
            20002060,
            10,
            tt_to_tdb_jd_fast(jd - 1.0),
            tt_to_tdb_jd_fast(jd + 1.0),
            &data),
        "compile chiron spk");

    CartesianState actual;
    expect_true(calc_spk_state(tt_to_tdb_jd_fast(jd), data, &actual), "eval chiron type 21");

    CartesianState expected;
    expected.position_au.x = -3.2545787394774681;
    expected.position_au.y = -8.8707642969652571;
    expected.position_au.z = -2.9799434571876002;
    expected.velocity_au_per_day.x = 0.0050283991128627307;
    expected.velocity_au_per_day.y = -0.003477840204792592;
    expected.velocity_au_per_day.z = -0.00077572311613640992;

    expect_less(max_position_diff_au(actual, expected), 1e-15, "chiron type 21 reference position diff au");
    expect_less(max_velocity_diff_au_per_day(actual, expected), 1e-15, "chiron type 21 reference velocity diff au/day");
    spk_ephemeris_data_destroy(data);

    StorageEphemerisBlock chiron_storage;
    expect_true(
        compile_spk_ephemeris_block_from_file(
            kChironSpkPath,
            20002060,
            10,
            tt_to_tdb_jd_fast(jd - 1.0),
            tt_to_tdb_jd_fast(jd + 1.0),
            &chiron_storage),
        "compile chiron type 21 block");
    CompiledEphemerisBlock chiron_block;
    get_compiled_block_from_storage(&chiron_storage, 0, &chiron_block);
    expect_true(chiron_block.position != 0, "chiron type 21 block exposes position");
    expect_true(chiron_block.velocity != 0, "chiron type 21 block exposes velocity");
    expect_true(chiron_block.acceleration != 0, "chiron type 21 block exposes acceleration");
    taiyin::Vector3 acceleration;
    taiyin::Vector3 velocity_before;
    taiyin::Vector3 velocity_after;
    expect_true(
        eval_compiled_ephemeris_block_acceleration(tt_to_tdb_jd_fast(jd), &chiron_block, &acceleration),
        "chiron type 21 acceleration available");
    expect_true(std::isfinite(acceleration.x) && std::isfinite(acceleration.y) && std::isfinite(acceleration.z), "chiron type 21 acceleration finite");
    expect_true(abs_max3(acceleration.x, acceleration.y, acceleration.z) > 0.0, "chiron type 21 acceleration nonzero");
    const double h = 1e-3;
    expect_true(
        taiyin::internal::eval_compiled_ephemeris_block_velocity(tt_to_tdb_jd_fast(jd) - h, &chiron_block, &velocity_before),
        "chiron type 21 velocity before acceleration check");
    expect_true(
        taiyin::internal::eval_compiled_ephemeris_block_velocity(tt_to_tdb_jd_fast(jd) + h, &chiron_block, &velocity_after),
        "chiron type 21 velocity after acceleration check");
    taiyin::Vector3 finite_difference_acceleration;
    finite_difference_acceleration.x = (velocity_after.x - velocity_before.x) / (2.0 * h);
    finite_difference_acceleration.y = (velocity_after.y - velocity_before.y) / (2.0 * h);
    finite_difference_acceleration.z = (velocity_after.z - velocity_before.z) / (2.0 * h);
    expect_less(max_vector_diff(acceleration, finite_difference_acceleration), 1e-10, "chiron type 21 acceleration finite-difference check");
    destroy_storage_ephemeris_block(&chiron_storage);

    std::vector<uint8_t> bytes;
    expect_true(read_file_bytes(kChironSpkPath, &bytes), "read chiron spk bytes");

    struct TestMemorySource {
        const uint8_t* bytes;
        size_t byte_count;
    } source_state = { &bytes[0], bytes.size() };
    SpkByteSource source;
    source.user_data = &source_state;
    source.byte_count = bytes.size();
    source.read = [](const void* user_data, uint64_t offset, void* out, size_t byte_count) -> bool {
        const TestMemorySource* state = static_cast<const TestMemorySource*>(user_data);
        if (!state || !state->bytes || !out || offset > state->byte_count || byte_count > state->byte_count - offset) {
            return false;
        }
        std::memcpy(out, state->bytes + static_cast<size_t>(offset), byte_count);
        return true;
    };
    source.destroy = 0;

    SpkEphemerisData* source_data = 0;
    expect_true(
        compile_spk_ephemeris_data_from_source(
            source,
            20002060,
            10,
            tt_to_tdb_jd_fast(jd - 1.0),
            tt_to_tdb_jd_fast(jd + 1.0),
            &source_data),
        "compile chiron spk ephemeris from source range");
    bytes.clear();
    bytes.shrink_to_fit();
    expect_true(calc_spk_state(tt_to_tdb_jd_fast(jd), source_data, &actual), "eval chiron source range");
    expect_less(max_position_diff_au(actual, expected), 1e-15, "chiron source range position diff au");
    expect_less(max_velocity_diff_au_per_day(actual, expected), 1e-15, "chiron source range velocity diff au/day");
    spk_ephemeris_data_destroy(source_data);
}

void check_de441_mars_legacy_geometric_oracle() {
    using taiyin::CartesianState;
    using taiyin::internal::CompiledEphemerisBlock;
    using taiyin::internal::StorageEphemerisBlock;
    using taiyin::internal::compile_spk_ephemeris_block_from_file;
    using taiyin::internal::destroy_storage_ephemeris_block;
    using taiyin::internal::get_compiled_block_from_storage;
    using taiyin::internal::eval_compiled_ephemeris_block;

    if (!file_exists(kDe441Path)) {
        std::printf("skipping Mars DE441 legacy oracle check; local DE441 is absent\n");
        return;
    }

    const double jd_tdb = 2460310.5;
    const double start = jd_tdb - 1.0;
    const double end = jd_tdb + 1.0;

    StorageEphemerisBlock mars_helio_storage;
    expect_true(
        compile_spk_ephemeris_block_from_file(kDe441Path, 4, 10, start, end, &mars_helio_storage),
        "compile Mars barycenter heliocentric DE441 block");
    CompiledEphemerisBlock mars_heliocentric_block;
    get_compiled_block_from_storage(&mars_helio_storage, 0, &mars_heliocentric_block);

    CartesianState mars_heliocentric;
    expect_true(eval_compiled_ephemeris_block(jd_tdb, &mars_heliocentric_block, &mars_heliocentric), "eval Mars heliocentric DE441 oracle");

    CartesianState expected_mars_heliocentric;
    expected_mars_heliocentric.position_au.x = -2.933582835667315e-01;
    expected_mars_heliocentric.position_au.y = -1.322144774524523e+00;
    expected_mars_heliocentric.position_au.z = -5.985235358958640e-01;
    expected_mars_heliocentric.velocity_au_per_day.x = 1.424344749869245e-02;
    expected_mars_heliocentric.velocity_au_per_day.y = -1.290365309495894e-03;
    expected_mars_heliocentric.velocity_au_per_day.z = -9.761546574392497e-04;

    expect_less(max_position_diff_au(mars_heliocentric, expected_mars_heliocentric), 1e-12, "Mars heliocentric legacy position oracle diff au");
    expect_less(max_velocity_diff_au_per_day(mars_heliocentric, expected_mars_heliocentric), 1e-10, "Mars heliocentric legacy velocity oracle diff au/day");
    destroy_storage_ephemeris_block(&mars_helio_storage);

    StorageEphemerisBlock mars_geo_storage;
    expect_true(
        compile_spk_ephemeris_block_from_file(kDe441Path, 4, 399, start, end, &mars_geo_storage),
        "compile Mars barycenter geocentric DE441 block");
    CompiledEphemerisBlock mars_geocentric_block;
    get_compiled_block_from_storage(&mars_geo_storage, 0, &mars_geocentric_block);

    CartesianState mars_geocentric;
    expect_true(eval_compiled_ephemeris_block(jd_tdb, &mars_geocentric_block, &mars_geocentric), "eval Mars geocentric DE441 oracle");

    CartesianState expected_mars_geocentric;
    expected_mars_geocentric.position_au.x = -0.1275070375325599;
    expected_mars_geocentric.position_au.y = -2.2114184754713575;
    expected_mars_geocentric.position_au.z = -0.98401101623457532;
    expected_mars_geocentric.velocity_au_per_day.x = 0.031478334918838988;
    expected_mars_geocentric.velocity_au_per_day.y = 0.0014262630529658089;
    expected_mars_geocentric.velocity_au_per_day.z = 0.00020090242227411344;

    expect_less(max_position_diff_au(mars_geocentric, expected_mars_geocentric), 1e-12, "Mars geocentric legacy position oracle diff au");
    expect_less(max_velocity_diff_au_per_day(mars_geocentric, expected_mars_geocentric), 1e-10, "Mars geocentric legacy velocity oracle diff au/day");
    destroy_storage_ephemeris_block(&mars_geo_storage);
}

void check_de441_mars_horizons_apparent_oracle() {
    using taiyin::CartesianState;
    using taiyin::Matrix3x3;
    using taiyin::NutationAngles;
    using taiyin::Vector3;
    using taiyin::internal::CompiledEphemerisBlock;
    using taiyin::internal::StorageEphemerisBlock;
    using taiyin::internal::compile_spk_ephemeris_block_from_file;
    using taiyin::internal::destroy_storage_ephemeris_block;
    using taiyin::internal::get_compiled_block_from_storage;
    using taiyin::internal::eval_compiled_ephemeris_block;
    using taiyin::internal::eval_compiled_ephemeris_block_acceleration;

    if (!file_exists(kDe441Path)) {
        std::printf("skipping Mars Horizons apparent oracle check; local DE441 is absent\n");
        return;
    }

    const double jd_tt = 2460310.50080074091;  // 2024-01-01T00:00:00Z plus Delta T used by legacy JS.
    const double jd_tdb = tt_to_tdb_jd_fast(jd_tt);
    const double start = jd_tdb - 2.0;
    const double end = jd_tdb + 2.0;
    const double nasa_astrometric_ra_deg = 266.695537368;
    const double nasa_astrometric_dec_deg = -23.952038314;
    const double nasa_apparent_ra_deg = 267.054418704;
    const double nasa_apparent_dec_deg = -23.961388660;

    StorageEphemerisBlock mars_storage;
    StorageEphemerisBlock earth_helio_storage;
    StorageEphemerisBlock earth_bary_storage;
    expect_true(
        compile_spk_ephemeris_block_from_file(kDe441Path, 4, 10, start, end, &mars_storage),
        "compile Mars heliocentric block for Horizons apparent oracle");
    expect_true(
        compile_spk_ephemeris_block_from_file(kDe441Path, 399, 10, start, end, &earth_helio_storage),
        "compile Earth heliocentric block for Horizons apparent oracle");
    expect_true(
        compile_spk_ephemeris_block_from_file(kDe441Path, 399, 0, start, end, &earth_bary_storage),
        "compile Earth barycentric block for Horizons apparent oracle");
    CompiledEphemerisBlock mars_heliocentric;
    CompiledEphemerisBlock earth_heliocentric;
    CompiledEphemerisBlock earth_barycentric;
    get_compiled_block_from_storage(&mars_storage, 0, &mars_heliocentric);
    get_compiled_block_from_storage(&earth_helio_storage, 0, &earth_heliocentric);
    get_compiled_block_from_storage(&earth_bary_storage, 0, &earth_barycentric);

    CartesianState earth_helio_state;
    CartesianState earth_bary_state;
    expect_true(eval_compiled_ephemeris_block(jd_tdb, &earth_heliocentric, &earth_helio_state), "eval Earth heliocentric state");
    expect_true(eval_compiled_ephemeris_block(jd_tdb, &earth_barycentric, &earth_bary_state), "eval Earth barycentric state");

    Vector3 astrometric_position;
    Vector3 astrometric_velocity;
    Vector3 retarded_mars_position;
    Vector3 retarded_mars_velocity;
    double light_time_days = 0.0;
    double light_time_rate = 0.0;
    expect_true(
        taiyin::solve_light_time_velocity(
            jd_tdb,
            earth_helio_state.position_au,
            earth_helio_state.velocity_au_per_day,
            vector3_position_from_block,
            vector3_velocity_from_block,
            &mars_heliocentric,
            taiyin::TAIYIN_LIGHT_TIME_DAYS_PER_AU,
            8,
            1e-14,
            &astrometric_position,
            &astrometric_velocity,
            &light_time_days,
            &light_time_rate,
            &retarded_mars_position,
            &retarded_mars_velocity),
        "solve Mars light time for Horizons astrometric oracle");

    double astrometric_ra_deg = 0.0;
    double astrometric_dec_deg = 0.0;
    expect_true(
        equatorial_ra_dec_degrees(astrometric_position, &astrometric_ra_deg, &astrometric_dec_deg),
        "convert Mars astrometric position to RA/Dec");
    expect_angle_arcsec_less(astrometric_ra_deg, nasa_astrometric_ra_deg, 0.02, "Mars Horizons astrometric RA");
    expect_angle_arcsec_less(astrometric_dec_deg, nasa_astrometric_dec_deg, 0.02, "Mars Horizons astrometric Dec");

    Vector3 apparent_position = astrometric_position;
    Vector3 apparent_velocity = astrometric_velocity;
    const Vector3 zero = { 0.0, 0.0, 0.0 };
    expect_true(
        taiyin::apply_gravitational_deflection_from_body(
            apparent_position,
            apparent_velocity,
            earth_helio_state.position_au,
            earth_helio_state.velocity_au_per_day,
            zero,
            zero,
            astrometric_position,
            astrometric_velocity,
            taiyin::TAIYIN_SOLAR_SCHWARZSCHILD_RADIUS_AU,
            taiyin::TAIYIN_SOLAR_DEFLECTION_LIMIT,
            &apparent_position,
            &apparent_velocity),
        "apply Mars solar gravitational deflection for Horizons apparent oracle");

    Vector3 earth_barycentric_acceleration;
    expect_true(
        eval_compiled_ephemeris_block_acceleration(jd_tdb, &earth_barycentric, &earth_barycentric_acceleration),
        "eval Earth barycentric acceleration for annual aberration");
    expect_true(
        taiyin::apply_annual_aberration(
            apparent_position,
            apparent_velocity,
            earth_helio_state.position_au,
            earth_helio_state.velocity_au_per_day,
            earth_bary_state.velocity_au_per_day,
            earth_barycentric_acceleration,
            taiyin::TAIYIN_LIGHT_TIME_DAYS_PER_AU,
            taiyin::TAIYIN_SOLAR_SCHWARZSCHILD_RADIUS_AU,
            &apparent_position,
            &apparent_velocity),
        "apply Mars annual aberration for Horizons apparent oracle");

    Matrix3x3 precession;
    NutationAngles nutation;
    expect_true(taiyin::vondrak2011_precession_matrix(jd_tt, &precession), "Mars apparent precession succeeds");
    expect_true(taiyin::iau2000b_nutation(jd_tt, &nutation), "Mars apparent nutation succeeds");
    const Matrix3x3 true_equator = taiyin::true_equator_of_date_matrix(precession, nutation);
    const Vector3 true_equator_position = taiyin::matrix3x3_multiply_vector(true_equator, apparent_position);

    double apparent_ra_deg = 0.0;
    double apparent_dec_deg = 0.0;
    expect_true(
        equatorial_ra_dec_degrees(true_equator_position, &apparent_ra_deg, &apparent_dec_deg),
        "convert Mars apparent position to RA/Dec");
    const double astrometric_ra_error_arcsec =
        signed_degree_difference(astrometric_ra_deg, nasa_astrometric_ra_deg) * kArcsecPerDegree;
    const double astrometric_dec_error_arcsec =
        (astrometric_dec_deg - nasa_astrometric_dec_deg) * kArcsecPerDegree;
    const double apparent_ra_error_arcsec =
        signed_degree_difference(apparent_ra_deg, nasa_apparent_ra_deg) * kArcsecPerDegree;
    const double apparent_dec_error_arcsec =
        (apparent_dec_deg - nasa_apparent_dec_deg) * kArcsecPerDegree;
    std::printf(
        "mars Horizons oracle diff: astrometric dRA %.6f arcsec dDec %.6f arcsec, apparent dRA %.6f arcsec dDec %.6f arcsec\n",
        astrometric_ra_error_arcsec,
        astrometric_dec_error_arcsec,
        apparent_ra_error_arcsec,
        apparent_dec_error_arcsec);
    expect_angle_arcsec_less(apparent_ra_deg, nasa_apparent_ra_deg, 0.15, "Mars Horizons apparent RA");
    expect_angle_arcsec_less(apparent_dec_deg, nasa_apparent_dec_deg, 0.15, "Mars Horizons apparent Dec");

    destroy_storage_ephemeris_block(&mars_storage);
    destroy_storage_ephemeris_block(&earth_helio_storage);
    destroy_storage_ephemeris_block(&earth_bary_storage);
}


void check_satellite_and_cob_paths() {
    using taiyin::CartesianState;
    using taiyin::internal::SpkEphemerisData;
    using taiyin::internal::calc_spk_state;
    using taiyin::internal::compile_spk_ephemeris_data_from_file;
    using taiyin::internal::spk_ephemeris_data_destroy;

    if (!file_exists(kJupiterSatellitesSpkPath)) {
        std::printf("skipping Jupiter satellite/COB path check; local BSP is absent\n");
        return;
    }

    const double jd = 2451600.0;
    const double jd_tdb = tt_to_tdb_jd_fast(jd);
    const double start = tt_to_tdb_jd_fast(jd - 1.0);
    const double end = tt_to_tdb_jd_fast(jd + 1.0);

    SpkEphemerisData* jupiter_center_to_ssb = 0;
    SpkEphemerisData* jupiter_center_to_barycenter = 0;
    SpkEphemerisData* jupiter_barycenter_to_ssb = 0;
    expect_true(
        compile_spk_ephemeris_data_from_file(kJupiterSatellitesSpkPath, 599, 0, start, end, &jupiter_center_to_ssb),
        "compile Jupiter center to SSB through COB path");
    expect_true(
        compile_spk_ephemeris_data_from_file(kJupiterSatellitesSpkPath, 599, 5, start, end, &jupiter_center_to_barycenter),
        "compile Jupiter COB offset");
    expect_true(
        compile_spk_ephemeris_data_from_file(kJupiterSatellitesSpkPath, 5, 0, start, end, &jupiter_barycenter_to_ssb),
        "compile Jupiter barycenter to SSB");

    CartesianState path_state;
    CartesianState cob_state;
    CartesianState bary_state;
    CartesianState expected_center_state;
    expect_true(calc_spk_state(jd_tdb, jupiter_center_to_ssb, &path_state), "eval Jupiter center to SSB");
    expect_true(calc_spk_state(jd_tdb, jupiter_center_to_barycenter, &cob_state), "eval Jupiter COB offset");
    expect_true(calc_spk_state(jd_tdb, jupiter_barycenter_to_ssb, &bary_state), "eval Jupiter barycenter to SSB");
    add_state(cob_state, bary_state, &expected_center_state);
    expect_less(max_position_diff_au(path_state, expected_center_state), 1e-14, "Jupiter COB path position diff au");
    expect_less(max_velocity_diff_au_per_day(path_state, expected_center_state), 1e-14, "Jupiter COB path velocity diff au/day");

    SpkEphemerisData* io_to_jupiter_center = 0;
    SpkEphemerisData* io_to_jupiter_barycenter = 0;
    expect_true(
        compile_spk_ephemeris_data_from_file(kJupiterSatellitesSpkPath, 501, 599, start, end, &io_to_jupiter_center),
        "compile Io to Jupiter center through common-center path");
    expect_true(
        compile_spk_ephemeris_data_from_file(kJupiterSatellitesSpkPath, 501, 5, start, end, &io_to_jupiter_barycenter),
        "compile Io to Jupiter barycenter");

    CartesianState io_center_path;
    CartesianState io_bary_state;
    CartesianState expected_io_center;
    expect_true(calc_spk_state(jd_tdb, io_to_jupiter_center, &io_center_path), "eval Io to Jupiter center");
    expect_true(calc_spk_state(jd_tdb, io_to_jupiter_barycenter, &io_bary_state), "eval Io to Jupiter barycenter");
    subtract_state(io_bary_state, cob_state, &expected_io_center);
    expect_less(max_position_diff_au(io_center_path, expected_io_center), 1e-14, "Io/Jupiter center position diff au");
    expect_less(max_velocity_diff_au_per_day(io_center_path, expected_io_center), 1e-14, "Io/Jupiter center velocity diff au/day");

    spk_ephemeris_data_destroy(jupiter_center_to_ssb);
    spk_ephemeris_data_destroy(jupiter_center_to_barycenter);
    spk_ephemeris_data_destroy(jupiter_barycenter_to_ssb);
    spk_ephemeris_data_destroy(io_to_jupiter_center);
    spk_ephemeris_data_destroy(io_to_jupiter_barycenter);
}

}  // namespace

int main() {
    const double jd = 2451600.0;
    check_spk_ephemeris_block_range_gate();
    check_de441_mars_legacy_geometric_oracle();
    check_de441_mars_horizons_apparent_oracle();
    compare_spk_to_opm4("venus type 2", kDe441Path, kVenusOpm4Path, 2, 10, jd, 5e-10, 2e-10);
    compare_spk_to_opm4("mars private OPM4", kDe441Path, kMarsOpm4Path, 4, 10, jd, 3e-9, 5e-10);
    compare_spk_to_opm4("ceres private OPM4", kMainBeltAsteroidsSpkPath, kCeresOpm4Path, 2000001, 10, jd, 2e-8, 2e-9);
    compare_spk_to_opm4("eros private OPM4", kNearEarthAsteroidsSpkPath, kErosOpm4Path, 2000433, 10, jd, 2e-8, 2e-9);
    compare_spk_to_opm4("jupiter COB private OPM4", kJupiterSatellitesSpkPath, kJupiterCobOpm4Path, 599, 5, jd, 1e-8, 1e-7);
    check_chiron_type21_reference();
    check_satellite_and_cob_paths();
    compare_spk_to_opm4("chiron type 21", kChironSpkPath, kChironOpm4Path, 20002060, 10, jd, 2e-8, 2e-9);
    return 0;
}
