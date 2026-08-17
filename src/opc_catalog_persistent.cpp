#include "taiyin/internal/opc_catalog_persistent.h"

#include "taiyin/internal/ephemeris_source_identity.h"
#include "taiyin/internal/mapped_file.h"
#include "taiyin/internal/path_utils.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#if defined(_WIN32)
#include "taiyin/internal/win32_dirent.h"
#include "taiyin/internal/win32_path.h"
#else
#include <dirent.h>
#endif
#include <fstream>
#include <limits>
#if !defined(_WIN32)
#include <sys/stat.h>
#endif
#include <vector>

namespace taiyin {
namespace internal {
namespace {

const char OPC_MAGIC[4] = { 'O', 'P', 'C', '1' };
const uint64_t FNV1A_64_OFFSET = 14695981039346656037ULL;
const uint64_t FNV1A_64_PRIME = 1099511628211ULL;
const uint64_t OPC_SOURCE_ID = 0;
const int64_t WINDOWS_TO_UNIX_EPOCH_SECONDS = 11644473600LL;

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

void fnv1a_update_bytes(uint64_t* hash, const void* data, size_t size) noexcept {
    const uint8_t* bytes = static_cast<const uint8_t*>(data);
    for (size_t i = 0; i < size; ++i) {
        *hash ^= bytes[i];
        *hash *= FNV1A_64_PRIME;
    }
}

void fnv1a_update_string(uint64_t* hash, const std::string& value) noexcept {
    if (!value.empty()) {
        fnv1a_update_bytes(hash, value.data(), value.size());
    }
    const char separator = '\0';
    fnv1a_update_bytes(hash, &separator, 1);
}

void fnv1a_update_u64(uint64_t* hash, uint64_t value) noexcept {
    fnv1a_update_bytes(hash, &value, sizeof(value));
}

bool get_file_stat_metadata(
    const std::string& path,
    uint64_t* out_size,
    int64_t* out_mtime_sec,
    int64_t* out_mtime_nsec
) noexcept {
    if (!out_size || !out_mtime_sec || !out_mtime_nsec) {
        return false;
    }
#if defined(_WIN32)
    std::wstring wide_path;
    if (!win32_utf8_to_wide(path, &wide_path)) {
        return false;
    }
    WIN32_FILE_ATTRIBUTE_DATA data;
    if (!GetFileAttributesExW(
            wide_path.c_str(), GetFileExInfoStandard, &data)
        || (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
        return false;
    }
    *out_size = (static_cast<uint64_t>(data.nFileSizeHigh) << 32)
        | static_cast<uint64_t>(data.nFileSizeLow);
    ULARGE_INTEGER timestamp;
    timestamp.LowPart = data.ftLastWriteTime.dwLowDateTime;
    timestamp.HighPart = data.ftLastWriteTime.dwHighDateTime;
    *out_mtime_sec = static_cast<int64_t>(timestamp.QuadPart / 10000000ULL)
        - WINDOWS_TO_UNIX_EPOCH_SECONDS;
    *out_mtime_nsec = static_cast<int64_t>(
        (timestamp.QuadPart % 10000000ULL) * 100ULL);
    return true;
#else
    struct stat st;
    if (stat(path.c_str(), &st) != 0 || !S_ISREG(st.st_mode) || st.st_size < 0) {
        return false;
    }
    *out_size = static_cast<uint64_t>(st.st_size);
#if defined(__APPLE__)
    *out_mtime_sec = static_cast<int64_t>(st.st_mtimespec.tv_sec);
    *out_mtime_nsec = static_cast<int64_t>(st.st_mtimespec.tv_nsec);
#elif defined(_WIN32)
    *out_mtime_sec = static_cast<int64_t>(st.st_mtime);
    *out_mtime_nsec = 0;
#else
    *out_mtime_sec = static_cast<int64_t>(st.st_mtim.tv_sec);
    *out_mtime_nsec = static_cast<int64_t>(st.st_mtim.tv_nsec);
#endif
    return true;
#endif
}

bool fnv1a_update_file_identity(uint64_t* hash, const std::string& path) noexcept {
    uint64_t size = 0;
    int64_t mtime_sec = 0;
    int64_t mtime_nsec = 0;
    if (!hash || !get_file_stat_metadata(path, &size, &mtime_sec, &mtime_nsec)) {
        return false;
    }
    fnv1a_update_u64(hash, size);
    return true;
}

std::string normalize_root(const std::string& root) {
    return trim_trailing_separators(root);
}

bool make_relative_path(const std::string& root, const std::string& path, std::string* out) {
    if (!out) {
        return false;
    }
    const std::string normalized_root = normalize_root(root);
    if (normalized_root.empty()) {
        *out = path;
        return !path.empty();
    }
    if (path == normalized_root) {
        return false;
    }
    if (path.size() <= normalized_root.size()
        || path.compare(0, normalized_root.size(), normalized_root) != 0
        || !is_path_separator(path[normalized_root.size()])) {
        return false;
    }
    *out = path.substr(normalized_root.size() + 1);
    return !out->empty();
}

bool is_opc_indexed_source_file(const std::string& path) noexcept {
    return has_suffix_case_insensitive(path, ".opm2")
        || has_suffix_case_insensitive(path, ".bsp")
        || has_suffix_case_insensitive(path, ".spk")
        || has_suffix_case_insensitive(path, ".tke1")
        || has_suffix_case_insensitive(path, ".tkc1");
}

bool collect_indexed_source_paths_recursive(
    const std::string& root,
    const std::string& current,
    std::vector<std::string>* out
) {
    if (!out) {
        return false;
    }

    DIR* dir = opendir(current.c_str());
    if (!dir) {
        return false;
    }

    bool ok = true;
    while (dirent* entry = readdir(dir)) {
        const char* name = entry->d_name;
        if (std::strcmp(name, ".") == 0 || std::strcmp(name, "..") == 0) {
            continue;
        }

        const std::string path = join_path(current, name);
        if (directory_exists(path)) {
            if (!collect_indexed_source_paths_recursive(root, path, out)) {
                ok = false;
            }
        } else if (regular_file_exists(path) && is_opc_indexed_source_file(path)) {
            std::string relative;
            if (make_relative_path(root, path, &relative)) {
                out->push_back(relative);
            } else {
                ok = false;
            }
        } else if (!regular_file_exists(path)) {
            ok = false;
        }
    }

    closedir(dir);
    return ok;
}

bool string_offset_valid(const char* strings, size_t string_table_size, uint32_t offset) noexcept {
    if (!strings || offset >= string_table_size) {
        return false;
    }
    const char* start = strings + offset;
    const size_t remaining = string_table_size - offset;
    return std::memchr(start, '\0', remaining) != 0;
}

EphemerisFrame frame_from_record(int frame_id) noexcept {
    return frame_id == static_cast<int>(EphemerisFrame::IcrfJ2000Equatorial)
        ? EphemerisFrame::IcrfJ2000Equatorial
        : EphemerisFrame::FrameUnknown;
}

EphemerisBlockFormat format_from_record(uint32_t format) noexcept {
    switch (static_cast<EphemerisBlockFormat>(format)) {
        case EphemerisBlockFormat::Opm2:
            return EphemerisBlockFormat::Opm2;
        case EphemerisBlockFormat::Spk:
            return EphemerisBlockFormat::Spk;
        case EphemerisBlockFormat::Kepler:
            return EphemerisBlockFormat::Kepler;
        case EphemerisBlockFormat::Tkc1:
            return EphemerisBlockFormat::Tkc1;
        default:
            return EphemerisBlockFormat::FormatUnknown;
    }
}

bool descriptor_format_is_persistent(EphemerisBlockFormat format) noexcept {
    return format == EphemerisBlockFormat::Opm2
        || format == EphemerisBlockFormat::Spk
        || format == EphemerisBlockFormat::Kepler
        || format == EphemerisBlockFormat::Tkc1;
}

bool descriptor_record_valid(
    const char* strings,
    size_t string_table_size,
    const OpcDescriptorRecord& record
) noexcept {
    return record.target_id != record.center_id
        && record.method_id != 0
        && frame_from_record(record.frame_id) != EphemerisFrame::FrameUnknown
        && descriptor_format_is_persistent(format_from_record(record.format))
        && record.source_id != 0
        && record.generation != 0
        && record.cache_policy_kind <= static_cast<uint32_t>(CacheNaturalSegment)
        && std::isfinite(record.jd_tdb_start)
        && std::isfinite(record.jd_tdb_end)
        && record.jd_tdb_end > record.jd_tdb_start
        && std::isfinite(record.cache_origin_jd)
        && std::isfinite(record.cache_span_days)
        && string_offset_valid(strings, string_table_size, record.path_offset);
}

bool descriptor_valid_for_persistence(
    const EphemerisBlockDescriptor& descriptor,
    const std::string& root,
    std::string* out_relative_path
) noexcept {
    if (!out_relative_path) {
        return false;
    }
    out_relative_path->clear();
    if (descriptor.target_id == descriptor.center_id
        || descriptor.method_id == 0
        || descriptor.frame == EphemerisFrame::FrameUnknown
        || !descriptor_format_is_persistent(descriptor.format)
        || descriptor.source_key.source_id == 0
        || descriptor.source_key.generation == 0
        || !std::isfinite(descriptor.jd_tdb_start)
        || !std::isfinite(descriptor.jd_tdb_end)
        || descriptor.jd_tdb_end <= descriptor.jd_tdb_start
        || descriptor.route_key.target_id != descriptor.target_id
        || descriptor.route_key.center_id != descriptor.center_id
        || descriptor.route_key.method_id != descriptor.method_id
        || descriptor.path.empty()
        || !make_relative_path(root, descriptor.path, out_relative_path)
        || out_relative_path->size() >= static_cast<size_t>(std::numeric_limits<uint32_t>::max())) {
        return false;
    }
    return is_opc_indexed_source_file(descriptor.path);
}

uint32_t append_string(std::vector<char>* strings, const std::string& value) {
    if (!strings || value.empty()) {
        return 0;
    }
    const uint32_t offset = static_cast<uint32_t>(strings->size());
    strings->insert(strings->end(), value.begin(), value.end());
    strings->push_back('\0');
    return offset;
}

void append_bytes(std::vector<uint8_t>* bytes, const void* data, size_t size) {
    const uint8_t* raw = static_cast<const uint8_t*>(data);
    bytes->insert(bytes->end(), raw, raw + size);
}

}  // namespace

static_assert(sizeof(OpcHeader) == 128, "OpcHeader size must match OPC v1");
static_assert(sizeof(OpcDescriptorRecord) == 136, "OpcDescriptorRecord size must match OPC v3");

bool compute_opc_catalog_fingerprint(
    const std::string& root,
    uint64_t* out
) noexcept {
    if (!out || root.empty() || !directory_exists(root)) {
        return false;
    }

    try {
        const std::string normalized_root = normalize_root(root);
        std::vector<std::string> paths;
        if (!collect_indexed_source_paths_recursive(normalized_root, normalized_root, &paths)) {
            return false;
        }
        std::sort(paths.begin(), paths.end());

        uint64_t hash = FNV1A_64_OFFSET;
        for (size_t i = 0; i < paths.size(); ++i) {
            const std::string full_path = join_path(normalized_root, paths[i]);
            fnv1a_update_string(&hash, paths[i]);
            if (!fnv1a_update_file_identity(&hash, full_path)) {
                return false;
            }
        }

        *out = hash;
        return true;
    } catch (...) {
        return false;
    }
}

bool load_opc_persistent_catalog(
    const std::string& catalog_path,
    const std::string& root,
    std::vector<EphemerisBlockDescriptor>* out
) noexcept {
    if (!out || catalog_path.empty() || root.empty()) {
        return false;
    }

    try {
        out->clear();

        uint64_t current_fingerprint = 0;
        if (!compute_opc_catalog_fingerprint(root, &current_fingerprint)) {
            return false;
        }

        MappedFile file;
        if (!file.open_readonly(catalog_path)) {
            return false;
        }
        const uint8_t* data = file.data();
        const size_t size = file.size();
        if (!data || size < sizeof(OpcHeader) || !is_native_little_endian()) {
            return false;
        }

        const OpcHeader* header = reinterpret_cast<const OpcHeader*>(data);
        if (std::memcmp(header->magic, OPC_MAGIC, sizeof(OPC_MAGIC)) != 0
            || header->version != OPC_VERSION
            || header->descriptor_count == 0
            || header->fingerprint != current_fingerprint
            || header->source_id != OPC_SOURCE_ID
            || header->source_version != OPC_DISCOVERY_VERSION
            || header->generation != OPC_DISCOVERY_VERSION
            || !checked_array_range(size, header->descriptor_records_offset, header->descriptor_count, sizeof(OpcDescriptorRecord))
            || !checked_range(size, header->string_table_offset, header->string_table_size)
            || header->string_table_size > static_cast<uint64_t>(std::numeric_limits<size_t>::max())) {
            return false;
        }

        const OpcDescriptorRecord* records = reinterpret_cast<const OpcDescriptorRecord*>(
            data + static_cast<size_t>(header->descriptor_records_offset));
        const char* strings = reinterpret_cast<const char*>(data + static_cast<size_t>(header->string_table_offset));
        const size_t string_table_size = static_cast<size_t>(header->string_table_size);
        if (string_table_size == 0 || strings[0] != '\0') {
            return false;
        }

        out->reserve(static_cast<size_t>(header->descriptor_count));
        for (uint32_t i = 0; i < header->descriptor_count; ++i) {
            const OpcDescriptorRecord& record = records[i];
            if (!descriptor_record_valid(strings, string_table_size, record)) {
                out->clear();
                return false;
            }

            const char* relative_path = strings + record.path_offset;
            if (relative_path[0] == '\0' || relative_path[0] == '/' || relative_path[0] == '\\') {
                out->clear();
                return false;
            }

            EphemerisBlockDescriptor descriptor;
            descriptor.route_key = EphemerisRouteKey(
                record.target_id,
                record.center_id,
                record.method_id,
                record.bucket_id);
            descriptor.path = join_path(root, relative_path);
            const EphemerisBlockFormat format = format_from_record(record.format);
            const uint64_t source_id = format == EphemerisBlockFormat::Spk
                ? classify_spk_source_id_from_path(descriptor.path)
                : record.source_id;
            descriptor.source_key = EphemerisBlockKey(
                source_id,
                record.block_id,
                record.generation,
                record.purpose);
            descriptor.target_id = record.target_id;
            descriptor.center_id = record.center_id;
            descriptor.method_id = record.method_id;
            descriptor.frame = frame_from_record(record.frame_id);
            descriptor.format = format;
            descriptor.jd_tdb_start = record.jd_tdb_start;
            descriptor.jd_tdb_end = record.jd_tdb_end;
            descriptor.cache_policy.kind = static_cast<EphemerisCachePolicyKind>(record.cache_policy_kind);
            descriptor.cache_policy.origin_jd = record.cache_origin_jd;
            descriptor.cache_policy.span_days = record.cache_span_days;
            descriptor.cache_policy.first_index = record.cache_first_index;
            descriptor.cache_policy.count = record.cache_count;
            // object_index is persisted separately so TKC1 can locate the object
            // independently of source_key.block_id (which may be rekeyed for file
            // identity). For non-TKC1 formats it is 0 and unused.
            descriptor.object_index = record.object_index;
            out->push_back(descriptor);
        }
        return !out->empty();
    } catch (...) {
        out->clear();
        return false;
    }
}

bool write_opc_persistent_catalog(
    const std::string& catalog_path,
    const std::string& root,
    const std::vector<EphemerisBlockDescriptor>& descriptors
) noexcept {
    if (catalog_path.empty() || root.empty() || descriptors.empty()) {
        return false;
    }

    try {
        uint64_t fingerprint = 0;
        if (!compute_opc_catalog_fingerprint(root, &fingerprint)) {
            return false;
        }

        std::vector<EphemerisBlockDescriptor> sorted = descriptors;
        std::sort(sorted.begin(), sorted.end(), [](
            const EphemerisBlockDescriptor& lhs,
            const EphemerisBlockDescriptor& rhs) {
            if (lhs.target_id != rhs.target_id) return lhs.target_id < rhs.target_id;
            if (lhs.center_id != rhs.center_id) return lhs.center_id < rhs.center_id;
            if (lhs.jd_tdb_start != rhs.jd_tdb_start) return lhs.jd_tdb_start < rhs.jd_tdb_start;
            return lhs.path < rhs.path;
        });

        if (sorted.size() > static_cast<size_t>(std::numeric_limits<uint32_t>::max())) {
            return false;
        }

        std::vector<char> strings(1, '\0');
        std::vector<OpcDescriptorRecord> records;
        records.reserve(sorted.size());

        for (size_t i = 0; i < sorted.size(); ++i) {
            const EphemerisBlockDescriptor& descriptor = sorted[i];
            std::string relative_path;
            if (!descriptor_valid_for_persistence(descriptor, root, &relative_path)) {
                return false;
            }

            OpcDescriptorRecord record;
            std::memset(&record, 0, sizeof(record));
            record.target_id = descriptor.target_id;
            record.center_id = descriptor.center_id;
            record.method_id = descriptor.method_id;
            record.frame_id = static_cast<int32_t>(descriptor.frame);
            record.jd_tdb_start = descriptor.jd_tdb_start;
            record.jd_tdb_end = descriptor.jd_tdb_end;
            record.source_id = descriptor.source_key.source_id;
            record.block_id = descriptor.source_key.block_id;
            record.generation = descriptor.source_key.generation;
            record.purpose = descriptor.source_key.purpose;
            record.bucket_id = descriptor.route_key.bucket_id;
            record.format = static_cast<uint32_t>(descriptor.format);
            record.path_offset = append_string(&strings, relative_path);
            record.cache_policy_kind = static_cast<uint32_t>(descriptor.cache_policy.kind);
            record.cache_origin_jd = descriptor.cache_policy.origin_jd;
            record.cache_span_days = descriptor.cache_policy.span_days;
            record.cache_first_index = descriptor.cache_policy.first_index;
            record.cache_count = descriptor.cache_policy.count;
            record.object_index = descriptor.object_index;
            record.reserved = 0;
            if (!get_file_stat_metadata(
                    descriptor.path,
                    &record.file_size,
                    &record.file_mtime_sec,
                    &record.file_mtime_nsec)) {
                return false;
            }
            records.push_back(record);
        }

        OpcHeader header;
        std::memset(&header, 0, sizeof(header));
        header.magic[0] = 'O'; header.magic[1] = 'P'; header.magic[2] = 'C'; header.magic[3] = '1';
        header.version = OPC_VERSION;
        header.descriptor_count = static_cast<uint32_t>(records.size());
        header.descriptor_records_offset = sizeof(OpcHeader);
        header.string_table_offset = header.descriptor_records_offset
            + static_cast<uint64_t>(records.size()) * sizeof(OpcDescriptorRecord);
        header.string_table_size = strings.size();
        header.fingerprint = fingerprint;
        header.source_id = OPC_SOURCE_ID;
        header.source_version = OPC_DISCOVERY_VERSION;
        header.generation = OPC_DISCOVERY_VERSION;

        std::vector<uint8_t> bytes;
        append_bytes(&bytes, &header, sizeof(header));
        if (!records.empty()) {
            append_bytes(&bytes, &records[0], records.size() * sizeof(OpcDescriptorRecord));
        }
        append_bytes(&bytes, &strings[0], strings.size());

        std::ofstream file(catalog_path.c_str(), std::ios::binary | std::ios::trunc);
        if (!file) {
            return false;
        }
        file.write(reinterpret_cast<const char*>(&bytes[0]), static_cast<std::streamsize>(bytes.size()));
        return static_cast<bool>(file);
    } catch (...) {
        return false;
    }
}

bool collect_ephemeris_descriptors_from_catalog_or_directory(
    const std::string& root,
    const std::string& catalog_path,
    const std::vector<EphemerisDiscoverFileFn>& discoverers,
    const EphemerisDiscoveryOptions& options,
    std::vector<EphemerisBlockDescriptor>* out
) noexcept {
    if (!out) {
        return false;
    }

    if (!catalog_path.empty() && load_opc_persistent_catalog(catalog_path, root, out)) {
        return true;
    }

    if (!discover_ephemeris_descriptors_from_directory(root, discoverers, options, out)) {
        return false;
    }
    if (!catalog_path.empty()) {
        write_opc_persistent_catalog(catalog_path, root, *out);
    }
    return true;
}

bool discover_ephemeris_catalog_from_catalog_or_directory(
    const std::string& root,
    const std::string& catalog_path,
    const std::vector<EphemerisDiscoverFileFn>& discoverers,
    const EphemerisDiscoveryOptions& options,
    EphemerisBlockCatalog* out
) noexcept {
    if (!out) {
        return false;
    }

    std::vector<EphemerisBlockDescriptor> descriptors;
    if (!collect_ephemeris_descriptors_from_catalog_or_directory(
            root,
            catalog_path,
            discoverers,
            options,
            &descriptors)) {
        return false;
    }
    for (size_t i = 0; i < descriptors.size(); ++i) {
        if (!out->add(descriptors[i])) {
            return false;
        }
    }
    return true;
}

}  // namespace internal
}  // namespace taiyin
