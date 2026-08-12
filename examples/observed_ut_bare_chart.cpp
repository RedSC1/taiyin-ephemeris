#include "taiyin/angle.h"
#include "taiyin/body_id.h"
#include "taiyin/dispatch.h"
#include "taiyin/runtime/observed_position.h"
#include "taiyin/runtime/runtime.h"
#include "taiyin/time.h"

#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <string>

namespace {

using namespace taiyin;
using namespace taiyin::runtime;

struct BodySpec {
    const char* name;
    int body_id;
};

const char* get_data_root(int argc, char** argv) {
    if (argc > 1 && argv[1] && argv[1][0] != '\0') {
        return argv[1];
    }
    const char* env = std::getenv("TAIYIN_DATA_ROOT");
    if (env && env[0] != '\0') {
        return env;
    }
    return "data";
}

void print_usage(const char* program) {
    std::cout << "usage: " << program << " [data_root]\n"
              << "\n"
              << "If data_root is omitted, the example uses TAIYIN_DATA_ROOT or ./data.\n"
              << "This example initializes the packaged runtime, sets a Beijing observer\n"
              << "context, then prints a small topocentric observed bare chart for\n"
              << "1046 BCE-01-20 06:30 UT (astronomical year -1045).\n";
}

}  // namespace

int main(int argc, char** argv) {
    if (argc > 1 && argv[1] && std::string(argv[1]) == "--help") {
        print_usage(argv[0]);
        return 0;
    }

    const char* data_root = get_data_root(argc, argv);

    EphemerisRuntimeConfig config;
    config.segment_cache_max_entries = 4096;
    config.data_root = data_root;
    config.load_packaged_data = true;
    if (!initialize_global_ephemeris_runtime(config)) {
        std::cerr << "failed to initialize runtime from data root: " << data_root << "\n";
        return 1;
    }

    NativeCalcContext context;
    const NativeObserverLocation beijing = native_observer_location_degrees(116.4074, 39.9042, 43.5);
    if (native_context_set_observer_location(&context, beijing) != TAIYIN_STATUS_OK
        || native_context_set_atmosphere_pressure_temperature(&context, 1010.0, 10.0) != TAIYIN_STATUS_OK
        || native_context_set_refraction_model(&context, dispatch::REFRACTION_BENNETT) != TAIYIN_STATUS_OK) {
        std::cerr << "failed to configure native calculation context\n";
        return 1;
    }

    const BodySpec bodies[] = {
        { "Sun", TAIYIN_BODY_SUN },
        { "Moon", TAIYIN_BODY_MOON },
        { "Mercury BC", TAIYIN_BODY_MERCURY_BARYCENTER },
        { "Venus BC", TAIYIN_BODY_VENUS_BARYCENTER },
        { "Mars BC", TAIYIN_BODY_MARS_BARYCENTER },
        { "Jupiter BC", TAIYIN_BODY_JUPITER_BARYCENTER },
        { "Saturn BC", TAIYIN_BODY_SATURN_BARYCENTER },
        { "Uranus BC", TAIYIN_BODY_URANUS_BARYCENTER },
        { "Neptune BC", TAIYIN_BODY_NEPTUNE_BARYCENTER },
        { "Pluto BC", TAIYIN_BODY_PLUTO_BARYCENTER },
    };
    const size_t body_count = sizeof(bodies) / sizeof(bodies[0]);
    int body_ids[sizeof(bodies) / sizeof(bodies[0])];
    for (size_t i = 0; i < body_count; ++i) {
        body_ids[i] = bodies[i].body_id;
    }

    // CalendarDateTime uses astronomical year numbering: year 0 is 1 BCE,
    // therefore historical 1046 BCE is represented as year -1045.
    const CalendarDateTime datetime_ut = { -1045, 1, 20, 6, 30, 0.0 };
    const double jd_ut = julian_day(datetime_ut);
    SplitJulianDate split_jd_ut;
    if (!split_julian_date_from_double(jd_ut, &split_jd_ut)) {
        std::cerr << "failed to convert chart date to split JD\n";
        return 1;
    }

    ObservedPosition observed[sizeof(bodies) / sizeof(bodies[0])];
    EphemerisEvalDiagnostic diagnostics[sizeof(bodies) / sizeof(bodies[0])];
    const uint64_t flags =
        TAIYIN_OBSERVED_SPEED
        | TAIYIN_OBSERVED_TOPOCENTRIC
        | TAIYIN_OBSERVED_HORIZONTAL
        | TAIYIN_OBSERVED_REFRACTION;
    const Status status = calc_observed_ut(
        &context,
        split_jd_ut,
        body_ids,
        body_count,
        flags,
        observed,
        diagnostics);
    if (status != TAIYIN_STATUS_OK) {
        std::cerr << "calc_observed_ut failed: " << status_name(status) << "\n";
        for (size_t i = 0; i < body_count; ++i) {
            if (diagnostics[i].status != TAIYIN_STATUS_OK) {
                std::cerr << "  " << bodies[i].name
                          << ": " << status_name(diagnostics[i].status)
                          << " target=" << diagnostics[i].target_id
                          << " center=" << diagnostics[i].center_id << "\n";
            }
        }
        return 1;
    }

    std::cout << std::fixed;
    std::cout << "Taiyin observed UT bare chart\n";
    std::cout << "data_root: " << data_root << "\n";
    std::cout << "date: 1046 BCE-01-20 06:30 UT (astronomical year -1045)\n";
    std::cout << "chart_ut_jd: " << std::setprecision(9) << jd_ut << "\n";
    std::cout << "observer: Beijing lon=116.4074 lat=39.9042 height=43.5m\n";
    std::cout << "atmosphere: Bennett pressure=1010mbar temperature=10C\n\n";

    std::cout << std::left << std::setw(12) << "Body"
              << std::right << std::setw(13) << "RA(deg)"
              << std::setw(13) << "Dec(deg)"
              << std::setw(13) << "Az(deg)"
              << std::setw(13) << "Alt(deg)"
              << std::setw(17) << "RefrAlt(deg)"
              << std::setw(13) << "Dist(AU)"
              << "\n";
    std::cout << std::string(98, '-') << "\n";

    for (size_t i = 0; i < body_count; ++i) {
        std::cout << std::left << std::setw(12) << bodies[i].name << std::right;
        std::cout << std::setw(13) << std::setprecision(5) << normalize_degrees(rad_to_deg(observed[i].apparent.longitude_rad))
                  << std::setw(13) << std::setprecision(5) << rad_to_deg(observed[i].apparent.latitude_rad)
                  << std::setw(13) << std::setprecision(5) << rad_to_deg(observed[i].horizontal.azimuth_rad)
                  << std::setw(13) << std::setprecision(5) << rad_to_deg(observed[i].horizontal.altitude_rad)
                  << std::setw(17) << std::setprecision(5) << rad_to_deg(observed[i].refracted_horizontal.altitude_rad)
                  << std::setw(13) << std::setprecision(6) << observed[i].apparent.distance_au
                  << "\n";
    }

    return 0;
}
