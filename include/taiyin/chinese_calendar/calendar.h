#ifndef TAIYIN_CHINESE_CALENDAR_CALENDAR_H
#define TAIYIN_CHINESE_CALENDAR_CALENDAR_H

#include "taiyin/runtime/native_context.h"
#include "taiyin/status.h"

#include <cstddef>
#include <cstdint>

namespace taiyin {
namespace chinese_calendar {

constexpr std::size_t TAIYIN_CHINESE_CALENDAR_TERM_COUNT = 25;
constexpr std::size_t TAIYIN_CHINESE_CALENDAR_NEW_MOON_COUNT = 15;
constexpr std::size_t TAIYIN_CHINESE_CALENDAR_MONTH_COUNT = 14;

enum ChineseCalendarRuleMode {
    TAIYIN_CHINESE_CALENDAR_HISTORICAL_CHINA = 0,
    TAIYIN_CHINESE_CALENDAR_ASTRONOMICAL = 1,
};

enum ChineseCalendarDayBoundaryMode {
    TAIYIN_CHINESE_CALENDAR_FIXED_UTC_OFFSET = 0,
    TAIYIN_CHINESE_CALENDAR_MEAN_SOLAR_MERIDIAN = 1,
};

enum ChineseCalendarMonthName {
    TAIYIN_CHINESE_MONTH_NAME_NORMAL = 0,
    TAIYIN_CHINESE_MONTH_NAME_THIRTEEN = 1,
    TAIYIN_CHINESE_MONTH_NAME_LATER_NINE = 2,
    TAIYIN_CHINESE_MONTH_NAME_ALT_TWELVE = 3,
    TAIYIN_CHINESE_MONTH_NAME_ALT_ONE = 4,
    // The later of two months that have the same written numeric name in one
    // historical lunar year. Render it using `month`; this value distinguishes
    // identity for bidirectional conversion rather than changing the label.
    TAIYIN_CHINESE_MONTH_NAME_LATER_SAME_NAME = 5,
};

struct ChineseCalendarConfig {
    // Native C++ representation. This is intentionally not byte-compatible
    // with the versioned C ABI structs; use the C API conversion layer rather
    // than memcpy or reinterpret_cast between the two representations.
    int32_t rule_mode;
    int32_t day_boundary_mode;
    int32_t utc_offset_minutes;
    int32_t reserved;
    double calendar_meridian_deg;

    ChineseCalendarConfig() noexcept;
};

struct ChineseCalendarContext {
    runtime::NativeCalcContext astronomy;
    ChineseCalendarConfig config;

    ChineseCalendarContext() noexcept;
    ChineseCalendarContext(const ChineseCalendarContext& other) noexcept;
    ChineseCalendarContext& operator=(
        const ChineseCalendarContext& other) noexcept;
};

struct SolarDate {
    int32_t year;
    uint8_t month;
    uint8_t day;
    uint8_t reserved[2];

    SolarDate() noexcept;
};

struct LunarDate {
    int32_t year;
    uint8_t month;
    uint8_t day;
    uint8_t is_leap;
    uint8_t month_days;
    uint8_t month_name;
    uint8_t reserved[3];

    LunarDate() noexcept;
};

struct SolarTermEvent {
    // Index 0 is winter solstice. Indices then advance by 15 degrees of
    // apparent geocentric solar longitude; index 24 is the next solstice.
    // Standalone prev/next queries use this same index modulo 24.
    uint8_t index_from_winter_solstice;
    uint8_t reserved[7];
    double target_longitude_rad;
    SplitJulianDate jd_ut;
    int64_t civil_day_number;

    SolarTermEvent() noexcept;
};

struct NewMoonEvent {
    SplitJulianDate jd_ut;
    int64_t civil_day_number;

    NewMoonEvent() noexcept;
};

struct ChineseCalendarMonth {
    int32_t lunar_year;
    uint8_t month;
    uint8_t is_leap;
    uint8_t day_count;
    uint8_t month_name;
    int64_t first_civil_day_number;
    SplitJulianDate astronomical_new_moon_jd_ut;

