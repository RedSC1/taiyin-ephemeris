#include "taiyin/internal/opm4_catalog_discovery.h"
#include "taiyin/legacy/pipeline_v2/artifact_store.h"
#include "taiyin/legacy/pipeline_v2/core_steps.h"
#include "taiyin/legacy/pipeline_v2/pipeline_context.h"
#include "taiyin/legacy/pipeline_v2/pipeline_v2.h"
#include "taiyin/runtime/taiyin_runtime.h"
#include "taiyin/vector3.h"

#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <vector>

namespace {

using namespace taiyin;
using namespace taiyin::internal;
using namespace taiyin::pipeline_v2;
using namespace taiyin::runtime;

const int MARS_ID = 499;
const int EARTH_ID = 399;
const int SOLAR_SYSTEM_BARYCENTER_ID = 0;
const int RELATIVE_MARS_FROM_EARTH_SUBJECT = 10001;
const int LIGHT_TIME_MARS_FROM_EARTH_SUBJECT = 10002;
const double DEMO_JD_TDB = 2460311.5;
const double REAL_MARS_DEMO_JD_TDB = 2451600.0;
const char* REAL_MARS_OPM4_PATH = std::getenv("TAIYIN_MARS_OPM4_PATH");

struct DemoMarsStateData {
    Vector3 position;
    Vector3 velocity;
    Vector3 acceleration;
    double reference_jd_tdb;
};

CorePipelineStepIds g_core_ids;

int failures = 0;

void expect_true(bool value, const char* label) {
    if (!value) {
        std::cerr << "FAIL: expected true: " << label << "\n";
        ++failures;
    }
}

void expect_near(double actual, double expected, double tolerance, const char* label) {
    if (std::fabs(actual - expected) > tolerance) {
        std::cerr << "FAIL: " << label << ": actual=" << actual << " expected=" << expected
                  << " diff=" << std::fabs(actual - expected) << " tolerance=" << tolerance << "\n";
        ++failures;
    }
}

bool file_exists(const char* path) {
    if (!path) {
        return false;
    }
    std::ifstream file(path, std::ios::binary);
    return static_cast<bool>(file);
}

bool finite_vector(const Vector3& value) {
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

bool finite_projected_position(const SphericalPositionResult& value) {
    return std::isfinite(value.longitude_rad)
        && std::isfinite(value.latitude_rad)
        && std::isfinite(value.radius_au)
        && value.radius_au > 0.0;
}

bool discover_single_opm4_descriptor(const char* path, EphemerisBlockDescriptor* out) {
    if (!out || !path) {
        return false;
    }
    std::vector<EphemerisBlockDescriptor> descriptors;
    EphemerisDiscoveryOptions options;
    options.strict = true;
    if (discover_opm4_file(path, options, &descriptors) != DiscoveryOk || descriptors.size() != 1) {
        return false;
    }
    *out = descriptors[0];
    return true;
}

bool demo_mars_position(double jd_tdb, const void* data, Vector3* out) noexcept {
    if (!data || !out) {
        return false;
    }
    const DemoMarsStateData* state = static_cast<const DemoMarsStateData*>(data);
    const double dt = jd_tdb - state->reference_jd_tdb;
    out->x = state->position.x + state->velocity.x * dt;
    out->y = state->position.y + state->velocity.y * dt;
    out->z = state->position.z + state->velocity.z * dt;
    return true;
}

bool demo_mars_velocity(double, const void* data, Vector3* out) noexcept {
    if (!data || !out) {
        return false;
    }
    const DemoMarsStateData* state = static_cast<const DemoMarsStateData*>(data);
    *out = state->velocity;
    return true;
}

bool demo_mars_acceleration(double, const void* data, Vector3* out) noexcept {
    if (!data || !out) {
        return false;
    }
    const DemoMarsStateData* state = static_cast<const DemoMarsStateData*>(data);
    *out = state->acceleration;
    return true;
}

void destroy_demo_mars_state(void* data) noexcept {
    DemoMarsStateData* state = static_cast<DemoMarsStateData*>(data);
    delete state;
}

EphemerisBlockDescriptor make_demo_descriptor(int target_id, int center_id, int method_id, int bucket_id) {
    EphemerisBlockDescriptor descriptor;
    descriptor.route_key = EphemerisRouteKey(target_id, center_id, method_id, bucket_id);
    descriptor.source_key = EphemerisBlockKey(static_cast<uint64_t>(method_id), static_cast<uint64_t>(bucket_id + 1), 1, 0);
    descriptor.target_id = target_id;
    descriptor.center_id = center_id;
    descriptor.method_id = method_id;
    descriptor.frame = EphemerisFrame::IcrfJ2000Equatorial;
    descriptor.format = EphemerisBlockFormat::Kepler;
    descriptor.jd_tdb_start = DEMO_JD_TDB - 1.0;
    descriptor.jd_tdb_end = DEMO_JD_TDB + 1.0;
    descriptor.path = "pipeline_v2_demo";
    return descriptor;
}

EphemerisBlockDescriptor make_mars_demo_descriptor() {
    return make_demo_descriptor(MARS_ID, SOLAR_SYSTEM_BARYCENTER_ID, 9001, 0);
}

StorageEphemerisBlock make_demo_storage(const Vector3& position, const Vector3& velocity) {
    StorageEphemerisBlock storage;
    storage.format = EphemerisBlockFormat::Kepler;
    storage.position = &demo_mars_position;
    storage.velocity = &demo_mars_velocity;
    storage.acceleration = &demo_mars_acceleration;
    storage.destroy_element = &destroy_demo_mars_state;

    DemoMarsStateData* state = new DemoMarsStateData();
    state->position = position;
    state->velocity = velocity;
    state->acceleration = Vector3{0.0001, 0.0002, 0.0003};
    state->reference_jd_tdb = DEMO_JD_TDB;
    storage.data_vector.push_back(state);
    storage.total_bytes = sizeof(DemoMarsStateData);
    return storage;
}

StorageEphemerisBlock make_mars_demo_storage(const Vector3& position) {
    return make_demo_storage(position, Vector3{0.001, 0.002, 0.003});
}

void test_hardcoded_mars_position_preset_demo() {
    TaiyinRuntime runtime;
    expect_true(register_core_pipeline_steps(&runtime.registry(), &g_core_ids), "register core pipeline steps");

    expect_true(g_core_ids.spherical_position_result_type.is_valid(), "register core spherical artifact type");
    expect_true(g_core_ids.project_spherical_step.is_valid(), "register core projection step");

    EphemerisBlockCatalog catalog;
    EphemerisBlockDescriptor descriptor = make_mars_demo_descriptor();
    expect_true(catalog.add(descriptor), "add Mars demo descriptor");

    EphemerisBlockCache cache(1024 * 1024);
    Vector3 demo_position = Vector3{1.25, 0.75, 0.20};
    StorageEphemerisBlock storage = make_mars_demo_storage(demo_position);
    expect_true(
        cache.insert(descriptor.route_key, descriptor.jd_tdb_start, descriptor.jd_tdb_end, &storage),
        "insert Mars demo state into cache"
    );

    runtime.ephemeris_service().set_catalog(&catalog);
    runtime.ephemeris_service().set_cache(&cache);

    ArtifactStore artifacts(&runtime.registry());
    PipelineDiagnostics diagnostics;
    PipelineContext ctx;
    ctx.runtime = &runtime.registry();
    ctx.taiyin_runtime = &runtime;
    ctx.artifacts = &artifacts;
    ctx.diagnostics = &diagnostics;

    BodyStateRequest request;
    request.target_id = MARS_ID;
    request.center_id = SOLAR_SYSTEM_BARYCENTER_ID;
    request.frame = EphemerisFrame::IcrfJ2000Equatorial;
    request.jd_tdb = DEMO_JD_TDB;
    expect_true(
        artifacts.set(g_core_ids.body_state_request_key, &request, g_core_ids.body_state_request_type),
        "write Mars body-state request"
    );

    Pipeline preset;
    expect_true(preset.add_step(g_core_ids.eval_body_state_step), "add core.eval_body_state to preset");
    expect_true(preset.add_step(g_core_ids.project_spherical_step), "add core.project_spherical to preset");
    expect_true(preset.run(&ctx), "run hardcoded Mars demo preset");

    BodyStateResult body_state;
    expect_true(
        artifacts.get(g_core_ids.body_state_result_key, &body_state, g_core_ids.body_state_result_type),
        "read Mars body-state result"
    );
    expect_true(body_state.cache_hit, "Mars body-state result is cache hit");
    expect_near(body_state.state.position_au.x, demo_position.x, 1.0e-15, "Mars state x");
    expect_near(body_state.state.velocity_au_per_day.y, 0.002, 1.0e-15, "Mars velocity y");

    SphericalPositionResult projected;
    expect_true(
        artifacts.get(g_core_ids.spherical_position_result_key, &projected, g_core_ids.spherical_position_result_type),
        "read Mars projected core output"
    );

    double expected_lon = 0.0;
    double expected_lat = 0.0;
    double expected_radius = 0.0;
    cartesian_to_spherical(demo_position, &expected_lon, &expected_lat, &expected_radius);
    expect_near(projected.longitude_rad, expected_lon, 1.0e-15, "Mars projected longitude");
    expect_near(projected.latitude_rad, expected_lat, 1.0e-15, "Mars projected latitude");
    expect_near(projected.radius_au, expected_radius, 1.0e-15, "Mars projected radius");
}

void test_multi_body_relative_state_preset_demo() {
    TaiyinRuntime runtime;
    expect_true(register_core_pipeline_steps(&runtime.registry(), &g_core_ids), "register core pipeline steps for relative demo");

    EphemerisBlockCatalog catalog;
    EphemerisBlockDescriptor mars_descriptor = make_demo_descriptor(MARS_ID, SOLAR_SYSTEM_BARYCENTER_ID, 9101, 1);
    EphemerisBlockDescriptor earth_descriptor = make_demo_descriptor(EARTH_ID, SOLAR_SYSTEM_BARYCENTER_ID, 9102, 2);
    expect_true(catalog.add(mars_descriptor), "add relative Mars descriptor");
    expect_true(catalog.add(earth_descriptor), "add relative Earth descriptor");

    EphemerisBlockCache cache(1024 * 1024);
    const Vector3 mars_position = Vector3{1.25, 0.75, 0.20};
    const Vector3 earth_position = Vector3{0.25, -0.10, 0.05};
    StorageEphemerisBlock mars_storage = make_mars_demo_storage(mars_position);
    StorageEphemerisBlock earth_storage = make_mars_demo_storage(earth_position);
    expect_true(
        cache.insert(mars_descriptor.route_key, mars_descriptor.jd_tdb_start, mars_descriptor.jd_tdb_end, &mars_storage),
        "insert relative Mars state into cache"
    );
    expect_true(
        cache.insert(earth_descriptor.route_key, earth_descriptor.jd_tdb_start, earth_descriptor.jd_tdb_end, &earth_storage),
        "insert relative Earth state into cache"
    );

    runtime.ephemeris_service().set_catalog(&catalog);
    runtime.ephemeris_service().set_cache(&cache);

    ArtifactStore artifacts(&runtime.registry());
    PipelineDiagnostics diagnostics;
    PipelineContext ctx;
    ctx.runtime = &runtime.registry();
    ctx.taiyin_runtime = &runtime;
    ctx.artifacts = &artifacts;
    ctx.diagnostics = &diagnostics;

    BodyStateRequest mars_request;
    mars_request.target_id = MARS_ID;
    mars_request.center_id = SOLAR_SYSTEM_BARYCENTER_ID;
    mars_request.frame = EphemerisFrame::IcrfJ2000Equatorial;
    mars_request.jd_tdb = DEMO_JD_TDB;
    expect_true(
        artifacts.set(ArtifactKey(g_core_ids.body_state_request_type, MARS_ID, 0), &mars_request, g_core_ids.body_state_request_type),
        "write Mars subject request"
    );

    BodyStateRequest earth_request;
    earth_request.target_id = EARTH_ID;
    earth_request.center_id = SOLAR_SYSTEM_BARYCENTER_ID;
    earth_request.frame = EphemerisFrame::IcrfJ2000Equatorial;
    earth_request.jd_tdb = DEMO_JD_TDB;
    expect_true(
        artifacts.set(ArtifactKey(g_core_ids.body_state_request_type, EARTH_ID, 0), &earth_request, g_core_ids.body_state_request_type),
        "write Earth subject request"
    );

    RelativeStateRequest relative_request;
    relative_request.target_subject = MARS_ID;
    relative_request.center_subject = EARTH_ID;
    expect_true(
        artifacts.set(
            ArtifactKey(g_core_ids.relative_state_request_type, RELATIVE_MARS_FROM_EARTH_SUBJECT, 0),
            &relative_request,
            g_core_ids.relative_state_request_type),
        "write relative-state request"
    );

    Pipeline preset;
    expect_true(preset.add_step(g_core_ids.eval_body_state_step, MARS_ID), "add subject Mars eval step");
    expect_true(preset.add_step(g_core_ids.eval_body_state_step, EARTH_ID), "add subject Earth eval step");
    expect_true(
        preset.add_step(g_core_ids.compute_relative_state_step, RELATIVE_MARS_FROM_EARTH_SUBJECT),
        "add relative-state step"
    );
    expect_true(
        preset.add_step(g_core_ids.project_spherical_step, RELATIVE_MARS_FROM_EARTH_SUBJECT),
        "add relative spherical projection step"
    );
    expect_true(preset.run(&ctx), "run multi-body relative preset");

    BodyStateResult relative;
    expect_true(
        artifacts.get(
            ArtifactKey(g_core_ids.body_state_result_type, RELATIVE_MARS_FROM_EARTH_SUBJECT, 0),
            &relative,
            g_core_ids.body_state_result_type),
        "read relative Mars-from-Earth result"
    );
    expect_near(relative.state.position_au.x, mars_position.x - earth_position.x, 1.0e-15, "relative x");
    expect_near(relative.state.position_au.y, mars_position.y - earth_position.y, 1.0e-15, "relative y");
    expect_near(relative.state.position_au.z, mars_position.z - earth_position.z, 1.0e-15, "relative z");
    expect_near(relative.state.velocity_au_per_day.x, 0.0, 1.0e-15, "relative velocity x");
    expect_true(relative.target_id == MARS_ID, "relative target id");
    expect_true(relative.center_id == EARTH_ID, "relative center id");
    expect_true(relative.cache_hit, "relative inputs were cache hits");

    SphericalPositionResult projected_relative;
    expect_true(
        artifacts.get(
            ArtifactKey(g_core_ids.spherical_position_result_type, RELATIVE_MARS_FROM_EARTH_SUBJECT, 0),
            &projected_relative,
            g_core_ids.spherical_position_result_type),
        "read projected relative Mars-from-Earth result"
    );
    double expected_lon = 0.0;
    double expected_lat = 0.0;
    double expected_radius = 0.0;
    cartesian_to_spherical(relative.state.position_au, &expected_lon, &expected_lat, &expected_radius);
    expect_near(projected_relative.longitude_rad, expected_lon, 1.0e-15, "relative projected longitude");
    expect_near(projected_relative.latitude_rad, expected_lat, 1.0e-15, "relative projected latitude");
    expect_near(projected_relative.radius_au, expected_radius, 1.0e-15, "relative projected radius");
    expect_true(projected_relative.target_id == MARS_ID, "relative projected target id");
    expect_true(projected_relative.center_id == EARTH_ID, "relative projected center id");
    expect_true(ctx.current_step_subject == RELATIVE_MARS_FROM_EARTH_SUBJECT, "runner exposes current step subject");
}

void test_light_time_iteration_preset_demo() {
    TaiyinRuntime runtime;
    expect_true(register_core_pipeline_steps(&runtime.registry(), &g_core_ids), "register core pipeline steps for light-time demo");
    expect_true(g_core_ids.update_light_time_step.is_valid(), "register core light-time step");

    EphemerisBlockCatalog catalog;
    EphemerisBlockDescriptor mars_descriptor = make_demo_descriptor(MARS_ID, SOLAR_SYSTEM_BARYCENTER_ID, 9201, 3);
    EphemerisBlockDescriptor earth_descriptor = make_demo_descriptor(EARTH_ID, SOLAR_SYSTEM_BARYCENTER_ID, 9202, 4);
    expect_true(catalog.add(mars_descriptor), "add light-time Mars descriptor");
    expect_true(catalog.add(earth_descriptor), "add light-time Earth descriptor");

    EphemerisBlockCache cache(1024 * 1024);
    const Vector3 mars_position = Vector3{1.25, 0.75, 0.20};
    const Vector3 mars_velocity = Vector3{0.20, 0.0, 0.0};
    const Vector3 earth_position = Vector3{0.25, -0.10, 0.05};
    const Vector3 earth_velocity = Vector3{0.0, 0.0, 0.0};
    StorageEphemerisBlock mars_storage = make_demo_storage(mars_position, mars_velocity);
    StorageEphemerisBlock earth_storage = make_demo_storage(earth_position, earth_velocity);
    expect_true(
        cache.insert(mars_descriptor.route_key, mars_descriptor.jd_tdb_start, mars_descriptor.jd_tdb_end, &mars_storage),
        "insert light-time Mars state into cache"
    );
    expect_true(
        cache.insert(earth_descriptor.route_key, earth_descriptor.jd_tdb_start, earth_descriptor.jd_tdb_end, &earth_storage),
        "insert light-time Earth state into cache"
    );

    runtime.ephemeris_service().set_catalog(&catalog);
    runtime.ephemeris_service().set_cache(&cache);

    ArtifactStore artifacts(&runtime.registry());
    PipelineDiagnostics diagnostics;
    PipelineContext ctx;
    ctx.runtime = &runtime.registry();
    ctx.taiyin_runtime = &runtime;
    ctx.artifacts = &artifacts;
    ctx.diagnostics = &diagnostics;
    ctx.max_step_visits = 32;

    BodyStateRequest earth_request;
    earth_request.target_id = EARTH_ID;
    earth_request.center_id = SOLAR_SYSTEM_BARYCENTER_ID;
    earth_request.frame = EphemerisFrame::IcrfJ2000Equatorial;
    earth_request.jd_tdb = DEMO_JD_TDB;
    expect_true(
        artifacts.set(ArtifactKey(g_core_ids.body_state_request_type, EARTH_ID, 0), &earth_request, g_core_ids.body_state_request_type),
        "write light-time Earth request"
    );

    BodyStateRequest mars_request;
    mars_request.target_id = MARS_ID;
    mars_request.center_id = SOLAR_SYSTEM_BARYCENTER_ID;
    mars_request.frame = EphemerisFrame::IcrfJ2000Equatorial;
    mars_request.jd_tdb = DEMO_JD_TDB;
    expect_true(
        artifacts.set(ArtifactKey(g_core_ids.body_state_request_type, MARS_ID, 0), &mars_request, g_core_ids.body_state_request_type),
        "write light-time Mars request"
    );

    RelativeStateRequest relative_request;
    relative_request.target_subject = MARS_ID;
    relative_request.center_subject = EARTH_ID;
    expect_true(
        artifacts.set(
            ArtifactKey(g_core_ids.relative_state_request_type, RELATIVE_MARS_FROM_EARTH_SUBJECT, 0),
            &relative_request,
            g_core_ids.relative_state_request_type),
        "write light-time relative-state request"
    );

    LightTimeIterationRequest light_time_request;
    light_time_request.target_subject = MARS_ID;
    light_time_request.relative_subject = RELATIVE_MARS_FROM_EARTH_SUBJECT;
    light_time_request.target_eval_step_index = 1;
    light_time_request.observation_jd_tdb = DEMO_JD_TDB;
    light_time_request.tolerance_days = 1.0e-12;
    light_time_request.max_iterations = 8;
    expect_true(
        artifacts.set(
            ArtifactKey(g_core_ids.light_time_iteration_request_type, LIGHT_TIME_MARS_FROM_EARTH_SUBJECT, 0),
            &light_time_request,
            g_core_ids.light_time_iteration_request_type),
        "write light-time iteration request"
    );

    Pipeline preset;
    expect_true(preset.add_step(g_core_ids.eval_body_state_step, EARTH_ID), "add light-time Earth eval step");
    expect_true(preset.add_step(g_core_ids.eval_body_state_step, MARS_ID), "add light-time Mars eval step");
    expect_true(
        preset.add_step(g_core_ids.compute_relative_state_step, RELATIVE_MARS_FROM_EARTH_SUBJECT),
        "add light-time relative-state step"
    );
    expect_true(
        preset.add_step(g_core_ids.update_light_time_step, LIGHT_TIME_MARS_FROM_EARTH_SUBJECT),
        "add light-time iteration step"
    );
    expect_true(
        preset.add_step(g_core_ids.project_spherical_step, RELATIVE_MARS_FROM_EARTH_SUBJECT),
        "add light-time projection step"
    );
    const bool run_ok = preset.run(&ctx);
    if (!run_ok && diagnostics.last_error) {
        std::cerr << "light-time demo diagnostic: " << diagnostics.last_error << "\n";
    }
    expect_true(run_ok, "run light-time iteration preset");
    expect_true(ctx.step_visit_count > preset.step_count(), "light-time iteration jumped back through preset");

    LightTimeIterationState light_time_state;
    expect_true(
        artifacts.get(
            ArtifactKey(g_core_ids.light_time_iteration_state_type, LIGHT_TIME_MARS_FROM_EARTH_SUBJECT, 0),
            &light_time_state,
            g_core_ids.light_time_iteration_state_type),
        "read light-time iteration state"
    );
    expect_true(light_time_state.converged, "light-time iteration converged");
    expect_true(light_time_state.iteration_count > 1, "light-time iteration ran more than once");
    expect_true(light_time_state.current_light_time_days > 0.0, "light-time is positive");

    BodyStateRequest final_mars_request;
    expect_true(
        artifacts.get(ArtifactKey(g_core_ids.body_state_request_type, MARS_ID, 0), &final_mars_request, g_core_ids.body_state_request_type),
        "read final light-time Mars request"
    );
    expect_true(final_mars_request.jd_tdb < DEMO_JD_TDB, "light-time shifted target request into past");

    BodyStateResult relative;
    expect_true(
        artifacts.get(
            ArtifactKey(g_core_ids.body_state_result_type, RELATIVE_MARS_FROM_EARTH_SUBJECT, 0),
            &relative,
            g_core_ids.body_state_result_type),
        "read light-time relative result"
    );
    const double mars_dt = final_mars_request.jd_tdb - DEMO_JD_TDB;
    const double expected_relative_x = mars_position.x + mars_velocity.x * mars_dt - earth_position.x;
    const double expected_relative_y = mars_position.y + mars_velocity.y * mars_dt - earth_position.y;
    const double expected_relative_z = mars_position.z + mars_velocity.z * mars_dt - earth_position.z;
    expect_near(relative.state.position_au.x, expected_relative_x, 1.0e-12, "light-time relative x");
    expect_near(relative.state.position_au.y, expected_relative_y, 1.0e-12, "light-time relative y");
    expect_near(relative.state.position_au.z, expected_relative_z, 1.0e-12, "light-time relative z");

    SphericalPositionResult projected;
    expect_true(
        artifacts.get(
            ArtifactKey(g_core_ids.spherical_position_result_type, RELATIVE_MARS_FROM_EARTH_SUBJECT, 0),
            &projected,
            g_core_ids.spherical_position_result_type),
        "read light-time projected result"
    );
    expect_true(finite_projected_position(projected), "light-time projected output is finite");
}

void test_real_mars_descriptor_load_preset_demo() {
    if (!file_exists(REAL_MARS_OPM4_PATH)) {
        std::cout << "test_legacy_pipeline_v2_mars_demo: skipping real Mars OPM4 demo; local data file not found\n";
        return;
    }

    EphemerisBlockDescriptor descriptor;
    expect_true(discover_single_opm4_descriptor(REAL_MARS_OPM4_PATH, &descriptor), "discover real Mars OPM4 descriptor");
    descriptor.jd_tdb_start = REAL_MARS_DEMO_JD_TDB - 1.0;
    descriptor.jd_tdb_end = REAL_MARS_DEMO_JD_TDB + 1.0;

    TaiyinRuntime runtime;
    expect_true(register_core_pipeline_steps(&runtime.registry(), &g_core_ids), "register core pipeline steps for real Mars demo");
    expect_true(g_core_ids.spherical_position_result_type.is_valid(), "register real core spherical artifact type");
    expect_true(g_core_ids.project_spherical_step.is_valid(), "register real core projection step");

    EphemerisBlockCatalog catalog;
    expect_true(catalog.add(descriptor), "add real Mars descriptor");

    EphemerisBlockCache cache(64 * 1024 * 1024);
    runtime.ephemeris_service().set_catalog(&catalog);
    runtime.ephemeris_service().set_cache(&cache);

    ArtifactStore artifacts(&runtime.registry());
    PipelineDiagnostics diagnostics;
    PipelineContext ctx;
    ctx.runtime = &runtime.registry();
    ctx.taiyin_runtime = &runtime;
    ctx.artifacts = &artifacts;
    ctx.diagnostics = &diagnostics;

    BodyStateRequest request;
    request.target_id = descriptor.target_id;
    request.center_id = descriptor.center_id;
    request.frame = descriptor.frame;
    request.jd_tdb = REAL_MARS_DEMO_JD_TDB;
    expect_true(
        artifacts.set(g_core_ids.body_state_request_key, &request, g_core_ids.body_state_request_type),
        "write real Mars body-state request"
    );

    Pipeline preset;
    expect_true(preset.add_step(g_core_ids.eval_body_state_step), "add real core.eval_body_state to preset");
    expect_true(preset.add_step(g_core_ids.project_spherical_step), "add real core.project_spherical to preset");
    const bool first_run_ok = preset.run(&ctx);
    if (!first_run_ok && diagnostics.last_error) {
        std::cerr << "real Mars first run diagnostic: " << diagnostics.last_error << "\n";
    }
    expect_true(first_run_ok, "run real Mars descriptor-load preset");

    BodyStateResult body_state;
    expect_true(
        artifacts.get(g_core_ids.body_state_result_key, &body_state, g_core_ids.body_state_result_type),
        "read real Mars body-state result"
    );
    expect_true(!body_state.cache_hit, "first real Mars eval reports cache miss load");
    expect_true(cache.entry_count() == 1, "real Mars cache contains loaded bucket");
    expect_true(finite_vector(body_state.state.position_au), "real Mars position is finite");
    expect_true(finite_vector(body_state.state.velocity_au_per_day), "real Mars velocity is finite");

    SphericalPositionResult projected;
    expect_true(
        artifacts.get(g_core_ids.spherical_position_result_key, &projected, g_core_ids.spherical_position_result_type),
        "read real Mars projected output"
    );
    expect_true(finite_projected_position(projected), "real Mars projected output is finite");

    const bool second_run_ok = preset.run(&ctx);
    if (!second_run_ok && diagnostics.last_error) {
        std::cerr << "real Mars second run diagnostic: " << diagnostics.last_error << "\n";
    }
    expect_true(second_run_ok, "rerun real Mars descriptor-load preset");
    expect_true(
        artifacts.get(g_core_ids.body_state_result_key, &body_state, g_core_ids.body_state_result_type),
        "read rerun real Mars body-state result"
    );
    expect_true(body_state.cache_hit, "second real Mars eval reports cache hit");
    expect_true(cache.entry_count() == 1, "real Mars cache reuses loaded bucket");
}

}  // namespace

int main() {
    test_hardcoded_mars_position_preset_demo();
    test_multi_body_relative_state_preset_demo();
    test_light_time_iteration_preset_demo();
    test_real_mars_descriptor_load_preset_demo();

    if (failures == 0) {
        std::cout << "test_legacy_pipeline_v2_mars_demo: ALL TESTS PASSED\n";
        return 0;
    }
    std::cerr << failures << " test_legacy_pipeline_v2_mars_demo failure(s)\n";
    return 1;
}
