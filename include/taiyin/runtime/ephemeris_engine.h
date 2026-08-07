#ifndef TAIYIN_RUNTIME_EPHEMERIS_ENGINE_H
#define TAIYIN_RUNTIME_EPHEMERIS_ENGINE_H

#include "taiyin/internal/ephemeris_block.h"
#include "taiyin/internal/ephemeris_catalog.h"
#include "taiyin/internal/ephemeris_route_rule.h"
#include "taiyin/internal/ephemeris_segment_cache.h"
#include "taiyin/internal/route_inflight_map.h"
#include "taiyin/runtime/body_registry.h"
#include "taiyin/state.h"
#include "taiyin/status.h"

#include <stddef.h>
#include <stdint.h>

namespace taiyin {
namespace runtime {

const uint8_t TAIYIN_TIME_DIAGNOSTIC_USED_LEAP_SECONDS = 1u << 0;
const uint8_t TAIYIN_TIME_DIAGNOSTIC_USED_EOP = 1u << 1;
const uint8_t TAIYIN_TIME_DIAGNOSTIC_USED_DELTA_T_MODEL = 1u << 2;

struct EphemerisRequest {
    int target_id;
    int center_id;
    internal::EphemerisFrame frame;
    SplitJulianDate jd_tdb;
    uint32_t components;
    uint64_t route_rule_id;
    const internal::EphemerisRouteRuleTable* route_rules;
    bool include_descriptor;

    EphemerisRequest()
        : target_id(0),
          center_id(0),
          frame(internal::EphemerisFrame::FrameUnknown),
          jd_tdb(),
          components(internal::EPHEMERIS_BLOCK_COMPONENT_STATE),
          route_rule_id(0),
          route_rules(0),
          include_descriptor(true) {}
};

struct EphemerisResult {
    CartesianState state;
    internal::EphemerisBlockDescriptor descriptor;
    bool cache_hit;

    EphemerisResult()
        : state(), descriptor(), cache_hit(false) {}
};

struct EphemerisSelectionResult {
    internal::EphemerisBlockDescriptor source_descriptor;
    bool has_source_descriptor;
    bool cache_hit;
    bool loaded;

    EphemerisSelectionResult()
        : source_descriptor(), has_source_descriptor(false), cache_hit(false), loaded(false) {}
};

struct EphemerisEvalDiagnostic {
    Status status;
    int target_id;
    int center_id;
    internal::EphemerisFrame frame;
    SplitJulianDate jd_tdb;
    int candidate_count;
    int attempted_method_id;
    double nearest_coverage_start;
    double nearest_coverage_end;
    int component_target_id;
    int component_center_id;
    int component_method_id;
    uint8_t time_scale_route;
    uint8_t time_scale_fallback_reason;
    uint8_t time_scale_flags;
    double tai_minus_utc_seconds;
    double dut1_seconds;
    double delta_t_seconds;

    EphemerisEvalDiagnostic() noexcept
        : status(TAIYIN_STATUS_OK),
          target_id(0),
          center_id(0),
          frame(internal::EphemerisFrame::FrameUnknown),
          jd_tdb(),
          candidate_count(0),
          attempted_method_id(0),
          nearest_coverage_start(0.0),
          nearest_coverage_end(0.0),
          component_target_id(0),
          component_center_id(0),
          component_method_id(0),
          time_scale_route(0),
          time_scale_fallback_reason(0),
          time_scale_flags(0),
          tai_minus_utc_seconds(0.0),
          dut1_seconds(0.0),
          delta_t_seconds(0.0) {}
};

size_t format_ephemeris_eval_diagnostic(
    const EphemerisEvalDiagnostic& diagnostic,
    char* buffer,
    size_t buffer_size
) noexcept;

class EphemerisEngine {
public:
    EphemerisEngine() noexcept;
    ~EphemerisEngine() noexcept;

    EphemerisEngine(const EphemerisEngine&) = delete;
    EphemerisEngine& operator=(const EphemerisEngine&) = delete;

    void set_catalog(internal::EphemerisBlockCatalog* catalog) noexcept;
    void set_segment_cache(internal::EphemerisSegmentCache* cache) noexcept;
    void set_body_registry(const EphemerisBodyRegistry* registry) noexcept;
    void set_default_route_rules(const internal::EphemerisRouteRuleTable* rules) noexcept;

    const internal::EphemerisBlockCatalog* catalog() const noexcept;
    internal::EphemerisSegmentCache* segment_cache() const noexcept;
    const EphemerisBodyRegistry* body_registry() const noexcept;
    const internal::EphemerisRouteRuleTable* default_route_rules() const noexcept;
    bool find_descriptor(
        const EphemerisRequest& request,
        internal::EphemerisBlockDescriptor* out
    ) const noexcept;
    Status eval_state(
        const EphemerisRequest& request,
        EphemerisResult* out,
        EphemerisEvalDiagnostic* diagnostic
    ) noexcept;
    Status eval_direct_body_state(
        const EphemerisRequest& request,
        EphemerisResult* out,
        EphemerisEvalDiagnostic* diagnostic
    ) noexcept;

private:
    Status eval_direct_state(
        const EphemerisRequest& request,
        EphemerisSelectionResult* selection,
        CartesianState* out,
        EphemerisEvalDiagnostic* diagnostic
    ) noexcept;
    Status eval_direct_state_for_rule(
        const EphemerisRequest& request,
        const internal::EphemerisRouteRule& rule,
        EphemerisSelectionResult* selection,
        CartesianState* out,
        EphemerisEvalDiagnostic* diagnostic
    ) noexcept;
    Status eval_direct_body_state_for_rule(
        const EphemerisRequest& request,
        const internal::EphemerisRouteRule& rule,
        EphemerisResult* out,
        EphemerisEvalDiagnostic* diagnostic
    ) noexcept;
    Status eval_state_for_rule(
        const EphemerisRequest& request,
        const internal::EphemerisRouteRule& rule,
        EphemerisResult* out,
        EphemerisEvalDiagnostic* diagnostic
    ) noexcept;
    Status eval_body_wrt_sun_for_rule(
        int body_id,
        internal::EphemerisFrame frame,
        const SplitJulianDate& jd_tdb,
        uint32_t components,
        uint64_t route_rule_id,
        const internal::EphemerisRouteRule& rule,
        bool include_descriptor,
        EphemerisResult* out,
        EphemerisEvalDiagnostic* diagnostic
    ) noexcept;
    Status eval_method_queue_state(
        const EphemerisRequest& request,
        EphemerisSelectionResult* selection,
        CartesianState* out,
        EphemerisEvalDiagnostic* diagnostic
    ) noexcept;
    Status eval_method_descriptor_state(
        const internal::EphemerisBlockDescriptor& source,
        const SplitJulianDate& jd_tdb,
        uint32_t components,
        bool include_descriptor,
        EphemerisSelectionResult* selection,
        CartesianState* out,
        EphemerisEvalDiagnostic* diagnostic
    ) noexcept;

    internal::EphemerisBlockCatalog* catalog_;
    internal::EphemerisSegmentCache* segment_cache_;
    const internal::EphemerisRouteRuleTable* default_route_rules_;
    const EphemerisBodyRegistry* body_registry_;
    internal::RouteInflightMap inflight_;
};

}  // namespace runtime
}  // namespace taiyin

#endif  // TAIYIN_RUNTIME_EPHEMERIS_ENGINE_H
