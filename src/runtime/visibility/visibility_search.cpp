#include "runtime/visibility/visibility_search_internal.h"

#include "runtime/visibility/visibility_math_internal.h"
#include "runtime/visibility/visibility_sampling_internal.h"

#include "taiyin/runtime/observed_position.h"

#include <cmath>
#include <limits>

namespace taiyin {
namespace runtime {
namespace {

constexpr double kDefaultCoarseStepDays = 2.0 / 24.0;
constexpr double kFallbackFineStepDays = 0.01;
constexpr double kDefaultRootToleranceDays = 1.0e-10;
constexpr double kDefaultResidualToleranceRad = 1.0e-6;

double nan_value() noexcept {
    return std::numeric_limits<double>::quiet_NaN();
}

SplitJulianDate invalid_jd() noexcept {
    return SplitJulianDate(0, nan_value());
}

void clear_result(VisibilityAltitudeSearchResult* out) noexcept {
    if (!out) return;
    *out = VisibilityAltitudeSearchResult();
}

int crossing_direction_from_residuals(double y0, double y1) noexcept {
    if (y1 > y0) return TAIYIN_VISIBILITY_CROSSING_RISING;
    if (y1 < y0) return TAIYIN_VISIBILITY_CROSSING_SETTING;
    return TAIYIN_VISIBILITY_CROSSING_ANY;
}

bool crossing_direction_matches(int actual, int requested) noexcept {
    return requested == TAIYIN_VISIBILITY_CROSSING_ANY
        || actual == TAIYIN_VISIBILITY_CROSSING_ANY
        || actual == requested;
}

bool valid_search_spec(const VisibilityAltitudeSearchSpec& spec) noexcept {
    const bool has_custom_sampler = spec.residual_sampler != 0;
    return (has_custom_sampler
            || (spec.body_id >= 0
        && (spec.residual_mode == TAIYIN_VISIBILITY_RESIDUAL_CENTER_ALTITUDE
            || spec.residual_mode == TAIYIN_VISIBILITY_RESIDUAL_TRUE_UPPER_LIMB
            || spec.residual_mode == TAIYIN_VISIBILITY_RESIDUAL_APPARENT_UPPER_LIMB
            || spec.residual_mode == TAIYIN_VISIBILITY_RESIDUAL_TRUE_LOWER_LIMB
            || spec.residual_mode == TAIYIN_VISIBILITY_RESIDUAL_APPARENT_LOWER_LIMB)
        && (spec.residual_mode == TAIYIN_VISIBILITY_RESIDUAL_CENTER_ALTITUDE
            || spec.physical_radius_km >= 0.0)))
        && (spec.crossing_direction == TAIYIN_VISIBILITY_CROSSING_ANY
            || spec.crossing_direction == TAIYIN_VISIBILITY_CROSSING_RISING
            || spec.crossing_direction == TAIYIN_VISIBILITY_CROSSING_SETTING)
        && split_julian_date_is_finite(spec.start_jd_ut)
        && split_julian_date_is_finite(spec.end_jd_ut)
        && spec.end_jd_ut > spec.start_jd_ut
        && std::isfinite(spec.target_altitude_rad);
}

double radius_distance_au(const VisibilityAltitudeSearchSpec& spec, double sampled_distance_au) noexcept {
    return spec.angular_radius_distance_au > 0.0 ? spec.angular_radius_distance_au : sampled_distance_au;
}

void update_range(
    VisibilityAltitudeSearchResult* out,
    const SplitJulianDate& jd_ut,
    double residual_rad
) noexcept {
    if (!out || !std::isfinite(residual_rad)) return;
    if (out->sample_count + out->refine_count == 1 || residual_rad < out->min_residual_rad) {
        out->min_residual_rad = residual_rad;
        out->min_residual_jd_ut = jd_ut;
    }
    if (out->sample_count + out->refine_count == 1 || residual_rad > out->max_residual_rad) {
        out->max_residual_rad = residual_rad;
        out->max_residual_jd_ut = jd_ut;
    }
}

Status sample_residual(
    const NativeCalcContext* context,
    const VisibilityAltitudeSearchSpec& spec,
    const SplitJulianDate& jd_ut,
    double* out_residual_rad,
    VisibilityAltitudeSearchResult* result,
    bool refine_sample,
    EphemerisEvalDiagnostic* diagnostic
) noexcept {
    if (!out_residual_rad) return TAIYIN_ERROR_INVALID_ARGUMENT;
    *out_residual_rad = nan_value();

    if (spec.residual_sampler) {
        const Status st = spec.residual_sampler(
            spec.residual_sampler_data,
            jd_ut,
            out_residual_rad,
            diagnostic);
        if (st != TAIYIN_STATUS_OK) return st;
        if (!std::isfinite(*out_residual_rad)) return TAIYIN_EPHEMERIS_ERROR_EVAL_FAILED;
        if (result) {
            if (refine_sample) ++result->refine_count;
            else ++result->sample_count;
            update_range(result, jd_ut, *out_residual_rad);
        }
        return TAIYIN_STATUS_OK;
    }

    uint64_t flags = spec.observed_flags;
    if (spec.residual_mode == TAIYIN_VISIBILITY_RESIDUAL_TRUE_UPPER_LIMB
        || spec.residual_mode == TAIYIN_VISIBILITY_RESIDUAL_APPARENT_UPPER_LIMB
        || spec.residual_mode == TAIYIN_VISIBILITY_RESIDUAL_TRUE_LOWER_LIMB
        || spec.residual_mode == TAIYIN_VISIBILITY_RESIDUAL_APPARENT_LOWER_LIMB) {
        flags &= ~TAIYIN_OBSERVED_REFRACTION;
    }

    double altitude = 0.0;
    double azimuth = 0.0;
    double hour_angle = 0.0;
    double distance = 0.0;
    const Status st = visibility_sample_body_center_horizontal_ut(
        context,
        spec.body_id,
        jd_ut,
        flags,
        &altitude,
        &azimuth,
        &hour_angle,
        &distance,
        0,
        0,
        diagnostic);
    if (st != TAIYIN_STATUS_OK) return st;

    double event_altitude = altitude;
    if (spec.residual_mode == TAIYIN_VISIBILITY_RESIDUAL_TRUE_UPPER_LIMB
        || spec.residual_mode == TAIYIN_VISIBILITY_RESIDUAL_APPARENT_UPPER_LIMB
        || spec.residual_mode == TAIYIN_VISIBILITY_RESIDUAL_TRUE_LOWER_LIMB
        || spec.residual_mode == TAIYIN_VISIBILITY_RESIDUAL_APPARENT_LOWER_LIMB) {
        const double radius = visibility_angular_radius_rad(
            spec.physical_radius_km,
            radius_distance_au(spec, distance));
        if (!std::isfinite(radius)) return TAIYIN_EPHEMERIS_ERROR_EVAL_FAILED;
        const bool lower_limb = spec.residual_mode == TAIYIN_VISIBILITY_RESIDUAL_TRUE_LOWER_LIMB
            || spec.residual_mode == TAIYIN_VISIBILITY_RESIDUAL_APPARENT_LOWER_LIMB;
        event_altitude = lower_limb ? altitude - radius : altitude + radius;
        if (spec.residual_mode == TAIYIN_VISIBILITY_RESIDUAL_APPARENT_UPPER_LIMB
            || spec.residual_mode == TAIYIN_VISIBILITY_RESIDUAL_APPARENT_LOWER_LIMB) {
            Status refr_st = visibility_apply_refraction_from_context(
                context, event_altitude, spec.observed_flags, &event_altitude);
            if (refr_st != TAIYIN_STATUS_OK) return refr_st;
        }
    }

    const double residual = event_altitude - spec.target_altitude_rad;
    if (!std::isfinite(residual)) return TAIYIN_EPHEMERIS_ERROR_EVAL_FAILED;
    *out_residual_rad = residual;
    if (result) {
        if (refine_sample) ++result->refine_count;
        else ++result->sample_count;
        update_range(result, jd_ut, residual);
    }
    return TAIYIN_STATUS_OK;
}

bool bracket_crosses(
    double y0,
    double y1,
    double tolerance,
    int requested_direction,
    int* out_direction
) noexcept {
    if (!visibility_has_crossing(y0, y1, tolerance)) return false;
    const int direction = crossing_direction_from_residuals(y0, y1);
    if (!crossing_direction_matches(direction, requested_direction)) return false;
    if (out_direction) *out_direction = direction;
    return true;
}

bool residual_near_zero(double value, double tolerance) noexcept {
    return std::isfinite(value) && std::fabs(value) <= tolerance;
}

bool same_nonzero_sign(double y0, double y1, double tolerance) noexcept {
    const int s0 = visibility_sign(y0, tolerance);
    const int s1 = visibility_sign(y1, tolerance);
    return (s0 == TAIYIN_VISIBILITY_SIGN_POSITIVE && s1 == TAIYIN_VISIBILITY_SIGN_POSITIVE)
        || (s0 == TAIYIN_VISIBILITY_SIGN_NEGATIVE && s1 == TAIYIN_VISIBILITY_SIGN_NEGATIVE);
}

Status refine_bisection(
    const NativeCalcContext* context,
    const VisibilityAltitudeSearchSpec& spec,
    const SplitJulianDate& t0,
    double y0,
    const SplitJulianDate& t1,
    double y1,
    double tolerance_days,
    VisibilityAltitudeSearchResult* out,
    EphemerisEvalDiagnostic* diagnostic
) noexcept {
    if (!out) return TAIYIN_ERROR_INVALID_ARGUMENT;
    SplitJulianDate a = t0;
    SplitJulianDate b = t1;
    double fa = y0;
    double fb = y1;

    if (std::fabs(fa) <= spec.residual_tolerance_rad) {
        out->jd_ut = a;
        out->residual_rad = fa;
        return TAIYIN_STATUS_OK;
    }
    if (std::fabs(fb) <= spec.residual_tolerance_rad) {
        out->jd_ut = b;
        out->residual_rad = fb;
        return TAIYIN_STATUS_OK;
    }

    for (int i = 0; i < 80; ++i) {
        const SplitJulianDate mid = a + 0.5 * (b - a);
        double fm = 0.0;
        const Status st = sample_residual(context, spec, mid, &fm, out, true, diagnostic);
        if (st != TAIYIN_STATUS_OK) return st;
        if (std::fabs(fm) <= spec.residual_tolerance_rad || std::fabs(b - a) <= tolerance_days) {
            out->jd_ut = mid;
            out->residual_rad = fm;
            return TAIYIN_STATUS_OK;
        }
        if (visibility_has_crossing(fa, fm, 0.0)) {
            b = mid;
            fb = fm;
        } else {
            a = mid;
            fa = fm;
        }
    }

    const SplitJulianDate mid = a + 0.5 * (b - a);
    double fm = 0.0;
    const Status st = sample_residual(context, spec, mid, &fm, out, true, diagnostic);
    if (st != TAIYIN_STATUS_OK) return st;
    out->jd_ut = mid;
    out->residual_rad = fm;
    (void)fb;
    return TAIYIN_STATUS_OK;
}

Status try_tangent_vertex(
    const NativeCalcContext* context,
    const VisibilityAltitudeSearchSpec& spec,
    const SplitJulianDate& tm,
    double ym,
    const SplitJulianDate& t0,
    double y0,
    const SplitJulianDate& tp,
    double yp,
    double residual_tolerance,
    VisibilityAltitudeSearchResult* out,
    bool* out_found,
    EphemerisEvalDiagnostic* diagnostic
) noexcept {
    if (!out || !out_found) return TAIYIN_ERROR_INVALID_ARGUMENT;
    *out_found = false;
    if (!same_nonzero_sign(ym, yp, residual_tolerance)) return TAIYIN_STATUS_OK;

    if (residual_near_zero(y0, residual_tolerance)) {
        out->altitude_state = TAIYIN_VISIBILITY_ALTITUDE_STATE_TANGENT;
        out->crossing_direction = TAIYIN_VISIBILITY_CROSSING_ANY;
        out->jd_ut = t0;
        out->residual_rad = y0;
        *out_found = true;
        return TAIYIN_STATUS_OK;
    }

    const double t0_offset = t0 - tm;
    const double tp_offset = tp - tm;
    const double vertex_offset = visibility_quadratic_vertex_time(
        0.0, ym, t0_offset, y0, tp_offset, yp);
    if (!std::isfinite(vertex_offset)
        || !(vertex_offset > 0.0)
        || !(vertex_offset < tp_offset)) {
        return TAIYIN_STATUS_OK;
    }
    const SplitJulianDate vertex_t = tm + vertex_offset;

    double vertex_y = 0.0;
    const Status st = sample_residual(context, spec, vertex_t, &vertex_y, out, true, diagnostic);
    if (st != TAIYIN_STATUS_OK) return st;
    if (!residual_near_zero(vertex_y, residual_tolerance)) return TAIYIN_STATUS_OK;

    out->altitude_state = TAIYIN_VISIBILITY_ALTITUDE_STATE_TANGENT;
    out->crossing_direction = TAIYIN_VISIBILITY_CROSSING_ANY;
    out->jd_ut = vertex_t;
    out->residual_rad = vertex_y;
    *out_found = true;
    return TAIYIN_STATUS_OK;
}

}  // namespace

VisibilityAltitudeSearchSpec::VisibilityAltitudeSearchSpec() noexcept
    : body_id(-1),
      residual_mode(TAIYIN_VISIBILITY_RESIDUAL_CENTER_ALTITUDE),
      crossing_direction(TAIYIN_VISIBILITY_CROSSING_ANY),
      start_jd_ut(invalid_jd()),
      end_jd_ut(invalid_jd()),
      target_altitude_rad(0.0),
      physical_radius_km(0.0),
      angular_radius_distance_au(nan_value()),
      coarse_step_days(kDefaultCoarseStepDays),
      root_tolerance_days(kDefaultRootToleranceDays),
      residual_tolerance_rad(kDefaultResidualToleranceRad),
      observed_flags(0u),
      residual_sampler(0),
      residual_sampler_data(0) {}

VisibilityAltitudeSearchResult::VisibilityAltitudeSearchResult() noexcept
    : altitude_state(TAIYIN_VISIBILITY_ALTITUDE_STATE_NOT_FOUND),
      crossing_direction(TAIYIN_VISIBILITY_CROSSING_ANY),
      jd_ut(invalid_jd()),
      residual_rad(nan_value()),
      min_residual_rad(nan_value()),
      max_residual_rad(nan_value()),
      min_residual_jd_ut(invalid_jd()),
      max_residual_jd_ut(invalid_jd()),
      sample_count(0),
      refine_count(0) {}

Status visibility_search_altitude_interval_ut(
    const NativeCalcContext* context,
    const VisibilityAltitudeSearchSpec& spec,
    VisibilityAltitudeSearchResult* out,
    EphemerisEvalDiagnostic* diagnostic
) noexcept {
    clear_result(out);
    if (!out || !valid_search_spec(spec) || (!context && !spec.residual_sampler)) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    const double step_days = spec.coarse_step_days > 0.0 ? spec.coarse_step_days : kDefaultCoarseStepDays;
    const double tolerance_days = spec.root_tolerance_days > 0.0 ? spec.root_tolerance_days : kDefaultRootToleranceDays;
    const double residual_tolerance = spec.residual_tolerance_rad >= 0.0
        ? spec.residual_tolerance_rad
        : kDefaultResidualToleranceRad;

    SplitJulianDate prev_t = spec.start_jd_ut;
    double prev_y = 0.0;
    Status st = sample_residual(context, spec, prev_t, &prev_y, out, false, diagnostic);
    if (st != TAIYIN_STATUS_OK) return st;

    bool have_prev_prev = false;
    SplitJulianDate prev_prev_t = invalid_jd();
    double prev_prev_y = nan_value();

    while (prev_t < spec.end_jd_ut) {
        SplitJulianDate cur_t = prev_t + step_days;
        if (cur_t > spec.end_jd_ut) cur_t = spec.end_jd_ut;
        if (!(cur_t > prev_t)) break;
        double cur_y = 0.0;
        st = sample_residual(context, spec, cur_t, &cur_y, out, false, diagnostic);
        if (st != TAIYIN_STATUS_OK) return st;

        if (have_prev_prev) {
            bool tangent_found = false;
            st = try_tangent_vertex(
                context,
                spec,
                prev_prev_t,
                prev_prev_y,
                prev_t,
                prev_y,
                cur_t,
                cur_y,
                residual_tolerance,
                out,
                &tangent_found,
                diagnostic);
            if (st != TAIYIN_STATUS_OK) return st;
            if (tangent_found) return TAIYIN_STATUS_OK;
        }

        int direction = TAIYIN_VISIBILITY_CROSSING_ANY;
        const bool defer_current_zero = residual_near_zero(cur_y, residual_tolerance) && cur_t < spec.end_jd_ut;
        if (!defer_current_zero
            && bracket_crosses(prev_y, cur_y, residual_tolerance, spec.crossing_direction, &direction)) {
            out->altitude_state = TAIYIN_VISIBILITY_ALTITUDE_STATE_CROSSES;
            out->crossing_direction = direction;
            st = refine_bisection(context, spec, prev_t, prev_y, cur_t, cur_y, tolerance_days, out, diagnostic);
            if (st != TAIYIN_STATUS_OK) return st;
            return TAIYIN_STATUS_OK;
        }

        if (cur_t >= spec.end_jd_ut) break;
        prev_prev_t = prev_t;
        prev_prev_y = prev_y;
        have_prev_prev = true;
        prev_t = cur_t;
        prev_y = cur_y;
    }

    out->altitude_state = visibility_classify_altitude_range(
        out->min_residual_rad,
        out->max_residual_rad,
        residual_tolerance);
    // A coarse bracket found above returns immediately. Here CROSSES only means
    // the sampled residual range straddles the target, so run a finer scan.
    if (out->altitude_state == TAIYIN_VISIBILITY_ALTITUDE_STATE_CROSSES
        && step_days > kFallbackFineStepDays * 1.01) {
        prev_t = spec.start_jd_ut;
        st = sample_residual(context, spec, prev_t, &prev_y, out, false, diagnostic);
        if (st != TAIYIN_STATUS_OK) return st;
        while (prev_t < spec.end_jd_ut) {
            SplitJulianDate cur_t = prev_t + kFallbackFineStepDays;
            if (cur_t > spec.end_jd_ut) cur_t = spec.end_jd_ut;
            if (!(cur_t > prev_t)) break;
            double cur_y = 0.0;
            st = sample_residual(context, spec, cur_t, &cur_y, out, false, diagnostic);
            if (st != TAIYIN_STATUS_OK) return st;

            int direction = TAIYIN_VISIBILITY_CROSSING_ANY;
            const bool defer_current_zero = residual_near_zero(cur_y, residual_tolerance) && cur_t < spec.end_jd_ut;
            if (!defer_current_zero
                && bracket_crosses(prev_y, cur_y, residual_tolerance, spec.crossing_direction, &direction)) {
                out->altitude_state = TAIYIN_VISIBILITY_ALTITUDE_STATE_CROSSES;
                out->crossing_direction = direction;
                st = refine_bisection(context, spec, prev_t, prev_y, cur_t, cur_y, tolerance_days, out, diagnostic);
                if (st != TAIYIN_STATUS_OK) return st;
                return TAIYIN_STATUS_OK;
            }

            if (cur_t >= spec.end_jd_ut) break;
            prev_t = cur_t;
            prev_y = cur_y;
        }
    }
    return TAIYIN_STATUS_OK;
}

}  // namespace runtime
}  // namespace taiyin
