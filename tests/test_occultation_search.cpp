#include "taiyin/runtime/occultation_search.h"

#include "taiyin/angle.h"
#include "taiyin/apparent_position.h"
#include "taiyin/body_id.h"
#include "taiyin/internal/star_file.h"
#include "taiyin/runtime/ephemeris_route.h"
#include "taiyin/runtime/native_context.h"
#include "taiyin/runtime/native_position.h"
#include "taiyin/runtime/observed_position.h"
#include "taiyin/runtime/runtime.h"
#include "taiyin/runtime/star_position.h"
#include "taiyin/physical_constants.h"
#include "taiyin/status.h"
#include "taiyin/time.h"
#include "test_env.h"

#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>
#include <unistd.h>

namespace {

// These bounds are Taiyin-derived sampling snapshots rather than external
// astrometric oracles. GCC and AppleClang libm evaluation differs by several
// microdegrees after the lunar-series replacement, so retain sub-arcsecond
// regression coverage without requiring bitwise cross-toolchain agreement.
constexpr double kDerivedEnvelopeFixtureToleranceDeg = 2.0e-4;
constexpr double kDerivedAltitudeFixtureToleranceRad = 2.0e-7;
constexpr double kRepeatedSearchFixtureToleranceDays = 2.0e-7;

void expect_true(bool value, const char* label, int* failures) {
    if (!value) {
        std::cerr << "FAIL: expected true: " << label << "\n";
        ++(*failures);
    }
}

void expect_equal(int actual, int expected, const char* label, int* failures) {
    if (actual != expected) {
        std::cerr << "FAIL: " << label << ": actual=" << actual
                  << " expected=" << expected << "\n";
        ++(*failures);
    }
}

void expect_status(taiyin::Status actual, taiyin::Status expected, const char* label, int* failures) {
    if (actual != expected) {
        std::cerr << "FAIL: " << label << ": actual=" << actual
                  << " expected=" << expected << "\n";
        ++(*failures);
    }
}

void expect_near(double actual, double expected, double tolerance, const char* label, int* failures) {
    const double diff = std::fabs(actual - expected);
    if (!(diff <= tolerance)) {
        std::cerr << std::setprecision(17)
                  << "FAIL: " << label << ": actual=" << actual
                  << " expected=" << expected
                  << " diff=" << diff
                  << " tolerance=" << tolerance << "\n";
        ++(*failures);
    }
}

taiyin::SplitJulianDate split_jd(double jd) {
    taiyin::SplitJulianDate out;
    if (!taiyin::split_julian_date_from_double(jd, &out)) {
        out.day_number = 0;
        out.day_fraction = std::nan("");
    }
    return out;
}

double scalar_jd(const taiyin::SplitJulianDate& jd) {
    return taiyin::split_julian_date_to_double(jd);
}

void expect_near(
    const taiyin::SplitJulianDate& actual,
    double expected,
    double tolerance,
    const char* label,
    int* failures
) {
    expect_near(scalar_jd(actual), expected, tolerance, label, failures);
}

void expect_near(
    const taiyin::SplitJulianDate& actual,
    const taiyin::SplitJulianDate& expected,
    double tolerance,
    const char* label,
    int* failures
) {
    expect_near(taiyin::days_between_split_jd(actual, expected), 0.0, tolerance, label, failures);
}

bool operator<(const taiyin::SplitJulianDate& lhs, double rhs) { return scalar_jd(lhs) < rhs; }
bool operator<=(const taiyin::SplitJulianDate& lhs, double rhs) { return scalar_jd(lhs) <= rhs; }
bool operator>(const taiyin::SplitJulianDate& lhs, double rhs) { return scalar_jd(lhs) > rhs; }
bool operator>=(const taiyin::SplitJulianDate& lhs, double rhs) { return scalar_jd(lhs) >= rhs; }
bool operator<(double lhs, const taiyin::SplitJulianDate& rhs) { return lhs < scalar_jd(rhs); }
bool operator<=(double lhs, const taiyin::SplitJulianDate& rhs) { return lhs <= scalar_jd(rhs); }
bool operator>(double lhs, const taiyin::SplitJulianDate& rhs) { return lhs > scalar_jd(rhs); }
bool operator>=(double lhs, const taiyin::SplitJulianDate& rhs) { return lhs >= scalar_jd(rhs); }

taiyin::Status search_next_geocentric_lunar_star_occultation_ut(
    const taiyin::runtime::NativeCalcContext* context,
    const char* star_key,
    double jd_start_ut,
    uint64_t flags,
    taiyin::runtime::LunarStarOccultationSearchResult* out,
    taiyin::runtime::EphemerisEvalDiagnostic* diagnostic = nullptr
) noexcept {
    return taiyin::runtime::search_next_geocentric_lunar_star_occultation_ut(
        context, star_key, split_jd(jd_start_ut), flags, out, diagnostic);
}

taiyin::Status search_next_local_lunar_star_occultation_ut(
    const taiyin::runtime::NativeCalcContext* context,
    const char* star_key,
    double jd_start_ut,
    uint64_t flags,
    taiyin::runtime::LunarStarOccultationSearchResult* out,
    taiyin::runtime::EphemerisEvalDiagnostic* diagnostic = nullptr
) noexcept {
    return taiyin::runtime::search_next_local_lunar_star_occultation_ut(
        context, star_key, split_jd(jd_start_ut), flags, out, diagnostic);
}

taiyin::Status search_next_geocentric_lunar_body_occultation_ut(
    const taiyin::runtime::NativeCalcContext* context,
    int body_id,
    double jd_start_ut,
    uint64_t flags,
    taiyin::runtime::LunarBodyOccultationSearchResult* out,
    taiyin::runtime::EphemerisEvalDiagnostic* diagnostic = nullptr
) noexcept {
    return taiyin::runtime::search_next_geocentric_lunar_body_occultation_ut(
        context, body_id, split_jd(jd_start_ut), flags, out, diagnostic);
}

taiyin::Status search_next_geocentric_lunar_body_occultation_ut(
    const taiyin::runtime::NativeCalcContext* context,
    int body_id,
    double target_radius_km,
    double jd_start_ut,
    uint64_t flags,
    taiyin::runtime::LunarBodyOccultationSearchResult* out,
    taiyin::runtime::EphemerisEvalDiagnostic* diagnostic = nullptr
) noexcept {
    return taiyin::runtime::search_next_geocentric_lunar_body_occultation_ut(
        context, body_id, target_radius_km, split_jd(jd_start_ut), flags, out, diagnostic);
}

taiyin::Status search_next_local_lunar_body_occultation_ut(
    const taiyin::runtime::NativeCalcContext* context,
    int body_id,
    double jd_start_ut,
    uint64_t flags,
    taiyin::runtime::LunarBodyOccultationSearchResult* out,
    taiyin::runtime::EphemerisEvalDiagnostic* diagnostic = nullptr
) noexcept {
    return taiyin::runtime::search_next_local_lunar_body_occultation_ut(
        context, body_id, split_jd(jd_start_ut), flags, out, diagnostic);
}

taiyin::Status search_next_local_lunar_body_occultation_ut(
    const taiyin::runtime::NativeCalcContext* context,
    int body_id,
    double target_radius_km,
    double jd_start_ut,
    uint64_t flags,
    taiyin::runtime::LunarBodyOccultationSearchResult* out,
    taiyin::runtime::EphemerisEvalDiagnostic* diagnostic = nullptr
) noexcept {
    return taiyin::runtime::search_next_local_lunar_body_occultation_ut(
        context, body_id, target_radius_km, split_jd(jd_start_ut), flags, out, diagnostic);
}

void expect_occultation_centrality(uint32_t type_flags, const char* label, int* failures) {
    const uint32_t centrality =
        type_flags
        & (taiyin::runtime::TAIYIN_OCCULTATION_TYPE_CENTRAL
           | taiyin::runtime::TAIYIN_OCCULTATION_TYPE_NONCENTRAL);
    if ((type_flags & taiyin::runtime::TAIYIN_OCCULTATION_TYPE_CENTRALITY_UNAVAILABLE) != 0u) {
        std::cerr << "FAIL: " << label << ": centrality unavailable unexpectedly, type_flags="
                  << type_flags << "\n";
        ++(*failures);
        return;
    }
    if (centrality != taiyin::runtime::TAIYIN_OCCULTATION_TYPE_CENTRAL
        && centrality != taiyin::runtime::TAIYIN_OCCULTATION_TYPE_NONCENTRAL) {
        std::cerr << "FAIL: " << label << ": missing exclusive centrality bits, type_flags="
                  << type_flags << "\n";
        ++(*failures);
    }
}

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

std::string repo_opm2_major_body_root() {
    const char* root = std::getenv("TAIYIN_REPO_ROOT");
    if (root && root[0] != '\0') {
        return std::string(root) + "/data/ephemerides/opm2/major-bodies/600y";
    }
    return "../data/ephemerides/opm2/major-bodies/600y";
}

std::string repo_opm2_cob_full_root() {
    const char* root = std::getenv("TAIYIN_REPO_ROOT");
    if (root && root[0] != '\0') {
        return std::string(root) + "/data/ephemerides/opm2/cob/full";
    }
    return "../data/ephemerides/opm2/cob/full";
}

std::string repo_opm2_uranus_cob_slice_root() {
    const char* root = std::getenv("TAIYIN_REPO_ROOT");
    if (root && root[0] != '\0') {
        return std::string(root) + "/data/ephemerides/opm2/cob/slices/uranus_1600_2200";
    }
    return "../data/ephemerides/opm2/cob/slices/uranus_1600_2200";
}

std::string repo_fixed_star_catalog_path() {
    const char* root = std::getenv("TAIYIN_REPO_ROOT");
    if (root && root[0] != '\0') {
        return std::string(root) + "/data/stars/catalogs/stars-fixed-traditional.tsc1";
    }
    return "../data/stars/catalogs/stars-fixed-traditional.tsc1";
}

std::string repo_lunar_limb_path() {
    const char* root = std::getenv("TAIYIN_REPO_ROOT");
    if (root && root[0] != '\0') {
        return std::string(root) + "/data/lunar-limb/kaguya_lalt_16ppd.tll1";
    }
    return "../data/lunar-limb/kaguya_lalt_16ppd.tll1";
}

bool initialize_runtime(int* failures) {
    taiyin::runtime::EphemerisRuntimeConfig config;
    const std::string major_root = repo_opm2_major_body_root();
    const std::string cob_root = repo_opm2_cob_full_root();
    const std::string uranus_cob_root = repo_opm2_uranus_cob_slice_root();
    const char* source_paths[] = { major_root.c_str(), cob_root.c_str(), uranus_cob_root.c_str() };
    config.source_paths = source_paths;
    config.source_path_count = 3;
    config.load_packaged_data = true;
    config.segment_cache_max_entries = 256;
    const bool ok = taiyin::runtime::initialize_global_ephemeris_runtime(config);
    expect_true(ok, "initialize OPM2 runtime", failures);
    return ok;
}

bool initialize_de441_runtime(const std::string& de441, int* failures) {
    const char* source_paths[] = { de441.c_str() };
    taiyin::runtime::EphemerisRuntimeConfig config;
    config.source_paths = source_paths;
    config.source_path_count = 1;
    config.load_packaged_data = false;
    config.segment_cache_max_entries = 512;
    const bool ok = taiyin::runtime::initialize_global_ephemeris_runtime(config);
    expect_true(ok, "initialize DE441 SPK runtime", failures);
    return ok;
}

std::string make_temp_dir() {
    char templ[] = "/tmp/taiyin-occultation-search-XXXXXX";
    char* path = mkdtemp(templ);
    return path ? std::string(path) : std::string();
}

double normalize_degrees(double degrees) {
    double value = std::fmod(degrees, 360.0);
    if (value < 0.0) value += 360.0;
    return value;
}

taiyin::runtime::NativeCalcContext make_geocentric_context() {
    taiyin::runtime::NativeCalcContext context;
    taiyin::runtime::native_context_set_geocentric_observer(
        &context,
        taiyin::TAIYIN_BODY_EARTH,
        taiyin::TAIYIN_BODY_EARTH);
    context.apparent_options.output_frame_id = taiyin::TAIYIN_APPARENT_FRAME_ICRF;
    return context;
}

taiyin::runtime::NativeCalcContext make_zero_topocentric_context() {
    taiyin::runtime::NativeCalcContext context = make_geocentric_context();
    taiyin::CartesianState offset = {};
    taiyin::runtime::native_context_set_topocentric_observer_offset(&context, offset);
    return context;
}

taiyin::runtime::NativeCalcContext make_topocentric_context(
    double longitude_deg,
    double latitude_deg,
    double height_m
) {
    taiyin::runtime::NativeCalcContext context = make_geocentric_context();
    taiyin::runtime::native_context_set_observer_location(
        &context,
        taiyin::runtime::native_observer_location_degrees(longitude_deg, latitude_deg, height_m));
    return context;
}

double swiss_default_pressure_mbar(double height_m) {
    return 1013.25 * std::pow(1.0 - 0.0065 * height_m / 288.0, 5.255);
}

void set_swiss_occultation_rise_set_atmosphere(
    taiyin::runtime::NativeCalcContext* context,
    double height_m
) {
    taiyin::runtime::native_context_set_atmosphere_pressure_temperature(
        context,
        swiss_default_pressure_mbar(height_m),
        0.0);
}

double clamp_unit(double value) {
    if (value < -1.0) return -1.0;
    if (value > 1.0) return 1.0;
    return value;
}

double angular_separation_rad(
    double lon_a,
    double lat_a,
    double lon_b,
    double lat_b
) {
    const double cos_lat_a = std::cos(lat_a);
    const double cos_lat_b = std::cos(lat_b);
    const double ax = cos_lat_a * std::cos(lon_a);
    const double ay = cos_lat_a * std::sin(lon_a);
    const double az = std::sin(lat_a);
    const double bx = cos_lat_b * std::cos(lon_b);
    const double by = cos_lat_b * std::sin(lon_b);
    const double bz = std::sin(lat_b);
    return std::acos(clamp_unit(ax * bx + ay * by + az * bz));
}

int physical_lunar_occultation_body(int body_id) {
    switch (body_id) {
    case taiyin::TAIYIN_BODY_MERCURY_BARYCENTER:
        return taiyin::TAIYIN_BODY_MERCURY;
    case taiyin::TAIYIN_BODY_VENUS_BARYCENTER:
        return taiyin::TAIYIN_BODY_VENUS;
    case taiyin::TAIYIN_BODY_MARS_BARYCENTER:
        return taiyin::TAIYIN_BODY_MARS;
    case taiyin::TAIYIN_BODY_JUPITER_BARYCENTER:
        return taiyin::TAIYIN_BODY_JUPITER;
    case taiyin::TAIYIN_BODY_SATURN_BARYCENTER:
        return taiyin::TAIYIN_BODY_SATURN;
    case taiyin::TAIYIN_BODY_URANUS_BARYCENTER:
        return taiyin::TAIYIN_BODY_URANUS;
    case taiyin::TAIYIN_BODY_NEPTUNE_BARYCENTER:
        return taiyin::TAIYIN_BODY_NEPTUNE;
    case taiyin::TAIYIN_BODY_PLUTO_BARYCENTER:
        return taiyin::TAIYIN_BODY_PLUTO;
    default:
        return body_id;
    }
}

double lunar_occultation_body_radius_km(int body_id) {
    switch (physical_lunar_occultation_body(body_id)) {
    case taiyin::TAIYIN_BODY_MERCURY:
        return 2439.7;
    case taiyin::TAIYIN_BODY_VENUS:
        return 6051.8;
    case taiyin::TAIYIN_BODY_MARS:
        return 3389.5;
    case taiyin::TAIYIN_BODY_JUPITER:
        return 69911.0;
    case taiyin::TAIYIN_BODY_SATURN:
        return 58232.0;
    case taiyin::TAIYIN_BODY_URANUS:
        return 25362.0;
    case taiyin::TAIYIN_BODY_NEPTUNE:
        return 24622.0;
    case taiyin::TAIYIN_BODY_PLUTO:
        return 1188.3;
    default:
        return 0.0;
    }
}

taiyin::Status eval_lunar_body_margin_ut(
    const taiyin::runtime::NativeCalcContext& context,
    int body_id,
    double jd_ut,
    bool topocentric,
    double* out_margin_rad
) {
    using namespace taiyin::runtime;
    if (!out_margin_rad || !std::isfinite(jd_ut)) return taiyin::TAIYIN_ERROR_INVALID_ARGUMENT;
    *out_margin_rad = NAN;

    const int body_ids[2] = { taiyin::TAIYIN_BODY_MOON, body_id };
    ObservedPosition observed[2];
    EphemerisEvalDiagnostic diagnostics[2];
    const taiyin::Status status = calc_observed_ut(
        &context,
        split_jd(jd_ut),
        body_ids,
        2,
        topocentric ? TAIYIN_OBSERVED_TOPOCENTRIC : 0u,
        observed,
        diagnostics);
    if (status != taiyin::TAIYIN_STATUS_OK) return status;

    const ObservedPosition& moon = observed[0];
    const ObservedPosition& target = observed[1];
    if (!std::isfinite(moon.apparent.longitude_rad)
        || !std::isfinite(moon.apparent.latitude_rad)
        || !std::isfinite(moon.apparent.distance_au)
        || !(moon.apparent.distance_au > 0.0)
        || !std::isfinite(target.apparent.longitude_rad)
        || !std::isfinite(target.apparent.latitude_rad)
        || !std::isfinite(target.apparent.distance_au)
        || !(target.apparent.distance_au > 0.0)) {
        return taiyin::TAIYIN_EPHEMERIS_ERROR_EVAL_FAILED;
    }

    const double moon_radius_km = 1737.4;
    const double moon_distance_km = moon.apparent.distance_au * taiyin::TAIYIN_AU_KM;
    const double target_radius_km = lunar_occultation_body_radius_km(body_id);
    const double target_distance_km = target.apparent.distance_au * taiyin::TAIYIN_AU_KM;
    if (!(moon_distance_km > moon_radius_km)
        || !(target_distance_km > target_radius_km)
        || !(target_radius_km >= 0.0)) {
        return taiyin::TAIYIN_EPHEMERIS_ERROR_EVAL_FAILED;
    }

    const double separation = angular_separation_rad(
        moon.apparent.longitude_rad,
        moon.apparent.latitude_rad,
        target.apparent.longitude_rad,
        target.apparent.latitude_rad);
    const double moon_radius = std::asin(moon_radius_km / moon_distance_km);
    const double target_radius = target_radius_km > 0.0
        ? std::asin(target_radius_km / target_distance_km)
        : 0.0;
    *out_margin_rad = moon_radius + target_radius - separation;
    return std::isfinite(*out_margin_rad)
        ? taiyin::TAIYIN_STATUS_OK
        : taiyin::TAIYIN_EPHEMERIS_ERROR_EVAL_FAILED;
}

bool first_coarse_lunar_body_occultation_sample(
    const taiyin::runtime::NativeCalcContext& context,
    int body_id,
    double start_jd_ut,
    double end_jd_ut,
    double step_days,
    bool topocentric,
    double* out_jd_ut
) {
    if (!out_jd_ut || start_jd_ut == end_jd_ut || !(step_days > 0.0)) return false;
    *out_jd_ut = NAN;
    const double direction = start_jd_ut < end_jd_ut ? 1.0 : -1.0;
    for (double jd = start_jd_ut;
         direction > 0.0 ? jd <= end_jd_ut + 1.0e-12 : jd >= end_jd_ut - 1.0e-12;
         jd += direction * step_days) {
        double margin = NAN;
        const taiyin::Status status = eval_lunar_body_margin_ut(context, body_id, jd, topocentric, &margin);
        if (status != taiyin::TAIYIN_STATUS_OK) return false;
        if (margin >= 0.0) {
            *out_jd_ut = jd;
            return true;
        }
    }
    return false;
}

taiyin::internal::Tsf1StarEntry make_star(
    const std::string& id,
    const std::string& name,
    double ra_deg,
    double dec_deg
) {
    taiyin::internal::Tsf1StarEntry entry;
    entry.id = id;
    entry.name = name;
    entry.aliases.push_back(name + " Alias");
    entry.magnitude = 2.0;
    entry.ra_deg = normalize_degrees(ra_deg);
    entry.dec_deg = dec_deg;
    entry.pm_ra_mas_yr = 0.0;
    entry.pm_dec_mas_yr = 0.0;
    entry.parallax_mas = 0.0;
    entry.radial_velocity_km_s = 0.0;
    entry.reference_epoch = 2000.0;
    return entry;
}

taiyin::internal::Tsf1StarEntry make_moving_star_at_epoch(
    const std::string& id,
    const std::string& name,
    double ra_deg,
    double dec_deg,
    double reference_epoch,
    double pm_dec_deg_per_year
) {
    taiyin::internal::Tsf1StarEntry entry = make_star(id, name, ra_deg, dec_deg);
    entry.reference_epoch = reference_epoch;
    entry.pm_dec_mas_yr = pm_dec_deg_per_year * 3600000.0;
    return entry;
}

bool moon_icrf_direction_at(
    taiyin::runtime::NativeCalcContext* context,
    double jd_ut,
    double* out_ra_deg,
    double* out_dec_deg,
    int* failures
) {
    double xyz[6] = {};
    taiyin::runtime::EphemerisEvalDiagnostic diagnostic;
    const taiyin::Status status = taiyin::runtime::calc_position_ut(
        context,
        taiyin::TAIYIN_BODY_MOON,
        split_jd(jd_ut),
        taiyin::runtime::TAIYIN_NATIVE_POSITION_XYZ | taiyin::runtime::TAIYIN_NATIVE_POSITION_TRUEPOS,
        xyz,
        &diagnostic);
    expect_status(status, taiyin::TAIYIN_STATUS_OK, "sample Moon ICRF direction", failures);
    if (status != taiyin::TAIYIN_STATUS_OK) return false;

    const double rxy = std::hypot(xyz[0], xyz[1]);
    const double r = std::hypot(rxy, xyz[2]);
    expect_true(std::isfinite(r) && r > 0.0, "Moon ICRF distance finite", failures);
    if (!(std::isfinite(r) && r > 0.0)) return false;

    *out_ra_deg = normalize_degrees(std::atan2(xyz[1], xyz[0]) * taiyin::TAIYIN_RAD_TO_DEG);
    *out_dec_deg = std::asin(std::max(-1.0, std::min(1.0, xyz[2] / r))) * taiyin::TAIYIN_RAD_TO_DEG;
    return true;
}

bool install_synthetic_star_catalog(
    taiyin::runtime::NativeCalcContext* context,
    double jd_ut,
    int* failures
) {
    double moon_ra_deg = 0.0;
    double moon_dec_deg = 0.0;
    if (!moon_icrf_direction_at(context, jd_ut, &moon_ra_deg, &moon_dec_deg, failures)) {
        return false;
    }

    const std::string root = make_temp_dir();
    expect_true(!root.empty(), "make temp occultation dir", failures);
    if (root.empty()) return false;

    taiyin::internal::Tsf1StarEntry entries[2];
    entries[0] = make_star("moon_path_star", "Moon Path Star", moon_ra_deg, moon_dec_deg);
    entries[1] = make_star("far_star", "Far Star", 0.0, 89.0);
    const std::string path = root + "/stars.tsf1";
    expect_true(taiyin::internal::save_star_file(path, entries, 2), "save occultation TSF1", failures);
    expect_status(
        taiyin::runtime::add_global_tsf1_star_catalog(path.c_str()),
        taiyin::TAIYIN_STATUS_OK,
        "load occultation TSF1",
        failures);
    return true;
}

bool install_synthetic_grid_star_catalog(
    taiyin::runtime::NativeCalcContext* context,
    const double* occultation_jds,
    size_t occultation_count,
    int* failures
) {
    const std::string root = make_temp_dir();
    expect_true(!root.empty(), "make temp occultation grid dir", failures);
    if (root.empty()) return false;

    std::vector<taiyin::internal::Tsf1StarEntry> entries;
    entries.reserve(occultation_count);
    for (size_t i = 0; i < occultation_count; ++i) {
        double moon_ra_deg = 0.0;
        double moon_dec_deg = 0.0;
        if (!moon_icrf_direction_at(context, occultation_jds[i], &moon_ra_deg, &moon_dec_deg, failures)) {
            return false;
        }
        const std::string id = "grid_moon_path_star_" + std::to_string(i);
        entries.push_back(make_star(id, "Grid Moon Path Star " + std::to_string(i), moon_ra_deg, moon_dec_deg));
    }

    const std::string path = root + "/grid-stars.tsf1";
    expect_true(
        taiyin::internal::save_star_file(path, entries.data(), entries.size()),
        "save occultation grid TSF1",
        failures);
    expect_status(
        taiyin::runtime::add_global_tsf1_star_catalog(path.c_str()),
        taiyin::TAIYIN_STATUS_OK,
        "load occultation grid TSF1",
        failures);
    return true;
}

bool install_synthetic_moving_star_catalog(
    taiyin::runtime::NativeCalcContext* context,
    double jd_ut,
    int* failures
) {
    double moon_ra_deg = 0.0;
    double moon_dec_deg = 0.0;
    if (!moon_icrf_direction_at(context, jd_ut, &moon_ra_deg, &moon_dec_deg, failures)) {
        return false;
    }

    const std::string root = make_temp_dir();
    expect_true(!root.empty(), "make temp moving occultation dir", failures);
    if (root.empty()) return false;

    const double reference_epoch = 2000.0 + (jd_ut - 2451545.0) / 365.25;
    taiyin::internal::Tsf1StarEntry entry = make_moving_star_at_epoch(
        "moving_moon_path_star",
        "Moving Moon Path Star",
        moon_ra_deg,
        moon_dec_deg,
        reference_epoch,
        120.0);
    const std::string path = root + "/moving-stars.tsf1";
    expect_true(taiyin::internal::save_star_file(path, &entry, 1), "save moving occultation TSF1", failures);
    expect_status(
        taiyin::runtime::add_global_tsf1_star_catalog(path.c_str()),
        taiyin::TAIYIN_STATUS_OK,
        "load moving occultation TSF1",
        failures);
    return true;
}

double jd_ut(int year, int month, int day, double hour) {
    return taiyin::julian_day({year, month, day, static_cast<int>(hour), 0, (hour - static_cast<int>(hour)) * 3600.0});
}

void test_synthetic_lunar_star_occultation(int* failures) {
    using namespace taiyin::runtime;

    NativeCalcContext context = make_geocentric_context();
    const double center = jd_ut(2024, 4, 8, 18.0);
    if (!install_synthetic_star_catalog(&context, center, failures)) {
        return;
    }

    LunarStarOccultationSearchResult result;
    EphemerisEvalDiagnostic diagnostic;
    expect_status(
        search_next_geocentric_lunar_star_occultation_ut(
            &context,
            "moon_path_star",
            center - 20.0,
            TAIYIN_OCCULTATION_SEARCH_TRUEPOS,
            &result,
            &diagnostic),
        taiyin::TAIYIN_STATUS_OK,
        "synthetic Moon-star occultation search",
        failures);
    expect_equal(result.kind, TAIYIN_OCCULTATION_KIND_LUNAR_STAR, "synthetic occultation kind", failures);
    expect_true(taiyin::split_julian_date_is_finite(result.jd_ut), "synthetic occultation jd finite", failures);
    expect_near(result.jd_ut, center, 0.03, "synthetic occultation close to seed epoch", failures);
    expect_true(result.separation_rad < result.moon_radius_rad, "synthetic occultation is inside Moon disk", failures);
    expect_true(result.margin_rad > 0.0, "synthetic occultation margin positive", failures);
    expect_true(taiyin::split_julian_date_is_finite(result.begin_jd_ut), "synthetic occultation begin finite", failures);
    expect_true(taiyin::split_julian_date_is_finite(result.end_jd_ut), "synthetic occultation end finite", failures);
    expect_true(result.begin_jd_ut < result.jd_ut, "synthetic occultation begin before maximum", failures);
    expect_true(result.end_jd_ut > result.jd_ut, "synthetic occultation end after maximum", failures);
    expect_near(result.target_radius_rad, 0.0, 1.0e-15, "synthetic star target radius zero", failures);
    expect_true(result.evaluation_count > 0, "synthetic occultation evaluated samples", failures);
    expect_true(result.evaluation_count < 350, "synthetic occultation uses seeded candidate search", failures);

    LunarStarOccultationSearchResult boundary_result;
    expect_status(
        search_next_geocentric_lunar_star_occultation_ut(
            &context,
            "moon_path_star",
            result.jd_ut - 1.0e-6,
            TAIYIN_OCCULTATION_SEARCH_TRUEPOS,
            &boundary_result,
            &diagnostic),
        taiyin::TAIYIN_STATUS_OK,
        "synthetic occultation search just before maximum",
        failures);
    expect_equal(
        boundary_result.kind,
        TAIYIN_OCCULTATION_KIND_LUNAR_STAR,
        "synthetic boundary occultation kind",
        failures);
    expect_near(
        boundary_result.jd_ut,
        result.jd_ut,
        2.0e-5,
        "synthetic boundary search keeps current occultation",
        failures);

    LunarStarOccultationSearchResult backward_result;
    expect_status(
        search_next_geocentric_lunar_star_occultation_ut(
            &context,
            "moon_path_star",
            center + 20.0,
            TAIYIN_OCCULTATION_SEARCH_TRUEPOS | TAIYIN_OCCULTATION_SEARCH_BACKWARD,
            &backward_result,
            &diagnostic),
        taiyin::TAIYIN_STATUS_OK,
        "synthetic Moon-star occultation backward search",
        failures);
    expect_equal(
        backward_result.kind,
        TAIYIN_OCCULTATION_KIND_LUNAR_STAR,
        "synthetic backward occultation kind",
        failures);
    expect_near(
        backward_result.jd_ut,
        result.jd_ut,
        2.0e-5,
        "synthetic backward search finds same occultation",
        failures);
}

void test_seeded_search_covers_synthetic_lunar_path_grid(int* failures) {
    using namespace taiyin::runtime;

    NativeCalcContext context = make_geocentric_context();
    const double centers[] = {
        jd_ut(2023, 2, 5, 18.0),
        jd_ut(2023, 4, 26, 6.0),
        jd_ut(2023, 7, 17, 12.0),
        jd_ut(2023, 10, 7, 0.0),
        jd_ut(2024, 1, 25, 18.0),
        jd_ut(2024, 4, 8, 18.0),
        jd_ut(2024, 8, 19, 6.0),
        jd_ut(2024, 11, 11, 12.0),
    };
    const size_t center_count = sizeof(centers) / sizeof(centers[0]);
    if (!install_synthetic_grid_star_catalog(&context, centers, center_count, failures)) {
        return;
    }

    for (size_t i = 0; i < center_count; ++i) {
        const std::string star_id = "grid_moon_path_star_" + std::to_string(i);
        LunarStarOccultationSearchResult result;
        EphemerisEvalDiagnostic diagnostic;
        expect_status(
            search_next_geocentric_lunar_star_occultation_ut(
                &context,
                star_id.c_str(),
                centers[i] - 20.0,
                TAIYIN_OCCULTATION_SEARCH_TRUEPOS,
                &result,
                &diagnostic),
            taiyin::TAIYIN_STATUS_OK,
            "seeded grid occultation search",
            failures);
        expect_equal(result.kind, TAIYIN_OCCULTATION_KIND_LUNAR_STAR, "seeded grid occultation kind", failures);
        expect_near(result.jd_ut, centers[i], 0.04, "seeded grid occultation close to planted epoch", failures);
        expect_true(result.margin_rad > 0.0, "seeded grid occultation margin positive", failures);
        expect_true(result.evaluation_count < 100, "seeded grid occultation avoids dense scan", failures);
    }
}

void test_moving_star_position_is_sampled_at_candidate_epoch(int* failures) {
    using namespace taiyin::runtime;

    NativeCalcContext context = make_geocentric_context();
    const double center = jd_ut(2024, 4, 8, 18.0);
    if (!install_synthetic_moving_star_catalog(&context, center, failures)) {
        return;
    }

    LunarStarOccultationSearchResult result;
    EphemerisEvalDiagnostic diagnostic;
    expect_status(
        search_next_geocentric_lunar_star_occultation_ut(
            &context,
            "moving_moon_path_star",
            center - 20.0,
            TAIYIN_OCCULTATION_SEARCH_TRUEPOS,
            &result,
            &diagnostic),
        taiyin::TAIYIN_STATUS_OK,
        "moving star occultation samples candidate epoch position",
        failures);
    expect_equal(result.kind, TAIYIN_OCCULTATION_KIND_LUNAR_STAR, "moving star occultation kind", failures);
    expect_near(result.jd_ut, center, 0.05, "moving star occultation close to planted epoch", failures);
    expect_true(result.margin_rad > 0.0, "moving star occultation margin positive", failures);
}

void test_packaged_star_catalog_occultation_smoke(int* failures) {
    using namespace taiyin::runtime;

    NativeCalcContext context = make_geocentric_context();
    const std::string catalog_path = repo_fixed_star_catalog_path();
    expect_status(
        add_global_tsc1_star_catalog(catalog_path.c_str()),
        taiyin::TAIYIN_STATUS_OK,
        "load packaged fixed-star TSC1 for occultation",
        failures);

    LunarStarOccultationSearchResult first_result;
    const double start = jd_ut(2024, 1, 1, 0.0);
    const char* const candidate_stars[] = {
        "aldebaran",
        "regulus",
        "spica",
        "antares",
    };
    bool found = false;
    for (size_t i = 0; i < sizeof(candidate_stars) / sizeof(candidate_stars[0]); ++i) {
        LunarStarOccultationSearchResult result;
        EphemerisEvalDiagnostic diagnostic;
        const taiyin::Status status = search_next_geocentric_lunar_star_occultation_ut(
            &context,
            candidate_stars[i],
            start,
            TAIYIN_OCCULTATION_SEARCH_TRUEPOS,
            &result,
            &diagnostic);
        if (status == taiyin::TAIYIN_STATUS_OK && result.kind == TAIYIN_OCCULTATION_KIND_LUNAR_STAR) {
            first_result = result;
            found = true;
            break;
        }
    }
    expect_true(found, "packaged low-latitude star occultation smoke finds a real catalog star", failures);
    if (found) {
        expect_true(taiyin::split_julian_date_is_finite(first_result.jd_ut) && first_result.jd_ut > start, "packaged star occultation after search start", failures);
        expect_true(first_result.margin_rad >= 0.0, "packaged star occultation margin nonnegative", failures);
        expect_true(first_result.begin_jd_ut < first_result.jd_ut, "packaged star occultation begin before maximum", failures);
        expect_true(first_result.end_jd_ut > first_result.jd_ut, "packaged star occultation end after maximum", failures);
    }
}

void test_local_lunar_star_occultation_swiss_oracles(int* failures) {
    using namespace taiyin::runtime;

    const std::string catalog_path = repo_fixed_star_catalog_path();
    expect_status(
        add_global_tsc1_star_catalog(catalog_path.c_str()),
        taiyin::TAIYIN_STATUS_OK,
        "load packaged fixed-star TSC1 for local occultation oracle",
        failures);

    struct Oracle {
        const char* star_key;
        double longitude_deg;
        double latitude_deg;
        double height_m;
        double start_jd_ut;
        double swiss_jd_ut;
        double swiss_begin_jd_ut;
        double swiss_end_jd_ut;
        double swiss_target_rise_jd_ut;
        double swiss_target_set_jd_ut;
        const char* label;
    };
    // SwissEph 2.10.03, SWIEPH, swe_lun_occult_where() location fed into
    // swe_lun_occult_when_loc(). These are local/topocentric maxima, not
    // geocentric closest approaches.
    const Oracle oracles[] = {
        {
            "antares",
            -78.709289952229,
            24.897937227562,
            0.0,
            jd_ut(2024, 1, 1, 0.0),
            2460318.136560418177,
            2460318.104565893300,
            2460318.168356117792,
            0.0,
            0.0,
            "Antares local lunar occultation vs SwissEph SWIEPH",
        },
        {
            "spica",
            76.028162365901,
            61.248881660487,
            0.0,
            jd_ut(2024, 1, 1, 0.0),
            2460478.301808112301,
            2460478.283348425757,
            2460478.319856342860,
            0.0,
            2460478.305817294866,
            "Spica local lunar occultation vs SwissEph SWIEPH",
        },
        {
            "regulus",
            -0.589914666297,
            62.108000036335,
            0.0,
            jd_ut(2024, 1, 1, 0.0),
            2460883.393484750297,
            2460883.389579345006,
            2460883.397376324050,
            0.0,
            2460883.397213968914,
            "Regulus local lunar occultation vs SwissEph SWIEPH",
        },
        {
            "aldebaran",
            107.096221490181,
            72.588443626740,
            0.0,
            jd_ut(2024, 1, 1, 0.0),
            2463828.035846429877,
            2463828.023684354499,
            2463828.048039565794,
            0.0,
            0.0,
            "Aldebaran local lunar occultation vs SwissEph SWIEPH",
        },
    };

    for (size_t i = 0; i < sizeof(oracles) / sizeof(oracles[0]); ++i) {
        NativeCalcContext context = make_topocentric_context(
            oracles[i].longitude_deg,
            oracles[i].latitude_deg,
            oracles[i].height_m);
        set_swiss_occultation_rise_set_atmosphere(&context, oracles[i].height_m);
        LunarStarOccultationSearchResult result;
        EphemerisEvalDiagnostic diagnostic;
        expect_status(
            search_next_local_lunar_star_occultation_ut(
                &context,
                oracles[i].star_key,
                oracles[i].start_jd_ut,
                0u,
                &result,
                &diagnostic),
            taiyin::TAIYIN_STATUS_OK,
            oracles[i].label,
            failures);
        expect_equal(result.kind, TAIYIN_OCCULTATION_KIND_LUNAR_STAR, "SwissEph local oracle kind", failures);
        expect_near(
            result.jd_ut,
            oracles[i].swiss_jd_ut,
            3.0 / 86400.0,
            oracles[i].label,
            failures);
        expect_near(
            result.begin_jd_ut,
            oracles[i].swiss_begin_jd_ut,
            5.0 / 86400.0,
            "SwissEph local oracle begin contact",
            failures);
        expect_near(
            result.end_jd_ut,
            oracles[i].swiss_end_jd_ut,
            5.0 / 86400.0,
            "SwissEph local oracle end contact",
            failures);
        expect_near(result.target_radius_rad, 0.0, 1.0e-15, "SwissEph local oracle star target radius zero", failures);
        expect_true(result.margin_rad > 0.0, "SwissEph local oracle has positive occultation margin", failures);

        LunarOccultationLocalVisibility visibility;
        expect_status(
            compute_lunar_star_occultation_local_visibility_ut(
                &context,
                oracles[i].star_key,
                &result,
                TAIYIN_OCCULTATION_VISIBILITY_REFRACTION,
                &visibility,
                &diagnostic),
            taiyin::TAIYIN_STATUS_OK,
            "SwissEph local star oracle visibility summary",
            failures);
        if (oracles[i].swiss_target_rise_jd_ut != 0.0) {
            expect_near(
                visibility.target_rise_jd_ut,
                oracles[i].swiss_target_rise_jd_ut,
                8.0 / 86400.0,
                "SwissEph local star oracle target rise",
                failures);
        } else {
            expect_true(!taiyin::split_julian_date_is_finite(visibility.target_rise_jd_ut), "SwissEph local star oracle has no target rise", failures);
        }
        if (oracles[i].swiss_target_set_jd_ut != 0.0) {
            expect_near(
                visibility.target_set_jd_ut,
                oracles[i].swiss_target_set_jd_ut,
                8.0 / 86400.0,
                "SwissEph local star oracle target set",
                failures);
        } else {
            expect_true(!taiyin::split_julian_date_is_finite(visibility.target_set_jd_ut), "SwissEph local star oracle has no target set", failures);
        }
    }
}

void test_lunar_body_occultation_smoke(int* failures) {
    using namespace taiyin::runtime;

    NativeCalcContext context = make_geocentric_context();
    const double start = jd_ut(2024, 1, 1, 0.0);
    const int candidate_bodies[] = {
        taiyin::TAIYIN_BODY_MERCURY,
        taiyin::TAIYIN_BODY_VENUS,
        taiyin::TAIYIN_BODY_MARS,
        taiyin::TAIYIN_BODY_JUPITER,
        taiyin::TAIYIN_BODY_SATURN,
    };

    LunarBodyOccultationSearchResult first_result;
    bool found = false;
    for (size_t i = 0; i < sizeof(candidate_bodies) / sizeof(candidate_bodies[0]); ++i) {
        LunarBodyOccultationSearchResult result;
        EphemerisEvalDiagnostic diagnostic;
        const taiyin::Status status = search_next_geocentric_lunar_body_occultation_ut(
            &context,
            candidate_bodies[i],
            start,
            TAIYIN_OCCULTATION_SEARCH_TRUEPOS,
            &result,
            &diagnostic);
        if (status == taiyin::TAIYIN_STATUS_OK && result.kind == TAIYIN_OCCULTATION_KIND_LUNAR_BODY) {
            first_result = result;
            found = true;
            break;
        }
    }
    expect_true(found, "lunar body occultation smoke finds a major planet", failures);
    if (found) {
        expect_true(taiyin::split_julian_date_is_finite(first_result.jd_ut) && first_result.jd_ut > start, "lunar body occultation after search start", failures);
        expect_true(first_result.begin_jd_ut < first_result.jd_ut, "lunar body occultation begin before maximum", failures);
        expect_true(first_result.end_jd_ut > first_result.jd_ut, "lunar body occultation end after maximum", failures);
        expect_true(first_result.target_radius_rad > 0.0, "lunar body occultation target radius positive", failures);
        expect_true(first_result.margin_rad >= 0.0, "lunar body occultation margin nonnegative", failures);
    }

    LunarBodyOccultationSearchResult result;
    EphemerisEvalDiagnostic diagnostic;
    expect_status(
        search_next_geocentric_lunar_body_occultation_ut(
            &context,
            taiyin::TAIYIN_BODY_MOON,
            start,
            0u,
            &result,
            &diagnostic),
        taiyin::TAIYIN_ERROR_INVALID_ARGUMENT,
        "lunar body occultation rejects Moon target",
        failures);
    expect_status(
        search_next_geocentric_lunar_body_occultation_ut(
            &context,
            taiyin::TAIYIN_BODY_SUN,
            start,
            0u,
            &result,
            &diagnostic),
        taiyin::TAIYIN_ERROR_INVALID_ARGUMENT,
        "lunar body occultation rejects Sun target",
        failures);

    NativeCalcContext geocentric = make_geocentric_context();
    expect_status(
        search_next_local_lunar_body_occultation_ut(
            &geocentric,
            taiyin::TAIYIN_BODY_VENUS,
            start,
            TAIYIN_OCCULTATION_SEARCH_TRUEPOS,
            &result,
            &diagnostic),
        taiyin::TAIYIN_ERROR_INVALID_ARGUMENT,
        "local lunar body occultation requires observer in context",
        failures);
}

void test_lunar_body_occultation_custom_radius(int* failures) {
    using namespace taiyin::runtime;

    NativeCalcContext context = make_geocentric_context();
    const double start = jd_ut(2024, 1, 1, 0.0);
    EphemerisEvalDiagnostic diagnostic;
    LunarBodyOccultationSearchResult standard;
    LunarBodyOccultationSearchResult enlarged;
    expect_status(
        search_next_geocentric_lunar_body_occultation_ut(
            &context,
            taiyin::TAIYIN_BODY_MERCURY,
            start,
            TAIYIN_OCCULTATION_SEARCH_TRUEPOS,
            &standard,
            &diagnostic),
        taiyin::TAIYIN_STATUS_OK,
        "standard Mercury lunar occultation for custom-radius comparison",
        failures);

    expect_status(
        search_next_geocentric_lunar_body_occultation_ut(
            &context,
            taiyin::TAIYIN_BODY_MERCURY,
            2.0 * 2439.7,
            start,
            TAIYIN_OCCULTATION_SEARCH_TRUEPOS,
            &enlarged,
            &diagnostic),
        taiyin::TAIYIN_STATUS_OK,
        "custom-radius Mercury lunar occultation",
        failures);
    expect_true(
        enlarged.target_radius_rad > standard.target_radius_rad,
        "custom target radius changes lunar occultation geometry",
        failures);
    expect_true(
        enlarged.first_contact_jd_ut < standard.first_contact_jd_ut
            && enlarged.fourth_contact_jd_ut > standard.fourth_contact_jd_ut,
        "custom target radius expands outer contact interval",
        failures);

    expect_status(
        search_next_geocentric_lunar_body_occultation_ut(
            &context,
            taiyin::TAIYIN_BODY_MERCURY,
            -1.0,
            start,
            0u,
            &enlarged,
            &diagnostic),
        taiyin::TAIYIN_ERROR_INVALID_ARGUMENT,
        "lunar body occultation rejects negative custom radius",
        failures);
}

void test_lunar_body_occultation_seed_regression_all_major_targets(int* failures) {
    using namespace taiyin::runtime;

    NativeCalcContext context = make_geocentric_context();
    const double start = jd_ut(2024, 1, 1, 0.0);
    struct Case {
        int body_id;
        double expected_jd_ut;
        const char* label;
    };
    const Case cases[] = {
        {
            taiyin::TAIYIN_BODY_MERCURY,
            2461090.465104086325,
            "Mercury geocentric lunar occultation seed regression",
        },
        {
            taiyin::TAIYIN_BODY_VENUS,
            2463631.564758849796,
            "Venus geocentric lunar occultation seed regression",
        },
        {
            taiyin::TAIYIN_BODY_MARS_BARYCENTER,
            2460435.594312015921,
            "Mars barycenter geocentric lunar occultation seed regression",
        },
        {
            taiyin::TAIYIN_BODY_JUPITER,
            2461319.933093877975,
            "Jupiter geocentric lunar occultation seed regression",
        },
        {
            taiyin::TAIYIN_BODY_SATURN,
            2460489.122546655126,
            "Saturn geocentric lunar occultation seed regression",
        },
        {
            taiyin::TAIYIN_BODY_URANUS,
            2462599.637160762213,
            "Uranus geocentric lunar occultation seed regression",
        },
        {
            taiyin::TAIYIN_BODY_NEPTUNE,
            2460435.297453072388,
            "Neptune geocentric lunar occultation seed regression",
        },
        {
            taiyin::TAIYIN_BODY_PLUTO,
            2460841.227537972853,
            "Pluto geocentric lunar occultation seed regression",
        },
    };

    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        LunarBodyOccultationSearchResult result;
        EphemerisEvalDiagnostic diagnostic;
        expect_status(
            search_next_geocentric_lunar_body_occultation_ut(
                &context,
                cases[i].body_id,
                start,
                0u,
                &result,
                &diagnostic),
            taiyin::TAIYIN_STATUS_OK,
            cases[i].label,
            failures);
        expect_equal(result.kind, TAIYIN_OCCULTATION_KIND_LUNAR_BODY, "major body seed regression kind", failures);
        expect_true(result.type_flags != 0u, "major body seed regression type classified", failures);
        expect_occultation_centrality(result.type_flags, "major body seed regression centrality classified", failures);
        expect_near(
            result.jd_ut,
            cases[i].expected_jd_ut,
            0.05,
            cases[i].label,
            failures);
        expect_true(result.begin_jd_ut < result.jd_ut, "major body seed regression begin before maximum", failures);
        expect_true(result.end_jd_ut > result.jd_ut, "major body seed regression end after maximum", failures);
        expect_true(result.target_radius_rad > 0.0, "major body seed regression target radius positive", failures);
        expect_true(result.margin_rad >= 0.0, "major body seed regression margin nonnegative", failures);
        expect_true(result.evaluation_count > 0, "major body seed regression evaluated samples", failures);
        expect_true(result.evaluation_count < 600, "major body seed regression avoids dense scan", failures);
    }
}

