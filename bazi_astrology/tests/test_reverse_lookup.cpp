#include "taiyin/bazi/reverse_lookup.h"
#include "taiyin/runtime/runtime.h"
#include "taiyin/body_id.h"
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <iomanip>
#include <limits>

using namespace taiyin;
using namespace taiyin::chinese_calendar;
using namespace taiyin::bazi;
static void check(bool value, const char* text) {
    if (!value) { std::cerr << text << '\n'; std::exit(1); }
}
static SplitJulianDate jd(int year, int month, int day, int hour = 0, int minute = 0) {
    CalendarDateTime d = {year, month, day, hour, minute, 0.0};
    SplitJulianDate result;
    check(julian_day_split(d, &result), "encode date"); return result;
}
static bool same(const GanzhiFourPillars& a, const GanzhiFourPillars& b) {
    return a.year == b.year && a.month == b.month && a.day == b.day && a.hour == b.hour;
}
static GanzhiFourPillars forward(const ChineseCalendarContext& cal,
    const BaziReverseRequest& r, const SplitJulianDate& time) {
    CalendarDateTime v; GanzhiFourPillars p;
    check(chart_time_from_ut1(&cal, r.clock, time, &v) == 0, "forward clock");
    check(calculate_four_pillars(&cal, time, v, r.rat_hour_mode, &p, NULL) == 0, "forward pillars");
    return p;
}
static std::vector<BaziReverseCandidate> search(const ChineseCalendarContext& cal, const BaziReverseRequest& r) {
    std::vector<BaziReverseCandidate> out;
    const Status s = reverse_lookup_at_ut1(&cal, r, &out);
    if (s != 0) std::cerr << "reverse status=" << s << '\n';
    check(s == 0, "reverse succeeds");
    for (size_t i = 0; i < out.size(); ++i) {
        const auto& c = out[i];
        check(c.start_jd_ut1 < c.end_jd_ut1, "positive interval");
        check(r.start_jd_ut1 <= c.start_jd_ut1 && c.end_jd_ut1 <= r.end_jd_ut1, "range clipped");
        check(!i || out[i-1].end_jd_ut1 < c.start_jd_ut1, "sorted maximal disjoint intervals");
        // Independent root refinements have a sub-microsecond numerical floor;
        // do not place a micro-interval probe picoseconds from a computed edge.
        const bool tiny = c.end_jd_ut1-c.start_jd_ut1 < 1e-8;
        for (double f : {tiny ? 0.25 : 0.001, 0.5, tiny ? 0.75 : 0.999}) {
            const auto probe=c.start_jd_ut1 + (c.end_jd_ut1-c.start_jd_ut1)*f;
            const bool matches = same(forward(cal,r,probe),r.pillars);
            if (!matches)
                std::cerr << std::setprecision(17) << "mismatching interval "
                    << c.start_jd_ut1.day_number << "+" << c.start_jd_ut1.day_fraction
                    << " width=" << c.end_jd_ut1-c.start_jd_ut1 << " f=" << f << '\n';
            check(matches, "forward verifies match interiors");
        }
    }
    return out;
}
int main() {
    runtime::EphemerisRuntimeConfig config;
    config.load_packaged_data = true; // Also registers the built-in fallback.
    check(runtime::initialize_global_ephemeris_runtime(config), "runtime");
    ChineseCalendarContext cal;
    runtime::NativeCalcContext astronomy;
    check(runtime::native_context_set_geocentric_observer(&astronomy, TAIYIN_BODY_EARTH, TAIYIN_BODY_EARTH)==0,"observer");
    const auto calendar_config=china_standard_astronomical_config(480);
    check(initialize_context(&cal,&astronomy,&calendar_config)==0,"calendar");
    // The first apparent-time inverse can lie on either side of the requested
    // hour. Both cases must refine to the first representable matching instant.
    {
        ChartClock clock; clock.mode=ChartClockMode::ApparentSolar;
        clock.longitude_rad=1.0;
        for (const CalendarDateTime local : {
            CalendarDateTime{1900,3,13,17,0,0.0},
            CalendarDateTime{2003,3,13,11,0,0.0}}) {
            SplitJulianDate instant, target;
            check(julian_day_split(local,&target),"clock regression target");
            check(chart_time_to_ut1(&cal,clock,local,&instant)==0,"refine apparent inverse");
            auto previous=instant;
            previous.day_fraction=std::nextafter(instant.day_fraction,
                -std::numeric_limits<double>::infinity());
            for (const auto probe : {previous,instant}) {
                CalendarDateTime mapped; SplitJulianDate mapped_jd;
                check(chart_time_from_ut1(&cal,clock,probe,&mapped)==0
                    && julian_day_split(mapped,&mapped_jd),"verify apparent inverse bracket");
                check(probe==instant ? mapped_jd>=target : mapped_jd<target,
                    "apparent inverse is the first representable right-side boundary");
            }
        }
    }
    BaziReverseRequest r;
    r.start_jd_ut1 = jd(2003,3,12)-480.0/1440;
    r.end_jd_ut1 = jd(2003,3,15)-480.0/1440;
    for (int clock = 0; clock < 3; ++clock) {
        r.clock.mode = static_cast<ChartClockMode>(clock);
        r.clock.longitude_rad = 118.582 * 3.14159265358979323846 / 180;
        for (int rat = 0; rat < 3; ++rat) {
            r.rat_hour_mode = rat;
            for (int hour : {0, 1, 11, 22, 23}) for (int minute : {0,30}) {
                CalendarDateTime local = {2003,3,13,hour,minute,0.0};
                SplitJulianDate instant;
                check(chart_time_to_ut1(&cal, r.clock, local, &instant) == 0, "inverse clock");
                CalendarDateTime normalized;
                check(chart_time_from_ut1(&cal,r.clock,instant,&normalized)==0
                    && normalized.hour==hour,"exact chart hour belongs to new slot");
                r.pillars = forward(cal, r, instant);
                const auto out = search(cal,r);
                bool found = false;
                for (const auto& c : out) found |= c.start_jd_ut1 <= instant && instant < c.end_jd_ut1;
                check(found, "rat-hour/solar-clock roundtrip");
            }
        }
    }
    // No-split Zi is one maximal 23:00 -> 01:00 interval across midnight.
    r.clock = ChartClock(); r.rat_hour_mode = 0;
    r.pillars = forward(cal,r,jd(2003,3,13,23,30)-480.0/1440);
    auto out = search(cal,r);
    check(out.size()==1 && out[0].start_virtual_time.hour==23
        && out[0].end_virtual_time.hour==1, "merge unified Zi across midnight");
    r.start_jd_ut1=out[0].start_jd_ut1+0.01;
    r.end_jd_ut1=out[0].end_jd_ut1-0.01;
    out=search(cal,r);
    check(out.size()==1 && out[0].start_jd_ut1==r.start_jd_ut1
        && out[0].end_jd_ut1==r.end_jd_ut1, "partial first/last hour");
    r.end_jd_ut1=r.start_jd_ut1;
    check(search(cal,r).empty(), "empty range");
    {
        BaziReverseRequest empty = r;
        empty.clock.mode = ChartClockMode::ApparentSolar;
        empty.start_jd_ut1 = empty.end_jd_ut1 = jd(100000,1,1);
        CalendarDateTime unused;
        check(chart_time_from_ut1(&cal,empty.clock,empty.start_jd_ut1,&unused)!=0,
            "empty-range regression endpoint really has no solar-clock coverage");
        std::vector<BaziReverseCandidate> existing(1);
        check(reverse_lookup_at_ut1(&cal,empty,&existing)==0 && existing.empty(),
            "unmappable empty range succeeds and replaces previous output");
        existing.resize(1);
        empty.clock.mode=static_cast<ChartClockMode>(99);
        check(reverse_lookup_at_ut1(&cal,empty,&existing)==TAIYIN_ERROR_INVALID_ARGUMENT
            && existing.size()==1,"empty range still validates clock before clearing output");
        empty.clock.mode=ChartClockMode::ApparentSolar;
        empty.clock.longitude_rad=4;
        check(reverse_lookup_at_ut1(&cal,empty,&existing)==TAIYIN_ERROR_INVALID_ARGUMENT,
            "empty range rejects invalid solar longitude");
    }
    r.end_jd_ut1=r.start_jd_ut1+0.0001;
    check(advance_ganzhi(r.pillars.month,1,&r.pillars.month)==0,"alter query month");
    check(search(cal,r).empty(),"no match succeeds");
    // Lichun is physical, not shifted by the chart's solar clock.
    for (int historical = 0; historical < 2; ++historical) {
        cal.config = historical ? historical_china_config() : china_standard_astronomical_config(480);
        const auto anchor=historical ? jd(23,1,10) : jd(2026,2,1);
        SolarTermEvent term; SplitJulianDate boundary;
        check(next_pillar_transition(&cal,anchor,&term,&boundary,NULL)==0,"find shared Jie transition");
        r.start_jd_ut1=boundary-0.01; r.end_jd_ut1=boundary+0.01;
        r.clock.mode=ChartClockMode::ApparentSolar;
        r.clock.longitude_rad=1.0;
        for (double side : {-0.001, 0.001}) {
            r.pillars=forward(cal,r,boundary+side);
            out=search(cal,r);
            check(out.size()==1,"Jie splits match");
            const auto edge=side<0 ? out[0].end_jd_ut1 : out[0].start_jd_ut1;
            check(std::fabs(edge-boundary)<1e-10,"uses exact effective Jie boundary");
        }
    }
    // Assigned historical Li Chun must switch year and month together at its
    // exact civil midnight, without an astronomical-root equality window.
    cal.config=historical_china_config();
    r.clock.mode=ChartClockMode::MeanSolar; r.clock.longitude_rad=1.0;
    r.rat_hour_mode=0;
    {
        // In 500, Mangzhong was assigned more than one day before its
        // astronomical root. A one-day lookahead misses the actual month start.
        SolarTermEvent term; SplitJulianDate boundary;
        check(getSpecificJieQi(&cal,500,5,&term,NULL)==0
            && term.index_from_winter_solstice==11,"historical early Jie fixture");
        check(pillar_term_boundary(&cal,term,&boundary)==0,"assigned early Jie boundary");
        check(term.jd_ut-boundary>1.0,"assigned Jie precedes root by more than one day");
        const auto before=forward(cal,r,boundary-0.001);
        const auto after=forward(cal,r,boundary+0.001);
        check(before.month!=after.month,"forward month changes at assigned historical midnight");
        for (const auto p : {before,after}) {
            r.start_jd_ut1=boundary-0.01; r.end_jd_ut1=boundary+0.01;
            r.pillars=p;
            const auto matches=search(cal,r);
            check(matches.size()==1 && (p.month==before.month
                ? matches[0].end_jd_ut1==boundary : matches[0].start_jd_ut1==boundary),
                "reverse splits at assigned historical midnight, not root minus one day");
        }
        for (double days : {0.0,0.5,1.0,2.5,3.5,4.0}) {
            SolarTermEvent previous; SplitJulianDate previous_boundary;
            check(previous_pillar_jie(&cal,boundary+days,&previous,&previous_boundary,NULL)==0
                && previous.index_from_winter_solstice==11 && previous_boundary==boundary,
                "historical assigned month remains selected until and beyond physical root");
        }
        // Check both early and late assigned days over the whole historical
        // year, including the January/December neighbours of the anchor chain.
        bool early=false, late=false;
        for (uint8_t index=1; index<24; index+=2) {
            SolarTermEvent event, previous, next; SplitJulianDate edge, found;
            check(getSpecificJieQi(&cal,500,index,&event,NULL)==0
                && pillar_term_boundary(&cal,event,&edge)==0,"historical Jie calendar fixture");
            early |= edge<event.jd_ut; late |= edge>event.jd_ut;
            check(previous_pillar_jie(&cal,edge-0.001,&previous,&found,NULL)==0
                && previous.index_from_winter_solstice!=event.index_from_winter_solstice,
                "historical month does not start before assigned midnight");
            check(previous_pillar_jie(&cal,edge,&previous,&found,NULL)==0
                && found==edge && previous.index_from_winter_solstice==event.index_from_winter_solstice,
                "historical previous Jie switches exactly at assigned midnight");
            check(next_pillar_transition(&cal,edge-0.001,&next,&found,NULL)==0 && found==edge,
                "historical next transition agrees with forward month");
        }
        check(early && late,"historical fixtures exercise assignments on both sides of roots");
    }
    for (int year : {23, 100}) {
        SolarTermEvent term; SplitJulianDate boundary;
        check(getSpecificJieQi(&cal,year,21,&term,NULL)==0,"historical Li Chun");
        check(pillar_term_boundary(&cal,term,&boundary)==0 && boundary!=term.jd_ut,
            "Li Chun fixture uses an assigned historical day");
        const double epsilon=1.0e-10;
        const auto before=forward(cal,r,boundary-4*epsilon);
        const auto after=forward(cal,r,boundary+4*epsilon);
        check(before.year!=after.year && before.month!=after.month,"historical year/month transition");
        for (double side : {-0.75, -0.25, 0.0, 0.25, 0.75}) {
            const auto actual=forward(cal,r,boundary+side*epsilon);
            const auto expected=side<0 ? before : after;
            check(actual.year==expected.year && actual.month==expected.month,
                "historical year and month stay synchronized on both sides of midnight");
        }
        for (double direction : {-std::numeric_limits<double>::infinity(),
            std::numeric_limits<double>::infinity()}) {
            auto adjacent=boundary;
            adjacent.day_fraction=std::nextafter(boundary.day_fraction,direction);
            const auto actual=forward(cal,r,adjacent);
            const auto expected=direction<0 ? before : after;
            check(actual.year==expected.year && actual.month==expected.month,
                "historical boundary is strict at adjacent representable Split-JD values");
        }
        // Mean-solar clock deliberately keeps the virtual hour away from
        // midnight; this isolates the physical assigned-Jie boundary.
        r.start_jd_ut1=boundary-2*epsilon; r.end_jd_ut1=boundary+2*epsilon;
        for (const auto p : {before, after}) {
            r.pillars=p;
            out=search(cal,r);
            check(out.size()==1,"historical microsecond interval is found");
            check(p.year==before.year ? out[0].end_jd_ut1==boundary
                : out[0].start_jd_ut1==boundary,"historical interval ends/starts at exact midnight");
        }
        r.start_jd_ut1=boundary-0.75*epsilon; r.end_jd_ut1=boundary-0.25*epsilon;
        r.pillars=before;
        out=search(cal,r);
        check(out.size()==1 && out[0].start_jd_ut1==r.start_jd_ut1
            && out[0].end_jd_ut1==r.end_jd_ut1,"pre-midnight historical range is retained whole");
    }
    // A precise Jie switches forward pillars inside the numerical equality
    // window, not at the raw event root. Exercise ranges only microseconds
    // wide as well as a longer interval whose end used to include wrong pillars.
    cal.config=china_standard_astronomical_config(480);
    r.clock=ChartClock(); r.rat_hour_mode=0;
    for (const auto anchor : {jd(2026,2,1), jd(2026,9,1)}) {
        SolarTermEvent term; SplitJulianDate transition;
        check(next_pillar_transition(&cal,anchor,&term,&transition,NULL)==0,"find equality-window Jie");
        const auto root=term.jd_ut;
        const double tolerance=1.0e-10;
        check(std::fabs((root-transition)-tolerance)<1e-14,
            "next transition includes solver equality window, event stays unmodified");
        const auto before=forward(cal,r,root-3*tolerance);
        const auto after=forward(cal,r,root+3*tolerance);
        check(!same(before,after),"Jie fixture changes pillars");
        for (const double width : {1.5*tolerance, 0.001}) {
            r.start_jd_ut1=root-width; r.end_jd_ut1=root+0.5*tolerance;
            r.pillars=before;
            out=search(cal,r);
            check(out.size()==1 && std::fabs(out[0].end_jd_ut1-transition)<tolerance/4,
                "pre-Jie match ends at forward transition, even in a tiny search");
            check(same(forward(cal,r,out[0].end_jd_ut1-tolerance/3),before),
                "last pre-Jie microseconds still match forward pillars");
            r.pillars=after;
            out=search(cal,r);
            check(out.size()==1 && std::fabs(out[0].start_jd_ut1-transition)<tolerance/4,
                "post-Jie match starts inside equality window");
            check(same(forward(cal,r,out[0].start_jd_ut1+tolerance/3),after),
                "first post-Jie microseconds match forward pillars");
        }
    }
    r.pillars.hour=0xff;
    std::vector<BaziReverseCandidate> sentinel(1);
    check(reverse_lookup_at_ut1(&cal,r,&sentinel)==TAIYIN_ERROR_INVALID_ARGUMENT && sentinel.size()==1,"invalid pillar leaves output intact");
    r.pillars=forward(cal,r,r.start_jd_ut1);
    r.pillars.hour=0x01;
    check(reverse_lookup_at_ut1(&cal,r,&sentinel)==TAIYIN_ERROR_INVALID_ARGUMENT,"parity invalid");
    r.pillars=forward(cal,r,r.start_jd_ut1);r.clock.mode=static_cast<ChartClockMode>(99);
    check(reverse_lookup_at_ut1(&cal,r,&sentinel)==TAIYIN_ERROR_INVALID_ARGUMENT,"invalid clock");
    r.clock.mode=ChartClockMode::ApparentSolar;r.clock.longitude_rad=4;
    check(reverse_lookup_at_ut1(&cal,r,&sentinel)==TAIYIN_ERROR_INVALID_ARGUMENT,"invalid longitude");
    std::cout << "BaZi reverse clocks/rat hours/Jie/history/range validation passed\n";
}
