#include "taiyin/angle.h"
#include "taiyin/physical_constants.h"
#include "taiyin/internal/ephemeris_block.h"
#include "taiyin/internal/moshier.h"
#include "taiyin/internal/moshier_builtin.h"
#include "taiyin/internal/spk.h"
#include "test_env.h"

#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <vector>

namespace {

const char* MER404_TS_PATH = taiyin_test::getenv_path("TAIYIN_MER404_TS_PATH");
const char* DE441_PATH = taiyin_test::getenv_path("TAIYIN_DE441_PATH");

void expect_true(bool value, const char* label) {
    if (!value) {
        std::fprintf(stderr, "expected true: %s\n", label);
        assert(false);
    }
}

void expect_false(bool value, const char* label) {
    if (value) {
        std::fprintf(stderr, "expected false: %s\n", label);
        assert(false);
    }
}

void expect_near(double actual, double expected, double tolerance, const char* label) {
    if (std::fabs(actual - expected) > tolerance) {
        std::fprintf(
            stderr,
            "%s mismatch: actual %.17g expected %.17g diff %.17g tolerance %.17g\n",
            label,
            actual,
            expected,
            actual - expected,
            tolerance);
        assert(false);
    }
}

bool file_exists(const char* path) {
    if (!path) {
        return false;
    }
    std::ifstream file(path);
    return static_cast<bool>(file);
}

bool read_text_file(const char* path, std::string* out) {
    if (!out) {
        return false;
    }
    std::ifstream file(path);
    if (!file) {
        return false;
    }
    out->assign(
        std::istreambuf_iterator<char>(file),
        std::istreambuf_iterator<char>());
    return true;
}

bool parse_number_array_after(const std::string& text, const std::string& marker, std::vector<double>* out) {
    if (!out) {
        return false;
    }
    out->clear();
    const size_t marker_pos = text.find(marker);
    if (marker_pos == std::string::npos) {
        return false;
    }
    const size_t start = text.find('[', marker_pos);
    const size_t end = text.find(']', start);
    if (start == std::string::npos || end == std::string::npos || end <= start) {
        return false;
    }

    const std::string body = text.substr(start + 1, end - start - 1);
    const char* cursor = body.c_str();
    while (*cursor) {
        char* next = 0;
        const double value = std::strtod(cursor, &next);
        if (next != cursor) {
            out->push_back(value);
            cursor = next;
            continue;
        }
        ++cursor;
    }
    return !out->empty();
}

bool parse_scalar_after(const std::string& text, const std::string& marker, double* out) {
    if (!out) {
        return false;
    }
    const size_t marker_pos = text.find(marker);
    if (marker_pos == std::string::npos) {
        return false;
    }
    const char* start = text.c_str() + marker_pos + marker.size();
    char* end = 0;
    const double value = std::strtod(start, &end);
    if (end == start) {
        return false;
    }
    *out = value;
    return true;
}

bool load_mer404_table_from_js(taiyin::internal::MoshierPlanetTable* table, std::vector<int8_t>* args_storage,
    std::vector<double>* lon_storage, std::vector<double>* lat_storage, std::vector<double>* rad_storage) {
    if (!table || !args_storage || !lon_storage || !lat_storage || !rad_storage) {
        return false;
    }

    std::string text;
    if (!read_text_file(MER404_TS_PATH, &text)) {
        return false;
    }

    std::vector<double> args_as_double;
    std::vector<double> max_harmonic;
    if (!parse_number_array_after(text, "const arg_tbl", &args_as_double)
        || !parse_number_array_after(text, "const lon_tbl", lon_storage)
        || !parse_number_array_after(text, "const lat_tbl", lat_storage)
        || !parse_number_array_after(text, "const rad_tbl", rad_storage)
        || !parse_number_array_after(text, "max_harmonic:", &max_harmonic)) {
        return false;
    }

    args_storage->clear();
    for (size_t i = 0; i < args_as_double.size(); ++i) {
        args_storage->push_back(static_cast<int8_t>(args_as_double[i]));
    }

    double value = 0.0;
    *table = taiyin::internal::MoshierPlanetTable();
    if (!parse_scalar_after(text, "maxargs:", &value)) {
        return false;
    }
    table->maxargs = static_cast<int>(value);
    for (int i = 0; i < taiyin::internal::MOSHIER_NARGS; ++i) {
        table->max_harmonic[i] = i < static_cast<int>(max_harmonic.size()) ? static_cast<int>(max_harmonic[i]) : 0;
    }
    if (!parse_scalar_after(text, "max_power_of_t:", &value)) {
        return false;
    }
    table->max_power_of_t = static_cast<int>(value);
    if (!parse_scalar_after(text, "distance:", &table->distance_au)
        || !parse_scalar_after(text, "timescale:", &table->timescale_days)
        || !parse_scalar_after(text, "trunclvl:", &table->trunclvl)) {
        return false;
    }

    table->arg_tbl = args_storage->empty() ? 0 : &(*args_storage)[0];
    table->arg_count = args_storage->size();
    table->lon_tbl = lon_storage->empty() ? 0 : &(*lon_storage)[0];
    table->lon_count = lon_storage->size();
    table->lat_tbl = lat_storage->empty() ? 0 : &(*lat_storage)[0];
    table->lat_count = lat_storage->size();
    table->rad_tbl = rad_storage->empty() ? 0 : &(*rad_storage)[0];
    table->rad_count = rad_storage->size();
    return true;
}

double vector_norm(const taiyin::Vector3& vector) {
    return std::sqrt(vector.x * vector.x + vector.y * vector.y + vector.z * vector.z);
}

double position_diff_norm_au(const taiyin::CartesianState& lhs, const taiyin::CartesianState& rhs) {
    const double dx = lhs.position_au.x - rhs.position_au.x;
    const double dy = lhs.position_au.y - rhs.position_au.y;
    const double dz = lhs.position_au.z - rhs.position_au.z;
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

double angular_position_diff_arcsec(const taiyin::CartesianState& lhs, const taiyin::CartesianState& rhs) {
    const double ax = lhs.position_au.x;
    const double ay = lhs.position_au.y;
    const double az = lhs.position_au.z;
    const double bx = rhs.position_au.x;
    const double by = rhs.position_au.y;
    const double bz = rhs.position_au.z;
    const double an = vector_norm(lhs.position_au);
    const double bn = vector_norm(rhs.position_au);
    if (an == 0.0 || bn == 0.0) {
        return 0.0;
    }

    const double cx = ay * bz - az * by;
    const double cy = az * bx - ax * bz;
    const double cz = ax * by - ay * bx;
    const double cross_norm = std::sqrt(cx * cx + cy * cy + cz * cz);
    const double dot = ax * bx + ay * by + az * bz;
    return std::atan2(cross_norm, dot) * taiyin::TAIYIN_RAD_TO_ARCSEC;
}

void test_synthetic_gplan_block() {
    using taiyin::CartesianState;
    using taiyin::internal::CompiledEphemerisBlock;
    using taiyin::internal::EphemerisBlockFormat;
    using taiyin::internal::MoshierCorrectionSegment;
    using taiyin::internal::MoshierPlanetEvaluator;
    using taiyin::internal::MoshierPlanetTable;
    using taiyin::internal::StorageEphemerisBlock;
    using taiyin::internal::get_compiled_block_from_storage;
    using taiyin::internal::destroy_storage_ephemeris_block;

    const int8_t args[] = { 0, 0, -1 };
    const double lon[] = { 90.0 * 3600.0 };
    const double lat[] = { 0.0 };
    const double rad[] = { 0.0 };

    MoshierPlanetTable table;
    table.maxargs = 1;
    table.max_harmonic[0] = 0;
    table.arg_tbl = args;
    table.arg_count = sizeof(args) / sizeof(args[0]);
    table.lon_tbl = lon;
    table.lon_count = 1;
    table.lat_tbl = lat;
    table.lat_count = 1;
    table.rad_tbl = rad;
    table.rad_count = 1;
    table.distance_au = 1.0;
    table.timescale_days = 3652500.0;
    table.trunclvl = 1.0;

    StorageEphemerisBlock storage;
    expect_true(
        taiyin::internal::compile_moshier_planet_ephemeris_block(
            1, 10, 2451540.0, 2451550.0, MoshierPlanetEvaluator::GPlan, 0, table, 0, 0, &storage),
        "compile synthetic Moshier block");
    CompiledEphemerisBlock block;
    get_compiled_block_from_storage(&storage, 0, &block);
    expect_true(block.format == EphemerisBlockFormat::Moshier, "synthetic block format");

    CartesianState state;
    expect_true(taiyin::internal::eval_compiled_ephemeris_block(2451545.0, &block, &state), "eval synthetic Moshier");
    expect_near(state.position_au.x, 0.0, 1e-12, "synthetic x");
    expect_near(state.position_au.y, std::cos(23.4392911 * taiyin::TAIYIN_PI / 180.0), 1e-12, "synthetic y");
    expect_near(state.position_au.z, std::sin(23.4392911 * taiyin::TAIYIN_PI / 180.0), 1e-12, "synthetic z");
    expect_false(taiyin::internal::eval_compiled_ephemeris_block(2451551.0, &block, &state), "reject outside Moshier range");
    destroy_storage_ephemeris_block(&storage);

    MoshierCorrectionSegment correction;
    correction.start_year = 1900.0;
    correction.end_year = 2100.0;
    correction.center_year = 2000.0;
    correction.half_width_years = 100.0;
    correction.lon_arcsec[0] = 0.0;
    correction.lon_arcsec[1] = 0.0;
    correction.lon_arcsec[2] = 0.0;
    correction.lon_arcsec[3] = 3600.0;
    correction.lat_arcsec[0] = 0.0;
    correction.lat_arcsec[1] = 0.0;
    correction.lat_arcsec[2] = 0.0;
    correction.lat_arcsec[3] = 0.0;

    StorageEphemerisBlock storage2;
    expect_true(
        taiyin::internal::compile_moshier_planet_ephemeris_block(
            1, 10, 2451540.0, 2451550.0, MoshierPlanetEvaluator::GPlan, 0, table, &correction, 1, &storage2),
        "compile corrected synthetic Moshier block");
    CompiledEphemerisBlock block2;
    get_compiled_block_from_storage(&storage2, 0, &block2);
    expect_true(taiyin::internal::eval_compiled_ephemeris_block(2451545.0, &block2, &state), "eval corrected Moshier");
    expect_near(state.position_au.x, std::sin(taiyin::TAIYIN_PI / 180.0), 1e-12, "corrected x");
    destroy_storage_ephemeris_block(&storage2);
}

void test_mercury_table_against_de441() {
    using taiyin::CartesianState;
    using taiyin::internal::CompiledEphemerisBlock;
    using taiyin::internal::StorageEphemerisBlock;
    using taiyin::internal::get_compiled_block_from_storage;
    using taiyin::internal::destroy_storage_ephemeris_block;

    if (!file_exists(DE441_PATH)) {
        std::printf("skipping Moshier Mercury/DE441 comparison; local DE441 is absent\n");
        return;
    }

    StorageEphemerisBlock moshier_storage;
    expect_true(
        taiyin::internal::compile_builtin_moshier_planet_ephemeris_block(
            1, 10, 2451545.0 - 3652.5, 2451545.0 + 3652.5, false, &moshier_storage),
        "compile builtin Mercury Moshier block");
    CompiledEphemerisBlock moshier_block;
    get_compiled_block_from_storage(&moshier_storage, 0, &moshier_block);
    expect_true(moshier_block.position != 0, "Mercury Moshier exposes position");
    expect_true(moshier_block.velocity != 0, "Mercury Moshier exposes velocity");
    expect_true(moshier_block.acceleration != 0, "Mercury Moshier exposes acceleration");
    taiyin::Vector3 mercury_acceleration;
    expect_true(
        taiyin::internal::eval_compiled_ephemeris_block_acceleration(2451545.0, &moshier_block, &mercury_acceleration),
        "eval Mercury Moshier acceleration");
    expect_true(
        std::isfinite(mercury_acceleration.x)
            && std::isfinite(mercury_acceleration.y)
            && std::isfinite(mercury_acceleration.z),
        "Mercury Moshier acceleration finite");
    expect_true(vector_norm(mercury_acceleration) > 0.0, "Mercury Moshier acceleration nonzero");

    StorageEphemerisBlock spk_storage;
    expect_true(
        taiyin::internal::compile_spk_ephemeris_block_from_file(
            DE441_PATH, 1, 10, 2451545.0 - 3652.5, 2451545.0 + 3652.5, &spk_storage),
        "compile Mercury DE441 block");
    CompiledEphemerisBlock spk_block;
    get_compiled_block_from_storage(&spk_storage, 0, &spk_block);

    double max_position_au = 0.0;
    double max_angular_arcsec = 0.0;
    for (int i = -10; i <= 10; ++i) {
        const double jd = 2451545.0 + i * 365.25;
        CartesianState moshier_state;
        CartesianState spk_state;
        expect_true(taiyin::internal::eval_compiled_ephemeris_block(jd, &moshier_block, &moshier_state), "eval Mercury Moshier");
        expect_true(taiyin::internal::eval_compiled_ephemeris_block(jd, &spk_block, &spk_state), "eval Mercury DE441");
        max_position_au = std::max(max_position_au, position_diff_norm_au(moshier_state, spk_state));
        max_angular_arcsec = std::max(max_angular_arcsec, angular_position_diff_arcsec(moshier_state, spk_state));
    }

    std::printf(
        "Mercury Moshier raw PLAN404 vs DE441: max position %.17g au, max angular %.17g arcsec\n",
        max_position_au,
        max_angular_arcsec);
    expect_true(max_position_au < 2e-5, "Mercury Moshier position vs DE441");
    expect_true(max_angular_arcsec < 15.0, "Mercury Moshier angular vs DE441");

    destroy_storage_ephemeris_block(&spk_storage);
    destroy_storage_ephemeris_block(&moshier_storage);
}

void test_builtin_major_planets_against_de441() {
    using taiyin::CartesianState;
    using taiyin::internal::CompiledEphemerisBlock;
    using taiyin::internal::StorageEphemerisBlock;
    using taiyin::internal::get_compiled_block_from_storage;
    using taiyin::internal::destroy_storage_ephemeris_block;

    if (!file_exists(DE441_PATH)) {
        std::printf("skipping builtin Moshier/DE441 comparison; local DE441 is absent\n");
        return;
    }

    struct BodyCheck {
        int target_id;
        const char* label;
        double max_allowed_position_au;
        double max_allowed_arcsec;
    };

    const BodyCheck bodies[] = {
        { 1, "Mercury", 2e-5, 15.0 },
        { 2, "Venus", 2e-5, 15.0 },
        { 3, "EMB", 2e-5, 15.0 },
        { 4, "Mars", 2e-4, 150.0 },
        { 5, "Jupiter", 2e-4, 150.0 },
        { 6, "Saturn", 2e-4, 150.0 },
        { 7, "Uranus", 2e-4, 150.0 },
        { 8, "Neptune", 2e-4, 150.0 },
        { 9, "Pluto", 2e-3, 500.0 },
    };

    for (size_t body_index = 0; body_index < sizeof(bodies) / sizeof(bodies[0]); ++body_index) {
        const BodyCheck& body = bodies[body_index];
        StorageEphemerisBlock moshier_storage;
        expect_true(
            taiyin::internal::compile_builtin_moshier_planet_ephemeris_block(
                body.target_id, 10, 2451545.0 - 3652.5, 2451545.0 + 3652.5, true, &moshier_storage),
            "compile corrected builtin Moshier block");
        CompiledEphemerisBlock moshier_block;
        get_compiled_block_from_storage(&moshier_storage, 0, &moshier_block);
        expect_true(moshier_block.acceleration != 0, "corrected builtin Moshier exposes acceleration");
        taiyin::Vector3 acceleration;
        expect_true(
            taiyin::internal::eval_compiled_ephemeris_block_acceleration(2451545.0, &moshier_block, &acceleration),
            "eval corrected builtin Moshier acceleration");
        expect_true(std::isfinite(acceleration.x) && std::isfinite(acceleration.y) && std::isfinite(acceleration.z), "corrected builtin Moshier acceleration finite");

        StorageEphemerisBlock spk_storage;
        expect_true(
            taiyin::internal::compile_spk_ephemeris_block_from_file(
                DE441_PATH, body.target_id, 10, 2451545.0 - 3652.5, 2451545.0 + 3652.5, &spk_storage),
            "compile DE441 block");
        CompiledEphemerisBlock spk_block;
        get_compiled_block_from_storage(&spk_storage, 0, &spk_block);

        double max_position_au = 0.0;
        double max_angular_arcsec = 0.0;
        for (int i = -10; i <= 10; ++i) {
            const double jd = 2451545.0 + i * 365.25;
            CartesianState moshier_state;
            CartesianState spk_state;
            expect_true(taiyin::internal::eval_compiled_ephemeris_block(jd, &moshier_block, &moshier_state), "eval corrected Moshier");
            expect_true(taiyin::internal::eval_compiled_ephemeris_block(jd, &spk_block, &spk_state), "eval DE441");
            max_position_au = std::max(max_position_au, position_diff_norm_au(moshier_state, spk_state));
            max_angular_arcsec = std::max(max_angular_arcsec, angular_position_diff_arcsec(moshier_state, spk_state));
        }

        std::printf(
            "%s Moshier corrected vs DE441: max position %.17g au, max angular %.17g arcsec\n",
            body.label,
            max_position_au,
            max_angular_arcsec);
        expect_true(max_position_au < body.max_allowed_position_au, "builtin corrected Moshier position vs DE441");
        expect_true(max_angular_arcsec < body.max_allowed_arcsec, "builtin corrected Moshier angular vs DE441");

        destroy_storage_ephemeris_block(&spk_storage);
        destroy_storage_ephemeris_block(&moshier_storage);
    }
}

void test_builtin_moon_and_earth_body_against_de441() {
    using taiyin::CartesianState;
    using taiyin::internal::CompiledEphemerisBlock;
    using taiyin::internal::StorageEphemerisBlock;
    using taiyin::internal::get_compiled_block_from_storage;
    using taiyin::internal::destroy_storage_ephemeris_block;

    if (!file_exists(DE441_PATH)) {
        std::printf("skipping Moon/Earth Moshier/DE441 comparison; local DE441 is absent\n");
        return;
    }

    struct BodyCheck {
        int target_id;
        int center_id;
        const char* label;
        double max_allowed_position_au;
        double max_allowed_arcsec;
    };

    const BodyCheck bodies[] = {
        { 301, 399, "Moon geocentric", 1e-7, 10.0 },
        { 399, 10, "Earth body heliocentric", 5e-7, 10.0 },
    };

    for (size_t body_index = 0; body_index < sizeof(bodies) / sizeof(bodies[0]); ++body_index) {
        const BodyCheck& body = bodies[body_index];
        StorageEphemerisBlock moshier_storage;
        expect_true(
            taiyin::internal::compile_builtin_moshier_planet_ephemeris_block(
                body.target_id, body.center_id, 2451545.0 - 3652.5, 2451545.0 + 3652.5, true, &moshier_storage),
            "compile Moon/Earth builtin Moshier block");
        CompiledEphemerisBlock moshier_block;
        get_compiled_block_from_storage(&moshier_storage, 0, &moshier_block);
        expect_true(moshier_block.acceleration != 0, "Moon/Earth builtin Moshier exposes acceleration");
        taiyin::Vector3 acceleration;
        expect_true(
            taiyin::internal::eval_compiled_ephemeris_block_acceleration(2451545.0, &moshier_block, &acceleration),
            "eval Moon/Earth builtin Moshier acceleration");
        expect_true(std::isfinite(acceleration.x) && std::isfinite(acceleration.y) && std::isfinite(acceleration.z), "Moon/Earth builtin Moshier acceleration finite");

        StorageEphemerisBlock spk_storage;
        expect_true(
            taiyin::internal::compile_spk_ephemeris_block_from_file(
                DE441_PATH, body.target_id, body.center_id, 2451545.0 - 3652.5, 2451545.0 + 3652.5, &spk_storage),
            "compile Moon/Earth DE441 block");
        CompiledEphemerisBlock spk_block;
        get_compiled_block_from_storage(&spk_storage, 0, &spk_block);

        double max_position_au = 0.0;
        double max_angular_arcsec = 0.0;
        for (int i = -10; i <= 10; ++i) {
            const double jd = 2451545.0 + i * 365.25;
            CartesianState moshier_state;
            CartesianState spk_state;
            expect_true(taiyin::internal::eval_compiled_ephemeris_block(jd, &moshier_block, &moshier_state), "eval Moon/Earth Moshier");
            expect_true(taiyin::internal::eval_compiled_ephemeris_block(jd, &spk_block, &spk_state), "eval Moon/Earth DE441");
            max_position_au = std::max(max_position_au, position_diff_norm_au(moshier_state, spk_state));
            max_angular_arcsec = std::max(max_angular_arcsec, angular_position_diff_arcsec(moshier_state, spk_state));
        }

        std::printf(
            "%s Moshier vs DE441: max position %.17g au, max angular %.17g arcsec\n",
            body.label,
            max_position_au,
            max_angular_arcsec);
        expect_true(max_position_au < body.max_allowed_position_au, "Moon/Earth Moshier position vs DE441");
        expect_true(max_angular_arcsec < body.max_allowed_arcsec, "Moon/Earth Moshier angular vs DE441");

        destroy_storage_ephemeris_block(&spk_storage);
        destroy_storage_ephemeris_block(&moshier_storage);
    }
}

}  // namespace

int main() {
    test_synthetic_gplan_block();
    test_mercury_table_against_de441();
    test_builtin_major_planets_against_de441();
    test_builtin_moon_and_earth_body_against_de441();
    return 0;
}
