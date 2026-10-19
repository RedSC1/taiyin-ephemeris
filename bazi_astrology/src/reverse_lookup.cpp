#include "taiyin/bazi/reverse_lookup.h"
#include <cmath>
#include <new>
#include <utility>

namespace taiyin { namespace bazi {
namespace {
bool valid_pillar(uint8_t v) noexcept {
    const unsigned stem = v >> 4, branch = v & 15;
    return stem < 10 && branch < 12 && (stem % 2) == (branch % 2);
}
bool valid_clock(const chinese_calendar::ChartClock& clock) noexcept {
    using chinese_calendar::ChartClockMode;
    return clock.mode == ChartClockMode::FixedOffset
        || ((clock.mode == ChartClockMode::MeanSolar || clock.mode == ChartClockMode::ApparentSolar)
            && std::isfinite(clock.longitude_rad)
            && std::fabs(clock.longitude_rad) <= 3.14159265358979323846);
}
bool same(const chinese_calendar::GanzhiFourPillars& a,
    const chinese_calendar::GanzhiFourPillars& b) noexcept {
    return a.year == b.year && a.month == b.month && a.day == b.day && a.hour == b.hour;
}
}
Status reverse_lookup_at_ut1(const chinese_calendar::ChineseCalendarContext* calendar,
    const BaziReverseRequest& request, std::vector<BaziReverseCandidate>* out,
    runtime::EphemerisEvalDiagnostic* diagnostic) noexcept {
    using namespace chinese_calendar;
    if (!calendar || !out || !split_julian_date_is_finite(request.start_jd_ut1)
        || !split_julian_date_is_finite(request.end_jd_ut1)
        || request.end_jd_ut1 < request.start_jd_ut1
        || !valid_clock(request.clock)
        || request.rat_hour_mode < 0 || request.rat_hour_mode > 2
        || !valid_pillar(request.pillars.year) || !valid_pillar(request.pillars.month)
        || !valid_pillar(request.pillars.day) || !valid_pillar(request.pillars.hour))
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    // An empty half-open interval needs no clock/ephemeris evaluation, even
    // when the selected apparent clock has no coverage at this endpoint.
    if (request.start_jd_ut1 == request.end_jd_ut1) {
        out->clear();
        return TAIYIN_STATUS_OK;
    }
    try {
        std::vector<BaziReverseCandidate> result;
        CalendarDateTime first, last;
        Status s = chart_time_from_ut1(calendar, request.clock, request.start_jd_ut1, &first, diagnostic);
        if (s != TAIYIN_STATUS_OK) return s;
        s = chart_time_from_ut1(calendar, request.clock, request.end_jd_ut1, &last, diagnostic);
        if (s != TAIYIN_STATUS_OK) return s;
        SplitJulianDate first_clock, last_clock;
        if (!julian_day_split(first,&first_clock) || !julian_day_split(last,&last_clock))
            return TAIYIN_ERROR_INVALID_ARGUMENT;
        first.hour = last.hour = first.minute = last.minute = 0;
        first.second = last.second = 0.0;
        SplitJulianDate day, final_day;
        if (!julian_day_split(first, &day) || !julian_day_split(last, &final_day))
            return TAIYIN_ERROR_INVALID_ARGUMENT;
        const int branch = request.pillars.hour & 15;
        for (; day <= final_day; day += 1.0) {
            CalendarDateTime date, tomorrow;
            if (!reverse_julian_day_split(day, &date)
                || !reverse_julian_day_split(day + 1.0, &tomorrow)) return TAIYIN_ERROR_INVALID_ARGUMENT;
            uint8_t today_pillar, tomorrow_pillar;
            s = calculate_day_pillar(date, &today_pillar);
            if (s != TAIYIN_STATUS_OK) return s;
            s = calculate_day_pillar(tomorrow, &tomorrow_pillar);
            if (s != TAIYIN_STATUS_OK) return s;
            // Conservative prefilter only: the forward engine decides how
            // late Zi borrows the day stem in each school.
            if (request.pillars.day != today_pillar && request.pillars.day != tomorrow_pillar) continue;
            for (int part = 0; part < (branch == 0 ? 2 : 1); ++part) {
                const int hour = branch == 0 ? (part == 0 ? 0 : 23) : 2 * branch - 1;
                CalendarDateTime begin = date, end = date;
                begin.hour = hour;
                if (hour == 23) end = tomorrow;
                else end.hour = branch == 0 ? 1 : hour + 2;
                SplitJulianDate begin_clock, end_clock;
                if (!julian_day_split(begin,&begin_clock) || !julian_day_split(end,&end_clock))
                    return TAIYIN_ERROR_INVALID_ARGUMENT;
                if (end_clock <= first_clock || last_clock <= begin_clock) continue;
                SplitJulianDate a, b;
                // Clip in virtual coordinates first. The clock is monotonic;
                // never evaluate an inverse outside the requested range just
                // to discard it (important at data/solver coverage edges).
                if (begin_clock <= first_clock) a=request.start_jd_ut1;
                else {
                    s = chart_time_to_ut1(calendar, request.clock, begin, &a, diagnostic);
                    if (s != TAIYIN_STATUS_OK) return s;
                }
                if (last_clock <= end_clock) b=request.end_jd_ut1;
                else {
                    s = chart_time_to_ut1(calendar, request.clock, end, &b, diagnostic);
                    if (s != TAIYIN_STATUS_OK) return s;
                }
                if (a < request.start_jd_ut1) a=request.start_jd_ut1;
                if (request.end_jd_ut1 < b) b=request.end_jd_ut1;
                if (!(a < b)) continue;
                SolarTermEvent jie;
                SplitJulianDate next_jie;
                s = next_pillar_transition(calendar, a, &jie, &next_jie, diagnostic);
                if (s != TAIYIN_STATUS_OK) return s;
                while (a < b) {
                    const SplitJulianDate boundary = next_jie < b ? next_jie : b;
                    if (!(a < boundary)) return TAIYIN_ERROR_INTERNAL;
                    // Midpoint avoids rounding a computed boundary onto its
                    // other side. Endpoints remain the exact shared roots.
                    const SplitJulianDate probe = a + (boundary - a) / 2.0;
                    CalendarDateTime virtual_time;
                    s = chart_time_from_ut1(calendar, request.clock, probe, &virtual_time, diagnostic);
                    if (s != TAIYIN_STATUS_OK) return s;
                    GanzhiFourPillars actual;
                    s = calculate_four_pillars(calendar, probe, virtual_time,
                        request.rat_hour_mode, &actual, diagnostic);
                    if (s != TAIYIN_STATUS_OK) return s;
                    if (same(actual, request.pillars)) {
                        BaziReverseCandidate candidate;
                        candidate.start_jd_ut1 = a;
                        candidate.end_jd_ut1 = boundary;
                        candidate.pillars = actual;
                        s = chart_time_from_ut1(calendar, request.clock, a, &candidate.start_virtual_time, diagnostic);
                        if (s != TAIYIN_STATUS_OK) return s;
                        s = chart_time_from_ut1(calendar, request.clock, boundary, &candidate.end_virtual_time, diagnostic);
                        if (s != TAIYIN_STATUS_OK) return s;
                        if (!result.empty() && result.back().end_jd_ut1 == a) {
                            result.back().end_jd_ut1 = boundary;
                            result.back().end_virtual_time = candidate.end_virtual_time;
                        } else result.push_back(candidate);
                    }
                    a = boundary;
                    if (a < b) {
                        s = next_pillar_transition(calendar, a, &jie, &next_jie, diagnostic);
                        if (s != TAIYIN_STATUS_OK) return s;
                    }
                }
            }
        }
        *out = std::move(result);
        return TAIYIN_STATUS_OK;
    } catch (const std::bad_alloc&) { return TAIYIN_ERROR_OUT_OF_MEMORY; }
    catch (...) { return TAIYIN_ERROR_INTERNAL; }
}
} }
