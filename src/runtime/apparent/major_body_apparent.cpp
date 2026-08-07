#include "taiyin/runtime/major_body_apparent.h"

#include "runtime/core/native_context_checks.h"
#include "runtime/core/runtime_state_block_adapter.h"

#include "taiyin/apparent_position.h"
#include "taiyin/body_id.h"
#include "taiyin/coordinates.h"
#include "taiyin/corrections.h"
#include "taiyin/dispatch.h"

#include <cmath>
#include <limits>
#include <mutex>
#include <vector>

namespace taiyin {
namespace runtime {
namespace {

struct MajorBodySpec {
    uint32_t mask_bit;
    int body_id;
    const char* name;
};

const MajorBodySpec MAJOR_BODY_SPECS[TAIYIN_MAJOR_BODY_COUNT] = {
    { TAIYIN_MAJOR_BODY_SUN, TAIYIN_BODY_SUN, "Sun" },
    { TAIYIN_MAJOR_BODY_MOON, TAIYIN_BODY_MOON, "Moon" },
    { TAIYIN_MAJOR_BODY_MERCURY, TAIYIN_BODY_MERCURY_BARYCENTER, "Mercury" },
    { TAIYIN_MAJOR_BODY_VENUS, TAIYIN_BODY_VENUS_BARYCENTER, "Venus" },
    { TAIYIN_MAJOR_BODY_MARS, TAIYIN_BODY_MARS_BARYCENTER, "Mars" },
    { TAIYIN_MAJOR_BODY_JUPITER, TAIYIN_BODY_JUPITER_BARYCENTER, "Jupiter" },
    { TAIYIN_MAJOR_BODY_SATURN, TAIYIN_BODY_SATURN_BARYCENTER, "Saturn" },
    { TAIYIN_MAJOR_BODY_URANUS, TAIYIN_BODY_URANUS_BARYCENTER, "Uranus" },
    { TAIYIN_MAJOR_BODY_NEPTUNE, TAIYIN_BODY_NEPTUNE_BARYCENTER, "Neptune" },
    { TAIYIN_MAJOR_BODY_PLUTO, TAIYIN_BODY_PLUTO_BARYCENTER, "Pluto" },
};

struct GlobalApparentConfigManager {
    AstroModelContext model_context;
    ApparentOptions apparent_options;
    std::vector<ApparentDeflector> deflectors;
    int solar_deflector_index;
    std::mutex mutex;

    GlobalApparentConfigManager() noexcept
        : model_context(), apparent_options(), deflectors(), solar_deflector_index(-1), mutex() {}
};

struct GlobalApparentConfigSnapshot {
    AstroModelContext model_context;
    ApparentOptions apparent_options;
    std::vector<ApparentDeflector> deflectors;
    int solar_deflector_index;

    GlobalApparentConfigSnapshot()
        : model_context(), apparent_options(), deflectors(), solar_deflector_index(-1) {}
};

struct ResolvedApparentConfig {
    ApparentOptions options;
    AstroModelContext requested_models;
    dispatch::PrecessionModelEntry precession;
    dispatch::NutationModelEntry nutation;
    std::vector<ApparentDeflector> deflectors;
    int solar_deflector_index;
    int resolved_tdb_model_id;
    int resolved_obliquity_model_id;
    int resolved_frame_route_id;

    ResolvedApparentConfig() noexcept
        : options(),
          requested_models(),
          precession(),
          nutation(),
          deflectors(),
          solar_deflector_index(-1),
          resolved_tdb_model_id(dispatch::TDB_FAST_PERIODIC),
          resolved_obliquity_model_id(0),
          resolved_frame_route_id(dispatch::FRAME_ROUTE_EQUINOX) {}
};

GlobalApparentConfigManager& global_apparent_config_manager() noexcept {
    static GlobalApparentConfigManager manager;
    return manager;
}

GlobalApparentConfigSnapshot snapshot_global_apparent_config() noexcept {
    GlobalApparentConfigManager& manager = global_apparent_config_manager();
    std::lock_guard<std::mutex> lock(manager.mutex);

    GlobalApparentConfigSnapshot snapshot;
    snapshot.model_context = manager.model_context;
    snapshot.apparent_options = manager.apparent_options;
    snapshot.apparent_options.model_context = 0;
    snapshot.apparent_options.deflectors = 0;
    snapshot.apparent_options.deflector_count = 0;
    snapshot.apparent_options.solar_deflector_index = -1;
    return snapshot;
}

AstroModelContext snapshot_global_astro_model_context() noexcept {
    GlobalApparentConfigManager& manager = global_apparent_config_manager();
    std::lock_guard<std::mutex> lock(manager.mutex);
    return manager.model_context;
}

bool valid_solar_deflector_index(size_t deflector_count, int solar_deflector_index) noexcept {
    return solar_deflector_index < 0
        || static_cast<size_t>(solar_deflector_index) < deflector_count;
}

uint32_t runtime_components_for_apparent_flags(uint32_t flags) noexcept {
    uint32_t components = internal::EPHEMERIS_BLOCK_COMPONENT_POSITION;
    if ((flags & TAIYIN_APPARENT_VELOCITY) != 0u) {
        components |= internal::EPHEMERIS_BLOCK_COMPONENT_VELOCITY;
    }
    if ((flags & TAIYIN_APPARENT_ACCELERATION) != 0u) {
        components |= internal::EPHEMERIS_BLOCK_COMPONENT_VELOCITY
            | internal::EPHEMERIS_BLOCK_COMPONENT_ACCELERATION;
    }
    return components;
}

uint32_t runtime_observer_components_for_apparent_flags(uint32_t flags) noexcept {
    uint32_t components = runtime_components_for_apparent_flags(flags);
    if ((flags & TAIYIN_APPARENT_ABERRATION) != 0u) {
        components |= internal::EPHEMERIS_BLOCK_COMPONENT_VELOCITY;
        if ((flags & TAIYIN_APPARENT_VELOCITY) != 0u) {
            components |= internal::EPHEMERIS_BLOCK_COMPONENT_ACCELERATION;
        }
    }
    return components;
}

Status copy_explicit_deflectors(
    const ApparentDeflector* deflectors,
    size_t deflector_count,
    int solar_deflector_index,
    std::vector<ApparentDeflector>* out
) noexcept {
    if (!out || (!deflectors && deflector_count > 0) || !valid_solar_deflector_index(deflector_count, solar_deflector_index)) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    try {
        out->clear();
        for (size_t i = 0; i < deflector_count; ++i) {
            out->push_back(deflectors[i]);
        }
    } catch (...) {
        out->clear();
        return TAIYIN_ERROR_OUT_OF_MEMORY;
    }
    return TAIYIN_STATUS_OK;
}

Status snapshot_global_deflectors(
    std::vector<ApparentDeflector>* out,
    int* out_solar_deflector_index
) noexcept {
    if (!out || !out_solar_deflector_index) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    GlobalApparentConfigManager& manager = global_apparent_config_manager();
    std::lock_guard<std::mutex> lock(manager.mutex);
    try {
        *out = manager.deflectors;
    } catch (...) {
        out->clear();
        return TAIYIN_ERROR_OUT_OF_MEMORY;
    }
    *out_solar_deflector_index = manager.solar_deflector_index;
    return TAIYIN_STATUS_OK;
}

const uint32_t SUPPORTED_MAJOR_BODY_APPARENT_FLAGS =
    TAIYIN_APPARENT_LIGHT_TIME
    | TAIYIN_APPARENT_SPHERICAL
    | TAIYIN_APPARENT_ABERRATION
    | TAIYIN_APPARENT_DEFLECTION
    | TAIYIN_APPARENT_VELOCITY
    | TAIYIN_APPARENT_ACCELERATION
    | TAIYIN_APPARENT_SHAPIRO_DELAY
    | TAIYIN_APPARENT_TOPOCENTRIC;

Status resolve_apparent_config(
    const MajorBodyApparentBatchRequest& request,
    ResolvedApparentConfig* out
) noexcept {
    if (!out) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }

