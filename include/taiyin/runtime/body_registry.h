#ifndef TAIYIN_RUNTIME_BODY_REGISTRY_H
#define TAIYIN_RUNTIME_BODY_REGISTRY_H

#include "taiyin/status.h"

#include <cstddef>
#include <unordered_map>

namespace taiyin {
namespace runtime {

class EphemerisEngine;
struct EphemerisRequest;
struct EphemerisResult;
struct EphemerisEvalDiagnostic;

typedef Status (*EphemerisBodyFallbackFn)(
    EphemerisEngine* service,
    const EphemerisRequest& request,
    EphemerisResult* out,
    EphemerisEvalDiagnostic* diagnostic
);

struct EphemerisBodyRouteEntry {
    bool has_direct;
    EphemerisBodyFallbackFn fallback;

    EphemerisBodyRouteEntry() noexcept
        : has_direct(false), fallback(0) {}
};

class EphemerisBodyRegistry {
public:
    bool mark_direct(int body_id) noexcept;
    bool unmark_direct(int body_id) noexcept;
    bool set_fallback(int body_id, EphemerisBodyFallbackFn fn) noexcept;
    bool remove_fallback(int body_id) noexcept;
    bool find(int body_id, EphemerisBodyRouteEntry* out) const noexcept;
    void clear() noexcept;
    size_t size() const noexcept;
    void swap(EphemerisBodyRegistry& other) noexcept;

private:
    std::unordered_map<int, EphemerisBodyRouteEntry> entries_;
};

}  // namespace runtime
}  // namespace taiyin

#endif  // TAIYIN_RUNTIME_BODY_REGISTRY_H
