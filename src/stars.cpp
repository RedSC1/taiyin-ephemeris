#include "taiyin/stars.h"
#include "taiyin/angle.h"
#include "taiyin/physical_constants.h"

#include <cmath>
#include <fstream>
#include <new>

namespace taiyin {

// Size: 584 bytes
static const uint8_t BUILTIN_STAR_CATALOG_BYTES[] = {
    0x54, 0x53, 0x43, 0x41, 0x01, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x73, 0x70, 0x69, 0x63,
    0x61, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x53, 0x70, 0x69, 0x63, 0x61, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x61, 0x6c, 0x70, 0x68, 0x61, 0x5f, 0x76, 0x69, 0x72, 0x2c, 0x76, 0x69,
    0x72, 0x5f, 0x61, 0x6c, 0x70, 0x68, 0x61, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x5c, 0x8f, 0xc2, 0xf5, 0x28, 0x5c, 0xef, 0x3f,
    0xc5, 0x37, 0x14, 0x3e, 0x8b, 0x29, 0x69, 0x40, 0x5f, 0xe5, 0x40, 0x77,
    0x98, 0x52, 0x26, 0xc0, 0xcd, 0xcc, 0xcc, 0xcc, 0xcc, 0x2c, 0x45, 0xc0,
    0xec, 0x51, 0xb8, 0x1e, 0x85, 0xab, 0x3e, 0xc0, 0x1f, 0x85, 0xeb, 0x51,
    0xb8, 0x1e, 0x2a, 0x40, 0x7b, 0x14, 0xae, 0x47, 0xe1, 0x7a, 0x0a, 0xc0,
    0x67, 0x61, 0x6c, 0x61, 0x63, 0x74, 0x69, 0x63, 0x5f, 0x63, 0x65, 0x6e,
    0x74, 0x65, 0x72, 0x5f, 0x6a, 0x32, 0x30, 0x30, 0x30, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x47, 0x61, 0x6c, 0x61,
    0x63, 0x74, 0x69, 0x63, 0x20, 0x43, 0x65, 0x6e, 0x74, 0x65, 0x72, 0x20,
    0x28, 0x4a, 0x32, 0x30, 0x30, 0x30, 0x20, 0x64, 0x69, 0x72, 0x65, 0x63,
    0x74, 0x69, 0x6f, 0x6e, 0x67, 0x61, 0x6c, 0x61, 0x63, 0x74, 0x69, 0x63,
    0x5f, 0x63, 0x65, 0x6e, 0x74, 0x65, 0x72, 0x2c, 0x67, 0x63, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0xa5, 0x04, 0x61, 0x5d, 0xab, 0xa6, 0x70, 0x40,
    0xe8, 0x8f, 0x1f, 0xe0, 0xff, 0x01, 0x3d, 0xc0, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x73, 0x67, 0x72, 0x5f, 0x61, 0x5f, 0x61, 0x70,
    0x70, 0x61, 0x72, 0x65, 0x6e, 0x74, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x53, 0x61, 0x67, 0x69, 0x74, 0x74, 0x61, 0x72, 0x69, 0x75, 0x73, 0x20,
    0x41, 0x2a, 0x20, 0x41, 0x70, 0x70, 0x61, 0x72, 0x65, 0x6e, 0x74, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x73, 0x67, 0x72, 0x5f,
    0x61, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xa5, 0x04, 0x61, 0x5d,
    0xab, 0xa6, 0x70, 0x40, 0xe8, 0x8f, 0x1f, 0xe0, 0xff, 0x01, 0x3d, 0xc0,
    0x68, 0x91, 0xed, 0x7c, 0x3f, 0x35, 0x09, 0xc0, 0xe3, 0xa5, 0x9b, 0xc4,
    0x20, 0x30, 0x16, 0xc0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};

struct StarEphemerisData;
static StarEphemerisData* star_data_compile_kinematics(const StarData& star) noexcept;

static void star_catalog_rebuild_alias_map(StarCatalog* catalog) noexcept {
    catalog->alias_map.clear();
    catalog->id_to_index.clear();
    if (!catalog->records || catalog->star_count == 0) return;

    for (size_t i = 0; i < catalog->star_count; ++i) {
        const auto& rec = catalog->records[i];
        
        std::string id_str(rec.id);
        if (!id_str.empty()) {
            int star_id = internal::register_celestial_body(id_str);
            catalog->id_to_index[star_id] = i;
            catalog->alias_map[id_str] = i;

            std::string name_str(rec.name);
            if (!name_str.empty()) {
                catalog->alias_map[name_str] = i;
                internal::register_celestial_body_alias(name_str, star_id);
            }

            std::string aliases_str(rec.aliases);
            size_t start = 0;
            while (start < aliases_str.size()) {
                size_t comma = aliases_str.find(',', start);
                std::string alias;
                if (comma == std::string::npos) {
                    alias = aliases_str.substr(start);
                    start = aliases_str.size();
                } else {
                    alias = aliases_str.substr(start, comma - start);
                    start = comma + 1;
                }
                if (!alias.empty()) {
                    catalog->alias_map[alias] = i;
                    internal::register_celestial_body_alias(alias, star_id);
                }
            }
        }
    }
}

bool star_catalog_load_from_memory(StarCatalog* catalog, const uint8_t* data, size_t size) noexcept {
    if (!catalog || !data || size < sizeof(StarCatalogHeader)) {
        return false;
    }

    const auto* header = reinterpret_cast<const StarCatalogHeader*>(data);
    if (header->magic[0] != 'T' || header->magic[1] != 'S' || 
        header->magic[2] != 'C' || header->magic[3] != 'A') {
        return false;
    }
    if (header->version != 1) {
        return false;
    }

    size_t expected_size = sizeof(StarCatalogHeader) + header->star_count * sizeof(BinaryStarRecord);
    if (size < expected_size) {
        return false;
    }

    catalog->records = reinterpret_cast<const BinaryStarRecord*>(data + sizeof(StarCatalogHeader));
    catalog->star_count = header->star_count;
    catalog->owned_buffer.clear();

    star_catalog_rebuild_alias_map(catalog);

    // Populate storage_block
    catalog->storage_block.cache_id = 0;
    catalog->storage_block.format = internal::EphemerisBlockFormat::FixedStar;
    catalog->storage_block.position = calc_star_position;
    catalog->storage_block.velocity = calc_star_velocity;
    catalog->storage_block.acceleration = calc_star_acceleration;
    catalog->storage_block.destroy_element = destroy_star_ephemeris_data;
    catalog->storage_block.total_bytes = 0;
    catalog->storage_block.id_to_index = catalog->id_to_index;

    catalog->storage_block.data_vector.resize(catalog->star_count);
    for (size_t i = 0; i < catalog->star_count; ++i) {
        const auto& rec = catalog->records[i];
        StarData sd;
        sd.ra_j2000_deg = rec.ra_j2000_deg;
        sd.dec_j2000_deg = rec.dec_j2000_deg;
        sd.pm_ra_mas_yr = rec.pm_ra_mas_yr;
        sd.pm_dec_mas_yr = rec.pm_dec_mas_yr;
        sd.parallax_mas = rec.parallax_mas;
        sd.radial_velocity_km_s = rec.radial_velocity_km_s;

        StarEphemerisData* star_data = star_data_compile_kinematics(sd);
        catalog->storage_block.data_vector[i] = star_data;
        if (star_data) {
            catalog->storage_block.total_bytes += sizeof(StarEphemerisData);
        }
    }

    return true;
}

bool star_catalog_find_star(const StarCatalog* catalog, const std::string& id_or_alias, StarData* out_data) noexcept {
    if (!catalog || !out_data) return false;
    auto it = catalog->alias_map.find(id_or_alias);
    if (it == catalog->alias_map.end()) {
        return false;
    }
    const auto& rec = catalog->records[it->second];
    out_data->ra_j2000_deg = rec.ra_j2000_deg;
    out_data->dec_j2000_deg = rec.dec_j2000_deg;
    out_data->pm_ra_mas_yr = rec.pm_ra_mas_yr;
    out_data->pm_dec_mas_yr = rec.pm_dec_mas_yr;
    out_data->parallax_mas = rec.parallax_mas;
    out_data->radial_velocity_km_s = rec.radial_velocity_km_s;
    return true;
}

bool star_catalog_find_star_by_id(const StarCatalog* catalog, int star_id, StarData* out_data) noexcept {
    if (!catalog || !out_data) return false;
    auto it = catalog->id_to_index.find(star_id);
    if (it == catalog->id_to_index.end()) {
        return false;
    }
    const auto& rec = catalog->records[it->second];
    out_data->ra_j2000_deg = rec.ra_j2000_deg;
    out_data->dec_j2000_deg = rec.dec_j2000_deg;
    out_data->pm_ra_mas_yr = rec.pm_ra_mas_yr;
    out_data->pm_dec_mas_yr = rec.pm_dec_mas_yr;
    out_data->parallax_mas = rec.parallax_mas;
    out_data->radial_velocity_km_s = rec.radial_velocity_km_s;
    return true;
}

bool star_catalog_find_record(const StarCatalog* catalog, const std::string& id_or_alias, BinaryStarRecord* out_record) noexcept {
    if (!catalog || !out_record) return false;
    auto it = catalog->alias_map.find(id_or_alias);
    if (it == catalog->alias_map.end()) {
        return false;
    }
    *out_record = catalog->records[it->second];
    return true;
}

bool star_catalog_get_compiled_block(const StarCatalog* catalog, int star_id, internal::CompiledEphemerisBlock* out) noexcept {
    if (!catalog || !out) return false;
    return internal::get_compiled_block_from_storage(&catalog->storage_block, star_id, out);
}

void star_catalog_destroy(StarCatalog* catalog) noexcept {
    if (!catalog) return;
    internal::destroy_storage_ephemeris_block(&catalog->storage_block);
    catalog->owned_buffer.clear();
    catalog->records = nullptr;
    catalog->star_count = 0;
    catalog->alias_map.clear();
    catalog->id_to_index.clear();
}

const StarCatalog* get_builtin_star_catalog() noexcept {
    static const StarCatalog catalog = []() {
        StarCatalog cat;
        star_catalog_load_from_memory(&cat, BUILTIN_STAR_CATALOG_BYTES, sizeof(BUILTIN_STAR_CATALOG_BYTES));
        return cat;
    }();
    return &catalog;
}

// 无状态评估纯函数实现
bool calc_star_position(double jd_tdb, const void* data, Vector3* out_position_au) noexcept {
    if (!data || !out_position_au) return false;
    const auto* star = static_cast<const StarEphemerisData*>(data);
    const double dt_days = jd_tdb - JD_J2000; // J2000.0 epoch
    *out_position_au = vector3_add(star->position_j2000_au, vector3_scale(star->velocity_j2000_au_per_day, dt_days));
    return true;
}

bool calc_star_velocity(double jd_tdb, const void* data, Vector3* out_velocity_au_per_day) noexcept {
    (void)jd_tdb;
    if (!data || !out_velocity_au_per_day) return false;
    const auto* star = static_cast<const StarEphemerisData*>(data);
    *out_velocity_au_per_day = star->velocity_j2000_au_per_day;
    return true;
}

bool calc_star_acceleration(double jd_tdb, const void* data, Vector3* out_acceleration_au_per_day2) noexcept {
    (void)jd_tdb;
    (void)data;
    if (!out_acceleration_au_per_day2) return false;
    *out_acceleration_au_per_day2 = Vector3{0.0, 0.0, 0.0};
    return true;
}

void destroy_star_ephemeris_data(void* data) noexcept {
    if (data) {
        delete static_cast<StarEphemerisData*>(data);
    }
}

static StarEphemerisData* star_data_compile_kinematics(const StarData& star) noexcept {
    StarEphemerisData* star_data = new (std::nothrow) StarEphemerisData();
    if (!star_data) return 0;

    // Convert RA/Dec to radians
    const double ra_rad = star.ra_j2000_deg * TAIYIN_DEG_TO_RAD;
    const double dec_rad = star.dec_j2000_deg * TAIYIN_DEG_TO_RAD;

    // J2000 direction unit vector
    const Vector3 u0 = spherical_to_cartesian(ra_rad, dec_rad, 1.0);

    // Distance in AU
    double distance_au = 1e9; // default huge distance if parallax <= 0
    if (star.parallax_mas > 0.0) {
        distance_au = TAIYIN_AU_PER_PARALLAX_MAS / star.parallax_mas;
    }

    // 3D J2000 Position
    star_data->position_j2000_au = vector3_scale(u0, distance_au);

    // 3D J2000 Velocity
    // East unit vector in tangent plane
    const Vector3 e_alpha{-std::sin(ra_rad), std::cos(ra_rad), 0.0};
    // North unit vector in tangent plane
    const Vector3 e_beta{-std::sin(dec_rad) * std::cos(ra_rad), -std::sin(dec_rad) * std::sin(ra_rad), std::cos(dec_rad)};

    // Velocities in AU/day
    const double v_r = star.radial_velocity_km_s * TAIYIN_KM_PER_S_TO_AU_PER_DAY;

    // Transverse velocity: pm is mas/year.
    double v_alpha = 0.0;
    double v_beta = 0.0;
    if (star.parallax_mas > 0.0) {
        v_alpha = star.pm_ra_mas_yr / (star.parallax_mas * DAYS_PER_JULIAN_YEAR);
        v_beta = star.pm_dec_mas_yr / (star.parallax_mas * DAYS_PER_JULIAN_YEAR);
    } else {
        v_alpha = star.pm_ra_mas_yr * TAIYIN_MAS_PER_YEAR_TO_RAD_PER_DAY * distance_au;
        v_beta = star.pm_dec_mas_yr * TAIYIN_MAS_PER_YEAR_TO_RAD_PER_DAY * distance_au;
    }

    star_data->velocity_j2000_au_per_day = vector3_add(
        vector3_scale(u0, v_r),
        vector3_add(
            vector3_scale(e_alpha, v_alpha),
            vector3_scale(e_beta, v_beta)
        )
    );

    return star_data;
}



}  // namespace taiyin
