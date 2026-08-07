#include "runtime/core/runtime_state_block_adapter.h"

#include "runtime/core/native_context_checks.h"

#include "taiyin/runtime/runtime.h"

#include <cmath>
#include <limits>

namespace taiyin {
namespace runtime {
namespace {

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

void set_zero_state(CartesianState* out) noexcept {
    if (!out) {
        return;
    }
    out->position_au = zero_vector();
    out->velocity_au_per_day = zero_vector();
    out->acceleration_au_per_day2 = zero_vector();
}

bool eval_runtime_block_state(
    const SplitJulianDate& jd_tdb,
    const void* data,
    uint32_t components,
    EphemerisResult* out
) noexcept {
    RuntimeCompiledBlockData* block_data =
        const_cast<RuntimeCompiledBlockData*>(static_cast<const RuntimeCompiledBlockData*>(data));
    if (!block_data || !out) {
        return false;
    }

    components &= internal::EPHEMERIS_BLOCK_COMPONENT_STATE;
    if (components == 0u) {
        components = internal::EPHEMERIS_BLOCK_COMPONENT_POSITION;
    }
    components |= block_data->preferred_components & internal::EPHEMERIS_BLOCK_COMPONENT_STATE;
    if ((components & internal::EPHEMERIS_BLOCK_COMPONENT_VELOCITY) != 0u) {
        components |= internal::EPHEMERIS_BLOCK_COMPONENT_POSITION;
    }
    if ((components & internal::EPHEMERIS_BLOCK_COMPONENT_ACCELERATION) != 0u) {
        components |= internal::EPHEMERIS_BLOCK_COMPONENT_POSITION | internal::EPHEMERIS_BLOCK_COMPONENT_VELOCITY;
    }

    if (block_data->evaluated
        && block_data->last_status == TAIYIN_STATUS_OK
        && block_data->cached_jd_tdb == jd_tdb
        && (block_data->cached_components & components) == components) {
        *out = block_data->cached_result;
        block_data->cache_hit = block_data->cache_hit && out->cache_hit;
        return native_cartesian_state_is_finite(out->state);
    }

    EphemerisEvalDiagnostic diagnostic;
    const Status status = eval_runtime_body_state(
        block_data->context,
        block_data->body_id,
        block_data->center_id,
        jd_tdb,
        components,
        block_data->context.route_rule_id,
        block_data->context.route_rules,
        out,
        &diagnostic);
    block_data->last_status = status;
    block_data->last_diagnostic = diagnostic;
    block_data->evaluated = true;
    if (status != TAIYIN_STATUS_OK) {
        return false;
    }
    block_data->cached_jd_tdb = jd_tdb;
    block_data->cached_components = components;
    block_data->cached_result = *out;
    block_data->cache_hit = block_data->cache_hit && out->cache_hit;
    return native_cartesian_state_is_finite(out->state);
}

bool runtime_block_position(const SplitJulianDate& jd_tdb, const void* data, Vector3* out) noexcept {
    if (!out) {
        return false;
    }
    EphemerisResult result;
    if (!eval_runtime_block_state(
            jd_tdb,
            data,
            internal::EPHEMERIS_BLOCK_COMPONENT_POSITION,
            &result)) {
        return false;
    }
    *out = result.state.position_au;
    return finite_vector(*out);
}

bool runtime_block_velocity(const SplitJulianDate& jd_tdb, const void* data, Vector3* out) noexcept {
    if (!out) {
        return false;
    }
    EphemerisResult result;
    if (!eval_runtime_block_state(
            jd_tdb,
            data,
            internal::EPHEMERIS_BLOCK_COMPONENT_POSITION | internal::EPHEMERIS_BLOCK_COMPONENT_VELOCITY,
            &result)) {
        return false;
    }
    *out = result.state.velocity_au_per_day;
    return finite_vector(*out);
}

bool runtime_block_acceleration(const SplitJulianDate& jd_tdb, const void* data, Vector3* out) noexcept {
    if (!out) {
        return false;
    }
    EphemerisResult result;
    if (!eval_runtime_block_state(
            jd_tdb,
            data,
            internal::EPHEMERIS_BLOCK_COMPONENT_STATE,
            &result)) {
        return false;
    }
    *out = result.state.acceleration_au_per_day2;
    return finite_vector(*out);
}

bool runtime_block_state(const SplitJulianDate& jd_tdb, const void* data, CartesianState* out) noexcept {
    if (!out) {
        return false;
    }
    EphemerisResult result;
    if (!eval_runtime_block_state(
            jd_tdb,
            data,
            internal::EPHEMERIS_BLOCK_COMPONENT_STATE,
            &result)) {
        return false;
    }
    *out = result.state;
    return native_cartesian_state_is_finite(*out);
}

}  // namespace

RuntimeStateEvalContext::RuntimeStateEvalContext() noexcept
    : service(0), use_global(true), route_rule_id(0), route_rules(0) {}

RuntimeCompiledBlockData::RuntimeCompiledBlockData() noexcept
    : context(),
      body_id(0),
      center_id(0),
      preferred_components(internal::EPHEMERIS_BLOCK_COMPONENT_POSITION),
      cached_jd_tdb(0, std::numeric_limits<double>::quiet_NaN()),
      cached_components(0),
      cached_result(),
      last_status(TAIYIN_STATUS_OK),
      last_diagnostic(),
      cache_hit(true),
      evaluated(false) {}

EphemerisRequest make_runtime_state_request(
    int target_id,
    int center_id,
    const SplitJulianDate& jd_tdb,
    uint32_t components,
    uint64_t route_rule_id,
    const internal::EphemerisRouteRuleTable* route_rules
) noexcept {
    EphemerisRequest request;
    request.target_id = target_id;
    request.center_id = center_id;
    request.frame = internal::EphemerisFrame::IcrfJ2000Equatorial;
    request.jd_tdb = jd_tdb;
    request.components = components;
    request.route_rule_id = route_rule_id;
    request.route_rules = route_rules;
    request.include_descriptor = false;
    return request;
}

Status eval_runtime_state_in_context(
    const RuntimeStateEvalContext& context,
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

    if (context.use_global) {
        return eval_global_ephemeris_state(request, out, diagnostic);
    }
    if (!context.service) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    return context.service->eval_state(request, out, diagnostic);
}

Status eval_runtime_body_state(
    const RuntimeStateEvalContext& context,
    int body_id,
    int center_id,
    const SplitJulianDate& jd_tdb,
    uint32_t components,
    uint64_t route_rule_id,
    const internal::EphemerisRouteRuleTable* route_rules,
    EphemerisResult* out,
    EphemerisEvalDiagnostic* diagnostic
) noexcept {
    return eval_runtime_state_in_context(
        context,
        make_runtime_state_request(body_id, center_id, jd_tdb, components, route_rule_id, route_rules),
        out,
        diagnostic);
}

internal::CompiledEphemerisBlock make_runtime_compiled_block(RuntimeCompiledBlockData* data) noexcept {
    internal::CompiledEphemerisBlock block;
    block.data = data;
    block.bytes = sizeof(RuntimeCompiledBlockData);
    block.position = &runtime_block_position;
    block.velocity = &runtime_block_velocity;
    block.acceleration = &runtime_block_acceleration;
    block.state = &runtime_block_state;
    block.format = internal::EphemerisBlockFormat::Custom;
    return block;
}

void copy_ephemeris_diagnostic(EphemerisEvalDiagnostic* dst, const EphemerisEvalDiagnostic& src) noexcept {
    if (dst) {
        *dst = src;
    }
}

Vector3 runtime_vector_from_array3(const double values[3]) noexcept {
    Vector3 out;
    out.x = values ? values[0] : 0.0;
    out.y = values ? values[1] : 0.0;
    out.z = values ? values[2] : 0.0;
    return out;
}

void runtime_vector_to_array3(const Vector3& value, double out[3]) noexcept {
    if (!out) {
        return;
    }
    out[0] = value.x;
    out[1] = value.y;
    out[2] = value.z;
}

}  // namespace runtime
}  // namespace taiyin