    *out = ResolvedApparentConfig();
    if (request.options) {
        out->options = *request.options;
        out->requested_models = out->options.model_context
            ? *out->options.model_context
            : snapshot_global_astro_model_context();
        if (out->options.deflectors || out->options.deflector_count > 0) {
            const Status deflector_status = copy_explicit_deflectors(
                out->options.deflectors,
                out->options.deflector_count,
                out->options.solar_deflector_index,
                &out->deflectors);
            if (deflector_status != TAIYIN_STATUS_OK) {
                return deflector_status;
            }
            out->solar_deflector_index = out->options.solar_deflector_index;
        } else {
            const Status deflector_status = snapshot_global_deflectors(
                &out->deflectors,
                &out->solar_deflector_index);
            if (deflector_status != TAIYIN_STATUS_OK) {
                return deflector_status;
            }
        }
    } else {
        const GlobalApparentConfigSnapshot snapshot = snapshot_global_apparent_config();
        out->options = snapshot.apparent_options;
        out->requested_models = snapshot.model_context;
        const Status deflector_status = snapshot_global_deflectors(
            &out->deflectors,
            &out->solar_deflector_index);
        if (deflector_status != TAIYIN_STATUS_OK) {
            return deflector_status;
        }
    }
    out->options.model_context = 0;
    out->options.deflectors = 0;
    out->options.deflector_count = 0;
    out->options.solar_deflector_index = -1;

    if ((out->options.flags & ~SUPPORTED_MAJOR_BODY_APPARENT_FLAGS) != 0u) {
        return TAIYIN_ERROR_UNSUPPORTED;
    }
    if (!dispatch::select_precession_model(out->requested_models.precession_model_id, &out->precession)
        || !dispatch::select_nutation_model(out->requested_models.nutation_model_id, &out->nutation)) {
        return TAIYIN_ERROR_UNSUPPORTED;
    }

    out->resolved_tdb_model_id = out->requested_models.tdb_model_id;
    out->resolved_obliquity_model_id = out->requested_models.obliquity_model_id;
    out->resolved_frame_route_id = out->requested_models.frame_route_id;
    return TAIYIN_STATUS_OK;
}

uint32_t mask_bit_for_body_id(int body_id) noexcept {
    for (size_t i = 0; i < TAIYIN_MAJOR_BODY_COUNT; ++i) {
        if (MAJOR_BODY_SPECS[i].body_id == body_id) {
            return MAJOR_BODY_SPECS[i].mask_bit;
        }
    }
    return 0u;
}

SplitJulianDate resolve_jd_tt(const MajorBodyApparentBatchRequest& request) noexcept {
    return split_julian_date_is_finite(request.jd_tt)
            && request.jd_tt != SplitJulianDate()
        ? request.jd_tt
        : request.jd_tdb;
}

Status compute_one_body(
    const RuntimeStateEvalContext& context,
    const MajorBodyApparentBatchRequest& request,
    const ResolvedApparentConfig& config,
    int body_id,
    const EphemerisResult&,
    MajorBodyApparentPosition* out,
    EphemerisEvalDiagnostic* diagnostic
) noexcept {
    if (!out) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }

    *out = MajorBodyApparentPosition();
    out->body_id = body_id;
    out->body_mask_bit = mask_bit_for_body_id(body_id);

