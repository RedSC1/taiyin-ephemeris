#include "taiyin/legacy/pipeline_v2/artifact_store.h"

#include <cstdlib>
#include <cstring>
#include <unordered_map>

namespace taiyin {
namespace pipeline_v2 {
namespace {

bool artifact_type_matches(const runtime::ArtifactKey& key, runtime::ArtifactTypeId expected_type) noexcept {
    return key.is_valid()
        && expected_type.is_valid()
        && key.type_id == expected_type.value;
}

bool alignment_supported(size_t alignment) noexcept {
    return alignment == 0 || alignment <= alignof(std::max_align_t);
}

}  // namespace

struct ArtifactStore::Impl {
    struct Entry {
        runtime::ArtifactTypeId type;
        void* value;
        size_t byte_size;
        runtime::RuntimeDestroyFn destroy;

        Entry()
            : type(), value(0), byte_size(0), destroy(0) {}
    };

    const runtime::RuntimeRegistry* registry;
    std::unordered_map<runtime::ArtifactKey, Entry, runtime::ArtifactKeyHash> values;

    Impl()
        : registry(0), values() {}
};

ArtifactStore::ArtifactStore(const runtime::RuntimeRegistry* registry)
    : impl_(new Impl()) {
    impl_->registry = registry;
}

ArtifactStore::~ArtifactStore() {
    clear();
    delete impl_;
    impl_ = 0;
}

void ArtifactStore::set_runtime_registry(const runtime::RuntimeRegistry* registry) noexcept {
    if (!impl_) {
        return;
    }
    impl_->registry = registry;
}

const runtime::RuntimeRegistry* ArtifactStore::runtime_registry() const noexcept {
    return impl_ ? impl_->registry : 0;
}

bool ArtifactStore::set(
    const runtime::ArtifactKey& key,
    const void* value,
    runtime::ArtifactTypeId expected_type
) noexcept {
    if (!impl_ || !impl_->registry || !value || !artifact_type_matches(key, expected_type)) {
        return false;
    }

    const runtime::ArtifactTypeDescriptor* descriptor = impl_->registry->artifact_type(expected_type);
    if (!descriptor || descriptor->byte_size == 0 || !alignment_supported(descriptor->byte_alignment)) {
        return false;
    }

    void* copied_value = 0;
    try {
        copied_value = std::malloc(descriptor->byte_size);
        if (!copied_value) {
            return false;
        }
        if (descriptor->copy) {
            descriptor->copy(copied_value, value);
        } else {
            std::memcpy(copied_value, value, descriptor->byte_size);
        }

        Impl::Entry entry;
        entry.type = expected_type;
        entry.value = copied_value;
        entry.byte_size = descriptor->byte_size;
        entry.destroy = descriptor->destroy;

        std::unordered_map<runtime::ArtifactKey, Impl::Entry, runtime::ArtifactKeyHash>::iterator found =
            impl_->values.find(key);
        if (found != impl_->values.end()) {
            Impl::Entry old_entry = found->second;
            found->second = entry;
            if (old_entry.value) {
                if (old_entry.destroy) {
                    old_entry.destroy(old_entry.value);
                }
                std::free(old_entry.value);
            }
        } else {
            impl_->values.insert(std::make_pair(key, entry));
        }
        return true;
    } catch (...) {
        if (copied_value) {
            if (descriptor->destroy) {
                descriptor->destroy(copied_value);
            }
            std::free(copied_value);
        }
        return false;
    }
}

bool ArtifactStore::get(
    const runtime::ArtifactKey& key,
    void* out,
    runtime::ArtifactTypeId expected_type
) const noexcept {
    if (!impl_ || !out || !artifact_type_matches(key, expected_type)) {
        return false;
    }

    try {
        std::unordered_map<runtime::ArtifactKey, Impl::Entry, runtime::ArtifactKeyHash>::const_iterator found =
            impl_->values.find(key);
        if (found == impl_->values.end() || found->second.type != expected_type || !found->second.value) {
            return false;
        }

        const runtime::ArtifactTypeDescriptor* descriptor = impl_->registry
            ? impl_->registry->artifact_type(expected_type)
            : 0;
        if (!descriptor || descriptor->byte_size != found->second.byte_size) {
            return false;
        }
        if (descriptor->copy) {
            descriptor->copy(out, found->second.value);
        } else {
            std::memcpy(out, found->second.value, found->second.byte_size);
        }
        return true;
    } catch (...) {
        return false;
    }
}

bool ArtifactStore::contains(const runtime::ArtifactKey& key) const noexcept {
    if (!impl_ || !key.is_valid()) {
        return false;
    }
    try {
        return impl_->values.find(key) != impl_->values.end();
    } catch (...) {
        return false;
    }
}

bool ArtifactStore::erase(const runtime::ArtifactKey& key) noexcept {
    if (!impl_ || !key.is_valid()) {
        return false;
    }
    try {
        std::unordered_map<runtime::ArtifactKey, Impl::Entry, runtime::ArtifactKeyHash>::iterator found =
            impl_->values.find(key);
        if (found == impl_->values.end()) {
            return false;
        }
        if (found->second.value) {
            if (found->second.destroy) {
                found->second.destroy(found->second.value);
            }
            std::free(found->second.value);
        }
        impl_->values.erase(found);
        return true;
    } catch (...) {
        return false;
    }
}

size_t ArtifactStore::size() const noexcept {
    return impl_ ? impl_->values.size() : 0;
}

void ArtifactStore::clear() noexcept {
    if (!impl_) {
        return;
    }
    try {
        std::unordered_map<runtime::ArtifactKey, Impl::Entry, runtime::ArtifactKeyHash>::iterator it =
            impl_->values.begin();
        while (it != impl_->values.end()) {
            if (it->second.value) {
                if (it->second.destroy) {
                    it->second.destroy(it->second.value);
                }
                std::free(it->second.value);
            }
            ++it;
        }
        impl_->values.clear();
    } catch (...) {
    }
}

}  // namespace pipeline_v2
}  // namespace taiyin
