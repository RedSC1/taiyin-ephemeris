#include "taiyin/angle.h"
#include "taiyin/body_id.h"
#include "taiyin/coordinates.h"
#include "taiyin/runtime/pipeline.h"
#include "taiyin/runtime/taiyin_runtime.h"

#include <cmath>
#include <cstdlib>
#include <iomanip>
#include <iostream>

namespace {

using namespace taiyin;
using namespace taiyin::internal;
using namespace taiyin::runtime;

const size_t MAX_BODIES = 16;

struct BodySpec {
    const char* name;
    int target_id;
    int center_id;
};

struct BareChartBody {
    const char* name;
    int target_id;
    int center_id;
    int method_id;
    double longitude_deg;
    bool cache_hit;
};

struct BareChart {
    BareChartBody bodies[MAX_BODIES];
    size_t body_count;

    BareChart()
        : bodies(), body_count(0) {}
};

struct BareChartScratch {
    BodySpec bodies[MAX_BODIES];
    EphemerisResult states[MAX_BODIES];
    double longitudes_rad[MAX_BODIES];
    size_t body_count;

    BareChartScratch()
        : bodies(), states(), longitudes_rad(), body_count(0) {}
};

struct EvalBodiesStepData {
    const BodySpec* bodies;
    size_t body_count;
    double jd_tdb;
};

struct WriteChartStepData {
    size_t max_body_count;
};

bool eval_bodies_step(PipelineFrame* frame, void* step_data) {
    BareChartScratch* scratch = static_cast<BareChartScratch*>(frame ? frame->scratch : 0);
    const EvalBodiesStepData* data = static_cast<const EvalBodiesStepData*>(step_data);
    if (!scratch || !data || !data->bodies || data->body_count > MAX_BODIES) {
        return false;
    }

    scratch->body_count = 0;
    for (size_t i = 0; i < data->body_count; ++i) {
        EphemerisRequest request;
        request.target_id = data->bodies[i].target_id;
        request.center_id = data->bodies[i].center_id;
        request.frame = EphemerisFrame::IcrfJ2000Equatorial;
        request.jd_tdb = data->jd_tdb;

        EphemerisResult result;
        if (eval_global_ephemeris_state(request, &result, 0) != taiyin::TAIYIN_STATUS_OK) {
            std::cerr << "warning: skipped " << data->bodies[i].name
                      << " target=" << request.target_id
                      << " center=" << request.center_id << "\n";
            continue;
        }

        const size_t out_index = scratch->body_count;
        scratch->bodies[out_index] = data->bodies[i];
        scratch->states[out_index] = result;
        ++scratch->body_count;
    }

    return scratch->body_count > 0;
}

bool project_longitudes_step(PipelineFrame* frame, void* step_data) {
    (void)step_data;
    BareChartScratch* scratch = static_cast<BareChartScratch*>(frame ? frame->scratch : 0);
    if (!scratch || scratch->body_count > MAX_BODIES) {
        return false;
    }

    const Matrix3x3 ecliptic_matrix = icrf_to_j2000_ecliptic_matrix();
    for (size_t i = 0; i < scratch->body_count; ++i) {
        const Vector3 position = transform_position_with_matrix(
            scratch->states[i].state.position_au,
            ecliptic_matrix
        );
        scratch->longitudes_rad[i] = normalize_radians(std::atan2(position.y, position.x));
    }
    return true;
}

bool write_chart_step(PipelineFrame* frame, void* step_data) {
    BareChart* chart = static_cast<BareChart*>(frame ? frame->chart : 0);
    BareChartScratch* scratch = static_cast<BareChartScratch*>(frame ? frame->scratch : 0);
    const WriteChartStepData* data = static_cast<const WriteChartStepData*>(step_data);
    if (!chart || !scratch || !data || scratch->body_count > data->max_body_count || scratch->body_count > MAX_BODIES) {
        return false;
    }

    chart->body_count = scratch->body_count;
    for (size_t i = 0; i < scratch->body_count; ++i) {
        chart->bodies[i].name = scratch->bodies[i].name;
        chart->bodies[i].target_id = scratch->bodies[i].target_id;
        chart->bodies[i].center_id = scratch->bodies[i].center_id;
        chart->bodies[i].method_id = scratch->states[i].descriptor.method_id;
        chart->bodies[i].longitude_deg = taiyin::rad_to_deg(scratch->longitudes_rad[i]);
        chart->bodies[i].cache_hit = scratch->states[i].cache_hit;
    }
    return true;
}

const char* get_opm4_root(int argc, char** argv) {
    if (argc > 1 && argv[1] && argv[1][0] != '\0') {
        return argv[1];
    }
    return std::getenv("TAIYIN_OPM4_ROOT");
}

void print_usage(const char* program) {
    std::cout << "usage: " << program << " /path/to/data_integrated_opm4\n"
              << "   or: TAIYIN_OPM4_ROOT=/path/to/data_integrated_opm4 " << program << "\n";
}

}  // namespace

