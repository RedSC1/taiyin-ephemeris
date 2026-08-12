#include "taiyin/runtime/runtime.h"

#include "taiyin/body_id.h"
#include "taiyin/internal/descriptor_loader.h"
#include "taiyin/internal/builtin_loader.h"
#include "taiyin/internal/eop.h"
#include "taiyin/internal/ephemeris_discovery.h"
#include "taiyin/internal/kepler_catalog_tkc1.h"
#include "taiyin/internal/kepler_file.h"
#include "taiyin/internal/semi_analytic.h"
#include "taiyin/internal/opc_catalog_persistent.h"
#include "taiyin/internal/opm2.h"
#include "taiyin/internal/opm2_catalog_discovery.h"
#include "taiyin/internal/path_utils.h"
#include "taiyin/internal/spk_catalog_discovery.h"
#include "taiyin/internal/writer_preferred_rwlock.h"
#include "taiyin/lunar_limb_tll1.h"
#include "taiyin/runtime/builtin_body_rules.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <new>
#include <string>
#include <sys/stat.h>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace taiyin {
namespace runtime {
namespace {

const size_t DEFAULT_EPHEMERIS_SEGMENT_CACHE_ENTRIES = 512u;
const char* TAIYIN_DATA_ROOT_ENV = "TAIYIN_DATA_ROOT";
const char* PACKAGED_CATALOG_NAME = "index.opc";
const char* PACKAGED_DATA_RELATIVE_ROOT = "data";

internal::WriterPreferredRwLock& global_ephemeris_runtime_rwlock() {
    static internal::WriterPreferredRwLock lock;
    return lock;
}

bool discover_ephemeris_descriptors_from_file(
    const std::string& path,
    const std::vector<internal::EphemerisDiscoverFileFn>& discoverers,
    const internal::EphemerisDiscoveryOptions& options,
    std::vector<internal::EphemerisBlockDescriptor>* out
) noexcept {
    if (!out || path.empty()) {
        return false;
    }

    try {
        out->clear();
        for (size_t i = 0; i < discoverers.size(); ++i) {
            internal::EphemerisDiscoverFileFn discoverer = discoverers[i];
            if (!discoverer) {
                continue;
            }

            const size_t before = out->size();
            const internal::EphemerisDiscoveryStatus status = discoverer(path, options, out);
            if (status == internal::DiscoveryNotApplicable) {
                continue;
            }
            if (status == internal::DiscoveryOk) {
                if (out->size() == before) {
                    out->clear();
                    return false;
                }
                return true;
            }

            out->resize(before);
            return false;
        }
    } catch (...) {
        out->clear();
        return false;
    }

    return false;
}

bool regular_file_exists(const std::string& path) noexcept {
    struct stat st;
    return stat(path.c_str(), &st) == 0 && S_ISREG(st.st_mode);
}

bool same_path_string(const std::string& lhs, const std::string& rhs) {
    return internal::trim_trailing_separators(lhs) == internal::trim_trailing_separators(rhs);
}

std::string final_path_component(const std::string& path) {
    const std::string trimmed = internal::trim_trailing_separators(path);
    if (trimmed.empty()) {
        return std::string();
    }
    size_t pos = trimmed.size();
    while (pos > 0 && !internal::is_path_separator(trimmed[pos - 1])) {
        --pos;
    }
    return trimmed.substr(pos);
}

bool append_unique_path(std::vector<std::string>* out, const std::string& path) {
    if (!out || path.empty()) {
        return false;
    }
    const std::string normalized = internal::trim_trailing_separators(path);
    if (normalized.empty()) {
        return false;
    }
    for (size_t i = 0; i < out->size(); ++i) {
        if (same_path_string((*out)[i], normalized)) {
            return true;
        }
    }
    out->push_back(normalized);
    return true;
}

bool root_supports_packaged_catalog(const std::string& root) {
    const std::string component = final_path_component(root);
    return component == "data" || component == "opm2" || component == "sbdb";
}

bool should_try_packaged_catalog(const std::string& root, std::string* catalog_path) {
    if (catalog_path) {
        *catalog_path = std::string();
    }
    if (root.empty()) {
        return false;
    }

    const std::string candidate = internal::join_path(root, PACKAGED_CATALOG_NAME);
    if (regular_file_exists(candidate)) {
        if (catalog_path) {
            *catalog_path = candidate;
        }
        return true;
    }

    if (root_supports_packaged_catalog(root)) {
        if (catalog_path) {
            *catalog_path = candidate;
        }
        return true;
    }
    return false;
}

void append_packaged_source_roots_from_base(
    const std::string& base,
    std::vector<std::string>* out
) {
    if (!out || base.empty()) {
        return;
    }

    const std::string normalized = internal::trim_trailing_separators(base);
    if (normalized.empty()) {
        return;
    }

    const std::string component = final_path_component(normalized);
    if (component == "data" || component == "opm2" || component == "sbdb") {
        append_unique_path(out, normalized);
        return;
    }

    append_unique_path(out, internal::join_path(normalized, PACKAGED_DATA_RELATIVE_ROOT));
}

void collect_packaged_source_roots(
    const EphemerisRuntimeConfig& config,
    std::vector<std::string>* out
) {
    if (!out || !config.load_packaged_data) {
        return;
    }

    if (config.data_root && config.data_root[0] != '\0') {
        append_packaged_source_roots_from_base(config.data_root, out);
    }

    const char* env_root = std::getenv(TAIYIN_DATA_ROOT_ENV);
    if (env_root && env_root[0] != '\0') {
        append_packaged_source_roots_from_base(env_root, out);
    }

#ifdef TAIYIN_PACKAGED_DATA_ROOT
    append_packaged_source_roots_from_base(TAIYIN_PACKAGED_DATA_ROOT, out);
#endif

    append_packaged_source_roots_from_base("data", out);
    append_packaged_source_roots_from_base("../data", out);
}

bool discover_packaged_ephemeris_descriptors(
    const std::string& root,
    const std::vector<internal::EphemerisDiscoverFileFn>& discoverers,
    const internal::EphemerisDiscoveryOptions& options,
    std::vector<internal::EphemerisBlockDescriptor>* out
) noexcept {
    if (!out) {
        return false;
    }

    std::string catalog_path;
    try {
        if (!should_try_packaged_catalog(root, &catalog_path)) {
            return false;
        }
    } catch (...) {
        return false;
    }
    return internal::collect_ephemeris_descriptors_from_catalog_or_directory(
        root,
        catalog_path,
        discoverers,
        options,
        out);
}

bool collect_descriptors_from_source_path_string(
    const std::string& source_path,
    bool strict_discovery,
    std::vector<internal::EphemerisBlockDescriptor>* out
) noexcept {
    if (source_path.empty() || !out) {
        return false;
    }

    struct stat st;
    if (stat(source_path.c_str(), &st) != 0) {
        return false;
    }

    std::vector<internal::EphemerisDiscoverFileFn> discoverers;
    internal::append_builtin_ephemeris_discoverers(&discoverers);
    if (discoverers.empty()) {
        return false;
    }

    internal::EphemerisDiscoveryOptions options;
    options.strict = strict_discovery;

    std::vector<internal::EphemerisBlockDescriptor> descriptors;
    bool discovered = false;
    if (S_ISDIR(st.st_mode)) {
        discovered = discover_packaged_ephemeris_descriptors(
            source_path,
            discoverers,
            options,
            &descriptors);
        if (!discovered) {
            discovered = internal::discover_ephemeris_descriptors_from_directory(
                source_path,
                discoverers,
                options,
                &descriptors);
        }
    } else if (S_ISREG(st.st_mode)) {
        discovered = discover_ephemeris_descriptors_from_file(
            source_path,
            discoverers,
            options,
            &descriptors);
    } else {
        return false;
    }

    if (!discovered || descriptors.empty()) {
        return false;
    }

    try {
        *out = descriptors;
    } catch (...) {
        out->clear();
        return false;
    }
    return true;
}

bool descriptor_ranges_overlap(
    const internal::EphemerisBlockDescriptor& lhs,
    const internal::EphemerisBlockDescriptor& rhs
) noexcept {
    return lhs.jd_tdb_start < rhs.jd_tdb_end && rhs.jd_tdb_start < lhs.jd_tdb_end;
}

bool catalog_has_overlapping_same_route_descriptor(
    const internal::EphemerisBlockCatalog& catalog,
    const internal::EphemerisBlockDescriptor& descriptor
) noexcept {
    for (size_t i = 0; i < catalog.size(); ++i) {
        internal::EphemerisBlockDescriptor existing;
        if (!catalog.get(i, &existing)) {
            continue;
        }
        if (existing.target_id == descriptor.target_id
            && existing.center_id == descriptor.center_id
            && existing.method_id == descriptor.method_id
            && existing.frame == descriptor.frame
            && descriptor_ranges_overlap(existing, descriptor)) {
            return true;
        }
    }
    return false;
}

// Overlap checks in add_packaged_source_roots compared every incoming
// descriptor against every catalog entry through catalog.get(), which
// takes the catalog lock and copies a descriptor per element, O(n^2)
// locked copies per batch. Keep a local route -> coverage-interval list
// instead; it is seeded once from the catalog and extended as adds land,
// which preserves the original ordering and skip semantics exactly.
struct RouteCoverageKey {
    int target_id;
    int center_id;
    int method_id;
    int frame;
    bool operator==(const RouteCoverageKey& other) const noexcept {
        return target_id == other.target_id
            && center_id == other.center_id
            && method_id == other.method_id
            && frame == other.frame;
    }
};

struct RouteCoverageKeyHash {
    size_t operator()(const RouteCoverageKey& key) const noexcept {
        uint64_t h = 1469598103934665603ull;
        h ^= static_cast<uint32_t>(key.target_id);
        h *= 1099511628211ull;
        h ^= static_cast<uint32_t>(key.center_id);
        h *= 1099511628211ull;
        h ^= static_cast<uint32_t>(key.method_id);
        h *= 1099511628211ull;
        h ^= static_cast<uint32_t>(key.frame);
        h *= 1099511628211ull;
        return static_cast<size_t>(h);
    }
};

struct RouteCoverageInterval {
    double jd_tdb_start;
    double jd_tdb_end;
};

typedef std::unordered_map<RouteCoverageKey, std::vector<RouteCoverageInterval>, RouteCoverageKeyHash>
    RouteCoverageMap;

RouteCoverageKey route_coverage_key_of(
    const internal::EphemerisBlockDescriptor& descriptor
) noexcept {
    RouteCoverageKey key;
    key.target_id = descriptor.target_id;
    key.center_id = descriptor.center_id;
    key.method_id = descriptor.method_id;
    key.frame = static_cast<int>(descriptor.frame);
    return key;
}

bool route_coverage_has_overlap(
    const RouteCoverageMap& coverage,
    const internal::EphemerisBlockDescriptor& descriptor
) noexcept {
    const RouteCoverageMap::const_iterator it = coverage.find(route_coverage_key_of(descriptor));
    if (it == coverage.end()) {
        return false;
    }
    for (size_t i = 0; i < it->second.size(); ++i) {
        if (descriptor.jd_tdb_start < it->second[i].jd_tdb_end
            && it->second[i].jd_tdb_start < descriptor.jd_tdb_end) {
            return true;
        }
    }
    return false;
}

void route_coverage_append(
    RouteCoverageMap* coverage,
    const internal::EphemerisBlockDescriptor& descriptor
) {
    if (!coverage) {
        return;
    }
    RouteCoverageInterval interval;
    interval.jd_tdb_start = descriptor.jd_tdb_start;
    interval.jd_tdb_end = descriptor.jd_tdb_end;
    (*coverage)[route_coverage_key_of(descriptor)].push_back(interval);
}

bool seed_route_coverage_from_catalog(
    const internal::EphemerisBlockCatalog& catalog,
    RouteCoverageMap* coverage
) {
    if (!coverage) {
        return false;
    }
    for (size_t i = 0; i < catalog.size(); ++i) {
        internal::EphemerisBlockDescriptor existing;
        if (!catalog.get(i, &existing)) {
            continue;
        }
        route_coverage_append(coverage, existing);
    }
    return true;
}

void add_source_index_if_missing(
    const internal::EphemerisBlockDescriptor& descriptor,
    internal::EphemerisBlockCatalog* catalog
) noexcept {
    if (!catalog || descriptor.source_key.source_id == 0 || descriptor.path.empty()) {
        return;
    }
    if (descriptor.format == internal::EphemerisBlockFormat::Opm2) {
        internal::EphemerisSourceIndex metadata;
        metadata.source_key = descriptor.source_key;
        metadata.format = descriptor.format;
        metadata.path = descriptor.path;
        catalog->add_source_index(metadata);
        return;
    }
    internal::EphemerisSourceIndex existing;
    if (catalog->find_source_index(descriptor.source_key, &existing)) {
        return;
    }
    internal::EphemerisSourceIndex index;
    if (internal::load_descriptor_source_index(descriptor, &index)) {
        catalog->add_source_index(index);
    }
}

struct RuntimeSourceKeyAssignment {
    internal::EphemerisBlockKey original;
    internal::EphemerisBlockFormat format;
    std::string path;
    internal::EphemerisBlockKey assigned;
};

bool descriptor_source_key_is_file_identity(
    internal::EphemerisBlockFormat format
) noexcept {
    return format == internal::EphemerisBlockFormat::Opm2
        || format == internal::EphemerisBlockFormat::Spk
        || format == internal::EphemerisBlockFormat::Kepler
        || format == internal::EphemerisBlockFormat::Tkc1
        || format == internal::EphemerisBlockFormat::Tsc1;
}

bool find_runtime_source_key_assignment(
    const std::vector<RuntimeSourceKeyAssignment>& assignments,
    const internal::EphemerisBlockKey& original,
    internal::EphemerisBlockFormat format,
    const std::string& path,
    internal::EphemerisBlockKey* out
) {
    if (!out) {
        return false;
    }
    for (size_t i = 0; i < assignments.size(); ++i) {
        if (assignments[i].original == original
            && assignments[i].format == format
            && assignments[i].path == path) {
            *out = assignments[i].assigned;
            return true;
        }
    }
    return false;
}

void append_runtime_source_key_assignment(
    std::vector<RuntimeSourceKeyAssignment>* assignments,
    const internal::EphemerisBlockKey& original,
    internal::EphemerisBlockFormat format,
    const std::string& path,
    const internal::EphemerisBlockKey& assigned
) {
    RuntimeSourceKeyAssignment entry;
    entry.original = original;
    entry.format = format;
    entry.path = path;
    entry.assigned = assigned;
    assignments->push_back(entry);
}

uint64_t next_runtime_source_block_id(
    internal::EphemerisBlockCatalog* catalog,
    uint64_t source_id,
    uint32_t generation,
    uint32_t purpose,
    uint64_t candidate,
    const std::unordered_set<internal::EphemerisBlockKey, internal::EphemerisBlockKeyHash>& reserved
) noexcept {
    if (candidate == 0) {
        candidate = 1;
    }
    internal::EphemerisSourceIndex existing;
    while (true) {
        const internal::EphemerisBlockKey key(source_id, candidate, generation, purpose);
        if (reserved.find(key) != reserved.end()
            || (catalog && catalog->find_source_index(key, &existing))) {
            ++candidate;
            if (candidate == 0) {
                candidate = 1;
            }
            continue;
        }
        break;
    }
    return candidate;
}

// TKC1 files carry per-object descriptors whose original block_id is the
// object index, which restarts at 0 for every file. Assigning keys
// descriptor-by-descriptor makes next_runtime_source_block_id re-scan the
// reserved set once per already-assigned object, O(n^2) per file. Group
// descriptors by physical file and allocate one contiguous block_id range
// per file instead, so each file probes the reserved set once.
struct Tkc1FileGroup {
    std::vector<size_t> indices;
    uint64_t source_id;
    uint32_t generation;
    uint32_t purpose;
};

struct Tkc1FileGroupKey {
    std::string path;
    uint64_t source_id;
    uint32_t generation;
    uint32_t purpose;
    bool operator==(const Tkc1FileGroupKey& other) const {
        return path == other.path
            && source_id == other.source_id
            && generation == other.generation
            && purpose == other.purpose;
    }
};

struct Tkc1FileGroupKeyHash {
    size_t operator()(const Tkc1FileGroupKey& key) const noexcept {
        size_t hash = std::hash<std::string>()(key.path);
        hash = hash * 0x9e3779b97f4a7c15ull ^ static_cast<size_t>(key.source_id);
        hash = hash * 0x9e3779b97f4a7c15ull ^ static_cast<size_t>(key.generation);
        hash = hash * 0x9e3779b97f4a7c15ull ^ static_cast<size_t>(key.purpose);
        return hash;
    }
};

bool assign_tkc1_file_group_source_keys(
    const Tkc1FileGroup& group,
    std::vector<internal::EphemerisBlockDescriptor>* descriptors,
    internal::EphemerisBlockCatalog* catalog,
    std::unordered_set<internal::EphemerisBlockKey, internal::EphemerisBlockKeyHash>* reserved_keys
) {
    if (!descriptors || !catalog || !reserved_keys) {
        return false;
    }

    std::vector<uint64_t> object_indexes;
    object_indexes.reserve(group.indices.size());
    bool all_original_free = true;
    for (size_t i = 0; i < group.indices.size(); ++i) {
        const internal::EphemerisBlockDescriptor& descriptor = (*descriptors)[group.indices[i]];
        const internal::EphemerisBlockKey& original = descriptor.source_key;
        internal::EphemerisSourceIndex existing;
        const bool free = (!catalog->find_source_index(original, &existing) || existing.path == descriptor.path)
            && reserved_keys->find(original) == reserved_keys->end();
        if (!free) {
            all_original_free = false;
        }
        object_indexes.push_back(original.block_id);
    }

    std::sort(object_indexes.begin(), object_indexes.end());
    for (size_t i = 1; i < object_indexes.size(); ++i) {
        if (object_indexes[i] <= object_indexes[i - 1]) {
            return false;
        }
    }

    if (all_original_free) {
        for (size_t i = 0; i < group.indices.size(); ++i) {
            reserved_keys->insert((*descriptors)[group.indices[i]].source_key);
        }
        return true;
    }

    const uint64_t span = object_indexes.back() - object_indexes.front();
    if (span >= std::numeric_limits<uint64_t>::max() - 1) {
        return false;
    }
    // The whole [base, base+span] range must be free, not just its endpoints:
    // the catalog may already hold non-contiguous TKC1 keys (e.g. overlap
    // filtering skipped some descriptors of an earlier root, or a partial
    // user/persistent catalog), and checking only the ends would let a new
    // descriptor reuse an occupied interior key and alias source identity.
    uint64_t base = next_runtime_source_block_id(
        catalog,
        group.source_id,
        group.generation,
        group.purpose,
        object_indexes.front() + 1,
        *reserved_keys);
    while (true) {
        uint64_t first_occupied = 0;
        bool found_occupied = false;
        for (uint64_t block_id = base; block_id <= base + span; ++block_id) {
            const internal::EphemerisBlockKey key(
                group.source_id, block_id, group.generation, group.purpose);
            internal::EphemerisSourceIndex existing;
            if (reserved_keys->find(key) != reserved_keys->end()
                || catalog->find_source_index(key, &existing)) {
                first_occupied = block_id;
                found_occupied = true;
                break;
            }
            if (block_id == base + span) {
                break;  // last slot verified free; avoid block_id wrap on ++
            }
        }
        if (!found_occupied) {
            break;
        }
        base = next_runtime_source_block_id(
            catalog,
            group.source_id,
            group.generation,
            group.purpose,
            first_occupied + 1,
            *reserved_keys);
    }

    for (size_t i = 0; i < group.indices.size(); ++i) {
        internal::EphemerisBlockDescriptor& descriptor = (*descriptors)[group.indices[i]];
        const uint64_t block_id = base + (descriptor.source_key.block_id - object_indexes.front());
        const internal::EphemerisBlockKey assigned(
            group.source_id, block_id, group.generation, group.purpose);
        descriptor.source_key = assigned;
        reserved_keys->insert(assigned);
    }
    return true;
}

bool assign_runtime_catalog_source_keys(
    std::vector<internal::EphemerisBlockDescriptor>* descriptors,
    internal::EphemerisBlockCatalog* catalog
) noexcept {
    if (!descriptors || !catalog) {
        return false;
    }

    try {
        std::vector<RuntimeSourceKeyAssignment> assignments;
        std::unordered_set<internal::EphemerisBlockKey, internal::EphemerisBlockKeyHash> reserved_keys;
        std::unordered_map<Tkc1FileGroupKey, size_t, Tkc1FileGroupKeyHash> tkc1_group_index;
        std::vector<Tkc1FileGroup> tkc1_groups;
        for (size_t i = 0; i < descriptors->size(); ++i) {
            internal::EphemerisBlockDescriptor& descriptor = (*descriptors)[i];
            const internal::EphemerisBlockKey original = descriptor.source_key;
            if (original.source_id == 0) {
                continue;
            }
            if (!descriptor_source_key_is_file_identity(descriptor.format)) {
                reserved_keys.insert(original);
                continue;
            }

            if (descriptor.format == internal::EphemerisBlockFormat::Tkc1) {
                Tkc1FileGroupKey key;
                key.path = descriptor.path;
                key.source_id = original.source_id;
                key.generation = original.generation;
                key.purpose = original.purpose;
                const std::unordered_map<Tkc1FileGroupKey, size_t, Tkc1FileGroupKeyHash>::iterator it =
                    tkc1_group_index.find(key);
                if (it != tkc1_group_index.end()) {
                    tkc1_groups[it->second].indices.push_back(i);
                } else {
                    Tkc1FileGroup group;
                    group.source_id = original.source_id;
                    group.generation = original.generation;
                    group.purpose = original.purpose;
                    group.indices.push_back(i);
                    tkc1_group_index.insert(std::make_pair(key, tkc1_groups.size()));
                    tkc1_groups.push_back(group);
                }
                continue;
            }

            internal::EphemerisBlockKey assigned_key;
            if (find_runtime_source_key_assignment(
                    assignments,
                    original,
                    descriptor.format,
                    descriptor.path,
                    &assigned_key)) {
                descriptor.source_key = assigned_key;
                continue;
            }

            internal::EphemerisSourceIndex existing;
            const bool original_exists_in_catalog = catalog->find_source_index(original, &existing);
            if ((!original_exists_in_catalog || existing.path == descriptor.path)
                && reserved_keys.find(original) == reserved_keys.end()) {
                append_runtime_source_key_assignment(
                    &assignments,
                    original,
                    descriptor.format,
                    descriptor.path,
                    original);
                reserved_keys.insert(original);
                continue;
            }

            uint64_t candidate = original.block_id + 1;
            const uint64_t next_block_id = next_runtime_source_block_id(
                catalog,
                original.source_id,
                original.generation,
                original.purpose,
                candidate,
                reserved_keys);
            const internal::EphemerisBlockKey next_key(
                original.source_id,
                next_block_id,
                original.generation,
                original.purpose);
            append_runtime_source_key_assignment(
                &assignments,
                original,
                descriptor.format,
                descriptor.path,
                next_key);
            reserved_keys.insert(next_key);
            descriptor.source_key = next_key;
        }
        for (size_t g = 0; g < tkc1_groups.size(); ++g) {
            if (!assign_tkc1_file_group_source_keys(
                    tkc1_groups[g], descriptors, catalog, &reserved_keys)) {
                return false;
            }
        }
    } catch (...) {
        return false;
    }
    return true;
}

bool add_descriptors_to_catalog(
    const std::vector<internal::EphemerisBlockDescriptor>& input_descriptors,
    internal::EphemerisBlockCatalog* catalog
) noexcept {
    if (!catalog || input_descriptors.empty()) {
        return false;
    }

    try {
        std::vector<internal::EphemerisBlockDescriptor> descriptors = input_descriptors;
        if (!assign_runtime_catalog_source_keys(&descriptors, catalog)) {
            return false;
        }
        for (size_t i = 0; i < descriptors.size(); ++i) {
            if (!catalog->add(descriptors[i])) {
                return false;
            }
            add_source_index_if_missing(descriptors[i], catalog);
        }
    } catch (...) {
        return false;
    }
    return true;
}

bool add_descriptors_from_source_path_string(
    const std::string& source_path,
    bool strict_discovery,
    internal::EphemerisBlockCatalog* catalog
) noexcept {
    std::vector<internal::EphemerisBlockDescriptor> descriptors;
    if (!collect_descriptors_from_source_path_string(source_path, strict_discovery, &descriptors)) {
        return false;
    }
    return add_descriptors_to_catalog(descriptors, catalog);
}

bool add_descriptors_from_source_path(
    const char* path,
    bool strict_discovery,
    internal::EphemerisBlockCatalog* catalog
) noexcept {
    if (!path || path[0] == '\0') {
        return false;
    }
    return add_descriptors_from_source_path_string(
        internal::trim_trailing_separators(path),
        strict_discovery,
        catalog);
}

bool add_packaged_source_roots(
    const std::vector<std::string>& roots,
    internal::EphemerisBlockCatalog* catalog
) noexcept {
    if (!catalog) {
        return false;
    }

    RouteCoverageMap route_coverage;
    try {
        if (!seed_route_coverage_from_catalog(*catalog, &route_coverage)) {
            return false;
        }
    } catch (...) {
        return false;
    }

    for (size_t i = 0; i < roots.size(); ++i) {
        if (!internal::directory_exists(roots[i])) {
            continue;
        }
        std::vector<internal::EphemerisBlockDescriptor> descriptors;
        if (!collect_descriptors_from_source_path_string(roots[i], false, &descriptors)) {
            continue;
        }
        if (!assign_runtime_catalog_source_keys(&descriptors, catalog)) {
            return false;
        }
        try {
            for (size_t descriptor_index = 0; descriptor_index < descriptors.size(); ++descriptor_index) {
                if (!route_coverage_has_overlap(route_coverage, descriptors[descriptor_index])) {
                    if (!catalog->add(descriptors[descriptor_index])) {
                        return false;
                    }
                    route_coverage_append(&route_coverage, descriptors[descriptor_index]);
                    add_source_index_if_missing(descriptors[descriptor_index], catalog);
                }
            }
        } catch (...) {
            return false;
        }
    }
    return true;
}

internal::EphemerisBlockDescriptor make_builtin_semi_analytic_descriptor(
    int target_id,
    int center_id,
    double jd_tdb_start,
    double jd_tdb_end
) noexcept {
    const uint64_t block_id = static_cast<uint64_t>(target_id) * 10000u
        + static_cast<uint64_t>(center_id);
    internal::EphemerisBlockDescriptor descriptor;
    descriptor.route_key = internal::EphemerisRouteKey(
        target_id,
        center_id,
        internal::SEMI_ANALYTIC_METHOD_ID,
        static_cast<int64_t>(block_id));
    descriptor.source_key = internal::EphemerisBlockKey(
        internal::SEMI_ANALYTIC_SOURCE_ID,
        block_id,
        internal::SEMI_ANALYTIC_SOURCE_GENERATION,
        internal::SEMI_ANALYTIC_SOURCE_PURPOSE);
    descriptor.target_id = target_id;
    descriptor.center_id = center_id;
    descriptor.method_id = internal::SEMI_ANALYTIC_METHOD_ID;
    descriptor.frame = internal::IcrfJ2000Equatorial;
    descriptor.format = internal::EphemerisBlockFormat::SemiAnalytic;
    descriptor.jd_tdb_start = jd_tdb_start;
    // Semi-analytic model metadata uses an inclusive final epoch, while the
    // catalog consistently stores half-open intervals.  Advancing by one
    // representable double keeps that exact endpoint routable without opening
    // an arbitrary interval beyond the validated model.
    descriptor.jd_tdb_end = std::nextafter(
        jd_tdb_end, std::numeric_limits<double>::infinity());
    descriptor.cache_policy.kind = internal::CacheWholeEntry;
    return descriptor;
}

bool resolve_request_route_rules(
    const Runtime& runtime,
    const EphemerisRequest& request,
    EphemerisRequest* out
) noexcept {
    if (!out) {
        return false;
    }
    *out = request;
    if (out->route_rules) {
        return true;
    }
    out->route_rules = runtime.ephemeris_route_rule(out->route_rule_id);
    return out->route_rules != 0;
}

bool add_builtin_semi_analytic_descriptors(
    internal::EphemerisBlockCatalog* catalog
) noexcept {
    if (!catalog) {
        return false;
    }
    const int planet_targets[] = {
        TAIYIN_BODY_MERCURY_BARYCENTER,
        TAIYIN_BODY_VENUS_BARYCENTER,
        TAIYIN_BODY_EMB,
        TAIYIN_BODY_MARS_BARYCENTER,
        TAIYIN_BODY_JUPITER_BARYCENTER,
        TAIYIN_BODY_SATURN_BARYCENTER,
        TAIYIN_BODY_URANUS_BARYCENTER,
        TAIYIN_BODY_NEPTUNE_BARYCENTER,
        TAIYIN_BODY_PLUTO_BARYCENTER,
    };
    try {
        for (size_t i = 0; i < sizeof(planet_targets) / sizeof(planet_targets[0]); ++i) {
            double start = 0.0;
            double end = 0.0;
            if (!internal::get_builtin_semi_analytic_coverage(
                    planet_targets[i], TAIYIN_BODY_SUN, &start, &end)
                || !catalog->add(make_builtin_semi_analytic_descriptor(
                    planet_targets[i], TAIYIN_BODY_SUN, start, end))) {
                return false;
            }
        }

        double sun_start = 0.0;
        double sun_end = 0.0;
        if (!internal::get_builtin_semi_analytic_coverage(
                TAIYIN_BODY_SUN, TAIYIN_BODY_SSB, &sun_start, &sun_end)
            || !catalog->add(make_builtin_semi_analytic_descriptor(
                TAIYIN_BODY_SUN, TAIYIN_BODY_SSB, sun_start, sun_end))) {
            return false;
        }

        double moon_start = 0.0;
        double moon_end = 0.0;
        if (!internal::get_builtin_semi_analytic_coverage(
                TAIYIN_BODY_MOON,
                TAIYIN_BODY_EARTH,
                &moon_start,
                &moon_end)
            || !catalog->add(make_builtin_semi_analytic_descriptor(
                TAIYIN_BODY_MOON,
                TAIYIN_BODY_EARTH,
                moon_start,
                moon_end))) {
            return false;
        }

        double earth_start = 0.0;
        double earth_end = 0.0;
        if (!internal::get_builtin_semi_analytic_coverage(
                TAIYIN_BODY_EARTH,
                TAIYIN_BODY_SUN,
                &earth_start,
                &earth_end)
            || !catalog->add(make_builtin_semi_analytic_descriptor(
                TAIYIN_BODY_EARTH,
                TAIYIN_BODY_SUN,
                earth_start,
                earth_end))) {
            return false;
        }

        const int satellite_system_targets[][2] = {
            {TAIYIN_BODY_PHOBOS, TAIYIN_BODY_MARS},
            {TAIYIN_BODY_DEIMOS, TAIYIN_BODY_MARS},
            {TAIYIN_BODY_MARS, TAIYIN_BODY_MARS_BARYCENTER},
            {TAIYIN_BODY_IO, TAIYIN_BODY_JUPITER},
            {TAIYIN_BODY_EUROPA, TAIYIN_BODY_JUPITER},
            {TAIYIN_BODY_GANYMEDE, TAIYIN_BODY_JUPITER},
            {TAIYIN_BODY_CALLISTO, TAIYIN_BODY_JUPITER},
            {TAIYIN_BODY_JUPITER, TAIYIN_BODY_JUPITER_BARYCENTER},
            {TAIYIN_BODY_CHARON, TAIYIN_BODY_PLUTO},
            {TAIYIN_BODY_NIX, TAIYIN_BODY_PLUTO},
            {TAIYIN_BODY_HYDRA, TAIYIN_BODY_PLUTO},
            {TAIYIN_BODY_KERBEROS, TAIYIN_BODY_PLUTO},
            {TAIYIN_BODY_STYX, TAIYIN_BODY_PLUTO},
            {TAIYIN_BODY_PLUTO, TAIYIN_BODY_PLUTO_BARYCENTER},
            {TAIYIN_BODY_TRITON, TAIYIN_BODY_NEPTUNE},
            {TAIYIN_BODY_NEPTUNE, TAIYIN_BODY_NEPTUNE_BARYCENTER},
        };
        for (size_t index = 0;
             index < sizeof(satellite_system_targets)
                 / sizeof(satellite_system_targets[0]);
             ++index) {
            double start = 0.0;
            double end = 0.0;
            const int target_id = satellite_system_targets[index][0];
            const int center_id = satellite_system_targets[index][1];
            if (!internal::get_builtin_semi_analytic_coverage(
                    target_id, center_id, &start, &end)
                || !catalog->add(make_builtin_semi_analytic_descriptor(
                    target_id, center_id, start, end))) {
                return false;
            }
        }
    } catch (...) {
        return false;
    }
    return true;
}

size_t normalized_segment_cache_entries(size_t value) noexcept {
    return value == 0 ? DEFAULT_EPHEMERIS_SEGMENT_CACHE_ENTRIES : value;
}

void destroy_owned_eop_table(internal::EarthOrientationTable* table) noexcept {
    if (!table) return;
    internal::destroy_earth_orientation_table(table);
    delete table;
}

void destroy_owned_lunar_limb_model(Tll1LunarLimbModel* model) noexcept {
    if (!model) return;
    tll1_lunar_limb_destroy(model);
    delete model;
}

internal::EarthOrientationTable* copy_eop_table(
    const internal::EarthOrientationTable* source
) noexcept {
    if (!source) return 0;
    if (!source->samples || source->count == 0) return 0;
    if (source->count > std::numeric_limits<size_t>::max()
            / sizeof(internal::EarthOrientationSample)) {
        return 0;
    }
    internal::EarthOrientationTable* copy =
        new (std::nothrow) internal::EarthOrientationTable();
    if (!copy) return 0;
    internal::EarthOrientationSample* samples =
        static_cast<internal::EarthOrientationSample*>(::operator new(
            source->count * sizeof(internal::EarthOrientationSample), std::nothrow));
    if (!samples) {
        delete copy;
        return 0;
    }
    for (size_t i = 0; i < source->count; ++i) {
        samples[i] = source->samples[i];
    }
    copy->samples = samples;
    copy->count = source->count;
    return copy;
}

Status load_runtime_eop_table(
    const EphemerisRuntimeConfig& config,
    internal::EarthOrientationTable** out
) noexcept {
    if (!out) return TAIYIN_ERROR_INVALID_ARGUMENT;
    *out = 0;
    if (config.eop_path && config.eop_path[0] != '\0') {
        internal::EarthOrientationTable* table =
            new (std::nothrow) internal::EarthOrientationTable();
        if (!table) return TAIYIN_ERROR_OUT_OF_MEMORY;
        table->samples = 0;
        table->count = 0;
        const Status status =
            internal::load_finals2000a_file_status(config.eop_path, table);
        if (status != TAIYIN_STATUS_OK) {
            delete table;
            return status;
        }
        *out = table;
        return TAIYIN_STATUS_OK;
    }
    if (!config.load_builtin_eop) return TAIYIN_STATUS_OK;
    internal::EarthOrientationTable* table =
        new (std::nothrow) internal::EarthOrientationTable();
    if (!table) return TAIYIN_ERROR_OUT_OF_MEMORY;
    table->samples = 0;
    table->count = 0;
    const Status status = internal::load_builtin_eop_table_status(table);
    if (status != TAIYIN_STATUS_OK) {
        delete table;
        return status;
    }
    *out = table;
    return TAIYIN_STATUS_OK;
}

Tll1LunarLimbModel* load_runtime_lunar_limb_model(
    const EphemerisRuntimeConfig& config
) noexcept {
    if (!config.lunar_limb_path || config.lunar_limb_path[0] == '\0') return 0;
    Tll1LunarLimbModel* model = new (std::nothrow) Tll1LunarLimbModel();
    if (!model) return 0;
    if (tll1_lunar_limb_load_from_file(model, config.lunar_limb_path)
        != TAIYIN_STATUS_OK) {
        delete model;
        return 0;
    }
    return model;
}

RuntimeDataSourceFormat runtime_data_source_format(
    internal::EphemerisBlockFormat format
) noexcept {
    switch (format) {
    case internal::EphemerisBlockFormat::Opm2:
        return TAIYIN_RUNTIME_DATA_FORMAT_OPM2;
    case internal::EphemerisBlockFormat::Spk:
        return TAIYIN_RUNTIME_DATA_FORMAT_SPK;
    case internal::EphemerisBlockFormat::Kepler:
        return TAIYIN_RUNTIME_DATA_FORMAT_KEPLER;
    case internal::EphemerisBlockFormat::SemiAnalytic:
        return TAIYIN_RUNTIME_DATA_FORMAT_SEMI_ANALYTIC;
    case internal::EphemerisBlockFormat::FixedStar:
        return TAIYIN_RUNTIME_DATA_FORMAT_FIXED_STAR;
    case internal::EphemerisBlockFormat::Tsc1:
        return TAIYIN_RUNTIME_DATA_FORMAT_TSC1;
    case internal::EphemerisBlockFormat::Tkc1:
        return TAIYIN_RUNTIME_DATA_FORMAT_TKC1;
    case internal::EphemerisBlockFormat::Custom:
        return TAIYIN_RUNTIME_DATA_FORMAT_CUSTOM;
    case internal::EphemerisBlockFormat::FormatUnknown:
    default:
        return TAIYIN_RUNTIME_DATA_FORMAT_UNKNOWN;
    }
}

}  // namespace

RegisteredDataSource::RegisteredDataSource() noexcept
    : kind(TAIYIN_RUNTIME_DATA_SOURCE_EPHEMERIS),
      format(TAIYIN_RUNTIME_DATA_FORMAT_UNKNOWN),
      flags(0u),
      source(),
      item_count(0u),
      jd_start(0.0),
      jd_end(0.0) {}

EphemerisRuntimeConfig::EphemerisRuntimeConfig() noexcept
    : segment_cache_max_entries(DEFAULT_EPHEMERIS_SEGMENT_CACHE_ENTRIES),
      source_paths(0),
      source_path_count(0),
      data_root(0),
      eop_path(0),
      lunar_limb_path(0),
      load_packaged_data(true),
      load_builtin_eop(false),
      strict_discovery(false) {}

Runtime::Runtime() noexcept
    : ephemeris_catalog_(),
      ephemeris_segment_cache_(
          new (std::nothrow) internal::EphemerisSegmentCache(DEFAULT_EPHEMERIS_SEGMENT_CACHE_ENTRIES)),
      ephemeris_body_registry_(),
      ephemeris_route_rules_(),
      ephemeris_source_priorities_(),
      ephemeris_engine_(),
      earth_orientation_table_(0),
      earth_orientation_source_(),
      earth_orientation_format_(TAIYIN_RUNTIME_DATA_FORMAT_UNKNOWN),
      earth_orientation_source_flags_(0u),
      retired_earth_orientation_tables_(),
      lunar_limb_model_(0),
      lunar_limb_source_(),
      retired_lunar_limb_models_() {
    reset_default_route_rules();
    register_builtin_body_rules(ephemeris_body_registry_);
    reset_ephemeris_bindings();
}

Runtime::~Runtime() noexcept {
    destroy_owned_eop_table(earth_orientation_table_);
    earth_orientation_table_ = 0;
    for (size_t i = 0; i < retired_earth_orientation_tables_.size(); ++i) {
        destroy_owned_eop_table(retired_earth_orientation_tables_[i]);
    }
    retired_earth_orientation_tables_.clear();
    destroy_owned_lunar_limb_model(lunar_limb_model_);
    lunar_limb_model_ = 0;
    for (size_t i = 0; i < retired_lunar_limb_models_.size(); ++i) {
        destroy_owned_lunar_limb_model(retired_lunar_limb_models_[i]);
    }
    retired_lunar_limb_models_.clear();
    delete ephemeris_segment_cache_;
    ephemeris_segment_cache_ = 0;
}

EphemerisEngine& Runtime::ephemeris_engine() noexcept {
    return ephemeris_engine_;
}

const EphemerisEngine& Runtime::ephemeris_engine() const noexcept {
    return ephemeris_engine_;
}

internal::EphemerisBlockCatalog& Runtime::ephemeris_catalog() noexcept {
    return ephemeris_catalog_;
}

const internal::EphemerisBlockCatalog& Runtime::ephemeris_catalog() const noexcept {
    return ephemeris_catalog_;
}

internal::EphemerisSegmentCache* Runtime::ephemeris_segment_cache() noexcept {
    return ephemeris_segment_cache_;
}

const internal::EphemerisSegmentCache* Runtime::ephemeris_segment_cache() const noexcept {
    return ephemeris_segment_cache_;
}

EphemerisBodyRegistry& Runtime::ephemeris_body_registry() noexcept {
    return ephemeris_body_registry_;
}

const EphemerisBodyRegistry& Runtime::ephemeris_body_registry() const noexcept {
    return ephemeris_body_registry_;
}

internal::EphemerisRouteRuleTable* Runtime::ephemeris_route_rule(uint64_t route_rule_id) noexcept {
    std::unordered_map<uint64_t, internal::EphemerisRouteRuleTable>::iterator it =
        ephemeris_route_rules_.find(route_rule_id);
    return it == ephemeris_route_rules_.end() ? 0 : &it->second;
}

const internal::EphemerisRouteRuleTable* Runtime::ephemeris_route_rule(uint64_t route_rule_id) const noexcept {
    std::unordered_map<uint64_t, internal::EphemerisRouteRuleTable>::const_iterator it =
        ephemeris_route_rules_.find(route_rule_id);
    return it == ephemeris_route_rules_.end() ? 0 : &it->second;
}

bool Runtime::register_ephemeris_route_rule(
    uint64_t route_rule_id,
    const internal::EphemerisRouteRuleTable& table
) noexcept {
    try {
        ephemeris_route_rules_[route_rule_id] = table;
    } catch (...) {
        return false;
    }
    return true;
}

bool Runtime::set_ephemeris_source_priority(
    const char* path_or_basename,
    int priority
) noexcept {
    return ephemeris_source_priorities_.set_path_priority(path_or_basename, priority);
}

bool Runtime::clear_ephemeris_source_priority(
    const char* path_or_basename
) noexcept {
    return ephemeris_source_priorities_.clear_path_priority(path_or_basename);
}

void Runtime::clear_all_ephemeris_source_priorities() noexcept {
    ephemeris_source_priorities_.clear();
}

const internal::EarthOrientationTable* Runtime::earth_orientation_table() const noexcept {
    return earth_orientation_table_;
}

const Tll1LunarLimbModel* Runtime::lunar_limb_model() const noexcept {
    return lunar_limb_model_;
}

bool Runtime::get_registered_data_sources(
    std::vector<RegisteredDataSource>* out
) const noexcept {
    if (!out) {
        return false;
    }
    try {
        out->clear();
        std::unordered_map<std::string, size_t> source_indexes;
        const size_t descriptor_count = ephemeris_catalog_.size();
        for (size_t i = 0; i < descriptor_count; ++i) {
            internal::EphemerisBlockDescriptor descriptor;
            if (!ephemeris_catalog_.get(i, &descriptor)) {
                out->clear();
                return false;
            }

            std::string source = descriptor.path;
            uint32_t flags = 0u;
            if (source.empty()) {
                if (descriptor.format
                    == internal::EphemerisBlockFormat::SemiAnalytic) {
                    source = "builtin:semi-analytic";
                    flags |= TAIYIN_RUNTIME_DATA_SOURCE_BUILTIN;
                } else {
                    source = "memory:ephemeris:";
                    source += std::to_string(descriptor.source_key.source_id);
                    flags |= TAIYIN_RUNTIME_DATA_SOURCE_MEMORY;
                }
            }

            const RuntimeDataSourceFormat format =
                runtime_data_source_format(descriptor.format);
            const std::string key = std::to_string(static_cast<int>(format))
                + "\n" + source;
            std::unordered_map<std::string, size_t>::iterator existing =
                source_indexes.find(key);
            if (existing == source_indexes.end()) {
                RegisteredDataSource item;
                item.kind = TAIYIN_RUNTIME_DATA_SOURCE_EPHEMERIS;
                item.format = format;
                item.flags = flags;
                item.source = source;
                item.item_count = 1u;
                if (std::isfinite(descriptor.jd_tdb_start)
                    && std::isfinite(descriptor.jd_tdb_end)
                    && descriptor.jd_tdb_end > descriptor.jd_tdb_start) {
                    item.flags |= TAIYIN_RUNTIME_DATA_SOURCE_HAS_COVERAGE;
                    item.jd_start = descriptor.jd_tdb_start;
                    item.jd_end = descriptor.jd_tdb_end;
                }
                source_indexes[key] = out->size();
                out->push_back(item);
                continue;
            }

            RegisteredDataSource& item = (*out)[existing->second];
            ++item.item_count;
            if (std::isfinite(descriptor.jd_tdb_start)
                && std::isfinite(descriptor.jd_tdb_end)
                && descriptor.jd_tdb_end > descriptor.jd_tdb_start) {
                if ((item.flags
                        & TAIYIN_RUNTIME_DATA_SOURCE_HAS_COVERAGE) == 0u) {
                    item.flags |= TAIYIN_RUNTIME_DATA_SOURCE_HAS_COVERAGE;
                    item.jd_start = descriptor.jd_tdb_start;
                    item.jd_end = descriptor.jd_tdb_end;
                } else {
                    item.jd_start = std::min(
                        item.jd_start, descriptor.jd_tdb_start);
                    item.jd_end = std::max(
                        item.jd_end, descriptor.jd_tdb_end);
                }
            }
        }

        if (earth_orientation_table_) {
            RegisteredDataSource item;
            item.kind = TAIYIN_RUNTIME_DATA_SOURCE_EARTH_ORIENTATION;
            item.format = earth_orientation_format_;
            item.flags = earth_orientation_source_flags_;
            item.source = earth_orientation_source_;
            item.item_count = earth_orientation_table_->count;
            if (earth_orientation_table_->samples
                && earth_orientation_table_->count > 0u) {
                const double start = earth_orientation_table_->samples[0].jd_utc;
                const double end = earth_orientation_table_->samples[
                    earth_orientation_table_->count - 1u].jd_utc;
                if (std::isfinite(start) && std::isfinite(end) && end >= start) {
                    item.flags |= TAIYIN_RUNTIME_DATA_SOURCE_HAS_COVERAGE;
                    item.jd_start = start;
                    item.jd_end = end;
                }
            }
            out->push_back(item);
        }

        if (lunar_limb_model_) {
            RegisteredDataSource item;
            item.kind = TAIYIN_RUNTIME_DATA_SOURCE_LUNAR_LIMB;
            item.format = TAIYIN_RUNTIME_DATA_FORMAT_TLL1;
            item.source = lunar_limb_source_;
            if (lunar_limb_model_->header) {
                item.item_count = static_cast<size_t>(
                    lunar_limb_model_->header->longitude_count)
                    * static_cast<size_t>(
                        lunar_limb_model_->header->latitude_count)
                    * static_cast<size_t>(
                        lunar_limb_model_->header->position_angle_count);
            }
            out->push_back(item);
        }
    } catch (...) {
        out->clear();
        return false;
    }
    return true;
}

bool Runtime::set_earth_orientation_table(
    const internal::EarthOrientationTable* table
) noexcept {
    std::string replacement_source;
    try {
        if (table) {
            replacement_source = "memory:eop";
        }
    } catch (...) {
        return false;
    }
    internal::EarthOrientationTable* replacement = table ? copy_eop_table(table) : 0;
    if (table && !replacement) return false;
    const Status status = replace_earth_orientation_table(replacement);
    if (status != TAIYIN_STATUS_OK) {
        destroy_owned_eop_table(replacement);
        return false;
    }
    earth_orientation_source_.swap(replacement_source);
    earth_orientation_format_ = table
        ? TAIYIN_RUNTIME_DATA_FORMAT_MEMORY
        : TAIYIN_RUNTIME_DATA_FORMAT_UNKNOWN;
    earth_orientation_source_flags_ = table
        ? TAIYIN_RUNTIME_DATA_SOURCE_MEMORY
        : 0u;
    return true;
}

Status Runtime::load_earth_orientation_table(const char* path) noexcept {
    if (!path || path[0] == '\0') {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    std::string replacement_source;
    try {
        replacement_source = internal::trim_trailing_separators(path);
    } catch (...) {
        return TAIYIN_ERROR_OUT_OF_MEMORY;
    }
    EphemerisRuntimeConfig config;
    config.eop_path = path;
    internal::EarthOrientationTable* replacement = 0;
    const Status load_status = load_runtime_eop_table(config, &replacement);
    if (load_status != TAIYIN_STATUS_OK) return load_status;
    const Status replace_status = replace_earth_orientation_table(replacement);
    if (replace_status != TAIYIN_STATUS_OK) {
        destroy_owned_eop_table(replacement);
        return replace_status;
    }
    earth_orientation_source_.swap(replacement_source);
    earth_orientation_format_ = TAIYIN_RUNTIME_DATA_FORMAT_FINALS2000A;
    earth_orientation_source_flags_ = 0u;
    return TAIYIN_STATUS_OK;
}

Status Runtime::load_builtin_earth_orientation_table() noexcept {
    std::string replacement_source;
    try {
        replacement_source = "builtin:eop";
    } catch (...) {
        return TAIYIN_ERROR_OUT_OF_MEMORY;
    }
    EphemerisRuntimeConfig config;
    config.load_builtin_eop = true;
    internal::EarthOrientationTable* replacement = 0;
    const Status load_status = load_runtime_eop_table(config, &replacement);
    if (load_status != TAIYIN_STATUS_OK) return load_status;
    const Status replace_status = replace_earth_orientation_table(replacement);
    if (replace_status != TAIYIN_STATUS_OK) {
        destroy_owned_eop_table(replacement);
        return replace_status;
    }
    earth_orientation_source_.swap(replacement_source);
    earth_orientation_format_ = TAIYIN_RUNTIME_DATA_FORMAT_BUILTIN_EOP;
    earth_orientation_source_flags_ = TAIYIN_RUNTIME_DATA_SOURCE_BUILTIN;
    return TAIYIN_STATUS_OK;
}

Status Runtime::replace_earth_orientation_table(
    internal::EarthOrientationTable* replacement
) noexcept {
    if (earth_orientation_table_) {
        try {
            retired_earth_orientation_tables_.push_back(
                earth_orientation_table_);
        } catch (const std::bad_alloc&) {
            return TAIYIN_ERROR_OUT_OF_MEMORY;
        } catch (...) {
            return TAIYIN_ERROR_INTERNAL;
        }
    }
    earth_orientation_table_ = replacement;
    return TAIYIN_STATUS_OK;
}

Status Runtime::load_lunar_limb_model(const char* path) noexcept {
    std::string replacement_source;
    try {
        if (path && path[0] != '\0') {
            replacement_source = internal::trim_trailing_separators(path);
        }
    } catch (...) {
        return TAIYIN_ERROR_OUT_OF_MEMORY;
    }
    Tll1LunarLimbModel* replacement = 0;
    if (path && path[0] != '\0') {
        replacement = new (std::nothrow) Tll1LunarLimbModel();
        if (!replacement) return TAIYIN_ERROR_OUT_OF_MEMORY;
        const Status status = tll1_lunar_limb_load_from_file(replacement, path);
        if (status != TAIYIN_STATUS_OK) {
            delete replacement;
            return status;
        }
    }
    // Retain the replaced model like retired EOP snapshots: readers that fetched
    // it under the read lock may still be using it after the lock is released.
    if (lunar_limb_model_) {
        try {
            retired_lunar_limb_models_.push_back(lunar_limb_model_);
        } catch (const std::bad_alloc&) {
            delete replacement;
            return TAIYIN_ERROR_OUT_OF_MEMORY;
        } catch (...) {
            delete replacement;
            return TAIYIN_ERROR_INTERNAL;
        }
    }
    lunar_limb_model_ = replacement;
    lunar_limb_source_.swap(replacement_source);
    return TAIYIN_STATUS_OK;
}

bool Runtime::initialize_ephemeris(const EphemerisRuntimeConfig& config) noexcept {
    internal::EarthOrientationTable* next_eop = 0;
    const Status eop_status = load_runtime_eop_table(config, &next_eop);
    if (eop_status != TAIYIN_STATUS_OK) {
        return false;
    }
    Tll1LunarLimbModel* next_lunar_limb = load_runtime_lunar_limb_model(config);
    if (config.lunar_limb_path && config.lunar_limb_path[0] != '\0'
        && !next_lunar_limb) {
        destroy_owned_eop_table(next_eop);
        return false;
    }
    std::string next_eop_source;
    RuntimeDataSourceFormat next_eop_format =
        TAIYIN_RUNTIME_DATA_FORMAT_UNKNOWN;
    uint32_t next_eop_flags = 0u;
    std::string next_lunar_limb_source;
    try {
        if (next_eop) {
            if (config.eop_path && config.eop_path[0] != '\0') {
                next_eop_source = internal::trim_trailing_separators(
                    config.eop_path);
                next_eop_format = TAIYIN_RUNTIME_DATA_FORMAT_FINALS2000A;
            } else {
                next_eop_source = "builtin:eop";
                next_eop_format = TAIYIN_RUNTIME_DATA_FORMAT_BUILTIN_EOP;
                next_eop_flags = TAIYIN_RUNTIME_DATA_SOURCE_BUILTIN;
            }
        }
        if (next_lunar_limb) {
            next_lunar_limb_source = internal::trim_trailing_separators(
                config.lunar_limb_path);
        }
    } catch (...) {
        destroy_owned_eop_table(next_eop);
        destroy_owned_lunar_limb_model(next_lunar_limb);
        return false;
    }
    internal::EphemerisBlockCatalog next_catalog;

    internal::EphemerisSegmentCache* next_segment_cache =
        new (std::nothrow) internal::EphemerisSegmentCache(
            normalized_segment_cache_entries(config.segment_cache_max_entries));
    if (!next_segment_cache) {
        destroy_owned_eop_table(next_eop);
        destroy_owned_lunar_limb_model(next_lunar_limb);
        return false;
    }
    bool ok = true;
    if (config.source_path_count > 0 && !config.source_paths) {
        ok = false;
    }

    std::vector<std::string> explicit_source_paths;
    for (size_t i = 0; ok && i < config.source_path_count; ++i) {
        if (config.source_paths[i]) {
            append_unique_path(&explicit_source_paths, config.source_paths[i]);
        }
        if (!add_descriptors_from_source_path(
                config.source_paths[i],
                config.strict_discovery,
                &next_catalog)) {
            ok = false;
        }
    }

    if (ok) {
        std::vector<std::string> packaged_roots;
        collect_packaged_source_roots(config, &packaged_roots);
        for (size_t i = 0; i < explicit_source_paths.size(); ++i) {
            for (size_t root_index = 0; root_index < packaged_roots.size();) {
                if (same_path_string(explicit_source_paths[i], packaged_roots[root_index])) {
                    packaged_roots.erase(packaged_roots.begin() + static_cast<std::ptrdiff_t>(root_index));
                } else {
                    ++root_index;
                }
            }
        }
        add_packaged_source_roots(packaged_roots, &next_catalog);
        if (config.load_packaged_data
            && !add_builtin_semi_analytic_descriptors(&next_catalog)) {
            ok = false;
        }
    }

    if (!ok) {
        delete next_segment_cache;
        destroy_owned_eop_table(next_eop);
        destroy_owned_lunar_limb_model(next_lunar_limb);
        return false;
    }

    EphemerisBodyRegistry next_body_registry;
    if (!rebuild_ephemeris_body_registry(next_catalog, &next_body_registry)) {
        delete next_segment_cache;
        destroy_owned_eop_table(next_eop);
        destroy_owned_lunar_limb_model(next_lunar_limb);
        return false;
    }

    if (earth_orientation_table_) {
        try {
            retired_earth_orientation_tables_.reserve(
                retired_earth_orientation_tables_.size() + 1);
        } catch (...) {
            delete next_segment_cache;
            destroy_owned_eop_table(next_eop);
            destroy_owned_lunar_limb_model(next_lunar_limb);
            return false;
        }
    }

    if (lunar_limb_model_) {
        try {
            retired_lunar_limb_models_.reserve(
                retired_lunar_limb_models_.size() + 1);
        } catch (...) {
            delete next_segment_cache;
            destroy_owned_eop_table(next_eop);
            destroy_owned_lunar_limb_model(next_lunar_limb);
            return false;
        }
    }

    try {
        ephemeris_catalog_ = next_catalog;
    } catch (...) {
        delete next_segment_cache;
        destroy_owned_eop_table(next_eop);
        destroy_owned_lunar_limb_model(next_lunar_limb);
        return false;
    }

    const Status replace_eop_status =
        replace_earth_orientation_table(next_eop);
    if (replace_eop_status != TAIYIN_STATUS_OK) {
        delete next_segment_cache;
        destroy_owned_eop_table(next_eop);
        destroy_owned_lunar_limb_model(next_lunar_limb);
        return false;
    }
    delete ephemeris_segment_cache_;
    ephemeris_segment_cache_ = next_segment_cache;
    if (lunar_limb_model_) {
        // Retained, not destroyed: readers that fetched it under the read lock
        // may still be dereferencing it. Capacity was reserved above.
        retired_lunar_limb_models_.push_back(lunar_limb_model_);
    }
    lunar_limb_model_ = next_lunar_limb;
    earth_orientation_source_.swap(next_eop_source);
    earth_orientation_format_ = next_eop_format;
    earth_orientation_source_flags_ = next_eop_flags;
    lunar_limb_source_.swap(next_lunar_limb_source);
    ephemeris_body_registry_.swap(next_body_registry);
    reset_ephemeris_bindings();
    return true;
}

bool Runtime::add_ephemeris_source_path(const char* path, bool strict_discovery) noexcept {
    try {
        internal::EphemerisBlockCatalog next_catalog = ephemeris_catalog_;
        if (!add_descriptors_from_source_path(path, strict_discovery, &next_catalog)) {
            return false;
        }
        EphemerisBodyRegistry next_registry = ephemeris_body_registry_;
        if (!rebuild_ephemeris_body_registry(next_catalog, &next_registry)) {
            return false;
        }
        // Publish atomically via no-throw swaps so a failed assignment cannot
        // leave a partially-updated live catalog behind.
        ephemeris_catalog_.swap(next_catalog);
        ephemeris_body_registry_.swap(next_registry);
        reset_ephemeris_bindings();
        return true;
    } catch (...) {
        return false;
    }
}

Status Runtime::eval_ephemeris_state(
    const EphemerisRequest& request,
    EphemerisResult* out,
    EphemerisEvalDiagnostic* diagnostic
) noexcept {
    EphemerisRequest resolved_request;
    if (!resolve_request_route_rules(*this, request, &resolved_request)) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    return ephemeris_engine_.eval_state(resolved_request, out, diagnostic);
}

void Runtime::reset_ephemeris_bindings() noexcept {
    ephemeris_engine_.set_catalog(&ephemeris_catalog_);
    ephemeris_engine_.set_segment_cache(ephemeris_segment_cache_);
    ephemeris_engine_.set_body_registry(&ephemeris_body_registry_);
    ephemeris_engine_.set_default_route_rules(ephemeris_route_rule(TAIYIN_EPHEMERIS_ROUTE_AUTO));
    ephemeris_engine_.set_source_priorities(&ephemeris_source_priorities_);
}

bool Runtime::rebuild_ephemeris_body_registry(
    const internal::EphemerisBlockCatalog& catalog,
    EphemerisBodyRegistry* out_registry
) noexcept {
    if (!out_registry) {
        return false;
    }
    out_registry->clear();
    for (size_t i = 0; i < catalog.size(); ++i) {
        internal::EphemerisBlockDescriptor descriptor;
        if (catalog.get(i, &descriptor) && descriptor.target_id != 0) {
            if (!out_registry->mark_direct(descriptor.target_id)) {
                return false;
            }
        }
    }
    return register_builtin_body_rules(*out_registry);
}

bool Runtime::reset_default_route_rules() noexcept {
    ephemeris_route_rules_.clear();
    try {
        internal::EphemerisRouteRuleTable automatic;
        struct NamedSpkAutoRule {
            uint64_t source_id;
            int priority;
        };
        static const NamedSpkAutoRule named_de_rules[] = {
            {internal::SPK_SOURCE_JPL_DE442, 440},
            {internal::SPK_SOURCE_JPL_DE441, 430},
            {internal::SPK_SOURCE_JPL_DE440, 420},
            {internal::SPK_SOURCE_JPL_DE438, 419},
            {internal::SPK_SOURCE_JPL_DE435, 418},
            {internal::SPK_SOURCE_JPL_DE432, 417},
            {internal::SPK_SOURCE_JPL_DE431, 416},
            {internal::SPK_SOURCE_JPL_DE430, 415},
            {internal::SPK_SOURCE_JPL_DE423, 414},
            {internal::SPK_SOURCE_JPL_DE421, 413},
            {internal::SPK_SOURCE_JPL_DE418, 412},
            {internal::SPK_SOURCE_JPL_DE414, 411},
            {internal::SPK_SOURCE_JPL_DE413, 410},
            {internal::SPK_SOURCE_JPL_DE410, 409},
            {internal::SPK_SOURCE_JPL_DE408, 408},
            {internal::SPK_SOURCE_JPL_DE406, 407},
            {internal::SPK_SOURCE_JPL_DE405, 406},
            {internal::SPK_SOURCE_JPL_DE403, 405},
            {internal::SPK_SOURCE_JPL_DE245, 404},
            {internal::SPK_SOURCE_JPL_DE202, 403},
            {internal::SPK_SOURCE_JPL_DE200, 402},
            {internal::SPK_SOURCE_JPL_DE130, 401},
            {internal::SPK_SOURCE_JPL_DE125, 400},
            {internal::SPK_SOURCE_JPL_DE118, 399},
            {internal::SPK_SOURCE_JPL_DE102, 398},
        };
        static const NamedSpkAutoRule named_satellite_rules[] = {
            {internal::SPK_SOURCE_JPL_MAR099, 397},
            {internal::SPK_SOURCE_JPL_JUP365, 397},
            {internal::SPK_SOURCE_JPL_JUP349, 396},
            {internal::SPK_SOURCE_JPL_JUP348, 395},
            {internal::SPK_SOURCE_JPL_JUP347, 394},
            {internal::SPK_SOURCE_JPL_SAT441, 397},
            {internal::SPK_SOURCE_JPL_SAT459, 396},
            {internal::SPK_SOURCE_JPL_SAT458, 395},
            {internal::SPK_SOURCE_JPL_SAT457, 394},
            {internal::SPK_SOURCE_JPL_SAT456, 393},
            {internal::SPK_SOURCE_JPL_SAT455, 392},
            {internal::SPK_SOURCE_JPL_SAT480, 391},
            {internal::SPK_SOURCE_JPL_SAT415, 390},
            {internal::SPK_SOURCE_JPL_URA182, 397},
            {internal::SPK_SOURCE_JPL_URA184, 396},
            {internal::SPK_SOURCE_JPL_URA117, 395},
            {internal::SPK_SOURCE_JPL_NEP098, 397},
            {internal::SPK_SOURCE_JPL_NEP105, 396},
            {internal::SPK_SOURCE_JPL_NEP104, 395},
            {internal::SPK_SOURCE_JPL_NEP097, 394},
            {internal::SPK_SOURCE_JPL_PLU060, 397},
        };
        bool rules_ok = automatic.upsert_source_method(
                internal::OPM2_SOURCE_TAIYIN_DE442_REBUILT,
                static_cast<int>(internal::OPM2_METHOD_ID),
                435,
                "Taiyin DE442 rebuilt OPM2 with built-in relative-body auxiliaries",
                false,
                true);
        for (size_t i = 0;
             rules_ok && i < sizeof(named_de_rules) / sizeof(named_de_rules[0]);
             ++i) {
            rules_ok = automatic.upsert_source_method(
                named_de_rules[i].source_id,
                internal::SPK_METHOD_ID,
                named_de_rules[i].priority,
                "recognized JPL DE SPK with satellite auxiliaries",
                true);
        }
        for (size_t i = 0;
             rules_ok
                 && i < sizeof(named_satellite_rules)
                     / sizeof(named_satellite_rules[0]);
             ++i) {
            rules_ok = automatic.upsert_source_method(
                named_satellite_rules[i].source_id,
                internal::SPK_METHOD_ID,
                named_satellite_rules[i].priority,
                "recognized JPL satellite SPK fallback");
        }
        if (!rules_ok
            || !automatic.upsert_source_method(
                internal::SPK_SOURCE_ID,
                internal::SPK_METHOD_ID,
                389,
                "external SPK")
            || !automatic.upsert_source_method(
                internal::OPM2_SOURCE_ID,
                static_cast<int>(internal::OPM2_METHOD_ID),
                300,
                "Taiyin prerelease OPM2")
            || !automatic.upsert_source_method(
                0,
                static_cast<int>(internal::OPM2_METHOD_ID),
                290,
                "other OPM2 product")
            || !automatic.upsert_source_method(
                internal::SEMI_ANALYTIC_SOURCE_ID,
                internal::SEMI_ANALYTIC_METHOD_ID,
                250,
                "builtin semi-analytical ephemeris")
            || !automatic.upsert_source_method(
                internal::TKC1_SOURCE_ID,
                internal::TKC1_KEPLER_METHOD_ID,
                200,
                "TKC1 Kepler catalog")
            || !automatic.upsert_source_method(
                internal::TAIYIN_KEPLER_FILE_SOURCE_ID,
                internal::TAIYIN_KEPLER_FILE_METHOD_ID,
                100,
                "Taiyin Kepler file")) {
            return false;
        }
        ephemeris_route_rules_[TAIYIN_EPHEMERIS_ROUTE_AUTO] = automatic;

        internal::EphemerisRouteRuleTable opm2;
        if (!opm2.upsert_source_method(
                0,
                static_cast<int>(internal::OPM2_METHOD_ID),
                100,
                "any OPM2 product")) {
            return false;
        }
        ephemeris_route_rules_[TAIYIN_EPHEMERIS_ROUTE_OPM2] = opm2;

        internal::EphemerisRouteRuleTable spk;
        if (!spk.upsert_source_method(
                0,
                internal::SPK_METHOD_ID,
                100,
                "any SPK product")) {
            return false;
        }
        ephemeris_route_rules_[TAIYIN_EPHEMERIS_ROUTE_SPK] = spk;

        internal::EphemerisRouteRuleTable semi_analytic;
        if (!semi_analytic.upsert_source_method(
                internal::SEMI_ANALYTIC_SOURCE_ID,
                internal::SEMI_ANALYTIC_METHOD_ID,
                100,
                "builtin semi-analytical ephemeris only")) {
            return false;
        }
        ephemeris_route_rules_[TAIYIN_EPHEMERIS_ROUTE_SEMI_ANALYTIC] = semi_analytic;
    } catch (...) {
        ephemeris_route_rules_.clear();
        return false;
    }
    return true;
}

Runtime& default_runtime() noexcept {
    static Runtime runtime;
    return runtime;
}

bool initialize_global_ephemeris_runtime(const EphemerisRuntimeConfig& config) noexcept {
    internal::WriteLockGuard lock(global_ephemeris_runtime_rwlock());
    return default_runtime().initialize_ephemeris(config);
}

bool add_global_ephemeris_source_path(const char* path) noexcept {
    internal::WriteLockGuard lock(global_ephemeris_runtime_rwlock());
    return default_runtime().add_ephemeris_source_path(path, false);
}

bool set_global_ephemeris_source_priority(
    const char* path_or_basename,
    int priority
) noexcept {
    internal::WriteLockGuard lock(global_ephemeris_runtime_rwlock());
    return default_runtime().set_ephemeris_source_priority(path_or_basename, priority);
}

bool clear_global_ephemeris_source_priority(
    const char* path_or_basename
) noexcept {
    internal::WriteLockGuard lock(global_ephemeris_runtime_rwlock());
    return default_runtime().clear_ephemeris_source_priority(path_or_basename);
}

void clear_all_global_ephemeris_source_priorities() noexcept {
    internal::WriteLockGuard lock(global_ephemeris_runtime_rwlock());
    default_runtime().clear_all_ephemeris_source_priorities();
}

Status eval_global_ephemeris_state(
    const EphemerisRequest& request,
    EphemerisResult* out,
    EphemerisEvalDiagnostic* diagnostic
) noexcept {
    internal::ReadLockGuard lock(global_ephemeris_runtime_rwlock());
    return default_runtime().eval_ephemeris_state(request, out, diagnostic);
}

bool add_global_ephemeris_descriptor(const internal::EphemerisBlockDescriptor& descriptor) noexcept {
    internal::WriteLockGuard lock(global_ephemeris_runtime_rwlock());
    if (!default_runtime().ephemeris_catalog().add(descriptor)) {
        return false;
    }
    if (!default_runtime().rebuild_ephemeris_body_registry(
            default_runtime().ephemeris_catalog(),
            &default_runtime().ephemeris_body_registry())) {
        return false;
    }
    return true;
}

bool add_global_custom_ephemeris_method(
    const internal::CustomEphemerisMethodDefinition& definition,
    int priority,
    const char* description,
    internal::EphemerisBlockDescriptor* out_descriptor
) noexcept {
    if (out_descriptor) {
        *out_descriptor = internal::EphemerisBlockDescriptor();
    }

    internal::WriteLockGuard lock(global_ephemeris_runtime_rwlock());
    internal::EphemerisBlockDescriptor descriptor;
    if (!internal::register_custom_ephemeris_method(definition, &descriptor)) {
        return false;
    }
    if (!default_runtime().ephemeris_catalog().add(descriptor)) {
        return false;
    }
    const char* method_description = description ? description : definition.description;
    internal::EphemerisRouteRuleTable* auto_rules =
        default_runtime().ephemeris_route_rule(TAIYIN_EPHEMERIS_ROUTE_AUTO);
    if (!auto_rules || !auto_rules->upsert_source_method(
            0,
            descriptor.method_id,
            priority,
            method_description)) {
        return false;
    }
    if (!default_runtime().rebuild_ephemeris_body_registry(
            default_runtime().ephemeris_catalog(),
            &default_runtime().ephemeris_body_registry())) {
        return false;
    }
    if (out_descriptor) {
        *out_descriptor = descriptor;
    }
    return true;
}

bool add_global_custom_ephemeris_file_method(
    const internal::CustomEphemerisFileMethodDefinition& definition,
    int priority,
    const char* description,
    internal::EphemerisBlockDescriptor* out_descriptor
) noexcept {
    if (out_descriptor) {
        *out_descriptor = internal::EphemerisBlockDescriptor();
    }

    internal::WriteLockGuard lock(global_ephemeris_runtime_rwlock());
    internal::EphemerisBlockDescriptor descriptor;
    if (!internal::register_custom_ephemeris_file_method(definition, &descriptor)) {
        return false;
    }
    if (!default_runtime().ephemeris_catalog().add(descriptor)) {
        return false;
    }
    const char* method_description = description ? description : definition.description;
    internal::EphemerisRouteRuleTable* auto_rules =
        default_runtime().ephemeris_route_rule(TAIYIN_EPHEMERIS_ROUTE_AUTO);
    if (!auto_rules || !auto_rules->upsert_source_method(
            0,
            descriptor.method_id,
            priority,
            method_description)) {
        return false;
    }
    if (!default_runtime().rebuild_ephemeris_body_registry(
            default_runtime().ephemeris_catalog(),
            &default_runtime().ephemeris_body_registry())) {
        return false;
    }
    if (out_descriptor) {
        *out_descriptor = descriptor;
    }
    return true;
}

bool register_global_ephemeris_route_rule(
    uint64_t route_rule_id,
    const internal::EphemerisRouteRuleTable& table
) noexcept {
    internal::WriteLockGuard lock(global_ephemeris_runtime_rwlock());
    return default_runtime().register_ephemeris_route_rule(route_rule_id, table);
}

const internal::EphemerisRouteRuleTable* global_ephemeris_route_rule(uint64_t route_rule_id) noexcept {
    internal::ReadLockGuard lock(global_ephemeris_runtime_rwlock());
    return default_runtime().ephemeris_route_rule(route_rule_id);
}

const internal::EarthOrientationTable* global_earth_orientation_table() noexcept {
    internal::ReadLockGuard lock(global_ephemeris_runtime_rwlock());
    return default_runtime().earth_orientation_table();
}

// The read lock protects the pointer read against a concurrent replacement.
// The returned model is also kept alive: load_global_lunar_limb_model() and
// initialize_ephemeris() retain replaced models in a retired list instead of
// destroying them, so a caller may safely dereference the pointer after this
// lock is released. Callers should not cache the pointer across their own
// long-lived operations, since a replacement is a valid new state.
const Tll1LunarLimbModel* global_lunar_limb_model() noexcept {
    internal::ReadLockGuard lock(global_ephemeris_runtime_rwlock());
    return default_runtime().lunar_limb_model();
}

bool set_global_earth_orientation_table(
    const internal::EarthOrientationTable* table
) noexcept {
    internal::WriteLockGuard lock(global_ephemeris_runtime_rwlock());
    return default_runtime().set_earth_orientation_table(table);
}

Status load_global_earth_orientation_table(const char* path) noexcept {
    internal::WriteLockGuard lock(global_ephemeris_runtime_rwlock());
    return default_runtime().load_earth_orientation_table(path);
}

Status load_global_builtin_earth_orientation_table() noexcept {
    internal::WriteLockGuard lock(global_ephemeris_runtime_rwlock());
    return default_runtime().load_builtin_earth_orientation_table();
}

Status load_global_lunar_limb_model(const char* path) noexcept {
    internal::WriteLockGuard lock(global_ephemeris_runtime_rwlock());
    return default_runtime().load_lunar_limb_model(path);
}

bool get_global_registered_data_sources(
    std::vector<RegisteredDataSource>* out
) noexcept {
    internal::ReadLockGuard lock(global_ephemeris_runtime_rwlock());
    return default_runtime().get_registered_data_sources(out);
}

void clear_global_ephemeris_cache() noexcept {
    internal::WriteLockGuard lock(global_ephemeris_runtime_rwlock());
    internal::EphemerisSegmentCache* segment_cache = default_runtime().ephemeris_segment_cache();
    if (segment_cache) {
        segment_cache->clear();
    }
}

bool find_global_ephemeris_descriptor(
    const EphemerisRequest& request,
    internal::EphemerisBlockDescriptor* out
) noexcept {
    if (out) {
        *out = internal::EphemerisBlockDescriptor();
    }
    if (!out) {
        return false;
    }

    internal::ReadLockGuard lock(global_ephemeris_runtime_rwlock());
    EphemerisRequest resolved_request;
    if (!resolve_request_route_rules(default_runtime(), request, &resolved_request)) {
        return false;
    }
    internal::EphemerisBlockDescriptor source;
    if (!default_runtime().ephemeris_engine().find_descriptor(resolved_request, &source)) {
        return false;
    }
    *out = source;
    return true;
}

size_t global_ephemeris_catalog_size() noexcept {
    internal::ReadLockGuard lock(global_ephemeris_runtime_rwlock());
    return default_runtime().ephemeris_catalog().size();
}

bool global_ephemeris_cache_contains(const internal::EphemerisSegmentCacheKey& key) noexcept {
    internal::ReadLockGuard lock(global_ephemeris_runtime_rwlock());
    const internal::EphemerisSegmentCache* segment_cache = default_runtime().ephemeris_segment_cache();
    return segment_cache && segment_cache->contains(key);
}

size_t global_ephemeris_cache_entry_count() noexcept {
    internal::ReadLockGuard lock(global_ephemeris_runtime_rwlock());
    const internal::EphemerisSegmentCache* segment_cache = default_runtime().ephemeris_segment_cache();
    return segment_cache ? segment_cache->entry_count() : 0;
}

}  // namespace runtime
}  // namespace taiyin
