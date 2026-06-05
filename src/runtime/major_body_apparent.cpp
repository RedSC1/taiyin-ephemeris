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
    TAIYIN_APPARENT_LIGHT_TIME | TAIYIN_APPARENT_SPHERICAL;

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

Matrix3x3 matrix_from_array(const double values[9]) noexcept {
    Matrix3x3 matrix = matrix3x3_identity();
    if (!values) {
        return matrix;
    }
    for (int row = 0; row < 3; ++row) {
        for (int col = 0; col < 3; ++col) {
            matrix.m[row][col] = values[row * 3 + col];
        }
    }
    return matrix;
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
    const EphemerisResult& observer,
    MajorBodyApparentPosition* out,
    EphemerisEvalDiagnostic* diagnostic
) noexcept {
    if (!out) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }

    *out = MajorBodyApparentPosition();
    out->body_id = body_id;
    out->body_mask_bit = mask_bit_for_body_id(body_id);

    EphemerisEvalDiagnostic target_diagnostic;
    EphemerisResult target_current;
    TaiyinStatus status = eval_body_state(
        context,
        body_id,
        request.center_id,
        request.jd_tdb,
        &target_current,
        &target_diagnostic);
    if (status != TAIYIN_STATUS_OK) {
        out->status = status;
        out->diagnostic = target_diagnostic;
        copy_diagnostic(diagnostic, target_diagnostic);
        return status;
    }
    if (!finite_state(target_current.state) || !finite_state(observer.state)) {
        target_diagnostic.status = TAIYIN_EPHEMERIS_ERROR_EVAL_FAILED;
        out->status = target_diagnostic.status;
        out->diagnostic = target_diagnostic;
        copy_diagnostic(diagnostic, target_diagnostic);
        return target_diagnostic.status;
    }

    out->geometric_state = cartesian_state_subtract(target_current.state, observer.state);
    out->apparent_state = out->geometric_state;
    out->cache_hit = target_current.cache_hit && observer.cache_hit;
    out->light_time_days = 0.0;

    if (vector3_norm(out->geometric_state.position_au) == 0.0) {
        target_diagnostic.status = TAIYIN_EPHEMERIS_ERROR_EVAL_FAILED;
        out->status = target_diagnostic.status;
        out->diagnostic = target_diagnostic;
        copy_diagnostic(diagnostic, target_diagnostic);
        return target_diagnostic.status;
    }

    if ((config.options.flags & TAIYIN_APPARENT_LIGHT_TIME) != 0u) {
        const int max_iterations = config.options.max_light_time_iterations > 0
            ? config.options.max_light_time_iterations
            : 1;
        const double tolerance_days = config.options.light_time_tolerance_days > 0.0
            ? config.options.light_time_tolerance_days
            : 1.0e-13;
        double light_time = vector3_norm(out->geometric_state.position_au) * TAIYIN_LIGHT_TIME_DAYS_PER_AU;
        EphemerisResult target_retarded = target_current;
        for (int iteration = 0; iteration < max_iterations; ++iteration) {
            EphemerisEvalDiagnostic retarded_diagnostic;
            status = eval_body_state(
                context,
                body_id,
                request.center_id,
                request.jd_tdb - light_time,
                &target_retarded,
                &retarded_diagnostic);
            if (status != TAIYIN_STATUS_OK) {
                out->status = status;
                out->diagnostic = retarded_diagnostic;
                copy_diagnostic(diagnostic, retarded_diagnostic);
                return status;
            }
            const CartesianState candidate = cartesian_state_subtract(target_retarded.state, observer.state);
            const double next_light_time = vector3_norm(candidate.position_au) * TAIYIN_LIGHT_TIME_DAYS_PER_AU;
            out->apparent_state = candidate;
            if (std::fabs(next_light_time - light_time) <= tolerance_days) {
                light_time = next_light_time;
                break;
            }
            light_time = next_light_time;
        }
        out->light_time_days = light_time;
        out->cache_hit = out->cache_hit && target_retarded.cache_hit;
    }

    double output_matrix_values[9];
    if (!taiyin_calc_apparent_matrices_flat(
            resolve_jd_tt(request),
            config.options.flags,
            config.options.output_frame_id,
            config.precession.model_id,
            config.nutation.model_id,
            config.resolved_obliquity_model_id,
            config.options.matrix_derivative_step_days,
            0,
            0,
            output_matrix_values,
            0,
            0,
            0,
            0,
            0,
            0)) {
        target_diagnostic.status = TAIYIN_ERROR_UNSUPPORTED;
        out->status = target_diagnostic.status;
        out->diagnostic = target_diagnostic;
        copy_diagnostic(diagnostic, target_diagnostic);
        return target_diagnostic.status;
    }
    const Matrix3x3 output_matrix = matrix_from_array(output_matrix_values);
    const Vector3 output_position = transform_position_with_matrix(
        out->apparent_state.position_au,
        output_matrix);
    cartesian_to_spherical(
        output_position,
        &out->longitude_rad,
        &out->latitude_rad,
        &out->distance_au);

    if (!std::isfinite(out->longitude_rad)
        || !std::isfinite(out->latitude_rad)
        || !std::isfinite(out->distance_au)
        || out->distance_au <= 0.0) {
        target_diagnostic.status = TAIYIN_EPHEMERIS_ERROR_EVAL_FAILED;
        out->status = target_diagnostic.status;
        out->diagnostic = target_diagnostic;
        copy_diagnostic(diagnostic, target_diagnostic);
        return target_diagnostic.status;
    }

    out->status = TAIYIN_STATUS_OK;
    out->diagnostic = target_diagnostic;
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
