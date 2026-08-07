#include "runtime/visibility/planet_visibility_internal.h"

#include "runtime/core/native_context_checks.h"
#include "runtime/visibility/visibility_angle_search_internal.h"
#include "runtime/visibility/visibility_math_internal.h"
#include "runtime/visibility/visibility_sampling_internal.h"

#include "taiyin/angle.h"
#include "taiyin/body_id.h"
#include "taiyin/internal/body_disc_radius.h"
#include "taiyin/runtime/observed_position.h"

#include <cmath>

namespace taiyin {
namespace runtime {
namespace {

constexpr double kDefaultCoarseStepDays = 1.0 / 24.0;
constexpr double kDefaultRootToleranceDays = 1.0e-10;
constexpr double kDefaultResidualToleranceRad = 1.0e-10;

bool valid_event_kind(int event_kind) noexcept {
    return event_kind == TAIYIN_PLANET_VISIBILITY_EVENT_RISE
        || event_kind == TAIYIN_PLANET_VISIBILITY_EVENT_SET;
}

bool valid_transit_event_kind(int event_kind) noexcept {
    return event_kind == TAIYIN_PLANET_VISIBILITY_EVENT_UPPER_TRANSIT
        || event_kind == TAIYIN_PLANET_VISIBILITY_EVENT_LOWER_TRANSIT;
}

bool valid_limb_kind(int limb_kind) noexcept {
    return limb_kind == TAIYIN_PLANET_VISIBILITY_LIMB_UPPER
        || limb_kind == TAIYIN_PLANET_VISIBILITY_LIMB_CENTER
        || limb_kind == TAIYIN_PLANET_VISIBILITY_LIMB_LOWER;
}

bool valid_planet_body_id(int body_id) noexcept {
    switch (body_id) {
    case TAIYIN_BODY_MERCURY:
    case TAIYIN_BODY_VENUS:
    case TAIYIN_BODY_MARS:
    case TAIYIN_BODY_JUPITER:
    case TAIYIN_BODY_SATURN:
    case TAIYIN_BODY_URANUS:
    case TAIYIN_BODY_NEPTUNE:
    case TAIYIN_BODY_PLUTO:
        return true;
    default:
        return false;
    }
}

int crossing_direction_for_event(int event_kind) noexcept {
    return event_kind == TAIYIN_PLANET_VISIBILITY_EVENT_RISE
        ? TAIYIN_VISIBILITY_CROSSING_RISING
        : TAIYIN_VISIBILITY_CROSSING_SETTING;
}

VisibilityAltitudeSearchSpec base_planet_spec(
    int body_id,
    const SplitJulianDate& start_jd_ut,
    const SplitJulianDate& end_jd_ut,
    int event_kind,
    double target_altitude_rad
) noexcept {
    VisibilityAltitudeSearchSpec spec;
    spec.body_id = body_id;
    spec.crossing_direction = crossing_direction_for_event(event_kind);
    spec.start_jd_ut = start_jd_ut;
    spec.end_jd_ut = end_jd_ut;
    spec.target_altitude_rad = target_altitude_rad;
    spec.physical_radius_km = ::taiyin::internal::body_disc_radius_km(
        body_id, ::taiyin::internal::BodyDiscRadiusConvention::MeanPhysical);
    spec.coarse_step_days = kDefaultCoarseStepDays;
    spec.root_tolerance_days = kDefaultRootToleranceDays;
    spec.residual_tolerance_rad = kDefaultResidualToleranceRad;
    spec.observed_flags = 0u;
    return spec;
}

bool resolve_public_planet_visibility_flags(
    uint64_t input_flags,
    uint64_t* out_internal_flags
) noexcept {
    if (!out_internal_flags) return false;
    if ((input_flags & TAIYIN_PLANET_VISIBILITY_FLAG_REFRACTION) != 0u
        && (input_flags & TAIYIN_PLANET_VISIBILITY_FLAG_NO_REFRACTION) != 0u) {
        return false;
    }
    const uint64_t known_flags =
        TAIYIN_PLANET_VISIBILITY_FLAG_REFRACTION
        | TAIYIN_PLANET_VISIBILITY_FLAG_NO_REFRACTION
        | TAIYIN_PLANET_VISIBILITY_STRICT_METEOROLOGY;
    if ((input_flags & ~known_flags) != 0u) return false;
    uint64_t flags = input_flags
        & ~static_cast<uint64_t>(TAIYIN_PLANET_VISIBILITY_FLAG_NO_REFRACTION);
    if ((input_flags & TAIYIN_PLANET_VISIBILITY_FLAG_NO_REFRACTION) == 0u) {
        flags |= TAIYIN_PLANET_VISIBILITY_FLAG_REFRACTION;
    }
    *out_internal_flags = flags;
    return true;
}

int public_altitude_state(int internal_state) noexcept {
    switch (internal_state) {
    case TAIYIN_VISIBILITY_ALTITUDE_STATE_CROSSES:
        return TAIYIN_PLANET_VISIBILITY_ALTITUDE_STATE_CROSSES;
    case TAIYIN_VISIBILITY_ALTITUDE_STATE_ALWAYS_ABOVE:
        return TAIYIN_PLANET_VISIBILITY_ALTITUDE_STATE_ALWAYS_ABOVE;
    case TAIYIN_VISIBILITY_ALTITUDE_STATE_ALWAYS_BELOW:
        return TAIYIN_PLANET_VISIBILITY_ALTITUDE_STATE_ALWAYS_BELOW;
    case TAIYIN_VISIBILITY_ALTITUDE_STATE_TANGENT:
        return TAIYIN_PLANET_VISIBILITY_ALTITUDE_STATE_TANGENT;
    default:
        return TAIYIN_PLANET_VISIBILITY_ALTITUDE_STATE_NOT_FOUND;
    }
}

int public_crossing_direction(int internal_direction) noexcept {
    switch (internal_direction) {
    case TAIYIN_VISIBILITY_CROSSING_RISING:
        return TAIYIN_PLANET_VISIBILITY_CROSSING_RISING;
    case TAIYIN_VISIBILITY_CROSSING_SETTING:
        return TAIYIN_PLANET_VISIBILITY_CROSSING_SETTING;
    default:
        return TAIYIN_PLANET_VISIBILITY_CROSSING_ANY;
    }
}

void copy_result(
    const VisibilityAltitudeSearchResult& src,
    PlanetVisibilityEventResult* dst
) noexcept {
    if (!dst) return;
    dst->altitude_state = public_altitude_state(src.altitude_state);
    dst->crossing_direction = public_crossing_direction(src.crossing_direction);
    dst->jd_ut = src.jd_ut;
    dst->residual_rad = src.residual_rad;
    dst->min_residual_rad = src.min_residual_rad;
    dst->max_residual_rad = src.max_residual_rad;
    dst->min_residual_jd_ut = src.min_residual_jd_ut;
    dst->max_residual_jd_ut = src.max_residual_jd_ut;
    dst->sample_count = src.sample_count;
    dst->refine_count = src.refine_count;
}

double transit_base_target_rad(int event_kind) noexcept {
    return event_kind == TAIYIN_PLANET_VISIBILITY_EVENT_UPPER_TRANSIT ? 0.0 : TAIYIN_PI;
}

struct PlanetTransitSampleData {
    const NativeCalcContext* context;
    int body_id;
};

Status sample_planet_hour_angle_ut(
    const void* user_data,
    const SplitJulianDate& jd_ut,
    double reference_hour_angle_rad,
    bool has_reference,
    double* out_hour_angle_rad,
    EphemerisEvalDiagnostic* diagnostic
) noexcept {
    if (!out_hour_angle_rad) return TAIYIN_ERROR_INVALID_ARGUMENT;
    *out_hour_angle_rad = NAN;
    const PlanetTransitSampleData* data = static_cast<const PlanetTransitSampleData*>(user_data);
    if (!data || !data->context || !valid_planet_body_id(data->body_id)) return TAIYIN_ERROR_INVALID_ARGUMENT;
    double altitude = 0.0;
    double azimuth = 0.0;
    double hour_angle = 0.0;
    double distance = 0.0;
    const Status st = visibility_sample_body_center_horizontal_ut(
        data->context,
        data->body_id,
        jd_ut,
        0u,
        &altitude,
        &azimuth,
        &hour_angle,
        &distance,
        0,
        0,
        diagnostic);
    if (st != TAIYIN_STATUS_OK) return st;
    double continuous_hour_angle = hour_angle;
    if (has_reference) {
        continuous_hour_angle = reference_hour_angle_rad
            + normalize_signed_radians(hour_angle - reference_hour_angle_rad);
    }
    if (!std::isfinite(continuous_hour_angle)) return TAIYIN_EPHEMERIS_ERROR_EVAL_FAILED;
    *out_hour_angle_rad = continuous_hour_angle;
    (void)altitude;
    (void)azimuth;
    (void)distance;
    return TAIYIN_STATUS_OK;
}

}  // namespace

PlanetVisibilityEventResult::PlanetVisibilityEventResult() noexcept
    : altitude_state(TAIYIN_PLANET_VISIBILITY_ALTITUDE_STATE_NOT_FOUND),
      crossing_direction(TAIYIN_PLANET_VISIBILITY_CROSSING_ANY),
      jd_ut(0, NAN),
      residual_rad(NAN),
      min_residual_rad(NAN),
      max_residual_rad(NAN),
      min_residual_jd_ut(0, NAN),
      max_residual_jd_ut(0, NAN),
      sample_count(0),
      refine_count(0) {}

Status search_planet_rise_set_ut(
    const NativeCalcContext* context,
    int body_id,
    const SplitJulianDate& start_jd_ut,
    const SplitJulianDate& end_jd_ut,
    int event_kind,
    int limb_kind,
    uint64_t planet_visibility_flags,
    PlanetVisibilityEventResult* out,
    EphemerisEvalDiagnostic* diagnostic
) noexcept {
    return search_planet_rise_set_at_horizon_ut(
        context,
        body_id,
        start_jd_ut,
        end_jd_ut,
        event_kind,
        limb_kind,
        0.0,
        planet_visibility_flags,
        out,
        diagnostic);
}

Status search_planet_rise_set_at_horizon_ut(
    const NativeCalcContext* context,
    int body_id,
    const SplitJulianDate& start_jd_ut,
    const SplitJulianDate& end_jd_ut,
    int event_kind,
    int limb_kind,
    double horizon_altitude_rad,
    uint64_t planet_visibility_flags,
    PlanetVisibilityEventResult* out,
    EphemerisEvalDiagnostic* diagnostic
) noexcept {
    if (out) *out = PlanetVisibilityEventResult();
    if (!context || !out) return TAIYIN_ERROR_INVALID_ARGUMENT;
    uint64_t internal_flags = 0u;
    if (!resolve_public_planet_visibility_flags(planet_visibility_flags, &internal_flags)) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    VisibilityAltitudeSearchResult internal_result;
    const Status st = planet_visibility_search_rise_set_at_horizon_ut(
        context,
        body_id,
        start_jd_ut,
        end_jd_ut,
        event_kind,
        limb_kind,
        horizon_altitude_rad,
        internal_flags,
        &internal_result,
        diagnostic);
    if (st != TAIYIN_STATUS_OK) return st;
    copy_result(internal_result, out);
    return TAIYIN_STATUS_OK;
}

Status search_planet_transit_ut(
    const NativeCalcContext* context,
    int body_id,
    const SplitJulianDate& start_jd_ut,
    const SplitJulianDate& end_jd_ut,
    int event_kind,
    PlanetVisibilityEventResult* out,
    EphemerisEvalDiagnostic* diagnostic
) noexcept {
    if (out) *out = PlanetVisibilityEventResult();
    if (!out) return TAIYIN_ERROR_INVALID_ARGUMENT;
    VisibilityAltitudeSearchResult internal_result;
    const Status st = planet_visibility_search_transit_ut(
        context,
        body_id,
        start_jd_ut,
        end_jd_ut,
        event_kind,
        &internal_result,
        diagnostic);
    if (st != TAIYIN_STATUS_OK) return st;
    copy_result(internal_result, out);
    return TAIYIN_STATUS_OK;
}

Status planet_visibility_search_rise_set_ut(
    const NativeCalcContext* context,
    int body_id,
    const SplitJulianDate& start_jd_ut,
    const SplitJulianDate& end_jd_ut,
    int event_kind,
    int limb_kind,
    uint64_t planet_visibility_flags,
    VisibilityAltitudeSearchResult* out,
    EphemerisEvalDiagnostic* diagnostic
) noexcept {
    return planet_visibility_search_rise_set_at_horizon_ut(
        context,
        body_id,
        start_jd_ut,
        end_jd_ut,
        event_kind,
        limb_kind,
        0.0,
        planet_visibility_flags,
        out,
        diagnostic);
}

Status planet_visibility_search_rise_set_at_horizon_ut(
    const NativeCalcContext* context,
    int body_id,
    const SplitJulianDate& start_jd_ut,
    const SplitJulianDate& end_jd_ut,
    int event_kind,
    int limb_kind,
    double horizon_altitude_rad,
    uint64_t planet_visibility_flags,
    VisibilityAltitudeSearchResult* out,
    EphemerisEvalDiagnostic* diagnostic
) noexcept {
    if (!valid_planet_body_id(body_id)
        || !valid_event_kind(event_kind)
        || !valid_limb_kind(limb_kind)
        || !std::isfinite(horizon_altitude_rad)
        || (planet_visibility_flags & ~(TAIYIN_PLANET_VISIBILITY_FLAG_REFRACTION
            | TAIYIN_PLANET_VISIBILITY_STRICT_METEOROLOGY)) != 0u) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    VisibilityAltitudeSearchSpec spec = base_planet_spec(
        body_id,
        start_jd_ut,
        end_jd_ut,
        event_kind,
        horizon_altitude_rad);
    const bool use_refraction = (planet_visibility_flags & TAIYIN_PLANET_VISIBILITY_FLAG_REFRACTION) != 0u;
    if (use_refraction
        && (planet_visibility_flags & TAIYIN_PLANET_VISIBILITY_STRICT_METEOROLOGY) != 0u) {
        NativeAtmosphere atmosphere;
        if (!context || !native_context_resolve_refraction_atmosphere(*context, false, &atmosphere)) {
            return TAIYIN_ERROR_INVALID_ARGUMENT;
        }
    }
    if (limb_kind == TAIYIN_PLANET_VISIBILITY_LIMB_CENTER) {
        spec.residual_mode = TAIYIN_VISIBILITY_RESIDUAL_CENTER_ALTITUDE;
        spec.observed_flags = use_refraction ? TAIYIN_OBSERVED_REFRACTION : 0u;
    } else if (limb_kind == TAIYIN_PLANET_VISIBILITY_LIMB_LOWER) {
        spec.residual_mode = use_refraction
            ? TAIYIN_VISIBILITY_RESIDUAL_APPARENT_LOWER_LIMB
            : TAIYIN_VISIBILITY_RESIDUAL_TRUE_LOWER_LIMB;
    } else {
        spec.residual_mode = use_refraction
            ? TAIYIN_VISIBILITY_RESIDUAL_APPARENT_UPPER_LIMB
            : TAIYIN_VISIBILITY_RESIDUAL_TRUE_UPPER_LIMB;
    }
    if ((planet_visibility_flags & TAIYIN_PLANET_VISIBILITY_STRICT_METEOROLOGY) != 0u) {
        spec.observed_flags |= TAIYIN_OBSERVED_STRICT_METEOROLOGY;
    }
    return visibility_search_altitude_interval_ut(context, spec, out, diagnostic);
}

Status planet_visibility_search_transit_ut(
    const NativeCalcContext* context,
    int body_id,
    const SplitJulianDate& start_jd_ut,
    const SplitJulianDate& end_jd_ut,
    int event_kind,
    VisibilityAltitudeSearchResult* out,
    EphemerisEvalDiagnostic* diagnostic
) noexcept {
    if (!context
        || !out
        || !valid_planet_body_id(body_id)
        || !valid_transit_event_kind(event_kind)
        || !split_julian_date_is_finite(start_jd_ut)
        || !split_julian_date_is_finite(end_jd_ut)
        || !(end_jd_ut > start_jd_ut)) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    PlanetTransitSampleData data = { context, body_id };
    VisibilityAngleTargetSearchSpec spec;
    spec.start_jd_ut = start_jd_ut;
    spec.end_jd_ut = end_jd_ut;
    spec.base_target_rad = transit_base_target_rad(event_kind);
    spec.coarse_step_days = kDefaultCoarseStepDays;
    spec.root_tolerance_days = kDefaultRootToleranceDays;
    spec.residual_tolerance_rad = kDefaultResidualToleranceRad;
    spec.sample = sample_planet_hour_angle_ut;
    spec.user_data = &data;
    return visibility_search_continuous_angle_target_ut(spec, out, diagnostic);
}

}  // namespace runtime
}  // namespace taiyin
