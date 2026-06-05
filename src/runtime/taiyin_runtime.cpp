#include "taiyin/runtime/taiyin_runtime.h"

#include "taiyin/internal/ephemeris_discovery.h"
#include "taiyin/internal/kepler_catalog_tkc1.h"
#include "taiyin/internal/kepler_file.h"
#include "taiyin/internal/writer_preferred_rwlock.h"
#include "taiyin/star_catalog_tsc1.h"

#include <new>
#include <string>
#include <sys/stat.h>
#include <vector>

namespace taiyin {
namespace runtime {
namespace {

const size_t DEFAULT_EPHEMERIS_CACHE_MAX_BYTES = 64u * 1024u * 1024u;
const int OPM4_METHOD_ID = 1;
const int SPK_METHOD_ID = 2;

internal::WriterPreferredRwLock& global_ephemeris_runtime_rwlock() {
    static internal::WriterPreferredRwLock lock;
    return lock;
}

void initialize_default_ephemeris_priorities(
    internal::EphemerisPriorityRegistry* priorities
) noexcept {
    if (!priorities) {
        return;
    }

    priorities->set_global_method_priority(SPK_METHOD_ID, 100);
    priorities->set_global_method_priority(OPM4_METHOD_ID, 90);
    priorities->set_global_method_priority(internal::TKC1_KEPLER_METHOD_ID, 70);
    priorities->set_global_method_priority(internal::TAIYIN_KEPLER_FILE_METHOD_ID, 60);
    priorities->set_global_method_priority(TSC1_STAR_METHOD_ID, 50);
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

bool add_descriptors_from_source_path(
    const char* path,
    bool strict_discovery,
    internal::EphemerisBlockCatalog* catalog
) noexcept {
    if (!path || path[0] == '\0' || !catalog) {
        return false;
    }

    struct stat st;
    if (stat(path, &st) != 0) {
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
    const std::string source_path(path);
    if (S_ISDIR(st.st_mode)) {
        discovered = internal::discover_ephemeris_descriptors_from_directory(
            source_path,
            discoverers,
            options,
            &descriptors);
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
        for (size_t i = 0; i < descriptors.size(); ++i) {
            if (!catalog->add(descriptors[i])) {
                return false;
            }
        }
    } catch (...) {
        return false;
    }
    return true;
}

size_t normalized_cache_max_bytes(size_t value) noexcept {
    return value == 0 ? DEFAULT_EPHEMERIS_CACHE_MAX_BYTES : value;
}

}  // namespace

EphemerisRuntimeConfig::EphemerisRuntimeConfig() noexcept
    : cache_max_bytes(DEFAULT_EPHEMERIS_CACHE_MAX_BYTES),
      cache_policy(),
      source_paths(0),
      source_path_count(0),
      strict_discovery(false) {}

TaiyinRuntime::TaiyinRuntime() noexcept
    : registry_(),
      services_(),
      ephemeris_catalog_(),
      ephemeris_priorities_(),
      ephemeris_cache_(new (std::nothrow) internal::EphemerisBlockCache(DEFAULT_EPHEMERIS_CACHE_MAX_BYTES)),
      ephemeris_service_(),
      ephemeris_service_id_() {
    initialize_default_ephemeris_priorities(&ephemeris_priorities_);
    reset_ephemeris_bindings();
    ephemeris_service_id_ = services_.register_service("core.ephemeris", &ephemeris_service_);
}

TaiyinRuntime::~TaiyinRuntime() noexcept {
    delete ephemeris_cache_;
    ephemeris_cache_ = 0;
}

RuntimeRegistry& TaiyinRuntime::registry() noexcept {
    return registry_;
}

const RuntimeRegistry& TaiyinRuntime::registry() const noexcept {
    return registry_;
}

ServiceLocator& TaiyinRuntime::services() noexcept {
    return services_;
}

const ServiceLocator& TaiyinRuntime::services() const noexcept {
    return services_;
}

EphemerisService& TaiyinRuntime::ephemeris_service() noexcept {
    return ephemeris_service_;
}

const EphemerisService& TaiyinRuntime::ephemeris_service() const noexcept {
    return ephemeris_service_;
}

internal::EphemerisBlockCatalog& TaiyinRuntime::ephemeris_catalog() noexcept {
    return ephemeris_catalog_;
}

const internal::EphemerisBlockCatalog& TaiyinRuntime::ephemeris_catalog() const noexcept {
    return ephemeris_catalog_;
}

internal::EphemerisPriorityRegistry& TaiyinRuntime::ephemeris_priorities() noexcept {
    return ephemeris_priorities_;
}

const internal::EphemerisPriorityRegistry& TaiyinRuntime::ephemeris_priorities() const noexcept {
    return ephemeris_priorities_;
}

internal::EphemerisBlockCache* TaiyinRuntime::ephemeris_cache() noexcept {
    return ephemeris_cache_;
}

const internal::EphemerisBlockCache* TaiyinRuntime::ephemeris_cache() const noexcept {
    return ephemeris_cache_;
}

bool TaiyinRuntime::initialize_ephemeris(const EphemerisRuntimeConfig& config) noexcept {
    internal::EphemerisBlockCatalog next_catalog;
    internal::EphemerisPriorityRegistry next_priorities;
    initialize_default_ephemeris_priorities(&next_priorities);

    internal::EphemerisBlockCache* next_cache = new (std::nothrow) internal::EphemerisBlockCache(
        normalized_cache_max_bytes(config.cache_max_bytes),
        config.cache_policy);
    if (!next_cache) {
        return false;
    }

    bool ok = true;
    if (config.source_path_count > 0 && !config.source_paths) {
        ok = false;
    }

    for (size_t i = 0; ok && i < config.source_path_count; ++i) {
        if (!add_descriptors_from_source_path(
                config.source_paths[i],
                config.strict_discovery,
                &next_catalog)) {
            ok = false;
        }
    }

    if (!ok) {
        next_catalog = internal::EphemerisBlockCatalog();
    }

    try {
        ephemeris_catalog_ = next_catalog;
        ephemeris_priorities_ = next_priorities;
    } catch (...) {
        delete next_cache;
        return false;
    }

    delete ephemeris_cache_;
    ephemeris_cache_ = next_cache;
    reset_ephemeris_bindings();
    return ok;
}

bool TaiyinRuntime::add_ephemeris_source_path(const char* path, bool strict_discovery) noexcept {
    return add_descriptors_from_source_path(path, strict_discovery, &ephemeris_catalog_);
}

TaiyinStatus TaiyinRuntime::eval_ephemeris_state(
    const EphemerisRequest& request,
    EphemerisResult* out,
    EphemerisEvalDiagnostic* diagnostic
) noexcept {
    return ephemeris_service_.eval_state(request, out, diagnostic);
}

ServiceId TaiyinRuntime::ephemeris_service_id() const noexcept {
    return ephemeris_service_id_;
}

void TaiyinRuntime::clear_registries() noexcept {
    registry_.clear();
    services_.clear();
    reset_ephemeris_bindings();
    ephemeris_service_id_ = services_.register_service("core.ephemeris", &ephemeris_service_);
}

void TaiyinRuntime::reset_ephemeris_bindings() noexcept {
    ephemeris_service_.set_catalog(&ephemeris_catalog_);
    ephemeris_service_.set_priorities(&ephemeris_priorities_);
    ephemeris_service_.set_cache(ephemeris_cache_);
}

TaiyinRuntime& default_taiyin_runtime() noexcept {
    static TaiyinRuntime runtime;
    return runtime;
}

bool initialize_global_ephemeris_runtime(const EphemerisRuntimeConfig& config) noexcept {
    internal::WriteLockGuard lock(global_ephemeris_runtime_rwlock());
    return default_taiyin_runtime().initialize_ephemeris(config);
}

bool add_global_ephemeris_source_path(const char* path) noexcept {
    internal::WriteLockGuard lock(global_ephemeris_runtime_rwlock());
    return default_taiyin_runtime().add_ephemeris_source_path(path, false);
}

TaiyinStatus eval_global_ephemeris_state(
    const EphemerisRequest& request,
    EphemerisResult* out,
    EphemerisEvalDiagnostic* diagnostic
) noexcept {
    internal::ReadLockGuard lock(global_ephemeris_runtime_rwlock());
    return default_taiyin_runtime().eval_ephemeris_state(request, out, diagnostic);
}

bool add_global_ephemeris_descriptor(const internal::EphemerisBlockDescriptor& descriptor) noexcept {
    internal::WriteLockGuard lock(global_ephemeris_runtime_rwlock());
    return default_taiyin_runtime().ephemeris_catalog().add(descriptor);
}

bool set_global_ephemeris_method_priority(int method_id, int priority) noexcept {
    internal::WriteLockGuard lock(global_ephemeris_runtime_rwlock());
    return default_taiyin_runtime().ephemeris_priorities().set_global_method_priority(method_id, priority);
}

bool set_global_ephemeris_target_method_priority(int target_id, int method_id, int priority) noexcept {
    internal::WriteLockGuard lock(global_ephemeris_runtime_rwlock());
    return default_taiyin_runtime().ephemeris_priorities().set_target_method_priority(target_id, method_id, priority);
}

void clear_global_ephemeris_cache() noexcept {
    internal::WriteLockGuard lock(global_ephemeris_runtime_rwlock());
    internal::EphemerisBlockCache* cache = default_taiyin_runtime().ephemeris_cache();
    if (cache) {
        cache->clear();
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
    const internal::EphemerisBlockDescriptor* source = 0;
    if (!default_taiyin_runtime().ephemeris_service().find_descriptor(request, &source) || !source) {
        return false;
    }
    *out = *source;
    return true;
}

size_t global_ephemeris_catalog_size() noexcept {
    internal::ReadLockGuard lock(global_ephemeris_runtime_rwlock());
    return default_taiyin_runtime().ephemeris_catalog().size();
}

bool global_ephemeris_cache_contains(const internal::EphemerisRouteKey& key) noexcept {
    internal::ReadLockGuard lock(global_ephemeris_runtime_rwlock());
    const internal::EphemerisBlockCache* cache = default_taiyin_runtime().ephemeris_cache();
    return cache && cache->contains(key);
}

size_t global_ephemeris_cache_entry_count() noexcept {
    internal::ReadLockGuard lock(global_ephemeris_runtime_rwlock());
    const internal::EphemerisBlockCache* cache = default_taiyin_runtime().ephemeris_cache();
    return cache ? cache->entry_count() : 0;
}

size_t global_ephemeris_cache_total_bytes() noexcept {
    internal::ReadLockGuard lock(global_ephemeris_runtime_rwlock());
    const internal::EphemerisBlockCache* cache = default_taiyin_runtime().ephemeris_cache();
    return cache ? cache->total_bytes() : 0;
}

size_t global_ephemeris_cache_max_bytes() noexcept {
    internal::ReadLockGuard lock(global_ephemeris_runtime_rwlock());
    const internal::EphemerisBlockCache* cache = default_taiyin_runtime().ephemeris_cache();
    return cache ? cache->max_bytes() : 0;
}

// Low-level unsafe accessors: these return internal objects without acquiring
// the global runtime lock. Prefer the locked helper APIs above for concurrent use.
internal::EphemerisBlockCatalog& global_ephemeris_catalog() noexcept {
    return default_taiyin_runtime().ephemeris_catalog();
}

internal::EphemerisPriorityRegistry& global_ephemeris_priorities() noexcept {
    return default_taiyin_runtime().ephemeris_priorities();
}

internal::EphemerisBlockCache* global_ephemeris_cache() noexcept {
    return default_taiyin_runtime().ephemeris_cache();
}

EphemerisService& global_ephemeris_service() noexcept {
    return default_taiyin_runtime().ephemeris_service();
}

}  // namespace runtime
}  // namespace taiyin