    const uint32_t apparent_flags = config.options.flags | TAIYIN_APPARENT_SPHERICAL;
    const bool needs_deflectors = (apparent_flags & (
        TAIYIN_APPARENT_ABERRATION
        | TAIYIN_APPARENT_DEFLECTION
        | TAIYIN_APPARENT_SHAPIRO_DELAY)) != 0u;
    if ((apparent_flags & TAIYIN_APPARENT_SHAPIRO_DELAY) != 0u
        && (apparent_flags & TAIYIN_APPARENT_LIGHT_TIME) == 0u) {
        out->status = TAIYIN_ERROR_INVALID_ARGUMENT;
        if (diagnostic) {
            diagnostic->status = out->status;
            diagnostic->target_id = body_id;
            diagnostic->center_id = request.observer_id;
            diagnostic->frame = internal::EphemerisFrame::IcrfJ2000Equatorial;
            diagnostic->jd_tdb = request.jd_tdb;
        }
        return out->status;
    }
    if (needs_deflectors && config.deflectors.empty()) {
        out->status = TAIYIN_ERROR_INVALID_ARGUMENT;
        if (diagnostic) {
            diagnostic->status = out->status;
            diagnostic->target_id = body_id;
            diagnostic->center_id = request.observer_id;
            diagnostic->frame = internal::EphemerisFrame::IcrfJ2000Equatorial;
            diagnostic->jd_tdb = request.jd_tdb;
        }
        return out->status;
    }
    if (needs_deflectors
        && (config.solar_deflector_index < 0
            || static_cast<size_t>(config.solar_deflector_index) >= config.deflectors.size())) {
        out->status = TAIYIN_ERROR_INVALID_ARGUMENT;
        if (diagnostic) {
            diagnostic->status = out->status;
            diagnostic->target_id = body_id;
            diagnostic->center_id = request.observer_id;
            diagnostic->frame = internal::EphemerisFrame::IcrfJ2000Equatorial;
            diagnostic->jd_tdb = request.jd_tdb;
        }
        return out->status;
    }
    if (config.deflectors.size() > static_cast<size_t>(std::numeric_limits<int>::max())) {
        out->status = TAIYIN_ERROR_INVALID_ARGUMENT;
        if (diagnostic) {
            diagnostic->status = out->status;
        }
        return out->status;
    }
    if ((apparent_flags & TAIYIN_APPARENT_TOPOCENTRIC) != 0u
        && !native_cartesian_state_is_finite(config.options.observer_offset)) {
        out->status = TAIYIN_ERROR_INVALID_ARGUMENT;
        if (diagnostic) {
            diagnostic->status = out->status;
            diagnostic->target_id = body_id;
            diagnostic->center_id = request.observer_id;
            diagnostic->frame = internal::EphemerisFrame::IcrfJ2000Equatorial;
            diagnostic->jd_tdb = request.jd_tdb;
        }
        return out->status;
    }

    double observer_offset_pos[3] = { 0.0, 0.0, 0.0 };
    double observer_offset_vel[3] = { 0.0, 0.0, 0.0 };
    double observer_offset_acc[3] = { 0.0, 0.0, 0.0 };
    if ((apparent_flags & TAIYIN_APPARENT_TOPOCENTRIC) != 0u) {
        runtime_vector_to_array3(config.options.observer_offset.position_au, observer_offset_pos);
        runtime_vector_to_array3(config.options.observer_offset.velocity_au_per_day, observer_offset_vel);
        runtime_vector_to_array3(config.options.observer_offset.acceleration_au_per_day2, observer_offset_acc);
    }

    RuntimeCompiledBlockData target_data;
    target_data.context = context;
    target_data.body_id = body_id;
    target_data.center_id = request.center_id;
    target_data.preferred_components = runtime_components_for_apparent_flags(apparent_flags);
    RuntimeCompiledBlockData observer_data;
    observer_data.context = context;
    observer_data.body_id = request.observer_id;
    observer_data.center_id = request.center_id;
    observer_data.preferred_components = runtime_observer_components_for_apparent_flags(apparent_flags);
    internal::CompiledEphemerisBlock target_block = make_runtime_compiled_block(&target_data);
    internal::CompiledEphemerisBlock observer_block = make_runtime_compiled_block(&observer_data);

    std::vector<RuntimeCompiledBlockData> deflector_data;
    std::vector<internal::CompiledEphemerisBlock> deflector_blocks;
    std::vector<const internal::CompiledEphemerisBlock*> deflector_block_ptrs;
    std::vector<int> deflector_ids;
    std::vector<double> deflector_schwarzschild_radius_au;
    std::vector<double> deflector_limit;
    try {
        deflector_data.reserve(config.deflectors.size());
        deflector_blocks.reserve(config.deflectors.size());
        deflector_block_ptrs.reserve(config.deflectors.size());
        deflector_ids.reserve(config.deflectors.size());
        deflector_schwarzschild_radius_au.reserve(config.deflectors.size());
        deflector_limit.reserve(config.deflectors.size());
        for (size_t i = 0; i < config.deflectors.size(); ++i) {
            RuntimeCompiledBlockData block_data;
            block_data.context = context;
            block_data.body_id = config.deflectors[i].body_id;
            block_data.center_id = request.center_id;
            block_data.preferred_components = runtime_components_for_apparent_flags(apparent_flags);
            deflector_data.push_back(block_data);
            deflector_blocks.push_back(make_runtime_compiled_block(&deflector_data.back()));
            deflector_block_ptrs.push_back(&deflector_blocks.back());
            deflector_ids.push_back(config.deflectors[i].body_id);
            deflector_schwarzschild_radius_au.push_back(config.deflectors[i].schwarzschild_radius_au);
            deflector_limit.push_back(config.deflectors[i].limit);
        }
    } catch (...) {
        out->status = TAIYIN_ERROR_OUT_OF_MEMORY;
        if (diagnostic) {
            diagnostic->status = out->status;
        }
        return out->status;
    }

