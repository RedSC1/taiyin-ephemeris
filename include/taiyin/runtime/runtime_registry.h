#ifndef TAIYIN_RUNTIME_RUNTIME_REGISTRY_H
#define TAIYIN_RUNTIME_RUNTIME_REGISTRY_H

#include "handle_types.h"

#include <cstddef>
#include <cstdint>
#include <string>

namespace taiyin {
namespace runtime {

typedef bool (*RuntimeMethodFn)(const void* request, void* out);

struct MethodDescriptor {
    MethodId id;
    uint32_t category;
    std::string name;
    RuntimeMethodFn execute;

    MethodDescriptor()
        : id(), category(0), name(), execute(0) {}
};

class RuntimeRegistry {
public:
    RuntimeRegistry();
    ~RuntimeRegistry();

    RuntimeRegistry(const RuntimeRegistry&) = delete;
    RuntimeRegistry& operator=(const RuntimeRegistry&) = delete;

    MethodId register_method(
        uint32_t category,
        const std::string& name,
        RuntimeMethodFn execute
    ) noexcept;
    bool resolve_method(uint32_t category, const std::string& name, MethodId* out) const noexcept;
    const MethodDescriptor* method(MethodId id) const noexcept;
    size_t method_count() const noexcept;

    void clear() noexcept;

private:
    struct Impl;
    Impl* impl_;
};

RuntimeRegistry& default_runtime_registry() noexcept;

}  // namespace runtime
}  // namespace taiyin

#endif  // TAIYIN_RUNTIME_RUNTIME_REGISTRY_H
