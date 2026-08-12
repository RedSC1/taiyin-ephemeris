/*
中文说明
--------
利簋铭文通常释写为：“珷（武）征商，隹（唯）甲子朝，歲鼎，克昏夙有商。”
中国国家博物馆的通俗说明把“歲鼎”与岁星（木星）联系起来，并采用
公元前 1046 年的武王克商年代口径。国家民委转载的官方介绍所附天象图明确
题为“公元前1046年1月20日甲子子夜朝歌地方的天象”。本例沿用该日期，在
牧野候选地点用 Taiyin 内置半解析星历复算观测天象。计算所得木星在 UTC+08
约 00:13（当地平太阳时约前一日 23:50）上中天，与官方图所示的“甲子子夜
木星中天”相符；到 06:30 木星已经西斜，日出时已经落到地平线下。因此，
本例不把 06:30 或日出时刻本身描述为“木星上中天”。它只展示指定日期、
地点与模型下的星历几何，不裁决铭文释读或历史断代。

English note
------------
The Li gui inscription is commonly transcribed as: "Wu marched against Shang;
on the morning of the Jia-Zi day, sui ding; by evening Shang was taken."
Popular material from the National Museum of China relates "sui ding" to
Sui-xing (Jupiter) and uses the 1046 BCE chronology. An officially published
reconstruction is explicitly captioned as the sky at Chaoge at midnight on
the Jia-Zi day, 1046 BCE-01-20. This example reuses that date and evaluates the
observed sky at a candidate Muye site with Taiyin's built-in semi-analytic
ephemeris. Jupiter reaches upper culmination near 00:13 UTC+08 (about 23:50
local mean solar time), consistent with the official midnight reconstruction.
By 06:30 it is already descending in the western sky, and it is below the
horizon at sunrise; the example therefore does not describe 06:30 or sunrise
itself as Jupiter's upper culmination. It demonstrates ephemeris geometry only
and does not adjudicate the inscription's interpretation or the historical
chronology.

National Museum of China reference:
https://www.chnmuseum.cn/Portals/0/web/zt/100n/guobao_content-5.html?id=27
https://www.neac.gov.cn/seac/c103391/202306/1164738.shtml
*/

#include "taiyin/angle.h"
#include "taiyin/body_id.h"
#include "taiyin/runtime/ephemeris_route.h"
#include "taiyin/runtime/native_context.h"
#include "taiyin/runtime/observed_position.h"
#include "taiyin/runtime/planet_visibility.h"
#include "taiyin/runtime/runtime.h"
#include "taiyin/runtime/solar_visibility.h"
#include "taiyin/status.h"
#include "taiyin/time.h"

#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <string>

namespace {

using namespace taiyin;
using namespace taiyin::runtime;

const double kLongitudeDeg = 114.10;
const double kLatitudeDeg = 35.50;
const double kHeightM = 80.0;
const double kUtcOffsetHours = 8.0;

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

SplitJulianDate local_clock_to_ut(
    int32_t year,
    uint8_t month,
    uint8_t day,
    uint8_t hour,
    uint8_t minute,
    double second
) {
    SplitJulianDate local;
    SplitJulianDate ut;
    if (!julian_day_split(
            {year, month, day, hour, minute, second}, &local)
        || !add_seconds_to_split_jd(
            local, -kUtcOffsetHours * 3600.0, &ut)) {
        return SplitJulianDate();
    }
    return ut;
}

void print_local_clock(const SplitJulianDate& jd_ut) {
    SplitJulianDate local;
    SplitJulianDate rounded;
    CalendarDateTime value = {};
    add_seconds_to_split_jd(jd_ut, kUtcOffsetHours * 3600.0, &local);
    // Round to the nearest displayed second before reversing the JD. This also
    // handles carries at xx:xx:59.999 without printing a synthetic :60.
    add_seconds_to_split_jd(local, 0.5, &rounded);
    reverse_julian_day_split(rounded, &value);
    std::cout << value.year << '-'
              << std::setw(2) << std::setfill('0') << value.month << '-'
              << std::setw(2) << value.day << ' '
              << std::setw(2) << value.hour << ':'
              << std::setw(2) << value.minute << ':'
              << std::setw(2) << static_cast<int>(value.second)
              << std::setfill(' ');
}

bool observed_sun_and_jupiter(
    const NativeCalcContext& context,
    const SplitJulianDate& jd_ut,
    ObservedPosition out[2]
) {
    const int body_ids[] = {
        TAIYIN_BODY_SUN,
        TAIYIN_BODY_JUPITER,
    };
    EphemerisEvalDiagnostic diagnostics[2];
    const uint64_t flags =
        TAIYIN_OBSERVED_TOPOCENTRIC
        | TAIYIN_OBSERVED_HORIZONTAL
        | TAIYIN_OBSERVED_REFRACTION
        | TAIYIN_OBSERVED_ALLOW_BARYCENTER_APPROX;
    return check(
        calc_observed_ut(
            &context, jd_ut, body_ids, 2, flags, out, diagnostics),
        "calc_observed_ut");
}

void print_event(
    const NativeCalcContext& context,
    const char* label,
    const SplitJulianDate& jd_ut
) {
    ObservedPosition observed[2];
    if (!observed_sun_and_jupiter(context, jd_ut, observed)) return;
    std::cout << std::left << std::setw(20) << label << std::right;
    print_local_clock(jd_ut);
    std::cout << std::fixed << std::setprecision(2)
              << "  Sun alt=" << std::setw(7)
              << observed[0].horizontal.altitude_rad * TAIYIN_RAD_TO_DEG
              << " deg"
              << "  Jupiter alt=" << std::setw(7)
              << observed[1].horizontal.altitude_rad * TAIYIN_RAD_TO_DEG
              << " deg"
              << "  az=" << std::setw(7)
              << observed[1].horizontal.azimuth_rad * TAIYIN_RAD_TO_DEG
              << " deg\n";
}

}  // namespace