    double geometric_pos[3] = { 0.0, 0.0, 0.0 };
    double geometric_vel[3] = { 0.0, 0.0, 0.0 };
    double geometric_acc[3] = { 0.0, 0.0, 0.0 };
    double astrometric_pos[3] = { 0.0, 0.0, 0.0 };
    double astrometric_vel[3] = { 0.0, 0.0, 0.0 };
    double astrometric_acc[3] = { 0.0, 0.0, 0.0 };
    double deflected_pos[3] = { 0.0, 0.0, 0.0 };
    double deflected_vel[3] = { 0.0, 0.0, 0.0 };
    double deflected_acc[3] = { 0.0, 0.0, 0.0 };
    double aberrated_pos[3] = { 0.0, 0.0, 0.0 };
    double aberrated_vel[3] = { 0.0, 0.0, 0.0 };
    double aberrated_acc[3] = { 0.0, 0.0, 0.0 };
    double apparent_pos[3] = { 0.0, 0.0, 0.0 };
    double apparent_vel[3] = { 0.0, 0.0, 0.0 };
    double apparent_acc[3] = { 0.0, 0.0, 0.0 };
    double light_time_rate = 0.0;
    double light_time_acceleration = 0.0;
    int light_time_iterations = 0;

    const bool ok = calc_apparent(
        request.jd_tdb,
        resolve_jd_tt(request),
        body_id,
        &target_block,
        request.observer_id,
        &observer_block,
        observer_offset_pos,
        observer_offset_vel,
        observer_offset_acc,
        static_cast<int>(config.deflectors.size()),
        config.solar_deflector_index,
        deflector_ids.empty() ? 0 : deflector_ids.data(),
        deflector_block_ptrs.empty() ? 0 : deflector_block_ptrs.data(),
        deflector_schwarzschild_radius_au.empty() ? 0 : deflector_schwarzschild_radius_au.data(),
        deflector_limit.empty() ? 0 : deflector_limit.data(),
        apparent_flags,
        config.options.output_frame_id,
        config.options.light_time_method_id,
        config.options.shapiro_delay_model_id,
        config.options.aberration_model_id,
        config.options.deflection_model_id,
        config.precession.model_id,
        config.nutation.model_id,
        config.resolved_obliquity_model_id,
        config.resolved_frame_route_id,
        config.options.celestial_pole_offset_dx_rad,
        config.options.celestial_pole_offset_dy_rad,
        config.options.celestial_pole_offset_dx_rate_rad_per_day,
        config.options.celestial_pole_offset_dy_rate_rad_per_day,
        config.options.max_light_time_iterations,
        config.options.light_time_tolerance_days,
        config.options.matrix_derivative_step_days,
        geometric_pos,
        geometric_vel,
        geometric_acc,
        astrometric_pos,
        astrometric_vel,
        astrometric_acc,
        deflected_pos,
        deflected_vel,
        deflected_acc,
        aberrated_pos,
        aberrated_vel,
        aberrated_acc,
        apparent_pos,
        apparent_vel,
        apparent_acc,
        &out->longitude_rad,
        &out->latitude_rad,
        &out->distance_au,
        0,
        0,
        0,
        0,
        0,
        0,
        &out->light_time_days,
        &light_time_rate,
        &light_time_acceleration,
        &light_time_iterations,
        config.options.custom_output_frame_evaluator,
        config.options.custom_output_frame_data);

    if (!ok) {
        EphemerisEvalDiagnostic failure_diagnostic;
        failure_diagnostic.status = TAIYIN_EPHEMERIS_ERROR_EVAL_FAILED;
        failure_diagnostic.target_id = body_id;
        failure_diagnostic.center_id = request.observer_id;
        failure_diagnostic.frame = internal::EphemerisFrame::IcrfJ2000Equatorial;
        failure_diagnostic.jd_tdb = request.jd_tdb;
        Status failure_status = failure_diagnostic.status;
        if (target_data.evaluated && target_data.last_status != TAIYIN_STATUS_OK) {
            failure_status = target_data.last_status;
            failure_diagnostic = target_data.last_diagnostic;
        } else if (observer_data.evaluated && observer_data.last_status != TAIYIN_STATUS_OK) {
            failure_status = observer_data.last_status;
            failure_diagnostic = observer_data.last_diagnostic;
        } else {
            for (size_t i = 0; i < deflector_data.size(); ++i) {
                if (deflector_data[i].evaluated && deflector_data[i].last_status != TAIYIN_STATUS_OK) {
                    failure_status = deflector_data[i].last_status;
                    failure_diagnostic = deflector_data[i].last_diagnostic;
                    break;
                }
            }
        }
        out->status = failure_status;
        out->diagnostic = failure_diagnostic;
        copy_ephemeris_diagnostic(diagnostic, failure_diagnostic);
        return failure_status;
    }

    out->geometric_state.position_au = runtime_vector_from_array3(geometric_pos);
    out->geometric_state.velocity_au_per_day = runtime_vector_from_array3(geometric_vel);
    out->geometric_state.acceleration_au_per_day2 = runtime_vector_from_array3(geometric_acc);
    out->apparent_state.position_au = runtime_vector_from_array3(apparent_pos);
    out->apparent_state.velocity_au_per_day = runtime_vector_from_array3(apparent_vel);
    out->apparent_state.acceleration_au_per_day2 = runtime_vector_from_array3(apparent_acc);
    out->cache_hit = target_data.cache_hit && observer_data.cache_hit;
    for (size_t i = 0; i < deflector_data.size(); ++i) {
        out->cache_hit = out->cache_hit && deflector_data[i].cache_hit;
    }

    if (!std::isfinite(out->longitude_rad)
        || !std::isfinite(out->latitude_rad)
        || !std::isfinite(out->distance_au)
        || out->distance_au <= 0.0) {
        EphemerisEvalDiagnostic eval_diagnostic;
        eval_diagnostic.status = TAIYIN_EPHEMERIS_ERROR_EVAL_FAILED;
        eval_diagnostic.target_id = body_id;
        eval_diagnostic.center_id = request.observer_id;
        eval_diagnostic.frame = internal::EphemerisFrame::IcrfJ2000Equatorial;
        eval_diagnostic.jd_tdb = request.jd_tdb;
        out->status = eval_diagnostic.status;
        out->diagnostic = eval_diagnostic;
        copy_ephemeris_diagnostic(diagnostic, eval_diagnostic);
        return eval_diagnostic.status;
    }

