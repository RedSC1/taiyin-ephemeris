#include "taiyin/runtime/star_visibility.h"

#include "runtime/visibility/visibility_angle_search_internal.h"
#include "runtime/visibility/visibility_math_internal.h"
#include "runtime/visibility/visibility_search_internal.h"

#include "runtime/core/native_context_checks.h"

#include "taiyin/angle.h"
#include "taiyin/dispatch.h"
#include "taiyin/earth_rotation.h"
#include "taiyin/runtime/observed_position.h"
#include "taiyin/runtime/star_position.h"
#include "taiyin/time.h"

#include <cmath>

namespace taiyin {
namespace runtime {
namespace {

constexpr double kDefaultCoarseStepDays = 1.0 / 24.0;
constexpr double kDefaultRootToleranceDays = 1.0e-9;
constexpr double kDefaultResidualToleranceRad = 1.0e-10;

bool valid_rise_set_event_kind(int event_kind) noexcept {
    return event_kind == TAIYIN_STAR_VISIBILITY_EVENT_RISE
        || event_kind == TAIYIN_STAR_VISIBILITY_EVENT_SET;
}

bool valid_transit_event_kind(int event_kind) noexcept {
    return event_kind == TAIYIN_STAR_VISIBILITY_EVENT_UPPER_TRANSIT
        || event_kind == TAIYIN_STAR_VISIBILITY_EVENT_LOWER_TRANSIT;
}

int crossing_direction_for_event(int event_kind) noexcept {
    return event_kind == TAIYIN_STAR_VISIBILITY_EVENT_RISE
        ? TAIYIN_VISIBILITY_CROSSING_RISING
        : TAIYIN_VISIBILITY_CROSSING_SETTING;
}

bool resolve_public_star_visibility_flags(
    uint64_t input_flags,
    uint64_t* out_internal_flags
) noexcept {
    if (!out_internal_flags) return false;
    if ((input_flags & TAIYIN_STAR_VISIBILITY_FLAG_REFRACTION) != 0u
        && (input_flags & TAIYIN_STAR_VISIBILITY_FLAG_NO_REFRACTION) != 0u) {
        return false;
    }
    const uint64_t known_flags =
        TAIYIN_STAR_VISIBILITY_FLAG_REFRACTION
        | TAIYIN_STAR_VISIBILITY_FLAG_NO_REFRACTION
        | TAIYIN_STAR_VISIBILITY_STRICT_METEOROLOGY;
    if ((input_flags & ~known_flags) != 0u) return false;

    uint64_t flags = input_flags
        & ~static_cast<uint64_t>(TAIYIN_STAR_VISIBILITY_FLAG_NO_REFRACTION);
    if ((input_flags & TAIYIN_STAR_VISIBILITY_FLAG_NO_REFRACTION) == 0u) {
        flags |= TAIYIN_STAR_VISIBILITY_FLAG_REFRACTION;
    }
    *out_internal_flags = flags;
    return true;
}

int public_altitude_state(int internal_state) noexcept {
    switch (internal_state) {
    case TAIYIN_VISIBILITY_ALTITUDE_STATE_CROSSES:
        return TAIYIN_STAR_VISIBILITY_ALTITUDE_STATE_CROSSES;
    case TAIYIN_VISIBILITY_ALTITUDE_STATE_ALWAYS_ABOVE:
        return TAIYIN_STAR_VISIBILITY_ALTITUDE_STATE_ALWAYS_ABOVE;
    case TAIYIN_VISIBILITY_ALTITUDE_STATE_ALWAYS_BELOW:
        return TAIYIN_STAR_VISIBILITY_ALTITUDE_STATE_ALWAYS_BELOW;
    case TAIYIN_VISIBILITY_ALTITUDE_STATE_TANGENT:
        return TAIYIN_STAR_VISIBILITY_ALTITUDE_STATE_TANGENT;
    default:
        return TAIYIN_STAR_VISIBILITY_ALTITUDE_STATE_NOT_FOUND;
    }
}

int public_crossing_direction(int internal_direction) noexcept {
    switch (internal_direction) {
    case TAIYIN_VISIBILITY_CROSSING_RISING:
        return TAIYIN_STAR_VISIBILITY_CROSSING_RISING;
    case TAIYIN_VISIBILITY_CROSSING_SETTING:
        return TAIYIN_STAR_VISIBILITY_CROSSING_SETTING;
    default:
        return TAIYIN_STAR_VISIBILITY_CROSSING_ANY;
    }
}

void copy_result(
    const VisibilityAltitudeSearchResult& src,
    StarVisibilityEventResult* dst
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

Status tt_from_ut(
    const NativeCalcContext* context,
    const SplitJulianDate& jd_ut,
    SplitJulianDate* out_jd_tt
) noexcept {
    if (!context || !out_jd_tt || !split_julian_date_is_finite(jd_ut)) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    const double delta_t = dispatch::eval_delta_t_with_ephemeris_correction(
        context->delta_t_model_id,
        context->ephemeris_family_id,
        jd_ut,
        0,
        0);
    if (!ut1_to_tt_split_jd(jd_ut, delta_t, out_jd_tt)) {
        return TAIYIN_ERROR_UNSUPPORTED;
    }
    return TAIYIN_STATUS_OK;
}

double hour_angle_rad(double sidereal_rad, double longitude_rad, double ra_rad) noexcept {
    return normalize_signed_radians(sidereal_rad + longitude_rad - ra_rad);
}

struct StarVisibilitySampleData {
    const NativeCalcContext* context;
    const char* star_key;
    double target_altitude_rad;
    uint64_t observed_flags;
};

Status sample_star_horizontal_ut(
    const StarVisibilitySampleData* data,
    const SplitJulianDate& jd_ut,
    double* out_altitude_rad,
    double* out_hour_angle_rad,
    EphemerisEvalDiagnostic* diagnostic
) noexcept {
    if (!data || !data->context || !data->star_key || !out_altitude_rad || !out_hour_angle_rad) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    *out_altitude_rad = NAN;
    *out_hour_angle_rad = NAN;

    ObservedPosition observed;
    const uint64_t flags =
        data->observed_flags | TAIYIN_OBSERVED_TOPOCENTRIC | TAIYIN_OBSERVED_HORIZONTAL;
    Status st = calc_observed_star_ut(
        data->context,
        data->star_key,
        jd_ut,
        flags,
        &observed,
        diagnostic);
    if (st != TAIYIN_STATUS_OK) return st;
    if (observed.status != TAIYIN_STATUS_OK) return observed.status;

    SplitJulianDate jd_tt;
    st = tt_from_ut(data->context, jd_ut, &jd_tt);
    if (st != TAIYIN_STATUS_OK) return st;
    double sidereal = 0.0;
    if (!gast_model_rad(
            data->context->model_context.precession_model_id,
            data->context->model_context.nutation_model_id,
            jd_ut,
            jd_tt,
            &sidereal)) {
        return TAIYIN_ERROR_UNSUPPORTED;
    }

    const bool use_refraction = (flags & TAIYIN_OBSERVED_REFRACTION) != 0u;
    const HorizontalCoordinates& horizontal = use_refraction
        ? observed.refracted_horizontal
        : observed.horizontal;
    const double hour_angle = hour_angle_rad(
        sidereal,
        data->context->observer_location.longitude_rad,
        observed.apparent.longitude_rad);

    if (!std::isfinite(horizontal.altitude_rad) || !std::isfinite(hour_angle)) {
        return TAIYIN_EPHEMERIS_ERROR_EVAL_FAILED;
    }
    *out_altitude_rad = horizontal.altitude_rad;
    *out_hour_angle_rad = hour_angle;
    return TAIYIN_STATUS_OK;
}

Status sample_star_altitude_residual_ut(
    const void* user_data,
    const SplitJulianDate& jd_ut,
    double* out_residual_rad,
    EphemerisEvalDiagnostic* diagnostic
) noexcept {
    if (!out_residual_rad) return TAIYIN_ERROR_INVALID_ARGUMENT;
    *out_residual_rad = NAN;
    const StarVisibilitySampleData* data = static_cast<const StarVisibilitySampleData*>(user_data);
    double altitude = 0.0;
    double hour_angle = 0.0;
    const Status st = sample_star_horizontal_ut(data, jd_ut, &altitude, &hour_angle, diagnostic);
    if (st != TAIYIN_STATUS_OK) return st;
    const double residual = altitude - data->target_altitude_rad;
    if (!std::isfinite(residual)) return TAIYIN_EPHEMERIS_ERROR_EVAL_FAILED;
    *out_residual_rad = residual;
    (void)hour_angle;
    return TAIYIN_STATUS_OK;
}

Status sample_star_hour_angle_ut(
    const void* user_data,
    const SplitJulianDate& jd_ut,
    double reference_hour_angle_rad,
    bool has_reference,
    double* out_hour_angle_rad,
    EphemerisEvalDiagnostic* diagnostic
) noexcept {
    if (!out_hour_angle_rad) return TAIYIN_ERROR_INVALID_ARGUMENT;
    *out_hour_angle_rad = NAN;
    const StarVisibilitySampleData* data = static_cast<const StarVisibilitySampleData*>(user_data);
    if (!data || !data->context || !data->star_key) return TAIYIN_ERROR_INVALID_ARGUMENT;

    NativeCalcContext equatorial_context = *data->context;
    equatorial_context.apparent_options.output_frame_id = TAIYIN_APPARENT_FRAME_TRUE_EQUATOR_OF_DATE;
    ObservedPosition observed;
    Status st = calc_observed_star_ut(
        &equatorial_context,
        data->star_key,
        jd_ut,
        data->observed_flags,
        &observed,
        diagnostic);
    if (st != TAIYIN_STATUS_OK) return st;
    if (observed.status != TAIYIN_STATUS_OK) return observed.status;
    if (!std::isfinite(observed.apparent.longitude_rad)) return TAIYIN_EPHEMERIS_ERROR_EVAL_FAILED;

    SplitJulianDate jd_tt;
    st = tt_from_ut(data->context, jd_ut, &jd_tt);
    if (st != TAIYIN_STATUS_OK) return st;
    double sidereal = 0.0;
    if (!gast_model_rad(
            data->context->model_context.precession_model_id,
            data->context->model_context.nutation_model_id,
            jd_ut,
            jd_tt,
            &sidereal)) {
        return TAIYIN_ERROR_UNSUPPORTED;
    }

    const double hour_angle = hour_angle_rad(
        sidereal,
        data->context->observer_location.longitude_rad,
        observed.apparent.longitude_rad);
    double continuous_hour_angle = hour_angle;
    if (has_reference) {
        continuous_hour_angle = reference_hour_angle_rad
            + normalize_signed_radians(hour_angle - reference_hour_angle_rad);
    }
    if (!std::isfinite(continuous_hour_angle)) return TAIYIN_EPHEMERIS_ERROR_EVAL_FAILED;
    *out_hour_angle_rad = continuous_hour_angle;
    return TAIYIN_STATUS_OK;
}

double transit_base_target_rad(int event_kind) noexcept {
    return event_kind == TAIYIN_STAR_VISIBILITY_EVENT_UPPER_TRANSIT ? 0.0 : TAIYIN_PI;
}

}  // namespace

StarVisibilityEventResult::StarVisibilityEventResult() noexcept
    : altitude_state(TAIYIN_STAR_VISIBILITY_ALTITUDE_STATE_NOT_FOUND),
      crossing_direction(TAIYIN_STAR_VISIBILITY_CROSSING_ANY),
      jd_ut(0, NAN),
      residual_rad(NAN),
      min_residual_rad(NAN),
      max_residual_rad(NAN),
      min_residual_jd_ut(0, NAN),
      max_residual_jd_ut(0, NAN),
      sample_count(0),
      refine_count(0) {}

Status search_star_rise_set_ut(
    const NativeCalcContext* context,
    const char* star_key,
    const SplitJulianDate& start_jd_ut,
    const SplitJulianDate& end_jd_ut,
    int event_kind,
    uint64_t star_visibility_flags,
    StarVisibilityEventResult* out,
    EphemerisEvalDiagnostic* diagnostic
) noexcept {
    return search_star_rise_set_at_horizon_ut(
        context,
        star_key,
        start_jd_ut,
        end_jd_ut,
        event_kind,
        0.0,
        star_visibility_flags,
        out,
        diagnostic);
}

Status search_star_rise_set_at_horizon_ut(
    const NativeCalcContext* context,
    const char* star_key,
    const SplitJulianDate& start_jd_ut,
    const SplitJulianDate& end_jd_ut,
    int event_kind,
    double horizon_altitude_rad,
    uint64_t star_visibility_flags,
    StarVisibilityEventResult* out,
    EphemerisEvalDiagnostic* diagnostic
) noexcept {
    if (out) *out = StarVisibilityEventResult();
    if (!context
        || !star_key
        || star_key[0] == '\0'
        || !out
        || !valid_rise_set_event_kind(event_kind)
        || !split_julian_date_is_finite(start_jd_ut)
        || !split_julian_date_is_finite(end_jd_ut)
        || !(end_jd_ut > start_jd_ut)
        || !std::isfinite(horizon_altitude_rad)) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    uint64_t internal_flags = 0u;
    if (!resolve_public_star_visibility_flags(star_visibility_flags, &internal_flags)) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }

