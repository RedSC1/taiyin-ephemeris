#ifndef TAIYIN_RUNTIME_PIPELINE_H
#define TAIYIN_RUNTIME_PIPELINE_H

#include <cstddef>

namespace taiyin {
namespace runtime {

struct PipelineFrame {
    void* chart;
    void* scratch;
    void* user_data;

    PipelineFrame() noexcept;
};

typedef bool (*PipelineStepFn)(PipelineFrame* frame, void* step_data);

struct PipelineStep {
    const char* name;
    PipelineStepFn run;
    void* step_data;

    PipelineStep() noexcept;
    PipelineStep(const char* name_value, PipelineStepFn run_value, void* step_data_value) noexcept;
};

struct PipelineRunResult {
    bool success;
    int failed_step_index;
    const char* failed_step_name;

    PipelineRunResult() noexcept;
};

bool run_pipeline_steps(
    const PipelineStep* steps,
    size_t step_count,
    PipelineFrame* frame,
    PipelineRunResult* out_result
) noexcept;

class Pipeline {
public:
    Pipeline();
    ~Pipeline();

    Pipeline(const Pipeline&) = delete;
    Pipeline& operator=(const Pipeline&) = delete;

    bool add_step(const PipelineStep& step) noexcept;
    void clear() noexcept;
    size_t step_count() const noexcept;
    bool run(PipelineFrame* frame, PipelineRunResult* out_result) const noexcept;

private:
    struct Impl;
    Impl* impl_;
};

}  // namespace runtime
}  // namespace taiyin

#endif  // TAIYIN_RUNTIME_PIPELINE_H
