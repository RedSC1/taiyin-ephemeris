#include "runtime/visibility/planet_visibility_internal.h"

#include "taiyin/body_id.h"
#include "taiyin/runtime/native_context.h"
#include "taiyin/runtime/runtime.h"
#include "taiyin/status.h"
#include "taiyin/time.h"
#include "test_env.h"

#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>

namespace {

const char* kNasaBspRoot = taiyin_test::getenv_path("TAIYIN_NASA_BSP_ROOT");
const char* kDe441Path = taiyin_test::getenv_path("TAIYIN_DE441_PATH");
const char* kJupiterSatellitesSpkPath = taiyin_test::getenv_path("TAIYIN_JUPITER_SATELLITES_SPK_PATH");
const char* kSaturnSatellitesSpkPath = taiyin_test::getenv_path("TAIYIN_SATURN_SATELLITES_SPK_PATH");
const char* kNeptuneSatellitesSpkPath = taiyin_test::getenv_path("TAIYIN_NEPTUNE_SATELLITES_SPK_PATH");
const char* kPlutoSatellitesSpkPath = taiyin_test::getenv_path("TAIYIN_PLUTO_SATELLITES_SPK_PATH");

bool file_exists(const std::string& path) {
    if (path.empty()) return false;
    std::ifstream file(path.c_str(), std::ios::binary);
    return static_cast<bool>(file);
}

std::string join_path(const std::string& root, const std::string& relative) {
    if (root.empty()) return std::string();
    std::string path = root;
    if (!path.empty() && path[path.size() - 1] != '/') {
        path += "/";
    }
    path += relative;
    return path;
}

std::string repo_opm2_major_body_root() {
    const char* root = std::getenv("TAIYIN_REPO_ROOT");
    if (root && root[0] != '\0') {
        return std::string(root) + "/data/ephemerides/opm2/major-bodies/600y";
    }
    const std::string from_repo_root = "data/ephemerides/opm2/major-bodies/600y";
    if (file_exists(join_path(from_repo_root, "sun.opm2"))) {
        return from_repo_root;
    }
    return "../data/ephemerides/opm2/major-bodies/600y";
}

std::string repo_opm2_cob_full_root() {
    const char* root = std::getenv("TAIYIN_REPO_ROOT");
    if (root && root[0] != '\0') {
        return std::string(root) + "/data/ephemerides/opm2/cob/full";
    }
    const std::string from_repo_root = "data/ephemerides/opm2/cob/full";
    if (file_exists(join_path(from_repo_root, "jupiter_cob.opm2"))) {
        return from_repo_root;
    }
    return "../data/ephemerides/opm2/cob/full";
}

std::string de441_path() {
    if (kDe441Path && kDe441Path[0] != '\0') {
        return std::string(kDe441Path);
    }
    if (kNasaBspRoot && kNasaBspRoot[0] != '\0') {
        return join_path(kNasaBspRoot, "planetary/de441.bsp");
    }
    return std::string();
}

std::string satellite_spk_path(const char* explicit_path, const char* relative_path) {
    if (explicit_path && explicit_path[0] != '\0') {
        return std::string(explicit_path);
    }
    if (kNasaBspRoot && kNasaBspRoot[0] != '\0') {
        return join_path(kNasaBspRoot, relative_path);
    }
    return std::string();
}

double jd_ut(int year, int month, int day, double hour) {
    return taiyin::julian_day({year, month, day, static_cast<int>(hour), 0, (hour - static_cast<int>(hour)) * 3600.0});
}

taiyin::runtime::NativeCalcContext make_context() {
    taiyin::runtime::NativeCalcContext context;
    taiyin::runtime::native_context_set_geocentric_observer(
        &context,
        taiyin::TAIYIN_BODY_EARTH,
        taiyin::TAIYIN_BODY_EARTH);
    taiyin::runtime::native_context_set_observer_location(
        &context,
        taiyin::runtime::native_observer_location_degrees(-104.9903, 39.7392, 1609.0));
    taiyin::runtime::native_context_use_solar_deflector(&context);
    context.apparent_options.flags =
        taiyin::TAIYIN_APPARENT_SPHERICAL
        | taiyin::TAIYIN_APPARENT_LIGHT_TIME
        | taiyin::TAIYIN_APPARENT_ABERRATION
        | taiyin::TAIYIN_APPARENT_DEFLECTION;
    context.apparent_options.output_frame_id = taiyin::TAIYIN_APPARENT_FRAME_TRUE_ECLIPTIC_OF_DATE;
    return context;
}

bool initialize_opm2_runtime() {
    const std::string major_root = repo_opm2_major_body_root();
    const std::string cob_root = repo_opm2_cob_full_root();
    const char* source_paths[] = { major_root.c_str(), cob_root.c_str() };
    taiyin::runtime::EphemerisRuntimeConfig config;
    config.source_paths = source_paths;
    config.source_path_count = 2;
    config.load_packaged_data = false;
    config.segment_cache_max_entries = 256;
    return taiyin::runtime::initialize_global_ephemeris_runtime(config);
}

bool initialize_spk_runtime(const std::string& planetary_spk, const std::string& satellite_spk) {
    const char* source_paths[] = { planetary_spk.c_str(), satellite_spk.c_str() };
    taiyin::runtime::EphemerisRuntimeConfig config;
    config.source_paths = source_paths;
    config.source_path_count = 2;
    config.load_packaged_data = false;
    config.segment_cache_max_entries = 256;
    return taiyin::runtime::initialize_global_ephemeris_runtime(config);
}

void expect_true(bool value, const std::string& label, int* failures) {
    if (!value) {
        std::cerr << "FAIL: expected true: " << label << "\n";
        ++(*failures);
    }
}

void expect_status(taiyin::Status actual, taiyin::Status expected, const std::string& label, int* failures) {
    if (actual != expected) {
        std::cerr << "FAIL: " << label << ": actual=" << actual << " expected=" << expected << "\n";
        ++(*failures);
    }
}

void expect_near_seconds(double actual_jd, double expected_jd, double tolerance_seconds, const std::string& label, int* failures) {
    const double diff_seconds = std::fabs(actual_jd - expected_jd) * taiyin::SECONDS_PER_DAY;
    if (!(diff_seconds <= tolerance_seconds)) {
        std::cerr << "FAIL: " << label << ": actual=" << actual_jd
                  << " expected=" << expected_jd
                  << " diff_seconds=" << diff_seconds
                  << " tolerance_seconds=" << tolerance_seconds << "\n";
        ++(*failures);
    }
}

struct EventResult {
    taiyin::Status status;
    int altitude_state;
    int crossing_direction;
    double jd_ut;
};

EventResult search_event(int body_id, int event_kind, double start, double end) {
    taiyin::runtime::NativeCalcContext context = make_context();
    taiyin::runtime::EphemerisEvalDiagnostic diagnostic;
    taiyin::runtime::VisibilityAltitudeSearchResult result;
    EventResult out;
    out.status = taiyin::TAIYIN_STATUS_OK;
    out.altitude_state = taiyin::runtime::TAIYIN_PLANET_VISIBILITY_ALTITUDE_STATE_NOT_FOUND;
    out.crossing_direction = taiyin::runtime::TAIYIN_VISIBILITY_CROSSING_ANY;
    out.jd_ut = NAN;
    taiyin::SplitJulianDate split_start;
    taiyin::SplitJulianDate split_end;
    taiyin::split_julian_date_from_double(start, &split_start);
    taiyin::split_julian_date_from_double(end, &split_end);
    if (event_kind == taiyin::runtime::TAIYIN_PLANET_VISIBILITY_EVENT_RISE
        || event_kind == taiyin::runtime::TAIYIN_PLANET_VISIBILITY_EVENT_SET) {
        out.status = taiyin::runtime::planet_visibility_search_rise_set_ut(
            &context,
            body_id,
            split_start,
            split_end,
            event_kind,
            taiyin::runtime::TAIYIN_PLANET_VISIBILITY_LIMB_UPPER,
            0u,
            &result,
            &diagnostic);
    } else {
        out.status = taiyin::runtime::planet_visibility_search_transit_ut(
            &context,
            body_id,
            split_start,
            split_end,
            event_kind,
            0u,
            &result,
            &diagnostic);
    }
    if (out.status == taiyin::TAIYIN_STATUS_OK) {
        out.altitude_state = result.altitude_state;
        out.crossing_direction = result.crossing_direction;
        out.jd_ut = taiyin::split_julian_date_to_double(result.jd_ut);
    }
    return out;
}

struct PlanetCase {
    const char* label;
    int body_id;
    const char* explicit_spk_path;
    const char* relative_spk_path;
};

struct EventCase {
    const char* label;
    int event_kind;
    double tolerance_seconds;
};

void check_planet_case(
    const PlanetCase& planet,
    const std::string& planetary_spk,
    double start,
    double end,
    int* checked,
    int* failures
) {
    const std::string satellite_spk = satellite_spk_path(planet.explicit_spk_path, planet.relative_spk_path);
    if (!file_exists(satellite_spk)) {
        std::cout << "SKIP: missing " << planet.label << " SPK; set TAIYIN_NASA_BSP_ROOT or body-specific SPK env var\n";
        return;
    }

    const EventCase events[] = {
        { "rise", taiyin::runtime::TAIYIN_PLANET_VISIBILITY_EVENT_RISE, 90.0 },
        { "set", taiyin::runtime::TAIYIN_PLANET_VISIBILITY_EVENT_SET, 90.0 },
        { "upper transit", taiyin::runtime::TAIYIN_PLANET_VISIBILITY_EVENT_UPPER_TRANSIT, 30.0 },
    };

    for (size_t i = 0; i < sizeof(events) / sizeof(events[0]); ++i) {
        const std::string label = std::string(planet.label) + " " + events[i].label;
        if (!initialize_opm2_runtime()) {
            std::cerr << "FAIL: initialize OPM2 runtime for " << label << "\n";
            ++(*failures);
            continue;
        }
        const EventResult opm2 = search_event(planet.body_id, events[i].event_kind, start, end);

        if (!initialize_spk_runtime(planetary_spk, satellite_spk)) {
            std::cerr << "FAIL: initialize SPK runtime for " << label << "\n";
            ++(*failures);
            continue;
        }
        const EventResult spk = search_event(planet.body_id, events[i].event_kind, start, end);

        expect_status(opm2.status, taiyin::TAIYIN_STATUS_OK, label + " OPM2 status", failures);
        expect_status(spk.status, taiyin::TAIYIN_STATUS_OK, label + " SPK status", failures);
        expect_true(
            opm2.altitude_state == taiyin::runtime::TAIYIN_PLANET_VISIBILITY_ALTITUDE_STATE_CROSSES,
            label + " OPM2 crosses",
            failures);
        expect_true(
            spk.altitude_state == taiyin::runtime::TAIYIN_PLANET_VISIBILITY_ALTITUDE_STATE_CROSSES,
            label + " SPK crosses",
            failures);
        if (opm2.status == taiyin::TAIYIN_STATUS_OK
            && spk.status == taiyin::TAIYIN_STATUS_OK
            && std::isfinite(opm2.jd_ut)
            && std::isfinite(spk.jd_ut)) {
            expect_near_seconds(opm2.jd_ut, spk.jd_ut, events[i].tolerance_seconds, label + " OPM2-vs-SPK", failures);
        }
        ++(*checked);
    }
}

}  // namespace

