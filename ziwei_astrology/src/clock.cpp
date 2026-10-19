#include "taiyin/ziwei/clock.h"
#include "taiyin/chinese_calendar/chart_clock.h"

namespace taiyin { namespace ziwei {
namespace {
chinese_calendar::ChartClock shared_clock(const ChartClock& source) noexcept {
    chinese_calendar::ChartClock out;
    out.mode = static_cast<chinese_calendar::ChartClockMode>(source.mode);
    out.longitude_rad = source.longitude_rad;
    return out;
}
}
// Preserve existing Ziwei types/symbols while sharing the exact clock solver.
Status chart_time_from_ut1(const chinese_calendar::ChineseCalendarContext* calendar,
    const ChartClock& clock, const SplitJulianDate& instant, CalendarDateTime* out,
    runtime::EphemerisEvalDiagnostic* diagnostic) noexcept {
    return chinese_calendar::chart_time_from_ut1(calendar, shared_clock(clock), instant, out, diagnostic);
}
Status chart_time_to_ut1(const chinese_calendar::ChineseCalendarContext* calendar,
    const ChartClock& clock, const CalendarDateTime& time, SplitJulianDate* out,
    runtime::EphemerisEvalDiagnostic* diagnostic) noexcept {
    return chinese_calendar::chart_time_to_ut1(calendar, shared_clock(clock), time, out, diagnostic);
}
} }
