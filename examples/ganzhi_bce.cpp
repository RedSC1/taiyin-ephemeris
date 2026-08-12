#include "taiyin/body_id.h"
#include "taiyin/chinese_calendar/calendar.h"
#include "taiyin/chinese_calendar/ganzhi.h"
#include "taiyin/runtime/ephemeris_route.h"
#include "taiyin/runtime/native_context.h"
#include "taiyin/runtime/runtime.h"
#include "taiyin/status.h"
#include "taiyin/time.h"

#include <cstdlib>
#include <iostream>
#include <string>

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

std::string ganzhi_name(uint8_t value, bool hanzi) {
    static const char* roman_stems[] = {
        "Jia", "Yi", "Bing", "Ding", "Wu", "Ji", "Geng", "Xin", "Ren", "Gui",
    };
    static const char* roman_branches[] = {
        "Zi", "Chou", "Yin", "Mao", "Chen", "Si", "Wu", "Wei", "Shen", "You", "Xu", "Hai",
    };
    static const char* hanzi_stems[] = {
        "甲", "乙", "丙", "丁", "戊", "己", "庚", "辛", "壬", "癸",
    };
    static const char* hanzi_branches[] = {
        "子", "丑", "寅", "卯", "辰", "巳", "午", "未", "申", "酉", "戌", "亥",
    };
    const uint8_t stem = static_cast<uint8_t>((value >> 4) & 0x0fu);
    const uint8_t branch = static_cast<uint8_t>(value & 0x0fu);
    if (stem >= 10u || branch >= 12u) return "invalid";
    if (hanzi) return std::string(hanzi_stems[stem]) + hanzi_branches[branch];
    return std::string(roman_stems[stem]) + '-' + roman_branches[branch];
}

void print_pillar(const char* label, uint8_t value) {
    uint8_t nayin_id = kInvalidNaYin;
    uint8_t element = kInvalidNaYin;
    if (!check(get_nayin_id(value, &nayin_id), "get_nayin_id")
        || !check(get_nayin_element(value, &element), "get_nayin_element")) {
        return;
    }
    static const char* elements[] = {"水", "木", "金", "土", "火"};
    static const char* nayin_names[] = {
        "海中金", "炉中火", "大林木", "路旁土", "剑锋金", "山头火",
        "涧下水", "城头土", "白蜡金", "杨柳木", "泉中水", "屋上土",
        "霹雳火", "松柏木", "长流水", "沙中金", "山下火", "平地木",
        "壁上土", "金箔金", "覆灯火", "天河水", "大驿土", "钗钏金",
        "桑柘木", "大溪水", "沙中土", "天上火", "石榴木", "大海水",
    };
    std::cout << label << ": " << ganzhi_name(value, true)
              << " (" << ganzhi_name(value, false) << ')'
              << "  encoded=0x" << std::hex << static_cast<int>(value) << std::dec
              << "  纳音=" << nayin_names[nayin_id]
              << "  五行=" << elements[element] << "\n";
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
    const ChineseCalendarConfig calendar_config = fixed_utc_offset_config(8 * 60);
    if (!check(
            initialize_context(&calendar, &astronomy, &calendar_config),
            "initialize_context")) {
        return 1;
    }

    // CalendarDateTime uses astronomical year numbering: -1045 is 1046 BCE.
    // virtual_time is the caller-selected UTC+8 civil clock. The absolute
    // instant passed to the astronomy layer is therefore eight hours earlier.
    const CalendarDateTime virtual_time = {-1045, 1, 20, 6, 30, 0.0};
    SplitJulianDate instant_utc;
    if (!julian_day_split(virtual_time, &instant_utc)
        || !add_seconds_to_split_jd(instant_utc, -8.0 * 3600.0, &instant_utc)) {
        std::cerr << "failed to construct the BCE instant\n";
        return 1;
    }

    GanzhiFourPillars pillars;
    EphemerisEvalDiagnostic diagnostic = {};
    if (!check(
            calculate_four_pillars(
                &calendar,
                instant_utc,
                virtual_time,
                TAIYIN_GANZHI_RAT_HOUR_NO_SPLIT,
                &pillars,
                &diagnostic),
            "calculate_four_pillars")) {
        return 1;
    }

    std::cout << "Taiyin Ganzhi calendar example\n"
              << "Civil clock: 1046 BCE-01-20 06:30:00 UTC+08"
              << " (astronomical year -1045)\n"
              << "Rule: year at Lichun, month at jie, no split at late Zi hour\n";
    print_pillar("Year ", pillars.year);
    print_pillar("Month", pillars.month);
    print_pillar("Day  ", pillars.day);
    print_pillar("Hour ", pillars.hour);

    return 0;
}