int main(int argc, char** argv) {
    using namespace taiyin;
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

    NativeCalcContext context;
    if (!check(
            native_context_set_geocentric_observer(
                &context, TAIYIN_BODY_EARTH, TAIYIN_BODY_EARTH),
            "native_context_set_geocentric_observer")
        || !check(
            native_context_set_observer_location(
                &context,
                native_observer_location_degrees(
                    kLongitudeDeg, kLatitudeDeg, kHeightM)),
            "native_context_set_observer_location")
        || !check(
            native_context_set_atmosphere(
                &context, native_standard_atmosphere()),
            "native_context_set_atmosphere")
        || !check(
            native_context_use_solar_deflector(&context),
            "native_context_use_solar_deflector")
        || !check(
            native_context_set_route_rule(
                &context, TAIYIN_EPHEMERIS_ROUTE_SEMI_ANALYTIC),
            "native_context_set_route_rule")) {
        return 1;
    }
    context.apparent_options.flags =
        TAIYIN_APPARENT_SPHERICAL
        | TAIYIN_APPARENT_LIGHT_TIME
        | TAIYIN_APPARENT_ABERRATION
        | TAIYIN_APPARENT_DEFLECTION;
    context.apparent_options.output_frame_id =
        TAIYIN_APPARENT_FRAME_TRUE_ECLIPTIC_OF_DATE;

    // Astronomical year -1045 is 1046 BCE. Taiyin's civil-date conversion
    // uses the Julian calendar before the 1582 reform.
    const SplitJulianDate midnight =
        local_clock_to_ut(-1045, 1, 20, 0, 0, 0.0);
    const SplitJulianDate chart_time =
        local_clock_to_ut(-1045, 1, 20, 6, 30, 0.0);
    const SplitJulianDate next_midnight = midnight + 1.0;
    PlanetVisibilityEventResult culmination;
    EphemerisEvalDiagnostic culmination_diagnostic;
    if (!check(
            search_planet_transit_ut(
                &context,
                TAIYIN_BODY_JUPITER,
                midnight,
                next_midnight,
                TAIYIN_PLANET_VISIBILITY_EVENT_UPPER_TRANSIT,
                TAIYIN_NATIVE_POSITION_ALLOW_BARYCENTER_APPROX,
                &culmination,
                &culmination_diagnostic),
            "search_planet_transit_ut")) {
        return 1;
    }

    SolarVisibilityEventResult sunrise;
    EphemerisEvalDiagnostic diagnostic;
    if (!check(
            search_solar_rise_set_ut(
                &context,
                midnight,
                next_midnight,
                TAIYIN_SOLAR_VISIBILITY_EVENT_RISE,
                TAIYIN_SOLAR_VISIBILITY_LIMB_UPPER,
                TAIYIN_SOLAR_VISIBILITY_FLAG_REFRACTION,
                &sunrise,
                &diagnostic),
            "search_solar_rise_set_ut")) {
        return 1;
    }

    std::cout << "Muye Jupiter example\n"
              << "Location: 35.50 N, 114.10 E, height 80 m\n"
              << "Date: 1046 BCE-01-20 (astronomical year -1045, Julian)\n"
              << "Clock: UTC+08\n"
              << "Ephemeris: built-in semi-analytic; physical Jupiter request "
                 "with barycenter approximation when required\n"
              << "Azimuth convention: north=0 deg, east=90 deg\n\n";
    print_event(context, "Local midnight", midnight);
    print_event(context, "Jupiter culmination", culmination.jd_ut);
    print_event(context, "Four-pillar time", chart_time);
    print_event(context, "Sunrise", sunrise.jd_ut);
    return 0;
}