    out->status = TAIYIN_STATUS_OK;
    out->diagnostic = target_data.last_diagnostic;
    if (diagnostic) {
        *diagnostic = EphemerisEvalDiagnostic();
        diagnostic->status = TAIYIN_STATUS_OK;
        diagnostic->target_id = body_id;
        diagnostic->center_id = request.observer_id;
        diagnostic->frame = internal::EphemerisFrame::IcrfJ2000Equatorial;
        diagnostic->jd_tdb = request.jd_tdb;
    }
    return TAIYIN_STATUS_OK;
}

void state_from_arrays(
    const double* positions,
    const double* velocities,
    const double* accelerations,
    size_t index,
    CartesianState* out
) noexcept {
    if (!out) {
        return;
    }
    const double* position = positions ? positions + index * 3 : 0;
    const double* velocity = velocities ? velocities + index * 3 : 0;
    const double* acceleration = accelerations ? accelerations + index * 3 : 0;
    out->position_au = runtime_vector_from_array3(position);
    out->velocity_au_per_day = runtime_vector_from_array3(velocity);
    out->acceleration_au_per_day2 = runtime_vector_from_array3(acceleration);
}

Status compute_body_batch(
    const RuntimeStateEvalContext& context,
    const MajorBodyApparentBatchRequest& request,
    const ResolvedApparentConfig& config,
    MajorBodyApparentBatchResult* out,
    EphemerisEvalDiagnostic* diagnostic
) noexcept {
    if (!out || !request.body_ids || request.body_count == 0) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }

