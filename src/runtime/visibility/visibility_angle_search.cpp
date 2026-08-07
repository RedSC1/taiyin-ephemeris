#include "runtime/visibility/visibility_angle_search_internal.h"

#include "runtime/visibility/visibility_math_internal.h"

#include "taiyin/angle.h"

#include <cmath>
#include <limits>

namespace taiyin {
namespace runtime {
namespace {

constexpr double kDefaultCoarseStepDays = 2.0 / 24.0;
constexpr double kDefaultRootToleranceDays = 1.0e-10;
constexpr double kDefaultResidualToleranceRad = 1.0e-10;

SplitJulianDate invalid_jd() noexcept {
    return SplitJulianDate(0, std::numeric_limits<double>::quiet_NaN());
}

bool valid_spec(const VisibilityAngleTargetSearchSpec& spec) noexcept {
    return split_julian_date_is_finite(spec.start_jd_ut)
        && split_julian_date_is_finite(spec.end_jd_ut)
        && spec.end_jd_ut > spec.start_jd_ut
        && std::isfinite(spec.base_target_rad)
        && spec.sample != 0;
}

void record_sample(
    VisibilityAltitudeSearchResult* out,
    const SplitJulianDate& jd_ut,
    double residual_rad,
    bool refine_sample
) noexcept {
    if (!out || !std::isfinite(residual_rad)) return;
    if (refine_sample) ++out->refine_count;
    else ++out->sample_count;
    if (out->sample_count + out->refine_count == 1 || residual_rad < out->min_residual_rad) {
        out->min_residual_rad = residual_rad;
        out->min_residual_jd_ut = jd_ut;
    }
    if (out->sample_count + out->refine_count == 1 || residual_rad > out->max_residual_rad) {
        out->max_residual_rad = residual_rad;
        out->max_residual_jd_ut = jd_ut;
    }
}

bool target_in_segment(
    double y0,
    double y1,
    double base_target_rad,
    double* out_target_rad
) noexcept {
    if (!std::isfinite(y0) || !std::isfinite(y1) || !out_target_rad) return false;
    const double lo = y0 < y1 ? y0 : y1;
    const double hi = y0 < y1 ? y1 : y0;
    const double k = std::ceil((lo - base_target_rad) / TAIYIN_TWO_PI);
    const double target = base_target_rad + k * TAIYIN_TWO_PI;
    if (target < lo || target > hi) return false;
    *out_target_rad = target;
    return true;
}

bool bracket_crosses(double y0, double y1) noexcept {
    if (!std::isfinite(y0) || !std::isfinite(y1)) return false;
    if (y0 == 0.0 || y1 == 0.0) return true;
    return y0 < 0.0 ? y1 > 0.0 : y1 < 0.0;
}

Status refine_bisection(
    const VisibilityAngleTargetSearchSpec& spec,
    const SplitJulianDate& t0,
    double y0,
    const SplitJulianDate& t1,
    double y1,
    double target_rad,
    double root_tolerance_days,
    double residual_tolerance_rad,
    VisibilityAltitudeSearchResult* out,
    EphemerisEvalDiagnostic* diagnostic
) noexcept {
    SplitJulianDate a = t0;
    SplitJulianDate b = t1;
    double ha = y0;
    double hb = y1;
    double fa = ha - target_rad;
    double fb = hb - target_rad;
    for (int i = 0; i < 80; ++i) {
        const SplitJulianDate mid = a + 0.5 * (b - a);
        const double reference = 0.5 * (ha + hb);
        double hm = 0.0;
        const Status st = spec.sample(spec.user_data, mid, reference, true, &hm, diagnostic);
        if (st != TAIYIN_STATUS_OK) return st;
        if (!std::isfinite(hm)) return TAIYIN_EPHEMERIS_ERROR_EVAL_FAILED;
        const double fm = hm - target_rad;
        record_sample(out, mid, fm, true);
        if (std::fabs(fm) <= residual_tolerance_rad || std::fabs(b - a) <= root_tolerance_days) {
            out->jd_ut = mid;
            out->residual_rad = normalize_signed_radians(fm);
            return TAIYIN_STATUS_OK;
        }
        if (bracket_crosses(fa, fm)) {
            b = mid;
            hb = hm;
            fb = fm;
        } else {
            a = mid;
            ha = hm;
            fa = fm;
        }
    }

    const SplitJulianDate mid = a + 0.5 * (b - a);
    double hm = 0.0;
    const Status st = spec.sample(spec.user_data, mid, 0.5 * (ha + hb), true, &hm, diagnostic);
    if (st != TAIYIN_STATUS_OK) return st;
    if (!std::isfinite(hm)) return TAIYIN_EPHEMERIS_ERROR_EVAL_FAILED;
    const double fm = hm - target_rad;
    record_sample(out, mid, fm, true);
    out->jd_ut = mid;
    out->residual_rad = normalize_signed_radians(fm);
    (void)fb;
    return TAIYIN_STATUS_OK;
}

}  // namespace

VisibilityAngleTargetSearchSpec::VisibilityAngleTargetSearchSpec() noexcept
    : start_jd_ut(invalid_jd()),
      end_jd_ut(invalid_jd()),
      base_target_rad(0.0),
      coarse_step_days(kDefaultCoarseStepDays),
      root_tolerance_days(kDefaultRootToleranceDays),
      residual_tolerance_rad(kDefaultResidualToleranceRad),
      sample(0),
      user_data(0) {}

Status visibility_search_continuous_angle_target_ut(
    const VisibilityAngleTargetSearchSpec& spec,
    VisibilityAltitudeSearchResult* out,
    EphemerisEvalDiagnostic* diagnostic
) noexcept {
    if (!out || !valid_spec(spec)) return TAIYIN_ERROR_INVALID_ARGUMENT;
    *out = VisibilityAltitudeSearchResult();
    const double coarse_step_days = spec.coarse_step_days > 0.0 ? spec.coarse_step_days : kDefaultCoarseStepDays;
    const double root_tolerance_days = spec.root_tolerance_days > 0.0
        ? spec.root_tolerance_days
        : kDefaultRootToleranceDays;
    const double residual_tolerance_rad = spec.residual_tolerance_rad >= 0.0
        ? spec.residual_tolerance_rad
        : kDefaultResidualToleranceRad;

    SplitJulianDate prev_t = spec.start_jd_ut;
    double prev_angle = 0.0;
    Status st = spec.sample(spec.user_data, prev_t, 0.0, false, &prev_angle, diagnostic);
    if (st != TAIYIN_STATUS_OK) return st;
    if (!std::isfinite(prev_angle)) return TAIYIN_EPHEMERIS_ERROR_EVAL_FAILED;
    record_sample(out, prev_t, normalize_signed_radians(prev_angle - spec.base_target_rad), false);

    while (prev_t < spec.end_jd_ut) {
        const SplitJulianDate next_t = prev_t + coarse_step_days;
        const SplitJulianDate cur_t = next_t < spec.end_jd_ut ? next_t : spec.end_jd_ut;
        if (!(cur_t > prev_t)) break;
        double cur_angle = 0.0;
        st = spec.sample(spec.user_data, cur_t, prev_angle, true, &cur_angle, diagnostic);
        if (st != TAIYIN_STATUS_OK) return st;
        if (!std::isfinite(cur_angle)) return TAIYIN_EPHEMERIS_ERROR_EVAL_FAILED;
        record_sample(out, cur_t, normalize_signed_radians(cur_angle - spec.base_target_rad), false);

        double target = 0.0;
        if (target_in_segment(prev_angle, cur_angle, spec.base_target_rad, &target)) {
            out->altitude_state = TAIYIN_VISIBILITY_ALTITUDE_STATE_CROSSES;
            out->crossing_direction = cur_angle > prev_angle
                ? TAIYIN_VISIBILITY_CROSSING_RISING
                : TAIYIN_VISIBILITY_CROSSING_SETTING;
            return refine_bisection(
                spec,
                prev_t,
                prev_angle,
                cur_t,
                cur_angle,
                target,
                root_tolerance_days,
                residual_tolerance_rad,
                out,
                diagnostic);
        }
        prev_t = cur_t;
        prev_angle = cur_angle;
    }

    out->altitude_state = TAIYIN_VISIBILITY_ALTITUDE_STATE_NOT_FOUND;
    return TAIYIN_STATUS_OK;
}

}  // namespace runtime
}  // namespace taiyin
