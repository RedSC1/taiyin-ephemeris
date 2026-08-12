#ifndef TAIYIN_INTERNAL_OPM2_H
#define TAIYIN_INTERNAL_OPM2_H

#include "ephemeris_block.h"
#include "ephemeris_source_identity.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace taiyin {
namespace internal {

const uint32_t OPM2_METHOD_ID = 1;
// Kept as a compatibility name for pre-provenance / unclassified OPM2 files.
const uint64_t OPM2_SOURCE_ID = OPM2_SOURCE_LEGACY;

const uint32_t OPM2_CONTAINER_VERSION = 1;
const uint32_t OPM2_HEADER_LEN = 28;
const uint32_t OPM2_SECTION_ENTRY_LEN = 32;
const uint32_t OPM2_SECTION_REQUIRED = 1u << 0;

const uint32_t OPM2_SEC_EPHE = 0x45485045u;
const uint32_t OPM2_SEC_GRID = 0x44495247u;
const uint32_t OPM2_SEC_DOMN = 0x4e4d4f44u;
const uint32_t OPM2_SEC_MODL = 0x4c444f4du;
const uint32_t OPM2_SEC_QNTB = 0x42544e51u;
const uint32_t OPM2_SEC_RCOF = 0x464f4352u;

const uint32_t OPM2_MODEL_RAW_XYZ_CHEB_V1 = 0x0001;
const uint32_t OPM2_MODEL_FIXED_FRAME_SHARED_SHAPE_V1 = 0x0002;
const uint32_t OPM2_MODEL_MEAN_APSIS_SHARED_SHAPE_V1 = 0x0003;
const uint32_t OPM2_MODEL_LUNAR_APSIS_SHARED_SHAPE_V1 = 0x0004;
const uint32_t OPM2_MODEL_LUNAR_FIXED_COEFF_REF_V1 = 0x0005;

const uint32_t OPM2_GRID_AFFINE_FIXED_SAFE_GLOBAL = 1;
const uint32_t OPM2_GRID_AFFINE_MEAN_PERIHELION = 2;
const uint32_t OPM2_GRID_AFFINE_MEAN_PERIGEE = 3;
const uint32_t OPM2_GRID_AFFINE_FIXED_MOON = 4;

const uint32_t OPM2_FRAME_BASIS_AFFINE_PLUS_SINCOS_PERIOD = 1;
const uint32_t OPM2_CORRECTION_NONE = 0;
const uint32_t OPM2_DOMAIN_AFFINE_EXPANSION_DAYS = 1;
const uint32_t OPM2_DOMAIN_EXPANSION_FRACTION = 2;
const uint32_t OPM2_TIME_SCALE_TDB = 1;
const uint32_t OPM2_FRAME_ECLIPTIC_J2000 = 1;
const uint32_t OPM2_QNTB_SHARED_AXIS_STEPS = 1u << 0;
const int OPM2_AXIS_COUNT = 3;

struct Opm2SectionEntry {
    uint32_t kind;
    uint16_t version;
    uint16_t flags;
    uint64_t offset;
    uint64_t length;
    uint32_t crc32;
    uint32_t reserved;

    Opm2SectionEntry();
};

struct Opm2EpheSection {
    int target_id;
    int center_id;
    uint32_t frame_id;
    uint32_t time_scale_id;
    uint32_t model_id;
    uint32_t storage_kind;
    uint32_t axis_count;
    double coverage_start_jd;
    double coverage_end_jd;

    Opm2EpheSection();
};

struct Opm2GridSection {
    uint32_t grid_kind;
    uint32_t correction_kind;
    int64_t first_segment_index;
    uint64_t segment_count;
    double origin_jd;
    double period_days;
    uint64_t correction_table_offset;
    uint64_t correction_table_size;

    Opm2GridSection();
};

struct Opm2DomainSection {
    uint32_t domain_kind;
    double left_expansion_days;
    double right_expansion_days;
    bool has_expansion_fraction;
    double expansion_fraction;

