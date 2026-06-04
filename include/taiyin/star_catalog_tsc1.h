#ifndef TAIYIN_STAR_CATALOG_TSC1_H
#define TAIYIN_STAR_CATALOG_TSC1_H

#include "vector3.h"
#include "internal/ephemeris_cache.h"
#include "internal/mapped_file.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>

namespace taiyin {

static const uint32_t TSC1_VERSION = 1;
static const int TSC1_STAR_METHOD_ID = 1001;
static const int TSC1_DEFAULT_CENTER_ID = 0;
static const double TSC1_DEFAULT_JD_TDB_START = 2000000.0;
static const double TSC1_DEFAULT_JD_TDB_END = 3000000.0;

#pragma pack(push, 1)
struct Tsc1Header {
    char magic[4];
    uint32_t version;
    uint32_t flags;
    uint32_t star_count;
    uint32_t alias_count;
    uint64_t star_records_offset;
    uint64_t alias_records_offset;
    uint64_t string_table_offset;
    uint64_t string_table_size;
    double catalog_min_epoch;
    double catalog_max_epoch;
    char reserved[64];
};

struct Tsc1StarRecord {
    uint32_t canonical_id_offset;
    uint32_t display_name_offset;
    uint64_t gaia_dr3_source_id;
    uint32_t hip_id;
    uint32_t hr_id;
    uint32_t hd_id;
    double ra_deg;
    double dec_deg;
    double pm_ra_mas_yr;
    double pm_dec_mas_yr;
    double parallax_mas;
    double radial_velocity_km_s;
    double reference_epoch;
    float magnitude;
    uint16_t astrometry_source;
    uint16_t flags;
};

struct Tsc1AliasEntry {
    uint32_t alias_offset;
    uint32_t star_index;
    uint64_t alias_hash;
};
#pragma pack(pop)

enum Tsc1AstrometrySource {
    TSC1_SOURCE_UNKNOWN = 0,
    TSC1_SOURCE_GAIA_DR3 = 1,
    TSC1_SOURCE_HIPPARCOS = 2,
    TSC1_SOURCE_BSC5 = 3,
    TSC1_SOURCE_MANUAL = 4
};

enum Tsc1StarFlags {
    TSC1_STAR_HAS_GAIA_ID = 1 << 0,
    TSC1_STAR_HAS_HIP_ID = 1 << 1,
    TSC1_STAR_HAS_HR_ID = 1 << 2,
    TSC1_STAR_HAS_HD_ID = 1 << 3,
    TSC1_STAR_HAS_RADIAL_VELOCITY = 1 << 4,
    TSC1_STAR_HAS_PARALLAX = 1 << 5,
    TSC1_STAR_SPECIAL_DIRECTION = 1 << 6
};

struct Tsc1StarEphemerisData {
    Vector3 position_ref_au;
    Vector3 velocity_ref_au_per_day;
    double reference_jd_tdb;
    uint32_t star_index;
    uint16_t astrometry_source;
    uint16_t flags;

    Tsc1StarEphemerisData();
};

struct Tsc1ResolvedStar {
    uint32_t star_index;
    int star_id;
    const Tsc1StarRecord* record;

    Tsc1ResolvedStar();
};

struct Tsc1Catalog {
    const uint8_t* data;
    size_t byte_count;
    const Tsc1Header* header;
    const Tsc1StarRecord* stars;
    const Tsc1AliasEntry* aliases;
    const char* strings;
    size_t string_table_size;

    internal::MappedFile file;

    Tsc1Catalog();
};

uint64_t tsc1_fnv1a_64(const std::string& value) noexcept;
std::string tsc1_normalize_alias(const std::string& value);

bool tsc1_catalog_load_from_memory(Tsc1Catalog* catalog, const uint8_t* data, size_t size) noexcept;
bool tsc1_catalog_load_from_file(Tsc1Catalog* catalog, const std::string& path) noexcept;
void tsc1_catalog_destroy(Tsc1Catalog* catalog) noexcept;

const Tsc1StarRecord* tsc1_catalog_star(const Tsc1Catalog* catalog, uint32_t index) noexcept;
const Tsc1StarRecord* tsc1_catalog_find(const Tsc1Catalog* catalog, const std::string& id_or_alias) noexcept;
bool tsc1_catalog_find_index(const Tsc1Catalog* catalog, const std::string& id_or_alias, uint32_t* out_index) noexcept;
const char* tsc1_catalog_string(const Tsc1Catalog* catalog, uint32_t offset) noexcept;

bool tsc1_compile_star_ephemeris_data(
    const Tsc1StarRecord* record,
    uint32_t star_index,
    Tsc1StarEphemerisData** out
) noexcept;

bool tsc1_compile_star_storage_block(
    const Tsc1Catalog* catalog,
    uint32_t star_index,
    int star_id,
    internal::StorageEphemerisBlock* out
) noexcept;

bool calc_tsc1_star_position(double jd_tdb, const void* data, Vector3* out_position_au) noexcept;
bool calc_tsc1_star_velocity(double jd_tdb, const void* data, Vector3* out_velocity_au_per_day) noexcept;
bool calc_tsc1_star_acceleration(double jd_tdb, const void* data, Vector3* out_acceleration_au_per_day2) noexcept;
void destroy_tsc1_star_ephemeris_data(void* data) noexcept;

class Tsc1StarProvider {
public:
    Tsc1StarProvider(size_t cache_max_bytes = 1024 * 1024, int catalog_id = 1) noexcept;
    ~Tsc1StarProvider() noexcept;

    Tsc1StarProvider(const Tsc1StarProvider&) = delete;
    Tsc1StarProvider& operator=(const Tsc1StarProvider&) = delete;

    bool load_from_memory(const uint8_t* data, size_t size) noexcept;
    bool load_from_file(const std::string& path) noexcept;
    void close() noexcept;

    bool resolve(const std::string& id_or_alias, Tsc1ResolvedStar* out) noexcept;
    bool eval_position(const std::string& id_or_alias, double jd_tdb, Vector3* out) noexcept;
    bool eval_velocity(const std::string& id_or_alias, double jd_tdb, Vector3* out) noexcept;
    bool eval_acceleration(const std::string& id_or_alias, double jd_tdb, Vector3* out) noexcept;
    bool eval_state(const std::string& id_or_alias, double jd_tdb, CartesianState* out) noexcept;

    bool contains_cached_star(const std::string& id_or_alias) noexcept;
    size_t cache_entry_count() const noexcept;
    size_t cache_total_bytes() const noexcept;

    const Tsc1Catalog* catalog() const noexcept;
    int catalog_id() const noexcept;

private:
    bool ensure_cached(uint32_t star_index, int star_id) noexcept;
    int resolve_or_register_star_id(uint32_t star_index, const Tsc1StarRecord* record) noexcept;
    internal::EphemerisRouteKey route_key(int star_id) const noexcept;

    Tsc1Catalog catalog_;
    internal::EphemerisBlockCache cache_;
    int catalog_id_;
    double jd_tdb_start_;
    double jd_tdb_end_;
    std::unordered_map<uint32_t, int> star_id_by_index_;
};

}  // namespace taiyin

#endif  // TAIYIN_STAR_CATALOG_TSC1_H
