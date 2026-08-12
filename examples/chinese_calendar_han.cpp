#include "taiyin/body_id.h"
#include "taiyin/chinese_calendar/calendar.h"
#include "taiyin/runtime/ephemeris_route.h"
#include "taiyin/runtime/native_context.h"
#include "taiyin/runtime/runtime.h"
#include "taiyin/status.h"
#include "taiyin/time.h"

#include <cstdlib>
#include <iomanip>
#include <iostream>

namespace {

using namespace taiyin;
using namespace taiyin::chinese_calendar;
using namespace taiyin::runtime;

const char* data_root_from_args(int argc, char** argv) {
    if (argc > 1 && argv[1] && argv[1][0] != '\0') return argv[1];
    const char* env = std::getenv("TAIYIN_DATA_ROOT");
    return env && env[0] != '\0' ? env : "data";
}

bool check(Status status, const char* operation) {
    if (status == TAIYIN_STATUS_OK) return true;
    std::cerr << operation << " failed: " << status_name(status) << '\n';
    return false;
}

const char* lunar_month_name(uint8_t month) {
    static const char* names[] = {
        "", "正月", "二月", "三月", "四月", "五月", "六月",
        "七月", "八月", "九月", "十月", "十一月", "十二月", "十三月",
    };
    return month <= 13u ? names[month] : "无效月";
}

const char* lunar_day_name(uint8_t day) {
    static const char* names[] = {
        "", "初一", "初二", "初三", "初四", "初五", "初六", "初七", "初八", "初九", "初十",
        "十一", "十二", "十三", "十四", "十五", "十六", "十七", "十八", "十九", "二十",
        "廿一", "廿二", "廿三", "廿四", "廿五", "廿六", "廿七", "廿八", "廿九", "三十",
    };
    return day <= 30u ? names[day] : "无效日";
}

const char* solar_term_name(uint8_t index) {
    static const char* names[] = {
        "冬至", "小寒", "大寒", "立春", "雨水", "惊蛰",
        "春分", "清明", "谷雨", "立夏", "小满", "芒种",
        "夏至", "小暑", "大暑", "立秋", "处暑", "白露",
        "秋分", "寒露", "霜降", "立冬", "小雪", "大雪",
    };
    return names[index % 24u];
}

void print_astronomical_year(int32_t year) {
    if (year <= 0) {
        std::cout << (1 - year) << " BCE";
    } else {
        std::cout << year << " CE";
    }
}

void print_beijing_time(const SplitJulianDate& jd_ut) {
    SplitJulianDate local;
    CalendarDateTime date = {};
    if (!add_seconds_to_split_jd(jd_ut, 8.0 * 3600.0, &local)
        || !reverse_julian_day_split(local, &date)) {
        std::cout << "invalid";
        return;
    }
    print_astronomical_year(date.year);
    std::cout << '-' << std::setfill('0') << std::setw(2) << date.month
              << '-' << std::setw(2) << date.day
              << ' ' << std::setw(2) << date.hour
              << ':' << std::setw(2) << date.minute
              << ':' << std::fixed << std::setprecision(1)
              << std::setw(4) << date.second << std::setfill(' ');
}

}  // namespace

int main(int argc, char** argv) {
    using namespace taiyin;
    using namespace taiyin::chinese_calendar;
    using namespace taiyin::runtime;

    const char* data_root = data_root_from_args(argc, argv);
    EphemerisRuntimeConfig runtime_config;
    runtime_config.data_root = data_root;
    runtime_config.load_packaged_data = true;
    if (!initialize_global_ephemeris_runtime(runtime_config)) {
        std::cerr << "failed to initialize runtime from data root: " << data_root << '\n';
        return 1;
    }

    NativeCalcContext astronomy;
    if (!check(
            native_context_set_geocentric_observer(
                &astronomy, TAIYIN_BODY_EARTH, TAIYIN_BODY_EARTH),
            "native_context_set_geocentric_observer")
        || !check(
            native_context_set_route_rule(
                &astronomy, TAIYIN_EPHEMERIS_ROUTE_SEMI_ANALYTIC),
            "native_context_set_route_rule")) {
        return 1;
    }

    ChineseCalendarContext calendar;
    const ChineseCalendarConfig calendar_config = historical_china_config();
    if (!check(
            initialize_context(&calendar, &astronomy, &calendar_config),
            "initialize_context")) {
        return 1;
    }

    // Astronomical year -103 is historical 104 BCE, at the modeled Taichu
    // calendar transition in the Western Han era.
    SolarDate solar;
    solar.year = -103;
    solar.month = 1;
    solar.day = 20;
    LunarDate lunar;
    EphemerisEvalDiagnostic diagnostic = {};
    if (!check(fromSolar(&calendar, &solar, &lunar, &diagnostic), "fromSolar")) {
        return 1;
    }

    SolarDate roundtrip;
    if (!check(fromLunar(&calendar, &lunar, &roundtrip, &diagnostic), "fromLunar")) {
        return 1;
    }

    const CalendarDateTime local_clock = {-103, 1, 20, 6, 30, 0.0};
    SplitJulianDate instant_ut;
    if (!julian_day_split(local_clock, &instant_ut)
        || !add_seconds_to_split_jd(instant_ut, -8.0 * 3600.0, &instant_ut)) {
        std::cerr << "failed to construct the Han-era instant\n";
        return 1;
    }
    SolarTermEvent previous_term;
    SolarTermEvent next_term;
    if (!check(
            getPrevJieQi(&calendar, instant_ut, &previous_term, &diagnostic),
            "getPrevJieQi")
        || !check(
            getNextJieQi(&calendar, instant_ut, &next_term, &diagnostic),
            "getNextJieQi")) {
        return 1;
    }

    std::cout << "Taiyin historical Chinese calendar example\n"
              << "Solar date: 104 BCE-01-20 (astronomical year -103, UTC+08 civil day)\n"
              << "Lunar date: ";
    print_astronomical_year(lunar.year);
    std::cout << ' ' << (lunar.is_leap ? "闰" : "")
              << lunar_month_name(lunar.month)
              << lunar_day_name(lunar.day)
              << ", month length=" << static_cast<int>(lunar.month_days) << " days\n"
              << "Round trip: ";
    print_astronomical_year(roundtrip.year);
    std::cout << '-' << static_cast<int>(roundtrip.month)
              << '-' << static_cast<int>(roundtrip.day) << "\n"
              << "Previous solar term: "
              << solar_term_name(previous_term.index_from_winter_solstice)
              << " (index=" << static_cast<int>(previous_term.index_from_winter_solstice) << ')'
              << " at ";
    print_beijing_time(previous_term.jd_ut);
    std::cout << " UTC+08\nNext solar term: "
              << solar_term_name(next_term.index_from_winter_solstice)
              << " (index=" << static_cast<int>(next_term.index_from_winter_solstice) << ')'
              << " at ";
    print_beijing_time(next_term.jd_ut);
    std::cout << " UTC+08\n";

    return 0;
}