void test_lunar_body_occultation_seed_matches_coarse_scan(int* failures) {
    using namespace taiyin::runtime;

    NativeCalcContext context = make_geocentric_context();
    struct Case {
        int body_id;
        double start_jd_ut;
        const char* label;
    };
    const Case cases[] = {
        {
            taiyin::TAIYIN_BODY_MERCURY,
            2461084.5,
            "Mercury seed result matches independent coarse margin scan",
        },
        {
            taiyin::TAIYIN_BODY_MARS_BARYCENTER,
            2460430.5,
            "Mars barycenter seed result matches independent coarse margin scan",
        },
        {
            taiyin::TAIYIN_BODY_SATURN,
            2460484.5,
            "Saturn seed result matches independent coarse margin scan",
        },
    };

    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        LunarBodyOccultationSearchResult result;
        EphemerisEvalDiagnostic diagnostic;
        expect_status(
            search_next_geocentric_lunar_body_occultation_ut(
                &context,
                cases[i].body_id,
                cases[i].start_jd_ut,
                0u,
                &result,
                &diagnostic),
            taiyin::TAIYIN_STATUS_OK,
            cases[i].label,
            failures);
        expect_equal(result.kind, TAIYIN_OCCULTATION_KIND_LUNAR_BODY, "coarse scan comparison kind", failures);
        expect_true(result.begin_jd_ut > cases[i].start_jd_ut, "coarse scan comparison event after start", failures);

        double coarse_jd = NAN;
        const bool coarse_found = first_coarse_lunar_body_occultation_sample(
            context,
            cases[i].body_id,
            cases[i].start_jd_ut,
            scalar_jd(result.jd_ut + 1.0),
            30.0 / 1440.0,
            false,
            &coarse_jd);
        expect_true(coarse_found, cases[i].label, failures);
        if (coarse_found) {
            expect_true(
                coarse_jd >= result.begin_jd_ut - 45.0 / 1440.0,
                "coarse scan first positive is not before returned contact window",
                failures);
            expect_true(
                coarse_jd <= result.end_jd_ut + 45.0 / 1440.0,
                "coarse scan first positive lands in returned contact window",
                failures);
        }
    }
}

