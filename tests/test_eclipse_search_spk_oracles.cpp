#include "taiyin/body_id.h"
#include "taiyin/dispatch.h"
#include "taiyin/runtime/eclipse_search.h"
#include "taiyin/runtime/native_context.h"
#include "taiyin/runtime/runtime.h"
#include "test_env.h"

#include <cmath>
#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

namespace {

const char* kNasaBspRoot = taiyin_test::getenv_path("TAIYIN_NASA_BSP_ROOT");
const char* kDe441Path = taiyin_test::getenv_path("TAIYIN_DE441_PATH");

bool file_exists(const std::string& path) {
    if (path.empty()) return false;
    std::ifstream file(path.c_str(), std::ios::binary);
    return static_cast<bool>(file);
}

std::string join_path(const std::string& root, const std::string& relative) {
    if (root.empty()) return std::string();
    std::string path = root;
    if (!path.empty() && path[path.size() - 1] != '/') path += "/";
    path += relative;
    return path;
}

std::string de441_path() {
    if (kDe441Path && kDe441Path[0] != '\0') return std::string(kDe441Path);
    if (kNasaBspRoot && kNasaBspRoot[0] != '\0') return join_path(kNasaBspRoot, "planetary/de441.bsp");
    return std::string();
}

double jd_year(double year) {
    return 2451545.0 + (year - 2000.0) * 365.2425;
}

taiyin::SplitJulianDate split_jd(double value) {
    taiyin::SplitJulianDate result;
    taiyin::split_julian_date_from_double(value, &result);
    return result;
}

taiyin::runtime::NativeCalcContext make_context() {
    taiyin::runtime::NativeCalcContext context;
    taiyin::runtime::native_context_set_geocentric_observer(
        &context,
        taiyin::TAIYIN_BODY_EARTH,
        taiyin::TAIYIN_BODY_EARTH);
    taiyin::runtime::native_context_use_solar_deflector(&context);
    taiyin::runtime::native_context_set_route_rule(
        &context,
        taiyin::runtime::TAIYIN_EPHEMERIS_ROUTE_SPK);
    context.apparent_options.flags = taiyin::TAIYIN_APPARENT_SPHERICAL
                                   | taiyin::TAIYIN_APPARENT_LIGHT_TIME
                                   | taiyin::TAIYIN_APPARENT_ABERRATION
                                   | taiyin::TAIYIN_APPARENT_DEFLECTION;
    context.apparent_options.output_frame_id = taiyin::TAIYIN_APPARENT_FRAME_TRUE_ECLIPTIC_OF_DATE;
    context.eclipse_shadow_model_id = static_cast<uint8_t>(taiyin::dispatch::ECLIPSE_SHADOW_CHAUVENET);
    context.eclipse_moon_radius_model_id = static_cast<uint8_t>(taiyin::dispatch::ECLIPSE_MOON_ALMANAC);
    return context;
}

bool initialize_spk_runtime(const std::string& de441) {
    const char* source_paths[] = {de441.c_str()};
    taiyin::runtime::EphemerisRuntimeConfig config;
    config.source_paths = source_paths;
    config.source_path_count = 1;
    config.load_packaged_data = false;
    config.segment_cache_max_entries = 512;
    return taiyin::runtime::initialize_global_ephemeris_runtime(config);
}

int expect_far_solar_events(
    const taiyin::runtime::NativeCalcContext& context,
    double start_year,
    double end_year,
    const char* label
) {
    std::vector<taiyin::runtime::SolarEclipseResult> results(512);
    size_t count = 0;
    taiyin::runtime::EphemerisEvalDiagnostic diag{};
    const taiyin::Status st = taiyin::runtime::search_solar_eclipses_tt(
        &context,
        split_jd(jd_year(start_year)),
        split_jd(jd_year(end_year)),
        taiyin::runtime::TAIYIN_ECLIPSE_ALL_SOLAR,
        taiyin::runtime::TAIYIN_ECLIPSE_INCLUDE_CONTACTS | taiyin::runtime::TAIYIN_ECLIPSE_TRUEPOS,
        results.data(),
        results.size(),
        &count,
        &diag);
    if (st != taiyin::TAIYIN_STATUS_OK) {
        std::printf("FAIL: %s search status=%d\n", label, st);
        return 1;
    }
    if (count == 0) {
        std::printf("FAIL: %s returned no solar eclipses\n", label);
        return 1;
    }
    return 0;
}

}  // namespace

int main() {
    const std::string de441 = de441_path();
    if (!file_exists(de441)) {
        std::printf("test_eclipse_search_spk_oracles: SKIPPED external DE441 SPK data absent\n");
        return 0;
    }
    if (!initialize_spk_runtime(de441)) {
        std::printf("FAIL: initialize DE441 SPK runtime\n");
        return 1;
    }

    const auto context = make_context();
    int failures = 0;
    failures += expect_far_solar_events(context, -12000.0, -11998.0, "ancient far solar search");
    failures += expect_far_solar_events(context, 16000.0, 16002.0, "future far solar search");
    return failures == 0 ? 0 : 1;
}
