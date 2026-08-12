#include "taiyin/body_id.h"
#include "taiyin/chinese_calendar/calendar.h"
#include "taiyin/dispatch.h"
#include "taiyin/angle.h"
#include "taiyin/runtime/ephemeris_route.h"
#include "taiyin/runtime/event_search.h"
#include "taiyin/runtime/native_context.h"
#include "taiyin/runtime/native_position.h"
#include "taiyin/runtime/runtime.h"
#include "taiyin/status.h"
#include "chinese_calendar/historical_calendar_data.h"

#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>

namespace {

int failures = 0;

void expect(bool condition, const char* label) {
    if (!condition) {
        std::cerr << "FAIL: " << label << "\n";
        ++failures;
    }
}

void expect_status(taiyin::Status status, const char* label) {
    if (status != taiyin::TAIYIN_STATUS_OK) {
        std::cerr << "FAIL: " << label << ": "
                  << taiyin::status_name(status) << "\n";
        ++failures;
    }
}

void expect_time_within_seconds(
    taiyin::SplitJulianDate actual,
    taiyin::SplitJulianDate expected,
    double tolerance_seconds,
    const char* label
) {
    const double difference_seconds = std::fabs(actual - expected) * 86400.0;
    if (difference_seconds > tolerance_seconds) {
        std::cerr << "FAIL: " << label << ": difference="
                  << difference_seconds << " seconds\n";
        ++failures;
    }
}

taiyin::SplitJulianDate split_jd(
    const taiyin::CalendarDateTime& date,
    const char* label
) {
    taiyin::SplitJulianDate out;
    expect(taiyin::julian_day_split(date, &out), label);
    return out;
}

taiyin::SplitJulianDate beijing_jd_ut(
    int32_t year,
    int32_t month,
    int32_t day,
    int32_t hour,
    int32_t minute,
    const char* label
) {
    const taiyin::CalendarDateTime beijing = {
        year, month, day, hour, minute, 0.0,
    };
    return split_jd(beijing, label) - 8.0 / 24.0;
}

void test_historical_table_integrity() {
    using namespace taiyin::chinese_calendar::internal;
    expect(kHistoricalCivilDayScale == INT64_C(1000000000),
           "historical profile fixed-point scale");
    expect(kHistoricalCivilOffsetMinutes == 480,
           "historical profile UTC+08 offset");
    expect(kHistoricalProfileEndJd == INT64_C(2436935),
           "historical profile cutoff");
    expect(kHistoricalNewMoonEventCount == 33161u,
           "historical new-moon profile event count");
    expect(kHistoricalNewMoonFirstCivilDay == INT64_C(1457698)
               && kHistoricalNewMoonLastCivilDay == INT64_C(2436933),
           "historical new-moon profile civil-day bounds");
    expect(kHistoricalSolarTermEventCount == 52324u,
           "historical solar-term profile event count");
    expect(kHistoricalSolarTermFirstCivilDay == INT64_C(1640650)
               && kHistoricalSolarTermLastCivilDay == INT64_C(2436925),
           "historical solar-term profile civil-day bounds");
    expect(kHistoricalNewMoonExactSegments.back().first_event_index
               + kHistoricalNewMoonExactSegments.back().event_count
               == kHistoricalNewMoonTail.first_event_index,
           "historical new-moon exact prefix joins tail");
    expect(kHistoricalNewMoonTail.first_event_index
               + kHistoricalNewMoonTail.event_count
               == kHistoricalNewMoonEventCount,
           "historical new-moon tail covers profile end");
    expect(kHistoricalSolarTermExactSegments.back().first_event_index
               + kHistoricalSolarTermExactSegments.back().event_count
               == kHistoricalSolarTermTail.first_event_index,
           "historical solar-term exact prefix joins tail");
    expect(kHistoricalSolarTermTail.first_event_index
               + kHistoricalSolarTermTail.event_count
               == kHistoricalSolarTermEventCount,
           "historical solar-term tail covers profile end");
    expect(kHistoricalNewMoonResidualRank.back() == 4603u,
           "historical new-moon sparse residual count");
    expect(kHistoricalSolarTermResidualRank.back() == 361u,
           "historical solar-term sparse residual count");
    expect(std::string(kHistoricalProfileSha256)
               == "61f195de0c39d86083ee85e090ea5d955e0ed81759d8f24d15cb9f4029975529",
           "historical profile fingerprint");
}

std::string data_root() {
    const char* explicit_root =
        std::getenv("TAIYIN_CHINESE_CALENDAR_DATA_ROOT");
    if (explicit_root && explicit_root[0] != '\0') {
        return explicit_root;
    }
    const char* root = std::getenv("TAIYIN_REPO_ROOT");
    if (root && root[0] != '\0') {
        return std::string(root) + "/data/ephemerides/opm2/major-bodies/600y";
    }
    return "../data/ephemerides/opm2/major-bodies/600y";
}

bool long_range_tests_enabled() {
    const char* value =
        std::getenv("TAIYIN_CHINESE_CALENDAR_LONG_RANGE");
    return value && std::string(value) == "1";
}

bool initialize_runtime() {
    const std::string root = data_root();
    const char* paths[] = {root.c_str()};
    taiyin::runtime::EphemerisRuntimeConfig config;
    config.source_paths = paths;
    config.source_path_count = 1;
    config.load_packaged_data = true;
    return taiyin::runtime::initialize_global_ephemeris_runtime(config);
}

taiyin::chinese_calendar::ChineseCalendarContext make_context(
    const taiyin::chinese_calendar::ChineseCalendarConfig& config,
    uint64_t route_rule_id = taiyin::runtime::TAIYIN_EPHEMERIS_ROUTE_AUTO
) {
    taiyin::runtime::NativeCalcContext astronomy;
    taiyin::runtime::native_context_set_geocentric_observer(
        &astronomy,
        taiyin::TAIYIN_BODY_EARTH,
        taiyin::TAIYIN_BODY_EARTH);
    expect_status(
        taiyin::runtime::native_context_set_route_rule(
            &astronomy, route_rule_id),
        "set Chinese calendar ephemeris route");
    taiyin::chinese_calendar::ChineseCalendarContext context;
    expect_status(
        taiyin::chinese_calendar::initialize_context(
            &context, &astronomy, &config),
        "initialize Chinese calendar context");
    return context;
}

void expect_roundtrip(
    const taiyin::chinese_calendar::ChineseCalendarContext& context,
    int32_t year,
    uint8_t month,
    uint8_t day,
    const char* label
) {
    using namespace taiyin::chinese_calendar;
    SolarDate solar;
    solar.year = year;
    solar.month = month;
    solar.day = day;
    LunarDate lunar;
    taiyin::runtime::EphemerisEvalDiagnostic diagnostic;
    expect_status(fromSolar(&context, &solar, &lunar, &diagnostic), label);
    SolarDate roundtrip;
    expect_status(fromLunar(&context, &lunar, &roundtrip, &diagnostic), label);
    if (roundtrip.year != solar.year
        || roundtrip.month != solar.month
        || roundtrip.day != solar.day) {
        std::cerr << "  " << label << ": "
                  << solar.year << "-" << static_cast<int>(solar.month)
                  << "-" << static_cast<int>(solar.day) << " -> lunar "
                  << lunar.year << "-" << static_cast<int>(lunar.month)
                  << "-" << static_cast<int>(lunar.day)
                  << (lunar.is_leap ? " leap" : "")
                  << " -> " << roundtrip.year << "-"
                  << static_cast<int>(roundtrip.month) << "-"
                  << static_cast<int>(roundtrip.day) << "\n";
    }
    expect(
        roundtrip.year == solar.year
            && roundtrip.month == solar.month
            && roundtrip.day == solar.day,
        label);
}

void expect_lunar(
    const taiyin::chinese_calendar::ChineseCalendarContext& context,
    int32_t solar_year,
    uint8_t solar_month,
    uint8_t solar_day,
    int32_t lunar_year,
    uint8_t lunar_month,
    uint8_t lunar_day,
    bool is_leap,
    uint8_t month_days,
    const char* label,
    uint8_t month_name =
        taiyin::chinese_calendar::TAIYIN_CHINESE_MONTH_NAME_NORMAL
) {
    using namespace taiyin::chinese_calendar;
    SolarDate solar;
    solar.year = solar_year;
    solar.month = solar_month;
    solar.day = solar_day;
    LunarDate lunar;
    taiyin::runtime::EphemerisEvalDiagnostic diagnostic;
    expect_status(fromSolar(&context, &solar, &lunar, &diagnostic), label);
    expect(
        lunar.year == lunar_year
            && lunar.month == lunar_month
            && lunar.day == lunar_day
            && lunar.is_leap == (is_leap ? 1u : 0u)
            && lunar.month_days == month_days
            && lunar.month_name == month_name,
        label);
}

void test_modern_roundtrip() {
    using namespace taiyin::chinese_calendar;
    const ChineseCalendarContext context =
        make_context(fixed_utc_offset_config(8 * 60));
    const ChineseCalendarContext copied_context = context;
    expect(
        copied_context.astronomy.apparent_options.model_context
            == &copied_context.astronomy.model_context,
        "copied Chinese calendar context repairs internal model pointer");
    const SolarDate solar = [] {
        SolarDate value;
        value.year = 2025;
        value.month = 1;
        value.day = 29;
        return value;
    }();
    LunarDate lunar;
    taiyin::runtime::EphemerisEvalDiagnostic diagnostic;
    const taiyin::CalendarDateTime ancient_probe = {
        -456, 4, 4, 12, 0, 0.0,
    };
    ChineseCalendarYear ancient_year;
    expect_status(
        calcY(
            &context,
            split_jd(ancient_probe, "ancient probe split JD") - 8.0 / 24.0,
            &ancient_year,
            &diagnostic),
        "ancient historical calcY");
    expect(
        ancient_year.month_count == TAIYIN_CHINESE_CALENDAR_MONTH_COUNT,
        "ancient historical calcY reports its month records");
    expect_status(
        fromSolar(&copied_context, &solar, &lunar, &diagnostic),
        "2025 Chinese New Year fromSolar");
    expect(
        lunar.year == 2025 && lunar.month == 1
            && lunar.day == 1 && lunar.is_leap == 0,
        "2025-01-29 is lunar 2025-01-01");

    SolarDate roundtrip;
    expect_status(
        fromLunar(&copied_context, &lunar, &roundtrip, &diagnostic),
        "2025 Chinese New Year fromLunar");
    expect(
        roundtrip.year == solar.year
            && roundtrip.month == solar.month
            && roundtrip.day == solar.day,
        "modern solar/lunar roundtrip");
    expect_lunar(
        context,
        2026, 3, 15,
        2026, 1, 27, false, 30,
        "2026 modern Dart fixture");
}

void test_2033_leap_month() {
    using namespace taiyin::chinese_calendar;
    const ChineseCalendarContext context =
        make_context(fixed_utc_offset_config(8 * 60));
    const taiyin::CalendarDateTime date = {
        2034, 1, 15, 12, 0, 0.0,
    };
    ChineseCalendarYear year;
    taiyin::runtime::EphemerisEvalDiagnostic diagnostic;
    expect_status(
        calcY(
            &context,
            split_jd(date, "2033 probe split JD"),
            &year,
            &diagnostic),
        "2033 leap-eleven calcY");
    expect(year.leap_month_index == 1, "2033 leap month index is one");
    expect(
        year.months[0].month == 11 && year.months[0].is_leap == 0,
        "2033 first winter-solstice month is eleven");
    expect(
        year.months[1].month == 11 && year.months[1].is_leap == 1,
        "2033 second winter-solstice month is leap eleven");
    expect_lunar(
        context,
        2033, 12, 22,
        2033, 11, 1, true, 29,
        "2033 leap-eleven Dart fixture");
    expect_lunar(
        context,
        2034, 1, 20,
        2033, 12, 1, false, 30,
        "2033 twelfth-month Dart fixture");
    expect_roundtrip(context, 2033, 12, 22, "2033 boundary roundtrip");
    expect_roundtrip(context, 2034, 1, 20, "2034 boundary roundtrip");
}

void test_day_boundary_changes_day_not_event() {
    using namespace taiyin::chinese_calendar;
    const ChineseCalendarContext china =
        make_context(fixed_utc_offset_config(8 * 60));
    const ChineseCalendarContext vietnam =
        make_context(fixed_utc_offset_config(7 * 60));
    const ChineseCalendarContext vietnam_mean_solar =
        make_context(fixed_meridian_config(105.0));
    const taiyin::CalendarDateTime date = {
        1800, 6, 1, 12, 0, 0.0,
    };
    ChineseCalendarYear china_year;
    ChineseCalendarYear vietnam_year;
    ChineseCalendarYear vietnam_mean_solar_year;
    taiyin::runtime::EphemerisEvalDiagnostic diagnostic;
    const taiyin::SplitJulianDate probe_jd =
        split_jd(date, "calendar meridian probe split JD");
    expect_status(
        calcY(&china, probe_jd, &china_year, &diagnostic),
        "UTC+8 calendar year");
    expect_status(
        calcY(&vietnam, probe_jd, &vietnam_year, &diagnostic),
        "UTC+7 calendar year");
    expect_status(
        calcY(
            &vietnam_mean_solar,
            probe_jd,
            &vietnam_mean_solar_year,
            &diagnostic),
        "105E mean-solar calendar year");
    expect(
        std::fabs(
            china_year.new_moons[6].jd_ut
            - vietnam_year.new_moons[6].jd_ut) < 1.0e-12,
        "day-boundary policy does not change geocentric new moon instant");
    expect(
        china_year.new_moons[6].civil_day_number == 2378640
            && vietnam_year.new_moons[6].civil_day_number == 2378639,
        "1800-05 new moon belongs to different UTC+8 and UTC+7 civil days");
    expect(
        vietnam_year.new_moons[6].civil_day_number
            == vietnam_mean_solar_year.new_moons[6].civil_day_number,
        "UTC+7 and 105E mean-solar boundaries agree");
}

void test_calendar_config_semantics() {
    using namespace taiyin::chinese_calendar;
    const ChineseCalendarConfig historical = historical_china_config();
    expect(
        historical.rule_mode == TAIYIN_CHINESE_CALENDAR_HISTORICAL_CHINA
            && historical.day_boundary_mode
                == TAIYIN_CHINESE_CALENDAR_FIXED_UTC_OFFSET
            && historical.utc_offset_minutes == 8 * 60,
        "historical China defaults to fixed UTC+8");

    const ChineseCalendarConfig modern = fixed_utc_offset_config(7 * 60);
    expect(
        modern.rule_mode == TAIYIN_CHINESE_CALENDAR_ASTRONOMICAL
            && modern.day_boundary_mode
                == TAIYIN_CHINESE_CALENDAR_FIXED_UTC_OFFSET
            && modern.utc_offset_minutes == 7 * 60,
        "astronomical calendar accepts an explicit fixed UTC offset");

    taiyin::runtime::NativeCalcContext astronomy;
    ChineseCalendarConfig invalid_historical = historical;
    invalid_historical.utc_offset_minutes = 7 * 60;
    ChineseCalendarContext context;
    expect(
        initialize_context(
            &context, &astronomy, &invalid_historical)
            == taiyin::TAIYIN_ERROR_INVALID_ARGUMENT,
        "historical correction tables reject non-UTC+8 day boundaries");
}

void test_single_solar_term_queries() {
    using namespace taiyin::chinese_calendar;
    const ChineseCalendarContext context =
        make_context(fixed_utc_offset_config(8 * 60));
    taiyin::runtime::EphemerisEvalDiagnostic diagnostic;
    const taiyin::SplitJulianDate march_probe = beijing_jd_ut(
        2025, 3, 1, 12, 0, "2025 solar-term query probe");
    SolarTermEvent prev_term;
    SolarTermEvent next_term;
    SolarTermEvent prev_jie;
    SolarTermEvent next_jie;
    SolarTermEvent prev_qi;
    SolarTermEvent next_qi;
    expect_status(
        getPrevJieQi(&context, march_probe, &prev_term, &diagnostic),
        "previous solar term query");
    expect_status(
        getNextJieQi(&context, march_probe, &next_term, &diagnostic),
        "next solar term query");
    expect_status(
        getPrevJie(&context, march_probe, &prev_jie, &diagnostic),
        "previous jie query");
    expect_status(
        getNextJie(&context, march_probe, &next_jie, &diagnostic),
        "next jie query");
    expect_status(
        getPrevQi(&context, march_probe, &prev_qi, &diagnostic),
        "previous qi query");
    expect_status(
        getNextQi(&context, march_probe, &next_qi, &diagnostic),
        "next qi query");
    expect(
        prev_term.index_from_winter_solstice == 4
            && next_term.index_from_winter_solstice == 5,
        "2025-03-01 lies between Yushui and Jingzhe");
    expect(
        prev_jie.index_from_winter_solstice == 3
            && next_jie.index_from_winter_solstice == 5,
        "2025-03-01 jie filtering returns Lichun and Jingzhe");
    expect(
        prev_qi.index_from_winter_solstice == 4
            && next_qi.index_from_winter_solstice == 6,
        "2025-03-01 qi filtering returns Yushui and Chunfen");

    SolarTermEvent exact_prev;
    SolarTermEvent exact_next;
    SolarTermEvent exact_prev_jie;
    SolarTermEvent exact_next_jie;
    SolarTermEvent exact_prev_qi;
    SolarTermEvent exact_next_qi;
    expect_status(
        getPrevJieQi(&context, prev_jie.jd_ut, &exact_prev, &diagnostic),
        "exact solar-term previous boundary");
    expect_status(
        getNextJieQi(&context, prev_jie.jd_ut, &exact_next, &diagnostic),
        "exact solar-term next boundary");
    expect_status(
        getPrevJie(&context, prev_jie.jd_ut, &exact_prev_jie, &diagnostic),
        "exact jie previous boundary");
    expect_status(
        getNextJie(&context, prev_jie.jd_ut, &exact_next_jie, &diagnostic),
        "exact jie next boundary");
    expect_status(
        getPrevQi(&context, prev_jie.jd_ut, &exact_prev_qi, &diagnostic),
        "exact qi previous boundary");
    expect_status(
        getNextQi(&context, prev_jie.jd_ut, &exact_next_qi, &diagnostic),
        "exact qi next boundary");
    expect(
        exact_prev.index_from_winter_solstice
                == prev_jie.index_from_winter_solstice
            && std::fabs(exact_prev.jd_ut - prev_jie.jd_ut) < 1.0e-9,
        "Prev includes an exact solar-term boundary");
    expect(
        exact_next.index_from_winter_solstice == 4,
        "Next excludes an exact solar-term boundary");
    expect(
        exact_prev_jie.index_from_winter_solstice == 3
            && exact_next_jie.index_from_winter_solstice == 5,
        "Jie boundary filtering keeps current then skips to next jie");
    expect(
        exact_prev_qi.index_from_winter_solstice == 2
            && exact_next_qi.index_from_winter_solstice == 4,
        "Qi boundary filtering straddles an exact jie boundary");

    const taiyin::SplitJulianDate j2000_probe = beijing_jd_ut(
        2000, 1, 1, 0, 0, "J2000 solar-term query probe");
    expect_status(
        getPrevJieQi(&context, j2000_probe, &prev_term, &diagnostic),
        "J2000 previous solar term query");
    expect_status(
        getNextJieQi(&context, j2000_probe, &next_term, &diagnostic),
        "J2000 next solar term query");
    expect(
        prev_term.index_from_winter_solstice == 0
            && next_term.index_from_winter_solstice == 1,
        "J2000 lies between Dongzhi and Xiaohan");

    const taiyin::SplitJulianDate ancient_probe = beijing_jd_ut(
        -1999, 6, 1, 12, 0, "BCE solar-term query probe");
    expect_status(
        getPrevJieQi(&context, ancient_probe, &prev_term, &diagnostic),
        "BCE previous solar term query");
    expect_status(
        getNextJieQi(&context, ancient_probe, &next_term, &diagnostic),
        "BCE next solar term query");
    expect(
        prev_term.jd_ut <= ancient_probe && next_term.jd_ut > ancient_probe,
        "BCE solar-term queries preserve strict Prev/Next ordering");

    SolarTermEvent spring_equinox;
    SolarTermEvent lichun;
    SolarTermEvent winter_solstice;
    expect_status(
        getSpecificJieQi(&context, 2025, 0, &spring_equinox, &diagnostic),
        "2025 spring equinox direct query");
    expect_status(
        getSpecificJieQi(&context, 2025, 21, &lichun, &diagnostic),
        "2025 cycle Lichun direct query");
    expect_status(
        getSpecificJieQi(&context, 2025, 18, &winter_solstice, &diagnostic),
        "2025 winter solstice direct query");
    expect(
        spring_equinox.index_from_winter_solstice == 6
            && lichun.index_from_winter_solstice == 3
            && winter_solstice.index_from_winter_solstice == 0,
        "direct solar-term query maps vernal-equinox indices correctly");
    const taiyin::SplitJulianDate lichun_2025_probe = beijing_jd_ut(
        2025, 2, 4, 0, 0, "2025 Lichun direct query probe");
    expect(
        lichun.jd_ut > lichun_2025_probe - 2.0
            && lichun.jd_ut < lichun_2025_probe + 2.0,
        "spring-cycle direct index maps Lichun to the same Gregorian year");
    expect(
        getSpecificJieQi(&context, 2025, 24, &winter_solstice, &diagnostic)
            == taiyin::TAIYIN_ERROR_INVALID_ARGUMENT,
        "direct solar-term query rejects an out-of-range index");

    // 2044's equinox occurs before 00:00 UTC on March 20. This guards the
    // direct API against a stale civil-date seed skipping a whole year.
    const taiyin::SplitJulianDate mid_2044 = beijing_jd_ut(
        2044, 6, 1, 12, 0, "2044 direct solar-term oracle probe");
    ChineseCalendarYear year_2044;
    expect_status(
        calcY(&context, mid_2044, &year_2044, &diagnostic),
        "2044 calcY solar-term oracle");
    expect(
        year_2044.solar_terms[24].index_from_winter_solstice == 24,
        "calcY preserves index 24 for its next-winter-solstice endpoint");
    for (uint8_t index = 0; index < 24; ++index) {
        SolarTermEvent direct;
        expect_status(
            getSpecificJieQi(&context, 2044, index, &direct, &diagnostic),
            "2044 direct solar-term query");
        const SolarTermEvent& expected = index <= 18
            ? year_2044.solar_terms[static_cast<std::size_t>(index) + 6u]
            : year_2044.solar_terms[static_cast<std::size_t>(index) - 18u];
        expect(
            std::fabs(direct.jd_ut - expected.jd_ut) < 1.0 / 86400.0
                && direct.civil_day_number == expected.civil_day_number,
            "direct solar-term query matches calcY across the 2044 cycle");
    }

    const ChineseCalendarContext historical_context =
        make_context(historical_china_config());
    const taiyin::SplitJulianDate mid_1800 = beijing_jd_ut(
        1800, 6, 1, 12, 0, "1800 historical solar-term oracle probe");
    ChineseCalendarYear year_1800;
    SolarTermEvent historical_direct;
    expect_status(
        calcY(&historical_context, mid_1800, &year_1800, &diagnostic),
        "1800 historical calcY solar-term oracle");
    expect_status(
        getSpecificJieQi(
            &historical_context, 1800, 0, &historical_direct, &diagnostic),
        "1800 historical direct solar-term query");
    expect(
        std::fabs(historical_direct.jd_ut - year_1800.solar_terms[6].jd_ut)
                < 1.0 / 86400.0
            && historical_direct.civil_day_number
                == year_1800.solar_terms[6].civil_day_number,
        "historical direct solar-term query matches calcY correction days");
}

void test_pmo_2026_calendar_oracles(uint64_t route_rule_id) {
    using namespace taiyin;
    using namespace taiyin::chinese_calendar;
    using namespace taiyin::runtime;

    // Chinese Academy of Sciences Purple Mountain Observatory,
    // "2026 Calendar Data". Published times are Beijing time rounded to the
    // minute, so they are treated as 60-second oracle intervals.
    struct PrintedMinute {
        int32_t month;
        int32_t day;
        int32_t hour;
        int32_t minute;
    };
    static const PrintedMinute kSolarTerms[] = {
        {1, 5, 16, 23}, {1, 20, 9, 45}, {2, 4, 4, 2},
        {2, 18, 23, 52}, {3, 5, 21, 59}, {3, 20, 22, 46},
        {4, 5, 2, 40}, {4, 20, 9, 39}, {5, 5, 19, 49},
        {5, 21, 8, 37}, {6, 5, 23, 48}, {6, 21, 16, 25},
        {7, 7, 9, 57}, {7, 23, 3, 13}, {8, 7, 19, 43},
        {8, 23, 10, 19}, {9, 7, 22, 41}, {9, 23, 8, 5},
        {10, 8, 14, 29}, {10, 23, 17, 38}, {11, 7, 17, 52},
        {11, 22, 15, 23}, {12, 7, 10, 53}, {12, 22, 4, 50},
    };

    const ChineseCalendarContext context =
        make_context(fixed_utc_offset_config(8 * 60), route_rule_id);
    ChineseCalendarYear year;
    EphemerisEvalDiagnostic diagnostic;
    const SplitJulianDate probe =
        beijing_jd_ut(2026, 6, 1, 12, 0, "PMO year probe");
    expect_status(
        calcY(&context, probe, &year, &diagnostic),
        "PMO 2026 calendar year");
    for (std::size_t i = 0;
         i < sizeof(kSolarTerms) / sizeof(kSolarTerms[0]);
         ++i) {
        const PrintedMinute& printed = kSolarTerms[i];
        const SplitJulianDate expected = beijing_jd_ut(
            2026,
            printed.month,
            printed.day,
            printed.hour,
            printed.minute,
            "PMO solar term minute");
        expect_time_within_seconds(
            year.solar_terms[i + 1].jd_ut,
            expected,
            60.0,
            "PMO 2026 solar term");
    }

    struct MonthOracle {
        int32_t lunar_year;
        uint8_t month;
        uint8_t day_count;
    };
    static const MonthOracle kMonths[] = {
        {2025, 11, 30}, {2025, 12, 29},
        {2026, 1, 30}, {2026, 2, 29}, {2026, 3, 30},
        {2026, 4, 29}, {2026, 5, 29}, {2026, 6, 30},
        {2026, 7, 29}, {2026, 8, 29}, {2026, 9, 30},
        {2026, 10, 30}, {2026, 11, 30}, {2026, 12, 29},
    };
    for (std::size_t i = 0; i < sizeof(kMonths) / sizeof(kMonths[0]); ++i) {
        expect(
            year.months[i].lunar_year == kMonths[i].lunar_year
                && year.months[i].month == kMonths[i].month
                && year.months[i].is_leap == 0
                && year.months[i].day_count == kMonths[i].day_count,
            "PMO 2026 lunar month name and size");
    }
    expect_lunar(
        context, 2026, 2, 16, 2025, 12, 29, false, 29,
        "PMO day before 2026 lunar new year");
    expect_lunar(
        context, 2026, 2, 17, 2026, 1, 1, false, 30,
        "PMO 2026 lunar new year");
    expect_lunar(
        context, 2026, 3, 19, 2026, 2, 1, false, 29,
        "PMO 2026 second lunar month");

    struct PhaseOracle {
        uint8_t quarter;
        PrintedMinute time;
    };
    static const PhaseOracle kPhases[] = {
        {2, {1, 3, 18, 3}}, {3, {1, 10, 23, 48}},
        {0, {1, 19, 3, 52}}, {1, {1, 26, 12, 47}},
        {2, {2, 2, 6, 9}}, {3, {2, 9, 20, 43}},
        {0, {2, 17, 20, 1}}, {1, {2, 24, 20, 28}},
        {2, {3, 3, 19, 38}}, {3, {3, 11, 17, 39}},
        {0, {3, 19, 9, 23}}, {1, {3, 26, 3, 18}},
        {2, {4, 2, 10, 12}}, {3, {4, 10, 12, 52}},
        {0, {4, 17, 19, 52}}, {1, {4, 24, 10, 32}},
        {2, {5, 2, 1, 23}}, {3, {5, 10, 5, 10}},
        {0, {5, 17, 4, 1}}, {1, {5, 23, 19, 11}},
        {2, {5, 31, 16, 45}}, {3, {6, 8, 18, 1}},
        {0, {6, 15, 10, 54}}, {1, {6, 22, 5, 55}},
        {2, {6, 30, 7, 57}}, {3, {7, 8, 3, 29}},
        {0, {7, 14, 17, 44}}, {1, {7, 21, 19, 6}},
        {2, {7, 29, 22, 36}}, {3, {8, 6, 10, 21}},
        {0, {8, 13, 1, 37}}, {1, {8, 20, 10, 46}},
        {2, {8, 28, 12, 19}}, {3, {9, 4, 15, 51}},
        {0, {9, 11, 11, 27}}, {1, {9, 19, 4, 44}},
        {2, {9, 27, 0, 49}}, {3, {10, 3, 21, 25}},
        {0, {10, 10, 23, 50}}, {1, {10, 19, 0, 13}},
        {2, {10, 26, 12, 12}}, {3, {11, 2, 4, 28}},
        {0, {11, 9, 15, 2}}, {1, {11, 17, 19, 48}},
        {2, {11, 24, 22, 54}}, {3, {12, 1, 14, 9}},
        {0, {12, 9, 8, 52}}, {1, {12, 17, 13, 43}},
        {2, {12, 24, 9, 28}}, {3, {12, 31, 2, 59}},
    };
    for (std::size_t i = 0; i < sizeof(kPhases) / sizeof(kPhases[0]); ++i) {
        const PhaseOracle& oracle = kPhases[i];
        const SplitJulianDate expected = beijing_jd_ut(
            2026,
            oracle.time.month,
            oracle.time.day,
            oracle.time.hour,
            oracle.time.minute,
            "PMO lunar phase minute");
        SplitJulianDate actual;
        std::size_t event_count = 0;
        expect_status(
            search_lunar_phase_crossings_default_step_ut(
                &context.astronomy,
                static_cast<double>(oracle.quarter) * TAIYIN_PI / 2.0,
                expected - 1.0,
                expected + 1.0,
                0,
                &actual,
                1,
                &event_count,
                &diagnostic),
            "PMO 2026 lunar phase search");
        expect(event_count == 1, "PMO 2026 lunar phase count");
        if (event_count == 1) {
            expect_time_within_seconds(
                actual, expected, 60.0, "PMO 2026 lunar phase");
        }
    }
}

void test_historical_calendar_fixtures() {
    using namespace taiyin::chinese_calendar;
    const ChineseCalendarContext context = make_context(
        historical_china_config(),
        taiyin::runtime::TAIYIN_EPHEMERIS_ROUTE_SEMI_ANALYTIC);

    SolarDate solar;
    solar.year = -456;
    solar.month = 4;
    solar.day = 4;
    LunarDate lunar;
    taiyin::runtime::EphemerisEvalDiagnostic diagnostic;
    expect_status(
        fromSolar(&context, &solar, &lunar, &diagnostic),
        "ancient historical fixture");
    expect(
        lunar.year == -456 && lunar.month == 5
            && lunar.day == 12 && lunar.is_leap == 0,
        "-456-04-04 is historical lunar -456-05-12");
    if (lunar.year != -456 || lunar.month != 5 || lunar.day != 12
        || lunar.is_leap != 0) {
        std::cerr << "  historical fixture resolved to lunar "
                  << lunar.year << "-" << static_cast<int>(lunar.month)
                  << "-" << static_cast<int>(lunar.day)
                  << (lunar.is_leap ? " leap" : "") << "\n";
    }

    expect_roundtrip(context, -456, 4, 4, "BCE historical roundtrip");

    // The Taichu transition changes the year-start convention. Adjacent
    // winter-solstice windows must not assign the same LunarDate identity to
    // both 105 BCE-01-03 and 104 BCE-01-20 (astronomical years -104/-103), or
    // fromLunar() cannot invert the value returned by fromSolar().
    expect_lunar(
        context,
        -104, 1, 3,
        -105, 11, 27, false, 29,
        "pre-Taichu winter-year identity");
    expect_lunar(
        context,
        -103, 1, 20,
        -104, 11, 27, false, 30,
        "Taichu transition winter-year identity");
    expect_roundtrip(
        context, -104, 1, 3, "pre-Taichu BCE roundtrip");
    expect_roundtrip(
        context, -103, 1, 20, "Taichu-transition BCE roundtrip");

    expect_lunar(
        context,
        10, 6, 1,
        10, 6, 1, false, 30,
        "Xin reform Dart fixture");
    expect_lunar(
        context,
        238, 6, 1,
        238, 6, 2, false, 29,
        "Jingchu reform Dart fixture");
    expect_lunar(
        context,
        690, 6, 1,
        690, 4, 19, false, 29,
        "Wu Zetian reform Dart fixture");

    // Reform windows can produce structurally adjacent months with the same
    // numeric month. Where the historical profile supplies an exceptional
    // written name or a new year boundary, preserve it in the structured
    // identity so fromSolar() remains invertible.
    expect_lunar(
        context,
        23, 12, 2,
        23, 12, 1, false, 29,
        "Xin alternate-twelve month",
        TAIYIN_CHINESE_MONTH_NAME_ALT_TWELVE);
    expect_lunar(
        context,
        24, 1, 12,
        23, 12, 13, false, 30,
        "post-Xin ordinary twelve month");
    expect_lunar(
        context,
        239, 12, 13,
        239, 12, 1, false, 30,
        "Jingchu alternate-twelve month",
        TAIYIN_CHINESE_MONTH_NAME_ALT_TWELVE);
    expect_lunar(
        context,
        240, 1, 12,
        239, 12, 1, false, 29,
        "post-Jingchu ordinary twelve month");
    expect_lunar(
        context,
        689, 12, 18,
        690, 1, 1, false, 29,
        "Wu Zetian renamed first month");
    expect_lunar(
        context,
        690, 2, 15,
        690, 1, 1, false, 29,
        "Wu Zetian alternate-one month",
        TAIYIN_CHINESE_MONTH_NAME_ALT_ONE);
    expect_lunar(
        context,
        761, 12, 2,
        762, 1, 1, false, 29,
        "Tang renamed first month advances lunar year");
    expect_lunar(
        context,
        762, 3, 30,
        762, 5, 1, false, 30,
        "Tang reform window end");
    expect_lunar(
        context,
        701, 1, 15,
        700, 12, 2, false, 30,
        "post-Wu same-name twelve month",
        TAIYIN_CHINESE_MONTH_NAME_LATER_SAME_NAME);
    expect_lunar(
        context,
        762, 5, 28,
        762, 5, 1, false, 30,
        "post-Tang same-name five month",
        TAIYIN_CHINESE_MONTH_NAME_LATER_SAME_NAME);

    expect_roundtrip(context, 10, 6, 1, "Xin reform roundtrip");
    expect_roundtrip(context, 238, 6, 1, "Jingchu reform roundtrip");
    expect_roundtrip(context, 690, 6, 1, "Wu Zetian reform roundtrip");
    expect_roundtrip(context, 23, 12, 2, "Xin alternate-twelve roundtrip");
    expect_roundtrip(context, 24, 1, 12, "post-Xin twelve roundtrip");
    expect_roundtrip(
        context, 239, 12, 13, "Jingchu alternate-twelve roundtrip");
    expect_roundtrip(
        context, 240, 1, 12, "post-Jingchu twelve roundtrip");
    expect_roundtrip(
        context, 690, 2, 15, "Wu Zetian alternate-one roundtrip");
    expect_roundtrip(
        context, 701, 1, 15, "post-Wu later-same-name roundtrip");
    expect_roundtrip(
        context, 761, 12, 2, "Tang renamed first-month roundtrip");
    expect_roundtrip(
        context, 762, 5, 28, "post-Tang later-same-name roundtrip");

    uint8_t ordinary_month_days = 0;
    taiyin::runtime::EphemerisEvalDiagnostic month_diagnostic;
    expect(
        getLunarMonthNum(
            &context, 23, 12, false, &ordinary_month_days, &month_diagnostic)
            == taiyin::TAIYIN_STATUS_OK
            && ordinary_month_days == 30,
        "ordinary Xin twelve month wins over alternate-twelve fallback");
    expect(
        getLunarMonthNum(
            &context, 239, 12, false, &ordinary_month_days, &month_diagnostic)
            == taiyin::TAIYIN_STATUS_OK
            && ordinary_month_days == 29,
        "ordinary Jingchu twelve month wins over alternate-twelve fallback");

    uint8_t exceptional_month_days = 0;
    expect(
        getLunarMonthNum(
            &context,
            -456,
            13,
            true,
            &exceptional_month_days,
            &month_diagnostic) == taiyin::TAIYIN_STATUS_OK,
        "exceptional-only leap thirteenth month is queryable");
    expect(
        exceptional_month_days == 29 || exceptional_month_days == 30,
        "exceptional-only leap thirteenth month has a civil month length");
}

void test_jingchu_transition_month() {
    using namespace taiyin::chinese_calendar;
    const ChineseCalendarContext context = make_context(
        historical_china_config(),
        taiyin::runtime::TAIYIN_EPHEMERIS_ROUTE_SEMI_ANALYTIC);

    // The 237 CE Jingchu-calendar transition month starts on civil day
    // 1807696 and is a recorded 28-day month. calcY must accept it rather
    // than returning TAIYIN_ERROR_INTERNAL.
    const taiyin::SplitJulianDate probe = beijing_jd_ut(
        237, 6, 1, 12, 0, "237 Jingchu transition probe");
    ChineseCalendarYear year;
    taiyin::runtime::EphemerisEvalDiagnostic diagnostic;
    expect_status(
        calcY(&context, probe, &year, &diagnostic),
        "237 calcY accepts Jingchu transition month");

    bool found = false;
    for (std::size_t i = 0; i < TAIYIN_CHINESE_CALENDAR_MONTH_COUNT; ++i) {
        if (year.new_moons[i].civil_day_number == INT64_C(1807696)) {
            found = year.new_moons[i + 1].civil_day_number
                    == INT64_C(1807724)
                && year.months[i].day_count == 28u;
        }
    }
    expect(found, "237 calcY reports the recorded 28-day Jingchu month");
}

void test_de441_long_range_stability() {
    using namespace taiyin::chinese_calendar;
    static const int32_t kYears[] = {
        -12999, -12000, -9000, -6000, -3000,
        0, 3000, 6000, 9000, 12000, 16000, 16999,
    };
    ChineseCalendarContext context =
        make_context(fixed_utc_offset_config(8 * 60));
    context.astronomy.model_context.precession_model_id =
        taiyin::dispatch::PRECESSION_VONDRAK2011;
    for (std::size_t year_index = 0;
         year_index < sizeof(kYears) / sizeof(kYears[0]);
         ++year_index) {
        const taiyin::CalendarDateTime date = {
            kYears[year_index], 7, 1, 12, 0, 0.0,
        };
        const taiyin::SplitJulianDate probe_jd_ut =
            split_jd(date, "long-range probe split JD");
        double sun_position[6] = {};
        double moon_position[6] = {};
        taiyin::runtime::EphemerisEvalDiagnostic position_diagnostic;
        const uint32_t position_flags =
            taiyin::runtime::TAIYIN_NATIVE_POSITION_SPEED
            | taiyin::runtime::TAIYIN_NATIVE_POSITION_RADIANS;
        const taiyin::Status sun_status =
            taiyin::runtime::calc_position_ut(
                &context.astronomy,
                taiyin::TAIYIN_BODY_SUN,
                probe_jd_ut,
                position_flags,
                sun_position,
                &position_diagnostic);
        const taiyin::Status moon_status =
            taiyin::runtime::calc_position_ut(
                &context.astronomy,
                taiyin::TAIYIN_BODY_MOON,
                probe_jd_ut,
                position_flags,
                moon_position,
                &position_diagnostic);
        if (sun_status != taiyin::TAIYIN_STATUS_OK
            || moon_status != taiyin::TAIYIN_STATUS_OK) {
            std::cerr << "  long-range position year " << kYears[year_index]
                      << ": Sun=" << taiyin::status_name(sun_status)
                      << ", Moon=" << taiyin::status_name(moon_status)
                      << "\n";
        }
        expect(
            sun_status == taiyin::TAIYIN_STATUS_OK
                && moon_status == taiyin::TAIYIN_STATUS_OK,
            "long-range Sun/Moon position coverage");
        ChineseCalendarYear result;
        taiyin::runtime::EphemerisEvalDiagnostic diagnostic;
        const taiyin::Status status = calcY(
            &context, probe_jd_ut, &result, &diagnostic);
        if (status != taiyin::TAIYIN_STATUS_OK) {
            std::cerr << "  long-range year " << kYears[year_index]
                      << " failed: " << taiyin::status_name(status)
                      << ", diagnostic=" << diagnostic.status
                      << ", target=" << diagnostic.target_id
                      << ", center=" << diagnostic.center_id
                      << ", method=" << diagnostic.attempted_method_id
                      << ", jd=" << diagnostic.jd_tdb.day_number
                      << "+" << diagnostic.jd_tdb.day_fraction << "\n";
            std::cerr << "    nearest coverage=["
                      << diagnostic.nearest_coverage_start << ", "
                      << diagnostic.nearest_coverage_end << "]"
                      << ", Sun lon=" << sun_position[0]
                      << ", speed=" << sun_position[3] << "\n";
            int sampled_crossings = 0;
            double previous_residual = 0.0;
            bool have_previous = false;
            for (int day_offset = -220; day_offset <= 220; day_offset += 2) {
                double sampled[6] = {};
                taiyin::runtime::EphemerisEvalDiagnostic scratch;
                if (taiyin::runtime::calc_position_ut(
                        &context.astronomy,
                        taiyin::TAIYIN_BODY_SUN,
                        probe_jd_ut + day_offset,
                        position_flags,
                        sampled,
                        &scratch) != taiyin::TAIYIN_STATUS_OK) {
                    continue;
                }
                const double residual = taiyin::angular_difference_radians(
                    sampled[0], 270.0 * taiyin::TAIYIN_DEG_TO_RAD);
                if (have_previous && previous_residual * residual < 0.0
                    && std::fabs(previous_residual - residual)
                        <= taiyin::TAIYIN_PI) {
                    ++sampled_crossings;
                }
                previous_residual = residual;
                have_previous = true;
            }
            std::cerr << "    independent 2-day crossings="
                      << sampled_crossings << "\n";
            ++failures;
            continue;
        }
        expect(
            result.solar_term_count == TAIYIN_CHINESE_CALENDAR_TERM_COUNT
                && result.new_moon_count
                    == TAIYIN_CHINESE_CALENDAR_NEW_MOON_COUNT
                && result.month_count
                    == TAIYIN_CHINESE_CALENDAR_MONTH_COUNT,
            "long-range calendar result counts");
        static const uint8_t kDirectIndices[] = {0, 18, 19, 23};
        static const std::size_t kCalendarIndices[] = {6, 24, 1, 5};
        for (std::size_t direct_index = 0;
             direct_index < sizeof(kDirectIndices) / sizeof(kDirectIndices[0]);
             ++direct_index) {
            SolarTermEvent direct;
            const taiyin::Status direct_status = getSpecificJieQi(
                &context,
                kYears[year_index],
                kDirectIndices[direct_index],
                &direct,
                &diagnostic);
            expect(
                direct_status == taiyin::TAIYIN_STATUS_OK,
                "long-range direct solar-term query succeeds");
            const SolarTermEvent& expected = result.solar_terms[
                kCalendarIndices[direct_index]];
            expect(
                direct_status == taiyin::TAIYIN_STATUS_OK
                    && std::fabs(direct.jd_ut - expected.jd_ut)
                        < 1.0 / 86400.0
                    && direct.civil_day_number == expected.civil_day_number,
                "long-range direct solar-term query matches calcY");
        }
        for (std::size_t i = 0;
             i + 1 < TAIYIN_CHINESE_CALENDAR_TERM_COUNT;
             ++i) {
            const double interval =
                result.solar_terms[i + 1].jd_ut
                - result.solar_terms[i].jd_ut;
            expect(
                taiyin::split_julian_date_is_finite(
                    result.solar_terms[i].jd_ut)
                    && interval > 14.0 && interval < 16.5,
                "long-range solar terms remain finite and ordered");
        }
        for (std::size_t i = 0;
             i + 1 < TAIYIN_CHINESE_CALENDAR_NEW_MOON_COUNT;
             ++i) {
            const double interval =
                result.new_moons[i + 1].jd_ut
                - result.new_moons[i].jd_ut;
            expect(
                taiyin::split_julian_date_is_finite(
                    result.new_moons[i].jd_ut)
                    && interval > 29.0 && interval < 30.0,
                "long-range new moons remain finite and ordered");
            expect(
                result.months[i].day_count == 29
                    || result.months[i].day_count == 30,
                "long-range civil lunar month has 29 or 30 days");
        }
    }
}

}  // namespace

int main() {
    test_historical_table_integrity();
    expect(initialize_runtime(), "initialize ephemeris runtime");
    if (failures == 0) {
        test_modern_roundtrip();
        test_2033_leap_month();
        test_day_boundary_changes_day_not_event();
        test_calendar_config_semantics();
        test_single_solar_term_queries();
        test_pmo_2026_calendar_oracles(
            taiyin::runtime::TAIYIN_EPHEMERIS_ROUTE_OPM2);
        test_pmo_2026_calendar_oracles(
            taiyin::runtime::TAIYIN_EPHEMERIS_ROUTE_SEMI_ANALYTIC);
        test_historical_calendar_fixtures();
        test_jingchu_transition_month();
        if (long_range_tests_enabled()) {
            test_de441_long_range_stability();
        }
    }
    if (failures != 0) {
        std::cerr << failures << " Chinese calendar test(s) failed\n";
        return 1;
    }
    std::cout << "Chinese calendar tests passed\n";
    return 0;
}
