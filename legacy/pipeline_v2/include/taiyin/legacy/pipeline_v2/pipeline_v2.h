#ifndef TAIYIN_PIPELINE_V2_PIPELINE_V2_H
#define TAIYIN_PIPELINE_V2_PIPELINE_V2_H

#include "taiyin/runtime/handle_types.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace taiyin {
namespace pipeline_v2 {

struct PipelineContext;

struct PipelineStepInvocation {
    runtime::StepId step;
    int32_t subject;
    uint32_t variant;

    PipelineStepInvocation() noexcept
        : step(), subject(0), variant(0) {}
    explicit PipelineStepInvocation(runtime::StepId step_id) noexcept
        : step(step_id), subject(0), variant(0) {}
    PipelineStepInvocation(runtime::StepId step_id, int32_t subject_value, uint32_t variant_value) noexcept
        : step(step_id), subject(subject_value), variant(variant_value) {}
};

class Pipeline {
public:
    Pipeline();
    ~Pipeline();

    Pipeline(const Pipeline&) = delete;
    Pipeline& operator=(const Pipeline&) = delete;

    bool add_step(runtime::StepId step) noexcept;
    bool add_step(runtime::StepId step, int32_t subject, uint32_t variant = 0) noexcept;
    bool run(PipelineContext* ctx) const noexcept;
    runtime::StepId step_at(size_t index) const noexcept;
    int32_t step_subject_at(size_t index) const noexcept;
    uint32_t step_variant_at(size_t index) const noexcept;
    size_t step_count() const noexcept;
    void clear() noexcept;

private:
    std::vector<PipelineStepInvocation> steps_;
};

}  // namespace pipeline_v2
}  // namespace taiyin

#endif  // TAIYIN_PIPELINE_V2_PIPELINE_V2_H
