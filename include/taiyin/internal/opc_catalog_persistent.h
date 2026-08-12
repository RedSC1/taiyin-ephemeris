#ifndef TAIYIN_INTERNAL_OPC_CATALOG_PERSISTENT_H
#define TAIYIN_INTERNAL_OPC_CATALOG_PERSISTENT_H

#include "ephemeris_catalog.h"
#include "ephemeris_discovery.h"

#include <cstdint>
#include <string>
#include <vector>

namespace taiyin {
namespace internal {

const uint32_t OPC_VERSION = 3;
// Bump when discovery semantics change even if the packed record schema does
// not. Cached descriptor sets from older discovery implementations must then
// be rebuilt from their source files.
const uint32_t OPC_DISCOVERY_VERSION = 4;
const uint64_t OPC_FINGERPRINT_EMPTY = 14695981039346656037ULL;

#pragma pack(push, 1)
struct OpcHeader {
    char magic[4];
    uint32_t version;
    uint32_t flags;
    uint32_t descriptor_count;
    uint64_t descriptor_records_offset;
    uint64_t string_table_offset;
    uint64_t string_table_size;
    uint64_t fingerprint;
    uint64_t source_id;
    uint32_t source_version;
    uint32_t generation;
    char reserved[64];
};

struct OpcDescriptorRecord {
    int32_t target_id;
    int32_t center_id;
    int32_t method_id;
    int32_t frame_id;
    double jd_tdb_start;
    double jd_tdb_end;
    uint64_t source_id;
    uint64_t block_id;
    uint32_t generation;
    uint32_t purpose;
    int32_t bucket_id;
    uint32_t format;
    uint32_t path_offset;
    uint32_t cache_policy_kind;
    uint64_t file_size;
    int64_t file_mtime_sec;
    int64_t file_mtime_nsec;
    double cache_origin_jd;
    double cache_span_days;
    int64_t cache_first_index;
    uint64_t cache_count;
    uint32_t object_index;
    uint32_t reserved;
};
#pragma pack(pop)

bool compute_opc_catalog_fingerprint(
    const std::string& root,
    uint64_t* out
) noexcept;

bool load_opc_persistent_catalog(
    const std::string& catalog_path,
    const std::string& root,
    std::vector<EphemerisBlockDescriptor>* out
) noexcept;

bool write_opc_persistent_catalog(
    const std::string& catalog_path,
    const std::string& root,
    const std::vector<EphemerisBlockDescriptor>& descriptors
) noexcept;

bool collect_ephemeris_descriptors_from_catalog_or_directory(
    const std::string& root,
    const std::string& catalog_path,
    const std::vector<EphemerisDiscoverFileFn>& discoverers,
    const EphemerisDiscoveryOptions& options,
    std::vector<EphemerisBlockDescriptor>* out
) noexcept;

bool discover_ephemeris_catalog_from_catalog_or_directory(
    const std::string& root,
    const std::string& catalog_path,
    const std::vector<EphemerisDiscoverFileFn>& discoverers,
    const EphemerisDiscoveryOptions& options,
    EphemerisBlockCatalog* out
) noexcept;

}  // namespace internal
}  // namespace taiyin

#endif  // TAIYIN_INTERNAL_OPC_CATALOG_PERSISTENT_H
