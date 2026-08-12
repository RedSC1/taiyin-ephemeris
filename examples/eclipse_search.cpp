#include "taiyin/body_id.h"
#include "taiyin/dispatch.h"
#include "taiyin/runtime/eclipse_search.h"
#include "taiyin/runtime/native_context.h"
#include "taiyin/runtime/runtime.h"
#include "taiyin/status.h"
#include "taiyin/time.h"

#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>

namespace {

using namespace taiyin;
using namespace taiyin::runtime;

const char* get_data_root(int argc, char** argv) {
    if (argc > 1 && argv[1] && argv[1][0] != '\0') return argv[1];
    const char* env = std::getenv("TAIYIN_DATA_ROOT");
    return env && env[0] != '\0' ? env : "data";
}

bool split_date(const CalendarDateTime& date, SplitJulianDate* out) {
    return out && julian_day_split(date, out);
}

void print_date(const SplitJulianDate& jd) {
    CalendarDateTime date = {};
    if (!reverse_julian_day_split(jd, &date)) {
        std::cout << "invalid time";
        return;
    }
    std::cout << std::setfill('0')
              << std::setw(4) << date.year << '-'
              << std::setw(2) << date.month << '-'
              << std::setw(2) << date.day << ' '
              << std::setw(2) << date.hour << ':'
              << std::setw(2) << date.minute << ':'
              << std::fixed << std::setprecision(1) << std::setw(4) << date.second
              << std::setfill(' ');
}

NativeCalcContext make_eclipse_context() {
    NativeCalcContext context;
    native_context_set_geocentric_observer(
        &context,
        TAIYIN_BODY_EARTH,
        TAIYIN_BODY_EARTH);
    native_context_set_atmosphere(&context, native_standard_atmosphere());
    native_context_use_solar_deflector(&context);
    context.apparent_options.flags =
        TAIYIN_APPARENT_SPHERICAL
        | TAIYIN_APPARENT_LIGHT_TIME
        | TAIYIN_APPARENT_ABERRATION
        | TAIYIN_APPARENT_DEFLECTION;
    context.apparent_options.output_frame_id =
        TAIYIN_APPARENT_FRAME_TRUE_ECLIPTIC_OF_DATE;
    context.eclipse_shadow_model_id =
        static_cast<uint8_t>(dispatch::ECLIPSE_SHADOW_CHAUVENET);
    context.eclipse_moon_radius_model_id =
        static_cast<uint8_t>(dispatch::ECLIPSE_MOON_ALMANAC);
    return context;
}

bool check(Status status, const char* operation) {
    if (status == TAIYIN_STATUS_OK) return true;
    std::cerr << operation << " failed: " << status_name(status) << '\n';
    return false;
}

}  // namespace

int main(int argc, char** argv) {
    using namespace taiyin;
    using namespace taiyin::runtime;

    if (argc > 1 && argv[1] && std::string(argv[1]) == "--help") {
        std::cout << "usage: " << argv[0] << " [data_root]\n"
                  << "Uses data_root, TAIYIN_DATA_ROOT, or ./data (in that order).\n";
        return 0;
    }

    const char* data_root = get_data_root(argc, argv);
    EphemerisRuntimeConfig config;
    config.data_root = data_root;
    config.load_packaged_data = true;
    config.segment_cache_max_entries = 4096;
    if (!initialize_global_ephemeris_runtime(config)) {
        std::cerr << "failed to initialize runtime from data root: " << data_root << '\n';
        return 1;
    }

    NativeCalcContext context = make_eclipse_context();
    EphemerisEvalDiagnostic diagnostic = {};
    const uint64_t flags = TAIYIN_ECLIPSE_INCLUDE_CONTACTS;

    SplitJulianDate lunar_start;
    if (!split_date({2025, 1, 1, 0, 0, 0.0}, &lunar_start)) return 1;
    LunarEclipseResultUt lunar = {};
    if (!check(
            search_next_lunar_eclipse_ut(
                &context,
                lunar_start,
                TAIYIN_ECLIPSE_TOTAL,
                flags,
                &lunar,
                &diagnostic),
            "search_next_lunar_eclipse_ut")) {
        return 1;
    }
    std::cout << "Next total lunar eclipse: ";
    print_date(lunar.maximum_jd_ut);
    std::cout << " UT, umbral magnitude=" << std::setprecision(6)
              << lunar.umbral_magnitude << "\n";

    SplitJulianDate solar_start;
    if (!split_date({2024, 1, 1, 0, 0, 0.0}, &solar_start)) return 1;
    SolarEclipseResultUt solar = {};
    if (!check(
            search_next_solar_eclipse_ut(
                &context,
                solar_start,
                TAIYIN_ECLIPSE_TOTAL,
                flags,
                &solar,
                &diagnostic),
            "search_next_solar_eclipse_ut")) {
        return 1;
    }
    std::cout << "Next total solar eclipse: ";
    print_date(solar.maximum_jd_ut);
    std::cout << " UT at " << std::setprecision(5)
              << solar.maximum_latitude_deg << ", "
              << solar.maximum_longitude_deg << "\n";

    NativeCalcContext local_context = context;
    if (!check(
            native_context_set_observer_location(
                &local_context,
                native_observer_location_degrees(-96.7970, 32.7767, 131.0)),
            "native_context_set_observer_location")) {
        return 1;
    }
    LocalSolarEclipseResultUt local = {};
    if (!check(
            solve_local_solar_eclipse_at_ut(
                &local_context,
                solar.maximum_jd_ut,
                flags,
                &local,
                &diagnostic),
            "solve_local_solar_eclipse_at_ut")) {
        return 1;
    }
    std::cout << "Dallas local maximum: ";
    print_date(local.maximum_jd_ut);
    std::cout << " UT, magnitude=" << std::setprecision(6) << local.magnitude
              << ", Sun altitude=" << std::setprecision(3)
              << local.sun_altitude_deg << " deg\n";

    SolarEclipseRouteProductSummary summary = {};
    size_t point_count = 0;
    const size_t route_samples = 128;
    if (!check(
            compute_solar_eclipse_route_map_product_ut_with_options(
                &context,
                solar.maximum_jd_ut,
                0,
                route_samples,
                nullptr,
                0,
                &point_count,
                &summary,
                &diagnostic),
            "route map size query")) {
        return 1;
    }

    std::vector<SolarEclipseRouteProductPoint> points(point_count);
    if (!check(
            compute_solar_eclipse_route_map_product_ut_with_options(
                &context,
                solar.maximum_jd_ut,
                0,
                route_samples,
                points.data(),
                points.size(),
                &point_count,
                &summary,
                &diagnostic),
            "route map calculation")) {
        return 1;
    }
    points.resize(point_count);
    std::cout << "Route map: " << points.size() << " polygon points; "
              << summary.core_polygon_point_count << " core, "
              << summary.penumbral_polygon_point_count << " penumbral, "
              << summary.half_magnitude_polygon_point_count << " half-magnitude\n";

    return 0;
}
