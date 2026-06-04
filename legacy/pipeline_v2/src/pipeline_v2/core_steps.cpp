#include "taiyin/legacy/pipeline_v2/core_steps.h"

#include "taiyin/legacy/pipeline_v2/artifact_store.h"
#include "taiyin/legacy/pipeline_v2/pipeline_context.h"
#include "taiyin/runtime/runtime_registry.h"
#include "taiyin/runtime/taiyin_runtime.h"
#include "taiyin/vector3.h"

namespace taiyin {
namespace pipeline_v2 {
namespace {

const int CORE_ARTIFACT_SUBJECT_DEFAULT = 0;
const uint32_t CORE_ARTIFACT_VARIANT_DEFAULT = 0;
const double SPEED_OF_LIGHT_AU_PER_DAY = 173.144632674240;

CorePipelineStepIds& core_ids() noexcept {
    static CorePipelineStepIds ids;
    return ids;
}

void set_step_error(PipelineContext* ctx, const char* message) noexcept {
    if (ctx && ctx->diagnostics) {
        ctx->diagnostics->set_error(message);
    }
}

runtime::ArtifactKey body_state_request_key_for(
    const CorePipelineStepIds& ids,
    int32_t subject,
    uint32_t variant
) noexcept {
    return runtime::ArtifactKey(ids.body_state_request_type, subject, variant);
}

runtime::ArtifactKey body_state_result_key_for(
    const CorePipelineStepIds& ids,
    int32_t subject,
    uint32_t variant
) noexcept {
    return runtime::ArtifactKey(ids.body_state_result_type, subject, variant);
}

runtime::ArtifactKey relative_state_request_key_for(
    const CorePipelineStepIds& ids,
    int32_t subject,
    uint32_t variant
) noexcept {
    return runtime::ArtifactKey(ids.relative_state_request_type, subject, variant);
}

runtime::ArtifactKey spherical_position_result_key_for(
    const CorePipelineStepIds& ids,
    int32_t subject,
    uint32_t variant
) noexcept {
    return runtime::ArtifactKey(ids.spherical_position_result_type, subject, variant);
}

runtime::ArtifactKey light_time_iteration_request_key_for(
    const CorePipelineStepIds& ids,
    int32_t subject,
    uint32_t variant
) noexcept {
    return runtime::ArtifactKey(ids.light_time_iteration_request_type, subject, variant);
}

runtime::ArtifactKey light_time_iteration_state_key_for(
    const CorePipelineStepIds& ids,
    int32_t subject,
    uint32_t variant
) noexcept {
    return runtime::ArtifactKey(ids.light_time_iteration_state_type, subject, variant);
}

void subtract_vector(const Vector3& lhs, const Vector3& rhs, Vector3* out) noexcept {
    if (!out) {
        return;
    }
    out->x = lhs.x - rhs.x;
    out->y = lhs.y - rhs.y;
    out->z = lhs.z - rhs.z;
}

double max_double(double lhs, double rhs) noexcept {
    return lhs > rhs ? lhs : rhs;
}

double min_double(double lhs, double rhs) noexcept {
    return lhs < rhs ? lhs : rhs;
}

double abs_double(double value) noexcept {
    return value < 0.0 ? -value : value;
}

}  // namespace

bool register_core_pipeline_steps(
    runtime::RuntimeRegistry* registry,
    CorePipelineStepIds* out
) noexcept {
    if (out) {
        *out = CorePipelineStepIds();
    }
    if (!registry) {
        return false;
    }

    CorePipelineStepIds ids;
    ids.body_state_request_type = registry->register_artifact_type(
        "core.body_state.request",
        sizeof(BodyStateRequest),
        alignof(BodyStateRequest)
    );
    ids.body_state_result_type = registry->register_artifact_type(
        "core.body_state.result",
        sizeof(BodyStateResult),
        alignof(BodyStateResult)
    );
    ids.relative_state_request_type = registry->register_artifact_type(
        "core.relative_state.request",
        sizeof(RelativeStateRequest),
        alignof(RelativeStateRequest)
    );
    ids.spherical_position_result_type = registry->register_artifact_type(
        "core.spherical_position.result",
        sizeof(SphericalPositionResult),
        alignof(SphericalPositionResult)
    );
    ids.light_time_iteration_request_type = registry->register_artifact_type(
        "core.light_time.iteration_request",
        sizeof(LightTimeIterationRequest),
        alignof(LightTimeIterationRequest)
    );
    ids.light_time_iteration_state_type = registry->register_artifact_type(
        "core.light_time.iteration_state",
        sizeof(LightTimeIterationState),
        alignof(LightTimeIterationState)
    );
    ids.eval_body_state_step = registry->register_step(
        "core.eval_body_state",
        &step_eval_body_state
    );
    ids.compute_relative_state_step = registry->register_step(
        "core.compute_relative_state",
        &step_compute_relative_state
    );
    ids.project_spherical_step = registry->register_step(
        "core.project_spherical",
        &step_project_spherical
    );
    ids.update_light_time_step = registry->register_step(
        "core.update_light_time",
        &step_update_light_time
    );

    if (!ids.body_state_request_type.is_valid()
        || !ids.body_state_result_type.is_valid()
        || !ids.relative_state_request_type.is_valid()
        || !ids.spherical_position_result_type.is_valid()
        || !ids.light_time_iteration_request_type.is_valid()
        || !ids.light_time_iteration_state_type.is_valid()
        || !ids.eval_body_state_step.is_valid()
        || !ids.compute_relative_state_step.is_valid()
        || !ids.project_spherical_step.is_valid()
        || !ids.update_light_time_step.is_valid()) {
        return false;
    }

    ids.body_state_request_key = runtime::ArtifactKey(
        ids.body_state_request_type,
        CORE_ARTIFACT_SUBJECT_DEFAULT,
        CORE_ARTIFACT_VARIANT_DEFAULT
    );
    ids.body_state_result_key = runtime::ArtifactKey(
        ids.body_state_result_type,
        CORE_ARTIFACT_SUBJECT_DEFAULT,
        CORE_ARTIFACT_VARIANT_DEFAULT
    );
    ids.relative_state_request_key = runtime::ArtifactKey(
        ids.relative_state_request_type,
        CORE_ARTIFACT_SUBJECT_DEFAULT,
        CORE_ARTIFACT_VARIANT_DEFAULT
    );
    ids.spherical_position_result_key = runtime::ArtifactKey(
        ids.spherical_position_result_type,
        CORE_ARTIFACT_SUBJECT_DEFAULT,
        CORE_ARTIFACT_VARIANT_DEFAULT
    );
    ids.light_time_iteration_request_key = runtime::ArtifactKey(
        ids.light_time_iteration_request_type,
        CORE_ARTIFACT_SUBJECT_DEFAULT,
        CORE_ARTIFACT_VARIANT_DEFAULT
    );
    ids.light_time_iteration_state_key = runtime::ArtifactKey(
        ids.light_time_iteration_state_type,
        CORE_ARTIFACT_SUBJECT_DEFAULT,
        CORE_ARTIFACT_VARIANT_DEFAULT
    );

    core_ids() = ids;
    if (out) {
        *out = ids;
    }
    return true;
}

bool step_eval_body_state(PipelineContext* ctx) {
    const CorePipelineStepIds& ids = core_ids();
    if (!ctx || !ctx->read_artifacts() || !ctx->write_artifacts()) {
        set_step_error(ctx, "core.eval_body_state missing artifact stores");
        return false;
    }
    if (!ctx->taiyin_runtime) {
        set_step_error(ctx, "core.eval_body_state missing TaiyinRuntime");
        return false;
    }
    if (!ids.body_state_request_type.is_valid() || !ids.body_state_result_type.is_valid()) {
        set_step_error(ctx, "core.eval_body_state not registered");
        return false;
    }

    const runtime::ArtifactKey request_key = body_state_request_key_for(
        ids,
        ctx->current_step_subject,
        ctx->current_step_variant
    );
    const runtime::ArtifactKey result_key = body_state_result_key_for(
        ids,
        ctx->current_step_subject,
        ctx->current_step_variant
    );

    BodyStateRequest request;
    if (!ctx->read_artifacts()->get(request_key, &request, ids.body_state_request_type)) {
        set_step_error(ctx, "core.eval_body_state missing BodyStateRequest");
        return false;
    }

    runtime::EphemerisRequest ephemeris_request;
    ephemeris_request.target_id = request.target_id;
    ephemeris_request.center_id = request.center_id;
    ephemeris_request.frame = request.frame;
    ephemeris_request.jd_tdb = request.jd_tdb;

    runtime::EphemerisResult ephemeris_result;
    if (!ctx->taiyin_runtime->ephemeris_service().eval_state(ephemeris_request, &ephemeris_result)) {
        set_step_error(ctx, "core.eval_body_state ephemeris eval failed");
        return false;
    }

    BodyStateResult result;
    result.state = ephemeris_result.state;
    result.target_id = ephemeris_result.descriptor.target_id;
    result.center_id = ephemeris_result.descriptor.center_id;
    result.frame = ephemeris_result.descriptor.frame;
    result.format = ephemeris_result.descriptor.format;
    result.jd_tdb_start = ephemeris_result.descriptor.jd_tdb_start;
    result.jd_tdb_end = ephemeris_result.descriptor.jd_tdb_end;
    result.cache_hit = ephemeris_result.cache_hit;

    if (!ctx->write_artifacts()->set(result_key, &result, ids.body_state_result_type)) {
        set_step_error(ctx, "core.eval_body_state failed to write result");
        return false;
    }
    return true;
}

bool step_project_spherical(PipelineContext* ctx) {
    const CorePipelineStepIds& ids = core_ids();
    if (!ctx || !ctx->read_artifacts() || !ctx->write_artifacts()) {
        set_step_error(ctx, "core.project_spherical missing artifact stores");
        return false;
    }
    if (!ids.body_state_result_type.is_valid() || !ids.spherical_position_result_type.is_valid()) {
        set_step_error(ctx, "core.project_spherical not registered");
        return false;
    }

    const runtime::ArtifactKey body_state_key = body_state_result_key_for(
        ids,
        ctx->current_step_subject,
        ctx->current_step_variant
    );
    BodyStateResult body_state;
    if (!ctx->read_artifacts()->get(body_state_key, &body_state, ids.body_state_result_type)) {
        set_step_error(ctx, "core.project_spherical missing BodyStateResult");
        return false;
    }

    SphericalPositionResult result;
    cartesian_to_spherical(
        body_state.state.position_au,
        &result.longitude_rad,
        &result.latitude_rad,
        &result.radius_au
    );
    result.target_id = body_state.target_id;
    result.center_id = body_state.center_id;
    result.frame = body_state.frame;
    result.jd_tdb_start = body_state.jd_tdb_start;
    result.jd_tdb_end = body_state.jd_tdb_end;
    result.cache_hit = body_state.cache_hit;

    const runtime::ArtifactKey result_key = spherical_position_result_key_for(
        ids,
        ctx->current_step_subject,
        ctx->current_step_variant
    );
    if (!ctx->write_artifacts()->set(result_key, &result, ids.spherical_position_result_type)) {
        set_step_error(ctx, "core.project_spherical failed to write result");
        return false;
    }
    return true;
}

bool step_update_light_time(PipelineContext* ctx) {
    const CorePipelineStepIds& ids = core_ids();
    if (!ctx || !ctx->read_artifacts() || !ctx->write_artifacts()) {
        set_step_error(ctx, "core.update_light_time missing artifact stores");
        return false;
    }
    if (!ids.light_time_iteration_request_type.is_valid()
        || !ids.light_time_iteration_state_type.is_valid()
        || !ids.body_state_request_type.is_valid()
        || !ids.body_state_result_type.is_valid()) {
        set_step_error(ctx, "core.update_light_time not registered");
        return false;
    }

    const runtime::ArtifactKey request_key = light_time_iteration_request_key_for(
        ids,
        ctx->current_step_subject,
        ctx->current_step_variant
    );
    LightTimeIterationRequest request;
    if (!ctx->read_artifacts()->get(request_key, &request, ids.light_time_iteration_request_type)) {
        set_step_error(ctx, "core.update_light_time missing LightTimeIterationRequest");
        return false;
    }
    if (request.max_iterations <= 0 || request.tolerance_days <= 0.0) {
        set_step_error(ctx, "core.update_light_time invalid convergence settings");
        return false;
    }
    if (request.target_eval_step_index >= ctx->current_step_index) {
        set_step_error(ctx, "core.update_light_time target eval step must precede current step");
        return false;
    }

    const runtime::ArtifactKey relative_key = body_state_result_key_for(
        ids,
        request.relative_subject,
        request.relative_variant
    );
    BodyStateResult relative;
    if (!ctx->read_artifacts()->get(relative_key, &relative, ids.body_state_result_type)) {
        set_step_error(ctx, "core.update_light_time missing relative BodyStateResult");
        return false;
    }

    const double radius_au = vector3_norm(relative.state.position_au);
    const double light_time_days = radius_au / SPEED_OF_LIGHT_AU_PER_DAY;

    const runtime::ArtifactKey state_key = light_time_iteration_state_key_for(
        ids,
        ctx->current_step_subject,
        ctx->current_step_variant
    );
    LightTimeIterationState state;
    const bool had_state = ctx->read_artifacts()->get(
        state_key,
        &state,
        ids.light_time_iteration_state_type
    );
    const bool can_compare = had_state && state.iteration_count > 0;
    const double delta_days = can_compare
        ? abs_double(light_time_days - state.current_light_time_days)
        : 0.0;

    state.previous_light_time_days = state.current_light_time_days;
    state.current_light_time_days = light_time_days;
    state.iteration_count += 1;

    if (can_compare && delta_days <= request.tolerance_days) {
        state.converged = true;
        if (!ctx->write_artifacts()->set(state_key, &state, ids.light_time_iteration_state_type)) {
            set_step_error(ctx, "core.update_light_time failed to write converged state");
            return false;
        }
        return true;
    }

    if (state.iteration_count > request.max_iterations) {
        state.converged = false;
        ctx->write_artifacts()->set(state_key, &state, ids.light_time_iteration_state_type);
        set_step_error(ctx, "core.update_light_time exceeded max iterations");
        return false;
    }

    const runtime::ArtifactKey target_request_key = body_state_request_key_for(
        ids,
        request.target_subject,
        request.target_variant
    );
    BodyStateRequest target_request;
    if (!ctx->read_artifacts()->get(target_request_key, &target_request, ids.body_state_request_type)) {
        set_step_error(ctx, "core.update_light_time missing target BodyStateRequest");
        return false;
    }
    target_request.jd_tdb = request.observation_jd_tdb - light_time_days;
    state.converged = false;

    if (!ctx->write_artifacts()->set(target_request_key, &target_request, ids.body_state_request_type)
        || !ctx->write_artifacts()->set(state_key, &state, ids.light_time_iteration_state_type)) {
        set_step_error(ctx, "core.update_light_time failed to write iteration artifacts");
        return false;
    }

    ctx->jump_to(request.target_eval_step_index);
    return true;
}

bool step_compute_relative_state(PipelineContext* ctx) {
    const CorePipelineStepIds& ids = core_ids();
    if (!ctx || !ctx->read_artifacts() || !ctx->write_artifacts()) {
        set_step_error(ctx, "core.compute_relative_state missing artifact stores");
        return false;
    }
    if (!ids.relative_state_request_type.is_valid() || !ids.body_state_result_type.is_valid()) {
        set_step_error(ctx, "core.compute_relative_state not registered");
        return false;
    }

    const runtime::ArtifactKey request_key = relative_state_request_key_for(
        ids,
        ctx->current_step_subject,
        ctx->current_step_variant
    );
    RelativeStateRequest request;
    if (!ctx->read_artifacts()->get(request_key, &request, ids.relative_state_request_type)) {
        set_step_error(ctx, "core.compute_relative_state missing RelativeStateRequest");
        return false;
    }

    const runtime::ArtifactKey target_key = body_state_result_key_for(
        ids,
        request.target_subject,
        request.target_variant
    );
    const runtime::ArtifactKey center_key = body_state_result_key_for(
        ids,
        request.center_subject,
        request.center_variant
    );

    BodyStateResult target;
    BodyStateResult center;
    if (!ctx->read_artifacts()->get(target_key, &target, ids.body_state_result_type)
        || !ctx->read_artifacts()->get(center_key, &center, ids.body_state_result_type)) {
        set_step_error(ctx, "core.compute_relative_state missing input body state");
        return false;
    }
    if (target.frame != center.frame) {
        set_step_error(ctx, "core.compute_relative_state frame mismatch");
        return false;
    }

    BodyStateResult result;
    subtract_vector(target.state.position_au, center.state.position_au, &result.state.position_au);
    subtract_vector(target.state.velocity_au_per_day, center.state.velocity_au_per_day, &result.state.velocity_au_per_day);
    subtract_vector(target.state.acceleration_au_per_day2, center.state.acceleration_au_per_day2, &result.state.acceleration_au_per_day2);
    result.target_id = target.target_id;
    result.center_id = center.target_id;
    result.frame = target.frame;
    result.format = target.format;
    result.jd_tdb_start = max_double(target.jd_tdb_start, center.jd_tdb_start);
    result.jd_tdb_end = min_double(target.jd_tdb_end, center.jd_tdb_end);
    result.cache_hit = target.cache_hit && center.cache_hit;

    const runtime::ArtifactKey result_key = body_state_result_key_for(
        ids,
        ctx->current_step_subject,
        ctx->current_step_variant
    );
    if (!ctx->write_artifacts()->set(result_key, &result, ids.body_state_result_type)) {
        set_step_error(ctx, "core.compute_relative_state failed to write result");
        return false;
    }
    return true;
}

}  // namespace pipeline_v2
}  // namespace taiyin
