#ifndef TAIYIN_ZIWEI_FLOW_CALENDAR_ADAPTER_H
#define TAIYIN_ZIWEI_FLOW_CALENDAR_ADAPTER_H

#include "taiyin/chinese_calendar/calendar.h"
#include "taiyin/status.h"
#include "taiyin/ziwei/calendar_adapter.h"
#include "taiyin/ziwei/limits.h"

#include <cstdint>

namespace taiyin {
namespace ziwei {

struct FlowResolutionOptions {
    // Lunar uses the resolved lunisolar year/month/day. SolarTerm uses the
    // Li-Chun/Jie year and month plus the day count from the preceding Jie.
    PillarBoundary boundary;
    int32_t rat_hour_mode;
    ChildhoodStrategy childhood_strategy;
};

struct ResolvedFlow {
    int32_t effective_birth_year;
    int32_t effective_target_year;
    uint8_t target_month;
    uint8_t target_month_sequence;
    // Physical month-building branch resolved from the calendar's Zhong-Qi
    // civil-day assignment.  This is distinct from month.limit.coordinate
    // .branch, which is the Liu-Nian Dou-Jun palace branch.
    Branch target_month_building_branch;
    uint8_t target_day;
    uint8_t target_hour_index;
    RatHourSegment target_rat_hour_segment;
    bool target_month_is_leap;

    DecadeLimit decade;
    SmallLimit small_limit;
    FlowYearLimit year;
    FlowMonthLimit month;
    FlowDayLimit day;
    FlowHourLimit hour;
};

FlowResolutionOptions default_flow_resolution_options() noexcept;

// Resolves one physical target instant into the complete five-level limit
// coordinate chain. Birth facts and natal must describe the same chart.
Status resolve_flow_from_calendar(
    const chinese_calendar::ChineseCalendarContext* calendar,
    const ResolvedBirth& birth,
    const NatalChart& natal,
    const SplitJulianDate& target_instant_utc,
    const CalendarDateTime& target_virtual_time,
    const FlowResolutionOptions& options,
    ResolvedFlow* out,
    runtime::EphemerisEvalDiagnostic* diagnostic
) noexcept;

// Atomically replaces chart.flow_stack with Decade->Year->Month->Day->Hour.
// On failure, the existing stack is left unchanged. Small limit is returned
// as parallel annual metadata in out_resolution rather than a sixth layer.
Status set_flow_stack_from_calendar(
    const chinese_calendar::ChineseCalendarContext* calendar,
    const ResolvedBirth& birth,
    const SplitJulianDate& target_instant_utc,
    const CalendarDateTime& target_virtual_time,
    const FlowResolutionOptions& options,
    const CompiledRules& rules,
    Chart* chart,
    ResolvedFlow* out_resolution,
    runtime::EphemerisEvalDiagnostic* diagnostic
) noexcept;

// Same atomic replacement, but only installs layers through deepest_level.
// The stack remains contiguous; removing Month necessarily removes Day/Hour.
Status set_flow_stack_through_from_calendar(
    const chinese_calendar::ChineseCalendarContext* calendar,
    const ResolvedBirth& birth,
    const SplitJulianDate& target_instant_utc,
    const CalendarDateTime& target_virtual_time,
    const FlowResolutionOptions& options,
    FlowLevel deepest_level,
    const CompiledRules& rules,
    Chart* chart,
    ResolvedFlow* out_resolution,
    runtime::EphemerisEvalDiagnostic* diagnostic
) noexcept;

// Moves a physical/virtual target to the canonical center of the adjacent
// logical hour. Split Rat-hour modes use 13 slots: Early Zi, Chou..Hai, Late
// Zi. Both output clocks describe the same shifted instant.
Status step_flow_hour_target(
    const SplitJulianDate& current_instant_utc,
    const CalendarDateTime& current_virtual_time,
    int32_t rat_hour_mode,
    int direction,
    SplitJulianDate* out_instant_utc,
    CalendarDateTime* out_virtual_time,
    RatHourSegment* out_rat_hour_segment
) noexcept;

// Moves by one local civil day while preserving the virtual wall-clock time.
Status step_flow_day_target(
    const SplitJulianDate& current_instant_utc,
    const CalendarDateTime& current_virtual_time,
    int direction,
    SplitJulianDate* out_instant_utc,
    CalendarDateTime* out_virtual_time
) noexcept;

}  // namespace ziwei
}  // namespace taiyin

#endif  // TAIYIN_ZIWEI_FLOW_CALENDAR_ADAPTER_H