void test_lunar_body_occultation_backward_seed_matches_coarse_scan(int* failures) {
    using namespace taiyin::runtime;

    NativeCalcContext context = make_geocentric_context();
    struct Case {
        int body_id;
        double start_jd_ut;
        const char* label;
    };
    const Case cases[] = {
        {
            taiyin::TAIYIN_BODY_MERCURY,
            2461095.5,
            "Mercury backward seed result matches independent coarse margin scan",
        },
        {
            taiyin::TAIYIN_BODY_MARS_BARYCENTER,
            2460440.5,
            "Mars barycenter backward seed result matches independent coarse margin scan",
        },
        {
            taiyin::TAIYIN_BODY_SATURN,
            2460494.5,
            "Saturn backward seed result matches independent coarse margin scan",
        },
    };

    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        LunarBodyOccultationSearchResult result;
        EphemerisEvalDiagnostic diagnostic;
        expect_status(
            search_next_geocentric_lunar_body_occultation_ut(
                &context,
                cases[i].body_id,
                cases[i].start_jd_ut,
                TAIYIN_OCCULTATION_SEARCH_BACKWARD,
                &result,
                &diagnostic),
            taiyin::TAIYIN_STATUS_OK,
            cases[i].label,
            failures);
        expect_equal(result.kind, TAIYIN_OCCULTATION_KIND_LUNAR_BODY, "backward coarse scan comparison kind", failures);
        expect_true(result.end_jd_ut < cases[i].start_jd_ut, "backward coarse scan comparison event before start", failures);

        double coarse_jd = NAN;
        const bool coarse_found = first_coarse_lunar_body_occultation_sample(
            context,
            cases[i].body_id,
            cases[i].start_jd_ut,
            scalar_jd(result.jd_ut - 1.0),
            30.0 / 1440.0,
            false,
            &coarse_jd);
        expect_true(coarse_found, cases[i].label, failures);
        if (coarse_found) {
            expect_true(
                coarse_jd >= result.begin_jd_ut - 45.0 / 1440.0,
                "backward coarse scan first positive is not before returned contact window",
                failures);
            expect_true(
                coarse_jd <= result.end_jd_ut + 45.0 / 1440.0,
                "backward coarse scan first positive lands in returned contact window",
                failures);
        }
    }
}

