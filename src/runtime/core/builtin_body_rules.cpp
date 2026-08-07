#include "taiyin/runtime/builtin_body_rules.h"

#include "taiyin/body_id.h"
#include "taiyin/physical_constants.h"
#include "taiyin/runtime/ephemeris_engine.h"
#include "taiyin/state.h"

#include <algorithm>

namespace taiyin {
namespace runtime {
namespace {

EphemerisRequest make_body_request(
    int target_id,
    int center_id,
    internal::EphemerisFrame frame,
    const SplitJulianDate& jd_tdb,
    uint32_t components,
    uint64_t route_rule_id,
    const internal::EphemerisRouteRuleTable* route_rules
) noexcept {
    EphemerisRequest request;
    request.target_id = target_id;
    request.center_id = center_id;
    request.frame = frame;
    request.jd_tdb = jd_tdb;
    request.components = components;
    request.route_rule_id = route_rule_id;
    request.route_rules = route_rules;
    return request;
}

Status set_status(EphemerisEvalDiagnostic* diagnostic, Status status) noexcept {
    if (diagnostic) {
        diagnostic->status = status;
    }
    return status;
}

Status component_status(Status status) noexcept {
    if (status == TAIYIN_EPHEMERIS_ERROR_NO_ROUTE) {
        return TAIYIN_EPHEMERIS_ERROR_COMPOSITE_MISSING_COMPONENT;
    }
    if (status == TAIYIN_EPHEMERIS_ERROR_COVERAGE_GAP) {
        return TAIYIN_EPHEMERIS_ERROR_COMPOSITE_COVERAGE_GAP;
    }
    return status;
}

int barycenter_for_body(int body_id) noexcept {
    switch (body_id) {
    case TAIYIN_BODY_MERCURY:
        return TAIYIN_BODY_MERCURY_BARYCENTER;
    case TAIYIN_BODY_VENUS:
        return TAIYIN_BODY_VENUS_BARYCENTER;
    case TAIYIN_BODY_MARS:
        return TAIYIN_BODY_MARS_BARYCENTER;
    case TAIYIN_BODY_JUPITER:
        return TAIYIN_BODY_JUPITER_BARYCENTER;
    case TAIYIN_BODY_SATURN:
        return TAIYIN_BODY_SATURN_BARYCENTER;
    case TAIYIN_BODY_URANUS:
        return TAIYIN_BODY_URANUS_BARYCENTER;
    case TAIYIN_BODY_NEPTUNE:
        return TAIYIN_BODY_NEPTUNE_BARYCENTER;
    case TAIYIN_BODY_PLUTO:
        return TAIYIN_BODY_PLUTO_BARYCENTER;
    default:
        return 0;
    }
}

bool body_is_barycenter_alias(int body_id) noexcept {
    return body_id == TAIYIN_BODY_MERCURY || body_id == TAIYIN_BODY_VENUS;
}

Status eval_emb_moon_composite(
    EphemerisEngine* service,
    const EphemerisRequest& request,
    bool earth,
    EphemerisResult* out,
    EphemerisEvalDiagnostic* diagnostic
) {
    if (!service || !out) {
        return set_status(diagnostic, TAIYIN_ERROR_INVALID_ARGUMENT);
    }

    EphemerisResult emb_result;
    const EphemerisRequest emb_request = make_body_request(
        TAIYIN_BODY_EMB,
        TAIYIN_BODY_SUN,
        request.frame,
        request.jd_tdb,
        request.components,
        request.route_rule_id,
        request.route_rules);
    Status status = service->eval_state(emb_request, &emb_result, diagnostic);
    if (status != TAIYIN_STATUS_OK) {
        if (diagnostic) {
            diagnostic->component_target_id = emb_request.target_id;
            diagnostic->component_center_id = emb_request.center_id;
            diagnostic->component_method_id = emb_result.descriptor.method_id;
        }
        return set_status(diagnostic, component_status(status));
    }

    EphemerisResult moon_result;
    const EphemerisRequest moon_request = make_body_request(
        TAIYIN_BODY_MOON,
        TAIYIN_BODY_EARTH,
        request.frame,
        request.jd_tdb,
        request.components,
        request.route_rule_id,
        request.route_rules);
    status = service->eval_direct_body_state(moon_request, &moon_result, diagnostic);
    if (status != TAIYIN_STATUS_OK) {
        if (diagnostic) {
            diagnostic->component_target_id = moon_request.target_id;
            diagnostic->component_center_id = moon_request.center_id;
            diagnostic->component_method_id = moon_result.descriptor.method_id;
        }
        return set_status(diagnostic, component_status(status));
    }

    const double earth_factor = 1.0 / (1.0 + TAIYIN_EARTH_MOON_MASS_RATIO);
    const double moon_factor = TAIYIN_EARTH_MOON_MASS_RATIO / (1.0 + TAIYIN_EARTH_MOON_MASS_RATIO);
    if (earth) {
        out->state = cartesian_state_subtract(
            emb_result.state,
            cartesian_state_scale(moon_result.state, earth_factor));
    } else {
        out->state = cartesian_state_add(
            emb_result.state,
            cartesian_state_scale(moon_result.state, moon_factor));
    }

    internal::EphemerisBlockDescriptor descriptor = emb_result.descriptor;
    descriptor.target_id = request.target_id;
    descriptor.center_id = request.center_id;
    descriptor.method_id = emb_result.descriptor.method_id;
    descriptor.frame = request.frame;
    descriptor.route_key = internal::EphemerisRouteKey(
        request.target_id,
        request.center_id,
        emb_result.descriptor.route_key.method_id,
        emb_result.descriptor.route_key.bucket_id);
    descriptor.jd_tdb_start = std::max(
        emb_result.descriptor.jd_tdb_start,
        moon_result.descriptor.jd_tdb_start);
    descriptor.jd_tdb_end = std::min(
        emb_result.descriptor.jd_tdb_end,
        moon_result.descriptor.jd_tdb_end);
    descriptor.path.clear();

    out->descriptor = descriptor;
    out->cache_hit = emb_result.cache_hit && moon_result.cache_hit;
    return set_status(diagnostic, TAIYIN_STATUS_OK);
}

Status eval_body_from_barycenter_offset(
    EphemerisEngine* service,
    const EphemerisRequest& request,
    EphemerisResult* out,
    EphemerisEvalDiagnostic* diagnostic
) {
    if (!service || !out) {
        return set_status(diagnostic, TAIYIN_ERROR_INVALID_ARGUMENT);
    }

    const int barycenter_id = barycenter_for_body(request.target_id);
    if (barycenter_id == 0) {
        return set_status(diagnostic, TAIYIN_EPHEMERIS_ERROR_NO_ROUTE);
    }

    EphemerisResult barycenter_result;
    const EphemerisRequest barycenter_request = make_body_request(
        barycenter_id,
        request.center_id,
        request.frame,
        request.jd_tdb,
        request.components,
        request.route_rule_id,
        request.route_rules);
    Status status = service->eval_state(barycenter_request, &barycenter_result, diagnostic);
    if (status != TAIYIN_STATUS_OK) {
        if (diagnostic) {
            diagnostic->component_target_id = barycenter_request.target_id;
            diagnostic->component_center_id = barycenter_request.center_id;
            diagnostic->component_method_id = barycenter_result.descriptor.method_id;
        }
        return set_status(diagnostic, component_status(status));
    }

    if (body_is_barycenter_alias(request.target_id)) {
        out->state = barycenter_result.state;
        out->descriptor = barycenter_result.descriptor;
        out->descriptor.target_id = request.target_id;
        out->descriptor.center_id = request.center_id;
        out->descriptor.frame = request.frame;
        out->descriptor.route_key = internal::EphemerisRouteKey(
            request.target_id,
            request.center_id,
            barycenter_result.descriptor.route_key.method_id,
            barycenter_result.descriptor.route_key.bucket_id);
        out->descriptor.path.clear();
        out->cache_hit = barycenter_result.cache_hit;
        return set_status(diagnostic, TAIYIN_STATUS_OK);
    }

    EphemerisResult offset_result;
    const EphemerisRequest offset_request = make_body_request(
        request.target_id,
        barycenter_id,
        request.frame,
        request.jd_tdb,
        request.components,
        request.route_rule_id,
        request.route_rules);
    status = service->eval_direct_body_state(offset_request, &offset_result, diagnostic);
    if (status != TAIYIN_STATUS_OK) {
        if (diagnostic) {
            diagnostic->component_target_id = offset_request.target_id;
            diagnostic->component_center_id = offset_request.center_id;
            diagnostic->component_method_id = offset_result.descriptor.method_id;
        }
        return set_status(diagnostic, component_status(status));
    }

    out->state = cartesian_state_add(barycenter_result.state, offset_result.state);

    internal::EphemerisBlockDescriptor descriptor = barycenter_result.descriptor;
    descriptor.target_id = request.target_id;
    descriptor.center_id = request.center_id;
    descriptor.frame = request.frame;
    descriptor.route_key = internal::EphemerisRouteKey(
        request.target_id,
        request.center_id,
        barycenter_result.descriptor.route_key.method_id,
        barycenter_result.descriptor.route_key.bucket_id);
    descriptor.jd_tdb_start = std::max(
        barycenter_result.descriptor.jd_tdb_start,
        offset_result.descriptor.jd_tdb_start);
    descriptor.jd_tdb_end = std::min(
        barycenter_result.descriptor.jd_tdb_end,
        offset_result.descriptor.jd_tdb_end);
    descriptor.path.clear();

    out->descriptor = descriptor;
    out->cache_hit = barycenter_result.cache_hit && offset_result.cache_hit;
    return set_status(diagnostic, TAIYIN_STATUS_OK);
}

}  // namespace

Status eval_earth_from_emb_moon(
    EphemerisEngine* service,
    const EphemerisRequest& request,
    EphemerisResult* out,
    EphemerisEvalDiagnostic* diagnostic
) {
    return eval_emb_moon_composite(service, request, true, out, diagnostic);
}

Status eval_moon_from_emb_moon(
    EphemerisEngine* service,
    const EphemerisRequest& request,
    EphemerisResult* out,
    EphemerisEvalDiagnostic* diagnostic
) {
    return eval_emb_moon_composite(service, request, false, out, diagnostic);
}

bool register_builtin_body_rules(EphemerisBodyRegistry& registry) noexcept {
    return registry.set_fallback(TAIYIN_BODY_EARTH, eval_earth_from_emb_moon)
        && registry.set_fallback(TAIYIN_BODY_MOON, eval_moon_from_emb_moon)
        && registry.set_fallback(TAIYIN_BODY_MERCURY, eval_body_from_barycenter_offset)
        && registry.set_fallback(TAIYIN_BODY_VENUS, eval_body_from_barycenter_offset)
        && registry.set_fallback(TAIYIN_BODY_MARS, eval_body_from_barycenter_offset)
        && registry.set_fallback(TAIYIN_BODY_JUPITER, eval_body_from_barycenter_offset)
        && registry.set_fallback(TAIYIN_BODY_SATURN, eval_body_from_barycenter_offset)
        && registry.set_fallback(TAIYIN_BODY_URANUS, eval_body_from_barycenter_offset)
        && registry.set_fallback(TAIYIN_BODY_NEPTUNE, eval_body_from_barycenter_offset)
        && registry.set_fallback(TAIYIN_BODY_PLUTO, eval_body_from_barycenter_offset);
}

}  // namespace runtime
}  // namespace taiyin
