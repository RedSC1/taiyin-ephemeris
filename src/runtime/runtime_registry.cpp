#include "taiyin/runtime/runtime_registry.h"

#include <limits>
#include <sstream>
#include <unordered_map>
#include <vector>

namespace taiyin {
namespace runtime {
namespace {

const uint32_t INVALID_ID_VALUE = 0;
const uint32_t FIRST_REGISTRY_ID_VALUE = 1;

std::string make_method_key(uint32_t category, const std::string& name) {
    std::ostringstream stream;
    stream << category << ':' << name;
    return stream.str();
}

template <typename Descriptor>
const Descriptor* descriptor_by_id(const std::vector<Descriptor>& descriptors, uint32_t id_value) noexcept {
    if (id_value == INVALID_ID_VALUE) {
        return 0;
    }
    const uint32_t index = id_value - FIRST_REGISTRY_ID_VALUE;
    if (index >= descriptors.size()) {
        return 0;
    }
    return &descriptors[index];
}

bool can_allocate_next(size_t size) noexcept {
    return size < static_cast<size_t>(std::numeric_limits<uint32_t>::max());
}

}  // namespace

struct RuntimeRegistry::Impl {
    std::vector<MethodDescriptor> methods;
    std::unordered_map<std::string, uint32_t> method_key_to_id;
};

RuntimeRegistry::RuntimeRegistry()
    : impl_(new Impl()) {}

RuntimeRegistry::~RuntimeRegistry() {
    delete impl_;
    impl_ = 0;
}


MethodId RuntimeRegistry::register_method(
    uint32_t category,
    const std::string& name,
    RuntimeMethodFn execute
) noexcept {
    if (!impl_ || name.empty() || !execute) {
        return MethodId();
    }

    try {
        const std::string key = make_method_key(category, name);
        std::unordered_map<std::string, uint32_t>::const_iterator found = impl_->method_key_to_id.find(key);
        if (found != impl_->method_key_to_id.end()) {
            return MethodId(found->second);
        }
        if (!can_allocate_next(impl_->methods.size())) {
            return MethodId();
        }

        const uint32_t id_value = static_cast<uint32_t>(impl_->methods.size()) + FIRST_REGISTRY_ID_VALUE;
        MethodDescriptor descriptor;
        descriptor.id = MethodId(id_value);
        descriptor.category = category;
        descriptor.name = name;
        descriptor.execute = execute;

        impl_->methods.push_back(descriptor);
        impl_->method_key_to_id[key] = id_value;
        return MethodId(id_value);
    } catch (...) {
        return MethodId();
    }
}

bool RuntimeRegistry::resolve_method(uint32_t category, const std::string& name, MethodId* out) const noexcept {
    if (out) {
        *out = MethodId();
    }
    if (!impl_ || !out || name.empty()) {
        return false;
    }

    try {
        const std::string key = make_method_key(category, name);
        std::unordered_map<std::string, uint32_t>::const_iterator found = impl_->method_key_to_id.find(key);
        if (found == impl_->method_key_to_id.end()) {
            return false;
        }
        *out = MethodId(found->second);
        return true;
    } catch (...) {
        return false;
    }
}

const MethodDescriptor* RuntimeRegistry::method(MethodId id) const noexcept {
    if (!impl_) {
        return 0;
    }
    return descriptor_by_id(impl_->methods, id.value);
}

size_t RuntimeRegistry::method_count() const noexcept {
    return impl_ ? impl_->methods.size() : 0;
}

void RuntimeRegistry::clear() noexcept {
    if (!impl_) {
        return;
    }
    try {
        impl_->methods.clear();
        impl_->method_key_to_id.clear();
    } catch (...) {
    }
}

RuntimeRegistry& default_runtime_registry() noexcept {
    static RuntimeRegistry registry;
    return registry;
}

}  // namespace runtime
}  // namespace taiyin
