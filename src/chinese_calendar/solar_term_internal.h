#ifndef TAIYIN_CHINESE_CALENDAR_SOLAR_TERM_INTERNAL_H
#define TAIYIN_CHINESE_CALENDAR_SOLAR_TERM_INTERNAL_H

#include "taiyin/chinese_calendar/calendar.h"

#include <cstdint>

namespace taiyin {
namespace chinese_calendar {
namespace internal {

// Independently refined copies of the same crossing can differ by a few
// tenths of a microsecond. This is a solver equality floor, not a civil-time
// boundary window.
constexpr double kSolarTermRootEqualityToleranceDays = 1.0e-10;

// True when the context's historical mode actually supplied this term's
// civil day from the historical profile rather than the astronomical
// fallback.  The ganzhi module uses this to normalize pillar boundaries to
// the assigned civil day without touching the calendar arrangement path.
bool historical_profile_term_day(
    const ChineseCalendarContext& context,
    const SolarTermEvent& term,
    int64_t* out_civil_day_number
) noexcept;

}  // namespace internal
}  // namespace chinese_calendar
}  // namespace taiyin

#endif  // TAIYIN_CHINESE_CALENDAR_SOLAR_TERM_INTERNAL_H
