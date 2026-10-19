#include "taiyin/chinese_calendar/chart_clock.h"
#include "taiyin/chinese_calendar/ganzhi.h"
#include "taiyin/runtime/solar_time.h"
#include <cmath>

namespace taiyin {
namespace chinese_calendar {
namespace {
constexpr double kTwoPi = 6.283185307179586476925286766559;
bool valid(const ChartClock& clock) noexcept {
    if (clock.mode == ChartClockMode::FixedOffset) return true;
    return (clock.mode == ChartClockMode::MeanSolar
        || clock.mode == ChartClockMode::ApparentSolar)
        && std::isfinite(clock.longitude_rad)
        && std::fabs(clock.longitude_rad) <= kTwoPi / 2.0;
}
double offset(const chinese_calendar::ChineseCalendarContext& calendar,
    const ChartClock& clock) noexcept {
    return clock.mode == ChartClockMode::FixedOffset
        ? calendar.config.utc_offset_minutes / 1440.0 : clock.longitude_rad / kTwoPi;
}
}

Status chart_time_from_ut1(
    const chinese_calendar::ChineseCalendarContext* calendar,
    const ChartClock& clock, const SplitJulianDate& jd_ut1,
    CalendarDateTime* out, runtime::EphemerisEvalDiagnostic* diagnostic) noexcept {
    if (!calendar || !out || !valid(clock) || !split_julian_date_is_finite(jd_ut1))
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    SplitJulianDate local = jd_ut1 + offset(*calendar, clock);
    if (clock.mode == ChartClockMode::ApparentSolar) {
        const Status s = runtime::local_mean_to_apparent_solar_time(
            &calendar->astronomy, local, clock.longitude_rad, &local, diagnostic);
        if (s != TAIYIN_STATUS_OK) return s;
    }
    CalendarDateTime result;
    if (!reverse_julian_day_split(local, &result)) return TAIYIN_ERROR_INVALID_ARGUMENT;
    return chinese_calendar::normalize_chart_virtual_time(result, out);
}

Status chart_time_to_ut1(
    const chinese_calendar::ChineseCalendarContext* calendar,
    const ChartClock& clock, const CalendarDateTime& time,
    SplitJulianDate* out, runtime::EphemerisEvalDiagnostic* diagnostic) noexcept {
    if (!calendar || !out || !valid(clock)) return TAIYIN_ERROR_INVALID_ARGUMENT;
    CalendarDateTime normalized;
    Status s = chinese_calendar::normalize_chart_virtual_time(time, &normalized);
    if (s != TAIYIN_STATUS_OK) return s;
    SplitJulianDate local;
    if (!julian_day_split(normalized, &local)) return TAIYIN_ERROR_INVALID_ARGUMENT;
    if (clock.mode == ChartClockMode::ApparentSolar) {
        s = runtime::local_apparent_to_mean_solar_time(
            &calendar->astronomy, local, clock.longitude_rad, &local, diagnostic);
        if (s != TAIYIN_STATUS_OK) return s;
    }
    SplitJulianDate result = local - offset(*calendar, clock);
    if (!split_julian_date_is_finite(result)) return TAIYIN_ERROR_INVALID_ARGUMENT;
    // Verify every inverse, including subsecond values produced by a previous
    // clock mapping/navigation. Checking only exact hours can move a requested
    // midnight + a few nanoseconds into the preceding date. This corrects the
    // inverse residual, not the supplied civil fields or arbitrary UT1 inputs.
    if (clock.mode == ChartClockMode::ApparentSolar) {
        SplitJulianDate boundary;
        if (!julian_day_split(normalized, &boundary)) return TAIYIN_ERROR_INVALID_ARGUMENT;
        const auto residual_at = [&](const SplitJulianDate& jd, double* residual) -> Status {
            CalendarDateTime evaluated;
            const Status status = chart_time_from_ut1(calendar, clock, jd, &evaluated, diagnostic);
            if (status != TAIYIN_STATUS_OK) return status;
            SplitJulianDate evaluated_jd;
            if (!julian_day_split(evaluated, &evaluated_jd)) return TAIYIN_ERROR_INTERNAL;
            *residual = evaluated_jd - boundary;
            return std::fabs(*residual) <= 4.0e-11
                ? TAIYIN_STATUS_OK : TAIYIN_EPHEMERIS_ERROR_EVAL_FAILED;
        };
        bool have_lower = false;
        bool have_upper = false;
        SplitJulianDate lower, upper;
        for (int i = 0; i < 8; ++i) {
            double residual;
            s = residual_at(result, &residual);
            if (s != TAIYIN_STATUS_OK) return s;
            if (residual >= 0.0) {
                upper = result;
                have_upper = true;
                if (have_lower) break;
                // An initially positive (or zero, normalized) residual still
                // needs a lower bracket. Do not accept the late initial guess.
                result -= residual + 1.0e-11;
            } else {
                lower = result;
                have_lower = true;
                if (have_upper) break;
                result += -residual + 1.0e-11;
            }
        }
        if (!have_lower || !have_upper) return TAIYIN_EPHEMERIS_ERROR_EVAL_FAILED;
        // Find the smallest right-side inverse, including normalized civil-hour
        // plateaus. The stepping epsilon only establishes a bracket, never the
        // returned boundary. Refine until adjacent representable Split-JDs.
        for (int i = 0; i < 64; ++i) {
            const SplitJulianDate middle = lower + (upper - lower) / 2.0;
            if (middle == lower || middle == upper) break;
            double residual;
            s = residual_at(middle, &residual);
            if (s != TAIYIN_STATUS_OK) return s;
            if (residual >= 0.0) upper = middle;
            else lower = middle;
        }
        result = upper;
    }
    *out = result;
    return TAIYIN_STATUS_OK;
}
}  // namespace chinese_calendar
}  // namespace taiyin
