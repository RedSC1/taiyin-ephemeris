#include "taiyin/legacy/pipeline_v2/artifact_store.h"
#include "taiyin/legacy/pipeline_v2/pipeline_context.h"
#include "taiyin/legacy/pipeline_v2/pipeline_v2.h"
#include "taiyin/runtime/runtime_registry.h"

#include <iostream>

namespace {

using namespace taiyin::pipeline_v2;
using namespace taiyin::runtime;

ArtifactTypeId g_int_type;
ArtifactKey g_input_key;
ArtifactKey g_middle_key;
ArtifactKey g_output_key;

struct CountingArtifact {
    int value;
};

int g_copy_count = 0;
int g_destroy_count = 0;

void expect_true(bool value, const char* label, int* failures) {
    if (!value) {
        std::cerr << "FAIL: expected true: " << label << "\n";
        ++(*failures);
    }
}

void expect_false(bool value, const char* label, int* failures) {
    if (value) {
        std::cerr << "FAIL: expected false: " << label << "\n";
        ++(*failures);
    }
}

void expect_i32(int actual, int expected, const char* label, int* failures) {
    if (actual != expected) {
        std::cerr << "FAIL: " << label << ": actual=" << actual << " expected=" << expected << "\n";
        ++(*failures);
    }
}

void expect_u32(uint32_t actual, uint32_t expected, const char* label, int* failures) {
    if (actual != expected) {
        std::cerr << "FAIL: " << label << ": actual=" << actual << " expected=" << expected << "\n";
        ++(*failures);
    }
}

void expect_size(size_t actual, size_t expected, const char* label, int* failures) {
    if (actual != expected) {
        std::cerr << "FAIL: " << label << ": actual=" << actual << " expected=" << expected << "\n";
        ++(*failures);
    }
}

void copy_counting_artifact(void* dst, const void* src) {
    CountingArtifact* out = static_cast<CountingArtifact*>(dst);
    const CountingArtifact* in = static_cast<const CountingArtifact*>(src);
    out->value = in->value;
    ++g_copy_count;
}

void destroy_counting_artifact(void*) {
    ++g_destroy_count;
}

bool step_double_input(PipelineContext* ctx) {
    int input = 0;
    if (!ctx || !ctx->artifacts || !ctx->artifacts->get(g_input_key, &input, g_int_type)) {
        if (ctx && ctx->diagnostics) {
            ctx->diagnostics->set_error("step_double_input missing input");
        }
        return false;
    }
    const int output = input * 2;
    return ctx->artifacts->set(g_middle_key, &output, g_int_type);
}

bool step_add_three(PipelineContext* ctx) {
    int input = 0;
    if (!ctx || !ctx->artifacts || !ctx->artifacts->get(g_middle_key, &input, g_int_type)) {
        if (ctx && ctx->diagnostics) {
            ctx->diagnostics->set_error("step_add_three missing input");
        }
        return false;
    }
    const int output = input + 3;
    return ctx->artifacts->set(g_output_key, &output, g_int_type);
}

bool step_always_fails(PipelineContext* ctx) {
    if (ctx && ctx->diagnostics) {
        ctx->diagnostics->set_error("intentional step failure");
    }
    return false;
}

bool step_increment_until_three(PipelineContext* ctx) {
    int value = 0;
    if (!ctx || !ctx->read_artifacts() || !ctx->write_artifacts()
        || !ctx->read_artifacts()->get(g_input_key, &value, g_int_type)) {
        if (ctx && ctx->diagnostics) {
            ctx->diagnostics->set_error("step_increment_until_three missing input");
        }
        return false;
    }

    ++value;
    if (!ctx->write_artifacts()->set(g_input_key, &value, g_int_type)) {
        return false;
    }
    if (value < 3) {
        ctx->jump_to(ctx->current_step_index);
    }
    return true;
}

bool step_copy_iteration_output(PipelineContext* ctx) {
    int value = 0;
    if (!ctx || !ctx->read_artifacts() || !ctx->write_artifacts()
        || !ctx->read_artifacts()->get(g_input_key, &value, g_int_type)) {
        if (ctx && ctx->diagnostics) {
            ctx->diagnostics->set_error("step_copy_iteration_output missing input");
        }
        return false;
    }
    return ctx->write_artifacts()->set(g_output_key, &value, g_int_type);
}

bool step_jump_forever(PipelineContext* ctx) {
    if (ctx) {
        ctx->jump_to(ctx->current_step_index);
    }
    return true;
}

bool step_read_input_write_output(PipelineContext* ctx) {
    int value = 0;
    if (!ctx || !ctx->read_artifacts() || !ctx->write_artifacts()
        || !ctx->read_artifacts()->get(g_input_key, &value, g_int_type)) {
        return false;
    }
    const int output = value + 100;
    return ctx->write_artifacts()->set(g_output_key, &output, g_int_type);
}

void test_artifact_store_basic(int* failures) {
    RuntimeRegistry registry;
    ArtifactTypeId int_type = registry.register_artifact_type("test.int", sizeof(int), alignof(int));
    ArtifactTypeId other_type = registry.register_artifact_type("test.other", sizeof(int), alignof(int));
    ArtifactKey key(int_type, 42, 1);
    ArtifactKey other_key(other_type, 42, 1);
    ArtifactStore store(&registry);

    int value = 11;
    expect_true(store.set(key, &value, int_type), "set int artifact", failures);
    expect_true(store.contains(key), "contains int artifact", failures);
    expect_size(store.size(), 1, "artifact store size after set", failures);

    int out = 0;
    expect_true(store.get(key, &out, int_type), "get int artifact", failures);
    expect_i32(out, 11, "int artifact value", failures);

    expect_false(store.get(key, &out, other_type), "get rejects mismatched expected type", failures);
    expect_false(store.set(other_key, &value, int_type), "set rejects mismatched key type", failures);

    value = 23;
    expect_true(store.set(key, &value, int_type), "overwrite int artifact", failures);
    expect_size(store.size(), 1, "artifact store size after overwrite", failures);
    expect_true(store.get(key, &out, int_type), "get overwritten artifact", failures);
    expect_i32(out, 23, "overwritten artifact value", failures);

    expect_true(store.erase(key), "erase artifact", failures);
    expect_false(store.contains(key), "contains false after erase", failures);
    expect_size(store.size(), 0, "artifact store size after erase", failures);
    expect_false(store.erase(key), "erase missing artifact fails", failures);
}

void test_artifact_store_custom_lifecycle(int* failures) {
    RuntimeRegistry registry;
    ArtifactTypeId counting_type = registry.register_artifact_type(
        "test.counting",
        sizeof(CountingArtifact),
        alignof(CountingArtifact),
        &copy_counting_artifact,
        &destroy_counting_artifact
    );
    ArtifactKey key(counting_type, 7, 0);
    ArtifactStore store(&registry);
    g_copy_count = 0;
    g_destroy_count = 0;

    CountingArtifact value;
    value.value = 9;
    expect_true(store.set(key, &value, counting_type), "set custom artifact", failures);
    expect_i32(g_copy_count, 1, "custom copy after set", failures);
    expect_i32(g_destroy_count, 0, "custom destroy not called after set", failures);

    CountingArtifact out;
    out.value = 0;
    expect_true(store.get(key, &out, counting_type), "get custom artifact", failures);
    expect_i32(out.value, 9, "custom artifact value", failures);
    expect_i32(g_copy_count, 2, "custom copy after get", failures);

    store.clear();
    expect_i32(g_destroy_count, 1, "custom destroy after clear", failures);
    expect_size(store.size(), 0, "custom store clear size", failures);
}

void test_pipeline_minimal_loop(int* failures) {
    RuntimeRegistry registry;
    g_int_type = registry.register_artifact_type("test.pipeline.int", sizeof(int), alignof(int));
    g_input_key = ArtifactKey(g_int_type, 1, 0);
    g_middle_key = ArtifactKey(g_int_type, 2, 0);
    g_output_key = ArtifactKey(g_int_type, 3, 0);

    StepId double_step = registry.register_step("test.double_input", &step_double_input);
    StepId add_step = registry.register_step("test.add_three", &step_add_three);

    ArtifactStore artifacts(&registry);
    PipelineDiagnostics diagnostics;
    PipelineContext ctx;
    ctx.runtime = &registry;
    ctx.artifacts = &artifacts;
    ctx.diagnostics = &diagnostics;

    int input = 7;
    expect_true(artifacts.set(g_input_key, &input, g_int_type), "set pipeline input", failures);

    Pipeline pipeline;
    expect_true(pipeline.add_step(double_step), "add first pipeline step", failures);
    expect_true(pipeline.add_step(add_step), "add second pipeline step", failures);
    expect_size(pipeline.step_count(), 2, "pipeline step count", failures);
    expect_u32(pipeline.step_at(0).value, double_step.value, "pipeline first step id", failures);

    expect_true(pipeline.run(&ctx), "pipeline v2 minimal loop runs", failures);
    expect_true(diagnostics.last_error == 0, "pipeline diagnostics stays clear", failures);

    int output = 0;
    expect_true(artifacts.get(g_output_key, &output, g_int_type), "get pipeline output", failures);
    expect_i32(output, 17, "pipeline output value", failures);
}

void test_pipeline_failures(int* failures) {
    RuntimeRegistry registry;
    ArtifactStore artifacts(&registry);
    PipelineDiagnostics diagnostics;
    PipelineContext ctx;
    ctx.runtime = &registry;
    ctx.artifacts = &artifacts;
    ctx.diagnostics = &diagnostics;

    Pipeline pipeline;
    expect_false(pipeline.add_step(StepId()), "invalid step rejected", failures);
    expect_true(pipeline.add_step(StepId(99)), "unregistered but valid step id accepted into plan", failures);
    expect_false(pipeline.run(&ctx), "missing step descriptor fails run", failures);
    expect_true(diagnostics.last_error != 0, "missing step sets diagnostic", failures);
    expect_u32(diagnostics.failed_step.value, 99, "missing step diagnostic id", failures);

    pipeline.clear();
    StepId fail_step = registry.register_step("test.always_fails", &step_always_fails);
    expect_true(pipeline.add_step(fail_step), "add failing step", failures);
    expect_false(pipeline.run(&ctx), "failing step fails run", failures);
    expect_true(diagnostics.last_error != 0, "failing step sets diagnostic", failures);
    expect_u32(diagnostics.failed_step.value, fail_step.value, "failing step diagnostic id", failures);

    ctx.runtime = 0;
    expect_false(pipeline.run(&ctx), "missing runtime fails run", failures);
    expect_true(diagnostics.last_error != 0, "missing runtime sets diagnostic", failures);
}

void test_pipeline_iteration_jump(int* failures) {
    RuntimeRegistry registry;
    g_int_type = registry.register_artifact_type("test.pipeline.iteration_int", sizeof(int), alignof(int));
    g_input_key = ArtifactKey(g_int_type, 1, 0);
    g_output_key = ArtifactKey(g_int_type, 2, 0);

    StepId iterate_step = registry.register_step("test.increment_until_three", &step_increment_until_three);
    StepId copy_step = registry.register_step("test.copy_iteration_output", &step_copy_iteration_output);

    ArtifactStore artifacts(&registry);
    PipelineDiagnostics diagnostics;
    PipelineContext ctx;
    ctx.runtime = &registry;
    ctx.artifacts = &artifacts;
    ctx.diagnostics = &diagnostics;

    int initial = 0;
    expect_true(artifacts.set(g_input_key, &initial, g_int_type), "set iteration input", failures);

    Pipeline pipeline;
    expect_true(pipeline.add_step(iterate_step), "add iterate step", failures);
    expect_true(pipeline.add_step(copy_step), "add copy step", failures);
    expect_true(pipeline.run(&ctx), "pipeline supports backward jump iteration", failures);
    expect_size(ctx.step_visit_count, 4, "iteration visits three loops plus copy", failures);

    int output = 0;
    expect_true(artifacts.get(g_output_key, &output, g_int_type), "get iteration output", failures);
    expect_i32(output, 3, "iteration output value", failures);
}

void test_pipeline_iteration_limit(int* failures) {
    RuntimeRegistry registry;
    StepId forever_step = registry.register_step("test.jump_forever", &step_jump_forever);

    ArtifactStore artifacts(&registry);
    PipelineDiagnostics diagnostics;
    PipelineContext ctx;
    ctx.runtime = &registry;
    ctx.artifacts = &artifacts;
    ctx.diagnostics = &diagnostics;
    ctx.max_step_visits = 3;

    Pipeline pipeline;
    expect_true(pipeline.add_step(forever_step), "add forever step", failures);
    expect_false(pipeline.run(&ctx), "pipeline stops infinite jump at max visits", failures);
    expect_true(diagnostics.last_error != 0, "max visits sets diagnostic", failures);
    expect_size(ctx.step_visit_count, 3, "max visits count", failures);
}

void test_pipeline_separate_input_output_artifacts(int* failures) {
    RuntimeRegistry registry;
    g_int_type = registry.register_artifact_type("test.pipeline.in_out_int", sizeof(int), alignof(int));
    g_input_key = ArtifactKey(g_int_type, 1, 0);
    g_output_key = ArtifactKey(g_int_type, 2, 0);
    StepId step = registry.register_step("test.read_input_write_output", &step_read_input_write_output);

    ArtifactStore input_artifacts(&registry);
    ArtifactStore output_artifacts(&registry);
    PipelineDiagnostics diagnostics;
    PipelineContext ctx;
    ctx.runtime = &registry;
    ctx.input_artifacts = &input_artifacts;
    ctx.output_artifacts = &output_artifacts;
    ctx.diagnostics = &diagnostics;

    int input = 5;
    expect_true(input_artifacts.set(g_input_key, &input, g_int_type), "set separate input artifact", failures);

    Pipeline pipeline;
    expect_true(pipeline.add_step(step), "add in/out step", failures);
    expect_true(pipeline.run(&ctx), "pipeline runs with separate input/output stores", failures);
    expect_false(input_artifacts.contains(g_output_key), "output not written to input store", failures);
    expect_true(output_artifacts.contains(g_output_key), "output written to output store", failures);

    int output = 0;
    expect_true(output_artifacts.get(g_output_key, &output, g_int_type), "get separate output artifact", failures);
    expect_i32(output, 105, "separate output value", failures);
}

}  // namespace

int main() {
    int failures = 0;
    test_artifact_store_basic(&failures);
    test_artifact_store_custom_lifecycle(&failures);
    test_pipeline_minimal_loop(&failures);
    test_pipeline_failures(&failures);
    test_pipeline_iteration_jump(&failures);
    test_pipeline_iteration_limit(&failures);
    test_pipeline_separate_input_output_artifacts(&failures);

    if (failures == 0) {
        std::cout << "test_legacy_pipeline_v2: ALL TESTS PASSED\n";
        return 0;
    }
    std::cerr << failures << " test_legacy_pipeline_v2 failure(s)\n";
    return 1;
}
