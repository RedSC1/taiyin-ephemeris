#include "taiyin/runtime/heliacal_visibility.h"

#include "runtime/visibility/heliacal_visibility_internal.h"
#include "taiyin/angle.h"
#include "runtime/core/native_context_checks.h"
#include "taiyin/runtime/native_position.h"
#include "taiyin/runtime/solar_visibility.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace taiyin {
namespace runtime {
namespace {

const double kAstronomicalTwilightAltitudeRad = -18.0 * TAIYIN_DEG_TO_RAD;
const double kHeliacalWindowUpperSunAltitudeRad = -0.85 * TAIYIN_DEG_TO_RAD;
constexpr double kGoldenRatioConjugate = 0.3819660112501051;
constexpr double kWindowToleranceDays = 1.0 / 86400.0;
constexpr int kMaxWindowOptimizeIterations = 32;
// Astronomical-twilight windows are normally at most a few hours. This keeps
// the prefilter below roughly a ten-minute cadence without turning a yearly
// search into an hourly brute-force scan.
constexpr int kCoarseWindowSampleCount = 13;
constexpr double kCoarseRefineMarginMagnitude = 2.5;
constexpr double kMaximumSearchDays = 10000.0;

enum HeliacalTargetKind {
    HELIACAL_TARGET_BODY,
    HELIACAL_TARGET_STAR,
};

struct HeliacalTarget {
    HeliacalTargetKind kind;
    int body_id;
    const char* star_key;
};

struct DailyVisibilityWindow {
    bool valid;
    SplitJulianDate start_jd_ut;
    SplitJulianDate end_jd_ut;
    SplitJulianDate best_jd_ut;
    int evaluation_count;
    HeliacalVisibilityResult visibility;

