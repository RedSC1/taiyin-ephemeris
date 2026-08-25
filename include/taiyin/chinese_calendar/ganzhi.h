#ifndef TAIYIN_CHINESE_CALENDAR_GANZHI_H
#define TAIYIN_CHINESE_CALENDAR_GANZHI_H

#include "taiyin/chinese_calendar/calendar.h"
#include "taiyin/status.h"
#include "taiyin/time.h"

#include <cstdint>

#if defined(_WIN32)
#if defined(TAIYIN_GANZHI_SHARED_BUILD)
#define TAIYIN_GANZHI_SHARED_API __declspec(dllexport)
#elif defined(TAIYIN_GANZHI_SHARED_IMPORT)
#define TAIYIN_GANZHI_SHARED_API __declspec(dllimport)
#else
#define TAIYIN_GANZHI_SHARED_API
#endif
#elif (defined(__GNUC__) || defined(__clang__)) \
    && (defined(TAIYIN_GANZHI_SHARED_BUILD) \
        || defined(TAIYIN_GANZHI_SHARED_IMPORT))
#define TAIYIN_GANZHI_SHARED_API __attribute__((visibility("default")))
#else
#define TAIYIN_GANZHI_SHARED_API
#endif

namespace taiyin {
namespace chinese_calendar {

constexpr uint8_t kInvalidGanzhi = 0xffu;
constexpr uint8_t kInvalidNaYin = 0xffu;

enum GanzhiWuXing {
    GanzhiWuXingWater = 0,
    GanzhiWuXingWood = 1,
    GanzhiWuXingMetal = 2,
    GanzhiWuXingEarth = 3,
    GanzhiWuXingFire = 4,
};

enum GanzhiRatHourMode {
    TAIYIN_GANZHI_RAT_HOUR_NO_SPLIT = 0,
    TAIYIN_GANZHI_RAT_HOUR_TODAY_GAN = 1,
    TAIYIN_GANZHI_RAT_HOUR_TOMORROW_GAN = 2,
};

struct TAIYIN_GANZHI_SHARED_API GanzhiFourPillars {
    uint8_t year;
    uint8_t month;
    uint8_t day;
    uint8_t hour;

    GanzhiFourPillars() noexcept;
};

// Normalizes the resolved virtual clock used by Chinese metaphysics.  This
// recognizes only the exact scalar/split-JD spellings of a civil-hour
// boundary and canonicalizes its calendar fields, preventing a JD round trip
// from spelling 11:00 as 10:59:59.999... . It never changes the physical UTC
// instant and must not be used by general astronomical time conversion.
TAIYIN_GANZHI_SHARED_API Status normalize_chart_virtual_time(
    const CalendarDateTime& virtual_time,
    CalendarDateTime* out
) noexcept;

// These are deliberately exported for the native BaZi extension to consume.
TAIYIN_GANZHI_SHARED_API Status make_ganzhi(
    uint8_t stem_id,
    uint8_t branch_id,
    uint8_t* out
) noexcept;

TAIYIN_GANZHI_SHARED_API Status advance_ganzhi(
    uint8_t value,
    int32_t delta,
    uint8_t* out
) noexcept;

// month_index follows the traditional sequence: 0=Yin, ..., 10=Zi, 11=Chou.
TAIYIN_GANZHI_SHARED_API Status get_month_ganzhi(
    uint8_t year_stem_id,
    uint8_t month_index,
    uint8_t* out
) noexcept;

// hour_index follows the branch sequence: 0=Zi, ..., 11=Hai.
TAIYIN_GANZHI_SHARED_API Status get_hour_ganzhi(
    uint8_t day_stem_id,
    uint8_t hour_index,
    uint8_t* out
) noexcept;

// Calculates the civil-date day pillar using the same noon/J2000 convention
// as calculate_four_pillars. The time-of-day fields are ignored.
TAIYIN_GANZHI_SHARED_API Status calculate_day_pillar(
    const CalendarDateTime& civil_date,
    uint8_t* out
) noexcept;

TAIYIN_GANZHI_SHARED_API Status get_nayin_element(
    uint8_t ganzhi,
    uint8_t* out_element_id
) noexcept;

TAIYIN_GANZHI_SHARED_API Status get_nayin_id(
    uint8_t ganzhi,
    uint8_t* out_nayin_id
) noexcept;

// virtual_time is the resolved civil clock used for day/hour boundaries. It
// may be the wall clock, mean solar time, or another caller-selected clock.
TAIYIN_GANZHI_SHARED_API Status calculate_four_pillars(
    const ChineseCalendarContext* context,
    const SplitJulianDate& instant_utc,
    const CalendarDateTime& virtual_time,
    int32_t rat_hour_mode,
    GanzhiFourPillars* out,
    runtime::EphemerisEvalDiagnostic* diagnostic
) noexcept;

}  // namespace chinese_calendar
}  // namespace taiyin

#undef TAIYIN_GANZHI_SHARED_API

#endif
