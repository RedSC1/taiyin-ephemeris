#include "taiyin/runtime/native_position.h"

#include "runtime/core/native_context_checks.h"
#include "runtime/core/native_position_policy.h"
#include "runtime/core/runtime_state_block_adapter.h"

#include "taiyin/angle.h"
#include "taiyin/apparent_position.h"
#include "taiyin/body_id.h"
#include "taiyin/dispatch.h"
#include "taiyin/internal/eop.h"
#include "taiyin/runtime/runtime.h"

#include <cmath>
#include <limits>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace taiyin {
namespace runtime {
namespace {

struct ResolvedNativeCalcContext {
    ApparentOptions options;
    FieldSet fields;
    AstroModelContext models;
    dispatch::PrecessionModelEntry precession;
    dispatch::NutationModelEntry nutation;
    std::vector<ApparentDeflector> deflectors;
    int solar_deflector_index;

    ResolvedNativeCalcContext() noexcept
        : options(),
          fields(),
          models(),
          precession(),
          nutation(),
          deflectors(),
          solar_deflector_index(-1) {}
};

const uint32_t SUPPORTED_NATIVE_APPARENT_FLAGS =
    TAIYIN_APPARENT_LIGHT_TIME
    | TAIYIN_APPARENT_SPHERICAL
    | TAIYIN_APPARENT_ABERRATION
    | TAIYIN_APPARENT_DEFLECTION
    | TAIYIN_APPARENT_VELOCITY
    | TAIYIN_APPARENT_ACCELERATION
    | TAIYIN_APPARENT_SHAPIRO_DELAY
    | TAIYIN_APPARENT_TOPOCENTRIC;

const uint32_t SUPPORTED_NATIVE_POSITION_FLAGS =
    TAIYIN_NATIVE_POSITION_SPEED
    | TAIYIN_NATIVE_POSITION_XYZ
    | TAIYIN_NATIVE_POSITION_EQUATORIAL
    | TAIYIN_NATIVE_POSITION_RADIANS
    | TAIYIN_NATIVE_POSITION_TRUEPOS
    | TAIYIN_NATIVE_POSITION_NO_ABERR
    | TAIYIN_NATIVE_POSITION_NO_GDEFL
    | TAIYIN_NATIVE_POSITION_ASTROMETRIC
    | TAIYIN_NATIVE_POSITION_NONUT
    | TAIYIN_NATIVE_POSITION_TOPOCENTRIC
    | TAIYIN_NATIVE_POSITION_ALLOW_BARYCENTER_APPROX;

struct NativePositionEvaluatorEntry {
    NativePositionEvaluatorFn position_evaluator;
    NativeStateEvaluatorFn state_evaluator;

    NativePositionEvaluatorEntry() noexcept
        : position_evaluator(nullptr), state_evaluator(nullptr) {}

    NativePositionEvaluatorEntry(
        NativePositionEvaluatorFn position_evaluator_value,
        NativeStateEvaluatorFn state_evaluator_value
    ) noexcept
        : position_evaluator(position_evaluator_value), state_evaluator(state_evaluator_value) {}
};

typedef std::unordered_map<int, NativePositionEvaluatorEntry> NativePositionEvaluatorMap;

NativePositionEvaluatorMap& native_position_evaluators() noexcept {
    static NativePositionEvaluatorMap evaluators;
    return evaluators;
}

std::mutex& native_position_evaluators_mutex() noexcept {
    static std::mutex mutex;
    return mutex;
}

NativePositionEvaluatorEntry find_native_position_evaluator(int target_id) noexcept {
    if (target_id >= 0) return NativePositionEvaluatorEntry();
    try {
        std::lock_guard<std::mutex> lock(native_position_evaluators_mutex());
        const NativePositionEvaluatorMap& evaluators =
            native_position_evaluators();
        NativePositionEvaluatorMap::const_iterator it =
            evaluators.find(target_id);
        return it == evaluators.end()
            ? NativePositionEvaluatorEntry()
            : it->second;
    } catch (...) {
        return NativePositionEvaluatorEntry();
    }
}

const int kMaxNestedNativePositionEvaluators = 16;
const double kEvaluatorStateAccelerationStepDays = 1.0e-3;
thread_local int active_native_position_evaluator_ids[kMaxNestedNativePositionEvaluators];
thread_local int active_native_position_evaluator_count = 0;

class NativePositionEvaluatorScope {
public:
    explicit NativePositionEvaluatorScope(int target_id) noexcept
        : entered_(false) {
        for (int i = 0; i < active_native_position_evaluator_count; ++i) {
            if (active_native_position_evaluator_ids[i] == target_id) return;
        }
        if (active_native_position_evaluator_count >= kMaxNestedNativePositionEvaluators) return;
        active_native_position_evaluator_ids[active_native_position_evaluator_count++] = target_id;
        entered_ = true;
    }

    ~NativePositionEvaluatorScope() noexcept {
        if (entered_) --active_native_position_evaluator_count;
    }

    NativePositionEvaluatorScope(const NativePositionEvaluatorScope&) = delete;
    NativePositionEvaluatorScope& operator=(const NativePositionEvaluatorScope&) = delete;
    NativePositionEvaluatorScope(NativePositionEvaluatorScope&&) = delete;
    NativePositionEvaluatorScope& operator=(NativePositionEvaluatorScope&&) = delete;

