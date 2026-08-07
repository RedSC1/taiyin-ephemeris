#include "taiyin/runtime/star_visibility.h"

#include "taiyin/body_id.h"
#include "taiyin/internal/star_file.h"
#include "taiyin/runtime/native_context.h"
#include "taiyin/runtime/runtime.h"
#include "taiyin/runtime/star_position.h"
#include "taiyin/status.h"
#include "taiyin/time.h"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <string>
#include <unistd.h>

namespace {

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
        std::cerr << "FAIL: " << label << ": actual=" << actual
                  << " expected=" << expected
                  << " diff=" << diff
                  << " tolerance=" << tolerance << "\n";
        ++(*failures);
    }
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

std::string repo_opm2_major_body_root() {
    const char* root = std::getenv("TAIYIN_REPO_ROOT");
    if (root && root[0] != '\0') {
        return std::string(root) + "/data/ephemerides/opm2/major-bodies/600y";
    }
    return "../data/ephemerides/opm2/major-bodies/600y";
}

bool initialize_runtime(int* failures) {
    taiyin::runtime::EphemerisRuntimeConfig config;
    const std::string data_root = repo_opm2_major_body_root();
    const char* source_paths[] = { data_root.c_str() };
    config.source_paths = source_paths;
    config.source_path_count = 1;
    config.load_packaged_data = false;
    config.segment_cache_max_entries = 256;
    const bool ok = taiyin::runtime::initialize_global_ephemeris_runtime(config);
    expect_true(ok, "initialize OPM2 runtime", failures);
    return ok;
}

std::string make_temp_dir() {
    char templ[] = "/tmp/taiyin-star-visibility-XXXXXX";
    char* path = mkdtemp(templ);
    return path ? std::string(path) : std::string();
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
    entry.ra_deg = ra_deg;
    entry.dec_deg = dec_deg;
    entry.pm_ra_mas_yr = 0.0;
    entry.pm_dec_mas_yr = 0.0;
    entry.parallax_mas = 0.0;
    entry.radial_velocity_km_s = 0.0;
    entry.reference_epoch = 2000.0;
    return entry;
}

bool install_test_star_catalog(std::string* out_path, int* failures) {
    const std::string root = make_temp_dir();
    expect_true(!root.empty(), "make temp star visibility dir", failures);
    if (root.empty()) return false;

    taiyin::internal::Tsf1StarEntry entries[2];
    entries[0] = make_star("equator_star", "Equator Star", 0.0, 0.0);
    entries[1] = make_star("north_pole_star", "North Pole Star", 0.0, 88.0);
    const std::string path = root + "/stars.tsf1";
    expect_true(taiyin::internal::save_star_file(path, entries, 2), "save star visibility TSF1", failures);
    expect_status(
        taiyin::runtime::add_global_tsf1_star_catalog(path.c_str()),
        taiyin::TAIYIN_STATUS_OK,
        "load star visibility TSF1",
        failures);
    if (out_path) *out_path = path;
    return true;
}

const taiyin::SplitJulianDate jd_ut(int year, int month, int day, double hour) {
    taiyin::SplitJulianDate out;
    taiyin::julian_day_split(
        {year, month, day, static_cast<int>(hour), 0, (hour - static_cast<int>(hour)) * 3600.0},
        &out);
    return out;
}

taiyin::runtime::NativeCalcContext make_context(
    double longitude_deg,
    double latitude_deg,
    double height_m
) {
    taiyin::runtime::NativeCalcContext context;
    taiyin::runtime::native_context_set_geocentric_observer(
        &context,
        taiyin::TAIYIN_BODY_EARTH,
        taiyin::TAIYIN_BODY_EARTH);
    taiyin::runtime::native_context_set_observer_location(
        &context,
        taiyin::runtime::native_observer_location_degrees(longitude_deg, latitude_deg, height_m));
    taiyin::runtime::native_context_set_atmosphere(&context, taiyin::runtime::native_standard_atmosphere());
    taiyin::runtime::native_context_use_solar_deflector(&context);
    context.apparent_options.flags =
        taiyin::TAIYIN_APPARENT_SPHERICAL
        | taiyin::TAIYIN_APPARENT_LIGHT_TIME
        | taiyin::TAIYIN_APPARENT_ABERRATION
        | taiyin::TAIYIN_APPARENT_DEFLECTION;
    context.apparent_options.output_frame_id = taiyin::TAIYIN_APPARENT_FRAME_TRUE_ECLIPTIC_OF_DATE;
    return context;
}

