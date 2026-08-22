#ifndef TAIYIN_ZIWEI_CALENDAR_ADAPTER_INTERNAL_H
#define TAIYIN_ZIWEI_CALENDAR_ADAPTER_INTERNAL_H

#include "taiyin/chinese_calendar/calendar.h"
#include "taiyin/status.h"
#include "taiyin/time.h"

#include <cstdint>

namespace taiyin {
namespace ziwei {
namespace detail {

Status resolve_logical_lunar_date(
    const chinese_calendar::ChineseCalendarContext* calendar,
    const CalendarDateTime& virtual_time,
    int32_t rat_hour_mode,
    chinese_calendar::LunarDate* out,
    runtime::EphemerisEvalDiagnostic* diagnostic
) noexcept;

Status calculate_solar_day_from_previous_jie(
    const chinese_calendar::ChineseCalendarContext* calendar,
    const SplitJulianDate& instant_utc,
    const CalendarDateTime& virtual_time,
    int32_t rat_hour_mode,
    uint16_t* out,
    runtime::EphemerisEvalDiagnostic* diagnostic
) noexcept;

}  // namespace detail
}  // namespace ziwei
}  // namespace taiyin

#endif  // TAIYIN_ZIWEI_CALENDAR_ADAPTER_INTERNAL_H
