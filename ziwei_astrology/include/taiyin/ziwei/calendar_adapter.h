#ifndef TAIYIN_ZIWEI_CALENDAR_ADAPTER_H
#define TAIYIN_ZIWEI_CALENDAR_ADAPTER_H

#include "taiyin/chinese_calendar/calendar.h"
#include "taiyin/chinese_calendar/ganzhi.h"
#include "taiyin/status.h"
#include "taiyin/ziwei/chart.h"

#include <cstdint>

namespace taiyin {
namespace ziwei {

struct BirthResolutionOptions {
    int32_t rat_hour_mode;
    LeapMonthStrategy leap_month_strategy;
    AnchorOptions anchor_options;
};

struct ResolvedBirth {
    CalendarFacts facts;
    Anchors anchors;
    Branch body_palace;
};

BirthResolutionOptions default_birth_resolution_options() noexcept;

// Resolves all astronomical/calendar inputs once, then hands the finite facts
// to the rule core. The caller owns the Chinese-calendar context and therefore
// retains control of historical/local calendar policy and ephemeris routing.
Status resolve_birth_from_calendar(
    const chinese_calendar::ChineseCalendarContext* calendar,
    const SplitJulianDate& instant_utc,
    const CalendarDateTime& virtual_time,
    Gender gender,
    const BirthResolutionOptions& options,
    ResolvedBirth* out,
    runtime::EphemerisEvalDiagnostic* diagnostic
) noexcept;

Status make_natal_chart_from_calendar(
    const chinese_calendar::ChineseCalendarContext* calendar,
    const SplitJulianDate& instant_utc,
    const CalendarDateTime& virtual_time,
    Gender gender,
    const BirthResolutionOptions& options,
    const CompiledRules& rules,
    NatalChart* out,
    runtime::EphemerisEvalDiagnostic* diagnostic
) noexcept;

}  // namespace ziwei
}  // namespace taiyin

#endif  // TAIYIN_ZIWEI_CALENDAR_ADAPTER_H