void test_local_lunar_body_occultation_seed_matches_coarse_scan(int* failures) {
    using namespace taiyin::runtime;

    struct Case {
        int body_id;
        double longitude_deg;
        double latitude_deg;
        double height_m;
        double forward_start_jd_ut;
        double backward_start_jd_ut;
        const char* label;
    };
    const Case cases[] = {
        {
            taiyin::TAIYIN_BODY_MERCURY,
            -144.104686755054,
            -10.079501905368,
            0.0,
            2461084.5,
            2461095.5,
            "Mercury local seed result matches independent topocentric coarse scan",
        },
        {
            taiyin::TAIYIN_BODY_VENUS,
            121.228398466012,
            27.170778406424,
            0.0,
            2464948.5,
            2464958.5,
            "Venus local seed result matches independent topocentric coarse scan",
        },
        {
            taiyin::TAIYIN_BODY_SATURN,
            101.040927690585,
            -61.791365228873,
            0.0,
            2460401.5,
            2460411.5,
            "Saturn local seed result matches independent topocentric coarse scan",
        },
    };

    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        NativeCalcContext context = make_topocentric_context(
            cases[i].longitude_deg,
            cases[i].latitude_deg,
            cases[i].height_m);

        LunarBodyOccultationSearchResult forward_result;
        EphemerisEvalDiagnostic diagnostic;
        expect_status(
            search_next_local_lunar_body_occultation_ut(
                &context,
                cases[i].body_id,
                cases[i].forward_start_jd_ut,
                0u,
                &forward_result,
                &diagnostic),
            taiyin::TAIYIN_STATUS_OK,
            cases[i].label,
            failures);
        expect_equal(forward_result.kind, TAIYIN_OCCULTATION_KIND_LUNAR_BODY, "local coarse scan forward kind", failures);

        double forward_coarse_jd = NAN;
        const bool forward_coarse_found = first_coarse_lunar_body_occultation_sample(
            context,
            cases[i].body_id,
            cases[i].forward_start_jd_ut,
            scalar_jd(forward_result.jd_ut + 1.0),
            30.0 / 1440.0,
            true,
            &forward_coarse_jd);
        expect_true(forward_coarse_found, cases[i].label, failures);
        if (forward_coarse_found) {
            expect_true(
                forward_coarse_jd >= forward_result.begin_jd_ut - 45.0 / 1440.0,
                "local forward coarse scan first positive is not before returned contact window",
                failures);
            expect_true(
                forward_coarse_jd <= forward_result.end_jd_ut + 45.0 / 1440.0,
                "local forward coarse scan first positive lands in returned contact window",
                failures);
        }

        LunarBodyOccultationSearchResult backward_result;
        expect_status(
            search_next_local_lunar_body_occultation_ut(
                &context,
                cases[i].body_id,
                cases[i].backward_start_jd_ut,
                TAIYIN_OCCULTATION_SEARCH_BACKWARD,
                &backward_result,
                &diagnostic),
            taiyin::TAIYIN_STATUS_OK,
            cases[i].label,
            failures);
        expect_equal(backward_result.kind, TAIYIN_OCCULTATION_KIND_LUNAR_BODY, "local coarse scan backward kind", failures);

        double backward_coarse_jd = NAN;
        const bool backward_coarse_found = first_coarse_lunar_body_occultation_sample(
            context,
            cases[i].body_id,
            cases[i].backward_start_jd_ut,
            scalar_jd(backward_result.jd_ut - 1.0),
            30.0 / 1440.0,
            true,
            &backward_coarse_jd);
        expect_true(backward_coarse_found, cases[i].label, failures);
        if (backward_coarse_found) {
            expect_true(
                backward_coarse_jd >= backward_result.begin_jd_ut - 45.0 / 1440.0,
                "local backward coarse scan first positive is not before returned contact window",
                failures);
            expect_true(
                backward_coarse_jd <= backward_result.end_jd_ut + 45.0 / 1440.0,
                "local backward coarse scan first positive lands in returned contact window",
                failures);
        }
    }
}

void test_lunar_body_occultation_swiss_oracles(int* failures) {
    using namespace taiyin::runtime;

    NativeCalcContext context = make_geocentric_context();
    struct Oracle {
        int body_id;
        double start_jd_ut;
        double swiss_jd_ut;
        const char* label;
    };
    const Oracle oracles[] = {
        {
            taiyin::TAIYIN_BODY_MERCURY,
            2460900.5,
            2461090.465108,
            "Mercury lunar occultation vs SwissEph SWIEPH",
        },
        {
            taiyin::TAIYIN_BODY_VENUS,
            2464925.5,
            2464953.554403,
            "Venus lunar occultation vs SwissEph SWIEPH",
        },
    };

    for (size_t i = 0; i < sizeof(oracles) / sizeof(oracles[0]); ++i) {
        LunarBodyOccultationSearchResult result;
        EphemerisEvalDiagnostic diagnostic;
        expect_status(
            search_next_geocentric_lunar_body_occultation_ut(
                &context,
                oracles[i].body_id,
                oracles[i].start_jd_ut,
                0u,
                &result,
                &diagnostic),
            taiyin::TAIYIN_STATUS_OK,
            oracles[i].label,
            failures);
        expect_equal(result.kind, TAIYIN_OCCULTATION_KIND_LUNAR_BODY, "SwissEph oracle occultation kind", failures);
        expect_true(result.type_flags != 0u, "SwissEph oracle occultation type classified", failures);
        expect_occultation_centrality(result.type_flags, "SwissEph oracle centrality classified", failures);
        expect_near(
            result.jd_ut,
            oracles[i].swiss_jd_ut,
            10.0 / 86400.0,
            oracles[i].label,
            failures);
        expect_true(result.begin_jd_ut < result.jd_ut, "SwissEph geocentric body oracle begin before maximum", failures);
        expect_true(result.end_jd_ut > result.jd_ut, "SwissEph geocentric body oracle end after maximum", failures);
        expect_true(result.target_radius_rad > 0.0, "SwissEph geocentric body oracle target radius positive", failures);
        expect_true(result.margin_rad > 0.0, "SwissEph geocentric body oracle margin positive", failures);

        LunarBodyOccultationSearchResult boundary_result;
        expect_status(
            search_next_geocentric_lunar_body_occultation_ut(
                &context,
                oracles[i].body_id,
                result.jd_ut - 1.0e-6,
                0u,
                &boundary_result,
                &diagnostic),
            taiyin::TAIYIN_STATUS_OK,
            "SwissEph geocentric body oracle forward search just before maximum",
            failures);
        expect_near(
            boundary_result.jd_ut,
            result.jd_ut,
            3.0 / 86400.0,
            "SwissEph geocentric body boundary search keeps current occultation",
            failures);

        LunarBodyOccultationSearchResult backward_result;
        expect_status(
            search_next_geocentric_lunar_body_occultation_ut(
                &context,
                oracles[i].body_id,
                result.jd_ut + 20.0,
                TAIYIN_OCCULTATION_SEARCH_BACKWARD,
                &backward_result,
                &diagnostic),
            taiyin::TAIYIN_STATUS_OK,
            "SwissEph geocentric body oracle backward search",
            failures);
        expect_near(
            backward_result.jd_ut,
            result.jd_ut,
            3.0 / 86400.0,
            "SwissEph geocentric body backward search finds same occultation",
            failures);
    }
}

uint64_t matching_occultation_filter(uint32_t type_flags) {
    using namespace taiyin::runtime;
    if ((type_flags & TAIYIN_OCCULTATION_TYPE_TOTAL) != 0u) {
        return TAIYIN_OCCULTATION_FILTER_TOTAL;
    }
    if ((type_flags & TAIYIN_OCCULTATION_TYPE_PARTIAL) != 0u) {
        return TAIYIN_OCCULTATION_FILTER_PARTIAL;
    }
    return TAIYIN_OCCULTATION_FILTER_GRAZING;
}

uint64_t opposite_centrality_filter(uint32_t type_flags) {
    using namespace taiyin::runtime;
    return (type_flags & TAIYIN_OCCULTATION_TYPE_CENTRAL) != 0u
        ? TAIYIN_OCCULTATION_FILTER_NONCENTRAL
        : TAIYIN_OCCULTATION_FILTER_CENTRAL;
}

void test_lunar_body_occultation_search_options(int* failures) {
    using namespace taiyin::runtime;

    NativeCalcContext context = make_geocentric_context();
    const double start = 2460900.5;
    LunarBodyOccultationSearchResult reference;
    EphemerisEvalDiagnostic diagnostic;
    expect_status(
        search_next_geocentric_lunar_body_occultation_ut(
            &context,
            taiyin::TAIYIN_BODY_MERCURY,
            start,
            0u,
            &reference,
            &diagnostic),
        taiyin::TAIYIN_STATUS_OK,
        "occultation options reference Mercury event",
        failures);
    expect_true(reference.candidate_count > 0, "reference records candidate count", failures);
    expect_true(taiyin::split_julian_date_is_finite(reference.candidate_jd_ut), "reference records candidate jd", failures);
    expect_true(taiyin::split_julian_date_is_finite(reference.next_search_jd_ut), "reference records continuation jd", failures);
    expect_true((reference.type_flags & TAIYIN_OCCULTATION_TYPE_ANNULAR) == 0u, "non-solar occultation has no annular type", failures);

    const uint64_t matching_filter = matching_occultation_filter(reference.type_flags);
    LunarBodyOccultationSearchResult filtered;
    expect_status(
        search_next_geocentric_lunar_body_occultation_ut(
            &context,
            taiyin::TAIYIN_BODY_MERCURY,
            start,
            matching_filter,
            &filtered,
            &diagnostic),
        taiyin::TAIYIN_STATUS_OK,
        "occultation matching type filter returns event",
        failures);
    expect_near(
        filtered.jd_ut,
        reference.jd_ut,
        1.0e-8,
        "occultation matching type filter keeps reference event",
        failures);

    LunarBodyOccultationSearchResult one_candidate;
    expect_status(
        search_next_geocentric_lunar_body_occultation_ut(
            &context,
            taiyin::TAIYIN_BODY_MERCURY,
            reference.jd_ut - 0.1,
            TAIYIN_OCCULTATION_SEARCH_ONE_CANDIDATE | matching_filter,
            &one_candidate,
            &diagnostic),
        taiyin::TAIYIN_STATUS_OK,
        "occultation one-candidate mode can return matching event",
        failures);
    expect_equal(one_candidate.candidate_count, 1, "one-candidate success probes one candidate", failures);
    expect_near(
        one_candidate.jd_ut,
        reference.jd_ut,
        kRepeatedSearchFixtureToleranceDays,
        "one-candidate success keeps reference event",
        failures);

    LunarBodyOccultationSearchResult filtered_out;
    expect_status(
        search_next_geocentric_lunar_body_occultation_ut(
            &context,
            taiyin::TAIYIN_BODY_MERCURY,
            reference.jd_ut - 0.1,
            TAIYIN_OCCULTATION_SEARCH_ONE_CANDIDATE | opposite_centrality_filter(reference.type_flags),
            &filtered_out,
            &diagnostic),
        taiyin::TAIYIN_EVENT_ERROR_NOT_FOUND,
        "occultation one-candidate mode stops after mismatched filter",
        failures);
    expect_equal(filtered_out.kind, TAIYIN_OCCULTATION_KIND_NONE, "one-candidate filtered result kind none", failures);
    expect_equal(filtered_out.candidate_count, 1, "one-candidate filtered result probes one candidate", failures);
    expect_true(taiyin::split_julian_date_is_finite(filtered_out.candidate_jd_ut), "one-candidate filtered result records candidate", failures);
    expect_true(taiyin::split_julian_date_is_finite(filtered_out.next_search_jd_ut), "one-candidate filtered result records continuation", failures);

    expect_status(
        search_next_geocentric_lunar_body_occultation_ut(
            &context,
            taiyin::TAIYIN_BODY_MERCURY,
            start,
            static_cast<uint64_t>(TAIYIN_NATIVE_POSITION_XYZ),
            &filtered_out,
            &diagnostic),
        taiyin::TAIYIN_ERROR_INVALID_ARGUMENT,
        "occultation search rejects native output-shape flags",
        failures);
}

