/*
中文说明
--------
《新唐书·天文志》记唐文宗太和七年：
“五月甲辰，荧惑守心中星。”

“荧惑”是火星，“心中星”通常对应心宿二（Antares）。本例不把现代
“最近角距”机械等同于古代“守”的全部判据，而是用 Taiyin 的历史历法和
干支先定位该年五月甲辰，再用正式事件搜索 API 计算记录日内及其前后火星
与心宿二的最小角距，并搜索邻近的火星黄经留行。这样可以直接观察火星是否
在心宿二附近缓行、留驻。本例只复算星历几何，不裁决史料或文字释读。

English note
------------
The astronomical treatise of the New Book of Tang records for the seventh
year of the Taihe era: "On the Jia-Chen day of the fifth month, Mars guarded
the central star of Xin." Mars is Yinghuo, and the central star of Xin is
commonly identified with Antares. This example does not equate the complete
historical meaning of "guarding" with a single modern closest-approach value.
It first resolves the recorded lunar date and sexagenary day with Taiyin's
historical calendar, then searches the Mars-Antares minimum separation on that
civil day and in a surrounding window, together with nearby stations of Mars.
It reconstructs ephemeris geometry only and does not adjudicate the text.

Source text:
https://ctext.org/wiki.pl?chapter=164320&if=en&remap=gb
*/

#include "taiyin/angle.h"
#include "taiyin/body_id.h"
#include "taiyin/chinese_calendar/calendar.h"
#include "taiyin/chinese_calendar/ganzhi.h"
#include "taiyin/runtime/ephemeris_route.h"
#include "taiyin/runtime/event_search.h"
#include "taiyin/runtime/native_context.h"
#include "taiyin/runtime/runtime.h"
#include "taiyin/runtime/star_position.h"
#include "taiyin/status.h"
#include "taiyin/time.h"

#include <cmath>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <limits>
#include <string>

