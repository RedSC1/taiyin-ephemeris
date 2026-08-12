#ifndef TAIYIN_RUNTIME_RUNTIME_H
#define TAIYIN_RUNTIME_RUNTIME_H

#include "body_registry.h"
#include "ephemeris_engine.h"
#include "ephemeris_route.h"
#include "taiyin/internal/custom_ephemeris_method.h"
#include "taiyin/internal/ephemeris_route_rule.h"
#include "taiyin/internal/ephemeris_source_priority.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace taiyin {

struct Tll1LunarLimbModel;

namespace internal {
struct EarthOrientationTable;
}

namespace runtime {

enum RuntimeDataSourceKind {
    TAIYIN_RUNTIME_DATA_SOURCE_EPHEMERIS = 1,
    TAIYIN_RUNTIME_DATA_SOURCE_EARTH_ORIENTATION = 2,
    TAIYIN_RUNTIME_DATA_SOURCE_LUNAR_LIMB = 3,
};

enum RuntimeDataSourceFormat {
    TAIYIN_RUNTIME_DATA_FORMAT_UNKNOWN = 0,
    TAIYIN_RUNTIME_DATA_FORMAT_OPM2 = 1,
    TAIYIN_RUNTIME_DATA_FORMAT_SPK = 2,
    TAIYIN_RUNTIME_DATA_FORMAT_KEPLER = 3,
    TAIYIN_RUNTIME_DATA_FORMAT_SEMI_ANALYTIC = 4,
    TAIYIN_RUNTIME_DATA_FORMAT_FIXED_STAR = 5,
    TAIYIN_RUNTIME_DATA_FORMAT_TSC1 = 6,
    TAIYIN_RUNTIME_DATA_FORMAT_TKC1 = 7,
    TAIYIN_RUNTIME_DATA_FORMAT_CUSTOM = 8,
    TAIYIN_RUNTIME_DATA_FORMAT_FINALS2000A = 100,
    TAIYIN_RUNTIME_DATA_FORMAT_BUILTIN_EOP = 101,
    TAIYIN_RUNTIME_DATA_FORMAT_TLL1 = 200,
    TAIYIN_RUNTIME_DATA_FORMAT_MEMORY = 1000,
};

const uint32_t TAIYIN_RUNTIME_DATA_SOURCE_HAS_COVERAGE = 1u << 0;
const uint32_t TAIYIN_RUNTIME_DATA_SOURCE_BUILTIN = 1u << 1;
const uint32_t TAIYIN_RUNTIME_DATA_SOURCE_MEMORY = 1u << 2;

struct RegisteredDataSource {
    RuntimeDataSourceKind kind;
    RuntimeDataSourceFormat format;
    uint32_t flags;
    std::string source;
    size_t item_count;
    double jd_start;
    double jd_end;

    RegisteredDataSource() noexcept;
};

struct EphemerisRuntimeConfig {
    size_t segment_cache_max_entries;
    const char* const* source_paths;
    size_t source_path_count;
    const char* data_root;
    const char* eop_path;
    const char* lunar_limb_path;
    bool load_packaged_data;
    bool load_builtin_eop;
    bool strict_discovery;

    EphemerisRuntimeConfig() noexcept;
};

class Runtime {
public:
    Runtime() noexcept;
    ~Runtime() noexcept;

    Runtime(const Runtime&) = delete;
    Runtime& operator=(const Runtime&) = delete;

    EphemerisEngine& ephemeris_engine() noexcept;
    const EphemerisEngine& ephemeris_engine() const noexcept;

    internal::EphemerisBlockCatalog& ephemeris_catalog() noexcept;
    const internal::EphemerisBlockCatalog& ephemeris_catalog() const noexcept;
    internal::EphemerisSegmentCache* ephemeris_segment_cache() noexcept;
    const internal::EphemerisSegmentCache* ephemeris_segment_cache() const noexcept;
    EphemerisBodyRegistry& ephemeris_body_registry() noexcept;
    const EphemerisBodyRegistry& ephemeris_body_registry() const noexcept;
    internal::EphemerisRouteRuleTable* ephemeris_route_rule(uint64_t route_rule_id) noexcept;
    const internal::EphemerisRouteRuleTable* ephemeris_route_rule(uint64_t route_rule_id) const noexcept;
    bool register_ephemeris_route_rule(
        uint64_t route_rule_id,
        const internal::EphemerisRouteRuleTable& table
    ) noexcept;
    // Setup-time policy. `path_or_basename` is either an exact source path or
    // a bare filename. It replaces that file's provider-default numeric value;
    // higher priority wins inside the provider and reorders AUTO's
    // source-specific product rules without crossing provider/method route
    // boundaries. Matching is case-insensitive on Windows and case-sensitive
    // on POSIX.
    bool set_ephemeris_source_priority(
        const char* path_or_basename,
        int priority
    ) noexcept;
    bool clear_ephemeris_source_priority(
        const char* path_or_basename
    ) noexcept;
    void clear_all_ephemeris_source_priorities() noexcept;
    const internal::EarthOrientationTable* earth_orientation_table() const noexcept;
    const Tll1LunarLimbModel* lunar_limb_model() const noexcept;
    bool get_registered_data_sources(
        std::vector<RegisteredDataSource>* out
    ) const noexcept;
    bool set_earth_orientation_table(
        const internal::EarthOrientationTable* table
    ) noexcept;
    Status load_earth_orientation_table(const char* path) noexcept;
    Status load_builtin_earth_orientation_table() noexcept;
    Status load_lunar_limb_model(const char* path) noexcept;

