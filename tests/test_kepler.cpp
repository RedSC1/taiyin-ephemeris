#include "taiyin/angle.h"
#include "taiyin/physical_constants.h"
#include "taiyin/vector3.h"
#include "taiyin/internal/ephemeris_block.h"
#include "taiyin/internal/kepler.h"
#include "taiyin/internal/spk.h"
#include "test_env.h"

#include <cassert>
#include <cmath>
#include <cstdio>
#include <fstream>

namespace {

const char* kDe441Path = taiyin_test::getenv_path("TAIYIN_DE441_PATH");

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

void expect_less(double actual, double limit, const char* label) {
    if (!(actual < limit)) {
        std::fprintf(stderr, "%s too large: actual %.17g limit %.17g\n", label, actual, limit);
        assert(false);
    }
}

bool file_exists(const char* path) {
    if (!path) {
        return false;
    }
    std::ifstream file(path, std::ios::binary);
    return static_cast<bool>(file);
}

double position_diff_norm_au(const taiyin::CartesianState& lhs, const taiyin::CartesianState& rhs) {
    const double dx = lhs.position_au.x - rhs.position_au.x;
    const double dy = lhs.position_au.y - rhs.position_au.y;
    const double dz = lhs.position_au.z - rhs.position_au.z;
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

double angular_position_diff_arcsec(const taiyin::CartesianState& lhs, const taiyin::CartesianState& rhs) {
    const double lhs_norm = taiyin::vector3_norm(lhs.position_au);
    const double rhs_norm = taiyin::vector3_norm(rhs.position_au);
    if (lhs_norm == 0.0 || rhs_norm == 0.0) {
        return 0.0;
    }

    const taiyin::Vector3 c = taiyin::vector3_cross(lhs.position_au, rhs.position_au);
    return std::atan2(taiyin::vector3_norm(c), taiyin::vector3_dot(lhs.position_au, rhs.position_au)) * taiyin::TAIYIN_RAD_TO_ARCSEC;
}

double two_body_energy(const taiyin::CartesianState& state, double mu_au3_day2) {
    const double r = taiyin::vector3_norm(state.position_au);
    const double v2 = taiyin::vector3_dot(state.velocity_au_per_day, state.velocity_au_per_day);
    return 0.5 * v2 - mu_au3_day2 / r;
}

bool state_to_kepler_elements(
    const taiyin::CartesianState& state,
    double epoch_jd_tdb,
    double mu_au3_day2,
    double jd_tdb_start,
    double jd_tdb_end,
    taiyin::internal::KeplerElements* out
) {
    if (!out) {
        return false;
    }

    const taiyin::Vector3 r = state.position_au;
    const taiyin::Vector3 v = state.velocity_au_per_day;
    const double r_norm = taiyin::vector3_norm(r);
    const double v2 = taiyin::vector3_dot(v, v);
    const taiyin::Vector3 h = taiyin::vector3_cross(r, v);
    const double h_norm = taiyin::vector3_norm(h);
    if (r_norm <= 0.0 || h_norm <= 0.0 || mu_au3_day2 <= 0.0) {
        return false;
    }

    taiyin::Vector3 e_vec;
    const taiyin::Vector3 v_cross_h = taiyin::vector3_cross(v, h);
    e_vec.x = v_cross_h.x / mu_au3_day2 - r.x / r_norm;
    e_vec.y = v_cross_h.y / mu_au3_day2 - r.y / r_norm;
    e_vec.z = v_cross_h.z / mu_au3_day2 - r.z / r_norm;
    const double eccentricity = taiyin::vector3_norm(e_vec);
    if (eccentricity <= 1e-12 || eccentricity >= 1.0) {
        return false;
    }

    const double energy = 0.5 * v2 - mu_au3_day2 / r_norm;
    const double semi_major_axis = -mu_au3_day2 / (2.0 * energy);
    const double inclination = std::acos(h.z / h_norm);
    const taiyin::Vector3 node = { -h.y, h.x, 0.0 };
    const double node_norm = taiyin::vector3_norm(node);
    if (semi_major_axis <= 0.0 || node_norm <= 1e-14) {
        return false;
    }

    const double omega_node = taiyin::normalize_radians(std::atan2(node.y, node.x));
    const double argument_periapsis = taiyin::normalize_radians(std::atan2(
        taiyin::vector3_dot(taiyin::vector3_cross(node, e_vec), h) / (node_norm * eccentricity * h_norm),
        taiyin::vector3_dot(node, e_vec) / (node_norm * eccentricity)));
    const double true_anomaly = std::atan2(
        taiyin::vector3_dot(taiyin::vector3_cross(e_vec, r), h) / (eccentricity * r_norm * h_norm),
        taiyin::vector3_dot(e_vec, r) / (eccentricity * r_norm));
    const double eccentric_anomaly = std::atan2(
        std::sqrt(1.0 - eccentricity * eccentricity) * std::sin(true_anomaly),
        eccentricity + std::cos(true_anomaly));
    const double mean_anomaly = taiyin::normalize_radians(
        eccentric_anomaly - eccentricity * std::sin(eccentric_anomaly));

    out->target_id = 2;
    out->center_id = 10;
    out->jd_tdb_start = jd_tdb_start;
    out->jd_tdb_end = jd_tdb_end;
    out->epoch_jd_tdb = epoch_jd_tdb;
    out->mu_au3_day2 = mu_au3_day2;
    out->semi_major_axis_au = semi_major_axis;
    out->eccentricity = eccentricity;
    out->inclination_rad = inclination;
    out->longitude_ascending_node_rad = omega_node;
    out->argument_periapsis_rad = argument_periapsis;
    out->mean_anomaly_at_epoch_rad = mean_anomaly;
    return true;
}

taiyin::internal::KeplerElements make_circular_element() {
    taiyin::internal::KeplerElements element;
    expect_true(
        taiyin::internal::make_elliptic_kepler_elements(
            2000001,
            10,
            2451545.0 - 365.25,
            2451545.0 + 365.25,
            2451545.0,
            taiyin::TAIYIN_SOLAR_MU_AU3_DAY2,
            1.0,
            0.0,
            0.0,
            0.0,
            0.0,
            0.0,
            &element),
        "make circular element");
    return element;
}

void check_elliptical_kepler_shape_and_energy() {
    using taiyin::CartesianState;
    using taiyin::internal::CompiledEphemerisBlock;
    using taiyin::internal::StorageEphemerisBlock;
    using taiyin::internal::KeplerElements;
    using taiyin::internal::compile_kepler_ephemeris_block;
    using taiyin::internal::destroy_storage_ephemeris_block;
    using taiyin::internal::eval_compiled_ephemeris_block;
    using taiyin::internal::get_compiled_block_from_storage;

    KeplerElements element = make_circular_element();
    element.semi_major_axis_au = 2.0;
    element.eccentricity = 0.25;
    element.jd_tdb_start = 2451545.0 - 2000.0;
    element.jd_tdb_end = 2451545.0 + 2000.0;

    StorageEphemerisBlock storage;
    expect_true(
        compile_kepler_ephemeris_block(&element, 1, 2451545.0 - 1000.0, 2451545.0 + 1000.0, &storage),
        "compile elliptical kepler block");
    CompiledEphemerisBlock block;
    get_compiled_block_from_storage(&storage, 0, &block);

    CartesianState state;
    expect_true(eval_compiled_ephemeris_block(2451545.0, &block, &state), "eval periapsis");
    expect_near(state.position_au.x, 1.5, 1e-14, "periapsis x");
    expect_near(state.position_au.y, 0.0, 1e-14, "periapsis y");
    expect_near(
        state.velocity_au_per_day.y,
        std::sqrt(taiyin::TAIYIN_SOLAR_MU_AU3_DAY2 / element.semi_major_axis_au)
            * std::sqrt((1.0 + element.eccentricity) / (1.0 - element.eccentricity)),
        1e-14,
        "periapsis velocity y");

    const double expected_energy = -taiyin::TAIYIN_SOLAR_MU_AU3_DAY2 / (2.0 * element.semi_major_axis_au);
    expect_near(two_body_energy(state, taiyin::TAIYIN_SOLAR_MU_AU3_DAY2), expected_energy, 1e-16, "periapsis energy");

    const double half_period_days =
        taiyin::TAIYIN_PI / std::sqrt(taiyin::TAIYIN_SOLAR_MU_AU3_DAY2 / (element.semi_major_axis_au * element.semi_major_axis_au * element.semi_major_axis_au));
    expect_true(eval_compiled_ephemeris_block(2451545.0 + half_period_days, &block, &state), "eval apoapsis");
    expect_near(state.position_au.x, -2.5, 1e-11, "apoapsis x");
    expect_near(state.position_au.y, 0.0, 1e-11, "apoapsis y");
    expect_near(two_body_energy(state, taiyin::TAIYIN_SOLAR_MU_AU3_DAY2), expected_energy, 1e-16, "apoapsis energy");

    destroy_storage_ephemeris_block(&storage);
}

void check_kepler_vs_de441_venus_osculating_drift() {
    using taiyin::CartesianState;
    using taiyin::internal::CompiledEphemerisBlock;
    using taiyin::internal::StorageEphemerisBlock;
    using taiyin::internal::compile_kepler_ephemeris_block;
    using taiyin::internal::compile_spk_ephemeris_block_from_file;
    using taiyin::internal::destroy_storage_ephemeris_block;
    using taiyin::internal::eval_compiled_ephemeris_block;
    using taiyin::internal::get_compiled_block_from_storage;

    if (!file_exists(kDe441Path)) {
        std::printf("skipping Kepler/DE441 drift check; local DE441 is absent\n");
        return;
    }

    const double epoch = 2451545.0;
    const double start = epoch - 40.0;
    const double end = epoch + 40.0;

    StorageEphemerisBlock spk_storage;
    expect_true(
        compile_spk_ephemeris_block_from_file(kDe441Path, 2, 10, start, end, &spk_storage),
        "compile Venus DE441 block");
    CompiledEphemerisBlock spk_block;
    get_compiled_block_from_storage(&spk_storage, 0, &spk_block);

    CartesianState epoch_state;
    expect_true(eval_compiled_ephemeris_block(epoch, &spk_block, &epoch_state), "eval Venus DE441 epoch");

    taiyin::internal::KeplerElements osculating;
    expect_true(
        state_to_kepler_elements(epoch_state, epoch, taiyin::TAIYIN_SOLAR_MU_AU3_DAY2, start, end, &osculating),
        "derive Venus osculating elements from DE441 state");

    StorageEphemerisBlock kepler_storage;
    expect_true(
        compile_kepler_ephemeris_block(&osculating, 1, start, end, &kepler_storage),
        "compile Venus osculating Kepler block");
    CompiledEphemerisBlock kepler_block;
    get_compiled_block_from_storage(&kepler_storage, 0, &kepler_block);

    double max_position_au = 0.0;
    double max_angular_arcsec = 0.0;
    for (int i = -4; i <= 4; ++i) {
        const double jd = epoch + static_cast<double>(i) * 10.0;
        CartesianState spk_state;
        CartesianState kepler_state;
        expect_true(eval_compiled_ephemeris_block(jd, &spk_block, &spk_state), "eval Venus DE441 sample");
        expect_true(eval_compiled_ephemeris_block(jd, &kepler_block, &kepler_state), "eval Venus Kepler sample");
        const double position_error = position_diff_norm_au(spk_state, kepler_state);
        const double angular_error = angular_position_diff_arcsec(spk_state, kepler_state);
        if (position_error > max_position_au) {
            max_position_au = position_error;
        }
        if (angular_error > max_angular_arcsec) {
            max_angular_arcsec = angular_error;
        }
    }

    std::printf(
        "Venus osculating Kepler vs DE441 over +/-40d: max position %.17g au, max angular %.17g arcsec\n",
        max_position_au,
        max_angular_arcsec);

    expect_less(max_position_au, 1e-3, "Venus osculating Kepler position drift au");
    expect_less(max_angular_arcsec, 400.0, "Venus osculating Kepler angular drift arcsec");

    destroy_storage_ephemeris_block(&kepler_storage);
    destroy_storage_ephemeris_block(&spk_storage);
}

}  // namespace

int main() {
    using taiyin::CartesianState;
    using taiyin::internal::CompiledEphemerisBlock;
    using taiyin::internal::StorageEphemerisBlock;
    using taiyin::internal::EphemerisBlockFormat;
    using taiyin::internal::KeplerElements;
    using taiyin::internal::calc_kepler_state;
    using taiyin::internal::compile_kepler_ephemeris_block;
    using taiyin::internal::compile_kepler_ephemeris_data;
    using taiyin::internal::destroy_storage_ephemeris_block;
    using taiyin::internal::eval_compiled_ephemeris_block;
    using taiyin::internal::get_compiled_block_from_storage;
    using taiyin::internal::kepler_ephemeris_data_destroy;
    using taiyin::internal::make_elliptic_kepler_elements;

    KeplerElements element = make_circular_element();

    StorageEphemerisBlock storage;
    expect_true(
        compile_kepler_ephemeris_block(&element, 1, 2451545.0 - 100.0, 2451545.0 + 100.0, &storage),
        "compile circular kepler block");
    CompiledEphemerisBlock block;
    get_compiled_block_from_storage(&storage, 0, &block);
    expect_true(block.format == EphemerisBlockFormat::Kepler, "kepler format");
    expect_true(block.data && block.position && block.velocity && block.acceleration && block.bytes > 0, "kepler block fields");

    CartesianState state;
    expect_true(eval_compiled_ephemeris_block(2451545.0, &block, &state), "eval circular kepler epoch");
    expect_near(state.position_au.x, 1.0, 1e-14, "epoch position x");
    expect_near(state.position_au.y, 0.0, 1e-14, "epoch position y");
    expect_near(state.velocity_au_per_day.x, 0.0, 1e-14, "epoch velocity x");
    expect_near(state.velocity_au_per_day.y, taiyin::TAIYIN_GAUSSIAN_GRAVITATIONAL_CONSTANT, 1e-14, "epoch velocity y");
    expect_near(state.acceleration_au_per_day2.x, -taiyin::TAIYIN_SOLAR_MU_AU3_DAY2, 1e-14, "epoch acceleration x");

    const double quarter_period_days = (2.0 * taiyin::TAIYIN_PI / taiyin::TAIYIN_GAUSSIAN_GRAVITATIONAL_CONSTANT) / 4.0;
    expect_true(
        eval_compiled_ephemeris_block(2451545.0 + quarter_period_days, &block, &state),
        "eval circular kepler quarter period");
    expect_near(state.position_au.x, 0.0, 1e-11, "quarter position x");
    expect_near(state.position_au.y, 1.0, 1e-11, "quarter position y");
    expect_near(state.velocity_au_per_day.x, -taiyin::TAIYIN_GAUSSIAN_GRAVITATIONAL_CONSTANT, 1e-11, "quarter velocity x");
    expect_near(state.velocity_au_per_day.y, 0.0, 1e-11, "quarter velocity y");

    expect_false(
        eval_compiled_ephemeris_block(2451545.0 + 120.0, &block, &state),
        "reject outside compiled range");
    destroy_storage_ephemeris_block(&storage);

    expect_false(
        compile_kepler_ephemeris_block(&element, 1, element.jd_tdb_start - 1.0, element.jd_tdb_start + 1.0, &storage),
        "reject requested range before element coverage");

    KeplerElements segmented[2];
    segmented[0] = make_circular_element();
    segmented[0].jd_tdb_start = 100.0;
    segmented[0].jd_tdb_end = 105.0;
    segmented[0].epoch_jd_tdb = 100.0;
    segmented[1] = segmented[0];
    segmented[1].jd_tdb_start = 105.0;
    segmented[1].jd_tdb_end = 110.0;

    taiyin::internal::KeplerEphemerisData* data = 0;
    expect_true(compile_kepler_ephemeris_data(segmented, 2, 101.0, 109.0, &data), "compile segmented data");
    expect_true(data->element_count == 2, "segmented data keeps two elements");
    expect_true(calc_kepler_state(106.0, data, &state), "eval second kepler segment");
    expect_false(calc_kepler_state(110.5, data, &state), "reject outside segmented runtime range");
    kepler_ephemeris_data_destroy(data);

    segmented[1].jd_tdb_start = 106.0;
    expect_false(compile_kepler_ephemeris_data(segmented, 2, 101.0, 109.0, &data), "reject segment gap");

    element.eccentricity = 1.0;
    expect_false(
        compile_kepler_ephemeris_block(&element, 1, 2451545.0 - 1.0, 2451545.0 + 1.0, &storage),
        "reject parabolic element in elliptical kepler block");

    KeplerElements bad_element;
    expect_false(
        make_elliptic_kepler_elements(
            2000001,
            10,
            2451545.0 + 1.0,
            2451545.0,
            2451545.0,
            taiyin::TAIYIN_SOLAR_MU_AU3_DAY2,
            1.0,
            0.0,
            0.0,
            0.0,
            0.0,
            0.0,
            &bad_element),
        "reject reversed helper range");
    expect_false(
        make_elliptic_kepler_elements(
            2000001,
            10,
            2451545.0 - 1.0,
            2451545.0 + 1.0,
            2451545.0,
            taiyin::TAIYIN_SOLAR_MU_AU3_DAY2,
            1.0,
            1.0,
            0.0,
            0.0,
            0.0,
            0.0,
            &bad_element),
        "reject parabolic helper element");

    check_elliptical_kepler_shape_and_energy();
    check_kepler_vs_de441_venus_osculating_drift();

    return 0;
}
