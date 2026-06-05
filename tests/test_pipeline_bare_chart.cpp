#include "taiyin/angle.h"
#include "taiyin/body_id.h"
#include "taiyin/time.h"
#include "taiyin/internal/custom_ephemeris_source_registry.h"
#include "taiyin/runtime/pipeline.h"
#include "taiyin/runtime/taiyin_runtime.h"

#include <cmath>
#include <cstddef>
#include <iostream>

namespace {

using namespace taiyin;
using namespace taiyin::internal;
using namespace taiyin::runtime;

const double JD0 = taiyin::JD_J2000;
const size_t MAX_BODIES = 16;

const int METHOD_MOCK_SPK = 91001;
const int METHOD_MOCK_VSOP = 91002;
const int METHOD_MOCK_KEPLER = 91003;

struct MockEphemerisData {
    double x;
    double y;
    double z;
};

struct BodySpec {
    const char* name;
    int target_id;
    int expected_method_id;
    double expected_longitude_rad;
};

struct BareChartBody {
    const char* name;
    int target_id;
    int method_id;
    double longitude_rad;
    bool cache_hit;
};

struct BareChart {
    BareChartBody bodies[MAX_BODIES];
    size_t body_count;

    BareChart()
        : bodies(), body_count(0) {}
};

struct BareChartScratch {
    EphemerisResult ephemeris[MAX_BODIES];
    double longitudes[MAX_BODIES];
    bool cache_hits[MAX_BODIES];
    size_t body_count;

