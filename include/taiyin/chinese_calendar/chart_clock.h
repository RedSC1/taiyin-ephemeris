#ifndef TAIYIN_CHINESE_CALENDAR_CHART_CLOCK_H
#define TAIYIN_CHINESE_CALENDAR_CHART_CLOCK_H

#include "taiyin/chinese_calendar/calendar.h"

namespace taiyin { namespace chinese_calendar {

#if defined(_WIN32) && defined(TAIYIN_GANZHI_SHARED_BUILD)
#define TAIYIN_CHART_CLOCK_API __declspec(dllexport)
#elif defined(_WIN32) && defined(TAIYIN_GANZHI_SHARED_IMPORT)
#define TAIYIN_CHART_CLOCK_API __declspec(dllimport)
#elif defined(TAIYIN_GANZHI_SHARED_BUILD) || defined(TAIYIN_GANZHI_SHARED_IMPORT)
#define TAIYIN_CHART_CLOCK_API __attribute__((visibility("default")))
#else
#define TAIYIN_CHART_CLOCK_API
#endif

// Metaphysics virtual clock, independent of lunar-month day-boundary policy.
enum class ChartClockMode { FixedOffset = 0, MeanSolar = 1, ApparentSolar = 2 };
struct ChartClock {
    ChartClockMode mode;
    double longitude_rad; // East-positive radians, used only for solar clocks.
    ChartClock() noexcept : mode(ChartClockMode::FixedOffset), longitude_rad(0.0) {}
};

// Inputs are UT1, not UTC. FixedOffset adds the calendar's configured offset
// to UT1; callers must explicitly convert UTC under their EOP/estimate policy.
TAIYIN_CHART_CLOCK_API Status chart_time_from_ut1(const ChineseCalendarContext*, const ChartClock&,
    const SplitJulianDate&, CalendarDateTime*,
    runtime::EphemerisEvalDiagnostic* = NULL) noexcept;
TAIYIN_CHART_CLOCK_API Status chart_time_to_ut1(const ChineseCalendarContext*, const ChartClock&,
    const CalendarDateTime&, SplitJulianDate*,
    runtime::EphemerisEvalDiagnostic* = NULL) noexcept;

} } // namespace taiyin::chinese_calendar
#undef TAIYIN_CHART_CLOCK_API
#endif
