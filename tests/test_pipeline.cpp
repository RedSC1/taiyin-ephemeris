#include "taiyin/runtime/pipeline.h"

#include <cstring>
#include <iostream>

namespace {

using namespace taiyin::runtime;

struct TestChart {
    int final_value;
    int mutation_count;

    TestChart()
        : final_value(0), mutation_count(0) {}
};

struct TestScratch {
    int value;
    int typed_result;
    int calls;

    TestScratch()
        : value(0), typed_result(0), calls(0) {}
};

struct AddRequest {
    int lhs;
    int rhs;
};

struct AddResult {
    int value;
};

struct AccumulateStepData {
    int delta;
};

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

void expect_int(int actual, int expected, const char* label, int* failures) {
    if (actual != expected) {
        std::cerr << "FAIL: " << label << ": actual=" << actual
                  << " expected=" << expected << "\n";
        ++(*failures);
    }
}

void expect_size(size_t actual, size_t expected, const char* label, int* failures) {
    if (actual != expected) {
        std::cerr << "FAIL: " << label << ": actual=" << actual
                  << " expected=" << expected << "\n";
        ++(*failures);
    }
}

void expect_cstr(const char* actual, const char* expected, const char* label, int* failures) {
    if ((!actual && expected) || (actual && !expected) || (actual && expected && std::strcmp(actual, expected) != 0)) {
        std::cerr << "FAIL: " << label << ": actual=" << (actual ? actual : "<null>")
                  << " expected=" << (expected ? expected : "<null>") << "\n";
        ++(*failures);
    }
}

bool typed_add(const AddRequest& request, AddResult* out) {
    if (!out) {
        return false;
    }
    out->value = request.lhs + request.rhs;
    return true;
}

bool write_scratch_value_step(PipelineFrame* frame, void* step_data) {
    TestScratch* scratch = static_cast<TestScratch*>(frame ? frame->scratch : 0);
    const int* value = static_cast<const int*>(step_data);
    if (!scratch || !value) {
        return false;
    }
    scratch->value = *value;
    ++scratch->calls;
    return true;
}

bool write_chart_from_scratch_step(PipelineFrame* frame, void* step_data) {
    (void)step_data;
    TestChart* chart = static_cast<TestChart*>(frame ? frame->chart : 0);
    TestScratch* scratch = static_cast<TestScratch*>(frame ? frame->scratch : 0);
    if (!chart || !scratch) {
        return false;
    }
    chart->final_value = scratch->value * 3;
    ++chart->mutation_count;
    return true;
}

bool typed_add_wrapper_step(PipelineFrame* frame, void* step_data) {
    TestScratch* scratch = static_cast<TestScratch*>(frame ? frame->scratch : 0);
    const AddRequest* request = static_cast<const AddRequest*>(step_data);
    if (!scratch || !request) {
        return false;
    }

    AddResult result;
    if (!typed_add(*request, &result)) {
        return false;
    }
    scratch->typed_result = result.value;
    ++scratch->calls;
    return true;
}

bool accumulate_step(PipelineFrame* frame, void* step_data) {
    TestScratch* scratch = static_cast<TestScratch*>(frame ? frame->scratch : 0);
    const AccumulateStepData* data = static_cast<const AccumulateStepData*>(step_data);
    if (!scratch || !data) {
        return false;
    }
    scratch->value += data->delta;
    ++scratch->calls;
    return true;
}

bool copy_scratch_value_to_chart_step(PipelineFrame* frame, void* step_data) {
    (void)step_data;
    TestChart* chart = static_cast<TestChart*>(frame ? frame->chart : 0);
    TestScratch* scratch = static_cast<TestScratch*>(frame ? frame->scratch : 0);
    if (!chart || !scratch) {
        return false;
    }
    chart->final_value = scratch->value;
    ++chart->mutation_count;
    return true;
}

bool fail_step(PipelineFrame* frame, void* step_data) {
    (void)step_data;
    TestScratch* scratch = static_cast<TestScratch*>(frame ? frame->scratch : 0);
    if (scratch) {
        ++scratch->calls;
    }
    return false;
}

bool mutate_chart_step(PipelineFrame* frame, void* step_data) {
    (void)step_data;
    TestChart* chart = static_cast<TestChart*>(frame ? frame->chart : 0);
    if (!chart) {
        return false;
    }
    chart->final_value = 999;
    ++chart->mutation_count;
    return true;
}

PipelineFrame make_frame(TestChart* chart, TestScratch* scratch) {
    PipelineFrame frame;
    frame.chart = chart;
    frame.scratch = scratch;
    frame.user_data = 0;
    return frame;
}

void test_stack_array_runner_executes_wrappers_in_order(int* failures) {
    TestChart chart;
    TestScratch scratch;
    PipelineFrame frame = make_frame(&chart, &scratch);
    int initial_value = 7;
    PipelineStep steps[2] = {
        PipelineStep("write_scratch", write_scratch_value_step, &initial_value),
        PipelineStep("write_chart", write_chart_from_scratch_step, 0),
    };

    PipelineRunResult result;
    expect_true(run_pipeline_steps(steps, 2, &frame, &result), "stack runner succeeds", failures);
    expect_true(result.success, "stack runner result success", failures);
    expect_int(scratch.value, 7, "scratch value written", failures);
    expect_int(chart.final_value, 21, "chart value written from scratch", failures);
    expect_int(scratch.calls, 1, "scratch step called once", failures);
    expect_int(chart.mutation_count, 1, "chart step called once", failures);
}

void test_wrapper_calls_typed_lower_level_function(int* failures) {
    TestChart chart;
    TestScratch scratch;
    PipelineFrame frame = make_frame(&chart, &scratch);
    AddRequest request;
    request.lhs = 11;
    request.rhs = 13;
    PipelineStep step("typed_add", typed_add_wrapper_step, &request);

    expect_true(run_pipeline_steps(&step, 1, &frame, 0), "typed wrapper step succeeds", failures);
    expect_int(scratch.typed_result, 24, "typed helper result stored in scratch", failures);
    expect_int(scratch.calls, 1, "typed wrapper called once", failures);
}

void test_step_data_config_works(int* failures) {
    TestChart chart;
    TestScratch scratch;
    PipelineFrame frame = make_frame(&chart, &scratch);
    AccumulateStepData add_five;
    add_five.delta = 5;
    AccumulateStepData subtract_two;
    subtract_two.delta = -2;
    PipelineStep steps[3] = {
        PipelineStep("add_five", accumulate_step, &add_five),
        PipelineStep("subtract_two", accumulate_step, &subtract_two),
        PipelineStep("copy", copy_scratch_value_to_chart_step, 0),
    };

    expect_true(run_pipeline_steps(steps, 3, &frame, 0), "configured steps succeed", failures);
    expect_int(scratch.value, 3, "configured deltas update scratch", failures);
    expect_int(chart.final_value, 3, "configured result copied to chart", failures);
    expect_int(scratch.calls, 2, "same wrapper used twice", failures);
}

void test_failure_stops_at_failing_step(int* failures) {
    TestChart chart;
    TestScratch scratch;
    PipelineFrame frame = make_frame(&chart, &scratch);
    AccumulateStepData add_one;
    add_one.delta = 1;
    PipelineStep steps[3] = {
        PipelineStep("add_one", accumulate_step, &add_one),
        PipelineStep("fail_here", fail_step, 0),
        PipelineStep("mutate_after_failure", mutate_chart_step, 0),
    };

    PipelineRunResult result;
    expect_false(run_pipeline_steps(steps, 3, &frame, &result), "failing pipeline returns false", failures);
    expect_false(result.success, "failure result success false", failures);
    expect_int(result.failed_step_index, 1, "failing step index", failures);
    expect_cstr(result.failed_step_name, "fail_here", "failing step name", failures);
    expect_int(scratch.value, 1, "previous step ran before failure", failures);
    expect_int(chart.final_value, 0, "later step did not mutate chart", failures);
    expect_int(chart.mutation_count, 0, "later step not called", failures);
}

void test_null_validation(int* failures) {
    TestChart chart;
    TestScratch scratch;
    PipelineFrame frame = make_frame(&chart, &scratch);
    int value = 4;
    PipelineStep good_step("good", write_scratch_value_step, &value);
    PipelineRunResult result;

    expect_false(run_pipeline_steps(&good_step, 1, 0, &result), "null frame fails", failures);
    expect_false(result.success, "null frame result success false", failures);
    expect_int(result.failed_step_index, -1, "null frame failure index", failures);

    PipelineStep steps[2] = {
        PipelineStep("good", write_scratch_value_step, &value),
        PipelineStep("null_run", 0, 0),
    };
    expect_false(run_pipeline_steps(steps, 2, &frame, &result), "null step function fails", failures);
    expect_false(result.success, "null step result success false", failures);
    expect_int(result.failed_step_index, 1, "null step failure index", failures);
    expect_cstr(result.failed_step_name, "null_run", "null step failure name", failures);
}

void test_pipeline_class_stores_and_runs_steps(int* failures) {
    TestChart chart;
    TestScratch scratch;
    PipelineFrame frame = make_frame(&chart, &scratch);
    AccumulateStepData add_two;
    add_two.delta = 2;
    AccumulateStepData add_three;
    add_three.delta = 3;

    Pipeline pipeline;
    expect_true(pipeline.add_step(PipelineStep("add_two", accumulate_step, &add_two)), "add first pipeline step", failures);
    expect_true(pipeline.add_step(PipelineStep("add_three", accumulate_step, &add_three)), "add second pipeline step", failures);
    expect_true(pipeline.add_step(PipelineStep("copy", copy_scratch_value_to_chart_step, 0)), "add copy pipeline step", failures);
    expect_false(pipeline.add_step(PipelineStep("bad", 0, 0)), "reject null pipeline step", failures);
    expect_size(pipeline.step_count(), 3, "pipeline step count", failures);

    PipelineRunResult result;
    expect_true(pipeline.run(&frame, &result), "pipeline class run succeeds", failures);
    expect_true(result.success, "pipeline class result success", failures);
    expect_int(chart.final_value, 5, "pipeline class wrote chart", failures);

    pipeline.clear();
    expect_size(pipeline.step_count(), 0, "pipeline clear resets step count", failures);
    chart.final_value = 123;
    expect_true(pipeline.run(&frame, &result), "empty pipeline run succeeds", failures);
    expect_true(result.success, "empty pipeline result success", failures);
    expect_int(chart.final_value, 123, "empty pipeline does not mutate chart", failures);
}

}  // namespace

int main() {
    int failures = 0;
    test_stack_array_runner_executes_wrappers_in_order(&failures);
    test_wrapper_calls_typed_lower_level_function(&failures);
    test_step_data_config_works(&failures);
    test_failure_stops_at_failing_step(&failures);
    test_null_validation(&failures);
    test_pipeline_class_stores_and_runs_steps(&failures);

    if (failures == 0) {
        std::cout << "test_pipeline: ALL TESTS PASSED\n";
        return 0;
    }
    std::cerr << failures << " pipeline test failure(s)\n";
    return 1;
}