int main(int argc, char** argv) {
    const char* opm4_root = get_opm4_root(argc, argv);
    if (!opm4_root) {
        print_usage(argv[0]);
        return 0;
    }

    EphemerisRuntimeConfig config;
    config.cache_max_bytes = 512 * 1024 * 1024;
    if (!initialize_global_ephemeris_runtime(config)) {
        std::cerr << "failed to initialize global ephemeris runtime\n";
        return 1;
    }
    if (!add_global_ephemeris_source_path(opm4_root)) {
        std::cerr << "failed to add ephemeris source path: " << opm4_root << "\n";
        return 1;
    }

    BodySpec bodies[] = {
        { "Mercury", TAIYIN_BODY_MERCURY_BARYCENTER, TAIYIN_BODY_SUN },
        { "Venus", TAIYIN_BODY_VENUS_BARYCENTER, TAIYIN_BODY_SUN },
        { "Mars", TAIYIN_BODY_MARS_BARYCENTER, TAIYIN_BODY_SUN },
        { "Jupiter", TAIYIN_BODY_JUPITER_BARYCENTER, TAIYIN_BODY_SUN },
        { "Saturn", TAIYIN_BODY_SATURN_BARYCENTER, TAIYIN_BODY_SUN },
        { "Uranus", TAIYIN_BODY_URANUS_BARYCENTER, TAIYIN_BODY_SUN },
        { "Neptune", TAIYIN_BODY_NEPTUNE_BARYCENTER, TAIYIN_BODY_SUN },
        { "Pluto", TAIYIN_BODY_PLUTO_BARYCENTER, TAIYIN_BODY_SUN },
        { "Moon", TAIYIN_BODY_MOON, TAIYIN_BODY_EARTH },
    };
    const size_t body_count = sizeof(bodies) / sizeof(bodies[0]);

    EvalBodiesStepData eval_data;
    eval_data.bodies = bodies;
    eval_data.body_count = body_count;
    eval_data.jd_tdb = 2460310.500800740905;

    WriteChartStepData write_data;
    write_data.max_body_count = MAX_BODIES;

    Pipeline pipeline;
    if (!pipeline.add_step(PipelineStep("eval_bodies", eval_bodies_step, &eval_data))
        || !pipeline.add_step(PipelineStep("project_longitudes", project_longitudes_step, 0))
        || !pipeline.add_step(PipelineStep("write_chart", write_chart_step, &write_data))) {
        std::cerr << "failed to build pipeline\n";
        return 1;
    }

    BareChart chart;
    BareChartScratch scratch;
    PipelineFrame frame;
    frame.chart = &chart;
    frame.scratch = &scratch;

    PipelineRunResult result;
    if (!pipeline.run(&frame, &result)) {
        std::cerr << "pipeline failed at step " << result.failed_step_index;
        if (result.failed_step_name) {
            std::cerr << " (" << result.failed_step_name << ")";
        }
        std::cerr << "\n";
        return 1;
    }

    std::cout << std::fixed << std::setprecision(9);
    std::cout << "body,target,center,method,longitude_deg,cache_hit\n";
    for (size_t i = 0; i < chart.body_count; ++i) {
        const BareChartBody& body = chart.bodies[i];
        std::cout << body.name << ","
                  << body.target_id << ","
                  << body.center_id << ","
                  << body.method_id << ","
                  << body.longitude_deg << ","
                  << (body.cache_hit ? "true" : "false") << "\n";
    }

    return 0;
}