void test_local_lunar_body_occultation_swiss_oracles(int* failures) {
    using namespace taiyin::runtime;

    struct Oracle {
        int body_id;
        double longitude_deg;
        double latitude_deg;
        double height_m;
        double start_jd_ut;
        double swiss_jd_ut;
        double swiss_first_contact_jd_ut;
        double swiss_second_contact_jd_ut;
        double swiss_third_contact_jd_ut;
        double swiss_fourth_contact_jd_ut;
        double swiss_target_rise_jd_ut;
        double swiss_target_set_jd_ut;
        const char* label;
    };
    const Oracle oracles[] = {
        {
            taiyin::TAIYIN_BODY_MERCURY,
            -144.104686755054,
            -10.079501905368,
            0.0,
            2460900.5,
            2461090.465110052843,
            2461090.430923180189,
            2461090.431199821644,
            2461090.498536294792,
            2461090.498805565760,
            0.0,
            0.0,
            "Mercury local lunar occultation vs SwissEph SWIEPH",
        },
        {
            taiyin::TAIYIN_BODY_VENUS,
            121.228398466012,
            27.170778406424,
            0.0,
            2464925.5,
            2464953.554410743061,
            2464953.520627252292,
            2464953.521206199657,
            2464953.587015563622,
            2464953.587573590688,
            0.0,
            0.0,
            "Venus local lunar occultation vs SwissEph SWIEPH",
        },
        {
            taiyin::TAIYIN_BODY_MARS_BARYCENTER,
            101.469055769899,
            8.863613056688,
            0.0,
            2460310.5,
            2460435.594311445486,
            2460435.564049476292,
            2460435.564216841944,
            2460435.624631531071,
            2460435.624801265541,
            0.0,
            0.0,
            "Mars barycenter local lunar occultation vs SwissEph Mars body",
        },
        {
            taiyin::TAIYIN_BODY_JUPITER,
            -92.862442750153,
            61.073126288858,
            0.0,
            2460310.5,
            2461292.280580133665,
            2461292.257778472733,
            2461292.258523666300,
            2461292.302153089084,
            2461292.302866659127,
            0.0,
            0.0,
            "Jupiter COB local lunar occultation vs SwissEph SWIEPH",
        },
        {
            taiyin::TAIYIN_BODY_SATURN,
            101.040927690585,
            -61.791365228873,
            0.0,
            2460310.5,
            2460406.929659899790,
            2460406.912444687448,
            2460406.912743221968,
            2460406.946294780821,
            2460406.946583540644,
            0.0,
            2460406.933164517861,
            "Saturn COB local lunar occultation vs SwissEph SWIEPH",
        },
    };

    for (size_t i = 0; i < sizeof(oracles) / sizeof(oracles[0]); ++i) {
        NativeCalcContext context = make_topocentric_context(
            oracles[i].longitude_deg,
            oracles[i].latitude_deg,
            oracles[i].height_m);
        set_swiss_occultation_rise_set_atmosphere(&context, oracles[i].height_m);
        LunarBodyOccultationSearchResult result;
        EphemerisEvalDiagnostic diagnostic;
        expect_status(
            search_next_local_lunar_body_occultation_ut(
                &context,
                oracles[i].body_id,
                oracles[i].start_jd_ut,
                0u,
                &result,
                &diagnostic),
            taiyin::TAIYIN_STATUS_OK,
            oracles[i].label,
            failures);
        expect_equal(result.kind, TAIYIN_OCCULTATION_KIND_LUNAR_BODY, "SwissEph local body oracle kind", failures);
        expect_true(result.type_flags != 0u, "SwissEph local body oracle type classified", failures);
        expect_occultation_centrality(result.type_flags, "SwissEph local body oracle centrality classified", failures);
        expect_near(
            result.jd_ut,
            oracles[i].swiss_jd_ut,
            5.0 / 86400.0,
            oracles[i].label,
            failures);
        expect_near(
            result.begin_jd_ut,
            oracles[i].swiss_first_contact_jd_ut,
            8.0 / 86400.0,
            "SwissEph local body oracle begin contact",
            failures);
        expect_near(
            result.end_jd_ut,
            oracles[i].swiss_fourth_contact_jd_ut,
            8.0 / 86400.0,
            "SwissEph local body oracle end contact",
            failures);
        expect_near(
            result.first_contact_jd_ut,
            oracles[i].swiss_first_contact_jd_ut,
            8.0 / 86400.0,
            "SwissEph local body oracle first contact",
            failures);
        expect_near(
            result.second_contact_jd_ut,
            oracles[i].swiss_second_contact_jd_ut,
            8.0 / 86400.0,
            "SwissEph local body oracle second contact",
            failures);
        expect_near(
            result.third_contact_jd_ut,
            oracles[i].swiss_third_contact_jd_ut,
            8.0 / 86400.0,
            "SwissEph local body oracle third contact",
            failures);
        expect_near(
            result.fourth_contact_jd_ut,
            oracles[i].swiss_fourth_contact_jd_ut,
            8.0 / 86400.0,
            "SwissEph local body oracle fourth contact",
            failures);
        expect_true(result.first_contact_jd_ut < result.second_contact_jd_ut, "local body C1 before C2", failures);
        expect_true(result.second_contact_jd_ut < result.jd_ut, "local body C2 before maximum", failures);
        expect_true(result.jd_ut < result.third_contact_jd_ut, "local body maximum before C3", failures);
        expect_true(result.third_contact_jd_ut < result.fourth_contact_jd_ut, "local body C3 before C4", failures);
        expect_near(result.begin_jd_ut, result.first_contact_jd_ut, 0.0, "begin aliases first contact", failures);
        expect_near(result.end_jd_ut, result.fourth_contact_jd_ut, 0.0, "end aliases fourth contact", failures);
        expect_true(result.target_radius_rad > 0.0, "SwissEph local body oracle target radius positive", failures);
        expect_true(result.margin_rad > 0.0, "SwissEph local body oracle margin positive", failures);

        LunarOccultationLocalVisibility visibility;
        expect_status(
            compute_lunar_body_occultation_local_visibility_ut(
                &context,
                oracles[i].body_id,
                &result,
                TAIYIN_OCCULTATION_VISIBILITY_REFRACTION,
                &visibility,
                &diagnostic),
            taiyin::TAIYIN_STATUS_OK,
            "SwissEph local body oracle visibility summary",
            failures);
        if (oracles[i].swiss_target_rise_jd_ut != 0.0) {
            expect_near(
                visibility.target_rise_jd_ut,
                oracles[i].swiss_target_rise_jd_ut,
                8.0 / 86400.0,
                "SwissEph local body oracle target rise",
                failures);
        } else {
            expect_true(!taiyin::split_julian_date_is_finite(visibility.target_rise_jd_ut), "SwissEph local body oracle has no target rise", failures);
        }
        if (oracles[i].swiss_target_set_jd_ut != 0.0) {
            expect_near(
                visibility.target_set_jd_ut,
                oracles[i].swiss_target_set_jd_ut,
                8.0 / 86400.0,
                "SwissEph local body oracle target set",
                failures);
        } else {
            expect_true(!taiyin::split_julian_date_is_finite(visibility.target_set_jd_ut), "SwissEph local body oracle has no target set", failures);
        }

        LunarBodyOccultationSearchResult boundary_result;
        expect_status(
            search_next_local_lunar_body_occultation_ut(
                &context,
                oracles[i].body_id,
                result.jd_ut - 1.0e-6,
                0u,
                &boundary_result,
                &diagnostic),
            taiyin::TAIYIN_STATUS_OK,
            "SwissEph local body oracle forward search just before maximum",
            failures);
        expect_near(
            boundary_result.jd_ut,
            result.jd_ut,
            3.0 / 86400.0,
            "SwissEph local body boundary search keeps current occultation",
            failures);

        LunarBodyOccultationSearchResult in_progress_result;
        expect_status(
            search_next_local_lunar_body_occultation_ut(
                &context,
                oracles[i].body_id,
                result.begin_jd_ut + 60.0 / 86400.0,
                0u,
                &in_progress_result,
                &diagnostic),
            taiyin::TAIYIN_STATUS_OK,
            "SwissEph local body oracle forward search during ingress",
            failures);
        expect_near(
            in_progress_result.jd_ut,
            result.jd_ut,
            3.0 / 86400.0,
            "SwissEph local body in-progress search finds same maximum",
            failures);

        LunarBodyOccultationSearchResult backward_result;
        expect_status(
            search_next_local_lunar_body_occultation_ut(
                &context,
                oracles[i].body_id,
                oracles[i].swiss_jd_ut + 20.0,
                TAIYIN_OCCULTATION_SEARCH_BACKWARD,
                &backward_result,
                &diagnostic),
            taiyin::TAIYIN_STATUS_OK,
            "SwissEph local body oracle backward search",
            failures);
        expect_equal(
            backward_result.kind,
            TAIYIN_OCCULTATION_KIND_LUNAR_BODY,
            "SwissEph local body backward oracle kind",
            failures);
        expect_near(
            backward_result.jd_ut,
            result.jd_ut,
            3.0 / 86400.0,
            "SwissEph local body backward search finds same occultation",
            failures);

        LunarBodyOccultationSearchResult backward_boundary_result;
        expect_status(
            search_next_local_lunar_body_occultation_ut(
                &context,
                oracles[i].body_id,
                result.jd_ut + 1.0e-6,
                TAIYIN_OCCULTATION_SEARCH_BACKWARD,
                &backward_boundary_result,
                &diagnostic),
            taiyin::TAIYIN_STATUS_OK,
            "SwissEph local body oracle backward search just after maximum",
            failures);
        expect_near(
            backward_boundary_result.jd_ut,
            result.jd_ut,
            3.0 / 86400.0,
            "SwissEph local body backward boundary search finds same occultation",
            failures);
    }
}