    const uint32_t apparent_flags = config.options.flags | TAIYIN_APPARENT_SPHERICAL;
    const bool needs_deflectors = (apparent_flags & (
        TAIYIN_APPARENT_ABERRATION
        | TAIYIN_APPARENT_DEFLECTION
        | TAIYIN_APPARENT_SHAPIRO_DELAY)) != 0u;
    if ((apparent_flags & TAIYIN_APPARENT_SHAPIRO_DELAY) != 0u
        && (apparent_flags & TAIYIN_APPARENT_LIGHT_TIME) == 0u) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    if (needs_deflectors
        && (config.deflectors.empty()
            || config.solar_deflector_index < 0
            || static_cast<size_t>(config.solar_deflector_index) >= config.deflectors.size())) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    if (config.deflectors.size() > static_cast<size_t>(std::numeric_limits<int>::max())) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    if ((apparent_flags & TAIYIN_APPARENT_TOPOCENTRIC) != 0u
        && !native_cartesian_state_is_finite(config.options.observer_offset)) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }

    double observer_offset_pos[3] = { 0.0, 0.0, 0.0 };
    double observer_offset_vel[3] = { 0.0, 0.0, 0.0 };
    double observer_offset_acc[3] = { 0.0, 0.0, 0.0 };
    if ((apparent_flags & TAIYIN_APPARENT_TOPOCENTRIC) != 0u) {
        runtime_vector_to_array3(config.options.observer_offset.position_au, observer_offset_pos);
        runtime_vector_to_array3(config.options.observer_offset.velocity_au_per_day, observer_offset_vel);
        runtime_vector_to_array3(config.options.observer_offset.acceleration_au_per_day2, observer_offset_acc);
    }

    RuntimeCompiledBlockData observer_data;
    observer_data.context = context;
    observer_data.body_id = request.observer_id;
    observer_data.center_id = request.center_id;
    observer_data.preferred_components = runtime_observer_components_for_apparent_flags(apparent_flags);
    internal::CompiledEphemerisBlock observer_block = make_runtime_compiled_block(&observer_data);

    std::vector<RuntimeCompiledBlockData> target_data;
    std::vector<internal::CompiledEphemerisBlock> target_blocks;
    std::vector<const internal::CompiledEphemerisBlock*> target_block_ptrs;
    std::vector<RuntimeCompiledBlockData> deflector_data;
    std::vector<internal::CompiledEphemerisBlock> deflector_blocks;
    std::vector<const internal::CompiledEphemerisBlock*> deflector_block_ptrs;
    std::vector<int> deflector_ids;
    std::vector<double> deflector_schwarzschild_radius_au;
    std::vector<double> deflector_limit;

    const bool need_velocity = (apparent_flags & (TAIYIN_APPARENT_VELOCITY | TAIYIN_APPARENT_ACCELERATION)) != 0u;
    const bool need_acceleration = (apparent_flags & TAIYIN_APPARENT_ACCELERATION) != 0u;
    std::vector<double> geometric_pos;
    std::vector<double> geometric_vel;
    std::vector<double> geometric_acc;
    std::vector<double> apparent_pos;
    std::vector<double> apparent_vel;
    std::vector<double> apparent_acc;
    std::vector<double> longitude_rad;
    std::vector<double> latitude_rad;
    std::vector<double> distance_au;
    std::vector<double> light_time_days;
    try {
        target_data.reserve(request.body_count);
        target_blocks.reserve(request.body_count);
        target_block_ptrs.reserve(request.body_count);
        for (size_t i = 0; i < request.body_count; ++i) {
            RuntimeCompiledBlockData block_data;
            block_data.context = context;
            block_data.body_id = request.body_ids[i];
            block_data.center_id = request.center_id;
            block_data.preferred_components = runtime_components_for_apparent_flags(apparent_flags);
            target_data.push_back(block_data);
            target_blocks.push_back(make_runtime_compiled_block(&target_data.back()));
            target_block_ptrs.push_back(&target_blocks.back());
        }

        deflector_data.reserve(config.deflectors.size());
        deflector_blocks.reserve(config.deflectors.size());
        deflector_block_ptrs.reserve(config.deflectors.size());
        deflector_ids.reserve(config.deflectors.size());
        deflector_schwarzschild_radius_au.reserve(config.deflectors.size());
        deflector_limit.reserve(config.deflectors.size());
        for (size_t i = 0; i < config.deflectors.size(); ++i) {
            RuntimeCompiledBlockData block_data;
            block_data.context = context;
            block_data.body_id = config.deflectors[i].body_id;
            block_data.center_id = request.center_id;
            block_data.preferred_components = runtime_components_for_apparent_flags(apparent_flags);
            deflector_data.push_back(block_data);
            deflector_blocks.push_back(make_runtime_compiled_block(&deflector_data.back()));
            deflector_block_ptrs.push_back(&deflector_blocks.back());
            deflector_ids.push_back(config.deflectors[i].body_id);
            deflector_schwarzschild_radius_au.push_back(config.deflectors[i].schwarzschild_radius_au);
            deflector_limit.push_back(config.deflectors[i].limit);
        }

        geometric_pos.assign(request.body_count * 3, 0.0);
        apparent_pos.assign(request.body_count * 3, 0.0);
        longitude_rad.assign(request.body_count, 0.0);
        latitude_rad.assign(request.body_count, 0.0);
        distance_au.assign(request.body_count, 0.0);
        light_time_days.assign(request.body_count, 0.0);
        if (need_velocity) {
            geometric_vel.assign(request.body_count * 3, 0.0);
            apparent_vel.assign(request.body_count * 3, 0.0);
        }
        if (need_acceleration) {
            geometric_acc.assign(request.body_count * 3, 0.0);
            apparent_acc.assign(request.body_count * 3, 0.0);
        }
    } catch (...) {
        return TAIYIN_ERROR_OUT_OF_MEMORY;
    }

    const bool ok = calc_apparent_batch(
        request.jd_tdb,
        resolve_jd_tt(request),
        static_cast<int>(request.body_count),
        request.body_ids,
        target_block_ptrs.empty() ? 0 : target_block_ptrs.data(),
        request.observer_id,
        &observer_block,
        observer_offset_pos,
        observer_offset_vel,
        observer_offset_acc,
        static_cast<int>(config.deflectors.size()),
        config.solar_deflector_index,
        deflector_ids.empty() ? 0 : deflector_ids.data(),
        deflector_block_ptrs.empty() ? 0 : deflector_block_ptrs.data(),
        deflector_schwarzschild_radius_au.empty() ? 0 : deflector_schwarzschild_radius_au.data(),
        deflector_limit.empty() ? 0 : deflector_limit.data(),
        apparent_flags,
        config.options.output_frame_id,
        config.options.light_time_method_id,
        config.options.shapiro_delay_model_id,
        config.options.aberration_model_id,
        config.options.deflection_model_id,
        config.precession.model_id,
        config.nutation.model_id,
        config.resolved_obliquity_model_id,
        config.resolved_frame_route_id,
        config.options.celestial_pole_offset_dx_rad,
        config.options.celestial_pole_offset_dy_rad,
        config.options.celestial_pole_offset_dx_rate_rad_per_day,
        config.options.celestial_pole_offset_dy_rate_rad_per_day,
        config.options.max_light_time_iterations,
        config.options.light_time_tolerance_days,
        config.options.matrix_derivative_step_days,
        geometric_pos.empty() ? 0 : geometric_pos.data(),
        geometric_vel.empty() ? 0 : geometric_vel.data(),
        geometric_acc.empty() ? 0 : geometric_acc.data(),
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        apparent_pos.empty() ? 0 : apparent_pos.data(),
        apparent_vel.empty() ? 0 : apparent_vel.data(),
        apparent_acc.empty() ? 0 : apparent_acc.data(),
        longitude_rad.empty() ? 0 : longitude_rad.data(),
        latitude_rad.empty() ? 0 : latitude_rad.data(),
        distance_au.empty() ? 0 : distance_au.data(),
        0,
        0,
        0,
        0,
        0,
        0,
        light_time_days.empty() ? 0 : light_time_days.data(),
        0,
        0,
        0,
        config.options.custom_output_frame_evaluator,
        config.options.custom_output_frame_data);
    if (!ok) {
        return TAIYIN_EPHEMERIS_ERROR_EVAL_FAILED;
    }

    *out = MajorBodyApparentBatchResult();
    out->body_count = request.body_count;
    out->status = TAIYIN_STATUS_OK;
    for (size_t i = 0; i < request.body_count; ++i) {
        MajorBodyApparentPosition& body = out->bodies[i];
        body = MajorBodyApparentPosition();
        body.body_id = request.body_ids[i];
        body.body_mask_bit = mask_bit_for_body_id(request.body_ids[i]);
        body.status = TAIYIN_STATUS_OK;
        state_from_arrays(geometric_pos.data(), geometric_vel.empty() ? 0 : geometric_vel.data(), geometric_acc.empty() ? 0 : geometric_acc.data(), i, &body.geometric_state);
        state_from_arrays(apparent_pos.data(), apparent_vel.empty() ? 0 : apparent_vel.data(), apparent_acc.empty() ? 0 : apparent_acc.data(), i, &body.apparent_state);
        body.longitude_rad = longitude_rad[i];
        body.latitude_rad = latitude_rad[i];
        body.distance_au = distance_au[i];
        body.light_time_days = light_time_days[i];
        body.cache_hit = observer_data.cache_hit && target_data[i].cache_hit;
        for (size_t j = 0; j < deflector_data.size(); ++j) {
            body.cache_hit = body.cache_hit && deflector_data[j].cache_hit;
        }
        body.diagnostic = target_data[i].last_diagnostic;
        if (!std::isfinite(body.longitude_rad)
            || !std::isfinite(body.latitude_rad)
            || !std::isfinite(body.distance_au)
            || body.distance_au <= 0.0) {
            body.status = TAIYIN_EPHEMERIS_ERROR_EVAL_FAILED;
            out->status = body.status;
            out->failed_body_id = body.body_id;
            if (diagnostic) {
                diagnostic->status = body.status;
                diagnostic->target_id = body.body_id;
                diagnostic->center_id = request.observer_id;
                diagnostic->frame = internal::EphemerisFrame::IcrfJ2000Equatorial;
                diagnostic->jd_tdb = request.jd_tdb;
            }
            return body.status;
        }
    }
    if (diagnostic) {
        diagnostic->status = TAIYIN_STATUS_OK;
    }
    return TAIYIN_STATUS_OK;
}