    StarVisibilitySampleData data;
    data.context = context;
    data.star_key = star_key;
    data.target_altitude_rad = horizon_altitude_rad;
    data.observed_flags =
        (internal_flags & TAIYIN_STAR_VISIBILITY_FLAG_REFRACTION) != 0u
            ? TAIYIN_OBSERVED_REFRACTION
            : 0u;
    if ((internal_flags & TAIYIN_STAR_VISIBILITY_STRICT_METEOROLOGY) != 0u) {
        data.observed_flags |= TAIYIN_OBSERVED_STRICT_METEOROLOGY;
    }

    VisibilityAltitudeSearchSpec spec;
    spec.crossing_direction = crossing_direction_for_event(event_kind);
    spec.start_jd_ut = start_jd_ut;
    spec.end_jd_ut = end_jd_ut;
    spec.target_altitude_rad = horizon_altitude_rad;
    spec.coarse_step_days = kDefaultCoarseStepDays;
    spec.root_tolerance_days = kDefaultRootToleranceDays;
    spec.residual_tolerance_rad = kDefaultResidualToleranceRad;
    spec.residual_sampler = sample_star_altitude_residual_ut;
    spec.residual_sampler_data = &data;

    VisibilityAltitudeSearchResult internal_result;
    const Status st = visibility_search_altitude_interval_ut(context, spec, &internal_result, diagnostic);
    if (st != TAIYIN_STATUS_OK) return st;
    copy_result(internal_result, out);
    return TAIYIN_STATUS_OK;
}

Status search_star_transit_ut(
    const NativeCalcContext* context,
    const char* star_key,
    const SplitJulianDate& start_jd_ut,
    const SplitJulianDate& end_jd_ut,
    int event_kind,
    StarVisibilityEventResult* out,
    EphemerisEvalDiagnostic* diagnostic
) noexcept {
    if (out) *out = StarVisibilityEventResult();
    if (!context
        || !star_key
        || star_key[0] == '\0'
        || !out
        || !valid_transit_event_kind(event_kind)
        || !split_julian_date_is_finite(start_jd_ut)
        || !split_julian_date_is_finite(end_jd_ut)
        || !(end_jd_ut > start_jd_ut)
        || !context->fields.has(TAIYIN_NATIVE_FIELD_OBSERVER_LOCATION)
        || !native_observer_location_is_finite(context->observer_location)) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }

    StarVisibilitySampleData data;
    data.context = context;
    data.star_key = star_key;
    data.target_altitude_rad = 0.0;
    data.observed_flags = 0u;

    VisibilityAngleTargetSearchSpec spec;
    spec.start_jd_ut = start_jd_ut;
    spec.end_jd_ut = end_jd_ut;
    spec.base_target_rad = transit_base_target_rad(event_kind);
    spec.coarse_step_days = kDefaultCoarseStepDays;
    spec.root_tolerance_days = kDefaultRootToleranceDays;
    spec.residual_tolerance_rad = kDefaultResidualToleranceRad;
    spec.sample = sample_star_hour_angle_ut;
    spec.user_data = &data;

    VisibilityAltitudeSearchResult internal_result;
    const Status st = visibility_search_continuous_angle_target_ut(spec, &internal_result, diagnostic);
    if (st != TAIYIN_STATUS_OK) return st;
    copy_result(internal_result, out);
    return TAIYIN_STATUS_OK;
}

}  // namespace runtime
}  // namespace taiyin
