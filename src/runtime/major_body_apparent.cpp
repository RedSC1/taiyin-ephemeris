#include "taiyin/runtime/major_body_apparent.h"

#include "taiyin/apparent_position.h"
#include "taiyin/body_id.h"
#include "taiyin/coordinates.h"
#include "taiyin/corrections.h"
#include "taiyin/dispatch.h"
#include "taiyin/runtime/taiyin_runtime.h"
#include "taiyin/vector3.h"

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

struct EvalContext {
    EphemerisService* service;
    bool use_global;
};

struct RuntimeCompiledBlockData {
    EvalContext context;
    int body_id;
    int center_id;
    mutable TaiyinStatus last_status;
    mutable EphemerisEvalDiagnostic last_diagnostic;
    mutable bool cache_hit;
    mutable bool evaluated;

    RuntimeCompiledBlockData() noexcept
        : context(),
          body_id(0),
          center_id(0),
          last_status(TAIYIN_STATUS_OK),
          last_diagnostic(),
          cache_hit(true),
          evaluated(false) {}
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

TaiyinStatus copy_explicit_deflectors(
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

TaiyinStatus snapshot_global_deflectors(
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
    | TAIYIN_APPARENT_SHAPIRO_DELAY;

TaiyinStatus resolve_apparent_config(
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
            const TaiyinStatus deflector_status = copy_explicit_deflectors(
                out->options.deflectors,
                out->options.deflector_count,
                out->options.solar_deflector_index,
                &out->deflectors);
            if (deflector_status != TAIYIN_STATUS_OK) {
                return deflector_status;
            }
            out->solar_deflector_index = out->options.solar_deflector_index;
        } else {
            const TaiyinStatus deflector_status = snapshot_global_deflectors(
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
        const TaiyinStatus deflector_status = snapshot_global_deflectors(
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

Vector3 zero_vector() noexcept {
    Vector3 out;
    out.x = 0.0;
    out.y = 0.0;
    out.z = 0.0;
    return out;
}

bool finite_vector(const Vector3& value) noexcept {
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

bool finite_state(const CartesianState& state) noexcept {
    return finite_vector(state.position_au)
        && finite_vector(state.velocity_au_per_day)
        && finite_vector(state.acceleration_au_per_day2);
}

void set_zero_state(CartesianState* out) noexcept {
    if (!out) {
        return;
    }
    out->position_au = zero_vector();
    out->velocity_au_per_day = zero_vector();
    out->acceleration_au_per_day2 = zero_vector();
}

EphemerisRequest make_state_request(
    int target_id,
    int center_id,
    double jd_tdb
) noexcept {
    EphemerisRequest request;
    request.target_id = target_id;
    request.center_id = center_id;
    request.frame = internal::EphemerisFrame::IcrfJ2000Equatorial;
    request.jd_tdb = jd_tdb;
    return request;
}

TaiyinStatus eval_state_in_context(
    const EvalContext& context,
    const EphemerisRequest& request,
    EphemerisResult* out,
    EphemerisEvalDiagnostic* diagnostic
) noexcept {
    if (request.target_id == request.center_id) {
        if (!out) {
            return TAIYIN_ERROR_INVALID_ARGUMENT;
        }
        *out = EphemerisResult();
        set_zero_state(&out->state);
        out->descriptor.target_id = request.target_id;
        out->descriptor.center_id = request.center_id;
        out->descriptor.frame = request.frame;
        out->descriptor.jd_tdb_start = -std::numeric_limits<double>::infinity();
        out->descriptor.jd_tdb_end = std::numeric_limits<double>::infinity();
        out->cache_hit = true;
        if (diagnostic) {
            *diagnostic = EphemerisEvalDiagnostic();
            diagnostic->status = TAIYIN_STATUS_OK;
            diagnostic->target_id = request.target_id;
            diagnostic->center_id = request.center_id;
            diagnostic->frame = request.frame;
            diagnostic->jd_tdb = request.jd_tdb;
        }
        return TAIYIN_STATUS_OK;
    }

    return context.use_global
        ? eval_global_ephemeris_state(request, out, diagnostic)
        : (context.service
            ? context.service->eval_state(request, out, diagnostic)
            : TAIYIN_ERROR_INVALID_ARGUMENT);
}

TaiyinStatus eval_body_state(
    const EvalContext& context,
    int body_id,
    int center_id,
    double jd_tdb,
    EphemerisResult* out,
    EphemerisEvalDiagnostic* diagnostic
) noexcept {
    return eval_state_in_context(
        context,
        make_state_request(body_id, center_id, jd_tdb),
        out,
        diagnostic);
}

bool eval_runtime_block_state(
    double jd_tdb,
    const void* data,
    EphemerisResult* out
) noexcept {
    const RuntimeCompiledBlockData* block_data = static_cast<const RuntimeCompiledBlockData*>(data);
    if (!block_data || !out) {
        return false;
    }

    EphemerisEvalDiagnostic diagnostic;
    const TaiyinStatus status = eval_body_state(
        block_data->context,
        block_data->body_id,
        block_data->center_id,
        jd_tdb,
        out,
        &diagnostic);
    block_data->last_status = status;
    block_data->last_diagnostic = diagnostic;
    block_data->evaluated = true;
    if (status != TAIYIN_STATUS_OK) {
        return false;
    }
    block_data->cache_hit = block_data->cache_hit && out->cache_hit;
    return finite_state(out->state);
}

bool runtime_block_position(double jd_tdb, const void* data, Vector3* out) noexcept {
    if (!out) {
        return false;
    }
    EphemerisResult result;
    if (!eval_runtime_block_state(jd_tdb, data, &result)) {
        return false;
    }
    *out = result.state.position_au;
    return finite_vector(*out);
}

bool runtime_block_velocity(double jd_tdb, const void* data, Vector3* out) noexcept {
    if (!out) {
        return false;
    }
    EphemerisResult result;
    if (!eval_runtime_block_state(jd_tdb, data, &result)) {
        return false;
    }
    *out = result.state.velocity_au_per_day;
    return finite_vector(*out);
}

bool runtime_block_acceleration(double jd_tdb, const void* data, Vector3* out) noexcept {
    if (!out) {
        return false;
    }
    EphemerisResult result;
    if (!eval_runtime_block_state(jd_tdb, data, &result)) {
        return false;
    }
    *out = result.state.acceleration_au_per_day2;
    return finite_vector(*out);
}

internal::CompiledEphemerisBlock make_runtime_compiled_block(RuntimeCompiledBlockData* data) noexcept {
    internal::CompiledEphemerisBlock block;
    block.data = data;
    block.bytes = sizeof(RuntimeCompiledBlockData);
    block.position = &runtime_block_position;
    block.velocity = &runtime_block_velocity;
    block.acceleration = &runtime_block_acceleration;
    block.format = internal::EphemerisBlockFormat::Custom;
    return block;
}

void copy_diagnostic(EphemerisEvalDiagnostic* dst, const EphemerisEvalDiagnostic& src) noexcept {
    if (dst) {
        *dst = src;
    }
}

uint32_t mask_bit_for_body_id(int body_id) noexcept {
    for (size_t i = 0; i < TAIYIN_MAJOR_BODY_COUNT; ++i) {
        if (MAJOR_BODY_SPECS[i].body_id == body_id) {
            return MAJOR_BODY_SPECS[i].mask_bit;
        }
    }
    return 0u;
}

Vector3 vector_from_array3(const double values[3]) noexcept {
    Vector3 out;
    out.x = values ? values[0] : 0.0;
    out.y = values ? values[1] : 0.0;
    out.z = values ? values[2] : 0.0;
    return out;
}

double resolve_jd_tt(const MajorBodyApparentBatchRequest& request) noexcept {
    return std::isfinite(request.jd_tt) && request.jd_tt != 0.0
        ? request.jd_tt
        : request.jd_tdb;
}

TaiyinStatus compute_one_body(
    const EvalContext& context,
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

    const uint32_t pipeline_flags = config.options.flags | TAIYIN_APPARENT_SPHERICAL;
    const bool needs_deflectors = (pipeline_flags & (
        TAIYIN_APPARENT_ABERRATION
        | TAIYIN_APPARENT_DEFLECTION
        | TAIYIN_APPARENT_SHAPIRO_DELAY)) != 0u;
    if ((pipeline_flags & TAIYIN_APPARENT_SHAPIRO_DELAY) != 0u
        && (pipeline_flags & TAIYIN_APPARENT_LIGHT_TIME) == 0u) {
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

    RuntimeCompiledBlockData target_data;
    target_data.context = context;
    target_data.body_id = body_id;
    target_data.center_id = request.center_id;
    RuntimeCompiledBlockData observer_data;
    observer_data.context = context;
    observer_data.body_id = request.observer_id;
    observer_data.center_id = request.center_id;
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

    const bool ok = taiyin_calc_apparent_flat(
        request.jd_tdb,
        resolve_jd_tt(request),
        body_id,
        &target_block,
        request.observer_id,
        &observer_block,
        0,
        0,
        0,
        static_cast<int>(config.deflectors.size()),
        config.solar_deflector_index,
        deflector_ids.empty() ? 0 : deflector_ids.data(),
        deflector_block_ptrs.empty() ? 0 : deflector_block_ptrs.data(),
        deflector_schwarzschild_radius_au.empty() ? 0 : deflector_schwarzschild_radius_au.data(),
        deflector_limit.empty() ? 0 : deflector_limit.data(),
        pipeline_flags,
        config.options.output_frame_id,
        config.options.light_time_method_id,
        config.options.shapiro_delay_model_id,
        config.options.aberration_model_id,
        config.options.deflection_model_id,
        config.precession.model_id,
        config.nutation.model_id,
        config.resolved_obliquity_model_id,
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
        &light_time_iterations);

    if (!ok) {
        EphemerisEvalDiagnostic failure_diagnostic;
        failure_diagnostic.status = TAIYIN_EPHEMERIS_ERROR_EVAL_FAILED;
        failure_diagnostic.target_id = body_id;
        failure_diagnostic.center_id = request.observer_id;
        failure_diagnostic.frame = internal::EphemerisFrame::IcrfJ2000Equatorial;
        failure_diagnostic.jd_tdb = request.jd_tdb;
        TaiyinStatus failure_status = failure_diagnostic.status;
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
        copy_diagnostic(diagnostic, failure_diagnostic);
        return failure_status;
    }

    out->geometric_state.position_au = vector_from_array3(geometric_pos);
    out->geometric_state.velocity_au_per_day = vector_from_array3(geometric_vel);
    out->geometric_state.acceleration_au_per_day2 = vector_from_array3(geometric_acc);
    out->apparent_state.position_au = vector_from_array3(apparent_pos);
    out->apparent_state.velocity_au_per_day = vector_from_array3(apparent_vel);
    out->apparent_state.acceleration_au_per_day2 = vector_from_array3(apparent_acc);
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
        copy_diagnostic(diagnostic, eval_diagnostic);
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

TaiyinStatus eval_major_body_apparent_batch_in_context(
    const EvalContext& context,
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
    if (!std::isfinite(request.jd_tdb)
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
    const TaiyinStatus config_status = resolve_apparent_config(request, &config);
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
    TaiyinStatus observer_status = eval_body_state(
        context,
        request.observer_id,
        request.center_id,
        request.jd_tdb,
        &observer,
        &observer_diagnostic);
    if (observer_status != TAIYIN_STATUS_OK) {
        out->status = observer_status;
        out->failed_body_id = request.observer_id;
        copy_diagnostic(diagnostic, observer_diagnostic);
        return observer_status;
    }

    TaiyinStatus first_error = TAIYIN_STATUS_OK;
    EphemerisEvalDiagnostic first_diagnostic;
    int first_failed_body_id = 0;

    for (size_t i = 0; i < request.body_count; ++i) {
        const int current_body_id = request.body_ids[i];
        MajorBodyApparentPosition* position = &out->bodies[out->body_count++];
        EphemerisEvalDiagnostic body_diagnostic;
        const TaiyinStatus body_status = compute_one_body(
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
        copy_diagnostic(diagnostic, first_diagnostic);
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
      model_context(0),
      deflectors(0),
      deflector_count(0),
      solar_deflector_index(-1) {}

MajorBodyApparentBatchRequest::MajorBodyApparentBatchRequest() noexcept
    : jd_tdb(0.0),
      jd_tt(0.0),
      observer_id(TAIYIN_BODY_EARTH),
      center_id(TAIYIN_BODY_SUN),
      body_ids(0),
      body_count(0),
      options(0) {}

AstroModelContext get_global_astro_model_context() noexcept {
    return snapshot_global_astro_model_context();
}

TaiyinStatus set_global_astro_model_context(const AstroModelContext& context) noexcept {
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

TaiyinStatus set_global_apparent_options(const ApparentOptions& options) noexcept {
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

TaiyinStatus set_global_apparent_deflectors(
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

TaiyinStatus eval_major_body_apparent_batch(
    EphemerisService* service,
    const MajorBodyApparentBatchRequest& request,
    MajorBodyApparentBatchResult* out,
    EphemerisEvalDiagnostic* diagnostic
) noexcept {
    EvalContext context;
    context.service = service;
    context.use_global = false;
    return eval_major_body_apparent_batch_in_context(context, request, out, diagnostic);
}

TaiyinStatus eval_global_major_body_apparent_batch(
    const MajorBodyApparentBatchRequest& request,
    MajorBodyApparentBatchResult* out,
    EphemerisEvalDiagnostic* diagnostic
) noexcept {
    EvalContext context;
    context.service = 0;
    context.use_global = true;
    return eval_major_body_apparent_batch_in_context(context, request, out, diagnostic);
}

}  // namespace runtime
}  // namespace taiyin