Status eval_major_body_apparent_batch_in_context(
    const RuntimeStateEvalContext& context,
    const MajorBodyApparentBatchRequest& request,
    MajorBodyApparentBatchResult* out,
    EphemerisEvalDiagnostic* diagnostic
) noexcept {
    if (out) {
        *out = MajorBodyApparentBatchResult();
    }
    if (diagnostic) {
        *diagnostic = EphemerisEvalDiagnostic();
        diagnostic->target_id = request.observer_id;
        diagnostic->center_id = request.center_id;
        diagnostic->frame = internal::EphemerisFrame::IcrfJ2000Equatorial;
        diagnostic->jd_tdb = request.jd_tdb;
    }
    if (!out) {
        if (diagnostic) {
            diagnostic->status = TAIYIN_ERROR_INVALID_ARGUMENT;
        }
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    if (!split_julian_date_is_finite(request.jd_tdb)
        || !request.body_ids
        || request.body_count == 0
        || request.body_count > TAIYIN_MAJOR_BODY_COUNT) {
        if (diagnostic) {
            diagnostic->status = TAIYIN_ERROR_INVALID_ARGUMENT;
        }
        out->status = TAIYIN_ERROR_INVALID_ARGUMENT;
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    ResolvedApparentConfig config;
    const Status config_status = resolve_apparent_config(request, &config);
    if (config_status != TAIYIN_STATUS_OK) {
        if (diagnostic) {
            diagnostic->status = config_status;
        }
        out->status = config_status;
        return config_status;
    }
    if (!context.use_global && !context.service) {
        if (diagnostic) {
            diagnostic->status = TAIYIN_ERROR_INVALID_ARGUMENT;
        }
        out->status = TAIYIN_ERROR_INVALID_ARGUMENT;
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }

    EphemerisEvalDiagnostic observer_diagnostic;
    EphemerisResult observer;
    Status observer_status = eval_runtime_body_state(
        context,
        request.observer_id,
        request.center_id,
        request.jd_tdb,
        internal::EPHEMERIS_BLOCK_COMPONENT_STATE,
        context.route_rule_id,
        context.route_rules,
        &observer,
        &observer_diagnostic);
    if (observer_status != TAIYIN_STATUS_OK) {
        out->status = observer_status;
        out->failed_body_id = request.observer_id;
        copy_ephemeris_diagnostic(diagnostic, observer_diagnostic);
        return observer_status;
    }

    EphemerisEvalDiagnostic batch_diagnostic;
    const Status batch_status = compute_body_batch(
        context,
        request,
        config,
        out,
        &batch_diagnostic);
    if (batch_status == TAIYIN_STATUS_OK) {
        if (diagnostic) {
            *diagnostic = batch_diagnostic;
        }
        return TAIYIN_STATUS_OK;
    }

    *out = MajorBodyApparentBatchResult();

    Status first_error = TAIYIN_STATUS_OK;
    EphemerisEvalDiagnostic first_diagnostic;
    int first_failed_body_id = 0;

    for (size_t i = 0; i < request.body_count; ++i) {
        const int current_body_id = request.body_ids[i];
        MajorBodyApparentPosition* position = &out->bodies[out->body_count++];
        EphemerisEvalDiagnostic body_diagnostic;
        const Status body_status = compute_one_body(
            context,
            request,
            config,
            current_body_id,
            observer,
            position,
            &body_diagnostic);
        if (body_status != TAIYIN_STATUS_OK && first_error == TAIYIN_STATUS_OK) {
            first_error = body_status;
            first_diagnostic = body_diagnostic;
            first_failed_body_id = current_body_id;
        }
    }

    out->status = first_error;
    out->failed_body_id = first_failed_body_id;
    if (first_error != TAIYIN_STATUS_OK) {
        copy_ephemeris_diagnostic(diagnostic, first_diagnostic);
    } else if (diagnostic) {
        diagnostic->status = TAIYIN_STATUS_OK;
    }
    return first_error;
}

}  // namespace

AstroModelContext::AstroModelContext() noexcept
    : tdb_model_id(dispatch::TDB_FAST_PERIODIC),
      precession_model_id(dispatch::MODEL_SELECTION_DEFAULT),
      nutation_model_id(dispatch::MODEL_SELECTION_DEFAULT),
      obliquity_model_id(0),
      frame_route_id(dispatch::FRAME_ROUTE_EQUINOX) {}

ApparentDeflector::ApparentDeflector() noexcept
    : body_id(0), schwarzschild_radius_au(0.0), limit(0.0) {}

ApparentOptions::ApparentOptions() noexcept
    : flags(TAIYIN_APPARENT_LIGHT_TIME | TAIYIN_APPARENT_SPHERICAL),
      output_frame_id(TAIYIN_APPARENT_FRAME_TRUE_ECLIPTIC_OF_DATE),
      light_time_method_id(0),
      shapiro_delay_model_id(0),
      aberration_model_id(0),
      deflection_model_id(0),
      max_light_time_iterations(8),
      light_time_tolerance_days(1.0e-13),
      matrix_derivative_step_days(1.0e-3),
      celestial_pole_offset_dx_rad(0.0),
      celestial_pole_offset_dy_rad(0.0),
      celestial_pole_offset_dx_rate_rad_per_day(0.0),
      celestial_pole_offset_dy_rate_rad_per_day(0.0),
      custom_output_frame_evaluator(0),
      custom_output_frame_data(0),
      model_context(0),
      observer_offset(),
      deflectors(0),
      deflector_count(0),
      solar_deflector_index(-1) {}

MajorBodyApparentBatchRequest::MajorBodyApparentBatchRequest() noexcept
    : jd_tdb(),
      jd_tt(),
      observer_id(TAIYIN_BODY_EARTH),
      center_id(TAIYIN_BODY_SUN),
      body_ids(0),
      body_count(0),
      options(0) {}

AstroModelContext get_global_astro_model_context() noexcept {
    return snapshot_global_astro_model_context();
}

Status set_global_astro_model_context(const AstroModelContext& context) noexcept {
    GlobalApparentConfigManager& manager = global_apparent_config_manager();
    std::lock_guard<std::mutex> lock(manager.mutex);
    manager.model_context = context;
    return TAIYIN_STATUS_OK;
}

void reset_global_astro_model_context() noexcept {
    GlobalApparentConfigManager& manager = global_apparent_config_manager();
    std::lock_guard<std::mutex> lock(manager.mutex);
    manager.model_context = AstroModelContext();
}

ApparentOptions get_global_apparent_options() noexcept {
    const GlobalApparentConfigSnapshot snapshot = snapshot_global_apparent_config();
    return snapshot.apparent_options;
}

Status set_global_apparent_options(const ApparentOptions& options) noexcept {
    GlobalApparentConfigManager& manager = global_apparent_config_manager();
    std::lock_guard<std::mutex> lock(manager.mutex);
    manager.apparent_options = options;
    manager.apparent_options.model_context = 0;
    manager.apparent_options.deflectors = 0;
    manager.apparent_options.deflector_count = 0;
    manager.apparent_options.solar_deflector_index = -1;
    return TAIYIN_STATUS_OK;
}

void reset_global_apparent_options() noexcept {
    GlobalApparentConfigManager& manager = global_apparent_config_manager();
    std::lock_guard<std::mutex> lock(manager.mutex);
    manager.apparent_options = ApparentOptions();
}

Status set_global_apparent_deflectors(
    const ApparentDeflector* deflectors,
    size_t deflector_count,
    int solar_deflector_index
) noexcept {
    if ((!deflectors && deflector_count > 0) || !valid_solar_deflector_index(deflector_count, solar_deflector_index)) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }

    std::vector<ApparentDeflector> replacement;
    try {
        for (size_t i = 0; i < deflector_count; ++i) {
            replacement.push_back(deflectors[i]);
        }
    } catch (...) {
        return TAIYIN_ERROR_OUT_OF_MEMORY;
    }

    GlobalApparentConfigManager& manager = global_apparent_config_manager();
    std::lock_guard<std::mutex> lock(manager.mutex);
    manager.deflectors.swap(replacement);
    manager.solar_deflector_index = solar_deflector_index;
    return TAIYIN_STATUS_OK;
}

