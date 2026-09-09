#ifndef TAIYIN_ZIWEI_CLOCK_H
#define TAIYIN_ZIWEI_CLOCK_H

#include "taiyin/chinese_calendar/calendar.h"

namespace taiyin {
namespace ziwei {

// Independent of the calendar's month-structure/day-boundary policy.
enum class ChartClockMode { FixedOffset = 0, MeanSolar = 1, ApparentSolar = 2 };
struct ChartClock {
    ChartClockMode mode;
    // East-positive radians; used only for solar clocks.
    double longitude_rad;
    ChartClock() noexcept : mode(ChartClockMode::FixedOffset), longitude_rad(0.0) {}
};

// UT1 is explicit: callers with UTC must resolve it through the runtime time
// conversion policy first. FixedOffset applies the calendar's configured
// offset to UT1; it is not a UTC/DST conversion service.
Status chart_time_from_ut1(
    const chinese_calendar::ChineseCalendarContext* calendar,
    const ChartClock& clock, const SplitJulianDate& jd_ut1,
    CalendarDateTime* out, runtime::EphemerisEvalDiagnostic* diagnostic = NULL) noexcept;
Status chart_time_to_ut1(
    const chinese_calendar::ChineseCalendarContext* calendar,
    const ChartClock& clock, const CalendarDateTime& time,
    SplitJulianDate* out, runtime::EphemerisEvalDiagnostic* diagnostic = NULL) noexcept;

}  // namespace ziwei
}  // namespace taiyin
#endif
