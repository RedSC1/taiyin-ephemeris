#ifndef TAIYIN_INTERNAL_KEPLER_CATALOG_TKC1_H
#define TAIYIN_INTERNAL_KEPLER_CATALOG_TKC1_H

#include "ephemeris_catalog.h"
#include "kepler.h"
#include "mapped_file.h"

#include <cstddef>
#include <cstdint>
#include <string>

namespace taiyin {
namespace internal {

const uint32_t TKC1_VERSION = 1;
const int TKC1_KEPLER_METHOD_ID = 3003;
const uint64_t TKC1_SOURCE_ID = 6;
const uint32_t TKC1_GENERATION = 1;
const uint32_t TKC1_PURPOSE = 0;

const uint32_t TKC1_OBJECT_NUMBERED = 1u << 0;
const uint32_t TKC1_OBJECT_NAMED = 1u << 1;
const uint32_t TKC1_OBJECT_NEO = 1u << 2;
const uint32_t TKC1_OBJECT_PHA = 1u << 3;
const uint32_t TKC1_OBJECT_COMET = 1u << 4;

#pragma pack(push, 1)
struct Tkc1Header {
    char magic[4];
    uint32_t version;
    uint32_t flags;
    uint32_t object_count;
    uint32_t element_count;
    uint32_t alias_count;
    uint32_t reserved_count;
    uint64_t object_records_offset;
    uint64_t element_records_offset;
    uint64_t alias_records_offset;
    uint64_t string_table_offset;
    uint64_t string_table_size;
    double catalog_jd_tdb_start;
    double catalog_jd_tdb_end;
    uint64_t source_id;
    uint32_t source_version;
    uint32_t generation;
    char reserved[96];
};

struct Tkc1ObjectRecord {
    int32_t target_id;
    int32_t center_id;
    int32_t method_id;
    int32_t frame_id;
    double jd_tdb_start;
    double jd_tdb_end;
    uint32_t element_start_index;
    uint32_t element_count;
    uint32_t canonical_name_offset;
    uint32_t display_name_offset;
    uint64_t sbdb_spkid;
    uint32_t small_body_number;
    uint32_t flags;
};

struct Tkc1ElementRecord {
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

struct Tkc1AliasRecord {
    uint32_t alias_offset;
    uint32_t object_index;
    uint64_t alias_hash;
};
#pragma pack(pop)

struct Tkc1Catalog {
    const uint8_t* data;
    size_t byte_count;
    const Tkc1Header* header;
    const Tkc1ObjectRecord* objects;
    const Tkc1ElementRecord* elements;
    const Tkc1AliasRecord* aliases;
    const char* strings;
    size_t string_table_size;

    MappedFile file;

    Tkc1Catalog();
};

uint64_t tkc1_fnv1a_64(const std::string& value) noexcept;
std::string tkc1_normalize_alias(const std::string& value);

bool tkc1_catalog_load_from_memory(Tkc1Catalog* catalog, const uint8_t* data, size_t size) noexcept;
bool tkc1_catalog_load_from_file(Tkc1Catalog* catalog, const std::string& path) noexcept;
void tkc1_catalog_destroy(Tkc1Catalog* catalog) noexcept;

const Tkc1ObjectRecord* tkc1_catalog_object(const Tkc1Catalog* catalog, uint32_t index) noexcept;
const Tkc1ElementRecord* tkc1_catalog_element(const Tkc1Catalog* catalog, uint32_t index) noexcept;
const char* tkc1_catalog_string(const Tkc1Catalog* catalog, uint32_t offset) noexcept;

bool tkc1_catalog_find_object_index_by_target_id(
    const Tkc1Catalog* catalog,
    int target_id,
    uint32_t* out_index
) noexcept;

bool tkc1_catalog_find_object_index_by_alias(
    const Tkc1Catalog* catalog,
    const std::string& alias,
    uint32_t* out_index
) noexcept;

bool tkc1_make_descriptor_for_object(
    const std::string& path,
    const Tkc1Catalog* catalog,
    uint32_t object_index,
    EphemerisBlockDescriptor* out
) noexcept;

bool tkc1_compile_object_storage_block(
    const Tkc1Catalog* catalog,
    uint32_t object_index,
    double jd_tdb_start,
    double jd_tdb_end,
    StorageEphemerisBlock* out
) noexcept;

bool tkc1_compile_object_storage_block_from_file(
    const std::string& path,
    uint32_t object_index,
    double jd_tdb_start,
    double jd_tdb_end,
    StorageEphemerisBlock* out
) noexcept;

}  // namespace internal
}  // namespace taiyin

#endif  // TAIYIN_INTERNAL_KEPLER_CATALOG_TKC1_H