int main() {
    const std::string planetary_spk = de441_path();
    if (!file_exists(planetary_spk)) {
        std::cout << "test_planet_visibility_spk_oracles: SKIPPED external DE441 SPK data absent\n";
        return 0;
    }

    const PlanetCase planets[] = {
        { "Jupiter", taiyin::TAIYIN_BODY_JUPITER, kJupiterSatellitesSpkPath, "satellites/jup365.bsp" },
        { "Saturn", taiyin::TAIYIN_BODY_SATURN, kSaturnSatellitesSpkPath, "satellites/sat441.bsp" },
        { "Neptune", taiyin::TAIYIN_BODY_NEPTUNE, kNeptuneSatellitesSpkPath, "satellites/nep097.bsp" },
        { "Pluto", taiyin::TAIYIN_BODY_PLUTO, kPlutoSatellitesSpkPath, "satellites/plu060.bsp" },
    };

    int failures = 0;
    int checked = 0;
    const double start = jd_ut(2024, 4, 8, 6.0);
    const double end = start + 1.0;
    for (size_t i = 0; i < sizeof(planets) / sizeof(planets[0]); ++i) {
        check_planet_case(planets[i], planetary_spk, start, end, &checked, &failures);
    }

    if (failures == 0) {
        if (checked == 0) {
            std::cout << "test_planet_visibility_spk_oracles: SKIPPED external satellite SPK data absent\n";
        } else {
            std::cout << "test_planet_visibility_spk_oracles: ALL TESTS PASSED (" << checked << " events)\n";
        }
        return 0;
    }
    std::cerr << failures << " test_planet_visibility_spk_oracles failure(s)\n";
    return 1;
}