namespace {

using namespace taiyin;
using namespace taiyin::chinese_calendar;
using namespace taiyin::runtime;

const double kUtcOffsetHours = 8.0;
const uint64_t kPositionFlags =
    TAIYIN_NATIVE_POSITION_ALLOW_BARYCENTER_APPROX;

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

SplitJulianDate local_midnight_to_ut(const SolarDate& date) {
    const CalendarDateTime local = {
        date.year, date.month, date.day, 0, 0, 0.0,
    };
    SplitJulianDate jd;
    SplitJulianDate result;
    if (!julian_day_split(local, &jd)
        || !add_seconds_to_split_jd(
            jd, -kUtcOffsetHours * 3600.0, &result)) {
        return SplitJulianDate(
            0, std::numeric_limits<double>::quiet_NaN());
    }
    return result;
}

void print_local_clock(const SplitJulianDate& jd_ut) {
    SplitJulianDate local;
    SplitJulianDate rounded;
    CalendarDateTime value = {};
    if (!add_seconds_to_split_jd(
            jd_ut, kUtcOffsetHours * 3600.0, &local)
        || !add_seconds_to_split_jd(local, 0.5, &rounded)
        || !reverse_julian_day_split(rounded, &value)) {
        std::cout << "invalid";
        return;
    }
    std::cout << value.year << '-'
              << std::setw(2) << std::setfill('0') << value.month << '-'
              << std::setw(2) << value.day << ' '
              << std::setw(2) << value.hour << ':'
              << std::setw(2) << value.minute << ':'
              << std::setw(2) << static_cast<int>(value.second)
              << std::setfill(' ');
}

bool locate_recorded_day(
    const ChineseCalendarContext& calendar,
    SolarDate* out,
    EphemerisEvalDiagnostic* diagnostic
) {
    uint8_t target = kInvalidGanzhi;
    if (!check(make_ganzhi(0, 4, &target), "make_ganzhi(Jia-Chen)")) {
        return false;
    }

    uint8_t month_days = 0;
    if (!check(
            getLunarMonthNum(
                &calendar, 833, 5, false, &month_days, diagnostic),
            "getLunarMonthNum(Taihe 7, month 5)")) {
        return false;
    }

    LunarDate lunar;
    lunar.year = 833;
    lunar.month = 5;
    lunar.day = 1;
    lunar.is_leap = 0;
    lunar.month_name = TAIYIN_CHINESE_MONTH_NAME_NORMAL;
    SolarDate first_day;
    if (!check(
            fromLunar(&calendar, &lunar, &first_day, diagnostic),
            "fromLunar(Taihe 7, month 5, day 1)")) {
        return false;
    }

    SplitJulianDate noon;
    if (!julian_day_split(
            {first_day.year, first_day.month, first_day.day, 12, 0, 0.0},
            &noon)) {
        std::cerr << "failed to construct the first civil day\n";
        return false;
    }
    for (uint8_t day = 0; day < month_days; ++day) {
        CalendarDateTime civil = {};
        const SplitJulianDate candidate_jd = noon + static_cast<double>(day);
        if (!reverse_julian_day_split(candidate_jd, &civil)) {
            std::cerr << "failed to advance the historical civil date\n";
            return false;
        }
        uint8_t pillar = kInvalidGanzhi;
        if (!check(
                calculate_day_pillar(civil, &pillar),
                "calculate_day_pillar")) {
            return false;
        }
        if (pillar == target) {
            out->year = civil.year;
            out->month = civil.month;
            out->day = civil.day;
            return true;
        }
    }
    std::cerr << "Jia-Chen was not found in lunar month 5 of year 833\n";
    return false;
}

void print_separation(
    const char* label,
    const BodyStarAngularSeparationSearchResult& result
) {
    const double degrees = result.separation_rad * TAIYIN_RAD_TO_DEG;
    std::cout << std::left << std::setw(31) << label << std::right;
    print_local_clock(result.jd);
    std::cout << std::fixed << std::setprecision(4)
              << "  separation=" << degrees << " deg ("
              << degrees * 60.0 << " arcmin)\n";
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
        std::cerr << "failed to initialize runtime from data root: "
                  << data_root << '\n';
        return 1;
    }
    const std::string star_catalog =
        std::string(data_root) + "/stars/catalogs/stars-fixed-traditional.tsc1";
    if (!check(
            add_global_tsc1_star_catalog(star_catalog.c_str()),
            "add_global_tsc1_star_catalog")) {
        return 1;
    }

    NativeCalcContext astronomy;
    if (!check(
            native_context_set_geocentric_observer(
                &astronomy, TAIYIN_BODY_EARTH, TAIYIN_BODY_EARTH),
            "native_context_set_geocentric_observer")
        || !check(
            native_context_use_solar_deflector(&astronomy),
            "native_context_use_solar_deflector")
        || !check(
            native_context_set_route_rule(
                &astronomy, TAIYIN_EPHEMERIS_ROUTE_SEMI_ANALYTIC),
            "native_context_set_route_rule")) {
        return 1;
    }
    astronomy.apparent_options.flags =
        TAIYIN_APPARENT_SPHERICAL
        | TAIYIN_APPARENT_LIGHT_TIME
        | TAIYIN_APPARENT_ABERRATION
        | TAIYIN_APPARENT_DEFLECTION;
    astronomy.apparent_options.output_frame_id =
        TAIYIN_APPARENT_FRAME_TRUE_ECLIPTIC_OF_DATE;

    ChineseCalendarContext calendar;
    const ChineseCalendarConfig calendar_config = historical_china_config();
    if (!check(
            initialize_context(&calendar, &astronomy, &calendar_config),
            "initialize_context")) {
        return 1;
    }

