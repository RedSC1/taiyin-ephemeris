#include "calendar_adapter_internal.h"

#include "taiyin/chinese_calendar/ganzhi.h"
#include "taiyin/time.h"

#include <cstdint>
#include <limits>

namespace taiyin {
namespace ziwei {
namespace detail {

Status resolve_logical_lunar_date(
    const chinese_calendar::ChineseCalendarContext* calendar,
    const CalendarDateTime& virtual_time,
    int32_t rat_hour_mode,
    chinese_calendar::LunarDate* out,
    runtime::EphemerisEvalDiagnostic* diagnostic
) noexcept {
    if (calendar == NULL || out == NULL) return TAIYIN_ERROR_INVALID_ARGUMENT;
    SplitJulianDate logical_virtual_jd;
    if (!julian_day_split(virtual_time, &logical_virtual_jd)) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    if (rat_hour_mode == chinese_calendar::TAIYIN_GANZHI_RAT_HOUR_NO_SPLIT
        && virtual_time.hour >= 23
        && !add_seconds_to_split_jd(
            logical_virtual_jd, 3600.0, &logical_virtual_jd)) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    CalendarDateTime logical_virtual_time;
    if (!reverse_julian_day_split(
            logical_virtual_jd, &logical_virtual_time)) {
        return TAIYIN_ERROR_INTERNAL;
    }
    chinese_calendar::SolarDate logical_solar_date;
    logical_solar_date.year = logical_virtual_time.year;
    logical_solar_date.month = static_cast<uint8_t>(logical_virtual_time.month);
    logical_solar_date.day = static_cast<uint8_t>(logical_virtual_time.day);
    return chinese_calendar::fromSolar(
        calendar, &logical_solar_date, out, diagnostic);
}

Status calculate_solar_day_from_previous_jie(
    const chinese_calendar::ChineseCalendarContext* calendar,
    const SplitJulianDate& instant_utc,
    const CalendarDateTime& virtual_time,
    int32_t rat_hour_mode,
    uint16_t* out,
    runtime::EphemerisEvalDiagnostic* diagnostic
) noexcept {
    if (calendar == NULL || out == NULL) return TAIYIN_ERROR_INVALID_ARGUMENT;
    SplitJulianDate virtual_jd;
    if (!julian_day_split(virtual_time, &virtual_jd)) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    chinese_calendar::SolarTermEvent previous_jie;
    const Status status = chinese_calendar::getPrevJie(
        calendar, instant_utc, &previous_jie, diagnostic);
    if (status != TAIYIN_STATUS_OK) return status;

    // Translate the Jie instant into the same wall/solar clock coordinate as
    // virtual_time before assigning both instants to logical civil days.
    const double clock_offset = virtual_jd - instant_utc;
    SplitJulianDate current_logical = virtual_jd;
    if (rat_hour_mode == chinese_calendar::TAIYIN_GANZHI_RAT_HOUR_NO_SPLIT
        && virtual_time.hour >= 23) {
        current_logical += 1.0 / 24.0;
    }
    const SplitJulianDate jie_virtual = previous_jie.jd_ut + clock_offset;
    SplitJulianDate jie_logical = jie_virtual;
    CalendarDateTime jie_clock;
    if (!reverse_julian_day_split(jie_virtual, &jie_clock)) {
        return TAIYIN_ERROR_INTERNAL;
    }
    if (rat_hour_mode == chinese_calendar::TAIYIN_GANZHI_RAT_HOUR_NO_SPLIT
        && jie_clock.hour >= 23) {
        jie_logical += 1.0 / 24.0;
    }
    const int64_t current_day = (current_logical + 0.5).day_number;
    const int64_t jie_day = (jie_logical + 0.5).day_number;
    const int64_t day = current_day - jie_day + 1;
    if (day < 1 || day > std::numeric_limits<uint16_t>::max()) {
        return TAIYIN_ERROR_INTERNAL;
    }
    *out = static_cast<uint16_t>(day);
    return TAIYIN_STATUS_OK;
}

}  // namespace detail
}  // namespace ziwei
}  // namespace taiyin
