#ifndef TAIYIN_ZIWEI_REVERSE_LOOKUP_H
#define TAIYIN_ZIWEI_REVERSE_LOOKUP_H

#include "taiyin/ziwei/calendar_adapter.h"
#include "taiyin/ziwei/star_registry.h"

#include <cstdint>
#include <vector>

namespace taiyin {
namespace ziwei {

// Tier-1 reverse lookup is intentionally expressed in physical palace
// branches.  Set a field to kReverseUnspecified to omit that star from the
// filter.  The fields mirror the traditional key stars used by the legacy
// oracle, but matching is always verified by the current compiled tables.
constexpr int32_t kReverseUnspecified = -1;

struct Tier1ReverseQuery {
    int32_t lucun_branch;
    int32_t hongluan_branch;
    int32_t zuofu_branch;
    int32_t youbi_branch;
    int32_t wenchang_branch;
    int32_t wenqu_branch;
    int32_t santai_branch;
    int32_t bazuo_branch;
    int32_t ziwei_branch;

    Tier1ReverseQuery() noexcept;
};

struct ReverseLookupRequest {
    // start_virtual_time describes the same event as start_instant_utc.  The
    // library advances both together, so it never guesses a timezone or
    // silently treats a local clock as UTC.
    SplitJulianDate start_instant_utc;
    SplitJulianDate end_instant_utc;
    CalendarDateTime start_virtual_time;
    Gender gender;
    BirthResolutionOptions birth_options;
    Tier1ReverseQuery query;

    ReverseLookupRequest() noexcept;
};

struct ReverseLookupCandidate {
    SplitJulianDate instant_utc;
    CalendarDateTime virtual_time;
    LunarDateFacts lunar_date;
    uint8_t hour_branch;
    RatHourSegment rat_hour_segment;
};

// Enumerates the finite logical-hour candidates in [start, end] and accepts
// only charts whose requested key-star placements match.  A result represents
// a logical birth-time slot, not a fictitious minute-precise reconstruction.
// This deliberately uses resolve_birth_from_calendar()/make_natal_chart(),
// rather than a second reverse-calendar implementation.
Status reverse_lookup_tier1_from_calendar(
    const chinese_calendar::ChineseCalendarContext* calendar,
    const ReverseLookupRequest& request,
    const CompiledRules& rules,
    const StarRegistry& registry,
    std::vector<ReverseLookupCandidate>* out,
    runtime::EphemerisEvalDiagnostic* diagnostic
) noexcept;

}  // namespace ziwei
}  // namespace taiyin

#endif  // TAIYIN_ZIWEI_REVERSE_LOOKUP_H