size_t get_global_apparent_deflector_count() noexcept {
    GlobalApparentConfigManager& manager = global_apparent_config_manager();
    std::lock_guard<std::mutex> lock(manager.mutex);
    return manager.deflectors.size();
}

size_t get_global_apparent_deflectors(
    ApparentDeflector* out,
    size_t capacity,
    int* out_solar_deflector_index
) noexcept {
    GlobalApparentConfigManager& manager = global_apparent_config_manager();
    std::lock_guard<std::mutex> lock(manager.mutex);
    if (out_solar_deflector_index) {
        *out_solar_deflector_index = manager.solar_deflector_index;
    }
    const size_t count = manager.deflectors.size();
    const size_t copy_count = capacity < count ? capacity : count;
    if (out) {
        for (size_t i = 0; i < copy_count; ++i) {
            out[i] = manager.deflectors[i];
        }
    }
    return count;
}

void reset_global_apparent_deflectors() noexcept {
    GlobalApparentConfigManager& manager = global_apparent_config_manager();
    std::lock_guard<std::mutex> lock(manager.mutex);
    manager.deflectors.clear();
    manager.solar_deflector_index = -1;
}

int major_body_id_for_mask_bit(uint32_t mask_bit) noexcept {
    for (size_t i = 0; i < TAIYIN_MAJOR_BODY_COUNT; ++i) {
        if (MAJOR_BODY_SPECS[i].mask_bit == mask_bit) {
            return MAJOR_BODY_SPECS[i].body_id;
        }
    }
    return 0;
}

const char* major_body_name_for_id(int body_id) noexcept {
    for (size_t i = 0; i < TAIYIN_MAJOR_BODY_COUNT; ++i) {
        if (MAJOR_BODY_SPECS[i].body_id == body_id) {
            return MAJOR_BODY_SPECS[i].name;
        }
    }
    return "Unknown";
}

Status eval_major_body_apparent_batch(
    EphemerisEngine* service,
    const MajorBodyApparentBatchRequest& request,
    MajorBodyApparentBatchResult* out,
    EphemerisEvalDiagnostic* diagnostic
) noexcept {
    RuntimeStateEvalContext context;
    context.service = service;
    context.use_global = false;
    return eval_major_body_apparent_batch_in_context(context, request, out, diagnostic);
}

Status eval_global_major_body_apparent_batch(
    const MajorBodyApparentBatchRequest& request,
    MajorBodyApparentBatchResult* out,
    EphemerisEvalDiagnostic* diagnostic
) noexcept {
    RuntimeStateEvalContext context;
    context.service = 0;
    context.use_global = true;
    return eval_major_body_apparent_batch_in_context(context, request, out, diagnostic);
}

}  // namespace runtime
}  // namespace taiyin