void test_equator_star_visibility(int* failures) {
    using namespace taiyin::runtime;

    NativeCalcContext context = make_context(0.0, 0.0, 0.0);
    const taiyin::SplitJulianDate start = jd_ut(2024, 4, 8, 0.0);
    StarVisibilityEventResult rise;
    expect_status(
        search_star_rise_set_ut(
            &context,
            "equator_star",
            start,
            start + 1.0,
            TAIYIN_STAR_VISIBILITY_EVENT_RISE,
            TAIYIN_STAR_VISIBILITY_FLAG_NO_REFRACTION,
            &rise),
        taiyin::TAIYIN_STATUS_OK,
        "equator star rise search",
        failures);
    expect_equal(rise.altitude_state, TAIYIN_STAR_VISIBILITY_ALTITUDE_STATE_CROSSES, "equator star rises", failures);
    expect_equal(rise.crossing_direction, TAIYIN_STAR_VISIBILITY_CROSSING_RISING, "equator star rise direction", failures);
    expect_true(taiyin::split_julian_date_is_finite(rise.jd_ut), "equator star rise jd finite", failures);
    expect_near(rise.residual_rad, 0.0, 1.0e-8, "equator star rise residual", failures);

    StarVisibilityEventResult default_refracted_rise;
    expect_status(
        search_star_rise_set_ut(
            &context,
            "equator_star",
            start,
            start + 1.0,
            TAIYIN_STAR_VISIBILITY_EVENT_RISE,
            0u,
            &default_refracted_rise),
        taiyin::TAIYIN_STATUS_OK,
        "equator star default refracted rise search",
        failures);
    expect_equal(
        default_refracted_rise.altitude_state,
        TAIYIN_STAR_VISIBILITY_ALTITUDE_STATE_CROSSES,
        "equator star default refracted rise crosses",
        failures);
    expect_equal(
        default_refracted_rise.crossing_direction,
        TAIYIN_STAR_VISIBILITY_CROSSING_RISING,
        "equator star default refracted rise direction",
        failures);
    expect_true(
        taiyin::split_julian_date_is_finite(default_refracted_rise.jd_ut),
        "equator star default refracted rise jd finite",
        failures);

    StarVisibilityEventResult set;
    expect_status(
        search_star_rise_set_ut(
            &context,
            "equator_star",
            start,
            start + 1.0,
            TAIYIN_STAR_VISIBILITY_EVENT_SET,
            TAIYIN_STAR_VISIBILITY_FLAG_NO_REFRACTION,
            &set),
        taiyin::TAIYIN_STATUS_OK,
        "equator star set search",
        failures);
    expect_equal(set.altitude_state, TAIYIN_STAR_VISIBILITY_ALTITUDE_STATE_CROSSES, "equator star sets", failures);
    expect_equal(set.crossing_direction, TAIYIN_STAR_VISIBILITY_CROSSING_SETTING, "equator star set direction", failures);
    expect_true(taiyin::split_julian_date_is_finite(set.jd_ut), "equator star set jd finite", failures);
    expect_near(set.residual_rad, 0.0, 1.0e-8, "equator star set residual", failures);
    expect_true(rise.jd_ut < set.jd_ut, "equator star rise before set in test window", failures);

    StarVisibilityEventResult upper;
    expect_status(
        search_star_transit_ut(
            &context,
            "equator_star",
            start,
            start + 1.0,
            TAIYIN_STAR_VISIBILITY_EVENT_UPPER_TRANSIT,
            &upper),
        taiyin::TAIYIN_STATUS_OK,
        "equator star upper transit search",
        failures);
    expect_equal(upper.altitude_state, TAIYIN_STAR_VISIBILITY_ALTITUDE_STATE_CROSSES, "equator star upper transit crosses", failures);
    expect_true(taiyin::split_julian_date_is_finite(upper.jd_ut), "equator star upper transit jd finite", failures);
    expect_near(upper.residual_rad, 0.0, 1.0e-8, "equator star transit residual", failures);

    NativeCalcContext equatorial_context = context;
    equatorial_context.apparent_options.output_frame_id = taiyin::TAIYIN_APPARENT_FRAME_TRUE_EQUATOR_OF_DATE;
    StarVisibilityEventResult equatorial_upper;
    expect_status(
        search_star_transit_ut(
            &equatorial_context,
            "equator_star",
            start,
            start + 1.0,
            TAIYIN_STAR_VISIBILITY_EVENT_UPPER_TRANSIT,
            &equatorial_upper),
        taiyin::TAIYIN_STATUS_OK,
        "equator star upper transit search from equatorial context",
        failures);
    expect_near(
        equatorial_upper.jd_ut,
        upper.jd_ut,
        1.0e-8,
        "star transit ignores caller output frame",
        failures);
}