    ChineseCalendarMonth() noexcept;
};

struct ChineseCalendarYear {
    SolarTermEvent solar_terms[TAIYIN_CHINESE_CALENDAR_TERM_COUNT];
    NewMoonEvent new_moons[TAIYIN_CHINESE_CALENDAR_NEW_MOON_COUNT];
    ChineseCalendarMonth months[TAIYIN_CHINESE_CALENDAR_MONTH_COUNT];
    uint8_t solar_term_count;
    uint8_t new_moon_count;
    uint8_t month_count;
    int8_t leap_month_index;
    int64_t first_winter_solstice_day_number;
    int64_t second_winter_solstice_day_number;

    ChineseCalendarYear() noexcept;
};

ChineseCalendarConfig historical_china_config() noexcept;
ChineseCalendarConfig fixed_utc_offset_config(
    int32_t utc_offset_minutes) noexcept;
ChineseCalendarConfig fixed_meridian_config(double longitude_deg) noexcept;

Status initialize_context(
    ChineseCalendarContext* out,
    const runtime::NativeCalcContext* astronomy,
    const ChineseCalendarConfig* config
) noexcept;

// Calculate the lunisolar layout containing jd_ut. The result spans one
// winter solstice to the next and intentionally exposes both astronomical UT
// instants and the calendar-profile civil days assigned to those instants.
Status calcY(
    const ChineseCalendarContext* context,
    SplitJulianDate jd_ut,
    ChineseCalendarYear* out,
    runtime::EphemerisEvalDiagnostic* diagnostic
) noexcept;

// Calculate one term directly without materializing a calendar year. The
// term_index_from_vernal_equinox uses a spring-equinox seasonal cycle:
// 0 is the spring equinox and 18 is the winter solstice in civil_year;
// 19 through 23 are Xiaohan through Jingzhe earlier in the same civil year.
// For remote proleptic years this index selects the seasonal crossing; it does
// not guarantee the rendered Gregorian date remains within civil_year.
Status getSpecificJieQi(
    const ChineseCalendarContext* context,
    int32_t civil_year,
    uint8_t term_index_from_vernal_equinox,
    SolarTermEvent* out,
    runtime::EphemerisEvalDiagnostic* diagnostic
) noexcept;

// Prev includes an event exactly at jd_ut, while Next advances to the
// subsequent term.
Status getPrevJieQi(
    const ChineseCalendarContext* context,
    SplitJulianDate jd_ut,
    SolarTermEvent* out,
    runtime::EphemerisEvalDiagnostic* diagnostic
) noexcept;

Status getNextJieQi(
    const ChineseCalendarContext* context,
    SplitJulianDate jd_ut,
    SolarTermEvent* out,
    runtime::EphemerisEvalDiagnostic* diagnostic
) noexcept;

Status getPrevJie(
    const ChineseCalendarContext* context,
    SplitJulianDate jd_ut,
    SolarTermEvent* out,
    runtime::EphemerisEvalDiagnostic* diagnostic
) noexcept;

Status getNextJie(
    const ChineseCalendarContext* context,
    SplitJulianDate jd_ut,
    SolarTermEvent* out,
    runtime::EphemerisEvalDiagnostic* diagnostic
) noexcept;

Status getPrevQi(
    const ChineseCalendarContext* context,
    SplitJulianDate jd_ut,
    SolarTermEvent* out,
    runtime::EphemerisEvalDiagnostic* diagnostic
) noexcept;

Status getNextQi(
    const ChineseCalendarContext* context,
    SplitJulianDate jd_ut,
    SolarTermEvent* out,
    runtime::EphemerisEvalDiagnostic* diagnostic
) noexcept;

Status fromSolar(
    const ChineseCalendarContext* context,
    const SolarDate* solar,
    LunarDate* out,
    runtime::EphemerisEvalDiagnostic* diagnostic
) noexcept;

Status fromLunar(
    const ChineseCalendarContext* context,
    const LunarDate* lunar,
    SolarDate* out,
    runtime::EphemerisEvalDiagnostic* diagnostic
) noexcept;

Status getLunarMonthNum(
    const ChineseCalendarContext* context,
    int32_t lunar_year,
    uint8_t month,
    bool is_leap,
    uint8_t* out_day_count,
    runtime::EphemerisEvalDiagnostic* diagnostic
) noexcept;

}  // namespace chinese_calendar
}  // namespace taiyin

#endif  // TAIYIN_CHINESE_CALENDAR_CALENDAR_H