    EphemerisEvalDiagnostic diagnostic;
    SolarDate recorded_date;
    if (!locate_recorded_day(calendar, &recorded_date, &diagnostic)) {
        return 1;
    }
    const SplitJulianDate recorded_begin =
        local_midnight_to_ut(recorded_date);
    const SplitJulianDate next_midnight = recorded_begin + 1.0;
    SplitJulianDate recorded_end;
    if (!add_seconds_to_split_jd(next_midnight, -1.0, &recorded_end)) {
        std::cerr << "failed to construct the recorded civil-day interval\n";
        return 1;
    }
    const SplitJulianDate surrounding_begin = recorded_begin - 60.0;
    const SplitJulianDate surrounding_end = next_midnight + 60.0;

    BodyStarAngularSeparationSearchResult recorded_minimum;
    const Status record_search_status =
        search_minimum_body_star_angular_separation_ut(
                &astronomy,
                TAIYIN_BODY_MARS,
                "antares",
                recorded_begin,
                recorded_end,
                0.125,
                kPositionFlags,
                &recorded_minimum,
                &diagnostic);
    if (!check(record_search_status, "search record-day Mars-Antares minimum")) {
        std::cerr << "underlying diagnostic: " << status_name(diagnostic.status)
                  << ", target=" << diagnostic.target_id
                  << ", component=" << diagnostic.component_target_id << '\n';
        return 1;
    }

    BodyStarAngularSeparationSearchResult surrounding_minimum;
    if (!check(
            search_minimum_body_star_angular_separation_ut(
                &astronomy,
                TAIYIN_BODY_MARS,
                "antares",
                surrounding_begin,
                surrounding_end,
                1.0,
                kPositionFlags,
                &surrounding_minimum,
                &diagnostic),
            "search surrounding Mars-Antares minimum")) {
        return 1;
    }

    SplitJulianDate stations[4];
    double station_longitudes[4] = {};
    size_t station_count = 0;
    if (!check(
            search_body_longitude_stations_auto_step_ut(
                &astronomy,
                TAIYIN_BODY_MARS,
                surrounding_begin,
                surrounding_end,
                kPositionFlags,
                stations,
                station_longitudes,
                4,
                &station_count,
                &diagnostic),
            "search Mars stations")) {
        return 1;
    }

    size_t nearest_station = 0;
    double nearest_distance = std::numeric_limits<double>::infinity();
    for (size_t i = 0; i < station_count; ++i) {
        const double distance = std::fabs(
            days_between_split_jd(stations[i], surrounding_minimum.jd));
        if (distance < nearest_distance) {
            nearest_distance = distance;
            nearest_station = i;
        }
    }

    std::cout << "Tang 833 Mars-Antares example\n"
              << "Record: New Book of Tang, Taihe 7, lunar month 5, "
                 "Jia-Chen: Yinghuo guarded Xin's central star\n"
              << "Resolved civil date (Julian, Taiyin UTC+08 model): "
              << recorded_date.year << '-'
              << std::setw(2) << std::setfill('0')
              << static_cast<int>(recorded_date.month) << '-'
              << std::setw(2) << static_cast<int>(recorded_date.day)
              << std::setfill(' ') << "\n"
              << "Ephemeris: built-in semi-analytic; physical Mars request "
                 "with barycenter approximation when required\n\n";
    print_separation("Minimum on recorded civil day", recorded_minimum);
    print_separation("Minimum within +/- 60 days", surrounding_minimum);
    if (station_count != 0) {
        std::cout << std::left << std::setw(31)
                  << "Nearest Mars longitude station" << std::right;
        print_local_clock(stations[nearest_station]);
        std::cout << std::fixed << std::setprecision(2)
                  << "  offset from minimum=" << nearest_distance
                  << " days"
                  << "  longitude="
                  << station_longitudes[nearest_station] * TAIYIN_RAD_TO_DEG
                  << " deg\n";
    } else {
        std::cout << "No Mars longitude station found in the surrounding window\n";
    }
    return 0;
}