void test_circumpolar_star_visibility(int* failures) {
    using namespace taiyin::runtime;

    NativeCalcContext context = make_context(0.0, 60.0, 0.0);
    const taiyin::SplitJulianDate start = jd_ut(2024, 4, 8, 0.0);
    StarVisibilityEventResult result;
    expect_status(
        search_star_rise_set_ut(
            &context,
            "north_pole_star",
            start,
            start + 1.0,
            TAIYIN_STAR_VISIBILITY_EVENT_RISE,
            TAIYIN_STAR_VISIBILITY_FLAG_NO_REFRACTION,
            &result),
        taiyin::TAIYIN_STATUS_OK,
        "circumpolar star rise search",
        failures);
    expect_equal(result.altitude_state, TAIYIN_STAR_VISIBILITY_ALTITUDE_STATE_ALWAYS_ABOVE, "circumpolar star always above", failures);
    expect_true(result.min_residual_rad > 0.0, "circumpolar star min residual positive", failures);
    expect_true(!taiyin::split_julian_date_is_finite(result.jd_ut), "circumpolar star rise jd is nan", failures);

    StarVisibilityEventResult upper;
    expect_status(
        search_star_transit_ut(
            &context,
            "north_pole_star",
            start,
            start + 1.0,
            TAIYIN_STAR_VISIBILITY_EVENT_UPPER_TRANSIT,
            &upper),
        taiyin::TAIYIN_STATUS_OK,
        "circumpolar star upper transit search",
        failures);
    expect_equal(
        upper.altitude_state,
        TAIYIN_STAR_VISIBILITY_ALTITUDE_STATE_CROSSES,
        "circumpolar star upper transit crosses",
        failures);
    expect_true(taiyin::split_julian_date_is_finite(upper.jd_ut), "circumpolar star upper transit jd finite", failures);
    expect_near(upper.residual_rad, 0.0, 1.0e-8, "circumpolar star upper transit residual", failures);
}

void test_flag_validation(int* failures) {
    using namespace taiyin::runtime;

    NativeCalcContext context = make_context(0.0, 0.0, 0.0);
    const taiyin::SplitJulianDate start = jd_ut(2024, 4, 8, 0.0);
    StarVisibilityEventResult result;
    expect_status(
        search_star_rise_set_ut(
            &context,
            "equator_star",
            start,
            start + 1.0,
            TAIYIN_STAR_VISIBILITY_EVENT_RISE,
            TAIYIN_STAR_VISIBILITY_FLAG_REFRACTION | TAIYIN_STAR_VISIBILITY_FLAG_NO_REFRACTION,
            &result),
        taiyin::TAIYIN_ERROR_INVALID_ARGUMENT,
        "star visibility rejects conflicting refraction flags",
        failures);
    expect_status(
        search_star_rise_set_ut(
            &context,
            "missing_star",
            start,
            start + 1.0,
            TAIYIN_STAR_VISIBILITY_EVENT_RISE,
            TAIYIN_STAR_VISIBILITY_FLAG_NO_REFRACTION,
            &result),
        taiyin::TAIYIN_FILE_ERROR_NOT_FOUND,
        "star visibility reports missing star",
        failures);

    NativeCalcContext missing_atmosphere = context;
    missing_atmosphere.fields.clear(TAIYIN_NATIVE_FIELD_ATMOSPHERE);
    missing_atmosphere.fields.clear(TAIYIN_NATIVE_FIELD_ATMOSPHERE_PRESSURE);
    missing_atmosphere.fields.clear(TAIYIN_NATIVE_FIELD_ATMOSPHERE_TEMPERATURE);
    expect_status(
        native_context_set_atmosphere_policy_flags(
            &missing_atmosphere, TAIYIN_NATIVE_ATMOSPHERE_ALLOW_STANDARD_FALLBACK),
        taiyin::TAIYIN_STATUS_OK,
        "star visibility enables standard-atmosphere fallback",
        failures);
    expect_status(
        search_star_rise_set_ut(
            &missing_atmosphere,
            "equator_star",
            start,
            start + 1.0,
            TAIYIN_STAR_VISIBILITY_EVENT_RISE,
            TAIYIN_STAR_VISIBILITY_STRICT_METEOROLOGY,
            &result),
        taiyin::TAIYIN_ERROR_INVALID_ARGUMENT,
        "star visibility strict mode bypasses standard atmosphere",
        failures);

    NativeCalcContext no_location_context;
    native_context_set_geocentric_observer(
        &no_location_context,
        taiyin::TAIYIN_BODY_EARTH,
        taiyin::TAIYIN_BODY_EARTH);
    expect_status(
        search_star_transit_ut(
            &no_location_context,
            "equator_star",
            start,
            start + 1.0,
            TAIYIN_STAR_VISIBILITY_EVENT_UPPER_TRANSIT,
            &result),
        taiyin::TAIYIN_ERROR_INVALID_ARGUMENT,
        "star transit requires observer location",
        failures);
}

}  // namespace

int main() {
    int failures = 0;
    if (initialize_runtime(&failures)) {
        taiyin::runtime::clear_global_star_catalogs();
        std::string catalog_path;
        if (install_test_star_catalog(&catalog_path, &failures)) {
            test_equator_star_visibility(&failures);
            test_circumpolar_star_visibility(&failures);
            test_flag_validation(&failures);
        }
        taiyin::runtime::clear_global_star_catalogs();
    }
    if (failures != 0) {
        std::cerr << failures << " star visibility checks failed\n";
        return 1;
    }
    return 0;
}
