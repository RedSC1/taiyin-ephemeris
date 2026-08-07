#ifndef TAIYIN_PIPELINE_V2_CORE_STEPS_H
#define TAIYIN_PIPELINE_V2_CORE_STEPS_H

#include "taiyin/internal/ephemeris_catalog.h"
#include "taiyin/runtime/handle_types.h"
#include "taiyin/state.h"

#include <cstddef>
#include <cstdint>

namespace taiyin {
namespace runtime {
class RuntimeRegistry;
}
namespace pipeline_v2 {

struct PipelineContext;

struct BodyStateRequest {
    int target_id;
    int center_id;
    internal::EphemerisFrame frame;
    double jd_tdb;

    BodyStateRequest()
        : target_id(0),
          center_id(0),
          frame(internal::EphemerisFrame::FrameUnknown),
          jd_tdb(0.0) {}
};

struct BodyStateResult {
    CartesianState state;
    int target_id;
    int center_id;
    internal::EphemerisFrame frame;
    internal::EphemerisBlockFormat format;
    double jd_tdb_start;
    double jd_tdb_end;
    bool cache_hit;

    BodyStateResult()
        : state(),
          target_id(0),
          center_id(0),
          frame(internal::EphemerisFrame::FrameUnknown),
          format(internal::EphemerisBlockFormat::FormatUnknown),
          jd_tdb_start(0.0),
          jd_tdb_end(0.0),
          cache_hit(false) {}
};

struct RelativeStateRequest {
    int target_subject;
    uint32_t target_variant;
    int center_subject;
    uint32_t center_variant;

    RelativeStateRequest()
        : target_subject(0),
          target_variant(0),
          center_subject(0),
          center_variant(0) {}
};

struct SphericalPositionResult {
    double longitude_rad;
    double latitude_rad;
    double radius_au;
    int target_id;
    int center_id;
    internal::EphemerisFrame frame;
    double jd_tdb_start;
    double jd_tdb_end;
    bool cache_hit;

    SphericalPositionResult()
        : longitude_rad(0.0),
          latitude_rad(0.0),
          radius_au(0.0),
          target_id(0),
          center_id(0),
          frame(internal::EphemerisFrame::FrameUnknown),
          jd_tdb_start(0.0),
          jd_tdb_end(0.0),
          cache_hit(false) {}
};

struct LightTimeIterationRequest {
    int target_subject;
    uint32_t target_variant;
    int relative_subject;
    uint32_t relative_variant;
    size_t target_eval_step_index;
    double observation_jd_tdb;
    double tolerance_days;
    int max_iterations;

    LightTimeIterationRequest()
        : target_subject(0),
          target_variant(0),
          relative_subject(0),
          relative_variant(0),
          target_eval_step_index(0),
          observation_jd_tdb(0.0),
          tolerance_days(1.0e-12),
          max_iterations(8) {}
};

struct LightTimeIterationState {
    int iteration_count;
    double previous_light_time_days;
    double current_light_time_days;
    bool converged;

    LightTimeIterationState()
        : iteration_count(0),
          previous_light_time_days(0.0),
          current_light_time_days(0.0),
          converged(false) {}
};

struct CorePipelineStepIds {
    runtime::ArtifactTypeId body_state_request_type;
    runtime::ArtifactTypeId body_state_result_type;
    runtime::ArtifactTypeId relative_state_request_type;
    runtime::ArtifactTypeId spherical_position_result_type;
    runtime::ArtifactTypeId light_time_iteration_request_type;
    runtime::ArtifactTypeId light_time_iteration_state_type;
    runtime::ArtifactKey body_state_request_key;
    runtime::ArtifactKey body_state_result_key;
    runtime::ArtifactKey relative_state_request_key;
    runtime::ArtifactKey spherical_position_result_key;
    runtime::ArtifactKey light_time_iteration_request_key;
    runtime::ArtifactKey light_time_iteration_state_key;
    runtime::StepId eval_body_state_step;
    runtime::StepId compute_relative_state_step;
    runtime::StepId project_spherical_step;
    runtime::StepId update_light_time_step;

    CorePipelineStepIds()
        : body_state_request_type(),
          body_state_result_type(),
          relative_state_request_type(),
          spherical_position_result_type(),
          light_time_iteration_request_type(),
          light_time_iteration_state_type(),
          body_state_request_key(),
          body_state_result_key(),
          relative_state_request_key(),
          spherical_position_result_key(),
          light_time_iteration_request_key(),
          light_time_iteration_state_key(),
          eval_body_state_step(),
          compute_relative_state_step(),
          project_spherical_step(),
          update_light_time_step() {}
};

bool register_core_pipeline_steps(
    runtime::RuntimeRegistry* registry,
    CorePipelineStepIds* out
) noexcept;

bool step_eval_body_state(PipelineContext* ctx);
bool step_compute_relative_state(PipelineContext* ctx);
bool step_project_spherical(PipelineContext* ctx);
bool step_update_light_time(PipelineContext* ctx);

}  // namespace pipeline_v2
}  // namespace taiyin

#endif  // TAIYIN_PIPELINE_V2_CORE_STEPS_H
