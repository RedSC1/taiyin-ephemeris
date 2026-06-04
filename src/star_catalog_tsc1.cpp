#include "taiyin/star_catalog_tsc1.h"

#include "taiyin/angle.h"
#include "taiyin/physical_constants.h"

#include <cmath>
#include <cctype>
#include <cstring>
#include <limits>
#include <new>

namespace taiyin {
namespace {

const char TSC1_MAGIC[4] = { 'T', 'S', 'C', '1' };
const uint64_t FNV1A_64_OFFSET = 14695981039346656037ULL;
const uint64_t FNV1A_64_PRIME = 1099511628211ULL;
const double DEFAULT_DIRECTION_DISTANCE_AU = 1e9;

bool checked_range(size_t size, uint64_t offset, uint64_t byte_count) noexcept {
    return offset <= static_cast<uint64_t>(size)
        && byte_count <= static_cast<uint64_t>(size) - offset;
}

bool checked_array_range(size_t size, uint64_t offset, uint64_t count, size_t element_size) noexcept {
    if (element_size != 0 && count > std::numeric_limits<uint64_t>::max() / static_cast<uint64_t>(element_size)) {
        return false;
    }
    return checked_range(size, offset, count * static_cast<uint64_t>(element_size));
}

bool is_native_little_endian() noexcept {
    const uint16_t value = 1;
    return *reinterpret_cast<const uint8_t*>(&value) == 1;
}

bool string_offset_valid(const Tsc1Catalog* catalog, uint32_t offset) noexcept {
    if (!catalog || !catalog->strings || offset >= catalog->string_table_size) {
        return false;
    }
    const char* start = catalog->strings + offset;
    const size_t remaining = catalog->string_table_size - offset;
    return std::memchr(start, '\0', remaining) != 0;
}

int compare_c_string(const char* lhs, const char* rhs) noexcept {
    if (!lhs && !rhs) return 0;
    if (!lhs) return -1;
    if (!rhs) return 1;
    return std::strcmp(lhs, rhs);
}

bool validate_catalog(Tsc1Catalog* catalog, const uint8_t* data, size_t size) noexcept {
    if (!catalog || !data || size < sizeof(Tsc1Header) || !is_native_little_endian()) {
        return false;
    }

    const Tsc1Header* header = reinterpret_cast<const Tsc1Header*>(data);
    if (std::memcmp(header->magic, TSC1_MAGIC, sizeof(TSC1_MAGIC)) != 0 || header->version != TSC1_VERSION) {
        return false;
    }

    if (!checked_array_range(size, header->star_records_offset, header->star_count, sizeof(Tsc1StarRecord))
        || !checked_array_range(size, header->alias_records_offset, header->alias_count, sizeof(Tsc1AliasEntry))
        || !checked_range(size, header->string_table_offset, header->string_table_size)
        || header->string_table_size > static_cast<uint64_t>(std::numeric_limits<size_t>::max())) {
        return false;
    }

    catalog->data = data;
    catalog->byte_count = size;
    catalog->header = header;
    catalog->stars = reinterpret_cast<const Tsc1StarRecord*>(data + static_cast<size_t>(header->star_records_offset));
    catalog->aliases = reinterpret_cast<const Tsc1AliasEntry*>(data + static_cast<size_t>(header->alias_records_offset));
    catalog->strings = reinterpret_cast<const char*>(data + static_cast<size_t>(header->string_table_offset));
    catalog->string_table_size = static_cast<size_t>(header->string_table_size);

    if (catalog->string_table_size == 0 || catalog->strings[0] != '\0') {
        return false;
    }

    for (uint32_t i = 0; i < header->star_count; ++i) {
        const Tsc1StarRecord& star = catalog->stars[i];
        if (!string_offset_valid(catalog, star.canonical_id_offset)
            || !string_offset_valid(catalog, star.display_name_offset)) {
            return false;
        }
    }

    uint64_t previous_hash = 0;
    const char* previous_alias = 0;
    for (uint32_t i = 0; i < header->alias_count; ++i) {
        const Tsc1AliasEntry& alias = catalog->aliases[i];
        if (alias.star_index >= header->star_count || !string_offset_valid(catalog, alias.alias_offset)) {
            return false;
        }
        const char* alias_string = catalog->strings + alias.alias_offset;
        if (i > 0) {
            if (alias.alias_hash < previous_hash) {
                return false;
            }
            if (alias.alias_hash == previous_hash && compare_c_string(alias_string, previous_alias) < 0) {
                return false;
            }
        }
        previous_hash = alias.alias_hash;
        previous_alias = alias_string;
    }

    return true;
}


}  // namespace

static_assert(sizeof(Tsc1Header) == 132, "Tsc1Header size must match TSC1 v1");
static_assert(sizeof(Tsc1StarRecord) == 92, "Tsc1StarRecord size must match TSC1 v1");
static_assert(sizeof(Tsc1AliasEntry) == 16, "Tsc1AliasEntry size must match TSC1 v1");

Tsc1ResolvedStar::Tsc1ResolvedStar()
    : star_index(0), star_id(0), record(0) {}

Tsc1Catalog::Tsc1Catalog()
    : data(0),
      byte_count(0),
      header(0),
      stars(0),
      aliases(0),
      strings(0),
      string_table_size(0),
      file() {}

uint64_t tsc1_fnv1a_64(const std::string& value) noexcept {
    uint64_t result = FNV1A_64_OFFSET;
    for (size_t i = 0; i < value.size(); ++i) {
        result ^= static_cast<uint8_t>(value[i]);
        result *= FNV1A_64_PRIME;
    }
    return result;
}

std::string tsc1_normalize_alias(const std::string& value) {
    std::string out;
    out.reserve(value.size());
    bool last_was_separator = false;
    for (size_t i = 0; i < value.size(); ++i) {
        const unsigned char ch = static_cast<unsigned char>(value[i]);
        if (std::isalnum(ch)) {
            out.push_back(static_cast<char>(std::tolower(ch)));
            last_was_separator = false;
        } else if (ch == '_' || ch == '-' || std::isspace(ch)) {
            if (!out.empty() && !last_was_separator) {
                out.push_back('_');
                last_was_separator = true;
            }
        }
    }
    while (!out.empty() && out[out.size() - 1] == '_') {
        out.erase(out.size() - 1);
    }
    return out;
}

bool tsc1_catalog_load_from_memory(Tsc1Catalog* catalog, const uint8_t* data, size_t size) noexcept {
    if (!catalog) {
        return false;
    }
    tsc1_catalog_destroy(catalog);
    return validate_catalog(catalog, data, size);
}

bool tsc1_catalog_load_from_file(Tsc1Catalog* catalog, const std::string& path) noexcept {
    if (!catalog) {
        return false;
    }
    tsc1_catalog_destroy(catalog);
    if (!catalog->file.open_readonly(path)) {
        return false;
    }
    if (!validate_catalog(catalog, catalog->file.data(), catalog->file.size())) {
        tsc1_catalog_destroy(catalog);
        return false;
    }
    return true;
}

void tsc1_catalog_destroy(Tsc1Catalog* catalog) noexcept {
    if (!catalog) {
        return;
    }
    catalog->data = 0;
    catalog->byte_count = 0;
    catalog->header = 0;
    catalog->stars = 0;
    catalog->aliases = 0;
    catalog->strings = 0;
    catalog->string_table_size = 0;
    catalog->file.close();
}

const Tsc1StarRecord* tsc1_catalog_star(const Tsc1Catalog* catalog, uint32_t index) noexcept {
    if (!catalog || !catalog->header || !catalog->stars || index >= catalog->header->star_count) {
        return 0;
    }
    return &catalog->stars[index];
}

const Tsc1StarRecord* tsc1_catalog_find(const Tsc1Catalog* catalog, const std::string& id_or_alias) noexcept {
    uint32_t star_index = 0;
    if (!tsc1_catalog_find_index(catalog, id_or_alias, &star_index)) {
        return 0;
    }
    return tsc1_catalog_star(catalog, star_index);
}

bool tsc1_catalog_find_index(const Tsc1Catalog* catalog, const std::string& id_or_alias, uint32_t* out_index) noexcept {
    if (out_index) {
        *out_index = 0;
    }
    if (!catalog || !catalog->header || !catalog->aliases || !catalog->strings || !out_index) {
        return false;
    }

    std::string alias;
    try {
        alias = tsc1_normalize_alias(id_or_alias);
    } catch (...) {
        return false;
    }
    if (alias.empty()) {
        return false;
    }

    const uint64_t hash = tsc1_fnv1a_64(alias);
    uint32_t lo = 0;
    uint32_t hi = catalog->header->alias_count;
    while (lo < hi) {
        const uint32_t mid = lo + (hi - lo) / 2;
        if (catalog->aliases[mid].alias_hash < hash) {
            lo = mid + 1;
        } else {
            hi = mid;
        }
    }

    for (uint32_t i = lo; i < catalog->header->alias_count && catalog->aliases[i].alias_hash == hash; ++i) {
        const Tsc1AliasEntry& entry = catalog->aliases[i];
        const char* entry_alias = tsc1_catalog_string(catalog, entry.alias_offset);
        if (entry_alias && alias == entry_alias) {
            *out_index = entry.star_index;
            return true;
        }
    }
    return false;
}

const char* tsc1_catalog_string(const Tsc1Catalog* catalog, uint32_t offset) noexcept {
    if (!string_offset_valid(catalog, offset)) {
        return 0;
    }
    return catalog->strings + offset;
}

Tsc1StarEphemerisData::Tsc1StarEphemerisData()
    : position_ref_au{0.0, 0.0, 0.0},
      velocity_ref_au_per_day{0.0, 0.0, 0.0},
      reference_jd_tdb(JD_J2000),
      star_index(0),
      astrometry_source(TSC1_SOURCE_UNKNOWN),
      flags(0) {}

static double finite_or_zero(double value) noexcept {
    return std::isfinite(value) ? value : 0.0;
}

static double reference_epoch_to_jd_tdb(double reference_epoch) noexcept {
    if (!std::isfinite(reference_epoch)) {
        return JD_J2000;
    }
    return JD_J2000 + (reference_epoch - 2000.0) * DAYS_PER_JULIAN_YEAR;
}

bool tsc1_compile_star_ephemeris_data(
    const Tsc1StarRecord* record,
    uint32_t star_index,
    Tsc1StarEphemerisData** out
) noexcept {
    if (!record || !out || !std::isfinite(record->ra_deg) || !std::isfinite(record->dec_deg)) {
        return false;
    }

    Tsc1StarEphemerisData* data = new (std::nothrow) Tsc1StarEphemerisData();
    if (!data) {
        return false;
    }

    const double ra_rad = record->ra_deg * TAIYIN_DEG_TO_RAD;
    const double dec_rad = record->dec_deg * TAIYIN_DEG_TO_RAD;
    const Vector3 u0 = spherical_to_cartesian(ra_rad, dec_rad, 1.0);

    const bool has_parallax = (record->flags & TSC1_STAR_HAS_PARALLAX) != 0
        && std::isfinite(record->parallax_mas)
        && record->parallax_mas > 0.0;
    const double distance_au = has_parallax
        ? TAIYIN_AU_PER_PARALLAX_MAS / record->parallax_mas
        : DEFAULT_DIRECTION_DISTANCE_AU;

    const double pm_ra_mas_yr = finite_or_zero(record->pm_ra_mas_yr);
    const double pm_dec_mas_yr = finite_or_zero(record->pm_dec_mas_yr);
    const double radial_velocity_km_s = ((record->flags & TSC1_STAR_HAS_RADIAL_VELOCITY) != 0)
        ? finite_or_zero(record->radial_velocity_km_s)
        : 0.0;

    data->position_ref_au = vector3_scale(u0, distance_au);

    const Vector3 e_alpha{-std::sin(ra_rad), std::cos(ra_rad), 0.0};
    const Vector3 e_beta{
        -std::sin(dec_rad) * std::cos(ra_rad),
        -std::sin(dec_rad) * std::sin(ra_rad),
        std::cos(dec_rad)
    };

    const double v_r = radial_velocity_km_s * TAIYIN_KM_PER_S_TO_AU_PER_DAY;
    double v_alpha = 0.0;
    double v_beta = 0.0;
    if (has_parallax) {
        v_alpha = pm_ra_mas_yr / (record->parallax_mas * DAYS_PER_JULIAN_YEAR);
        v_beta = pm_dec_mas_yr / (record->parallax_mas * DAYS_PER_JULIAN_YEAR);
    } else {
        v_alpha = pm_ra_mas_yr * TAIYIN_MAS_PER_YEAR_TO_RAD_PER_DAY * distance_au;
        v_beta = pm_dec_mas_yr * TAIYIN_MAS_PER_YEAR_TO_RAD_PER_DAY * distance_au;
    }

    data->velocity_ref_au_per_day = vector3_add(
        vector3_scale(u0, v_r),
        vector3_add(vector3_scale(e_alpha, v_alpha), vector3_scale(e_beta, v_beta))
    );
    data->reference_jd_tdb = reference_epoch_to_jd_tdb(record->reference_epoch);
    data->star_index = star_index;
    data->astrometry_source = record->astrometry_source;
    data->flags = record->flags;

    *out = data;
    return true;
}

bool tsc1_compile_star_storage_block(
    const Tsc1Catalog* catalog,
    uint32_t star_index,
    int star_id,
    internal::StorageEphemerisBlock* out
) noexcept {
    if (!catalog || !out || star_id == 0) {
        return false;
    }
    const Tsc1StarRecord* record = tsc1_catalog_star(catalog, star_index);
    if (!record) {
        return false;
    }

    internal::destroy_storage_ephemeris_block(out);

    Tsc1StarEphemerisData* data = 0;
    if (!tsc1_compile_star_ephemeris_data(record, star_index, &data)) {
        return false;
    }

    try {
        out->data_vector.resize(1);
        out->id_to_index[star_id] = 0;
    } catch (...) {
        delete data;
        internal::destroy_storage_ephemeris_block(out);
        return false;
    }

    out->cache_id = 0;
    out->format = internal::EphemerisBlockFormat::FixedStar;
    out->position = calc_tsc1_star_position;
    out->velocity = calc_tsc1_star_velocity;
    out->acceleration = calc_tsc1_star_acceleration;
    out->destroy_element = destroy_tsc1_star_ephemeris_data;
    out->total_bytes = sizeof(Tsc1StarEphemerisData);
    out->data_vector[0] = data;
    return true;
}

bool calc_tsc1_star_position(double jd_tdb, const void* data, Vector3* out_position_au) noexcept {
    if (!data || !out_position_au) {
        return false;
    }
    const Tsc1StarEphemerisData* star = static_cast<const Tsc1StarEphemerisData*>(data);
    const double dt_days = jd_tdb - star->reference_jd_tdb;
    *out_position_au = vector3_add(star->position_ref_au, vector3_scale(star->velocity_ref_au_per_day, dt_days));
    return true;
}

bool calc_tsc1_star_velocity(double jd_tdb, const void* data, Vector3* out_velocity_au_per_day) noexcept {
    (void)jd_tdb;
    if (!data || !out_velocity_au_per_day) {
        return false;
    }
    const Tsc1StarEphemerisData* star = static_cast<const Tsc1StarEphemerisData*>(data);
    *out_velocity_au_per_day = star->velocity_ref_au_per_day;
    return true;
}

bool calc_tsc1_star_acceleration(double jd_tdb, const void* data, Vector3* out_acceleration_au_per_day2) noexcept {
    (void)jd_tdb;
    (void)data;
    if (!out_acceleration_au_per_day2) {
        return false;
    }
    *out_acceleration_au_per_day2 = Vector3{0.0, 0.0, 0.0};
    return true;
}

void destroy_tsc1_star_ephemeris_data(void* data) noexcept {
    delete static_cast<Tsc1StarEphemerisData*>(data);
}

Tsc1StarProvider::Tsc1StarProvider(size_t cache_max_bytes, int catalog_id) noexcept
    : catalog_(),
      cache_(cache_max_bytes),
      catalog_id_(catalog_id),
      jd_tdb_start_(TSC1_DEFAULT_JD_TDB_START),
      jd_tdb_end_(TSC1_DEFAULT_JD_TDB_END),
      star_id_by_index_() {}

Tsc1StarProvider::~Tsc1StarProvider() noexcept {
    close();
}

bool Tsc1StarProvider::load_from_memory(const uint8_t* data, size_t size) noexcept {
    close();
    return tsc1_catalog_load_from_memory(&catalog_, data, size);
}

bool Tsc1StarProvider::load_from_file(const std::string& path) noexcept {
    close();
    return tsc1_catalog_load_from_file(&catalog_, path);
}

void Tsc1StarProvider::close() noexcept {
    cache_.clear();
    star_id_by_index_.clear();
    tsc1_catalog_destroy(&catalog_);
}

bool Tsc1StarProvider::resolve(const std::string& id_or_alias, Tsc1ResolvedStar* out) noexcept {
    if (out) {
        *out = Tsc1ResolvedStar();
    }
    if (!out) {
        return false;
    }

    uint32_t star_index = 0;
    if (!tsc1_catalog_find_index(&catalog_, id_or_alias, &star_index)) {
        return false;
    }
    const Tsc1StarRecord* record = tsc1_catalog_star(&catalog_, star_index);
    if (!record) {
        return false;
    }
    const int star_id = resolve_or_register_star_id(star_index, record);
    if (star_id == 0) {
        return false;
    }

    out->star_index = star_index;
    out->star_id = star_id;
    out->record = record;
    return true;
}

bool Tsc1StarProvider::eval_position(const std::string& id_or_alias, double jd_tdb, Vector3* out) noexcept {
    if (!out) {
        return false;
    }
    Tsc1ResolvedStar resolved;
    if (!resolve(id_or_alias, &resolved)) {
        return false;
    }
    const internal::EphemerisRouteKey key = route_key(resolved.star_id);
    if (cache_.eval_position(key, jd_tdb, out)) {
        return true;
    }
    return ensure_cached(resolved.star_index, resolved.star_id)
        && cache_.eval_position(key, jd_tdb, out);
}

bool Tsc1StarProvider::eval_velocity(const std::string& id_or_alias, double jd_tdb, Vector3* out) noexcept {
    if (!out) {
        return false;
    }
    Tsc1ResolvedStar resolved;
    if (!resolve(id_or_alias, &resolved)) {
        return false;
    }
    const internal::EphemerisRouteKey key = route_key(resolved.star_id);
    if (cache_.eval_velocity(key, jd_tdb, out)) {
        return true;
    }
    return ensure_cached(resolved.star_index, resolved.star_id)
        && cache_.eval_velocity(key, jd_tdb, out);
}

bool Tsc1StarProvider::eval_acceleration(const std::string& id_or_alias, double jd_tdb, Vector3* out) noexcept {
    if (!out) {
        return false;
    }
    Tsc1ResolvedStar resolved;
    if (!resolve(id_or_alias, &resolved)) {
        return false;
    }
    const internal::EphemerisRouteKey key = route_key(resolved.star_id);
    if (cache_.eval_acceleration(key, jd_tdb, out)) {
        return true;
    }
    return ensure_cached(resolved.star_index, resolved.star_id)
        && cache_.eval_acceleration(key, jd_tdb, out);
}

bool Tsc1StarProvider::eval_state(const std::string& id_or_alias, double jd_tdb, CartesianState* out) noexcept {
    if (!out) {
        return false;
    }
    Tsc1ResolvedStar resolved;
    if (!resolve(id_or_alias, &resolved)) {
        return false;
    }
    const internal::EphemerisRouteKey key = route_key(resolved.star_id);
    if (cache_.eval_state(key, jd_tdb, out)) {
        return true;
    }
    return ensure_cached(resolved.star_index, resolved.star_id)
        && cache_.eval_state(key, jd_tdb, out);
}

bool Tsc1StarProvider::contains_cached_star(const std::string& id_or_alias) noexcept {
    Tsc1ResolvedStar resolved;
    if (!resolve(id_or_alias, &resolved)) {
        return false;
    }
    return cache_.contains(route_key(resolved.star_id));
}

size_t Tsc1StarProvider::cache_entry_count() const noexcept {
    return cache_.entry_count();
}

size_t Tsc1StarProvider::cache_total_bytes() const noexcept {
    return cache_.total_bytes();
}

const Tsc1Catalog* Tsc1StarProvider::catalog() const noexcept {
    return &catalog_;
}

int Tsc1StarProvider::catalog_id() const noexcept {
    return catalog_id_;
}

bool Tsc1StarProvider::ensure_cached(uint32_t star_index, int star_id) noexcept {
    if (star_id == 0) {
        return false;
    }
    const internal::EphemerisRouteKey key = route_key(star_id);
    if (cache_.contains(key)) {
        return true;
    }

    internal::StorageEphemerisBlock storage;
    if (!tsc1_compile_star_storage_block(&catalog_, star_index, star_id, &storage)) {
        return false;
    }
    if (!cache_.insert(key, jd_tdb_start_, jd_tdb_end_, &storage, 1.0, 0)) {
        internal::destroy_storage_ephemeris_block(&storage);
        return false;
    }
    return true;
}

int Tsc1StarProvider::resolve_or_register_star_id(uint32_t star_index, const Tsc1StarRecord* record) noexcept {
    std::unordered_map<uint32_t, int>::const_iterator existing = star_id_by_index_.find(star_index);
    if (existing != star_id_by_index_.end()) {
        return existing->second;
    }
    if (!record) {
        return 0;
    }
    const char* canonical_id = tsc1_catalog_string(&catalog_, record->canonical_id_offset);
    if (!canonical_id || canonical_id[0] == '\0') {
        return 0;
    }

    int star_id = internal::register_celestial_body(canonical_id);
    const char* display_name = tsc1_catalog_string(&catalog_, record->display_name_offset);
    if (display_name && display_name[0] != '\0') {
        internal::register_celestial_body_alias(display_name, star_id);
    }

    for (uint32_t i = 0; catalog_.header && i < catalog_.header->alias_count; ++i) {
        const Tsc1AliasEntry& alias = catalog_.aliases[i];
        if (alias.star_index != star_index) {
            continue;
        }
        const char* alias_string = tsc1_catalog_string(&catalog_, alias.alias_offset);
        if (alias_string && alias_string[0] != '\0') {
            internal::register_celestial_body_alias(alias_string, star_id);
        }
    }

    try {
        star_id_by_index_[star_index] = star_id;
    } catch (...) {
        return 0;
    }
    return star_id;
}

internal::EphemerisRouteKey Tsc1StarProvider::route_key(int star_id) const noexcept {
    return internal::EphemerisRouteKey(star_id, TSC1_DEFAULT_CENTER_ID, TSC1_STAR_METHOD_ID, catalog_id_);
}

}  // namespace taiyin
