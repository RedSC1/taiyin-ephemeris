#include "taiyin/internal/star_file.h"

#include "taiyin/internal/kepler_file.h"
#include "taiyin/internal/path_utils.h"

#include <algorithm>
#include <cerrno>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <limits>
#include <map>
#include <set>
#include <sstream>
#include <unordered_map>

#ifdef _WIN32
#include <direct.h>
#else
#include <sys/stat.h>
#include <sys/types.h>
#endif

namespace taiyin {
namespace internal {
namespace {

bool is_space(char c) noexcept {
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

std::string trim(const std::string& value) {
    size_t start = 0;
    while (start < value.size() && is_space(value[start])) {
        ++start;
    }
    size_t end = value.size();
    while (end > start && is_space(value[end - 1])) {
        --end;
    }
    return value.substr(start, end - start);
}

bool parse_double_value(const std::string& value, double* out) noexcept {
    if (!out || value.empty()) {
        return false;
    }
    char* end = 0;
    const double parsed = std::strtod(value.c_str(), &end);
    if (!end || *end != '\0') {
        return false;
    }
    *out = parsed;
    return true;
}

bool parse_int_value(const std::string& value, int* out) noexcept {
    if (!out || value.empty()) {
        return false;
    }
    char* end = 0;
    const long parsed = std::strtol(value.c_str(), &end, 10);
    if (!end || *end != '\0'
        || parsed < static_cast<long>(std::numeric_limits<int>::min())
        || parsed > static_cast<long>(std::numeric_limits<int>::max())) {
        return false;
    }
    *out = static_cast<int>(parsed);
    return true;
}

bool parse_size_value(const std::string& value, size_t* out) noexcept {
    if (!out || value.empty()) {
        return false;
    }
    char* end = 0;
    const unsigned long parsed = std::strtoul(value.c_str(), &end, 10);
    if (!end || *end != '\0' || parsed == 0) {
        return false;
    }
    *out = static_cast<size_t>(parsed);
    return true;
}

bool get_required_string(
    const std::unordered_map<std::string, std::string>& values,
    const std::string& key,
    std::string* out
) noexcept {
    if (!out) {
        return false;
    }
    std::unordered_map<std::string, std::string>::const_iterator it = values.find(key);
    if (it == values.end() || it->second.empty()) {
        return false;
    }
    *out = it->second;
    return true;
}

bool get_optional_string(
    const std::unordered_map<std::string, std::string>& values,
    const std::string& key,
    std::string* out
) noexcept {
    if (!out) {
        return false;
    }
    std::unordered_map<std::string, std::string>::const_iterator it = values.find(key);
    if (it == values.end()) {
        return true;
    }
    *out = it->second;
    return true;
}

bool get_required_double(
    const std::unordered_map<std::string, std::string>& values,
    const std::string& key,
    double* out
) noexcept {
    std::string value;
    return get_required_string(values, key, &value) && parse_double_value(value, out);
}

bool get_optional_double(
    const std::unordered_map<std::string, std::string>& values,
    const std::string& key,
    double* out
) noexcept {
    if (!out) {
        return false;
    }
    std::unordered_map<std::string, std::string>::const_iterator it = values.find(key);
    if (it == values.end()) {
        return true;
    }
    return parse_double_value(it->second, out);
}

std::string slugify_label(const std::string& label) {
    std::string slug;
    bool last_was_separator = false;
    for (size_t i = 0; i < label.size(); ++i) {
        const unsigned char raw = static_cast<unsigned char>(label[i]);
        if (std::isalnum(raw)) {
            slug.push_back(static_cast<char>(std::tolower(raw)));
            last_was_separator = false;
        } else if (raw == '_' || raw == '-' || std::isspace(raw)) {
            if (!slug.empty() && !last_was_separator) {
                slug.push_back('_');
                last_was_separator = true;
            }
        }
    }
    while (!slug.empty() && slug[slug.size() - 1] == '_') {
        slug.erase(slug.size() - 1);
    }
    return slug.empty() ? std::string("user_stars") : slug;
}

bool make_one_directory(const std::string& path) noexcept {
    if (path.empty() || directory_exists(path)) {
        return !path.empty();
    }
#ifdef _WIN32
    if (_mkdir(path.c_str()) == 0) {
        return true;
    }
#else
    if (mkdir(path.c_str(), 0755) == 0) {
        return true;
    }
#endif
    return errno == EEXIST && directory_exists(path);
}

bool make_directories(const std::string& path) noexcept {
    if (path.empty()) {
        return false;
    }
    try {
        std::string current;
        size_t index = 0;
        if (is_path_separator(path[0])) {
            current = path.substr(0, 1);
            index = 1;
        }
        while (index < path.size()) {
            while (index < path.size() && is_path_separator(path[index])) {
                ++index;
            }
            const size_t start = index;
            while (index < path.size() && !is_path_separator(path[index])) {
                ++index;
            }
            if (start == index) {
                continue;
            }
            current = join_path(current, path.substr(start, index - start));
            if (!make_one_directory(current)) {
                return false;
            }
        }
        return directory_exists(path);
    } catch (...) {
        return false;
    }
}

bool parse_star_file_values(
    const std::string& path,
    std::unordered_map<std::string, std::string>* values
) noexcept {
    if (!values || path.empty()) {
        return false;
    }
    try {
        std::ifstream file(path.c_str(), std::ios::in);
        if (!file) {
            return false;
        }
        values->clear();
        std::string line;
        bool saw_magic = false;
        while (std::getline(file, line)) {
            const std::string stripped = trim(line);
            if (stripped.empty() || stripped[0] == '#') {
                continue;
            }
            if (!saw_magic) {
                if (stripped != "TSF1") {
                    return false;
                }
                saw_magic = true;
                continue;
            }
            const size_t equals = stripped.find('=');
            if (equals == std::string::npos || equals == 0) {
                return false;
            }
            const std::string key = trim(stripped.substr(0, equals));
            const std::string value = trim(stripped.substr(equals + 1));
            if (key.empty() || value.empty()) {
                return false;
            }
            (*values)[key] = value;
        }
        return saw_magic;
    } catch (...) {
        values->clear();
        return false;
    }
}

std::vector<std::string> split_aliases(const std::string& value) {
    std::vector<std::string> aliases;
    std::istringstream stream(value);
    std::string item;
    while (std::getline(stream, item, ',')) {
        const std::string alias = trim(item);
        if (!alias.empty()) {
            aliases.push_back(alias);
        }
    }
    return aliases;
}

bool star_entry_is_valid(const Tsf1StarEntry& entry) noexcept {
    return !tsc1_normalize_alias(entry.id).empty()
        && std::isfinite(entry.ra_deg)
        && std::isfinite(entry.dec_deg)
        && entry.dec_deg >= -90.0
        && entry.dec_deg <= 90.0
        && std::isfinite(entry.pm_ra_mas_yr)
        && std::isfinite(entry.pm_dec_mas_yr)
        && std::isfinite(entry.parallax_mas)
        && entry.parallax_mas >= 0.0
        && std::isfinite(entry.radial_velocity_km_s)
        && std::isfinite(entry.reference_epoch);
}

class StringTableBuilder {
public:
    StringTableBuilder() : data_(1, '\0'), offsets_() {
        offsets_[""] = 0;
    }

    uint32_t add(const std::string& value) {
        std::map<std::string, uint32_t>::const_iterator found = offsets_.find(value);
        if (found != offsets_.end()) {
            return found->second;
        }
        const uint32_t offset = static_cast<uint32_t>(data_.size());
        data_.insert(data_.end(), value.begin(), value.end());
        data_.push_back('\0');
        offsets_[value] = offset;
        return offset;
    }

    const std::vector<char>& data() const noexcept {
        return data_;
    }

private:
    std::vector<char> data_;
    std::map<std::string, uint32_t> offsets_;
};

struct AliasBuildRecord {
    std::string alias;
    uint32_t star_index;
    uint64_t hash;
};

template <typename T>
void append_pod(std::vector<uint8_t>* bytes, const T& value) {
    const uint8_t* raw = reinterpret_cast<const uint8_t*>(&value);
    bytes->insert(bytes->end(), raw, raw + sizeof(T));
}

void append_aliases_for_entry(
    const Tsf1StarEntry& entry,
    uint32_t star_index,
    std::vector<AliasBuildRecord>* out
) {
    std::set<std::string> seen;
    const std::string canonical = tsc1_normalize_alias(entry.id);
    if (!canonical.empty()) {
        seen.insert(canonical);
    }
    const std::string normalized_name = tsc1_normalize_alias(entry.name.empty() ? entry.id : entry.name);
    if (!normalized_name.empty()) {
        seen.insert(normalized_name);
    }
    for (size_t i = 0; i < entry.aliases.size(); ++i) {
        const std::string normalized = tsc1_normalize_alias(entry.aliases[i]);
        if (!normalized.empty()) {
            seen.insert(normalized);
        }
    }
    for (std::set<std::string>::const_iterator it = seen.begin(); it != seen.end(); ++it) {
        AliasBuildRecord record;
        record.alias = *it;
        record.star_index = star_index;
        record.hash = tsc1_fnv1a_64(record.alias);
        out->push_back(record);
    }
}

std::string join_aliases(const std::vector<std::string>& aliases) {
    std::ostringstream out;
    for (size_t i = 0; i < aliases.size(); ++i) {
        if (i != 0) {
            out << ",";
        }
        out << aliases[i];
    }
    return out.str();
}

}  // namespace

Tsf1StarEntry::Tsf1StarEntry()
    : id(),
      name(),
      aliases(),
      magnitude(std::numeric_limits<double>::quiet_NaN()),
      ra_deg(0.0),
      dec_deg(0.0),
      pm_ra_mas_yr(0.0),
      pm_dec_mas_yr(0.0),
      parallax_mas(0.0),
      radial_velocity_km_s(0.0),
      reference_epoch(2000.0) {}

std::string default_user_star_directory() noexcept {
    return make_user_star_directory(TAIYIN_DEFAULT_DATA_DIRECTORY);
}

std::string make_user_star_directory(
    const std::string& data_root
) noexcept {
    try {
        const std::string root = trim_trailing_separators(
            data_root.empty() ? std::string(TAIYIN_DEFAULT_DATA_DIRECTORY) : data_root);
        return join_path(root, TAIYIN_USER_STAR_RELATIVE_DIRECTORY);
    } catch (...) {
        return std::string();
    }
}

std::string make_user_star_file_path(
    const std::string& data_root,
    const std::string& label
) noexcept {
    try {
        std::ostringstream name;
        name << slugify_label(label) << ".tsf1";
        return join_path(make_user_star_directory(data_root), name.str());
    } catch (...) {
        return std::string();
    }
}

bool save_user_star_file(
    const std::string& data_root,
    const std::string& label,
    const Tsf1StarEntry* entries,
    size_t entry_count,
    std::string* out_path
) noexcept {
    if (out_path) {
        out_path->clear();
    }
    const std::string directory = make_user_star_directory(data_root);
    const std::string path = make_user_star_file_path(data_root, label);
    if (directory.empty() || path.empty() || !make_directories(directory)) {
        return false;
    }
    if (!save_star_file(path, entries, entry_count)) {
        return false;
    }
    if (out_path) {
        *out_path = path;
    }
    return true;
}

bool save_star_file(
    const std::string& path,
    const Tsf1StarEntry* entries,
    size_t entry_count
) noexcept {
    if (path.empty() || !entries || entry_count == 0) {
        return false;
    }
    for (size_t i = 0; i < entry_count; ++i) {
        if (!star_entry_is_valid(entries[i])) {
            return false;
        }
    }
    try {
        std::ofstream file(path.c_str(), std::ios::out | std::ios::trunc);
        if (!file) {
            return false;
        }
        file << std::setprecision(17);
        file << "TSF1\n";
        file << "version=1\n";
        file << "star_count=" << entry_count << "\n\n";
        for (size_t i = 0; i < entry_count; ++i) {
            const Tsf1StarEntry& entry = entries[i];
            file << "star." << i << ".id=" << tsc1_normalize_alias(entry.id) << "\n";
            file << "star." << i << ".name=" << (entry.name.empty() ? entry.id : entry.name) << "\n";
            if (!entry.aliases.empty()) {
                file << "star." << i << ".aliases=" << join_aliases(entry.aliases) << "\n";
            }
            file << "star." << i << ".ra_deg=" << entry.ra_deg << "\n";
            file << "star." << i << ".dec_deg=" << entry.dec_deg << "\n";
            file << "star." << i << ".pm_ra_mas_yr=" << entry.pm_ra_mas_yr << "\n";
            file << "star." << i << ".pm_dec_mas_yr=" << entry.pm_dec_mas_yr << "\n";
            file << "star." << i << ".parallax_mas=" << entry.parallax_mas << "\n";
            file << "star." << i << ".radial_velocity_km_s=" << entry.radial_velocity_km_s << "\n";
            file << "star." << i << ".reference_epoch=" << entry.reference_epoch << "\n";
            if (!std::isnan(entry.magnitude)) {
                file << "star." << i << ".magnitude=" << entry.magnitude << "\n";
            }
            if (i + 1 < entry_count) {
                file << "\n";
            }
        }
        return static_cast<bool>(file);
    } catch (...) {
        return false;
    }
}

bool load_star_file(
    const std::string& path,
    std::vector<Tsf1StarEntry>* out_entries
) noexcept {
    if (!out_entries) {
        return false;
    }
    try {
        out_entries->clear();
        std::unordered_map<std::string, std::string> values;
        if (!parse_star_file_values(path, &values)) {
            return false;
        }
        int version = 0;
        size_t star_count = 0;
        std::string star_count_value;
        if (!get_required_string(values, "version", &star_count_value)
            || !parse_int_value(star_count_value, &version)
            || version != 1
            || !get_required_string(values, "star_count", &star_count_value)
            || !parse_size_value(star_count_value, &star_count)) {
            return false;
        }

        std::vector<Tsf1StarEntry> entries;
        entries.reserve(star_count);
        for (size_t i = 0; i < star_count; ++i) {
            std::ostringstream prefix;
            prefix << "star." << i << ".";
            Tsf1StarEntry entry;
            std::string aliases;
            if (!get_required_string(values, prefix.str() + "id", &entry.id)
                || !get_required_double(values, prefix.str() + "ra_deg", &entry.ra_deg)
                || !get_required_double(values, prefix.str() + "dec_deg", &entry.dec_deg)
                || !get_optional_string(values, prefix.str() + "name", &entry.name)
                || !get_optional_string(values, prefix.str() + "aliases", &aliases)
                || !get_optional_double(values, prefix.str() + "pm_ra_mas_yr", &entry.pm_ra_mas_yr)
                || !get_optional_double(values, prefix.str() + "pm_dec_mas_yr", &entry.pm_dec_mas_yr)
                || !get_optional_double(values, prefix.str() + "parallax_mas", &entry.parallax_mas)
                || !get_optional_double(values, prefix.str() + "radial_velocity_km_s", &entry.radial_velocity_km_s)
                || !get_optional_double(values, prefix.str() + "reference_epoch", &entry.reference_epoch)
                || !get_optional_double(values, prefix.str() + "magnitude", &entry.magnitude)) {
                return false;
            }
            if (entry.name.empty()) {
                entry.name = entry.id;
            }
            entry.id = tsc1_normalize_alias(entry.id);
            entry.aliases = split_aliases(aliases);
            if (!star_entry_is_valid(entry)) {
                return false;
            }
            entries.push_back(entry);
        }
        *out_entries = entries;
        return true;
    } catch (...) {
        out_entries->clear();
        return false;
    }
}

bool build_tsc1_catalog_bytes_from_star_entries(
    const Tsf1StarEntry* entries,
    size_t entry_count,
    std::vector<uint8_t>* out_bytes
) noexcept {
    if (!out_bytes) {
        return false;
    }
    try {
        out_bytes->clear();
        if (!entries || entry_count == 0
            || entry_count > static_cast<size_t>(std::numeric_limits<uint32_t>::max())) {
            return false;
        }

        StringTableBuilder strings;
        std::vector<Tsc1StarRecord> stars(entry_count);
        std::vector<AliasBuildRecord> alias_build_records;
        double min_epoch = std::numeric_limits<double>::infinity();
        double max_epoch = -std::numeric_limits<double>::infinity();

        for (size_t i = 0; i < entry_count; ++i) {
            const Tsf1StarEntry& entry = entries[i];
            if (!star_entry_is_valid(entry)) {
                return false;
            }
            const std::string canonical = tsc1_normalize_alias(entry.id);
            const std::string display_name = entry.name.empty() ? entry.id : entry.name;
            Tsc1StarRecord record;
            std::memset(&record, 0, sizeof(record));
            record.canonical_id_offset = strings.add(canonical);
            record.display_name_offset = strings.add(display_name);
            record.ra_deg = entry.ra_deg;
            record.dec_deg = entry.dec_deg;
            record.pm_ra_mas_yr = entry.pm_ra_mas_yr;
            record.pm_dec_mas_yr = entry.pm_dec_mas_yr;
            record.parallax_mas = entry.parallax_mas;
            record.radial_velocity_km_s = entry.radial_velocity_km_s;
            record.reference_epoch = entry.reference_epoch;
            record.magnitude = static_cast<float>(entry.magnitude);
            record.astrometry_source = TSC1_SOURCE_MANUAL;
            record.flags = 0;
            if (entry.parallax_mas > 0.0) {
                record.flags |= TSC1_STAR_HAS_PARALLAX;
            } else {
                record.flags |= TSC1_STAR_SPECIAL_DIRECTION;
            }
            if (entry.radial_velocity_km_s != 0.0) {
                record.flags |= TSC1_STAR_HAS_RADIAL_VELOCITY;
            }
            stars[i] = record;
            min_epoch = std::min(min_epoch, entry.reference_epoch);
            max_epoch = std::max(max_epoch, entry.reference_epoch);
            append_aliases_for_entry(entry, static_cast<uint32_t>(i), &alias_build_records);
        }
        if (alias_build_records.empty() || !std::isfinite(min_epoch) || !std::isfinite(max_epoch)) {
            return false;
        }
        std::sort(alias_build_records.begin(), alias_build_records.end(), [](const AliasBuildRecord& lhs, const AliasBuildRecord& rhs) {
            if (lhs.hash != rhs.hash) {
                return lhs.hash < rhs.hash;
            }
            return lhs.alias < rhs.alias;
        });

        std::vector<Tsc1AliasEntry> aliases;
        aliases.reserve(alias_build_records.size());
        for (size_t i = 0; i < alias_build_records.size(); ++i) {
            Tsc1AliasEntry alias;
            alias.alias_offset = strings.add(alias_build_records[i].alias);
            alias.star_index = alias_build_records[i].star_index;
            alias.alias_hash = alias_build_records[i].hash;
            aliases.push_back(alias);
        }

        Tsc1Header header;
        std::memset(&header, 0, sizeof(header));
        header.magic[0] = 'T';
        header.magic[1] = 'S';
        header.magic[2] = 'C';
        header.magic[3] = '1';
        header.version = TSC1_VERSION;
        header.star_count = static_cast<uint32_t>(stars.size());
        header.alias_count = static_cast<uint32_t>(aliases.size());
        header.star_records_offset = sizeof(Tsc1Header);
        header.alias_records_offset = header.star_records_offset + stars.size() * sizeof(Tsc1StarRecord);
        header.string_table_offset = header.alias_records_offset + aliases.size() * sizeof(Tsc1AliasEntry);
        header.string_table_size = strings.data().size();
        header.catalog_min_epoch = min_epoch;
        header.catalog_max_epoch = max_epoch;

        std::vector<uint8_t> bytes;
        append_pod(&bytes, header);
        for (size_t i = 0; i < stars.size(); ++i) {
            append_pod(&bytes, stars[i]);
        }
        for (size_t i = 0; i < aliases.size(); ++i) {
            append_pod(&bytes, aliases[i]);
        }
        const std::vector<char>& string_data = strings.data();
        bytes.insert(bytes.end(), string_data.begin(), string_data.end());

        Tsc1Catalog catalog;
        if (!tsc1_catalog_load_from_memory(&catalog, bytes.empty() ? 0 : &bytes[0], bytes.size())) {
            return false;
        }
        tsc1_catalog_destroy(&catalog);
        *out_bytes = bytes;
        return true;
    } catch (...) {
        out_bytes->clear();
        return false;
    }
}

bool build_tsc1_catalog_bytes_from_star_file(
    const std::string& path,
    std::vector<uint8_t>* out_bytes
) noexcept {
    if (!out_bytes) {
        return false;
    }
    std::vector<Tsf1StarEntry> entries;
    if (!load_star_file(path, &entries) || entries.empty()) {
        out_bytes->clear();
        return false;
    }
    return build_tsc1_catalog_bytes_from_star_entries(&entries[0], entries.size(), out_bytes);
}

EphemerisDiscoveryStatus discover_star_file(
    const std::string& path,
    const EphemerisDiscoveryOptions&,
    std::vector<EphemerisBlockDescriptor>* out
) noexcept {
    if (!out || !has_suffix(path, ".tsf1")) {
        return DiscoveryNotApplicable;
    }
    try {
        std::vector<Tsf1StarEntry> entries;
        if (!load_star_file(path, &entries) || entries.empty()) {
            return DiscoveryError;
        }
        const uint64_t block_id = static_cast<uint64_t>(out->size() + 1);
        if (block_id > static_cast<uint64_t>(std::numeric_limits<int>::max())) {
            return DiscoveryError;
        }
        EphemerisBlockDescriptor descriptor;
        descriptor.route_key = EphemerisRouteKey(
            0,
            TSC1_DEFAULT_CENTER_ID,
            TSC1_STAR_METHOD_ID,
            static_cast<int>(block_id));
        descriptor.source_key = EphemerisBlockKey(
            TAIYIN_STAR_FILE_SOURCE_ID,
            block_id,
            TAIYIN_STAR_FILE_GENERATION,
            TAIYIN_STAR_FILE_PURPOSE);
        descriptor.target_id = 0;
        descriptor.center_id = TSC1_DEFAULT_CENTER_ID;
        descriptor.method_id = TSC1_STAR_METHOD_ID;
        descriptor.frame = EphemerisFrame::IcrfJ2000Equatorial;
        descriptor.format = EphemerisBlockFormat::Tsc1;
        descriptor.jd_tdb_start = TSC1_DEFAULT_JD_TDB_START;
        descriptor.jd_tdb_end = TSC1_DEFAULT_JD_TDB_END;
        descriptor.path = path;
        out->push_back(descriptor);
        return DiscoveryOk;
    } catch (...) {
        return DiscoveryError;
    }
}

void append_star_file_discoverer(
    std::vector<EphemerisDiscoverFileFn>* out
) noexcept {
    if (!out) {
        return;
    }
    try {
        out->push_back(&discover_star_file);
    } catch (...) {
    }
}

bool collect_star_file_descriptors_from_directory(
    const std::string& root,
    std::vector<EphemerisBlockDescriptor>* out
) noexcept {
    if (!out) {
        return false;
    }
    std::vector<EphemerisDiscoverFileFn> discoverers;
    append_star_file_discoverer(&discoverers);
    EphemerisDiscoveryOptions options;
    options.strict = true;
    return discover_ephemeris_descriptors_from_directory(root, discoverers, options, out);
}

bool discover_star_file_catalog_from_directory(
    const std::string& root,
    EphemerisBlockCatalog* out
) noexcept {
    if (!out) {
        return false;
    }
    std::vector<EphemerisDiscoverFileFn> discoverers;
    append_star_file_discoverer(&discoverers);
    EphemerisDiscoveryOptions options;
    options.strict = true;
    return discover_ephemeris_catalog_from_directory(root, discoverers, options, out);
}

}  // namespace internal
}  // namespace taiyin
