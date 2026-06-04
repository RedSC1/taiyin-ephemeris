#include "taiyin/legacy/pipeline_v2/pipeline_v2.h"

#include "taiyin/legacy/pipeline_v2/pipeline_context.h"
#include "taiyin/runtime/runtime_registry.h"

namespace taiyin {
namespace pipeline_v2 {
namespace {

void set_error(PipelineContext* ctx, const char* message, runtime::StepId step = runtime::StepId()) noexcept {
    if (ctx && ctx->diagnostics) {
        ctx->diagnostics->set_error(message, step);
    }
}

bool has_artifact_stores(const PipelineContext* ctx) noexcept {
    return ctx && ctx->read_artifacts() && ctx->write_artifacts();
}

void begin_run(PipelineContext* ctx) noexcept {
    ctx->current_step_index = 0;
    ctx->current_step_subject = 0;
    ctx->current_step_variant = 0;
    ctx->next_step_index = 0;
    ctx->has_next_step_index = false;
    ctx->stop_requested = false;
    ctx->stop_success = false;
    ctx->step_visit_count = 0;
}

void clear_step_request(PipelineContext* ctx) noexcept {
    ctx->next_step_index = 0;
    ctx->has_next_step_index = false;
}

}  // namespace

Pipeline::Pipeline()
    : steps_() {}

Pipeline::~Pipeline() {}

bool Pipeline::add_step(runtime::StepId step) noexcept {
    return add_step(step, 0, 0);
}

bool Pipeline::add_step(runtime::StepId step, int32_t subject, uint32_t variant) noexcept {
    if (!step.is_valid()) {
        return false;
    }
    try {
        steps_.push_back(PipelineStepInvocation(step, subject, variant));
        return true;
    } catch (...) {
        return false;
    }
}

bool Pipeline::run(PipelineContext* ctx) const noexcept {
    if (!ctx) {
        return false;
    }
    if (ctx->diagnostics) {
        ctx->diagnostics->clear();
    }
    if (!ctx->runtime) {
        set_error(ctx, "pipeline_v2 missing runtime registry");
        return false;
    }
    if (!has_artifact_stores(ctx)) {
        set_error(ctx, "pipeline_v2 missing artifact store");
        return false;
    }

    begin_run(ctx);

    size_t i = 0;
    while (i < steps_.size()) {
        const PipelineStepInvocation& invocation = steps_[i];
        const runtime::StepId step_id = invocation.step;
        if (ctx->max_step_visits > 0 && ctx->step_visit_count >= ctx->max_step_visits) {
            set_error(ctx, "pipeline_v2 exceeded max step visits", step_id);
            return false;
        }
        ++ctx->step_visit_count;
        clear_step_request(ctx);
        ctx->current_step_index = i;
        ctx->current_step_subject = invocation.subject;
        ctx->current_step_variant = invocation.variant;

        const runtime::PipelineStepDescriptor* descriptor = ctx->runtime->step(step_id);
        if (!descriptor || !descriptor->execute) {
            set_error(ctx, "pipeline_v2 missing step descriptor", step_id);
            return false;
        }
        if (!descriptor->execute(ctx)) {
            if (ctx->diagnostics && !ctx->diagnostics->last_error) {
                ctx->diagnostics->set_error("pipeline_v2 step failed", step_id);
            } else if (ctx->diagnostics && !ctx->diagnostics->failed_step.is_valid()) {
                ctx->diagnostics->failed_step = step_id;
            }
            return false;
        }

        if (ctx->stop_requested) {
            if (!ctx->stop_success && ctx->diagnostics && !ctx->diagnostics->last_error) {
                ctx->diagnostics->set_error("pipeline_v2 stop requested failure", step_id);
            }
            return ctx->stop_success;
        }

        if (ctx->has_next_step_index) {
            if (ctx->next_step_index >= steps_.size()) {
                set_error(ctx, "pipeline_v2 jump target out of range", step_id);
                return false;
            }
            i = ctx->next_step_index;
        } else {
            ++i;
        }
    }
    return true;
}

runtime::StepId Pipeline::step_at(size_t index) const noexcept {
    if (index >= steps_.size()) {
        return runtime::StepId();
    }
    return steps_[index].step;
}

int32_t Pipeline::step_subject_at(size_t index) const noexcept {
    if (index >= steps_.size()) {
        return 0;
    }
    return steps_[index].subject;
}

uint32_t Pipeline::step_variant_at(size_t index) const noexcept {
    if (index >= steps_.size()) {
        return 0;
    }
    return steps_[index].variant;
}

size_t Pipeline::step_count() const noexcept {
    return steps_.size();
}

void Pipeline::clear() noexcept {
    try {
        steps_.clear();
    } catch (...) {
    }
}

}  // namespace pipeline_v2
}  // namespace taiyin
