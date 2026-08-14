#ifndef TAIYIN_RUNTIME_RUNTIME_STATE_BLOCK_ADAPTER_H
#define TAIYIN_RUNTIME_RUNTIME_STATE_BLOCK_ADAPTER_H

#include "taiyin/internal/ephemeris_block.h"
#include "taiyin/runtime/ephemeris_engine.h"
#include "taiyin/status.h"
#include "taiyin/vector3.h"

namespace taiyin {
namespace runtime {

struct NativeEphemerisStateCache;

struct RuntimeStateEvalContext {
    EphemerisEngine* service;
    bool use_global;
    uint64_t route_rule_id;
    const internal::EphemerisRouteRuleTable* route_rules;
    NativeEphemerisStateCache* epoch_state_cache;
    SplitJulianDate epoch_jd_tdb;

    RuntimeStateEvalContext() noexcept;
};

struct RuntimeCompiledBlockData {
    RuntimeStateEvalContext context;
    int body_id;
    int center_id;
    uint32_t preferred_components;
    mutable SplitJulianDate cached_jd_tdb;
    mutable uint32_t cached_components;
    mutable EphemerisResult cached_result;
    mutable Status last_status;
    mutable EphemerisEvalDiagnostic last_diagnostic;
    mutable bool cache_hit;
    mutable bool evaluated;

    RuntimeCompiledBlockData() noexcept;
};

EphemerisRequest make_runtime_state_request(
    int target_id,
    int center_id,
    const SplitJulianDate& jd_tdb,
    uint32_t components = internal::EPHEMERIS_BLOCK_COMPONENT_STATE,
    uint64_t route_rule_id = 0,
    const internal::EphemerisRouteRuleTable* route_rules = 0
) noexcept;

Status eval_runtime_state_in_context(
    const RuntimeStateEvalContext& context,
    const EphemerisRequest& request,
    EphemerisResult* out,
    EphemerisEvalDiagnostic* diagnostic
) noexcept;

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
) noexcept;

internal::CompiledEphemerisBlock make_runtime_compiled_block(RuntimeCompiledBlockData* data) noexcept;

void copy_ephemeris_diagnostic(EphemerisEvalDiagnostic* dst, const EphemerisEvalDiagnostic& src) noexcept;

Vector3 runtime_vector_from_array3(const double values[3]) noexcept;
void runtime_vector_to_array3(const Vector3& value, double out[3]) noexcept;

}  // namespace runtime
}  // namespace taiyin

#endif  // TAIYIN_RUNTIME_RUNTIME_STATE_BLOCK_ADAPTER_H
