#include "taiyin/runtime/service_locator.h"

#include <limits>
#include <unordered_map>
#include <vector>

namespace taiyin {
namespace runtime {
namespace {

const uint32_t INVALID_ID_VALUE = 0;
const uint32_t FIRST_SERVICE_ID_VALUE = 1;

bool can_allocate_next(size_t size) noexcept {
    return size < static_cast<size_t>(std::numeric_limits<uint32_t>::max());
}

const ServiceDescriptor* descriptor_by_id(
    const std::vector<ServiceDescriptor>& descriptors,
    uint32_t id_value
) noexcept {
    if (id_value == INVALID_ID_VALUE) {
        return 0;
    }
    const uint32_t index = id_value - FIRST_SERVICE_ID_VALUE;
    if (index >= descriptors.size()) {
        return 0;
    }
    return &descriptors[index];
}

}  // namespace

struct ServiceLocator::Impl {
    std::vector<ServiceDescriptor> services;
    std::unordered_map<std::string, uint32_t> service_name_to_id;
};

ServiceLocator::ServiceLocator()
    : impl_(new Impl()) {}

ServiceLocator::~ServiceLocator() {
    delete impl_;
    impl_ = 0;
}

ServiceId ServiceLocator::register_service(const std::string& name, void* service) noexcept {
    if (!impl_ || name.empty() || !service) {
        return ServiceId();
    }

    try {
        std::unordered_map<std::string, uint32_t>::const_iterator found = impl_->service_name_to_id.find(name);
        if (found != impl_->service_name_to_id.end()) {
            return ServiceId(found->second);
        }
        if (!can_allocate_next(impl_->services.size())) {
            return ServiceId();
        }

        const uint32_t id_value = static_cast<uint32_t>(impl_->services.size()) + FIRST_SERVICE_ID_VALUE;
        ServiceDescriptor descriptor;
        descriptor.id = ServiceId(id_value);
        descriptor.name = name;
        descriptor.service = service;

        impl_->services.push_back(descriptor);
        impl_->service_name_to_id[name] = id_value;
        return ServiceId(id_value);
    } catch (...) {
        return ServiceId();
    }
}

bool ServiceLocator::resolve_service(const std::string& name, ServiceId* out) const noexcept {
    if (out) {
        *out = ServiceId();
    }
    if (!impl_ || !out || name.empty()) {
        return false;
    }

    try {
        std::unordered_map<std::string, uint32_t>::const_iterator found = impl_->service_name_to_id.find(name);
        if (found == impl_->service_name_to_id.end()) {
            return false;
        }
        *out = ServiceId(found->second);
        return true;
    } catch (...) {
        return false;
    }
}

void* ServiceLocator::service(ServiceId id) const noexcept {
    if (!impl_) {
        return 0;
    }
    const ServiceDescriptor* service_descriptor = descriptor_by_id(impl_->services, id.value);
    return service_descriptor ? service_descriptor->service : 0;
}

const ServiceDescriptor* ServiceLocator::descriptor(ServiceId id) const noexcept {
    if (!impl_) {
        return 0;
    }
    return descriptor_by_id(impl_->services, id.value);
}

size_t ServiceLocator::service_count() const noexcept {
    return impl_ ? impl_->services.size() : 0;
}

void ServiceLocator::clear() noexcept {
    if (!impl_) {
        return;
    }
    try {
        impl_->services.clear();
        impl_->service_name_to_id.clear();
    } catch (...) {
    }
}

}  // namespace runtime
}  // namespace taiyin