void test_lunar_occultation_where_swiss_oracles(int* failures) {
    using namespace taiyin::runtime;

    const std::string catalog_path = repo_fixed_star_catalog_path();
    expect_status(
        add_global_tsc1_star_catalog(catalog_path.c_str()),
        taiyin::TAIYIN_STATUS_OK,
        "load packaged fixed-star TSC1 for occultation where oracle",
        failures);

    NativeCalcContext context = make_geocentric_context();
    EphemerisEvalDiagnostic diagnostic;

    LunarStarOccultationSearchResult star_event;
    star_event.kind = TAIYIN_OCCULTATION_KIND_LUNAR_STAR;
    star_event.jd_ut = split_jd(2460318.136560418177);

    LunarOccultationWhereResult star_where;
    expect_status(
        compute_lunar_star_occultation_where_ut(
            &context,
            "antares",
            &star_event,
            0u,
            &star_where,
            &diagnostic),
        taiyin::TAIYIN_STATUS_OK,
        "Antares occultation where vs SwissEph location",
        failures);
    expect_equal(star_where.center_line_hits_earth, 1, "Antares where center line hits Earth", failures);
    expect_true(
        (star_where.type_flags & TAIYIN_OCCULTATION_TYPE_CENTRAL) != 0u,
        "Antares where type is central",
        failures);
    expect_true(
        (star_where.type_flags & TAIYIN_OCCULTATION_TYPE_TOTAL) != 0u,
        "Antares where type is total",
        failures);
    expect_near(
        star_where.longitude_deg,
        -78.709289952229,
        0.05,
        "Antares where longitude vs SwissEph",
        failures);
    expect_near(
        star_where.latitude_deg,
        24.897937227562,
        0.05,
        "Antares where latitude vs SwissEph",
        failures);
    expect_true(star_where.local_sample.valid != 0, "Antares where local sample valid", failures);
    expect_true(star_where.margin_rad > 0.0, "Antares where margin positive", failures);

    // SwissEph exposes the best where() point above. The Taiyin-derived path,
    // limit, polygon, and phenomena snapshots were repinned after split-JD
    // evaluation-order changes; strict tolerances and the oracle above remain unchanged.
    expect_true(
        taiyin::split_julian_date_is_finite(star_where.center_line_begin_jd_ut)
            && star_where.center_line_begin_jd_ut < star_where.jd_ut,
        "Antares where center-line begin before maximum",
        failures);
    expect_near(
        star_where.center_line_begin_jd_ut,
        2460318.0932017099,
        1.0e-9,
        "Antares where center-line begin fixture",
        failures);
    expect_true(
        taiyin::split_julian_date_is_finite(star_where.center_line_end_jd_ut)
            && star_where.center_line_end_jd_ut > star_where.jd_ut,
        "Antares where center-line end after maximum",
        failures);
    expect_near(
        star_where.center_line_end_jd_ut,
        2460318.1800921597,
        1.0e-9,
        "Antares where center-line end fixture",
        failures);
    expect_equal(star_where.center_line_path_count, 16, "Antares where center-line path count fixture", failures);
    expect_true(
        std::isfinite(star_where.center_line_path_distance_km)
            && star_where.center_line_path_distance_km > 0.0,
        "Antares where center-line path distance finite",
        failures);
    expect_near(
        star_where.center_line_path_distance_km,
        9448.890289875948,
        1.0,
        "Antares where center-line path distance fixture",
        failures);
    expect_true(
        star_where.latitude_deg >= star_where.center_line_min_latitude_deg
            && star_where.latitude_deg <= star_where.center_line_max_latitude_deg,
        "Antares where maximum latitude inside path envelope",
        failures);
    expect_near(
        star_where.center_line_min_longitude_deg,
        -120.46885001004189,
        kDerivedEnvelopeFixtureToleranceDeg,
        "Antares where center-line min longitude fixture",
        failures);
    expect_near(
        star_where.center_line_max_longitude_deg,
        -35.598964727162326,
        kDerivedEnvelopeFixtureToleranceDeg,
        "Antares where center-line max longitude fixture",
        failures);
    expect_near(
        star_where.center_line_min_latitude_deg,
        23.454039214400,
        kDerivedEnvelopeFixtureToleranceDeg,
        "Antares where center-line min latitude fixture",
        failures);
    expect_near(
        star_where.center_line_max_latitude_deg,
        53.852409519322563,
        kDerivedEnvelopeFixtureToleranceDeg,
        "Antares where center-line max latitude fixture",
        failures);
    expect_equal(star_where.outer_limit_path_count, 14, "Antares where outer-limit path count fixture", failures);
    expect_true(
        std::isfinite(star_where.outer_limit_mean_width_km)
            && star_where.outer_limit_mean_width_km > 0.0,
        "Antares where outer-limit mean width finite",
        failures);
    expect_near(
        star_where.outer_limit_mean_width_km,
        4777.620379523052,
        1.0,
        "Antares where outer-limit mean width fixture",
        failures);
    expect_true(
        std::isfinite(star_where.outer_limit_max_width_km)
            && star_where.outer_limit_max_width_km >= star_where.outer_limit_mean_width_km,
        "Antares where outer-limit max width finite",
        failures);
    expect_near(
        star_where.outer_limit_max_width_km,
        10065.417889945889,
        1.0,
        "Antares where outer-limit max width fixture",
        failures);
    expect_equal(
        star_where.visible_region_polygon_count,
        2 * star_where.outer_limit_path_count,
        "Antares where polygon closes north/south paths",
        failures);
    expect_equal(star_where.visible_region_polygon_count, 28, "Antares where polygon count fixture", failures);
    expect_true(
        std::isfinite(star_where.visible_region_min_longitude_deg)
            && std::isfinite(star_where.visible_region_max_longitude_deg)
            && star_where.visible_region_max_longitude_deg >= star_where.visible_region_min_longitude_deg,
        "Antares where polygon longitude envelope finite",
        failures);
    expect_near(
        star_where.visible_region_min_longitude_deg,
        -138.65117290334697,
        kDerivedEnvelopeFixtureToleranceDeg,
        "Antares where polygon min longitude fixture",
        failures);
    expect_near(
        star_where.visible_region_max_longitude_deg,
        60.260030704263414,
        kDerivedEnvelopeFixtureToleranceDeg,
        "Antares where polygon max longitude fixture",
        failures);
    expect_near(
        star_where.visible_region_min_latitude_deg,
        2.9212303739685539,
        kDerivedEnvelopeFixtureToleranceDeg,
        "Antares where polygon min latitude fixture",
        failures);
    expect_near(
        star_where.visible_region_max_latitude_deg,
        85.508443308469,
        kDerivedEnvelopeFixtureToleranceDeg,
        "Antares where polygon max latitude fixture",
        failures);
    expect_true(
        std::isfinite(star_where.phenomena.angular_distance_rad)
            && std::isfinite(star_where.phenomena.obscuration)
            && star_where.phenomena.obscuration >= 0.0
            && star_where.phenomena.obscuration <= 1.0,
        "Antares where phenomena finite",
        failures);
    expect_near(
        star_where.phenomena.angular_distance_rad,
        0.000188846162 * taiyin::TAIYIN_DEG_TO_RAD,
        4.0e-6,
        "Antares where phenomena angular distance vs SwissEph attr[7]",
        failures);
    expect_near(star_where.phenomena.magnitude, 1.0, 1.0e-15, "Antares where phenomena magnitude vs SwissEph attr[0]", failures);
    expect_near(star_where.phenomena.obscuration, 1.0, 1.0e-15, "Antares where phenomena obscuration vs SwissEph attr[2]", failures);
    expect_near(
        star_where.phenomena.occulted_fraction,
        1.0,
        1.0e-15,
        "Antares where phenomena occulted fraction fixture",
        failures);

    LunarStarOccultationSearchResult searched_star_event;
    expect_status(
        search_next_geocentric_lunar_star_occultation_ut(
            &context,
            "antares",
            2460318.0,
            0u,
            &searched_star_event,
            &diagnostic),
        taiyin::TAIYIN_STATUS_OK,
        "Antares geocentric search result for where path regression",
        failures);
    expect_true(
        taiyin::split_julian_date_is_finite(searched_star_event.first_contact_jd_ut)
            && taiyin::split_julian_date_is_finite(searched_star_event.fourth_contact_jd_ut),
        "Antares searched event carries contacts",
        failures);
    LunarOccultationWhereResult searched_star_where;
    expect_status(
        compute_lunar_star_occultation_where_ut(
            &context,
            "antares",
            &searched_star_event,
            0u,
            &searched_star_where,
            &diagnostic),
        taiyin::TAIYIN_STATUS_OK,
        "Antares searched event where keeps center-line path",
        failures);
    expect_true(
        taiyin::split_julian_date_is_finite(searched_star_where.center_line_begin_jd_ut)
            && searched_star_where.center_line_begin_jd_ut < searched_star_where.jd_ut,
        "Antares searched where center-line begin finite",
        failures);
    expect_true(
        taiyin::split_julian_date_is_finite(searched_star_where.center_line_end_jd_ut)
            && searched_star_where.center_line_end_jd_ut > searched_star_where.jd_ut,
        "Antares searched where center-line end finite",
        failures);
    expect_true(
        searched_star_where.center_line_path_count > 0
            && searched_star_where.visible_region_polygon_count > 0,
        "Antares searched where path and polygon not truncated by contacts",
        failures);

    LunarStarOccultationSearchResult noncentral_star_event;
    noncentral_star_event.kind = TAIYIN_OCCULTATION_KIND_LUNAR_STAR;
    noncentral_star_event.jd_ut = split_jd(2460478.301808112767);

    LunarOccultationWhereResult noncentral_star_where;
    expect_status(
        compute_lunar_star_occultation_where_ut(
            &context,
            "spica",
            &noncentral_star_event,
            0u,
            &noncentral_star_where,
            &diagnostic),
        taiyin::TAIYIN_STATUS_OK,
        "Spica noncentral occultation where vs SwissEph location",
        failures);
    expect_equal(noncentral_star_where.center_line_hits_earth, 0, "Spica where center line misses Earth", failures);
    expect_true(
        (noncentral_star_where.type_flags & TAIYIN_OCCULTATION_TYPE_NONCENTRAL) != 0u,
        "Spica where type is noncentral",
        failures);
    expect_near(
        noncentral_star_where.longitude_deg,
        76.045677359,
        0.08,
        "Spica noncentral where longitude vs SwissEph",
        failures);
    expect_near(
        noncentral_star_where.latitude_deg,
        61.194492310,
        0.08,
        "Spica noncentral where latitude vs SwissEph",
        failures);
    expect_equal(noncentral_star_where.center_line_path_count, 0, "Spica noncentral where has no center-line path", failures);
    expect_equal(noncentral_star_where.outer_limit_path_count, 0, "Spica noncentral where has no outer-limit path", failures);
    expect_equal(noncentral_star_where.visible_region_polygon_count, 0, "Spica noncentral where has no polygon", failures);
    expect_true(noncentral_star_where.local_sample.valid != 0, "Spica noncentral where local sample valid", failures);

    LunarBodyOccultationSearchResult body_event;
    body_event.kind = TAIYIN_OCCULTATION_KIND_LUNAR_BODY;
    body_event.jd_ut = split_jd(2461090.465110052843);

    LunarOccultationWhereResult body_where;
    expect_status(
        compute_lunar_body_occultation_where_ut(
            &context,
            taiyin::TAIYIN_BODY_MERCURY,
            &body_event,
            0u,
            &body_where,
            &diagnostic),
        taiyin::TAIYIN_STATUS_OK,
        "Mercury occultation where vs SwissEph location",
        failures);
    expect_equal(body_where.center_line_hits_earth, 1, "Mercury where center line hits Earth", failures);
    expect_true(
        (body_where.type_flags & TAIYIN_OCCULTATION_TYPE_CENTRAL) != 0u,
        "Mercury where type is central",
        failures);
    expect_true(body_where.type_flags != 0u, "Mercury where type classified", failures);
    expect_near(
        body_where.longitude_deg,
        -144.104686755054,
        0.05,
        "Mercury where longitude vs SwissEph",
        failures);
    expect_near(
        body_where.latitude_deg,
        -10.079501905368,
        0.05,
        "Mercury where latitude vs SwissEph",
        failures);
    expect_true(body_where.local_sample.valid != 0, "Mercury where local sample valid", failures);
    expect_true(body_where.margin_rad > 0.0, "Mercury where margin positive", failures);

    // SwissEph exposes the best where() point above. The Taiyin-derived path,
    // limit, polygon, and phenomena snapshots were repinned after split-JD
    // evaluation-order changes; strict tolerances and the oracle above remain unchanged.
    expect_true(
        taiyin::split_julian_date_is_finite(body_where.center_line_begin_jd_ut)
            && body_where.center_line_begin_jd_ut < body_where.jd_ut,
        "Mercury where center-line begin before maximum",
        failures);
    expect_near(
        body_where.center_line_begin_jd_ut,
        2461090.3887131121,
        1.0e-9,
        "Mercury where center-line begin fixture",
        failures);
    expect_true(
        taiyin::split_julian_date_is_finite(body_where.center_line_end_jd_ut)
            && body_where.center_line_end_jd_ut > body_where.jd_ut,
        "Mercury where center-line end after maximum",
        failures);
    expect_near(
        body_where.center_line_end_jd_ut,
        2461090.5414929627,
        1.0e-9,
        "Mercury where center-line end fixture",
        failures);
    expect_equal(body_where.center_line_path_count, 16, "Mercury where center-line path count fixture", failures);
    expect_true(
        std::isfinite(body_where.center_line_path_distance_km)
            && body_where.center_line_path_distance_km > 0.0,
        "Mercury where center-line path distance finite",
        failures);
    expect_near(
        body_where.center_line_path_distance_km,
        14846.889481695167,
        1.0,
        "Mercury where center-line path distance fixture",
        failures);
    expect_true(
        body_where.latitude_deg >= body_where.center_line_min_latitude_deg
            && body_where.latitude_deg <= body_where.center_line_max_latitude_deg,
        "Mercury where maximum latitude inside path envelope",
        failures);
    expect_near(
        body_where.center_line_min_longitude_deg,
        -169.653905353445,
        kDerivedEnvelopeFixtureToleranceDeg,
        "Mercury where center-line min longitude fixture",
        failures);
    expect_near(
        body_where.center_line_max_longitude_deg,
        179.563326781778,
        kDerivedEnvelopeFixtureToleranceDeg,
        "Mercury where center-line max longitude fixture",
        failures);
    expect_near(
        body_where.center_line_min_latitude_deg,
        -34.731574638229297,
        kDerivedEnvelopeFixtureToleranceDeg,
        "Mercury where center-line min latitude fixture",
        failures);
    expect_near(
        body_where.center_line_max_latitude_deg,
        20.440325305893701,
        kDerivedEnvelopeFixtureToleranceDeg,
        "Mercury where center-line max latitude fixture",
        failures);
    expect_equal(body_where.outer_limit_path_count, 16, "Mercury where outer-limit path count fixture", failures);
    expect_true(
        std::isfinite(body_where.outer_limit_mean_width_km)
            && body_where.outer_limit_mean_width_km > 0.0,
        "Mercury where outer-limit mean width finite",
        failures);
    expect_near(
        body_where.outer_limit_mean_width_km,
        3582.614113910650,
        1.0,
        "Mercury where outer-limit mean width fixture",
        failures);
    expect_true(
        std::isfinite(body_where.outer_limit_max_width_km)
            && body_where.outer_limit_max_width_km >= body_where.outer_limit_mean_width_km,
        "Mercury where outer-limit max width finite",
        failures);
    expect_near(
        body_where.outer_limit_max_width_km,
        3666.484109552137,
        1.0,
        "Mercury where outer-limit max width fixture",
        failures);
    expect_equal(
        body_where.visible_region_polygon_count,
        2 * body_where.outer_limit_path_count,
        "Mercury where polygon closes north/south paths",
        failures);
    expect_equal(body_where.visible_region_polygon_count, 32, "Mercury where polygon count fixture", failures);
    expect_near(
        body_where.visible_region_min_longitude_deg,
        146.20123233527787,
        kDerivedEnvelopeFixtureToleranceDeg,
        "Mercury where polygon min longitude fixture",
        failures);
    expect_near(
        body_where.visible_region_max_longitude_deg,
        277.16821215973596,
        kDerivedEnvelopeFixtureToleranceDeg,
        "Mercury where polygon max longitude fixture",
        failures);
    expect_near(
        body_where.visible_region_min_latitude_deg,
        -50.652030823818819,
        kDerivedEnvelopeFixtureToleranceDeg,
        "Mercury where polygon min latitude fixture",
        failures);
    expect_near(
        body_where.visible_region_max_latitude_deg,
        36.006401691586440,
        kDerivedEnvelopeFixtureToleranceDeg,
        "Mercury where polygon max latitude fixture",
        failures);
    expect_true(
        std::isfinite(body_where.phenomena.diameter_ratio)
            && body_where.phenomena.diameter_ratio > 0.0
            && std::isfinite(body_where.phenomena.obscuration)
            && body_where.phenomena.obscuration >= 0.0,
        "Mercury where phenomena finite",
        failures);
    expect_near(
        body_where.phenomena.angular_distance_rad,
        0.000015344191 * taiyin::TAIYIN_DEG_TO_RAD,
        3.0e-7,
        "Mercury where phenomena angular distance vs SwissEph attr[7]",
        failures);
    expect_near(
        body_where.phenomena.diameter_ratio,
        274.403680624219,
        0.25,
        "Mercury where phenomena diameter ratio vs SwissEph attr[1]",
        failures);
    expect_near(
        body_where.phenomena.magnitude,
        137.693956714570,
        0.15,
        "Mercury where phenomena magnitude vs SwissEph attr[0]",
        failures);
    expect_near(
        body_where.phenomena.obscuration,
        75297.379940118219,
        100.0,
        "Mercury where phenomena obscuration vs SwissEph attr[2]",
        failures);
    expect_near(
        body_where.phenomena.occulted_fraction,
        1.0,
        1.0e-15,
        "Mercury where phenomena occulted fraction fixture",
        failures);

    LunarBodyOccultationSearchResult noncentral_body_event;
    noncentral_body_event.kind = TAIYIN_OCCULTATION_KIND_LUNAR_BODY;
    noncentral_body_event.jd_ut = split_jd(2460406.929659899790);

    LunarOccultationWhereResult noncentral_body_where;
    expect_status(
        compute_lunar_body_occultation_where_ut(
            &context,
            taiyin::TAIYIN_BODY_SATURN,
            &noncentral_body_event,
            0u,
            &noncentral_body_where,
            &diagnostic),
        taiyin::TAIYIN_STATUS_OK,
        "Saturn noncentral occultation where vs SwissEph location",
        failures);
    expect_equal(noncentral_body_where.center_line_hits_earth, 0, "Saturn where center line misses Earth", failures);
    expect_true(
        (noncentral_body_where.type_flags & TAIYIN_OCCULTATION_TYPE_NONCENTRAL) != 0u,
        "Saturn where type is noncentral",
        failures);
    expect_near(
        noncentral_body_where.longitude_deg,
        100.984625331,
        0.08,
        "Saturn noncentral where longitude vs SwissEph",
        failures);
    expect_near(
        noncentral_body_where.latitude_deg,
        -61.740766382,
        0.08,
        "Saturn noncentral where latitude vs SwissEph",
        failures);
    expect_equal(noncentral_body_where.center_line_path_count, 0, "Saturn noncentral where has no center-line path", failures);
    expect_equal(noncentral_body_where.outer_limit_path_count, 0, "Saturn noncentral where has no outer-limit path", failures);
    expect_equal(noncentral_body_where.visible_region_polygon_count, 0, "Saturn noncentral where has no polygon", failures);
    expect_true(noncentral_body_where.local_sample.valid != 0, "Saturn noncentral where local sample valid", failures);

    LunarOccultationWhereResult invalid_where;
    expect_status(
        compute_lunar_body_occultation_where_ut(
            &context,
            taiyin::TAIYIN_BODY_MERCURY,
            &body_event,
            TAIYIN_OCCULTATION_SEARCH_BACKWARD,
            &invalid_where,
            &diagnostic),
        taiyin::TAIYIN_ERROR_INVALID_ARGUMENT,
        "where rejects backward flag",
        failures);
}

void test_lunar_body_occultation_semi_analytic_route_converges(int* failures) {
    using namespace taiyin::runtime;

    NativeCalcContext opm_context = make_topocentric_context(
        -144.104686755054,
        -10.079501905368,
        0.0);
    expect_status(
        native_context_set_route_rule(&opm_context, TAIYIN_EPHEMERIS_ROUTE_OPM2),
        taiyin::TAIYIN_STATUS_OK,
        "set OPM2 route for lunar body occultation",
        failures);
    NativeCalcContext semi_analytic_context = make_topocentric_context(
        -144.104686755054,
        -10.079501905368,
        0.0);
    expect_status(
        native_context_set_route_rule(&semi_analytic_context, TAIYIN_EPHEMERIS_ROUTE_SEMI_ANALYTIC),
        taiyin::TAIYIN_STATUS_OK,
        "set semi-analytical route for lunar body occultation",
        failures);

    LunarBodyOccultationSearchResult opm_result;
    LunarBodyOccultationSearchResult semi_analytic_result;
    EphemerisEvalDiagnostic diagnostic;
    expect_status(
        search_next_local_lunar_body_occultation_ut(
            &opm_context,
            taiyin::TAIYIN_BODY_MERCURY,
            2460900.5,
            0u,
            &opm_result,
            &diagnostic),
        taiyin::TAIYIN_STATUS_OK,
        "OPM2 local lunar Mercury occultation convergence baseline",
        failures);
    expect_status(
        search_next_local_lunar_body_occultation_ut(
            &semi_analytic_context,
            taiyin::TAIYIN_BODY_MERCURY,
            2460900.5,
            0u,
            &semi_analytic_result,
            &diagnostic),
        taiyin::TAIYIN_STATUS_OK,
        "semi-analytical local lunar Mercury occultation converges",
        failures);
    expect_near(
        semi_analytic_result.jd_ut,
        opm_result.jd_ut,
        2.0 / 86400.0,
        "semi-analytical occultation maximum tracks OPM2",
        failures);
    expect_near(
        semi_analytic_result.first_contact_jd_ut,
        opm_result.first_contact_jd_ut,
        2.0 / 86400.0,
        "semi-analytical occultation C1 tracks OPM2",
        failures);
    expect_near(
        semi_analytic_result.second_contact_jd_ut,
        opm_result.second_contact_jd_ut,
        2.0 / 86400.0,
        "semi-analytical occultation C2 tracks OPM2",
        failures);
    expect_near(
        semi_analytic_result.third_contact_jd_ut,
        opm_result.third_contact_jd_ut,
        2.0 / 86400.0,
        "semi-analytical occultation C3 tracks OPM2",
        failures);
    expect_near(
        semi_analytic_result.fourth_contact_jd_ut,
        opm_result.fourth_contact_jd_ut,
        2.0 / 86400.0,
        "semi-analytical occultation C4 tracks OPM2",
        failures);
    expect_true(semi_analytic_result.evaluation_count > 0, "semi-analytical occultation performed evaluations", failures);
}

void test_lunar_body_occultation_semi_analytic_ancient_seed_regression(int* failures) {
    using namespace taiyin::runtime;

    NativeCalcContext context = make_geocentric_context();
    expect_status(
        native_context_set_route_rule(&context, TAIYIN_EPHEMERIS_ROUTE_SEMI_ANALYTIC),
        taiyin::TAIYIN_STATUS_OK,
        "set semi-analytical route for ancient lunar body occultation seeds",
        failures);

    struct Case {
        int body_id;
        double start_jd_ut;
        double expected_jd_ut;
        const char* label;
    };
    const Case cases[] = {
        {
            taiyin::TAIYIN_BODY_MERCURY,
            jd_ut(-1000, 1, 1, 0.0),
            1356408.813653436722,
            "semi-analytical ancient Mercury lunar occultation seed regression",
        },
        {
            taiyin::TAIYIN_BODY_VENUS,
            jd_ut(-1000, 1, 1, 0.0),
            1356020.545352539513,
            "semi-analytical ancient Venus lunar occultation seed regression",
        },
        {
            taiyin::TAIYIN_BODY_MARS_BARYCENTER,
            jd_ut(-1000, 1, 1, 0.0),
            1356300.503603027435,
            "semi-analytical ancient Mars barycenter lunar occultation seed regression",
        },
        {
            taiyin::TAIYIN_BODY_MERCURY,
            jd_ut(0, 1, 1, 0.0),
            1721405.519355713623,
            "semi-analytical year 0 Mercury lunar occultation seed regression",
        },
        {
            taiyin::TAIYIN_BODY_VENUS,
            jd_ut(0, 1, 1, 0.0),
            1721229.137530456064,
            "semi-analytical year 0 Venus lunar occultation seed regression",
        },
        {
            taiyin::TAIYIN_BODY_MARS_BARYCENTER,
            jd_ut(0, 1, 1, 0.0),
            1722073.928310092073,
            "semi-analytical year 0 Mars barycenter lunar occultation seed regression",
        },
        {
            taiyin::TAIYIN_BODY_MERCURY,
            jd_ut(1000, 1, 1, 0.0),
            2086907.600520880660,
            "semi-analytical medieval Mercury lunar occultation seed regression",
        },
        {
            taiyin::TAIYIN_BODY_VENUS,
            jd_ut(1000, 1, 1, 0.0),
            2086408.148638136452,
            "semi-analytical medieval Venus lunar occultation seed regression",
        },
        {
            taiyin::TAIYIN_BODY_MARS_BARYCENTER,
            jd_ut(1000, 1, 1, 0.0),
            2086987.517650492257,
            "semi-analytical medieval Mars barycenter lunar occultation seed regression",
        },
    };

    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        LunarBodyOccultationSearchResult result;
        EphemerisEvalDiagnostic diagnostic;
        expect_status(
            search_next_geocentric_lunar_body_occultation_ut(
                &context,
                cases[i].body_id,
                cases[i].start_jd_ut,
                0u,
                &result,
                &diagnostic),
            taiyin::TAIYIN_STATUS_OK,
            cases[i].label,
            failures);
        expect_equal(result.kind, TAIYIN_OCCULTATION_KIND_LUNAR_BODY, "semi-analytical ancient seed kind", failures);
        expect_near(result.jd_ut, cases[i].expected_jd_ut, 0.05, cases[i].label, failures);
        expect_true(result.jd_ut > cases[i].start_jd_ut, "semi-analytical ancient seed result after start", failures);
        expect_true(result.begin_jd_ut < result.jd_ut, "semi-analytical ancient seed begin before maximum", failures);
        expect_true(result.end_jd_ut > result.jd_ut, "semi-analytical ancient seed end after maximum", failures);
        expect_true(result.target_radius_rad > 0.0, "semi-analytical ancient seed target radius positive", failures);
        expect_true(result.margin_rad >= 0.0, "semi-analytical ancient seed margin nonnegative", failures);
        expect_true(result.evaluation_count > 0, "semi-analytical ancient seed evaluated samples", failures);
        expect_true(result.evaluation_count < 700, "semi-analytical ancient seed avoids dense scan", failures);
    }
}

