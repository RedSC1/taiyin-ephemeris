#ifndef TAIYIN_LUNAR_LIMB_TLL1_H
#define TAIYIN_LUNAR_LIMB_TLL1_H

#include "taiyin/internal/mapped_file.h"
#include "taiyin/status.h"

#include <cstddef>
#include <cstdint>
#include <string>

namespace taiyin {

static const uint16_t TLL1_VERSION = 1;
static const uint32_t TLL1_FLAG_LITTLE_ENDIAN = 1u << 0;
static const uint32_t TLL1_FLAG_SIGNED_INT16_OFFSETS = 1u << 1;
static const uint32_t TLL1_FLAG_KAGUYA_LALT_DERIVED = 1u << 2;
static const uint64_t TLL1_SOURCE_KAGUYA_LALT = 1;

#pragma pack(push, 1)
struct Tll1Header {
    char magic[4];
    uint16_t version;
    uint16_t header_size;
    uint32_t flags;
    uint32_t longitude_count;
    uint32_t latitude_count;
    uint32_t position_angle_count;
    uint32_t reserved_count;
    uint32_t reserved_word;
    uint64_t payload_offset;
    uint64_t payload_size;
    double longitude_start_deg;
    double longitude_step_deg;
    double latitude_start_deg;
    double latitude_step_deg;
    double position_angle_start_deg;
    double position_angle_step_deg;
    double reference_radius_m;
    double mean_distance_m;
    double offset_scale_m;
    uint64_t source_id;
    uint32_t source_version;
    uint32_t generation;
    char reserved[56];
};
#pragma pack(pop)

struct Tll1LunarLimbModel {
    const uint8_t* data;
    size_t byte_count;
    const Tll1Header* header;
    const int16_t* offsets;
    internal::MappedFile file;

    Tll1LunarLimbModel() noexcept;
};

// A model loaded from memory borrows data; the byte buffer must outlive the
// model and every NativeCalcContext calculation that refers to it.
Status tll1_lunar_limb_load_from_memory(
    Tll1LunarLimbModel* model,
    const uint8_t* data,
    size_t size
) noexcept;

Status tll1_lunar_limb_load_from_file(
    Tll1LunarLimbModel* model,
    const std::string& path
) noexcept;

void tll1_lunar_limb_destroy(Tll1LunarLimbModel* model) noexcept;

Status tll1_lunar_limb_offset_m(
    const Tll1LunarLimbModel* model,
    double libration_longitude_deg,
    double libration_latitude_deg,
    double position_angle_deg,
    double* out_offset_m
) noexcept;

Status tll1_lunar_limb_radius_m(
    const Tll1LunarLimbModel* model,
    double libration_longitude_deg,
    double libration_latitude_deg,
    double position_angle_deg,
    double* out_radius_m
) noexcept;

}  // namespace taiyin

#endif  // TAIYIN_LUNAR_LIMB_TLL1_H