    BareChartScratch()
        : ephemeris(), longitudes(), cache_hits(), body_count(0) {}
};

struct EvalBodiesStepData {
    const BodySpec* bodies;
    size_t body_count;
    double jd_tdb;
};

struct ProjectLongitudesStepData {
    size_t body_count;
};

struct WriteChartStepData {
    const BodySpec* bodies;
    size_t body_count;
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

void expect_near(double actual, double expected, double tolerance, const char* label, int* failures) {
    if (std::fabs(actual - expected) > tolerance) {
        std::cerr << "FAIL: " << label << ": actual=" << actual
                  << " expected=" << expected << " tolerance=" << tolerance << "\n";
        ++(*failures);
    }
}

MockEphemerisData make_mock_data(int target_id, int method_id) {
    const double angle = taiyin::normalize_radians(
        0.013 * static_cast<double>(target_id % 360)
        + 0.00001 * static_cast<double>(method_id % 1000)
    );
    const double radius = 0.5
        + 0.01 * static_cast<double>(target_id % 17)
        + 0.001 * static_cast<double>(method_id % 13);

    MockEphemerisData data;
    data.x = radius * std::cos(angle);
    data.y = radius * std::sin(angle);
    data.z = 0.001 * static_cast<double>((target_id + method_id) % 11);
    return data;
}

bool mock_position(double jd_tdb, const void* data, Vector3* out) {
    (void)jd_tdb;
    const MockEphemerisData* mock = static_cast<const MockEphemerisData*>(data);
    if (!mock || !out) {
        return false;
    }
    out->x = mock->x;
    out->y = mock->y;
    out->z = mock->z;
    return true;
}

bool mock_velocity(double jd_tdb, const void* data, Vector3* out) {
    (void)jd_tdb;
    (void)data;
    if (!out) {
        return false;
    }
    out->x = 0.0;
    out->y = 0.0;
    out->z = 0.0;
    return true;
}

bool mock_acceleration(double jd_tdb, const void* data, Vector3* out) {
    (void)jd_tdb;
    (void)data;
    if (!out) {
        return false;
    }
    out->x = 0.0;
    out->y = 0.0;
    out->z = 0.0;
    return true;
}

bool mock_clone(const void* source_data, size_t source_bytes, void** out_data, size_t* out_bytes) {
    if (!source_data || !out_data || !out_bytes || source_bytes != sizeof(MockEphemerisData)) {
        return false;
    }

    MockEphemerisData* clone = new MockEphemerisData(*static_cast<const MockEphemerisData*>(source_data));
    *out_data = clone;
    *out_bytes = sizeof(MockEphemerisData);
    return true;
}

void mock_destroy(void* data) {
    delete static_cast<MockEphemerisData*>(data);
}

bool add_mock_descriptor(int target_id, int method_id) {
    const MockEphemerisData data = make_mock_data(target_id, method_id);

    CustomEphemerisSourceDefinition definition;
    definition.target_id = target_id;
    definition.center_id = 0;
    definition.method_id = method_id;
    definition.frame = EphemerisFrame::IcrfJ2000Equatorial;
    definition.jd_tdb_start = JD0 - 36525.0;
    definition.jd_tdb_end = JD0 + 36525.0;
    definition.data = &data;
    definition.bytes = sizeof(data);
    definition.position = mock_position;
    definition.velocity = mock_velocity;
    definition.acceleration = mock_acceleration;
    definition.clone = mock_clone;
    definition.destroy = mock_destroy;

    uint64_t source_id = 0;
    EphemerisBlockDescriptor descriptor;
    return register_custom_ephemeris_source(definition, &source_id)
        && make_custom_ephemeris_descriptor(source_id, &descriptor)
        && add_global_ephemeris_descriptor(descriptor);
}

double expected_longitude_for(int target_id, int method_id) {
    const MockEphemerisData data = make_mock_data(target_id, method_id);
    return taiyin::normalize_radians(std::atan2(data.y, data.x));
}

bool eval_bodies_step(PipelineFrame* frame, void* step_data) {
    BareChartScratch* scratch = static_cast<BareChartScratch*>(frame ? frame->scratch : 0);
    const EvalBodiesStepData* data = static_cast<const EvalBodiesStepData*>(step_data);
    if (!scratch || !data || !data->bodies || data->body_count > MAX_BODIES) {
        return false;
    }

    scratch->body_count = data->body_count;
    for (size_t i = 0; i < data->body_count; ++i) {
        EphemerisRequest request;
        request.target_id = data->bodies[i].target_id;
        request.center_id = 0;
        request.frame = EphemerisFrame::IcrfJ2000Equatorial;
        request.jd_tdb = data->jd_tdb;
        if (!eval_global_ephemeris_state(request, &scratch->ephemeris[i])) {
            return false;
        }
        scratch->cache_hits[i] = scratch->ephemeris[i].cache_hit;
    }
    return true;
}

bool project_longitudes_step(PipelineFrame* frame, void* step_data) {
    BareChartScratch* scratch = static_cast<BareChartScratch*>(frame ? frame->scratch : 0);
    const ProjectLongitudesStepData* data = static_cast<const ProjectLongitudesStepData*>(step_data);
    if (!scratch || !data || data->body_count > scratch->body_count || data->body_count > MAX_BODIES) {
        return false;
    }

    for (size_t i = 0; i < data->body_count; ++i) {
        const Vector3& position = scratch->ephemeris[i].state.position_au;
        scratch->longitudes[i] = taiyin::normalize_radians(std::atan2(position.y, position.x));
    }
    return true;
}

bool write_chart_step(PipelineFrame* frame, void* step_data) {
    BareChart* chart = static_cast<BareChart*>(frame ? frame->chart : 0);
    BareChartScratch* scratch = static_cast<BareChartScratch*>(frame ? frame->scratch : 0);
    const WriteChartStepData* data = static_cast<const WriteChartStepData*>(step_data);
    if (!chart || !scratch || !data || !data->bodies || data->body_count > scratch->body_count || data->body_count > MAX_BODIES) {
        return false;
    }

    chart->body_count = data->body_count;
    for (size_t i = 0; i < data->body_count; ++i) {
        chart->bodies[i].name = data->bodies[i].name;
        chart->bodies[i].target_id = data->bodies[i].target_id;
        chart->bodies[i].method_id = scratch->ephemeris[i].descriptor.method_id;
        chart->bodies[i].longitude_rad = scratch->longitudes[i];
        chart->bodies[i].cache_hit = scratch->cache_hits[i];
    }
    return true;
}

void reset_global_runtime_for_test() {
    EphemerisRuntimeConfig config;
    initialize_global_ephemeris_runtime(config);
}

void test_pipeline_builds_bare_chart_with_multiple_methods(int* failures) {
    clear_custom_ephemeris_sources();
    reset_global_runtime_for_test();
    expect_true(set_global_ephemeris_method_priority(METHOD_MOCK_SPK, 300), "set SPK mock priority", failures);
    expect_true(set_global_ephemeris_method_priority(METHOD_MOCK_VSOP, 200), "set VSOP mock priority", failures);
    expect_true(set_global_ephemeris_method_priority(METHOD_MOCK_KEPLER, 100), "set Kepler mock priority", failures);
    expect_true(set_global_ephemeris_target_method_priority(TAIYIN_BODY_CERES, METHOD_MOCK_KEPLER, 500),
                "target-specific Ceres prefers Kepler", failures);

    const int spk_targets[] = { TAIYIN_BODY_SUN, TAIYIN_BODY_MOON, TAIYIN_BODY_PLUTO_BARYCENTER, TAIYIN_BODY_CERES };
    for (size_t i = 0; i < sizeof(spk_targets) / sizeof(spk_targets[0]); ++i) {
        expect_true(add_mock_descriptor(spk_targets[i], METHOD_MOCK_SPK), "add SPK-like descriptor", failures);
    }

    const int vsop_targets[] = {
        TAIYIN_BODY_MERCURY_BARYCENTER, TAIYIN_BODY_VENUS_BARYCENTER, TAIYIN_BODY_MARS_BARYCENTER, TAIYIN_BODY_JUPITER_BARYCENTER,
        TAIYIN_BODY_SATURN_BARYCENTER, TAIYIN_BODY_URANUS_BARYCENTER, TAIYIN_BODY_NEPTUNE_BARYCENTER
    };
    for (size_t i = 0; i < sizeof(vsop_targets) / sizeof(vsop_targets[0]); ++i) {
        expect_true(add_mock_descriptor(vsop_targets[i], METHOD_MOCK_VSOP), "add VSOP-like descriptor", failures);
    }

    const int kepler_targets[] = {
        TAIYIN_BODY_MERCURY_BARYCENTER, TAIYIN_BODY_CERES, TAIYIN_BODY_PALLAS, TAIYIN_BODY_JUNO,
        TAIYIN_BODY_VESTA, TAIYIN_BODY_CHIRON
    };
    for (size_t i = 0; i < sizeof(kepler_targets) / sizeof(kepler_targets[0]); ++i) {
        expect_true(add_mock_descriptor(kepler_targets[i], METHOD_MOCK_KEPLER), "add Kepler-like descriptor", failures);
    }

    BodySpec bodies[] = {
        { "Sun", TAIYIN_BODY_SUN, METHOD_MOCK_SPK, expected_longitude_for(TAIYIN_BODY_SUN, METHOD_MOCK_SPK) },
        { "Moon", TAIYIN_BODY_MOON, METHOD_MOCK_SPK, expected_longitude_for(TAIYIN_BODY_MOON, METHOD_MOCK_SPK) },
        { "Mercury", TAIYIN_BODY_MERCURY_BARYCENTER, METHOD_MOCK_VSOP, expected_longitude_for(TAIYIN_BODY_MERCURY_BARYCENTER, METHOD_MOCK_VSOP) },
        { "Venus", TAIYIN_BODY_VENUS_BARYCENTER, METHOD_MOCK_VSOP, expected_longitude_for(TAIYIN_BODY_VENUS_BARYCENTER, METHOD_MOCK_VSOP) },
        { "Mars", TAIYIN_BODY_MARS_BARYCENTER, METHOD_MOCK_VSOP, expected_longitude_for(TAIYIN_BODY_MARS_BARYCENTER, METHOD_MOCK_VSOP) },
        { "Jupiter", TAIYIN_BODY_JUPITER_BARYCENTER, METHOD_MOCK_VSOP, expected_longitude_for(TAIYIN_BODY_JUPITER_BARYCENTER, METHOD_MOCK_VSOP) },
        { "Saturn", TAIYIN_BODY_SATURN_BARYCENTER, METHOD_MOCK_VSOP, expected_longitude_for(TAIYIN_BODY_SATURN_BARYCENTER, METHOD_MOCK_VSOP) },
        { "Uranus", TAIYIN_BODY_URANUS_BARYCENTER, METHOD_MOCK_VSOP, expected_longitude_for(TAIYIN_BODY_URANUS_BARYCENTER, METHOD_MOCK_VSOP) },
        { "Neptune", TAIYIN_BODY_NEPTUNE_BARYCENTER, METHOD_MOCK_VSOP, expected_longitude_for(TAIYIN_BODY_NEPTUNE_BARYCENTER, METHOD_MOCK_VSOP) },
        { "Pluto", TAIYIN_BODY_PLUTO_BARYCENTER, METHOD_MOCK_SPK, expected_longitude_for(TAIYIN_BODY_PLUTO_BARYCENTER, METHOD_MOCK_SPK) },
        { "Ceres", TAIYIN_BODY_CERES, METHOD_MOCK_KEPLER, expected_longitude_for(TAIYIN_BODY_CERES, METHOD_MOCK_KEPLER) },
        { "Pallas", TAIYIN_BODY_PALLAS, METHOD_MOCK_KEPLER, expected_longitude_for(TAIYIN_BODY_PALLAS, METHOD_MOCK_KEPLER) },
        { "Juno", TAIYIN_BODY_JUNO, METHOD_MOCK_KEPLER, expected_longitude_for(TAIYIN_BODY_JUNO, METHOD_MOCK_KEPLER) },
        { "Vesta", TAIYIN_BODY_VESTA, METHOD_MOCK_KEPLER, expected_longitude_for(TAIYIN_BODY_VESTA, METHOD_MOCK_KEPLER) },
        { "Chiron", TAIYIN_BODY_CHIRON, METHOD_MOCK_KEPLER, expected_longitude_for(TAIYIN_BODY_CHIRON, METHOD_MOCK_KEPLER) },
    };
    const size_t body_count = sizeof(bodies) / sizeof(bodies[0]);

    EvalBodiesStepData eval_data;
    eval_data.bodies = bodies;
    eval_data.body_count = body_count;
    eval_data.jd_tdb = JD0;

    ProjectLongitudesStepData project_data;
    project_data.body_count = body_count;

    WriteChartStepData write_data;
    write_data.bodies = bodies;
    write_data.body_count = body_count;

    Pipeline pipeline;
    expect_true(pipeline.add_step(PipelineStep("eval_bodies", eval_bodies_step, &eval_data)), "add eval bodies step", failures);
    expect_true(pipeline.add_step(PipelineStep("project_longitudes", project_longitudes_step, &project_data)), "add project longitudes step", failures);
    expect_true(pipeline.add_step(PipelineStep("write_chart", write_chart_step, &write_data)), "add write chart step", failures);

    BareChart first_chart;
    BareChartScratch first_scratch;
    PipelineFrame first_frame;
    first_frame.chart = &first_chart;
    first_frame.scratch = &first_scratch;

    PipelineRunResult result;
    expect_true(pipeline.run(&first_frame, &result), "first bare chart pipeline run", failures);
    expect_true(result.success, "first bare chart result success", failures);
    expect_size(first_chart.body_count, body_count, "first chart body count", failures);
    expect_size(global_ephemeris_cache_entry_count(), body_count, "selected body cache entry count", failures);

    for (size_t i = 0; i < body_count; ++i) {
        expect_int(first_chart.bodies[i].target_id, bodies[i].target_id, "first chart target id", failures);
        expect_int(first_chart.bodies[i].method_id, bodies[i].expected_method_id, "first chart selected method", failures);
        expect_near(first_chart.bodies[i].longitude_rad, bodies[i].expected_longitude_rad, 1.0e-12,
                    "first chart longitude", failures);
        expect_false(first_chart.bodies[i].cache_hit, "first chart initial loads are misses", failures);
    }

    BareChart second_chart;
    BareChartScratch second_scratch;
    PipelineFrame second_frame;
    second_frame.chart = &second_chart;
    second_frame.scratch = &second_scratch;

    expect_true(pipeline.run(&second_frame, &result), "second bare chart pipeline run", failures);
    expect_true(result.success, "second bare chart result success", failures);
    expect_size(second_chart.body_count, body_count, "second chart body count", failures);

    for (size_t i = 0; i < body_count; ++i) {
        expect_int(second_chart.bodies[i].method_id, bodies[i].expected_method_id, "second chart selected method", failures);
        expect_near(second_chart.bodies[i].longitude_rad, bodies[i].expected_longitude_rad, 1.0e-12,
                    "second chart longitude", failures);
        expect_true(second_chart.bodies[i].cache_hit, "second chart uses cache hits", failures);
    }

    reset_global_runtime_for_test();
    clear_custom_ephemeris_sources();
}

}  // namespace

int main() {
    int failures = 0;
    test_pipeline_builds_bare_chart_with_multiple_methods(&failures);

    if (failures == 0) {
        std::cout << "test_pipeline_bare_chart: ALL TESTS PASSED\n";
        return 0;
    }
    std::cerr << failures << " pipeline bare chart test failure(s)\n";
    return 1;
}