void test_lunar_occultation_local_visibility_summary(int* failures) {
    using namespace taiyin::runtime;

    const std::string catalog_path = repo_fixed_star_catalog_path();
    expect_status(
        add_global_tsc1_star_catalog(catalog_path.c_str()),
        taiyin::TAIYIN_STATUS_OK,
        "load packaged fixed-star TSC1 for occultation visibility",
        failures);

    NativeCalcContext star_context = make_topocentric_context(
        -78.709289952229,
        24.897937227562,
        0.0);
    LunarStarOccultationSearchResult star_occultation;
    EphemerisEvalDiagnostic diagnostic;
    expect_status(
        search_next_local_lunar_star_occultation_ut(
            &star_context,
            "antares",
            jd_ut(2024, 1, 1, 0.0),
            0u,
            &star_occultation,
            &diagnostic),
        taiyin::TAIYIN_STATUS_OK,
        "search Antares local occultation for visibility summary",
        failures);

    // This is a local-circumstances fixture for Taiyin's structured summary.
    // The local search/contact times are separately compared with SwissEph in
    // the Swiss oracle tests above; interval arrays and phenomena are Taiyin-only
    // summary products.
    expect_near(
        star_occultation.jd_ut,
        2460318.1365588373,
        1.0e-7,
        "Antares local search maximum fixture",
        failures);
    expect_near(
        star_occultation.begin_jd_ut,
        2460318.1045534317,
        1.0e-7,
        "Antares local search begin fixture",
        failures);
    expect_near(
        star_occultation.end_jd_ut,
        2460318.168365567,
        1.0e-7,
        "Antares local search end fixture",
        failures);

    LunarOccultationLocalVisibility star_visibility;
    expect_status(
        compute_lunar_star_occultation_local_visibility_ut(
            &star_context,
            "antares",
            &star_occultation,
            0u,
            &star_visibility,
            &diagnostic),
        taiyin::TAIYIN_STATUS_OK,
        "compute Antares local occultation visibility summary",
        failures);
    expect_true(star_visibility.first_contact.valid != 0, "star visibility C1 valid", failures);
    expect_true(star_visibility.maximum.valid != 0, "star visibility maximum valid", failures);
    expect_true(star_visibility.fourth_contact.valid != 0, "star visibility C4 valid", failures);
    expect_equal(star_visibility.second_contact.valid, 0, "star visibility C2 invalid for point star", failures);
    expect_equal(star_visibility.third_contact.valid, 0, "star visibility C3 invalid for point star", failures);
    expect_true(std::isfinite(star_visibility.maximum.moon_altitude_rad), "star visibility Moon altitude finite", failures);
    expect_true(std::isfinite(star_visibility.maximum.target_altitude_rad), "star visibility target altitude finite", failures);
    expect_true(std::isfinite(star_visibility.maximum.sun_altitude_rad), "star visibility Sun altitude finite", failures);
    expect_true(
        std::fabs(star_visibility.maximum.moon_altitude_rad - star_visibility.maximum.target_altitude_rad) < 0.02,
        "star visibility Moon and target altitude close at maximum",
        failures);
    expect_true(
        taiyin::split_julian_date_is_finite(star_visibility.visible_begin_jd_ut)
            && taiyin::split_julian_date_is_finite(star_visibility.visible_end_jd_ut)
            && star_visibility.visible_begin_jd_ut <= star_occultation.jd_ut
            && star_visibility.visible_end_jd_ut >= star_occultation.jd_ut,
        "star visibility interval covers maximum",
        failures);
    expect_near(
        star_visibility.visible_begin_jd_ut,
        2460318.1045534317,
        1.0e-7,
        "Antares visible begin fixture",
        failures);
    expect_near(
        star_visibility.visible_end_jd_ut,
        2460318.168365567,
        1.0e-7,
        "Antares visible end fixture",
        failures);
    expect_true(star_visibility.visible_interval_count > 0, "star visibility interval list populated", failures);
    expect_equal(star_visibility.visible_interval_count, 1, "Antares visible interval count fixture", failures);
    expect_near(
        star_visibility.visible_intervals[0].begin_jd_ut,
        star_visibility.visible_begin_jd_ut,
        1.0e-12,
        "star first visible interval mirrors legacy begin",
        failures);
    expect_near(
        star_visibility.visible_intervals[0].end_jd_ut,
        star_visibility.visible_end_jd_ut,
        1.0e-12,
        "star first visible interval mirrors legacy end",
        failures);
    expect_equal(star_visibility.dark_visible_interval_count, 0, "Antares dark visible interval count fixture", failures);
    expect_true(!taiyin::split_julian_date_is_finite(star_visibility.dark_visible_begin_jd_ut), "Antares dark visible begin absent", failures);
    expect_true(!taiyin::split_julian_date_is_finite(star_visibility.dark_visible_end_jd_ut), "Antares dark visible end absent", failures);
    expect_true(
        std::isfinite(star_occultation.phenomena.angular_distance_rad)
            && star_occultation.phenomena.occulted_fraction == 1.0,
        "star search phenomena point target covered",
        failures);
    expect_near(
        star_occultation.phenomena.angular_distance_rad,
        0.000184264845 * taiyin::TAIYIN_DEG_TO_RAD,
        1.0e-6,
        "Antares search phenomena angular distance vs SwissEph attr[7]",
        failures);
    expect_near(star_occultation.phenomena.magnitude, 1.0, 1.0e-15, "Antares search phenomena magnitude vs SwissEph attr[0]", failures);
    expect_near(
        star_occultation.phenomena.occulted_fraction,
        1.0,
        1.0e-15,
        "Antares search phenomena occulted fraction fixture",
        failures);
    expect_near(
        star_visibility.maximum.moon_altitude_rad,
        0.65699833879090708,
        kDerivedAltitudeFixtureToleranceRad,
        "Antares local maximum Moon altitude fixture",
        failures);
    expect_near(
        star_visibility.maximum.target_altitude_rad,
        37.643328843642 * taiyin::TAIYIN_DEG_TO_RAD,
        2.0e-5,
        "Antares local maximum target true altitude vs SwissEph attr[5]",
        failures);
    expect_near(
        normalize_degrees(star_visibility.maximum.target_azimuth_rad * taiyin::TAIYIN_RAD_TO_DEG),
        normalize_degrees(11.792913055355 + 180.0),
        0.02,
        "Antares local maximum target azimuth vs SwissEph attr[4]",
        failures);
    expect_near(
        star_visibility.maximum.sun_altitude_rad,
        0.59290925946054329,
        kDerivedAltitudeFixtureToleranceRad,
        "Antares local maximum Sun altitude fixture",
        failures);
    expect_true(
        !taiyin::split_julian_date_is_finite(star_visibility.target_rise_jd_ut)
            || (star_visibility.target_rise_jd_ut >= star_occultation.begin_jd_ut
                && star_visibility.target_rise_jd_ut <= star_occultation.end_jd_ut),
        "star visibility target rise inside event when present",
        failures);
    expect_true(
        !taiyin::split_julian_date_is_finite(star_visibility.target_set_jd_ut)
            || (star_visibility.target_set_jd_ut >= star_occultation.begin_jd_ut
                && star_visibility.target_set_jd_ut <= star_occultation.end_jd_ut),
        "star visibility target set inside event when present",
        failures);

    NativeCalcContext geocentric = make_geocentric_context();
    LunarOccultationLocalVisibility invalid_visibility;
    expect_status(
        compute_lunar_star_occultation_local_visibility_ut(
            &geocentric,
            "antares",
            &star_occultation,
            0u,
            &invalid_visibility,
            &diagnostic),
        taiyin::TAIYIN_ERROR_INVALID_ARGUMENT,
        "star local visibility requires observer",
        failures);
    expect_status(
        compute_lunar_star_occultation_local_visibility_ut(
            &star_context,
            "antares",
            &star_occultation,
            1u << 31,
            &invalid_visibility,
            &diagnostic),
        taiyin::TAIYIN_ERROR_INVALID_ARGUMENT,
        "star local visibility rejects unknown flags",
        failures);

    NativeCalcContext body_context = make_topocentric_context(
        -144.104686755054,
        -10.079501905368,
        0.0);
    LunarBodyOccultationSearchResult body_occultation;
    expect_status(
        search_next_local_lunar_body_occultation_ut(
            &body_context,
            taiyin::TAIYIN_BODY_MERCURY,
            2460900.5,
            0u,
            &body_occultation,
            &diagnostic),
        taiyin::TAIYIN_STATUS_OK,
        "search Mercury local occultation for visibility summary",
        failures);

    // This is a local-circumstances fixture for Taiyin's structured summary.
    // The local search/contact times are separately compared with SwissEph in
    // the Swiss oracle tests above; interval arrays and phenomena are Taiyin-only
    // summary products.
    expect_near(
        body_occultation.jd_ut,
        2461090.4651050493,
        1.0e-7,
        "Mercury local search maximum fixture",
        failures);
    expect_near(
        body_occultation.begin_jd_ut,
        2461090.430932260118,
        1.0e-7,
        "Mercury local search begin fixture",
        failures);
    expect_near(
        body_occultation.end_jd_ut,
        2461090.498786330223,
        1.0e-7,
        "Mercury local search end fixture",
        failures);
    expect_near(
        body_occultation.second_contact_jd_ut,
        2461090.431180335581,
        1.0e-7,
        "Mercury local search C2 fixture",
        failures);
    expect_near(
        body_occultation.third_contact_jd_ut,
        2461090.498544779141,
        1.0e-7,
        "Mercury local search C3 fixture",
        failures);

    LunarOccultationLocalVisibility body_visibility;
    expect_status(
        compute_lunar_body_occultation_local_visibility_ut(
            &body_context,
            taiyin::TAIYIN_BODY_MERCURY,
            &body_occultation,
            0u,
            &body_visibility,
            &diagnostic),
        taiyin::TAIYIN_STATUS_OK,
        "compute Mercury local occultation visibility summary",
        failures);
    expect_true(body_visibility.first_contact.valid != 0, "body visibility C1 valid", failures);
    expect_true(body_visibility.second_contact.valid != 0, "body visibility C2 valid", failures);
    expect_true(body_visibility.maximum.valid != 0, "body visibility maximum valid", failures);
    expect_true(body_visibility.third_contact.valid != 0, "body visibility C3 valid", failures);
    expect_true(body_visibility.fourth_contact.valid != 0, "body visibility C4 valid", failures);
    expect_true(std::isfinite(body_visibility.maximum.moon_azimuth_rad), "body visibility Moon azimuth finite", failures);
    expect_true(std::isfinite(body_visibility.maximum.target_azimuth_rad), "body visibility target azimuth finite", failures);
    expect_true(
        std::fabs(body_visibility.maximum.moon_altitude_rad - body_visibility.maximum.target_altitude_rad) < 0.02,
        "body visibility Moon and target altitude close at maximum",
        failures);
    expect_true(
        taiyin::split_julian_date_is_finite(body_visibility.visible_begin_jd_ut)
            && taiyin::split_julian_date_is_finite(body_visibility.visible_end_jd_ut)
            && body_visibility.visible_begin_jd_ut <= body_occultation.jd_ut
            && body_visibility.visible_end_jd_ut >= body_occultation.jd_ut,
        "body visibility interval covers maximum",
        failures);
    expect_near(
        body_visibility.visible_begin_jd_ut,
        2461090.430932260118,
        1.0e-7,
        "Mercury visible begin fixture",
        failures);
    expect_near(
        body_visibility.visible_end_jd_ut,
        2461090.498786330223,
        1.0e-7,
        "Mercury visible end fixture",
        failures);
    expect_true(body_visibility.visible_interval_count > 0, "body visibility interval list populated", failures);
    expect_equal(body_visibility.visible_interval_count, 1, "Mercury visible interval count fixture", failures);
    expect_equal(body_visibility.dark_visible_interval_count, 0, "Mercury dark visible interval count fixture", failures);
    expect_true(
        std::isfinite(body_occultation.phenomena.diameter_ratio)
            && body_occultation.phenomena.diameter_ratio > 0.0
            && std::isfinite(body_occultation.phenomena.occulted_fraction)
            && body_occultation.phenomena.occulted_fraction >= 0.0
            && body_occultation.phenomena.occulted_fraction <= 1.0,
        "body search phenomena finite",
        failures);
    expect_near(
        body_occultation.phenomena.angular_distance_rad,
        0.000004268868 * taiyin::TAIYIN_DEG_TO_RAD,
        1.0e-7,
        "Mercury search phenomena angular distance vs SwissEph attr[7]",
        failures);
    expect_near(
        body_occultation.phenomena.diameter_ratio,
        274.403649859152,
        0.25,
        "Mercury search phenomena diameter ratio vs SwissEph attr[1]",
        failures);
    expect_near(
        body_occultation.phenomena.magnitude,
        137.699631654390,
        0.15,
        "Mercury search phenomena magnitude vs SwissEph attr[0]",
        failures);
    expect_near(
        body_occultation.phenomena.obscuration,
        75297.363056024304,
        100.0,
        "Mercury search phenomena obscuration vs SwissEph attr[2]",
        failures);
    expect_near(
        body_occultation.phenomena.occulted_fraction,
        1.0,
        1.0e-15,
        "Mercury search phenomena occulted fraction fixture",
        failures);
    expect_near(
        body_visibility.maximum.moon_altitude_rad,
        1.4451985267320839,
        kDerivedAltitudeFixtureToleranceRad,
        "Mercury local maximum Moon altitude fixture",
        failures);
    expect_near(
        body_visibility.maximum.target_altitude_rad,
        82.802950613841 * taiyin::TAIYIN_DEG_TO_RAD,
        2.0e-5,
        "Mercury local maximum target true altitude vs SwissEph attr[5]",
        failures);
    expect_near(
        normalize_degrees(body_visibility.maximum.target_azimuth_rad * taiyin::TAIYIN_RAD_TO_DEG),
        normalize_degrees(152.337702927429 + 180.0),
        0.03,
        "Mercury local maximum target azimuth vs SwissEph attr[4]",
        failures);
    expect_near(
        body_visibility.maximum.sun_altitude_rad,
        1.2292709942637432,
        kDerivedAltitudeFixtureToleranceRad,
        "Mercury local maximum Sun altitude fixture",
        failures);
}

