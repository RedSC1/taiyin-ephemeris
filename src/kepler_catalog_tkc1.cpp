#include "taiyin/internal/kepler_catalog_tkc1.h"

#include <cctype>
#include <cmath>
#include <cstring>
#include <limits>
#include <vector>

namespace taiyin {
namespace internal {
namespace {

const char TKC1_MAGIC[4] = { 'T', 'K', 'C', '1' };
const uint64_t FNV1A_64_OFFSET = 14695981039346656037ULL;
const uint64_t FNV1A_64_PRIME = 1099511628211ULL;

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

bool string_offset_valid(const Tkc1Catalog* catalog, uint32_t offset) noexcept {
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

EphemerisFrame frame_from_id(int frame_id) noexcept {
    return frame_id == static_cast<int>(EphemerisFrame::IcrfJ2000Equatorial)
        ? EphemerisFrame::IcrfJ2000Equatorial
        : EphemerisFrame::FrameUnknown;
}

bool object_record_valid(const Tkc1Catalog* catalog, const Tkc1ObjectRecord& object) noexcept {
    if (!catalog
        || object.target_id == object.center_id
        || object.method_id == 0
        || frame_from_id(object.frame_id) == EphemerisFrame::FrameUnknown
        || !std::isfinite(object.jd_tdb_start)
        || !std::isfinite(object.jd_tdb_end)
        || object.jd_tdb_end <= object.jd_tdb_start
        || object.element_count == 0
        || object.element_start_index > catalog->header->element_count
        || object.element_count > catalog->header->element_count - object.element_start_index
        || !string_offset_valid(catalog, object.canonical_name_offset)
        || !string_offset_valid(catalog, object.display_name_offset)) {
        return false;
    }
    return true;
}

bool element_record_valid(const Tkc1ElementRecord& element) noexcept {
    return std::isfinite(element.jd_tdb_start)
        && std::isfinite(element.jd_tdb_end)
        && std::isfinite(element.epoch_jd_tdb)
        && std::isfinite(element.mu_au3_day2)
        && std::isfinite(element.semi_major_axis_au)
        && std::isfinite(element.eccentricity)
        && std::isfinite(element.inclination_rad)
        && std::isfinite(element.longitude_ascending_node_rad)
        && std::isfinite(element.argument_periapsis_rad)
        && std::isfinite(element.mean_anomaly_at_epoch_rad)
        && element.jd_tdb_end > element.jd_tdb_start
        && element.mu_au3_day2 > 0.0
        && element.semi_major_axis_au > 0.0
        && element.eccentricity >= 0.0
        && element.eccentricity < 1.0;
}

bool validate_catalog(Tkc1Catalog* catalog, const uint8_t* data, size_t size) noexcept {
    if (!catalog || !data || size < sizeof(Tkc1Header) || !is_native_little_endian()) {
        return false;
    }

    const Tkc1Header* header = reinterpret_cast<const Tkc1Header*>(data);
    if (std::memcmp(header->magic, TKC1_MAGIC, sizeof(TKC1_MAGIC)) != 0 || header->version != TKC1_VERSION) {
        return false;
    }

    if (!std::isfinite(header->catalog_jd_tdb_start)
        || !std::isfinite(header->catalog_jd_tdb_end)
        || header->catalog_jd_tdb_end <= header->catalog_jd_tdb_start
        || header->object_count == 0
        || header->element_count == 0
        || !checked_array_range(size, header->object_records_offset, header->object_count, sizeof(Tkc1ObjectRecord))
        || !checked_array_range(size, header->element_records_offset, header->element_count, sizeof(Tkc1ElementRecord))
        || !checked_array_range(size, header->alias_records_offset, header->alias_count, sizeof(Tkc1AliasRecord))
        || !checked_range(size, header->string_table_offset, header->string_table_size)
        || header->string_table_size > static_cast<uint64_t>(std::numeric_limits<size_t>::max())) {
        return false;
    }

    catalog->data = data;
    catalog->byte_count = size;
    catalog->header = header;
    catalog->objects = reinterpret_cast<const Tkc1ObjectRecord*>(data + static_cast<size_t>(header->object_records_offset));
    catalog->elements = reinterpret_cast<const Tkc1ElementRecord*>(data + static_cast<size_t>(header->element_records_offset));
    catalog->aliases = reinterpret_cast<const Tkc1AliasRecord*>(data + static_cast<size_t>(header->alias_records_offset));
    catalog->strings = reinterpret_cast<const char*>(data + static_cast<size_t>(header->string_table_offset));
    catalog->string_table_size = static_cast<size_t>(header->string_table_size);

    if (catalog->string_table_size == 0 || catalog->strings[0] != '\0') {
        return false;
    }

    int previous_target_id = std::numeric_limits<int>::min();
    for (uint32_t i = 0; i < header->object_count; ++i) {
        const Tkc1ObjectRecord& object = catalog->objects[i];
        if (!object_record_valid(catalog, object) || object.target_id < previous_target_id) {
            return false;
        }
        previous_target_id = object.target_id;
    }

    for (uint32_t i = 0; i < header->element_count; ++i) {
        if (!element_record_valid(catalog->elements[i])) {
            return false;
        }
    }

    uint64_t previous_hash = 0;
    const char* previous_alias = 0;
    for (uint32_t i = 0; i < header->alias_count; ++i) {
        const Tkc1AliasRecord& alias = catalog->aliases[i];
        if (alias.object_index >= header->object_count || !string_offset_valid(catalog, alias.alias_offset)) {
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

static_assert(sizeof(Tkc1Header) == 196, "Tkc1Header size must match TKC1 v1");
static_assert(sizeof(Tkc1ObjectRecord) == 64, "Tkc1ObjectRecord size must match TKC1 v1");
static_assert(sizeof(Tkc1ElementRecord) == 80, "Tkc1ElementRecord size must match TKC1 v1");
static_assert(sizeof(Tkc1AliasRecord) == 16, "Tkc1AliasRecord size must match TKC1 v1");

Tkc1Catalog::Tkc1Catalog()
    : data(0),
      byte_count(0),
      header(0),
      objects(0),
      elements(0),
      aliases(0),
      strings(0),
      string_table_size(0),
      file() {}

uint64_t tkc1_fnv1a_64(const std::string& value) noexcept {
    uint64_t result = FNV1A_64_OFFSET;
    for (size_t i = 0; i < value.size(); ++i) {
        result ^= static_cast<uint8_t>(value[i]);
        result *= FNV1A_64_PRIME;
    }
    return result;
}

std::string tkc1_normalize_alias(const std::string& value) {
    std::string out;
    out.reserve(value.size());
    bool last_was_separator = false;
    for (size_t i = 0; i < value.size(); ++i) {
        const unsigned char ch = static_cast<unsigned char>(value[i]);
        if (std::isalnum(ch)) {
            out.push_back(static_cast<char>(std::tolower(ch)));
            last_was_separator = false;
        } else if (ch == '_' || ch == '-' || std::isspace(ch) || ch == '(' || ch == ')') {
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

bool tkc1_catalog_load_from_memory(Tkc1Catalog* catalog, const uint8_t* data, size_t size) noexcept {
    if (!catalog) {
        return false;
    }
    tkc1_catalog_destroy(catalog);
    return validate_catalog(catalog, data, size);
}

bool tkc1_catalog_load_from_file(Tkc1Catalog* catalog, const std::string& path) noexcept {
    if (!catalog) {
        return false;
    }
    tkc1_catalog_destroy(catalog);
    if (!catalog->file.open_readonly(path)) {
        return false;
    }
    if (!validate_catalog(catalog, catalog->file.data(), catalog->file.size())) {
        tkc1_catalog_destroy(catalog);
        return false;
    }
    return true;
}

void tkc1_catalog_destroy(Tkc1Catalog* catalog) noexcept {
    if (!catalog) {
        return;
    }
    catalog->data = 0;
    catalog->byte_count = 0;
    catalog->header = 0;
    catalog->objects = 0;
    catalog->elements = 0;
    catalog->aliases = 0;
    catalog->strings = 0;
    catalog->string_table_size = 0;
    catalog->file.close();
}

const Tkc1ObjectRecord* tkc1_catalog_object(const Tkc1Catalog* catalog, uint32_t index) noexcept {
    if (!catalog || !catalog->header || !catalog->objects || index >= catalog->header->object_count) {
        return 0;
    }
    return &catalog->objects[index];
}

const Tkc1ElementRecord* tkc1_catalog_element(const Tkc1Catalog* catalog, uint32_t index) noexcept {
    if (!catalog || !catalog->header || !catalog->elements || index >= catalog->header->element_count) {
        return 0;
    }
    return &catalog->elements[index];
}

const char* tkc1_catalog_string(const Tkc1Catalog* catalog, uint32_t offset) noexcept {
    if (!string_offset_valid(catalog, offset)) {
        return 0;
    }
    return catalog->strings + offset;
}

bool tkc1_catalog_find_object_index_by_target_id(
    const Tkc1Catalog* catalog,
    int target_id,
    uint32_t* out_index
) noexcept {
    if (out_index) {
        *out_index = 0;
    }
    if (!catalog || !catalog->header || !catalog->objects || !out_index) {
        return false;
    }

    uint32_t lo = 0;
    uint32_t hi = catalog->header->object_count;
    while (lo < hi) {
        const uint32_t mid = lo + (hi - lo) / 2;
        if (catalog->objects[mid].target_id < target_id) {
            lo = mid + 1;
        } else {
            hi = mid;
        }
    }
    if (lo < catalog->header->object_count && catalog->objects[lo].target_id == target_id) {
        *out_index = lo;
        return true;
    }
    return false;
}

bool tkc1_catalog_find_object_index_by_alias(
    const Tkc1Catalog* catalog,
    const std::string& alias_value,
    uint32_t* out_index
) noexcept {
    if (out_index) {
        *out_index = 0;
    }
    if (!catalog || !catalog->header || !catalog->aliases || !catalog->strings || !out_index) {
        return false;
    }

    std::string alias;
    try {
        alias = tkc1_normalize_alias(alias_value);
    } catch (...) {
        return false;
    }
    if (alias.empty()) {
        return false;
    }

    const uint64_t hash = tkc1_fnv1a_64(alias);
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
        const Tkc1AliasRecord& entry = catalog->aliases[i];
        const char* entry_alias = tkc1_catalog_string(catalog, entry.alias_offset);
        if (entry_alias && alias == entry_alias) {
            *out_index = entry.object_index;
            return true;
        }
    }
    return false;
}

bool tkc1_make_descriptor_for_object(
    const std::string& path,
    const Tkc1Catalog* catalog,
    uint32_t object_index,
    EphemerisBlockDescriptor* out
) noexcept {
    if (!out) {
        return false;
    }
    *out = EphemerisBlockDescriptor();
    const Tkc1ObjectRecord* object = tkc1_catalog_object(catalog, object_index);
    if (!object || path.empty() || object_index > static_cast<uint32_t>(std::numeric_limits<int>::max())) {
        return false;
    }

    EphemerisFrame frame = frame_from_id(object->frame_id);
    if (frame == EphemerisFrame::FrameUnknown) {
        return false;
    }

    EphemerisBlockDescriptor descriptor;
    descriptor.route_key = EphemerisRouteKey(
        object->target_id,
        object->center_id,
        object->method_id,
        static_cast<int>(object_index));
    descriptor.source_key = EphemerisBlockKey(
        TKC1_SOURCE_ID,
        object_index,
        catalog && catalog->header ? catalog->header->generation : TKC1_GENERATION,
        TKC1_PURPOSE);
    descriptor.target_id = object->target_id;
    descriptor.center_id = object->center_id;
    descriptor.method_id = object->method_id;
    descriptor.frame = frame;
    descriptor.format = EphemerisBlockFormat::Tkc1;
    descriptor.jd_tdb_start = object->jd_tdb_start;
    descriptor.jd_tdb_end = object->jd_tdb_end;
    descriptor.path = path;
    *out = descriptor;
    return true;
}

bool tkc1_compile_object_storage_block(
    const Tkc1Catalog* catalog,
    uint32_t object_index,
    double jd_tdb_start,
    double jd_tdb_end,
    StorageEphemerisBlock* out
) noexcept {
    if (!catalog || !out || jd_tdb_end <= jd_tdb_start) {
        return false;
    }
    const Tkc1ObjectRecord* object = tkc1_catalog_object(catalog, object_index);
    if (!object
        || jd_tdb_start < object->jd_tdb_start
        || jd_tdb_end > object->jd_tdb_end
        || object->element_count == 0
        || object->element_start_index > catalog->header->element_count
        || object->element_count > catalog->header->element_count - object->element_start_index) {
        return false;
    }

    try {
        std::vector<KeplerElements> elements;
        elements.reserve(object->element_count);
        for (uint32_t i = 0; i < object->element_count; ++i) {
            const Tkc1ElementRecord& record = catalog->elements[object->element_start_index + i];
            KeplerElements element;
            if (!make_elliptic_kepler_elements(
                    object->target_id,
                    object->center_id,
                    record.jd_tdb_start,
                    record.jd_tdb_end,
                    record.epoch_jd_tdb,
                    record.mu_au3_day2,
                    record.semi_major_axis_au,
                    record.eccentricity,
                    record.inclination_rad,
                    record.longitude_ascending_node_rad,
                    record.argument_periapsis_rad,
                    record.mean_anomaly_at_epoch_rad,
                    &element)) {
                return false;
            }
            elements.push_back(element);
        }
        return compile_kepler_ephemeris_block(
            &elements[0],
            elements.size(),
            jd_tdb_start,
            jd_tdb_end,
            out);
    } catch (...) {
        return false;
    }
}

bool tkc1_compile_object_storage_block_from_file(
    const std::string& path,
    uint32_t object_index,
    double jd_tdb_start,
    double jd_tdb_end,
    StorageEphemerisBlock* out
) noexcept {
    if (!out) {
        return false;
    }
    Tkc1Catalog catalog;
    if (!tkc1_catalog_load_from_file(&catalog, path)) {
        return false;
    }
    const bool ok = tkc1_compile_object_storage_block(&catalog, object_index, jd_tdb_start, jd_tdb_end, out);
    tkc1_catalog_destroy(&catalog);
    return ok;
}

}  // namespace internal
}  // namespace taiyin