    DailyVisibilityWindow() noexcept
        : valid(false),
          start_jd_ut(0, std::numeric_limits<double>::quiet_NaN()),
          end_jd_ut(0, std::numeric_limits<double>::quiet_NaN()),
          best_jd_ut(0, std::numeric_limits<double>::quiet_NaN()),
          evaluation_count(0),
          visibility() {}
};

bool valid_event_kind(int event_kind) noexcept {
    return event_kind == TAIYIN_HELIACAL_VISIBILITY_EVENT_MORNING_FIRST
        || event_kind == TAIYIN_HELIACAL_VISIBILITY_EVENT_MORNING_LAST
        || event_kind == TAIYIN_HELIACAL_VISIBILITY_EVENT_EVENING_FIRST
        || event_kind == TAIYIN_HELIACAL_VISIBILITY_EVENT_EVENING_LAST;
}

bool morning_event_kind(int event_kind) noexcept {
    return event_kind == TAIYIN_HELIACAL_VISIBILITY_EVENT_MORNING_FIRST
        || event_kind == TAIYIN_HELIACAL_VISIBILITY_EVENT_MORNING_LAST;
}

bool first_event_kind(int event_kind) noexcept {
    return event_kind == TAIYIN_HELIACAL_VISIBILITY_EVENT_MORNING_FIRST
        || event_kind == TAIYIN_HELIACAL_VISIBILITY_EVENT_EVENING_FIRST;
}

Status eval_target_visibility(
    const NativeCalcContext* context,
    const HeliacalTarget& target,
    const SplitJulianDate& jd_ut,
    uint64_t flags,
    const HeliacalVisibilityConditions* conditions,
    HeliacalVisibilityResult* out,
    EphemerisEvalDiagnostic* diagnostic
) noexcept {
    if (target.kind == HELIACAL_TARGET_BODY) {
        return calc_body_heliacal_visibility_ut(
            context, target.body_id, jd_ut, flags, conditions, out, diagnostic);
    }
    return calc_star_heliacal_visibility_ut(
        context, target.star_key, jd_ut, flags, conditions, out, diagnostic);
}

Status find_daily_twilight_window(
    const NativeCalcContext* context,
    const SplitJulianDate& day_start_jd_ut,
    bool morning,
    SplitJulianDate* out_start_jd_ut,
    SplitJulianDate* out_end_jd_ut,
    EphemerisEvalDiagnostic* diagnostic
) noexcept {
    if (!context || !out_start_jd_ut || !out_end_jd_ut
        || !split_julian_date_is_finite(day_start_jd_ut)) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    *out_start_jd_ut = SplitJulianDate(0, std::numeric_limits<double>::quiet_NaN());
    *out_end_jd_ut = SplitJulianDate(0, std::numeric_limits<double>::quiet_NaN());
    const int crossing_kind = morning
        ? TAIYIN_SOLAR_VISIBILITY_EVENT_RISE : TAIYIN_SOLAR_VISIBILITY_EVENT_SET;
    SolarVisibilityEventResult upper;
    Status status = search_solar_rise_set_at_horizon_ut(
        context,
        day_start_jd_ut,
        day_start_jd_ut + 1.0,
        crossing_kind,
        TAIYIN_SOLAR_VISIBILITY_LIMB_CENTER,
        kHeliacalWindowUpperSunAltitudeRad,
        TAIYIN_SOLAR_VISIBILITY_FLAG_NO_REFRACTION,
        &upper,
        diagnostic);
    if (status != TAIYIN_STATUS_OK) return status;
    if (!split_julian_date_is_finite(upper.jd_ut)) return TAIYIN_EVENT_ERROR_NOT_FOUND;
    const SplitJulianDate astronomical_start_jd_ut = morning ? upper.jd_ut - 1.0 : upper.jd_ut;
    const SplitJulianDate astronomical_end_jd_ut = morning ? upper.jd_ut : upper.jd_ut + 1.0;
    SolarVisibilityEventResult astronomical;
    status = search_solar_rise_set_at_horizon_ut(
        context,
        astronomical_start_jd_ut,
        astronomical_end_jd_ut,
        crossing_kind,
        TAIYIN_SOLAR_VISIBILITY_LIMB_CENTER,
        kAstronomicalTwilightAltitudeRad,
        TAIYIN_SOLAR_VISIBILITY_FLAG_NO_REFRACTION,
        &astronomical,
        diagnostic);
    if (status != TAIYIN_STATUS_OK) return status;
    if (!split_julian_date_is_finite(astronomical.jd_ut)) {
        return TAIYIN_EVENT_ERROR_NOT_FOUND;
    }
    *out_start_jd_ut = morning ? astronomical.jd_ut : upper.jd_ut;
    *out_end_jd_ut = morning ? upper.jd_ut : astronomical.jd_ut;
    return *out_end_jd_ut > *out_start_jd_ut
        ? TAIYIN_STATUS_OK : TAIYIN_EVENT_ERROR_NOT_FOUND;
}

Status maximize_daily_visibility_window(
    const NativeCalcContext* context,
    const HeliacalTarget& target,
    const SplitJulianDate& window_start_jd_ut,
    const SplitJulianDate& window_end_jd_ut,
    uint64_t flags,
    const HeliacalVisibilityConditions* conditions,
    bool force_refine,
    DailyVisibilityWindow* out,
    EphemerisEvalDiagnostic* diagnostic
) noexcept {
    if (!context || !out || !split_julian_date_is_finite(window_start_jd_ut)
        || !split_julian_date_is_finite(window_end_jd_ut)
        || !(window_end_jd_ut > window_start_jd_ut)) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    *out = DailyVisibilityWindow();
    out->start_jd_ut = window_start_jd_ut;
    out->end_jd_ut = window_end_jd_ut;

    auto eval_at = [&](double offset_days, HeliacalVisibilityResult* result) -> Status {
        const SplitJulianDate jd_ut = window_start_jd_ut + offset_days;
        const Status status = eval_target_visibility(
            context, target, jd_ut, flags, conditions, result, diagnostic);
        if (status == TAIYIN_STATUS_OK) ++out->evaluation_count;
        return status;
    };
    auto consider = [&](double offset_days, const HeliacalVisibilityResult& result) noexcept -> bool {
        if (!std::isfinite(result.visibility_margin_magnitude)) return false;
        if (!split_julian_date_is_finite(out->best_jd_ut)
            || result.visibility_margin_magnitude > out->visibility.visibility_margin_magnitude) {
            out->best_jd_ut = window_start_jd_ut + offset_days;
            out->visibility = result;
        }
        return true;
    };

    const double window_duration_days = window_end_jd_ut - window_start_jd_ut;
    double samples[kCoarseWindowSampleCount] = {};
    HeliacalVisibilityResult sample_results[kCoarseWindowSampleCount];
    int best_index = -1;
    Status status = TAIYIN_STATUS_OK;
    for (int i = 0; i < kCoarseWindowSampleCount; ++i) {
        const double fraction = static_cast<double>(i)
            / static_cast<double>(kCoarseWindowSampleCount - 1);
        samples[i] = fraction * window_duration_days;
        status = eval_at(samples[i], &sample_results[i]);
        if (status != TAIYIN_STATUS_OK) return status;
        if (!consider(samples[i], sample_results[i])) return TAIYIN_EPHEMERIS_ERROR_EVAL_FAILED;
        if (best_index < 0
            || sample_results[i].visibility_margin_magnitude
                > sample_results[best_index].visibility_margin_magnitude) {
            best_index = i;
        }
    }
    if (best_index < 0) return TAIYIN_EPHEMERIS_ERROR_EVAL_FAILED;
    const bool should_refine = force_refine
        || std::fabs(sample_results[best_index].visibility_margin_magnitude)
            <= kCoarseRefineMarginMagnitude;
    if (!should_refine) {
        out->valid = true;
        return TAIYIN_STATUS_OK;
    }

    double left = best_index > 0 ? samples[best_index - 1] : samples[best_index];
    double right = best_index + 1 < kCoarseWindowSampleCount
        ? samples[best_index + 1] : samples[best_index];
    if (!(right > left)) {
        out->valid = true;
        return TAIYIN_STATUS_OK;
    }
    // Bounded Brent maximization: a parabolic candidate saves evaluations on
    // the smooth part of a twilight curve, while an invalid candidate falls
    // back to a golden-section step and always remains inside [left, right].
    double x = samples[best_index];
    double w = x;
    double v = x;
    double fx = sample_results[best_index].visibility_margin_magnitude;
    double fw = fx;
    double fv = fx;
    double step = 0.0;
    double previous_step = 0.0;
    const double relative_tolerance = std::sqrt(std::numeric_limits<double>::epsilon());

    for (int iteration = 0; iteration < kMaxWindowOptimizeIterations; ++iteration) {
        const double midpoint = 0.5 * (left + right);
        const double tolerance = kWindowToleranceDays + relative_tolerance * std::fabs(x);
        const double double_tolerance = 2.0 * tolerance;
        if (std::fabs(x - midpoint) <= double_tolerance - 0.5 * (right - left)) break;

        bool used_parabola = false;
        if (std::fabs(previous_step) > tolerance) {
            const double r = (x - w) * (fx - fv);
            const double q = (x - v) * (fx - fw);
            double numerator = (x - v) * q - (x - w) * r;
            double denominator = 2.0 * (q - r);
            if (denominator > 0.0) numerator = -numerator;
            denominator = std::fabs(denominator);
            const double old_previous_step = previous_step;
            previous_step = step;
            if (denominator > 0.0
                && std::fabs(numerator) < std::fabs(0.5 * denominator * old_previous_step)
                && numerator > denominator * (left - x)
                && numerator < denominator * (right - x)) {
                step = numerator / denominator;
                const double candidate = x + step;
                if (candidate - left < double_tolerance || right - candidate < double_tolerance) {
                    step = midpoint >= x ? tolerance : -tolerance;
                }
                used_parabola = true;
            }
        }
        if (!used_parabola) {
            previous_step = x >= midpoint ? left - x : right - x;
            step = kGoldenRatioConjugate * previous_step;
        }
        const double candidate = std::fabs(step) >= tolerance
            ? x + step : x + (step >= 0.0 ? tolerance : -tolerance);
        HeliacalVisibilityResult result_candidate;
        status = eval_at(candidate, &result_candidate);
        if (status != TAIYIN_STATUS_OK) return status;
        if (!consider(candidate, result_candidate)) return TAIYIN_EPHEMERIS_ERROR_EVAL_FAILED;
        const double candidate_margin = result_candidate.visibility_margin_magnitude;
        if (candidate_margin >= fx) {
            if (candidate >= x) left = x;
            else right = x;
            v = w;
            fv = fw;
            w = x;
            fw = fx;
            x = candidate;
            fx = candidate_margin;
        } else {
            if (candidate < x) left = candidate;
            else right = candidate;
            if (candidate_margin >= fw || w == x) {
                v = w;
                fv = fw;
                w = candidate;
                fw = candidate_margin;
            } else if (candidate_margin >= fv || v == x || v == w) {
                v = candidate;
                fv = candidate_margin;
            }
        }
    }
    out->valid = split_julian_date_is_finite(out->best_jd_ut);
    return out->valid ? TAIYIN_STATUS_OK : TAIYIN_EPHEMERIS_ERROR_EVAL_FAILED;
}

Status search_next_heliacal_visibility_impl_ut(
    const NativeCalcContext* context,
    const HeliacalTarget& target,
    const SplitJulianDate& jd_start_ut,
    int event_kind,
    double max_search_days,
    uint64_t flags,
    const HeliacalVisibilityConditions* conditions,
    HeliacalVisibilitySearchResult* out,
    EphemerisEvalDiagnostic* diagnostic
) noexcept {
    if (out) *out = HeliacalVisibilitySearchResult();
    if (!context || !out || !split_julian_date_is_finite(jd_start_ut)
        || !valid_event_kind(event_kind)
        || !std::isfinite(max_search_days) || !(max_search_days > 0.0)
        || max_search_days > kMaximumSearchDays
        || !valid_heliacal_visibility_flags(flags)
        || !native_context_has_observer_location(*context)) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    const bool morning = morning_event_kind(event_kind);
    const bool first = first_event_kind(event_kind);
    SplitJulianDate normalized_start;
    if (!normalize_split_julian_date(
            jd_start_ut.day_number, jd_start_ut.day_fraction, &normalized_start)) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    const SplitJulianDate first_midnight_jd_ut = normalized_start.day_fraction >= 0.5
        ? SplitJulianDate(normalized_start.day_number, 0.5)
        : SplitJulianDate(normalized_start.day_number - 1, 0.5);
    const SplitJulianDate search_end_jd_ut = jd_start_ut + max_search_days;
    if (!split_julian_date_is_finite(first_midnight_jd_ut)
        || !split_julian_date_is_finite(search_end_jd_ut)
        || !(search_end_jd_ut > jd_start_ut)) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    const SplitJulianDate first_day_start_jd_ut = first_midnight_jd_ut - 1.0;
    if (!split_julian_date_is_finite(first_day_start_jd_ut)) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    const int maximum_day_count = static_cast<int>(std::ceil(max_search_days)) + 2;
    DailyVisibilityWindow previous;
    bool have_previous = false;

    for (int day_index = 0; day_index < maximum_day_count; ++day_index) {
        const SplitJulianDate day_start_jd_ut = first_day_start_jd_ut + static_cast<double>(day_index);
        if (!split_julian_date_is_finite(day_start_jd_ut)) return TAIYIN_ERROR_INVALID_ARGUMENT;
        if (day_start_jd_ut >= search_end_jd_ut) break;
        ++out->scanned_day_count;
        SplitJulianDate window_start_jd_ut(0, std::numeric_limits<double>::quiet_NaN());
        SplitJulianDate window_end_jd_ut(0, std::numeric_limits<double>::quiet_NaN());
        Status status = find_daily_twilight_window(
            context,
            day_start_jd_ut,
            morning,
            &window_start_jd_ut,
            &window_end_jd_ut,
            diagnostic);
        if (status == TAIYIN_EVENT_ERROR_NOT_FOUND) {
            have_previous = false;
            continue;
        }
        if (status != TAIYIN_STATUS_OK) return status;

        DailyVisibilityWindow current;
        status = maximize_daily_visibility_window(
            context,
            target,
            window_start_jd_ut,
            window_end_jd_ut,
            flags,
            conditions,
            false,
            &current,
            diagnostic);
        if (status != TAIYIN_STATUS_OK) return status;
        ++out->sampled_window_count;
        out->visibility_evaluation_count += current.evaluation_count;

        if (current.best_jd_ut < jd_start_ut) {
            previous = current;
            have_previous = true;
            continue;
        }
        const bool current_visible = current.visibility.visible != 0;
        if (have_previous) {
            const bool previous_visible = previous.visibility.visible != 0;
            bool transition_matches = first
                ? !previous_visible && current_visible
                : previous_visible && !current_visible;
            if (transition_matches) {
                DailyVisibilityWindow refined_previous;
                status = maximize_daily_visibility_window(
                    context,
                    target,
                    previous.start_jd_ut,
                    previous.end_jd_ut,
                    flags,
                    conditions,
                    true,
                    &refined_previous,
                    diagnostic);
                if (status != TAIYIN_STATUS_OK) return status;
                out->visibility_evaluation_count += refined_previous.evaluation_count;
                DailyVisibilityWindow refined_current;
                status = maximize_daily_visibility_window(
                    context,
                    target,
                    current.start_jd_ut,
                    current.end_jd_ut,
                    flags,
                    conditions,
                    true,
                    &refined_current,
                    diagnostic);
                if (status != TAIYIN_STATUS_OK) return status;
                out->visibility_evaluation_count += refined_current.evaluation_count;
                previous = refined_previous;
                current = refined_current;
                transition_matches = first
                    ? previous.visibility.visible == 0 && current.visibility.visible != 0
                    : previous.visibility.visible != 0 && current.visibility.visible == 0;
            }
            if (transition_matches) {
                const DailyVisibilityWindow& event_window = first ? current : previous;
                if (event_window.best_jd_ut >= jd_start_ut
                    && event_window.best_jd_ut <= search_end_jd_ut) {
                    out->event_kind = event_kind;
                    out->jd_ut = event_window.best_jd_ut;
                    out->window_start_jd_ut = event_window.start_jd_ut;
                    out->window_end_jd_ut = event_window.end_jd_ut;
                    out->visibility = event_window.visibility;
                    return TAIYIN_STATUS_OK;
                }
            }
        }
        previous = current;
        have_previous = true;
    }
    return TAIYIN_EVENT_ERROR_NOT_FOUND;
}

}  // namespace

HeliacalVisibilitySearchResult::HeliacalVisibilitySearchResult() noexcept
    : event_kind(0),
      jd_ut(0, std::numeric_limits<double>::quiet_NaN()),
      window_start_jd_ut(0, std::numeric_limits<double>::quiet_NaN()),
      window_end_jd_ut(0, std::numeric_limits<double>::quiet_NaN()),
      scanned_day_count(0),
      sampled_window_count(0),
      visibility_evaluation_count(0),
      visibility() {}

Status search_next_body_heliacal_visibility_ut(
    const NativeCalcContext* context,
    int body_id,
    const SplitJulianDate& jd_start_ut,
    int event_kind,
    double max_search_days,
    uint64_t flags,
    const HeliacalVisibilityConditions* conditions,
    HeliacalVisibilitySearchResult* out,
    EphemerisEvalDiagnostic* diagnostic
) noexcept {
    if (!valid_heliacal_body_target(body_id)) return TAIYIN_ERROR_INVALID_ARGUMENT;
    HeliacalTarget target;
    target.kind = HELIACAL_TARGET_BODY;
    target.body_id = body_id;
    target.star_key = nullptr;
    return search_next_heliacal_visibility_impl_ut(
        context, target, jd_start_ut, event_kind, max_search_days, flags, conditions, out, diagnostic);
}

Status search_next_star_heliacal_visibility_ut(
    const NativeCalcContext* context,
    const char* star_key,
    const SplitJulianDate& jd_start_ut,
    int event_kind,
    double max_search_days,
    uint64_t flags,
    const HeliacalVisibilityConditions* conditions,
    HeliacalVisibilitySearchResult* out,
    EphemerisEvalDiagnostic* diagnostic
) noexcept {
    if (!star_key || star_key[0] == '\0') return TAIYIN_ERROR_INVALID_ARGUMENT;
    HeliacalTarget target;
    target.kind = HELIACAL_TARGET_STAR;
    target.body_id = 0;
    target.star_key = star_key;
    return search_next_heliacal_visibility_impl_ut(
        context, target, jd_start_ut, event_kind, max_search_days, flags, conditions, out, diagnostic);
}

}  // namespace runtime
}  // namespace taiyin
