#ifndef TAIYIN_BAZI_REVERSE_LOOKUP_H
#define TAIYIN_BAZI_REVERSE_LOOKUP_H
#include "taiyin/chinese_calendar/chart_clock.h"
#include "taiyin/chinese_calendar/ganzhi.h"
#include <vector>

namespace taiyin { namespace bazi {
struct BaziReverseRequest {
    SplitJulianDate start_jd_ut1;
    SplitJulianDate end_jd_ut1;
    chinese_calendar::GanzhiFourPillars pillars; // All four required; no wildcard.
    chinese_calendar::ChartClock clock;
    int32_t rat_hour_mode;
    BaziReverseRequest() noexcept : rat_hour_mode(0) {}
};
struct BaziReverseCandidate {
    SplitJulianDate start_jd_ut1;
    SplitJulianDate end_jd_ut1;
    CalendarDateTime start_virtual_time;
    CalendarDateTime end_virtual_time;
    chinese_calendar::GanzhiFourPillars pillars;
};

// Returns maximal matching half-open intervals within [start,end), ordered
// by UT1. Empty ranges/no matches succeed with an empty vector. Every match
// is verified by calculate_four_pillars; historical/astronomical Jie policy
// and all three rat-hour modes are inherited from the existing engine.
// Solar clocks are inverted at each boundary, never approximated by a fixed
// equation-of-time offset. Output is unchanged on failure. No global state.
Status reverse_lookup_at_ut1(const chinese_calendar::ChineseCalendarContext*,
    const BaziReverseRequest&, std::vector<BaziReverseCandidate>*,
    runtime::EphemerisEvalDiagnostic* = NULL) noexcept;
} }
#endif
