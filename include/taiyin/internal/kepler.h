#ifndef TAIYIN_INTERNAL_KEPLER_H
#define TAIYIN_INTERNAL_KEPLER_H

#include "../state.h"

#include <cstddef>

namespace taiyin {
namespace internal {

struct CompiledEphemerisBlock;
struct StorageEphemerisBlock;

struct KeplerElements {
    int target_id;
    int center_id;
    double jd_tdb_start;
    double jd_tdb_end;
    double epoch_jd_tdb;
    double mu_au3_day2;
    double semi_major_axis_au;
    double eccentricity;
    double inclination_rad;
    double longitude_ascending_node_rad;
    double argument_periapsis_rad;
    double mean_anomaly_at_epoch_rad;
};

bool kepler_allocated_bytes(size_t element_count, size_t* out_bytes) noexcept;

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
) noexcept;

struct KeplerEphemerisData {
    int target_id;
    int center_id;
    double jd_tdb_start;
    double jd_tdb_end;
    int eccentric_anomaly_max_iterations;
    double eccentric_anomaly_tolerance_rad;
    size_t element_count;

    KeplerElements* get_elements() {
        return reinterpret_cast<KeplerElements*>(this + 1);
    }

    const KeplerElements* get_elements() const {
        return reinterpret_cast<const KeplerElements*>(this + 1);
    }

    size_t get_total_allocated_bytes() const {
        size_t bytes = 0;
        kepler_allocated_bytes(element_count, &bytes);
        return bytes;
    }
};

KeplerEphemerisData* kepler_ephemeris_data_create(size_t element_count) noexcept;
void kepler_ephemeris_data_destroy(KeplerEphemerisData* data) noexcept;
void kepler_ephemeris_data_destroy_void(void* data) noexcept;

bool compile_kepler_ephemeris_data(
    const KeplerElements* elements,
    size_t element_count,
    double jd_tdb_start,
    double jd_tdb_end,
    KeplerEphemerisData** out
) noexcept;

bool compile_kepler_ephemeris_block(
    const KeplerElements* elements,
    size_t element_count,
    double jd_tdb_start,
    double jd_tdb_end,
    StorageEphemerisBlock* out
) noexcept;

bool calc_kepler_state(
    double jd_tdb,
    const KeplerEphemerisData* data,
    int eccentric_anomaly_max_iterations,
    double eccentric_anomaly_tolerance_rad,
    CartesianState* out
) noexcept;

bool calc_kepler_state(
    double jd_tdb,
    const KeplerEphemerisData* data,
    CartesianState* out
) noexcept;

bool calc_kepler_state_void(
    double jd_tdb,
    const void* data,
    CartesianState* out
) noexcept;

}  // namespace internal
}  // namespace taiyin

#endif  // TAIYIN_INTERNAL_KEPLER_H
