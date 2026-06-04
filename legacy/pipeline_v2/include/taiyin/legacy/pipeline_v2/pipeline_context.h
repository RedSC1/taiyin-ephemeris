#ifndef TAIYIN_PIPELINE_V2_PIPELINE_CONTEXT_H
#define TAIYIN_PIPELINE_V2_PIPELINE_CONTEXT_H

#include "taiyin/runtime/handle_types.h"

#include <cstddef>
#include <cstdint>

namespace taiyin {
namespace runtime {
class RuntimeRegistry;
class TaiyinRuntime;
}
namespace pipeline_v2 {

class ArtifactStore;

struct PipelineDiagnostics {
    const char* last_error;
    runtime::StepId failed_step;

    PipelineDiagnostics() noexcept
        : last_error(0), failed_step() {}

    void clear() noexcept {
        last_error = 0;
        failed_step = runtime::StepId();
    }

    void set_error(const char* message, runtime::StepId step = runtime::StepId()) noexcept {
        last_error = message;
        failed_step = step;
    }
};

struct PipelineContext {
    runtime::RuntimeRegistry* runtime;
    runtime::TaiyinRuntime* taiyin_runtime;
    ArtifactStore* artifacts;
    ArtifactStore* input_artifacts;
    ArtifactStore* output_artifacts;
    PipelineDiagnostics* diagnostics;

    size_t current_step_index;
    int32_t current_step_subject;
    uint32_t current_step_variant;
    size_t next_step_index;
    bool has_next_step_index;
    bool stop_requested;
    bool stop_success;
    size_t max_step_visits;
    size_t step_visit_count;

    PipelineContext() noexcept
        : runtime(0),
          taiyin_runtime(0),
          artifacts(0),
          input_artifacts(0),
          output_artifacts(0),
          diagnostics(0),
          current_step_index(0),
          current_step_subject(0),
          current_step_variant(0),
          next_step_index(0),
          has_next_step_index(false),
          stop_requested(false),
          stop_success(false),
          max_step_visits(10000),
          step_visit_count(0) {}

    ArtifactStore* read_artifacts() const noexcept {
        return input_artifacts ? input_artifacts : artifacts;
    }

    ArtifactStore* write_artifacts() const noexcept {
        return output_artifacts ? output_artifacts : artifacts;
    }

    void jump_to(size_t index) noexcept {
        next_step_index = index;
        has_next_step_index = true;
    }

    void request_stop(bool success) noexcept {
        stop_requested = true;
        stop_success = success;
    }
};

}  // namespace pipeline_v2
}  // namespace taiyin

#endif  // TAIYIN_PIPELINE_V2_PIPELINE_CONTEXT_H