    bool entered() const noexcept { return entered_; }

private:
    bool entered_;
};


bool valid_solar_deflector_index(size_t deflector_count, int solar_deflector_index) noexcept {
    return solar_deflector_index < 0
        || static_cast<size_t>(solar_deflector_index) < deflector_count;
}

Status copy_deflectors(
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

Status resolve_context(
    const NativeCalcContext& context,
    ResolvedNativeCalcContext* out
) noexcept {
    if (!out) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }

    *out = ResolvedNativeCalcContext();
    out->models = context.model_context;
    out->fields = context.fields;
    out->options = context.apparent_options;
    out->options.model_context = 0;

    if (context.fields.has(TAIYIN_NATIVE_FIELD_DEFLECTORS)) {
        out->solar_deflector_index = context.apparent_options.solar_deflector_index;
        const Status deflector_status = copy_deflectors(
            context.apparent_options.deflectors,
            context.apparent_options.deflector_count,
            context.apparent_options.solar_deflector_index,
            &out->deflectors);
        if (deflector_status != TAIYIN_STATUS_OK) {
            return deflector_status;
        }
    }
    out->options.deflectors = 0;
    out->options.deflector_count = 0;
    out->options.solar_deflector_index = -1;

    if ((out->options.flags & ~SUPPORTED_NATIVE_APPARENT_FLAGS) != 0u) {
        return TAIYIN_ERROR_UNSUPPORTED;
    }
    if (!dispatch::select_precession_model(out->models.precession_model_id, &out->precession)
        || !dispatch::select_nutation_model(out->models.nutation_model_id, &out->nutation)) {
        return TAIYIN_ERROR_UNSUPPORTED;
    }
    return TAIYIN_STATUS_OK;
}

Status apply_celestial_pole_offset_from_eop(
    NativeCalcContext* context,
    const SplitJulianDate& jd_utc
) noexcept {
    if (!context || !split_julian_date_is_finite(jd_utc)) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    const internal::EarthOrientationTable* eop_table = global_earth_orientation_table();
    if (!eop_table) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    internal::EarthOrientationSample eop;
    internal::EarthOrientationRates rates;
    internal::EarthRotationDerivatives derivatives;
    if (!internal::interpolate_earth_orientation(eop_table, jd_utc, &eop)) {
        return TAIYIN_ERROR_UNSUPPORTED;
    }
    if (!internal::derive_earth_orientation_rates(eop_table, jd_utc, &rates, &derivatives)) {
        rates.dx_rate_rad_per_day = 0.0;
        rates.dy_rate_rad_per_day = 0.0;
    }
    (void)derivatives;
    context->apparent_options.celestial_pole_offset_dx_rad = eop.dx_rad;
    context->apparent_options.celestial_pole_offset_dy_rad = eop.dy_rad;
    context->apparent_options.celestial_pole_offset_dx_rate_rad_per_day = rates.dx_rate_rad_per_day;
    context->apparent_options.celestial_pole_offset_dy_rate_rad_per_day = rates.dy_rate_rad_per_day;
    return TAIYIN_STATUS_OK;
}

void set_diagnostic(
    EphemerisEvalDiagnostic* diagnostic,
    Status status,
    int target_id,
    int center_id,
    const SplitJulianDate& jd_tdb
) noexcept {
    if (!diagnostic) {
        return;
    }
    *diagnostic = EphemerisEvalDiagnostic();
    diagnostic->status = status;
    diagnostic->target_id = target_id;
    diagnostic->center_id = center_id;
    diagnostic->frame = internal::EphemerisFrame::IcrfJ2000Equatorial;
    diagnostic->jd_tdb = jd_tdb;
}

void set_diagnostic(
    EphemerisEvalDiagnostic* diagnostic,
    Status status,
    int target_id,
    int center_id,
    double jd_tdb
) noexcept {
    SplitJulianDate split_jd;
    split_julian_date_from_double(jd_tdb, &split_jd);
    set_diagnostic(diagnostic, status, target_id, center_id, split_jd);
}

void clear_position_out(double out[6]) noexcept {
    if (!out) {
        return;
    }
    for (int i = 0; i < 6; ++i) {
        out[i] = 0.0;
    }
}

void clear_state_out(CartesianState* out) noexcept {
    if (!out) {
        return;
    }
    *out = CartesianState();
}

uint32_t native_position_apparent_flags(const NativeCalcContext& context, uint32_t flags) noexcept {
    uint32_t apparent_flags = context.apparent_options.flags;
    apparent_flags |= TAIYIN_APPARENT_SPHERICAL;
    apparent_flags &= ~(TAIYIN_APPARENT_VELOCITY | TAIYIN_APPARENT_ACCELERATION);
    if ((flags & TAIYIN_NATIVE_POSITION_SPEED) != 0u) {
        apparent_flags |= TAIYIN_APPARENT_VELOCITY;
    }
    if ((flags & TAIYIN_NATIVE_POSITION_TOPOCENTRIC) != 0u) {
        apparent_flags |= TAIYIN_APPARENT_TOPOCENTRIC;
    }
    if ((flags & TAIYIN_NATIVE_POSITION_TRUEPOS) != 0u) {
        apparent_flags &= ~(TAIYIN_APPARENT_LIGHT_TIME
            | TAIYIN_APPARENT_ABERRATION
            | TAIYIN_APPARENT_DEFLECTION
            | TAIYIN_APPARENT_SHAPIRO_DELAY
            | TAIYIN_APPARENT_TOPOCENTRIC);
    } else if ((flags & TAIYIN_NATIVE_POSITION_ASTROMETRIC) != 0u) {
        apparent_flags |= TAIYIN_APPARENT_LIGHT_TIME;
        apparent_flags &= ~(TAIYIN_APPARENT_ABERRATION
            | TAIYIN_APPARENT_DEFLECTION
            | TAIYIN_APPARENT_SHAPIRO_DELAY);
    } else {
        if ((flags & TAIYIN_NATIVE_POSITION_NO_ABERR) != 0u) {
            apparent_flags &= ~TAIYIN_APPARENT_ABERRATION;
        }
        if ((flags & TAIYIN_NATIVE_POSITION_NO_GDEFL) != 0u) {
            apparent_flags &= ~TAIYIN_APPARENT_DEFLECTION;
        }
    }
    return apparent_flags;
}

int without_nutation_output_frame(int output_frame_id) noexcept {
    switch (output_frame_id) {
    case TAIYIN_APPARENT_FRAME_TRUE_EQUATOR_OF_DATE:
        return TAIYIN_APPARENT_FRAME_MEAN_EQUATOR_OF_DATE;
    case TAIYIN_APPARENT_FRAME_TRUE_ECLIPTIC_OF_DATE:
        return TAIYIN_APPARENT_FRAME_MEAN_ECLIPTIC_OF_DATE;
    case TAIYIN_APPARENT_FRAME_CIRS:
        return -1;
    default:
        return output_frame_id;
    }
}

int native_position_output_frame(const NativeCalcContext& context, uint32_t flags) noexcept {
    if ((flags & TAIYIN_NATIVE_POSITION_EQUATORIAL) != 0u) {
        return (flags & TAIYIN_NATIVE_POSITION_NONUT) != 0u
            ? TAIYIN_APPARENT_FRAME_MEAN_EQUATOR_OF_DATE
            : TAIYIN_APPARENT_FRAME_TRUE_EQUATOR_OF_DATE;
    }
    return (flags & TAIYIN_NATIVE_POSITION_NONUT) != 0u
        ? without_nutation_output_frame(context.apparent_options.output_frame_id)
        : context.apparent_options.output_frame_id;
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

Status fail_position(
    double out[6],
    EphemerisEvalDiagnostic* diagnostic,
    Status status,
    int target_id,
    int center_id,
    const SplitJulianDate& jd_tdb
) noexcept {
    clear_position_out(out);
    set_diagnostic(diagnostic, status, target_id, center_id, jd_tdb);
    return status;
}

Status fail_position(
    double out[6],
    EphemerisEvalDiagnostic* diagnostic,
    Status status,
    int target_id,
    int center_id,
    double jd_tdb
) noexcept {
    SplitJulianDate split_jd;
    split_julian_date_from_double(jd_tdb, &split_jd);
    return fail_position(out, diagnostic, status, target_id, center_id, split_jd);
}

Status fail_state(
    CartesianState* out,
    EphemerisEvalDiagnostic* diagnostic,
    Status status,
    int target_id,
    int center_id,
    const SplitJulianDate& jd_tdb
) noexcept {
    clear_state_out(out);
    set_diagnostic(diagnostic, status, target_id, center_id, jd_tdb);
    return status;
}

Status fail_state(
    CartesianState* out,
    EphemerisEvalDiagnostic* diagnostic,
    Status status,
    int target_id,
    int center_id,
    double jd_tdb
) noexcept {
    SplitJulianDate split_jd;
    split_julian_date_from_double(jd_tdb, &split_jd);
    return fail_state(out, diagnostic, status, target_id, center_id, split_jd);
}

Status resolve_tt_to_tdb(
    const NativeCalcContext& context,
    const SplitJulianDate& jd_tt,
    SplitJulianDate* out_jd_tdb
) noexcept {
    if (!out_jd_tdb || !split_julian_date_is_finite(jd_tt)) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    const double tdb_minus_tt_seconds = dispatch::eval_tdb(context.model_context.tdb_model_id, jd_tt, 0);
    SplitJulianDate jd_tdb;
    if (!std::isfinite(tdb_minus_tt_seconds)
        || !add_seconds_to_split_jd(jd_tt, tdb_minus_tt_seconds, &jd_tdb)) {
        return TAIYIN_ERROR_UNSUPPORTED;
    }
    *out_jd_tdb = jd_tdb;
    return TAIYIN_STATUS_OK;
}

Status calc_position_tdb_once(
    const NativeCalcContext* context,
    int target_id,
    const SplitJulianDate& jd_tdb,
    const SplitJulianDate& jd_tt,
    uint32_t flags,
    double out[6],
    EphemerisEvalDiagnostic* diagnostic,
    CartesianState* state_out = 0
) noexcept {
    clear_position_out(out);
    clear_state_out(state_out);
    if (!context) {
        return fail_position(out, diagnostic, TAIYIN_ERROR_INVALID_ARGUMENT, target_id, 0, jd_tdb);
    }
    const NativeCalcContext& ctx = *context;
    const SplitJulianDate resolved_jd_tt = split_julian_date_is_finite(jd_tt)
        && jd_tt != SplitJulianDate()
        ? jd_tt
        : jd_tdb;
    set_diagnostic(diagnostic, TAIYIN_STATUS_OK, target_id, ctx.observer_id, jd_tdb);

    if (!out || !split_julian_date_is_finite(jd_tdb)
        || !split_julian_date_is_finite(resolved_jd_tt)) {
        return fail_position(out, diagnostic, TAIYIN_ERROR_INVALID_ARGUMENT, target_id, ctx.observer_id, jd_tdb);
    }
    if ((flags & ~SUPPORTED_NATIVE_POSITION_FLAGS) != 0u) {
        return fail_position(out, diagnostic, TAIYIN_ERROR_UNSUPPORTED, target_id, ctx.observer_id, jd_tdb);
    }
    if (ctx.observer_id != TAIYIN_BODY_EARTH
        && ((flags & TAIYIN_NATIVE_POSITION_TOPOCENTRIC) != 0u
            || (ctx.apparent_options.flags & TAIYIN_APPARENT_TOPOCENTRIC) != 0u)) {
        return fail_position(
            out,
            diagnostic,
            TAIYIN_ERROR_UNSUPPORTED,
            target_id,
            ctx.observer_id,
            jd_tdb);
    }

    ResolvedNativeCalcContext resolved;
    const Status config_status = resolve_context(ctx, &resolved);
    if (config_status != TAIYIN_STATUS_OK) {
        return fail_position(out, diagnostic, config_status, target_id, ctx.observer_id, jd_tdb);
    }

    uint32_t apparent_flags = native_position_apparent_flags(ctx, flags);
    if (state_out) {
        apparent_flags |= TAIYIN_APPARENT_VELOCITY | TAIYIN_APPARENT_ACCELERATION;
    }
    const int output_frame_id = native_position_output_frame(ctx, flags);
    if (output_frame_id < 0) {
        return fail_position(out, diagnostic, TAIYIN_ERROR_UNSUPPORTED, target_id, ctx.observer_id, jd_tdb);
    }
    if ((apparent_flags & ~SUPPORTED_NATIVE_APPARENT_FLAGS) != 0u) {
        return fail_position(out, diagnostic, TAIYIN_ERROR_UNSUPPORTED, target_id, ctx.observer_id, jd_tdb);
    }
    if ((apparent_flags & TAIYIN_APPARENT_SHAPIRO_DELAY) != 0u
        && (apparent_flags & TAIYIN_APPARENT_LIGHT_TIME) == 0u) {
        return fail_position(out, diagnostic, TAIYIN_ERROR_INVALID_ARGUMENT, target_id, ctx.observer_id, jd_tdb);
    }

    const bool needs_deflectors = (apparent_flags & (
        TAIYIN_APPARENT_ABERRATION
        | TAIYIN_APPARENT_DEFLECTION
        | TAIYIN_APPARENT_SHAPIRO_DELAY)) != 0u;
    if (needs_deflectors && resolved.deflectors.empty()) {
        return fail_position(out, diagnostic, TAIYIN_ERROR_INVALID_ARGUMENT, target_id, ctx.observer_id, jd_tdb);
    }
    if (needs_deflectors
        && (resolved.solar_deflector_index < 0
            || static_cast<size_t>(resolved.solar_deflector_index) >= resolved.deflectors.size())) {
        return fail_position(out, diagnostic, TAIYIN_ERROR_INVALID_ARGUMENT, target_id, ctx.observer_id, jd_tdb);
    }
    if (resolved.deflectors.size() > static_cast<size_t>(std::numeric_limits<int>::max())) {
        return fail_position(out, diagnostic, TAIYIN_ERROR_INVALID_ARGUMENT, target_id, ctx.observer_id, jd_tdb);
    }
    if ((apparent_flags & TAIYIN_APPARENT_TOPOCENTRIC) != 0u
        && (!resolved.fields.has(TAIYIN_NATIVE_FIELD_TOPOCENTRIC_OFFSET)
            || !native_cartesian_state_is_finite(resolved.options.observer_offset))) {
        return fail_position(out, diagnostic, TAIYIN_ERROR_INVALID_ARGUMENT, target_id, ctx.observer_id, jd_tdb);
    }

    double observer_offset_pos[3] = { 0.0, 0.0, 0.0 };
    double observer_offset_vel[3] = { 0.0, 0.0, 0.0 };
    double observer_offset_acc[3] = { 0.0, 0.0, 0.0 };
    if ((apparent_flags & TAIYIN_APPARENT_TOPOCENTRIC) != 0u) {
        runtime_vector_to_array3(resolved.options.observer_offset.position_au, observer_offset_pos);
        runtime_vector_to_array3(resolved.options.observer_offset.velocity_au_per_day, observer_offset_vel);
        runtime_vector_to_array3(resolved.options.observer_offset.acceleration_au_per_day2, observer_offset_acc);
    }

    RuntimeStateEvalContext eval_context;
    eval_context.service = 0;
    eval_context.use_global = true;
    eval_context.route_rule_id = ctx.route_rule_id;
    eval_context.route_rules = ctx.route_rules;

    RuntimeCompiledBlockData target_data;
    target_data.context = eval_context;
    target_data.body_id = target_id;
    target_data.center_id = ctx.center_id;
    target_data.preferred_components = runtime_components_for_apparent_flags(apparent_flags);
    RuntimeCompiledBlockData observer_data;
    observer_data.context = eval_context;
    observer_data.body_id = ctx.observer_id;
    observer_data.center_id = ctx.center_id;
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
        deflector_data.reserve(resolved.deflectors.size());
        deflector_blocks.reserve(resolved.deflectors.size());
        deflector_block_ptrs.reserve(resolved.deflectors.size());
        deflector_ids.reserve(resolved.deflectors.size());
        deflector_schwarzschild_radius_au.reserve(resolved.deflectors.size());
        deflector_limit.reserve(resolved.deflectors.size());
        for (size_t i = 0; i < resolved.deflectors.size(); ++i) {
            RuntimeCompiledBlockData block_data;
            block_data.context = eval_context;
            block_data.body_id = resolved.deflectors[i].body_id;
            block_data.center_id = ctx.center_id;
            block_data.preferred_components = runtime_components_for_apparent_flags(apparent_flags);
            deflector_data.push_back(block_data);
            deflector_blocks.push_back(make_runtime_compiled_block(&deflector_data.back()));
            deflector_block_ptrs.push_back(&deflector_blocks.back());
            deflector_ids.push_back(resolved.deflectors[i].body_id);
            deflector_schwarzschild_radius_au.push_back(resolved.deflectors[i].schwarzschild_radius_au);
            deflector_limit.push_back(resolved.deflectors[i].limit);
        }
    } catch (...) {
        return fail_position(out, diagnostic, TAIYIN_ERROR_OUT_OF_MEMORY, target_id, ctx.observer_id, jd_tdb);
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
    double lon_rad = 0.0;
    double lat_rad = 0.0;
    double distance_au = 0.0;
    double lon_rate_rad_per_day = 0.0;
    double lat_rate_rad_per_day = 0.0;
    double distance_rate_au_per_day = 0.0;
    double lon_acc_rad_per_day2 = 0.0;
    double lat_acc_rad_per_day2 = 0.0;
    double distance_acc_au_per_day2 = 0.0;
    double light_time_days = 0.0;
    double light_time_rate = 0.0;
    double light_time_acceleration = 0.0;
    int light_time_iterations = 0;

    const bool ok = calc_apparent(
        jd_tdb,
        resolved_jd_tt,
        target_id,
        &target_block,
        ctx.observer_id,
        &observer_block,
        observer_offset_pos,
        observer_offset_vel,
        observer_offset_acc,
        static_cast<int>(resolved.deflectors.size()),
        resolved.solar_deflector_index,
        deflector_ids.empty() ? 0 : deflector_ids.data(),
        deflector_block_ptrs.empty() ? 0 : deflector_block_ptrs.data(),
        deflector_schwarzschild_radius_au.empty() ? 0 : deflector_schwarzschild_radius_au.data(),
        deflector_limit.empty() ? 0 : deflector_limit.data(),
        apparent_flags,
        output_frame_id,
        resolved.options.light_time_method_id,
        resolved.options.shapiro_delay_model_id,
        resolved.options.aberration_model_id,
        resolved.options.deflection_model_id,
        resolved.precession.model_id,
        resolved.nutation.model_id,
        resolved.models.obliquity_model_id,
        resolved.models.frame_route_id,
        resolved.options.celestial_pole_offset_dx_rad,
        resolved.options.celestial_pole_offset_dy_rad,
        resolved.options.celestial_pole_offset_dx_rate_rad_per_day,
        resolved.options.celestial_pole_offset_dy_rate_rad_per_day,
        resolved.options.max_light_time_iterations,
        resolved.options.light_time_tolerance_days,
        resolved.options.matrix_derivative_step_days,
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
        &lon_rad,
        &lat_rad,
        &distance_au,
        &lon_rate_rad_per_day,
        &lat_rate_rad_per_day,
        &distance_rate_au_per_day,
        &lon_acc_rad_per_day2,
        &lat_acc_rad_per_day2,
        &distance_acc_au_per_day2,
        &light_time_days,
        &light_time_rate,
        &light_time_acceleration,
        &light_time_iterations,
        resolved.options.custom_output_frame_evaluator,
        resolved.options.custom_output_frame_data);

    if (!ok) {
        EphemerisEvalDiagnostic failure_diagnostic;
        failure_diagnostic.status = TAIYIN_EPHEMERIS_ERROR_EVAL_FAILED;
        failure_diagnostic.target_id = target_id;
        failure_diagnostic.center_id = ctx.observer_id;
        failure_diagnostic.frame = internal::EphemerisFrame::IcrfJ2000Equatorial;
        failure_diagnostic.jd_tdb = jd_tdb;
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
        clear_position_out(out);
        clear_state_out(state_out);
        copy_ephemeris_diagnostic(diagnostic, failure_diagnostic);
        return failure_status;
    }

    if (state_out) {
        state_out->position_au = runtime_vector_from_array3(apparent_pos);
        state_out->velocity_au_per_day = runtime_vector_from_array3(apparent_vel);
        state_out->acceleration_au_per_day2 = runtime_vector_from_array3(apparent_acc);
        if (!native_cartesian_state_is_finite(*state_out)) {
            return fail_state(
                state_out,
                diagnostic,
                TAIYIN_EPHEMERIS_ERROR_EVAL_FAILED,
                target_id,
                ctx.observer_id,
                jd_tdb);
        }
    }

    if ((flags & TAIYIN_NATIVE_POSITION_XYZ) != 0u) {
        out[0] = apparent_pos[0];
        out[1] = apparent_pos[1];
        out[2] = apparent_pos[2];
        if ((flags & TAIYIN_NATIVE_POSITION_SPEED) != 0u) {
            out[3] = apparent_vel[0];
            out[4] = apparent_vel[1];
            out[5] = apparent_vel[2];
        }
    } else {
        const double angle_scale = (flags & TAIYIN_NATIVE_POSITION_RADIANS) != 0u
            ? 1.0
            : TAIYIN_RAD_TO_DEG;
        out[0] = lon_rad * angle_scale;
        out[1] = lat_rad * angle_scale;
        out[2] = distance_au;
        if ((flags & TAIYIN_NATIVE_POSITION_SPEED) != 0u) {
            out[3] = lon_rate_rad_per_day * angle_scale;
            out[4] = lat_rate_rad_per_day * angle_scale;
            out[5] = distance_rate_au_per_day;
        }
    }

    if (!std::isfinite(out[0]) || !std::isfinite(out[1]) || !std::isfinite(out[2])) {
        return fail_position(out, diagnostic, TAIYIN_EPHEMERIS_ERROR_EVAL_FAILED, target_id, ctx.observer_id, jd_tdb);
    }
    set_diagnostic(diagnostic, TAIYIN_STATUS_OK, target_id, ctx.observer_id, jd_tdb);
    return TAIYIN_STATUS_OK;
}

}  // namespace

Status calc_position_tdb(
    const NativeCalcContext* context,
    int target_id,
    const SplitJulianDate& jd_tdb,
    const SplitJulianDate& jd_tt,
    uint32_t flags,
    double out[6],
    EphemerisEvalDiagnostic* diagnostic
) noexcept {
    if (!out) {
        return fail_position(
            out, diagnostic, TAIYIN_ERROR_INVALID_ARGUMENT, target_id,
            context ? context->observer_id : 0, jd_tdb);
    }
    if (!context || !split_julian_date_is_finite(jd_tdb)) {
        return fail_position(
            out, diagnostic, TAIYIN_ERROR_INVALID_ARGUMENT, target_id,
            context ? context->observer_id : 0, jd_tdb);
    }
    const SplitJulianDate resolved_jd_tt = split_julian_date_is_finite(jd_tt)
        && jd_tt != SplitJulianDate()
        ? jd_tt
        : jd_tdb;
    const NativePositionEvaluatorEntry evaluator = find_native_position_evaluator(target_id);
    if (evaluator.position_evaluator) {
        NativePositionEvaluatorScope scope(target_id);
        if (!scope.entered()) {
            return fail_position(
                out, diagnostic, TAIYIN_ERROR_INTERNAL, target_id,
                context ? context->observer_id : 0, jd_tdb);
        }
        return evaluator.position_evaluator(
            context, target_id, jd_tdb, resolved_jd_tt, flags, out, diagnostic);
    }
    const Status status = calc_position_tdb_once(
        context, target_id, jd_tdb, resolved_jd_tt, flags, out, diagnostic);
    if (!native_position_should_try_barycenter_approx(target_id, flags, status)) {
        return status;
    }

    const int approx_target_id = native_position_barycenter_approx_target(target_id);
    EphemerisEvalDiagnostic approx_diagnostic;
    const uint32_t approx_flags = flags & ~TAIYIN_NATIVE_POSITION_ALLOW_BARYCENTER_APPROX;
    const Status approx_status = calc_position_tdb_once(
        context, approx_target_id, jd_tdb, resolved_jd_tt, approx_flags, out, &approx_diagnostic);
    if (approx_status != TAIYIN_STATUS_OK) {
        return status;
    }

    if (diagnostic) {
        *diagnostic = approx_diagnostic;
        diagnostic->target_id = target_id;
        diagnostic->component_target_id = approx_target_id;
    }
    return TAIYIN_STATUS_OK;
}

Status calc_position_tt(
    const NativeCalcContext* context,
    int target_id,
    const SplitJulianDate& jd_tt,
    uint32_t flags,
    double out[6],
    EphemerisEvalDiagnostic* diagnostic
) noexcept {
    clear_position_out(out);
    if (!context) {
        return fail_position(out, diagnostic, TAIYIN_ERROR_INVALID_ARGUMENT, target_id, 0, jd_tt);
    }
    const NativeCalcContext& ctx = *context;
    if (!out || !split_julian_date_is_finite(jd_tt)) {
        return fail_position(out, diagnostic, TAIYIN_ERROR_INVALID_ARGUMENT, target_id, ctx.observer_id, jd_tt);
    }
    SplitJulianDate jd_tdb;
    const Status time_status = resolve_tt_to_tdb(ctx, jd_tt, &jd_tdb);
    if (time_status != TAIYIN_STATUS_OK) {
        return fail_position(out, diagnostic, time_status, target_id, ctx.observer_id, jd_tt);
    }
    return calc_position_tdb(context, target_id, jd_tdb, jd_tt, flags, out, diagnostic);
}

Status calc_position_ut_delta_t(
    const NativeCalcContext* context,
    int target_id,
    const SplitJulianDate& jd_ut1,
    double delta_t_seconds,
    uint32_t flags,
    double out[6],
    EphemerisEvalDiagnostic* diagnostic
) noexcept {
    clear_position_out(out);
    if (!context) {
        return fail_position(out, diagnostic, TAIYIN_ERROR_INVALID_ARGUMENT, target_id, 0, jd_ut1);
    }
    const NativeCalcContext& ctx = *context;
    if (!out || !split_julian_date_is_finite(jd_ut1) || !std::isfinite(delta_t_seconds)) {
        return fail_position(out, diagnostic, TAIYIN_ERROR_INVALID_ARGUMENT, target_id, ctx.observer_id, jd_ut1);
    }
    SplitJulianDate jd_tt;
    if (!ut1_to_tt_split_jd(jd_ut1, delta_t_seconds, &jd_tt)) {
        return fail_position(
            out, diagnostic, TAIYIN_ERROR_INVALID_ARGUMENT,
            target_id, ctx.observer_id, jd_ut1);
    }
    SplitJulianDate jd_tdb;
    const Status time_status = resolve_tt_to_tdb(ctx, jd_tt, &jd_tdb);
    if (time_status != TAIYIN_STATUS_OK) {
        return fail_position(out, diagnostic, time_status, target_id, ctx.observer_id, jd_ut1);
    }
    return calc_position_tdb(context, target_id, jd_tdb, jd_tt, flags, out, diagnostic);
}

Status calc_position_ut(
    const NativeCalcContext* context,
    int target_id,
    const SplitJulianDate& jd_ut,
    uint32_t flags,
    double out[6],
    EphemerisEvalDiagnostic* diagnostic
) noexcept {
    clear_position_out(out);
    if (!context) {
        return fail_position(out, diagnostic, TAIYIN_ERROR_INVALID_ARGUMENT, target_id, 0, jd_ut);
    }
    const NativeCalcContext& ctx = *context;
    if (!out || !split_julian_date_is_finite(jd_ut)) {
        return fail_position(out, diagnostic, TAIYIN_ERROR_INVALID_ARGUMENT, target_id, ctx.observer_id, jd_ut);
    }
    const double delta_t_seconds = dispatch::eval_delta_t_with_ephemeris_correction(
        ctx.delta_t_model_id,
        ctx.ephemeris_family_id,
        jd_ut,
        0,
        0);
    return calc_position_ut_delta_t(
        context,
        target_id,
        jd_ut,
        delta_t_seconds,
        flags,
        out,
        diagnostic);
}

Status calc_position_utc(
    const NativeCalcContext* context,
    int target_id,
    const CalendarDateTime& datetime_utc,
    uint32_t flags,
    double out[6],
    EphemerisEvalDiagnostic* diagnostic
) noexcept {
    clear_position_out(out);
    if (!context) {
        return fail_position(out, diagnostic, TAIYIN_ERROR_INVALID_ARGUMENT, target_id, 0, julian_day(datetime_utc));
    }
    const NativeCalcContext& ctx = *context;
    if (!out) {
        return fail_position(out, diagnostic, TAIYIN_ERROR_INVALID_ARGUMENT, target_id, ctx.observer_id, julian_day(datetime_utc));
    }

    const internal::EarthOrientationTable* eop_table = global_earth_orientation_table();
    if (!eop_table) {
        return fail_position(out, diagnostic, TAIYIN_ERROR_UNSUPPORTED, target_id, ctx.observer_id, julian_day(datetime_utc));
    }

    TimeScaleOptions options;
    options.policy = TimeScalePrecise;
    options.tdb_model_id = ctx.model_context.tdb_model_id;
    options.delta_t_model_id = ctx.delta_t_model_id;
    options.ephemeris_family_id = ctx.ephemeris_family_id;
    options.leap_second_table = builtin_leap_second_table();
    PreciseTimeScales scales;
    TimeScaleDiagnostic time_diagnostic;
    if (!make_time_scales_from_utc(datetime_utc, eop_table, &options, &scales, &time_diagnostic)) {
        return fail_position(out, diagnostic, TAIYIN_ERROR_UNSUPPORTED, target_id, ctx.observer_id, julian_day(datetime_utc));
    }
    NativeCalcContext scratch = ctx;
    const SplitJulianDate resolved_jd_tdb = scales.jd_tdb;
    const SplitJulianDate resolved_jd_tt = scales.jd_tt;
    const Status cpo_status = apply_celestial_pole_offset_from_eop(
        &scratch, scales.jd_utc);
    if (cpo_status != TAIYIN_STATUS_OK) {
        return fail_position(
            out,
            diagnostic,
            cpo_status,
            target_id,
            ctx.observer_id,
            resolved_jd_tdb);
    }
    return calc_position_tdb(
        &scratch,
        target_id,
        resolved_jd_tdb,
        resolved_jd_tt,
        flags,
        out,
        diagnostic);
}

Status calc_positions_tdb(
    const NativeCalcContext* context,
    const int* target_ids,
    size_t target_count,
    const SplitJulianDate& jd_tdb,
    const SplitJulianDate& jd_tt,
    uint32_t flags,
    double* out,
    EphemerisEvalDiagnostic* diagnostics
) noexcept {
    if (target_count == 0) {
        return TAIYIN_STATUS_OK;
    }
    if (!context) {
        for (size_t i = 0; out && i < target_count; ++i) {
            clear_position_out(out + i * 6);
        }
        for (size_t i = 0; diagnostics && i < target_count; ++i) {
            set_diagnostic(diagnostics + i, TAIYIN_ERROR_INVALID_ARGUMENT, target_ids ? target_ids[i] : 0, 0, jd_tdb);
        }
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    const NativeCalcContext& ctx = *context;
    if (!target_ids || !out || !split_julian_date_is_finite(jd_tdb)
        || !split_julian_date_is_finite(jd_tt)) {
        for (size_t i = 0; out && i < target_count; ++i) {
            clear_position_out(out + i * 6);
        }
        for (size_t i = 0; diagnostics && i < target_count; ++i) {
            set_diagnostic(diagnostics + i, TAIYIN_ERROR_INVALID_ARGUMENT, target_ids ? target_ids[i] : 0, ctx.observer_id, jd_tdb);
        }
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }

    Status first_status = TAIYIN_STATUS_OK;
    for (size_t i = 0; i < target_count; ++i) {
        const Status status = calc_position_tdb(
            context,
            target_ids[i],
            jd_tdb,
            jd_tt,
            flags,
            out + i * 6,
            diagnostics ? diagnostics + i : 0);
        if (status != TAIYIN_STATUS_OK && first_status == TAIYIN_STATUS_OK) {
            first_status = status;
        }
    }
    return first_status;
}

Status calc_positions_tt(
    const NativeCalcContext* context,
    const int* target_ids,
    size_t target_count,
    const SplitJulianDate& jd_tt,
    uint32_t flags,
    double* out,
    EphemerisEvalDiagnostic* diagnostics
) noexcept {
    if (target_count == 0) {
        return TAIYIN_STATUS_OK;
    }
    if (!context) {
        for (size_t i = 0; out && i < target_count; ++i) {
            clear_position_out(out + i * 6);
        }
        for (size_t i = 0; diagnostics && i < target_count; ++i) {
            set_diagnostic(diagnostics + i, TAIYIN_ERROR_INVALID_ARGUMENT, target_ids ? target_ids[i] : 0, 0, jd_tt);
        }
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    const NativeCalcContext& ctx = *context;
    if (!target_ids || !out || !split_julian_date_is_finite(jd_tt)) {
        for (size_t i = 0; out && i < target_count; ++i) {
            clear_position_out(out + i * 6);
        }
        for (size_t i = 0; diagnostics && i < target_count; ++i) {
            set_diagnostic(diagnostics + i, TAIYIN_ERROR_INVALID_ARGUMENT, target_ids ? target_ids[i] : 0, ctx.observer_id, jd_tt);
        }
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    SplitJulianDate jd_tdb;
    const Status time_status = resolve_tt_to_tdb(ctx, jd_tt, &jd_tdb);
    if (time_status != TAIYIN_STATUS_OK) {
        for (size_t i = 0; i < target_count; ++i) {
            clear_position_out(out + i * 6);
            if (diagnostics) {
                set_diagnostic(diagnostics + i, time_status, target_ids[i], ctx.observer_id, jd_tt);
            }
        }
        return time_status;
    }
    return calc_positions_tdb(context, target_ids, target_count, jd_tdb, jd_tt, flags, out, diagnostics);
}

Status calc_positions_ut_delta_t(
    const NativeCalcContext* context,
    const int* target_ids,
    size_t target_count,
    const SplitJulianDate& jd_ut1,
    double delta_t_seconds,
    uint32_t flags,
    double* out,
    EphemerisEvalDiagnostic* diagnostics
) noexcept {
    if (target_count == 0) {
        return TAIYIN_STATUS_OK;
    }
    if (!context) {
        for (size_t i = 0; out && i < target_count; ++i) {
            clear_position_out(out + i * 6);
        }
        for (size_t i = 0; diagnostics && i < target_count; ++i) {
            set_diagnostic(diagnostics + i, TAIYIN_ERROR_INVALID_ARGUMENT, target_ids ? target_ids[i] : 0, 0, jd_ut1);
        }
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    const NativeCalcContext& ctx = *context;
    if (!target_ids || !out || !split_julian_date_is_finite(jd_ut1)
        || !std::isfinite(delta_t_seconds)) {
        for (size_t i = 0; out && i < target_count; ++i) {
            clear_position_out(out + i * 6);
        }
        for (size_t i = 0; diagnostics && i < target_count; ++i) {
            set_diagnostic(diagnostics + i, TAIYIN_ERROR_INVALID_ARGUMENT, target_ids ? target_ids[i] : 0, ctx.observer_id, jd_ut1);
        }
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    SplitJulianDate jd_tt;
    if (!ut1_to_tt_split_jd(jd_ut1, delta_t_seconds, &jd_tt)) {
        for (size_t i = 0; i < target_count; ++i) {
            clear_position_out(out + i * 6);
            if (diagnostics) {
                set_diagnostic(
                    diagnostics + i, TAIYIN_ERROR_INVALID_ARGUMENT,
                    target_ids[i], ctx.observer_id, jd_ut1);
            }
        }
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    SplitJulianDate jd_tdb;
    const Status time_status = resolve_tt_to_tdb(ctx, jd_tt, &jd_tdb);
    if (time_status != TAIYIN_STATUS_OK) {
        for (size_t i = 0; i < target_count; ++i) {
            clear_position_out(out + i * 6);
            if (diagnostics) {
                set_diagnostic(diagnostics + i, time_status, target_ids[i], ctx.observer_id, jd_ut1);
            }
        }
        return time_status;
    }
    return calc_positions_tdb(context, target_ids, target_count, jd_tdb, jd_tt, flags, out, diagnostics);
}

Status calc_positions_ut(
    const NativeCalcContext* context,
    const int* target_ids,
    size_t target_count,
    const SplitJulianDate& jd_ut,
    uint32_t flags,
    double* out,
    EphemerisEvalDiagnostic* diagnostics
) noexcept {
    if (target_count == 0) {
        return TAIYIN_STATUS_OK;
    }
    if (!context) {
        for (size_t i = 0; out && i < target_count; ++i) {
            clear_position_out(out + i * 6);
        }
        for (size_t i = 0; diagnostics && i < target_count; ++i) {
            set_diagnostic(diagnostics + i, TAIYIN_ERROR_INVALID_ARGUMENT, target_ids ? target_ids[i] : 0, 0, jd_ut);
        }
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    const NativeCalcContext& ctx = *context;
    if (!target_ids || !out || !split_julian_date_is_finite(jd_ut)) {
        for (size_t i = 0; out && i < target_count; ++i) {
            clear_position_out(out + i * 6);
        }
        for (size_t i = 0; diagnostics && i < target_count; ++i) {
            set_diagnostic(diagnostics + i, TAIYIN_ERROR_INVALID_ARGUMENT, target_ids ? target_ids[i] : 0, ctx.observer_id, jd_ut);
        }
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    const double delta_t_seconds = dispatch::eval_delta_t_with_ephemeris_correction(
        ctx.delta_t_model_id,
        ctx.ephemeris_family_id,
        jd_ut,
        0,
        0);
    return calc_positions_ut_delta_t(
        context,
        target_ids,
        target_count,
        jd_ut,
        delta_t_seconds,
        flags,
        out,
        diagnostics);
}

Status calc_positions_utc(
    const NativeCalcContext* context,
    const int* target_ids,
    size_t target_count,
    const CalendarDateTime& datetime_utc,
    uint32_t flags,
    double* out,
    EphemerisEvalDiagnostic* diagnostics
) noexcept {
    if (target_count == 0) {
        return TAIYIN_STATUS_OK;
    }
    SplitJulianDate jd_utc;
    const bool has_jd_utc = julian_day_split(datetime_utc, &jd_utc);
    if (!context) {
        for (size_t i = 0; out && i < target_count; ++i) {
            clear_position_out(out + i * 6);
        }
        for (size_t i = 0; diagnostics && i < target_count; ++i) {
            set_diagnostic(diagnostics + i, TAIYIN_ERROR_INVALID_ARGUMENT, target_ids ? target_ids[i] : 0, 0, jd_utc);
        }
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    const NativeCalcContext& ctx = *context;
    if (!target_ids || !out || !has_jd_utc) {
        for (size_t i = 0; out && i < target_count; ++i) {
            clear_position_out(out + i * 6);
        }
        for (size_t i = 0; diagnostics && i < target_count; ++i) {
            set_diagnostic(diagnostics + i, TAIYIN_ERROR_INVALID_ARGUMENT, target_ids ? target_ids[i] : 0, ctx.observer_id, jd_utc);
        }
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    const internal::EarthOrientationTable* eop_table = global_earth_orientation_table();
    if (!eop_table) {
        for (size_t i = 0; i < target_count; ++i) {
            clear_position_out(out + i * 6);
            if (diagnostics) {
                set_diagnostic(diagnostics + i, TAIYIN_ERROR_UNSUPPORTED, target_ids[i], ctx.observer_id, jd_utc);
            }
        }
        return TAIYIN_ERROR_UNSUPPORTED;
    }

    TimeScaleOptions options;
    options.policy = TimeScalePrecise;
    options.tdb_model_id = ctx.model_context.tdb_model_id;
    options.delta_t_model_id = ctx.delta_t_model_id;
    options.ephemeris_family_id = ctx.ephemeris_family_id;
    options.leap_second_table = builtin_leap_second_table();
    PreciseTimeScales scales;
    TimeScaleDiagnostic time_diagnostic;
    if (!make_time_scales_from_utc(datetime_utc, eop_table, &options, &scales, &time_diagnostic)) {
        for (size_t i = 0; i < target_count; ++i) {
            clear_position_out(out + i * 6);
            if (diagnostics) {
                set_diagnostic(diagnostics + i, TAIYIN_ERROR_UNSUPPORTED, target_ids[i], ctx.observer_id, jd_utc);
            }
        }
        return TAIYIN_ERROR_UNSUPPORTED;
    }
    NativeCalcContext scratch = ctx;
    const SplitJulianDate resolved_jd_tdb = scales.jd_tdb;
    const SplitJulianDate resolved_jd_tt = scales.jd_tt;
    const Status cpo_status = apply_celestial_pole_offset_from_eop(
        &scratch, scales.jd_utc);
    if (cpo_status != TAIYIN_STATUS_OK) {
        for (size_t i = 0; i < target_count; ++i) {
            clear_position_out(out + i * 6);
            if (diagnostics) {
                set_diagnostic(
                    diagnostics + i,
                    cpo_status,
                    target_ids[i],
                    ctx.observer_id,
                    resolved_jd_tdb);
            }
        }
        return cpo_status;
    }
    return calc_positions_tdb(
        &scratch,
        target_ids,
        target_count,
        resolved_jd_tdb,
        resolved_jd_tt,
        flags,
        out,
        diagnostics);
}

Status calc_state_tdb(
    const NativeCalcContext* context,
    int target_id,
    const SplitJulianDate& jd_tdb,
    const SplitJulianDate& jd_tt,
    uint32_t flags,
    CartesianState* out,
    EphemerisEvalDiagnostic* diagnostic
) noexcept {
    clear_state_out(out);
    if (!out) {
        return fail_state(out, diagnostic, TAIYIN_ERROR_INVALID_ARGUMENT, target_id, context ? context->observer_id : 0, jd_tdb);
    }
    if (!context || !split_julian_date_is_finite(jd_tdb)) {
        return fail_state(
            out, diagnostic, TAIYIN_ERROR_INVALID_ARGUMENT, target_id,
            context ? context->observer_id : 0, jd_tdb);
    }
    const SplitJulianDate resolved_jd_tt = split_julian_date_is_finite(jd_tt)
        && jd_tt != SplitJulianDate()
        ? jd_tt
        : jd_tdb;
    const NativePositionEvaluatorEntry evaluator = find_native_position_evaluator(target_id);
    if (evaluator.position_evaluator) {
        if (evaluator.state_evaluator) {
            NativePositionEvaluatorScope scope(target_id);
            if (!scope.entered()) {
                return fail_state(
                    out, diagnostic, TAIYIN_ERROR_INTERNAL, target_id,
                    context ? context->observer_id : 0, jd_tdb);
            }
            return evaluator.state_evaluator(
                context, target_id, jd_tdb, resolved_jd_tt, flags, out, diagnostic);
        }
        double position[6] = {};
        const Status status = calc_position_tdb(
            context,
            target_id,
            jd_tdb,
            resolved_jd_tt,
            flags | TAIYIN_NATIVE_POSITION_XYZ | TAIYIN_NATIVE_POSITION_SPEED,
            position,
            diagnostic);
        if (status != TAIYIN_STATUS_OK) {
            clear_state_out(out);
            return status;
        }
        out->position_au = Vector3{position[0], position[1], position[2]};
        out->velocity_au_per_day = Vector3{position[3], position[4], position[5]};
        double before[6] = {};
        double after[6] = {};
        EphemerisEvalDiagnostic scratch_diagnostic;
        const double step_days = kEvaluatorStateAccelerationStepDays;
        SplitJulianDate before_jd;
        SplitJulianDate after_jd;
        SplitJulianDate before_jd_tt;
        SplitJulianDate after_jd_tt;
        const bool sampled_neighbors = add_days_to_split_jd(jd_tdb, -step_days, &before_jd)
            && add_days_to_split_jd(jd_tdb, step_days, &after_jd)
            && add_days_to_split_jd(resolved_jd_tt, -step_days, &before_jd_tt)
            && add_days_to_split_jd(resolved_jd_tt, step_days, &after_jd_tt)
            && before_jd != jd_tdb
            && after_jd != jd_tdb
            && calc_position_tdb(
                context,
                target_id,
                before_jd,
                before_jd_tt,
                flags | TAIYIN_NATIVE_POSITION_XYZ | TAIYIN_NATIVE_POSITION_SPEED,
                before,
                &scratch_diagnostic) == TAIYIN_STATUS_OK
            && calc_position_tdb(
                context,
                target_id,
                after_jd,
                after_jd_tt,
                flags | TAIYIN_NATIVE_POSITION_XYZ | TAIYIN_NATIVE_POSITION_SPEED,
                after,
                &scratch_diagnostic) == TAIYIN_STATUS_OK;
        if (sampled_neighbors) {
            const double inverse_span_days = 1.0 / (2.0 * step_days);
            const bool has_analytic_velocity = std::isfinite(position[3])
                && std::isfinite(position[4]) && std::isfinite(position[5]);
            if (!has_analytic_velocity) {
                out->velocity_au_per_day = Vector3{
                    (after[0] - before[0]) * inverse_span_days,
                    (after[1] - before[1]) * inverse_span_days,
                    (after[2] - before[2]) * inverse_span_days,
                };
            }
            const bool neighbor_velocities_are_finite = std::isfinite(before[3])
                && std::isfinite(before[4]) && std::isfinite(before[5])
                && std::isfinite(after[3]) && std::isfinite(after[4])
                && std::isfinite(after[5]);
            if (neighbor_velocities_are_finite) {
                out->acceleration_au_per_day2 = Vector3{
                    (after[3] - before[3]) * inverse_span_days,
                    (after[4] - before[4]) * inverse_span_days,
                    (after[5] - before[5]) * inverse_span_days,
                };
            } else {
                const double inverse_step_squared = 1.0 / (step_days * step_days);
                out->acceleration_au_per_day2 = Vector3{
                    (after[0] - 2.0 * position[0] + before[0]) * inverse_step_squared,
                    (after[1] - 2.0 * position[1] + before[1]) * inverse_step_squared,
                    (after[2] - 2.0 * position[2] + before[2]) * inverse_step_squared,
                };
            }
        } else {
            const double nan_value = std::numeric_limits<double>::quiet_NaN();
            out->acceleration_au_per_day2 = Vector3{nan_value, nan_value, nan_value};
        }
        return TAIYIN_STATUS_OK;
    }

    double ignored_position[6] = {};
    const uint32_t state_flags = flags | TAIYIN_NATIVE_POSITION_XYZ | TAIYIN_NATIVE_POSITION_SPEED;
    const Status status = calc_position_tdb_once(
        context, target_id, jd_tdb, resolved_jd_tt, state_flags, ignored_position, diagnostic, out);
    if (!native_position_should_try_barycenter_approx(target_id, flags, status)) {
        return status;
    }

    const int approx_target_id = native_position_barycenter_approx_target(target_id);
    EphemerisEvalDiagnostic approx_diagnostic;
    CartesianState approx_state;
    const uint32_t approx_flags = flags & ~TAIYIN_NATIVE_POSITION_ALLOW_BARYCENTER_APPROX;
    const Status approx_status = calc_position_tdb_once(
        context,
        approx_target_id,
        jd_tdb,
        resolved_jd_tt,
        approx_flags | TAIYIN_NATIVE_POSITION_XYZ | TAIYIN_NATIVE_POSITION_SPEED,
        ignored_position,
        &approx_diagnostic,
        &approx_state);
    if (approx_status != TAIYIN_STATUS_OK) {
        clear_state_out(out);
        return status;
    }

    *out = approx_state;
    if (diagnostic) {
        *diagnostic = approx_diagnostic;
        diagnostic->target_id = target_id;
        diagnostic->component_target_id = approx_target_id;
    }
    return TAIYIN_STATUS_OK;
}

bool register_global_native_position_evaluator(
    int target_id,
    NativePositionEvaluatorFn evaluator,
    NativeStateEvaluatorFn state_evaluator
) noexcept {
    if (target_id >= 0 || !evaluator) return false;
    try {
        std::lock_guard<std::mutex> lock(native_position_evaluators_mutex());
        const std::pair<NativePositionEvaluatorMap::iterator, bool> inserted =
            native_position_evaluators().emplace(
                target_id,
                NativePositionEvaluatorEntry(evaluator, state_evaluator));
        if (inserted.second) return true;
        return inserted.first->second.position_evaluator == evaluator
            && inserted.first->second.state_evaluator == state_evaluator;
    } catch (...) {
        return false;
    }
}

bool unregister_global_native_position_evaluator(int target_id) noexcept {
    if (target_id >= 0) return false;
    try {
        std::lock_guard<std::mutex> lock(native_position_evaluators_mutex());
        return native_position_evaluators().erase(target_id) != 0;
    } catch (...) {
        return false;
    }
}

Status calc_state_tt(
    const NativeCalcContext* context,
    int target_id,
    const SplitJulianDate& jd_tt,
    uint32_t flags,
    CartesianState* out,
    EphemerisEvalDiagnostic* diagnostic
) noexcept {
    clear_state_out(out);
    if (!context) {
        return fail_state(out, diagnostic, TAIYIN_ERROR_INVALID_ARGUMENT, target_id, 0, jd_tt);
    }
    const NativeCalcContext& ctx = *context;
    if (!out || !split_julian_date_is_finite(jd_tt)) {
        return fail_state(out, diagnostic, TAIYIN_ERROR_INVALID_ARGUMENT, target_id, ctx.observer_id, jd_tt);
    }
    SplitJulianDate jd_tdb;
    const Status time_status = resolve_tt_to_tdb(ctx, jd_tt, &jd_tdb);
    if (time_status != TAIYIN_STATUS_OK) {
        return fail_state(out, diagnostic, time_status, target_id, ctx.observer_id, jd_tt);
    }
    return calc_state_tdb(context, target_id, jd_tdb, jd_tt, flags, out, diagnostic);
}

Status calc_state_ut_delta_t(
    const NativeCalcContext* context,
    int target_id,
    const SplitJulianDate& jd_ut1,
    double delta_t_seconds,
    uint32_t flags,
    CartesianState* out,
    EphemerisEvalDiagnostic* diagnostic
) noexcept {
    clear_state_out(out);
    if (!context) {
        return fail_state(out, diagnostic, TAIYIN_ERROR_INVALID_ARGUMENT, target_id, 0, jd_ut1);
    }
    const NativeCalcContext& ctx = *context;
    if (!out || !split_julian_date_is_finite(jd_ut1) || !std::isfinite(delta_t_seconds)) {
        return fail_state(out, diagnostic, TAIYIN_ERROR_INVALID_ARGUMENT, target_id, ctx.observer_id, jd_ut1);
    }
    SplitJulianDate jd_tt;
    if (!ut1_to_tt_split_jd(jd_ut1, delta_t_seconds, &jd_tt)) {
        return fail_state(
            out, diagnostic, TAIYIN_ERROR_INVALID_ARGUMENT,
            target_id, ctx.observer_id, jd_ut1);
    }
    SplitJulianDate jd_tdb;
    const Status time_status = resolve_tt_to_tdb(ctx, jd_tt, &jd_tdb);
    if (time_status != TAIYIN_STATUS_OK) {
        return fail_state(out, diagnostic, time_status, target_id, ctx.observer_id, jd_ut1);
    }
    return calc_state_tdb(context, target_id, jd_tdb, jd_tt, flags, out, diagnostic);
}

Status calc_state_ut(
    const NativeCalcContext* context,
    int target_id,
    const SplitJulianDate& jd_ut,
    uint32_t flags,
    CartesianState* out,
    EphemerisEvalDiagnostic* diagnostic
) noexcept {
    clear_state_out(out);
    if (!context) {
        return fail_state(out, diagnostic, TAIYIN_ERROR_INVALID_ARGUMENT, target_id, 0, jd_ut);
    }
    const NativeCalcContext& ctx = *context;
    if (!out || !split_julian_date_is_finite(jd_ut)) {
        return fail_state(out, diagnostic, TAIYIN_ERROR_INVALID_ARGUMENT, target_id, ctx.observer_id, jd_ut);
    }
    const double delta_t_seconds = dispatch::eval_delta_t_with_ephemeris_correction(
        ctx.delta_t_model_id,
        ctx.ephemeris_family_id,
        jd_ut,
        0,
        0);
    return calc_state_ut_delta_t(context, target_id, jd_ut, delta_t_seconds, flags, out, diagnostic);
}

Status calc_state_utc(
    const NativeCalcContext* context,
    int target_id,
    const CalendarDateTime& datetime_utc,
    uint32_t flags,
    CartesianState* out,
    EphemerisEvalDiagnostic* diagnostic
) noexcept {
    clear_state_out(out);
    SplitJulianDate jd_utc;
    const bool has_jd_utc = julian_day_split(datetime_utc, &jd_utc);
    if (!context) {
        return fail_state(out, diagnostic, TAIYIN_ERROR_INVALID_ARGUMENT, target_id, 0, jd_utc);
    }
    const NativeCalcContext& ctx = *context;
    if (!out || !has_jd_utc) {
        return fail_state(out, diagnostic, TAIYIN_ERROR_INVALID_ARGUMENT, target_id, ctx.observer_id, jd_utc);
    }
    const internal::EarthOrientationTable* eop_table = global_earth_orientation_table();
    if (!eop_table) {
        return fail_state(out, diagnostic, TAIYIN_ERROR_UNSUPPORTED, target_id, ctx.observer_id, jd_utc);
    }

    TimeScaleOptions options;
    options.policy = TimeScalePrecise;
    options.tdb_model_id = ctx.model_context.tdb_model_id;
    options.delta_t_model_id = ctx.delta_t_model_id;
    options.ephemeris_family_id = ctx.ephemeris_family_id;
    options.leap_second_table = builtin_leap_second_table();
    PreciseTimeScales scales;
    TimeScaleDiagnostic time_diagnostic;
    if (!make_time_scales_from_utc(datetime_utc, eop_table, &options, &scales, &time_diagnostic)) {
        return fail_state(out, diagnostic, TAIYIN_ERROR_UNSUPPORTED, target_id, ctx.observer_id, jd_utc);
    }
    NativeCalcContext scratch = ctx;
    const SplitJulianDate resolved_jd_tdb = scales.jd_tdb;
    const SplitJulianDate resolved_jd_tt = scales.jd_tt;
    const Status cpo_status = apply_celestial_pole_offset_from_eop(
        &scratch, scales.jd_utc);
    if (cpo_status != TAIYIN_STATUS_OK) {
        return fail_state(
            out,
            diagnostic,
            cpo_status,
            target_id,
            ctx.observer_id,
            resolved_jd_tdb);
    }
    return calc_state_tdb(
        &scratch,
        target_id,
        resolved_jd_tdb,
        resolved_jd_tt,
        flags,
        out,
        diagnostic);
}

Status calc_default_position_tdb(
    int target_id,
    const SplitJulianDate& jd_tdb,
    const SplitJulianDate& jd_tt,
    uint32_t flags,
    double out[6],
    EphemerisEvalDiagnostic* diagnostic
) noexcept {
    const NativeCalcContext context = get_default_native_calc_context();
    return calc_position_tdb(&context, target_id, jd_tdb, jd_tt, flags, out, diagnostic);
}

Status calc_default_position_tt(
    int target_id,
    const SplitJulianDate& jd_tt,
    uint32_t flags,
    double out[6],
    EphemerisEvalDiagnostic* diagnostic
) noexcept {
    const NativeCalcContext context = get_default_native_calc_context();
    return calc_position_tt(&context, target_id, jd_tt, flags, out, diagnostic);
}

Status calc_default_position_ut_delta_t(
    int target_id,
    const SplitJulianDate& jd_ut1,
    double delta_t_seconds,
    uint32_t flags,
    double out[6],
    EphemerisEvalDiagnostic* diagnostic
) noexcept {
    const NativeCalcContext context = get_default_native_calc_context();
    return calc_position_ut_delta_t(&context, target_id, jd_ut1, delta_t_seconds, flags, out, diagnostic);
}

Status calc_default_position_ut(
    int target_id,
    const SplitJulianDate& jd_ut,
    uint32_t flags,
    double out[6],
    EphemerisEvalDiagnostic* diagnostic
) noexcept {
    const NativeCalcContext context = get_default_native_calc_context();
    return calc_position_ut(&context, target_id, jd_ut, flags, out, diagnostic);
}

Status calc_default_position_utc(
    int target_id,
    const CalendarDateTime& datetime_utc,
    uint32_t flags,
    double out[6],
    EphemerisEvalDiagnostic* diagnostic
) noexcept {
    const NativeCalcContext context = get_default_native_calc_context();
    return calc_position_utc(&context, target_id, datetime_utc, flags, out, diagnostic);
}

Status calc_default_state_tdb(
    int target_id,
    const SplitJulianDate& jd_tdb,
    const SplitJulianDate& jd_tt,
    uint32_t flags,
    CartesianState* out,
    EphemerisEvalDiagnostic* diagnostic
) noexcept {
    const NativeCalcContext context = get_default_native_calc_context();
    return calc_state_tdb(&context, target_id, jd_tdb, jd_tt, flags, out, diagnostic);
}

Status calc_default_state_tt(
    int target_id,
    const SplitJulianDate& jd_tt,
    uint32_t flags,
    CartesianState* out,
    EphemerisEvalDiagnostic* diagnostic
) noexcept {
    const NativeCalcContext context = get_default_native_calc_context();
    return calc_state_tt(&context, target_id, jd_tt, flags, out, diagnostic);
}

Status calc_default_state_ut_delta_t(
    int target_id,
    const SplitJulianDate& jd_ut1,
    double delta_t_seconds,
    uint32_t flags,
    CartesianState* out,
    EphemerisEvalDiagnostic* diagnostic
) noexcept {
    const NativeCalcContext context = get_default_native_calc_context();
    return calc_state_ut_delta_t(&context, target_id, jd_ut1, delta_t_seconds, flags, out, diagnostic);
}

Status calc_default_state_ut(
    int target_id,
    const SplitJulianDate& jd_ut,
    uint32_t flags,
    CartesianState* out,
    EphemerisEvalDiagnostic* diagnostic
) noexcept {
    const NativeCalcContext context = get_default_native_calc_context();
    return calc_state_ut(&context, target_id, jd_ut, flags, out, diagnostic);
}

Status calc_default_state_utc(
    int target_id,
    const CalendarDateTime& datetime_utc,
    uint32_t flags,
    CartesianState* out,
    EphemerisEvalDiagnostic* diagnostic
) noexcept {
    const NativeCalcContext context = get_default_native_calc_context();
    return calc_state_utc(&context, target_id, datetime_utc, flags, out, diagnostic);
}


}  // namespace runtime
}  // namespace taiyin
