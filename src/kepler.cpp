#include "taiyin/internal/kepler.h"

#include "taiyin/angle.h"
#include "taiyin/internal/ephemeris_block.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <new>
#include <vector>

namespace taiyin {
namespace internal {
namespace {

const double RANGE_EPS_DAYS = 1e-9;
const int DEFAULT_ECCENTRIC_ANOMALY_MAX_ITERATIONS = 32;
const double DEFAULT_ECCENTRIC_ANOMALY_TOLERANCE_RAD = 1e-14;

bool finite_ordered_range(double start, double end) noexcept {
    return std::isfinite(start) && std::isfinite(end) && end >= start;
}

double normalize_angle(double angle) noexcept {
    double result = std::fmod(angle, TAIYIN_TWO_PI);
    if (result > TAIYIN_PI) {
        result -= TAIYIN_TWO_PI;
    } else if (result < -TAIYIN_PI) {
        result += TAIYIN_TWO_PI;
    }
    return result;
}

bool is_valid_element(const KeplerElements& element) noexcept {
    return finite_ordered_range(element.jd_tdb_start, element.jd_tdb_end)
        && element.jd_tdb_end > element.jd_tdb_start
        && std::isfinite(element.epoch_jd_tdb)
        && std::isfinite(element.mu_au3_day2)
        && std::isfinite(element.semi_major_axis_au)
        && std::isfinite(element.eccentricity)
        && std::isfinite(element.inclination_rad)
        && std::isfinite(element.longitude_ascending_node_rad)
        && std::isfinite(element.argument_periapsis_rad)
        && std::isfinite(element.mean_anomaly_at_epoch_rad)
        && element.mu_au3_day2 > 0.0
        && element.semi_major_axis_au > 0.0
        && element.eccentricity >= 0.0
        && element.eccentricity < 1.0;
}

bool elements_overlap_range(const KeplerElements& element, double start, double end) noexcept {
    return !(element.jd_tdb_end < start || element.jd_tdb_start > end);
}

bool solve_eccentric_anomaly(
    double mean_anomaly,
    double eccentricity,
    int max_iterations,
    double tolerance_rad,
    double* out_e
) noexcept {
    if (!out_e || max_iterations <= 0 || tolerance_rad <= 0.0) {
        return false;
    }

    double e_anomaly = eccentricity < 0.8 ? mean_anomaly : (mean_anomaly >= 0.0 ? TAIYIN_PI : -TAIYIN_PI);
    for (int i = 0; i < max_iterations; ++i) {
        const double sin_e = std::sin(e_anomaly);
        const double cos_e = std::cos(e_anomaly);
        const double f = e_anomaly - eccentricity * sin_e - mean_anomaly;
        const double fp = 1.0 - eccentricity * cos_e;
        if (fp == 0.0) {
            return false;
        }
        const double delta = f / fp;
        e_anomaly -= delta;
        if (std::fabs(delta) < tolerance_rad) {
            *out_e = e_anomaly;
            return true;
        }
    }

    *out_e = e_anomaly;
    return std::isfinite(e_anomaly);
}

void rotate_orbital_to_inertial(
    const KeplerElements& element,
    double x_orb,
    double y_orb,
    Vector3* out
) noexcept {
    const double cos_o = std::cos(element.longitude_ascending_node_rad);
    const double sin_o = std::sin(element.longitude_ascending_node_rad);
    const double cos_i = std::cos(element.inclination_rad);
    const double sin_i = std::sin(element.inclination_rad);
    const double cos_w = std::cos(element.argument_periapsis_rad);
    const double sin_w = std::sin(element.argument_periapsis_rad);

    const double m00 = cos_o * cos_w - sin_o * sin_w * cos_i;
    const double m01 = -cos_o * sin_w - sin_o * cos_w * cos_i;
    const double m10 = sin_o * cos_w + cos_o * sin_w * cos_i;
    const double m11 = -sin_o * sin_w + cos_o * cos_w * cos_i;
    const double m20 = sin_w * sin_i;
    const double m21 = cos_w * sin_i;

    out->x = m00 * x_orb + m01 * y_orb;
    out->y = m10 * x_orb + m11 * y_orb;
    out->z = m20 * x_orb + m21 * y_orb;
}

const KeplerElements* find_element(double jd_tdb, const KeplerEphemerisData* data) noexcept {
    const KeplerElements* elements = data->get_elements();
    for (size_t i = 0; i < data->element_count; ++i) {
        if (jd_tdb >= elements[i].jd_tdb_start && jd_tdb <= elements[i].jd_tdb_end) {
            return &elements[i];
        }
    }
    return 0;
}

}  // namespace

bool kepler_allocated_bytes(size_t element_count, size_t* out_bytes) noexcept {
    if (!out_bytes) {
        return false;
    }
    if (element_count > (std::numeric_limits<size_t>::max() - sizeof(KeplerEphemerisData)) / sizeof(KeplerElements)) {
        return false;
    }
    *out_bytes = sizeof(KeplerEphemerisData) + element_count * sizeof(KeplerElements);
    return true;
}

bool make_elliptic_kepler_elements(
    int target_id,
    int center_id,
    double jd_tdb_start,
    double jd_tdb_end,
    double epoch_jd_tdb,
    double mu_au3_day2,
    double semi_major_axis_au,
    double eccentricity,
    double inclination_rad,
    double longitude_ascending_node_rad,
    double argument_periapsis_rad,
    double mean_anomaly_at_epoch_rad,
    KeplerElements* out
) noexcept {
    if (!out) {
        return false;
    }

    KeplerElements element;
    element.target_id = target_id;
    element.center_id = center_id;
    element.jd_tdb_start = jd_tdb_start;
    element.jd_tdb_end = jd_tdb_end;
    element.epoch_jd_tdb = epoch_jd_tdb;
    element.mu_au3_day2 = mu_au3_day2;
    element.semi_major_axis_au = semi_major_axis_au;
    element.eccentricity = eccentricity;
    element.inclination_rad = inclination_rad;
    element.longitude_ascending_node_rad = longitude_ascending_node_rad;
    element.argument_periapsis_rad = argument_periapsis_rad;
    element.mean_anomaly_at_epoch_rad = mean_anomaly_at_epoch_rad;

    if (!is_valid_element(element)) {
        return false;
    }

    *out = element;
    return true;
}

KeplerEphemerisData* kepler_ephemeris_data_create(size_t element_count) noexcept {
    if (element_count == 0) {
        return 0;
    }

    size_t bytes = 0;
    if (!kepler_allocated_bytes(element_count, &bytes)) {
        return 0;
    }

    void* raw = ::operator new(bytes, std::nothrow);
    if (!raw) {
        return 0;
    }
    std::memset(raw, 0, bytes);

    KeplerEphemerisData* data = new (raw) KeplerEphemerisData();
    data->element_count = element_count;
    data->eccentric_anomaly_max_iterations = DEFAULT_ECCENTRIC_ANOMALY_MAX_ITERATIONS;
    data->eccentric_anomaly_tolerance_rad = DEFAULT_ECCENTRIC_ANOMALY_TOLERANCE_RAD;
    return data;
}

void kepler_ephemeris_data_destroy(KeplerEphemerisData* data) noexcept {
    if (!data) {
        return;
    }
    data->~KeplerEphemerisData();
    ::operator delete(data);
}

void kepler_ephemeris_data_destroy_void(void* data) noexcept {
    kepler_ephemeris_data_destroy(static_cast<KeplerEphemerisData*>(data));
}

bool compile_kepler_ephemeris_data(
    const KeplerElements* elements,
    size_t element_count,
    double jd_tdb_start,
    double jd_tdb_end,
    KeplerEphemerisData** out
) noexcept {
    if (!elements || element_count == 0 || !out || !finite_ordered_range(jd_tdb_start, jd_tdb_end)) {
        return false;
    }
    *out = 0;

    std::vector<KeplerElements> selected;
    selected.reserve(element_count);
    for (size_t i = 0; i < element_count; ++i) {
        if (!is_valid_element(elements[i])) {
            return false;
        }
        if (elements_overlap_range(elements[i], jd_tdb_start, jd_tdb_end)) {
            if (!selected.empty()
                && (elements[i].target_id != selected[0].target_id
                    || elements[i].center_id != selected[0].center_id)) {
                return false;
            }
            selected.push_back(elements[i]);
        }
    }
    if (selected.empty()) {
        return false;
    }

    std::sort(selected.begin(), selected.end(), [](const KeplerElements& lhs, const KeplerElements& rhs) {
        if (lhs.jd_tdb_start != rhs.jd_tdb_start) {
            return lhs.jd_tdb_start < rhs.jd_tdb_start;
        }
        return lhs.jd_tdb_end < rhs.jd_tdb_end;
    });

    if (selected[0].jd_tdb_start > jd_tdb_start + RANGE_EPS_DAYS) {
        return false;
    }

    double covered_until = selected[0].jd_tdb_end;
    for (size_t i = 1; i < selected.size() && covered_until < jd_tdb_end - RANGE_EPS_DAYS; ++i) {
        if (selected[i].jd_tdb_start > covered_until + RANGE_EPS_DAYS) {
            return false;
        }
        if (selected[i].jd_tdb_end > covered_until) {
            covered_until = selected[i].jd_tdb_end;
        }
    }
    if (covered_until < jd_tdb_end - RANGE_EPS_DAYS) {
        return false;
    }

    KeplerEphemerisData* data = kepler_ephemeris_data_create(selected.size());
    if (!data) {
        return false;
    }

    data->target_id = selected[0].target_id;
    data->center_id = selected[0].center_id;
    data->jd_tdb_start = jd_tdb_start;
    data->jd_tdb_end = jd_tdb_end;
    std::memcpy(data->get_elements(), &selected[0], selected.size() * sizeof(KeplerElements));
    *out = data;
    return true;
}

bool calc_kepler_position_void(double jd_tdb, const void* data, Vector3* out) noexcept {
    if (!out) {
        return false;
    }
    CartesianState state;
    if (!calc_kepler_state_void(jd_tdb, data, &state)) {
        return false;
    }
    *out = state.position_au;
    return true;
}

bool calc_kepler_velocity_void(double jd_tdb, const void* data, Vector3* out) noexcept {
    if (!out) {
        return false;
    }
    CartesianState state;
    if (!calc_kepler_state_void(jd_tdb, data, &state)) {
        return false;
    }
    *out = state.velocity_au_per_day;
    return true;
}

bool calc_kepler_acceleration_void(double jd_tdb, const void* data, Vector3* out) noexcept {
    if (!out) {
        return false;
    }
    CartesianState state;
    if (!calc_kepler_state_void(jd_tdb, data, &state)) {
        return false;
    }
    *out = state.acceleration_au_per_day2;
    return true;
}

bool compile_kepler_ephemeris_block(
    const KeplerElements* elements,
    size_t element_count,
    double jd_tdb_start,
    double jd_tdb_end,
    StorageEphemerisBlock* out
) noexcept {
    if (!out) {
        return false;
    }
    *out = StorageEphemerisBlock();

    KeplerEphemerisData* data = 0;
    if (!compile_kepler_ephemeris_data(elements, element_count, jd_tdb_start, jd_tdb_end, &data)) {
        return false;
    }

    out->cache_id = 0;
    out->format = EphemerisBlockFormat::Kepler;
    out->position = calc_kepler_position_void;
    out->velocity = calc_kepler_velocity_void;
    out->acceleration = calc_kepler_acceleration_void;
    out->destroy_element = kepler_ephemeris_data_destroy_void;
    out->data_vector.push_back(data);
    out->total_bytes = data->get_total_allocated_bytes();
    return true;
}

bool calc_kepler_state(
    double jd_tdb,
    const KeplerEphemerisData* data,
    int eccentric_anomaly_max_iterations,
    double eccentric_anomaly_tolerance_rad,
    CartesianState* out
) noexcept {
    if (!data || !out || !std::isfinite(jd_tdb) || eccentric_anomaly_max_iterations <= 0 || eccentric_anomaly_tolerance_rad <= 0.0) {
        return false;
    }
    if (jd_tdb < data->jd_tdb_start || jd_tdb > data->jd_tdb_end) {
        return false;
    }

    const KeplerElements* element = find_element(jd_tdb, data);
    if (!element) {
        return false;
    }

    const double a = element->semi_major_axis_au;
    const double e = element->eccentricity;
    const double n = std::sqrt(element->mu_au3_day2 / (a * a * a));
    const double mean_anomaly = normalize_angle(
        element->mean_anomaly_at_epoch_rad + n * (jd_tdb - element->epoch_jd_tdb));

    double eccentric_anomaly = 0.0;
    if (!solve_eccentric_anomaly(
            mean_anomaly,
            e,
            eccentric_anomaly_max_iterations,
            eccentric_anomaly_tolerance_rad,
            &eccentric_anomaly)) {
        return false;
    }

    const double cos_e = std::cos(eccentric_anomaly);
    const double sin_e = std::sin(eccentric_anomaly);
    const double one_minus_e_cos_e = 1.0 - e * cos_e;
    if (one_minus_e_cos_e <= 0.0) {
        return false;
    }

    const double sqrt_one_minus_e2 = std::sqrt(1.0 - e * e);
    const double x_orb = a * (cos_e - e);
    const double y_orb = a * sqrt_one_minus_e2 * sin_e;
    const double d_e_dt = n / one_minus_e_cos_e;
    const double vx_orb = -a * sin_e * d_e_dt;
    const double vy_orb = a * sqrt_one_minus_e2 * cos_e * d_e_dt;

    rotate_orbital_to_inertial(*element, x_orb, y_orb, &out->position_au);
    rotate_orbital_to_inertial(*element, vx_orb, vy_orb, &out->velocity_au_per_day);

    const double r2 =
        out->position_au.x * out->position_au.x
        + out->position_au.y * out->position_au.y
        + out->position_au.z * out->position_au.z;
    if (r2 <= 0.0) {
        return false;
    }

    const double inv_r3 = 1.0 / (r2 * std::sqrt(r2));
    const double accel_scale = -element->mu_au3_day2 * inv_r3;
    out->acceleration_au_per_day2.x = accel_scale * out->position_au.x;
    out->acceleration_au_per_day2.y = accel_scale * out->position_au.y;
    out->acceleration_au_per_day2.z = accel_scale * out->position_au.z;
    return true;
}

bool calc_kepler_state(
    double jd_tdb,
    const KeplerEphemerisData* data,
    CartesianState* out
) noexcept {
    if (!data) {
        return false;
    }
    return calc_kepler_state(
        jd_tdb,
        data,
        data->eccentric_anomaly_max_iterations,
        data->eccentric_anomaly_tolerance_rad,
        out);
}

bool calc_kepler_state_void(
    double jd_tdb,
    const void* data,
    CartesianState* out
) noexcept {
    return calc_kepler_state(jd_tdb, static_cast<const KeplerEphemerisData*>(data), out);
}

}  // namespace internal
}  // namespace taiyin