    Opm2DomainSection();
};

struct Opm2LunarCoeffRefModel {
    uint16_t degree;
    uint16_t axis_count;
    uint16_t frame_basis_kind;
    double frame_time_origin_jd;
    double frame_time_unit_days;
    double frame_period_years;
    double u_coeff[4];
    double v_coeff[4];
    double angle_coeff[4];
    std::vector<double> ref_coeffs;

    Opm2LunarCoeffRefModel();
};

struct Opm2EphemerisData {
    Opm2EpheSection ephe;
    Opm2GridSection grid;
    Opm2DomainSection domain;
    SplitJulianDate coverage_start_jd;
    SplitJulianDate coverage_end_jd;
    SplitJulianDate grid_origin_jd;
    std::vector<SplitJulianDate> segment_start_jds;
    std::vector<double> quant_steps;
    std::vector<int64_t> qcoeffs;
    std::vector<double> shape_x;
    std::vector<double> shape_y;
    std::vector<double> frame_coeffs;
    uint32_t frame_rows;
    double frame_first_mid_jd;
    double frame_last_mid_jd;
    std::vector<double> frame_params;
    Opm2LunarCoeffRefModel lunar_model;
    std::vector<double> lunar_params;
    size_t bytes;

    Opm2EphemerisData();
};

bool parse_opm2_summary(
    const void* bytes,
    size_t byte_count,
    Opm2EpheSection* ephe,
    Opm2GridSection* grid,
    uint32_t* source_id = 0
) noexcept;

bool compile_opm2_ephemeris_data(
    const void* bytes,
    size_t byte_count,
    Opm2EphemerisData** out
) noexcept;

bool compile_opm2_ephemeris_data_for_range(
    const void* bytes,
    size_t byte_count,
    double jd_tdb_start,
    double jd_tdb_end,
    Opm2EphemerisData** out
) noexcept;

void opm2_ephemeris_data_destroy(Opm2EphemerisData* data) noexcept;
void opm2_ephemeris_data_destroy_void(void* data) noexcept;
bool calc_opm2_position(const SplitJulianDate& jd_tdb, const Opm2EphemerisData* data, Vector3* out_position_au) noexcept;
bool calc_opm2_velocity(const SplitJulianDate& jd_tdb, const Opm2EphemerisData* data, Vector3* out_velocity_au_per_day) noexcept;
bool calc_opm2_position_velocity(
    const SplitJulianDate& jd_tdb,
    const Opm2EphemerisData* data,
    Vector3* out_position_au,
    Vector3* out_velocity_au_per_day
) noexcept;
bool calc_opm2_acceleration(const SplitJulianDate& jd_tdb, const Opm2EphemerisData* data, Vector3* out_acceleration_au_per_day2) noexcept;
bool calc_opm2_position_void(const SplitJulianDate& jd_tdb, const void* data, Vector3* out_position_au) noexcept;
bool calc_opm2_velocity_void(const SplitJulianDate& jd_tdb, const void* data, Vector3* out_velocity_au_per_day) noexcept;
bool calc_opm2_position_velocity_void(
    const SplitJulianDate& jd_tdb,
    const void* data,
    Vector3* out_position_au,
    Vector3* out_velocity_au_per_day
) noexcept;
bool calc_opm2_acceleration_void(const SplitJulianDate& jd_tdb, const void* data, Vector3* out_acceleration_au_per_day2) noexcept;
bool calc_opm2_state(const SplitJulianDate& jd_tdb, const Opm2EphemerisData* data, CartesianState* out) noexcept;
bool calc_opm2_state_void(const SplitJulianDate& jd_tdb, const void* data, CartesianState* out) noexcept;

}  // namespace internal
}  // namespace taiyin

#endif  // TAIYIN_INTERNAL_OPM2_H
