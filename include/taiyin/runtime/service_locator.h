#ifndef TAIYIN_RUNTIME_SERVICE_LOCATOR_H
#define TAIYIN_RUNTIME_SERVICE_LOCATOR_H

#include "handle_types.h"

#include <cstddef>
#include <string>

namespace taiyin {
namespace runtime {

struct ServiceDescriptor {
    ServiceId id;
    std::string name;
    void* service;

    ServiceDescriptor()
        : id(), name(), service(0) {}
};

class ServiceLocator {
public:
    ServiceLocator();
    ~ServiceLocator();

    ServiceLocator(const ServiceLocator&) = delete;
    ServiceLocator& operator=(const ServiceLocator&) = delete;

    ServiceId register_service(const std::string& name, void* service) noexcept;
    bool resolve_service(const std::string& name, ServiceId* out) const noexcept;
    void* service(ServiceId id) const noexcept;
    const ServiceDescriptor* descriptor(ServiceId id) const noexcept;
    size_t service_count() const noexcept;
    void clear() noexcept;

private:
    struct Impl;
    Impl* impl_;
};

}  // namespace runtime
}  // namespace taiyin

#endif  // TAIYIN_RUNTIME_SERVICE_LOCATOR_H