    bool initialize_ephemeris(const EphemerisRuntimeConfig& config) noexcept;
    bool add_ephemeris_source_path(const char* path, bool strict_discovery = false) noexcept;
    bool rebuild_ephemeris_body_registry(
        const internal::EphemerisBlockCatalog& catalog,
        EphemerisBodyRegistry* out_registry
    ) noexcept;
    Status eval_ephemeris_state(
        const EphemerisRequest& request,
        EphemerisResult* out,
        EphemerisEvalDiagnostic* diagnostic
    ) noexcept;

private:
    void reset_ephemeris_bindings() noexcept;
    bool reset_default_route_rules() noexcept;
    Status replace_earth_orientation_table(
        internal::EarthOrientationTable* replacement
    ) noexcept;

    internal::EphemerisBlockCatalog ephemeris_catalog_;
    internal::EphemerisSegmentCache* ephemeris_segment_cache_;
    EphemerisBodyRegistry ephemeris_body_registry_;
    std::unordered_map<uint64_t, internal::EphemerisRouteRuleTable> ephemeris_route_rules_;
    internal::EphemerisSourcePriorityTable ephemeris_source_priorities_;
    EphemerisEngine ephemeris_engine_;
    internal::EarthOrientationTable* earth_orientation_table_;
    std::string earth_orientation_source_;
    RuntimeDataSourceFormat earth_orientation_format_;
    uint32_t earth_orientation_source_flags_;
    std::vector<internal::EarthOrientationTable*>
        retired_earth_orientation_tables_;
    Tll1LunarLimbModel* lunar_limb_model_;
    std::string lunar_limb_source_;
    std::vector<Tll1LunarLimbModel*> retired_lunar_limb_models_;
};

Runtime& default_runtime() noexcept;
bool initialize_global_ephemeris_runtime(const EphemerisRuntimeConfig& config) noexcept;
bool add_global_ephemeris_source_path(const char* path) noexcept;
bool set_global_ephemeris_source_priority(
    const char* path_or_basename,
    int priority
) noexcept;
bool clear_global_ephemeris_source_priority(
    const char* path_or_basename
) noexcept;
void clear_all_global_ephemeris_source_priorities() noexcept;
Status eval_global_ephemeris_state(
    const EphemerisRequest& request,
    EphemerisResult* out,
    EphemerisEvalDiagnostic* diagnostic
) noexcept;

bool add_global_ephemeris_descriptor(const internal::EphemerisBlockDescriptor& descriptor) noexcept;
bool add_global_custom_ephemeris_method(
    const internal::CustomEphemerisMethodDefinition& definition,
    int priority,
    const char* description,
    internal::EphemerisBlockDescriptor* out_descriptor
) noexcept;
bool add_global_custom_ephemeris_file_method(
    const internal::CustomEphemerisFileMethodDefinition& definition,
    int priority,
    const char* description,
    internal::EphemerisBlockDescriptor* out_descriptor
) noexcept;
// Route rules are setup-time configuration. Register a new id instead of
// mutating or unregistering a table that a NativeCalcContext may already hold.
bool register_global_ephemeris_route_rule(
    uint64_t route_rule_id,
    const internal::EphemerisRouteRuleTable& table
) noexcept;
const internal::EphemerisRouteRuleTable* global_ephemeris_route_rule(uint64_t route_rule_id) noexcept;
// The returned immutable snapshot remains valid until the global Runtime is
// destroyed, even if a later setup-time call installs a replacement table.
const internal::EarthOrientationTable* global_earth_orientation_table() noexcept;
const Tll1LunarLimbModel* global_lunar_limb_model() noexcept;
// Setup-time operations. Global runtime data is immutable while calculations run.
bool set_global_earth_orientation_table(
    const internal::EarthOrientationTable* table
) noexcept;
Status load_global_earth_orientation_table(const char* path) noexcept;
Status load_global_builtin_earth_orientation_table() noexcept;
Status load_global_lunar_limb_model(const char* path) noexcept;
bool get_global_registered_data_sources(
    std::vector<RegisteredDataSource>* out
) noexcept;
void clear_global_ephemeris_cache() noexcept;

bool find_global_ephemeris_descriptor(
    const EphemerisRequest& request,
    internal::EphemerisBlockDescriptor* out
) noexcept;
size_t global_ephemeris_catalog_size() noexcept;
bool global_ephemeris_cache_contains(const internal::EphemerisSegmentCacheKey& key) noexcept;
size_t global_ephemeris_cache_entry_count() noexcept;

}  // namespace runtime
}  // namespace taiyin

#endif  // TAIYIN_RUNTIME_RUNTIME_H
