#ifndef TAIYIN_RUNTIME_TAIYIN_RUNTIME_H
#define TAIYIN_RUNTIME_TAIYIN_RUNTIME_H

#include "ephemeris_service.h"
#include "runtime_registry.h"
#include "service_locator.h"

#include <cstddef>

namespace taiyin {
namespace runtime {

struct EphemerisRuntimeConfig {
    size_t cache_max_bytes;
    internal::EphemerisCachePolicy cache_policy;
    const char* const* source_paths;
    size_t source_path_count;
    bool strict_discovery;

    EphemerisRuntimeConfig() noexcept;
};

class TaiyinRuntime {
public:
    TaiyinRuntime() noexcept;
    ~TaiyinRuntime() noexcept;

    TaiyinRuntime(const TaiyinRuntime&) = delete;
    TaiyinRuntime& operator=(const TaiyinRuntime&) = delete;

    RuntimeRegistry& registry() noexcept;
    const RuntimeRegistry& registry() const noexcept;

    ServiceLocator& services() noexcept;
    const ServiceLocator& services() const noexcept;

    EphemerisService& ephemeris_service() noexcept;
    const EphemerisService& ephemeris_service() const noexcept;

    internal::EphemerisBlockCatalog& ephemeris_catalog() noexcept;
    const internal::EphemerisBlockCatalog& ephemeris_catalog() const noexcept;
    internal::EphemerisPriorityRegistry& ephemeris_priorities() noexcept;
    const internal::EphemerisPriorityRegistry& ephemeris_priorities() const noexcept;
    internal::EphemerisBlockCache* ephemeris_cache() noexcept;
    const internal::EphemerisBlockCache* ephemeris_cache() const noexcept;

    bool initialize_ephemeris(const EphemerisRuntimeConfig& config) noexcept;
    bool add_ephemeris_source_path(const char* path, bool strict_discovery = false) noexcept;
    TaiyinStatus eval_ephemeris_state(
        const EphemerisRequest& request,
        EphemerisResult* out,
        EphemerisEvalDiagnostic* diagnostic
    ) noexcept;

    ServiceId ephemeris_service_id() const noexcept;
    void clear_registries() noexcept;

private:
    void reset_ephemeris_bindings() noexcept;

    RuntimeRegistry registry_;
    ServiceLocator services_;
    internal::EphemerisBlockCatalog ephemeris_catalog_;
    internal::EphemerisPriorityRegistry ephemeris_priorities_;
    internal::EphemerisBlockCache* ephemeris_cache_;
    EphemerisService ephemeris_service_;
    ServiceId ephemeris_service_id_;
};

TaiyinRuntime& default_taiyin_runtime() noexcept;
bool initialize_global_ephemeris_runtime(const EphemerisRuntimeConfig& config) noexcept;
bool add_global_ephemeris_source_path(const char* path) noexcept;
TaiyinStatus eval_global_ephemeris_state(
    const EphemerisRequest& request,
    EphemerisResult* out,
    EphemerisEvalDiagnostic* diagnostic
) noexcept;

bool add_global_ephemeris_descriptor(const internal::EphemerisBlockDescriptor& descriptor) noexcept;
bool set_global_ephemeris_method_priority(int method_id, int priority) noexcept;
bool set_global_ephemeris_target_method_priority(int target_id, int method_id, int priority) noexcept;
void clear_global_ephemeris_cache() noexcept;

bool find_global_ephemeris_descriptor(
    const EphemerisRequest& request,
    internal::EphemerisBlockDescriptor* out
) noexcept;
size_t global_ephemeris_catalog_size() noexcept;
bool global_ephemeris_cache_contains(const internal::EphemerisRouteKey& key) noexcept;
size_t global_ephemeris_cache_entry_count() noexcept;
size_t global_ephemeris_cache_total_bytes() noexcept;
size_t global_ephemeris_cache_max_bytes() noexcept;

// Low-level unsafe accessors: these return internal objects without acquiring
// the global runtime lock. Prefer the locked helper APIs above for concurrent
// use. These remain for compatibility and single-threaded binding checks.
internal::EphemerisBlockCatalog& global_ephemeris_catalog() noexcept;
internal::EphemerisPriorityRegistry& global_ephemeris_priorities() noexcept;
internal::EphemerisBlockCache* global_ephemeris_cache() noexcept;
EphemerisService& global_ephemeris_service() noexcept;

}  // namespace runtime
}  // namespace taiyin

#endif  // TAIYIN_RUNTIME_TAIYIN_RUNTIME_H
