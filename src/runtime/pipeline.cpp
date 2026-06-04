#include "taiyin/runtime/pipeline.h"

#include <new>
#include <vector>

namespace taiyin {
namespace runtime {

PipelineFrame::PipelineFrame() noexcept
    : chart(0), scratch(0), user_data(0) {}

PipelineStep::PipelineStep() noexcept
    : name(0), run(0), step_data(0) {}

PipelineStep::PipelineStep(
    const char* name_value,
    PipelineStepFn run_value,
    void* step_data_value
) noexcept
    : name(name_value), run(run_value), step_data(step_data_value) {}

PipelineRunResult::PipelineRunResult() noexcept
    : success(true), failed_step_index(-1), failed_step_name(0) {}

namespace {

void reset_result(PipelineRunResult* result) noexcept {
    if (result) {
        result->success = true;
        result->failed_step_index = -1;
        result->failed_step_name = 0;
    }
}

void set_failure(PipelineRunResult* result, int index, const char* name) noexcept {
    if (result) {
        result->success = false;
        result->failed_step_index = index;
        result->failed_step_name = name;
    }
}

}  // namespace

bool run_pipeline_steps(
    const PipelineStep* steps,
    size_t step_count,
    PipelineFrame* frame,
    PipelineRunResult* out_result
) noexcept {
    reset_result(out_result);
    if (!frame) {
        set_failure(out_result, -1, 0);
        return false;
    }
    if (step_count == 0) {
        return true;
    }
    if (!steps) {
        set_failure(out_result, -1, 0);
        return false;
    }

    for (size_t i = 0; i < step_count; ++i) {
        const PipelineStep& step = steps[i];
        if (!step.run) {
            set_failure(out_result, static_cast<int>(i), step.name);
            return false;
        }
        if (!step.run(frame, step.step_data)) {
            set_failure(out_result, static_cast<int>(i), step.name);
            return false;
        }
    }

    return true;
}

struct Pipeline::Impl {
    std::vector<PipelineStep> steps;
};

Pipeline::Pipeline()
    : impl_(new (std::nothrow) Impl()) {}

Pipeline::~Pipeline() {
    delete impl_;
}

bool Pipeline::add_step(const PipelineStep& step) noexcept {
    if (!impl_ || !step.run) {
        return false;
    }
    try {
        impl_->steps.push_back(step);
    } catch (...) {
        return false;
    }
    return true;
}

void Pipeline::clear() noexcept {
    if (impl_) {
        impl_->steps.clear();
    }
}

size_t Pipeline::step_count() const noexcept {
    return impl_ ? impl_->steps.size() : 0;
}

bool Pipeline::run(PipelineFrame* frame, PipelineRunResult* out_result) const noexcept {
    if (!impl_) {
        reset_result(out_result);
        set_failure(out_result, -1, 0);
        return false;
    }
    const PipelineStep* steps = impl_->steps.empty() ? 0 : &impl_->steps[0];
    return run_pipeline_steps(steps, impl_->steps.size(), frame, out_result);
}

}  // namespace runtime
}  // namespace taiyin
