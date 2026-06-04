#ifndef TAIYIN_INTERNAL_OPM_CORE_H
#define TAIYIN_INTERNAL_OPM_CORE_H

#include "../state.h"
#include "../vector3.h"

#include <cstddef>
#include <cstdint>

namespace taiyin {
namespace internal {

struct OpmSegment {
    double jd_tdb_start;
    double jd_tdb_end;
    Vector3 u;
    Vector3 v;
    Vector3 w;
    size_t coeff_offset;
};

struct OpmCompileRange {
    bool enabled;
    double jd_tdb_start;
    double jd_tdb_end;

    OpmCompileRange()
        : enabled(false),
          jd_tdb_start(0.0),
          jd_tdb_end(0.0) {}

    OpmCompileRange(double start, double end)
        : enabled(true),
          jd_tdb_start(start),
          jd_tdb_end(end) {}
};

bool opm_allocated_bytes(size_t segment_count, size_t total_coeffs, size_t* out_bytes) noexcept;

struct OpmEphemerisData {
    int body_id;
    double jd_tdb_start;
    double jd_tdb_end;
    double quant_unit_km;
    uint8_t degree_xy;
    uint8_t degree_z;
    size_t segment_count;
    size_t total_coeffs;

    OpmSegment* get_segments() {
        return reinterpret_cast<OpmSegment*>(this + 1);
    }

    const OpmSegment* get_segments() const {
        return reinterpret_cast<const OpmSegment*>(this + 1);
    }

    int32_t* get_coeff_pool() {
        return reinterpret_cast<int32_t*>(get_segments() + segment_count);
    }

    const int32_t* get_coeff_pool() const {
        return reinterpret_cast<const int32_t*>(get_segments() + segment_count);
    }

    size_t get_total_allocated_bytes() const {
        size_t bytes = 0;
        opm_allocated_bytes(segment_count, total_coeffs, &bytes);
        return bytes;
    }
};

bool opm_reference_allocated_bytes(size_t coeff_count, size_t* out_bytes) noexcept;

struct OpmReferenceData {
    uint8_t degree_xy;
    double quant_unit_km;
    size_t coeff_count;

    int32_t* get_ref_cx_int() {
        return reinterpret_cast<int32_t*>(this + 1);
    }

    const int32_t* get_ref_cx_int() const {
        return reinterpret_cast<const int32_t*>(this + 1);
    }

    int32_t* get_ref_cy_int() {
        return get_ref_cx_int() + coeff_count;
    }

    const int32_t* get_ref_cy_int() const {
        return get_ref_cx_int() + coeff_count;
    }

    size_t get_total_allocated_bytes() const {
        size_t bytes = 0;
        opm_reference_allocated_bytes(coeff_count, &bytes);
        return bytes;
    }
};

OpmEphemerisData* opm_ephemeris_data_create(size_t segment_count, size_t total_coeffs) noexcept;
void opm_ephemeris_data_destroy(OpmEphemerisData* data) noexcept;
void opm_ephemeris_data_destroy_void(void* data) noexcept;

OpmReferenceData* opm_reference_data_create(size_t coeff_count) noexcept;
void opm_reference_data_destroy(OpmReferenceData* data) noexcept;
void opm_reference_data_destroy_void(void* data) noexcept;

bool compile_opm_segment_stream(
    const void* bytes,
    size_t byte_count,
    int body_id,
    double jd_tdb_start,
    double jd_tdb_end,
    double quant_unit_km,
    size_t segment_count,
    uint8_t degree_xy,
    uint8_t degree_z,
    const OpmCompileRange& range,
    OpmEphemerisData** out
) noexcept;

bool calc_opm_state(
    double jd_tdb,
    const OpmEphemerisData* data,
    const OpmReferenceData* reference,
    CartesianState* out
) noexcept;

struct OpmEvalData {
    const OpmEphemerisData* ephemeris;
    const OpmReferenceData* reference;
};

bool calc_opm_eval_data_state(double jd_tdb, const OpmEvalData* data, CartesianState* out) noexcept;
bool calc_opm_eval_data_state_void(double jd_tdb, const void* data, CartesianState* out) noexcept;
bool calc_opm_state_void(double jd_tdb, const void* data, CartesianState* out) noexcept;

}  // namespace internal
}  // namespace taiyin

#endif  // TAIYIN_INTERNAL_OPM_CORE_H