void test_lunar_body_occultation_de441_edge_seed_regression(int* failures) {
    using namespace taiyin::runtime;

    const std::string de441 = de441_path();
    if (!file_exists(de441)) {
        std::printf("test_occultation_search: SKIPPED external DE441 SPK edge seed regression\n");
        return;
    }
    if (!initialize_de441_runtime(de441, failures)) {
        return;
    }

    NativeCalcContext context = make_geocentric_context();
    expect_status(
        native_context_set_route_rule(&context, TAIYIN_EPHEMERIS_ROUTE_SPK),
        taiyin::TAIYIN_STATUS_OK,
        "set SPK route for DE441 edge occultation seeds",
        failures);

    const double de441_start_jd = -3100015.5;
    const double de441_end_jd = 8000016.5;
    const double early_start = de441_start_jd + 365.25 * 5.0;
    const double late_start = de441_end_jd - 365.25 * 5.0;

    struct Case {
        int body_id;
        double start_jd_ut;
        uint64_t flags;
        double expected_jd_ut;
        const char* label;
    };
    const Case cases[] = {
        {
            taiyin::TAIYIN_BODY_MERCURY,
            early_start,
            0u,
            -3097282.144998283591,
            "DE441 early-edge Mercury lunar occultation seed regression",
        },
        {
            taiyin::TAIYIN_BODY_VENUS,
            early_start,
            0u,
            -3097159.885264597367,
            "DE441 early-edge Venus lunar occultation seed regression",
        },
        {
            taiyin::TAIYIN_BODY_MARS_BARYCENTER,
            early_start,
            0u,
            -3097888.250152923632,
            "DE441 early-edge Mars barycenter lunar occultation seed regression",
        },
        {
            taiyin::TAIYIN_BODY_MERCURY,
            late_start,
            TAIYIN_OCCULTATION_SEARCH_BACKWARD,
            7998040.123432542197,
            "DE441 late-edge Mercury lunar occultation seed regression",
        },
        {
            taiyin::TAIYIN_BODY_VENUS,
            late_start,
            TAIYIN_OCCULTATION_SEARCH_BACKWARD,
            7997862.951326374896,
            "DE441 late-edge Venus lunar occultation seed regression",
        },
        {
            taiyin::TAIYIN_BODY_MARS_BARYCENTER,
            late_start,
            TAIYIN_OCCULTATION_SEARCH_BACKWARD,
            7997659.171116663143,
            "DE441 late-edge Mars barycenter lunar occultation seed regression",
        },
    };

    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        LunarBodyOccultationSearchResult result;
        EphemerisEvalDiagnostic diagnostic;
        expect_status(
            search_next_geocentric_lunar_body_occultation_ut(
                &context,
                cases[i].body_id,
                cases[i].start_jd_ut,
                cases[i].flags,
                &result,
                &diagnostic),
            taiyin::TAIYIN_STATUS_OK,
            cases[i].label,
            failures);
        expect_equal(result.kind, TAIYIN_OCCULTATION_KIND_LUNAR_BODY, "DE441 edge seed kind", failures);
        expect_near(result.jd_ut, cases[i].expected_jd_ut, 0.05, cases[i].label, failures);
        if ((cases[i].flags & TAIYIN_OCCULTATION_SEARCH_BACKWARD) != 0u) {
            expect_true(result.jd_ut < cases[i].start_jd_ut, "DE441 late-edge backward result before start", failures);
        } else {
            expect_true(result.jd_ut > cases[i].start_jd_ut, "DE441 early-edge forward result after start", failures);
        }
        expect_true(result.begin_jd_ut < result.jd_ut, "DE441 edge seed begin before maximum", failures);
        expect_true(result.end_jd_ut > result.jd_ut, "DE441 edge seed end after maximum", failures);
        expect_true(result.target_radius_rad > 0.0, "DE441 edge seed target radius positive", failures);
        expect_true(result.margin_rad >= 0.0, "DE441 edge seed margin nonnegative", failures);
        expect_true(result.evaluation_count > 0, "DE441 edge seed evaluated samples", failures);
        expect_true(result.evaluation_count < 800, "DE441 edge seed avoids dense scan", failures);
    }
}

void test_no_occultation_and_missing_star(int* failures) {
    using namespace taiyin::runtime;

    NativeCalcContext context = make_geocentric_context();
    const double center = jd_ut(2024, 4, 8, 18.0);
    LunarStarOccultationSearchResult result;
    EphemerisEvalDiagnostic diagnostic;
    expect_status(
        search_next_geocentric_lunar_star_occultation_ut(
            &context,
            "far_star",
            center,
            TAIYIN_OCCULTATION_SEARCH_TRUEPOS,
            &result,
            &diagnostic),
        taiyin::TAIYIN_EVENT_ERROR_NOT_FOUND,
        "far star is not occulted",
        failures);
    expect_equal(result.kind, TAIYIN_OCCULTATION_KIND_NONE, "far star result kind none", failures);
    expect_equal(result.evaluation_count, 0, "far star rejected before candidate scan", failures);

    expect_status(
        search_next_geocentric_lunar_star_occultation_ut(
            &context,
            "missing_star",
            center,
            TAIYIN_OCCULTATION_SEARCH_TRUEPOS,
            &result,
            &diagnostic),
        taiyin::TAIYIN_FILE_ERROR_NOT_FOUND,
        "missing occultation star propagates file error",
        failures);
}

void test_local_observer_contract(int* failures) {
    using namespace taiyin::runtime;

    const double center = jd_ut(2024, 4, 8, 18.0);
    NativeCalcContext geocentric = make_geocentric_context();
    LunarStarOccultationSearchResult result;
    EphemerisEvalDiagnostic diagnostic;
    expect_status(
        search_next_local_lunar_star_occultation_ut(
            &geocentric,
            "moon_path_star",
            center - 0.25,
            TAIYIN_OCCULTATION_SEARCH_TRUEPOS,
            &result,
            &diagnostic),
        taiyin::TAIYIN_ERROR_INVALID_ARGUMENT,
        "local lunar-star occultation requires observer in context",
        failures);

    NativeCalcContext local = make_zero_topocentric_context();
    expect_status(
        search_next_local_lunar_star_occultation_ut(
            &local,
            "moon_path_star",
            center - 0.25,
            TAIYIN_OCCULTATION_SEARCH_TRUEPOS,
            &result,
            &diagnostic),
        taiyin::TAIYIN_STATUS_OK,
        "local lunar-star occultation uses observer from context",
        failures);
    expect_equal(result.kind, TAIYIN_OCCULTATION_KIND_LUNAR_STAR, "local synthetic occultation kind", failures);
}

void test_lunar_limb_occultation_contacts(int* failures) {
    using namespace taiyin::runtime;

    EphemerisEvalDiagnostic diagnostic;
    expect_status(
        load_global_lunar_limb_model(nullptr),
        taiyin::TAIYIN_STATUS_OK,
        "clear lunar limb before occultation correction test",
        failures);

    NativeCalcContext geocentric = make_geocentric_context();
    LunarBodyOccultationSearchResult missing_model;
    expect_status(
        search_next_geocentric_lunar_body_occultation_ut(
            &geocentric,
            taiyin::TAIYIN_BODY_MERCURY,
            2461090.0,
            TAIYIN_OCCULTATION_LUNAR_LIMB_CORRECTION,
            &missing_model,
            &diagnostic),
        taiyin::TAIYIN_ERROR_UNSUPPORTED,
        "lunar occultation correction requires TLL1 model",
        failures);

    LunarBodyOccultationSearchResult smooth_body;
    expect_status(
        search_next_geocentric_lunar_body_occultation_ut(
            &geocentric,
            taiyin::TAIYIN_BODY_MERCURY,
            2461090.0,
            0u,
            &smooth_body,
            &diagnostic),
        taiyin::TAIYIN_STATUS_OK,
        "smooth Mercury occultation before TLL1 load",
        failures);

    expect_status(
        load_global_lunar_limb_model(repo_lunar_limb_path().c_str()),
        taiyin::TAIYIN_STATUS_OK,
        "load TLL1 for lunar occultation contacts",
        failures);

    LunarBodyOccultationSearchResult attached_but_disabled;
    expect_status(
        search_next_geocentric_lunar_body_occultation_ut(
            &geocentric,
            taiyin::TAIYIN_BODY_MERCURY,
            2461090.0,
            0u,
            &attached_but_disabled,
            &diagnostic),
        taiyin::TAIYIN_STATUS_OK,
        "loaded TLL1 remains opt-in for occultations",
        failures);
    expect_near(
        attached_but_disabled.moon_radius_rad,
        smooth_body.moon_radius_rad,
        0.0,
        "disabled TLL1 preserves smooth occultation radius",
        failures);
    expect_near(
        attached_but_disabled.first_contact_jd_ut,
        smooth_body.first_contact_jd_ut,
        0.0,
        "disabled TLL1 preserves smooth occultation C1",
        failures);
    expect_near(
        attached_but_disabled.fourth_contact_jd_ut,
        smooth_body.fourth_contact_jd_ut,
        0.0,
        "disabled TLL1 preserves smooth occultation C4",
        failures);

    LunarBodyOccultationSearchResult corrected_body;
    expect_status(
        search_next_geocentric_lunar_body_occultation_ut(
            &geocentric,
            taiyin::TAIYIN_BODY_MERCURY,
            2461090.0,
            TAIYIN_OCCULTATION_LUNAR_LIMB_CORRECTION,
            &corrected_body,
            &diagnostic),
        taiyin::TAIYIN_STATUS_OK,
        "TLL1-corrected Mercury occultation",
        failures);
    expect_true(
        std::fabs(corrected_body.moon_radius_rad - smooth_body.moon_radius_rad) > 1.0e-12,
        "TLL1 changes Mercury occultation lunar radius",
        failures);
    expect_true(
        std::fabs(corrected_body.jd_ut - smooth_body.jd_ut) > 1.0e-9
            && std::fabs(corrected_body.jd_ut - smooth_body.jd_ut) < 60.0 / 86400.0,
        "TLL1 independently refines Mercury maximum time",
        failures);
    expect_true(
        std::fabs(corrected_body.first_contact_jd_ut - smooth_body.first_contact_jd_ut) > 1.0e-10,
        "TLL1 changes Mercury occultation C1",
        failures);
    expect_true(
        taiyin::split_julian_date_is_finite(corrected_body.second_contact_jd_ut)
            && std::fabs(corrected_body.second_contact_jd_ut - smooth_body.second_contact_jd_ut) > 1.0e-10,
        "TLL1 changes Mercury occultation C2",
        failures);
    expect_true(
        taiyin::split_julian_date_is_finite(corrected_body.third_contact_jd_ut)
            && std::fabs(corrected_body.third_contact_jd_ut - smooth_body.third_contact_jd_ut) > 1.0e-10,
        "TLL1 changes Mercury occultation C3",
        failures);
    expect_true(
        std::fabs(corrected_body.fourth_contact_jd_ut - smooth_body.fourth_contact_jd_ut) > 1.0e-10,
        "TLL1 changes Mercury occultation C4",
        failures);
    expect_true(
        std::fabs(corrected_body.first_contact_jd_ut - smooth_body.first_contact_jd_ut) < 60.0 / 86400.0
            && std::fabs(corrected_body.second_contact_jd_ut - smooth_body.second_contact_jd_ut) < 60.0 / 86400.0
            && std::fabs(corrected_body.third_contact_jd_ut - smooth_body.third_contact_jd_ut) < 60.0 / 86400.0
            && std::fabs(corrected_body.fourth_contact_jd_ut - smooth_body.fourth_contact_jd_ut) < 60.0 / 86400.0,
        "TLL1 Mercury contact corrections remain bounded",
        failures);
    expect_true(
        corrected_body.first_contact_jd_ut < corrected_body.second_contact_jd_ut
            && corrected_body.second_contact_jd_ut < corrected_body.jd_ut
            && corrected_body.jd_ut < corrected_body.third_contact_jd_ut
            && corrected_body.third_contact_jd_ut < corrected_body.fourth_contact_jd_ut,
        "TLL1 Mercury contacts remain ordered",
        failures);
    expect_true(
        corrected_body.evaluation_count <= smooth_body.evaluation_count + 100,
        "TLL1 Mercury polishing remains bounded",
        failures);

    expect_status(
        add_global_tsc1_star_catalog(repo_fixed_star_catalog_path().c_str()),
        taiyin::TAIYIN_STATUS_OK,
        "load fixed stars for TLL1 occultation test",
        failures);
    NativeCalcContext local = make_topocentric_context(-78.709289952229, 24.897937227562, 0.0);
    LunarStarOccultationSearchResult smooth_star;
    LunarStarOccultationSearchResult corrected_star;
    expect_status(
        search_next_local_lunar_star_occultation_ut(
            &local, "antares", 2460318.0, 0u, &smooth_star, &diagnostic),
        taiyin::TAIYIN_STATUS_OK,
        "smooth local Antares occultation",
        failures);
    expect_status(
        search_next_local_lunar_star_occultation_ut(
            &local,
            "antares",
            2460318.0,
            TAIYIN_OCCULTATION_LUNAR_LIMB_CORRECTION,
            &corrected_star,
            &diagnostic),
        taiyin::TAIYIN_STATUS_OK,
        "TLL1-corrected local Antares occultation",
        failures);
    expect_true(
        std::fabs(corrected_star.first_contact_jd_ut - smooth_star.first_contact_jd_ut) > 1.0e-10
            || std::fabs(corrected_star.fourth_contact_jd_ut - smooth_star.fourth_contact_jd_ut) > 1.0e-10,
        "TLL1 changes at least one local star contact",
        failures);
    expect_true(
        std::fabs(corrected_star.jd_ut - smooth_star.jd_ut) > 1.0e-9
            && std::fabs(corrected_star.jd_ut - smooth_star.jd_ut) < 60.0 / 86400.0,
        "TLL1 independently refines local star maximum time",
        failures);
    expect_true(
        corrected_star.first_contact_jd_ut < corrected_star.jd_ut
            && corrected_star.jd_ut < corrected_star.fourth_contact_jd_ut,
        "TLL1 local star contacts enclose maximum",
        failures);
    expect_true(
        corrected_star.evaluation_count <= smooth_star.evaluation_count + 100,
        "TLL1 local star polishing remains bounded",
        failures);

    LunarStarOccultationSearchResult central_star;
    expect_status(
        search_next_geocentric_lunar_star_occultation_ut(
            &geocentric,
            "moon_path_star",
            jd_ut(2024, 4, 8, 18.0) - 1.0,
            TAIYIN_OCCULTATION_SEARCH_TRUEPOS
                | TAIYIN_OCCULTATION_LUNAR_LIMB_CORRECTION,
            &central_star,
            &diagnostic),
        taiyin::TAIYIN_STATUS_OK,
        "TLL1 accepts a nearly centered synthetic occultation",
        failures);
    expect_true(
        central_star.first_contact_jd_ut < central_star.jd_ut
            && central_star.jd_ut < central_star.fourth_contact_jd_ut,
        "TLL1 centered synthetic contacts enclose maximum",
        failures);

    LunarOccultationWhereResult where;
    expect_status(
        compute_lunar_star_occultation_where_ut(
            &local,
            "antares",
            &corrected_star,
            TAIYIN_OCCULTATION_LUNAR_LIMB_CORRECTION,
            &where,
            &diagnostic),
        taiyin::TAIYIN_ERROR_INVALID_ARGUMENT,
        "where rejects contact-only lunar limb flag",
        failures);

    expect_status(
        load_global_lunar_limb_model(nullptr),
        taiyin::TAIYIN_STATUS_OK,
        "clear lunar limb after occultation correction test",
        failures);
}

}  // namespace

int main() {
    int failures = 0;
    if (initialize_runtime(&failures)) {
        taiyin::runtime::clear_global_star_catalogs();
        test_synthetic_lunar_star_occultation(&failures);
        test_seeded_search_covers_synthetic_lunar_path_grid(&failures);
        test_moving_star_position_is_sampled_at_candidate_epoch(&failures);
        test_packaged_star_catalog_occultation_smoke(&failures);
        test_local_lunar_star_occultation_swiss_oracles(&failures);
        test_lunar_body_occultation_smoke(&failures);
        test_lunar_body_occultation_custom_radius(&failures);
        test_lunar_body_occultation_seed_regression_all_major_targets(&failures);
        test_lunar_body_occultation_seed_matches_coarse_scan(&failures);
        test_lunar_body_occultation_backward_seed_matches_coarse_scan(&failures);
        test_local_lunar_body_occultation_seed_matches_coarse_scan(&failures);
        test_lunar_body_occultation_swiss_oracles(&failures);
        test_lunar_body_occultation_search_options(&failures);
        test_local_lunar_body_occultation_swiss_oracles(&failures);
        test_lunar_occultation_where_swiss_oracles(&failures);
        test_lunar_body_occultation_semi_analytic_route_converges(&failures);
        test_lunar_body_occultation_semi_analytic_ancient_seed_regression(&failures);
        test_lunar_occultation_local_visibility_summary(&failures);
        test_no_occultation_and_missing_star(&failures);
        test_local_observer_contract(&failures);
        test_lunar_limb_occultation_contacts(&failures);
        taiyin::runtime::clear_global_star_catalogs();
    }
    test_lunar_body_occultation_de441_edge_seed_regression(&failures);
    if (failures != 0) {
        std::cerr << failures << " occultation search checks failed\n";
        return 1;
    }
    return 0;
}
