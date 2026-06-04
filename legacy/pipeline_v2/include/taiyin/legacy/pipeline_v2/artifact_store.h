#ifndef TAIYIN_PIPELINE_V2_ARTIFACT_STORE_H
#define TAIYIN_PIPELINE_V2_ARTIFACT_STORE_H

#include "taiyin/runtime/handle_types.h"
#include "taiyin/runtime/runtime_registry.h"

#include <cstddef>

namespace taiyin {
namespace pipeline_v2 {

class ArtifactStore {
public:
    explicit ArtifactStore(const runtime::RuntimeRegistry* registry = 0);
    ~ArtifactStore();

    ArtifactStore(const ArtifactStore&) = delete;
    ArtifactStore& operator=(const ArtifactStore&) = delete;

    void set_runtime_registry(const runtime::RuntimeRegistry* registry) noexcept;
    const runtime::RuntimeRegistry* runtime_registry() const noexcept;

    bool set(
        const runtime::ArtifactKey& key,
        const void* value,
        runtime::ArtifactTypeId expected_type
    ) noexcept;
    bool get(
        const runtime::ArtifactKey& key,
        void* out,
        runtime::ArtifactTypeId expected_type
    ) const noexcept;
    bool contains(const runtime::ArtifactKey& key) const noexcept;
    bool erase(const runtime::ArtifactKey& key) noexcept;
    size_t size() const noexcept;
    void clear() noexcept;

private:
    struct Impl;
    Impl* impl_;
};

}  // namespace pipeline_v2
}  // namespace taiyin

#endif  // TAIYIN_PIPELINE_V2_ARTIFACT_STORE_H
