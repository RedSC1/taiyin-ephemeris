#ifndef TAIYIN_STARS_H
#define TAIYIN_STARS_H

#include "vector3.h"
#include "coordinates.h"
#include "internal/ephemeris_block.h"

#include <vector>
#include <string>
#include <unordered_map>
#include <cstdint>
#include <cstddef>

namespace taiyin {

// 1. Core physical star data structure used in math computations
struct StarData {
    double ra_j2000_deg;
    double dec_j2000_deg;
    double pm_ra_mas_yr;     // already includes cos(dec)
    double pm_dec_mas_yr;
    double parallax_mas;
    double radial_velocity_km_s;
};

// 2. Flat Binary Star Catalog Format Structures
#pragma pack(push, 1)
struct StarCatalogHeader {
    char magic[4];          // "TSCA"
    uint32_t version;       // 1
    uint32_t star_count;
    char reserved[20];
};

struct BinaryStarRecord {
    char id[32];
    char name[32];
    char aliases[64];       // Comma-separated list of aliases
    double magnitude;
    double ra_j2000_deg;
    double dec_j2000_deg;
    double pm_ra_mas_yr;
    double pm_dec_mas_yr;
    double parallax_mas;
    double radial_velocity_km_s;
};
#pragma pack(pop)

// 3. 恒星在 CompiledEphemerisBlock 中的专属底层数据
struct StarEphemerisData {
    Vector3 position_j2000_au;
    Vector3 velocity_j2000_au_per_day;
};

// 4. 扁平二进制星表 POD 结构体 (支持零拷贝内存映射)
struct StarCatalog {
    const BinaryStarRecord* records;
    size_t star_count;
    std::vector<uint8_t> owned_buffer;
    std::unordered_map<std::string, size_t> alias_map;
    std::unordered_map<int, size_t> id_to_index;
    internal::StorageEphemerisBlock storage_block;
};

// 5. 无状态星表加载与查找纯函数
bool star_catalog_load_from_memory(StarCatalog* catalog, const uint8_t* data, size_t size) noexcept;
bool star_catalog_find_star(const StarCatalog* catalog, const std::string& id_or_alias, StarData* out_data) noexcept;
bool star_catalog_find_star_by_id(const StarCatalog* catalog, int star_id, StarData* out_data) noexcept;
bool star_catalog_find_record(const StarCatalog* catalog, const std::string& id_or_alias, BinaryStarRecord* out_record) noexcept;
bool star_catalog_get_compiled_block(const StarCatalog* catalog, int star_id, internal::CompiledEphemerisBlock* out) noexcept;
void star_catalog_destroy(StarCatalog* catalog) noexcept;
const StarCatalog* get_builtin_star_catalog() noexcept;

// 6. 无状态评估回调纯函数 (符合 EphemerisPositionFn / EphemerisVelocityFn / EphemerisAccelerationFn)
bool calc_star_position(double jd_tdb, const void* data, Vector3* out_position_au) noexcept;
bool calc_star_velocity(double jd_tdb, const void* data, Vector3* out_velocity_au_per_day) noexcept;
bool calc_star_acceleration(double jd_tdb, const void* data, Vector3* out_acceleration_au_per_day2) noexcept;
void destroy_star_ephemeris_data(void* data) noexcept;



}  // namespace taiyin

#endif  // TAIYIN_STARS_H
