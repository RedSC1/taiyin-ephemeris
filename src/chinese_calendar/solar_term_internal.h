#ifndef TAIYIN_CHINESE_CALENDAR_SOLAR_TERM_INTERNAL_H
#define TAIYIN_CHINESE_CALENDAR_SOLAR_TERM_INTERNAL_H

namespace taiyin {
namespace chinese_calendar {
namespace internal {

// Independently refined copies of the same crossing can differ by a few
// tenths of a microsecond. This is a solver equality floor, not a civil-time
// boundary window.
constexpr double kSolarTermRootEqualityToleranceDays = 1.0e-10;

}  // namespace internal
}  // namespace chinese_calendar
}  // namespace taiyin

#endif  // TAIYIN_CHINESE_CALENDAR_SOLAR_TERM_INTERNAL_H
