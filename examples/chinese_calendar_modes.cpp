#include "taiyin/body_id.h"
#include "taiyin/chinese_calendar/calendar.h"
#include "taiyin/runtime/ephemeris_route.h"
#include "taiyin/runtime/native_context.h"
#include "taiyin/runtime/runtime.h"
#include "taiyin/status.h"
#include "taiyin/time.h"

#include <cstdlib>
#include <iostream>

namespace {

using namespace taiyin;
using namespace taiyin::chinese_calendar;
using namespace taiyin::runtime;

bool make_calendar(
    const NativeCalcContext& astronomy,
    const ChineseCalendarConfig& config,
    ChineseCalendarContext* out
) {
    const Status status = initialize_context(out, &astronomy, &config);
    if (status == TAIYIN_STATUS_OK) return true;
    std::cerr << "initialize_context failed: " << status_name(status) << '\n';
    return false;
}

bool print_lunar(
    const char* label,
    const ChineseCalendarContext& context,
    const SplitJulianDate& instant_ut,
    uint8_t expected_month,
    uint8_t expected_day
) {
    LunarDate lunar;
    EphemerisEvalDiagnostic diagnostic;
    const Status status = fromInstant(
        &context, instant_ut, &lunar, &diagnostic);
    if (status != TAIYIN_STATUS_OK) {
        std::cerr << label << " failed: " << status_name(status) << '\n';
        return false;
    }
    std::cout << label << ": " << lunar.year << '-'
              << static_cast<int>(lunar.month) << '-'
              << static_cast<int>(lunar.day) << '\n';
    return lunar.month == expected_month && lunar.day == expected_day;
}

}  // namespace

int main(int argc, char** argv) {
    const char* data_root = argc > 1 ? argv[1] : "data";
    EphemerisRuntimeConfig runtime_config;
    runtime_config.data_root = data_root;
    runtime_config.load_packaged_data = true;
    if (!initialize_global_ephemeris_runtime(runtime_config)) {
        std::cerr << "failed to initialize runtime from " << data_root << '\n';
        return EXIT_FAILURE;
    }

    NativeCalcContext astronomy;
    if (native_context_set_geocentric_observer(
            &astronomy, TAIYIN_BODY_EARTH, TAIYIN_BODY_EARTH)
            != TAIYIN_STATUS_OK
        || native_context_set_route_rule(
            &astronomy, TAIYIN_EPHEMERIS_ROUTE_AUTO)
            != TAIYIN_STATUS_OK) {
        return EXIT_FAILURE;
    }

    ChineseCalendarContext beijing_historical;
    ChineseCalendarContext india_historical;
    ChineseCalendarContext india_china_astronomical;
    ChineseCalendarContext india_local_astronomical;
    if (!make_calendar(
            astronomy,
            china_standard_historical_config(8 * 60),
            &beijing_historical)
        || !make_calendar(
            astronomy,
            china_standard_historical_config(5 * 60 + 30),
            &india_historical)
        || !make_calendar(
            astronomy,
            china_standard_astronomical_config(5 * 60 + 30),
            &india_china_astronomical)
        || !make_calendar(
            astronomy,
            local_astronomical_utc_offset_config(5 * 60 + 30),
            &india_local_astronomical)) {
        return EXIT_FAILURE;
    }

    // New moon: 2026-08-12 17:36:45 UT, or 2026-08-13 01:36 in
    // Beijing and 2026-08-12 23:06 in India.
    SplitJulianDate instant_ut;
    if (!julian_day_split({2026, 8, 12, 17, 40, 0.0}, &instant_ut)) {
        return EXIT_FAILURE;
    }

    const bool ok =
        print_lunar(
            "Beijing / China historical",
            beijing_historical,
            instant_ut,
            7,
            1)
        && print_lunar(
            "India / China historical",
            india_historical,
            instant_ut,
            6,
            30)
        && print_lunar(
            "India / China-standard astronomical",
            india_china_astronomical,
            instant_ut,
            6,
            30)
        && print_lunar(
            "India / local astronomical",
            india_local_astronomical,
            instant_ut,
            7,
            1);

    SolarDate reform_date;
    reform_date.year = 23;
    reform_date.month = 12;
    reform_date.day = 2;
    LunarDate reform_lunar;
    EphemerisEvalDiagnostic diagnostic;
    if (fromSolar(
            &india_historical,
            &reform_date,
            &reform_lunar,
            &diagnostic) != TAIYIN_STATUS_OK
        || reform_lunar.month_name
            != TAIYIN_CHINESE_MONTH_NAME_ALT_TWELVE) {
        std::cerr << "historical alternate-twelve month was not preserved\n";
        return EXIT_FAILURE;
    }
    std::cout << "Historical alternate month: "
              << static_cast<int>(reform_lunar.month) << '-'
              << static_cast<int>(reform_lunar.day) << '\n';
    return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
