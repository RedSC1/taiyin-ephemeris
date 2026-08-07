#include "taiyin/dispatch.h"
#include "taiyin/angle.h"
#include "taiyin/internal/star_file.h"
#include "taiyin/runtime/heliacal_visibility.h"
#include "taiyin/runtime/runtime.h"
#include "taiyin/runtime/star_position.h"
#include "taiyin/time.h"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <string>
#include <unistd.h>

namespace {

void expect_true(bool value, const char* label, int* failures) {
    if (!value) {
        std::cerr << "FAIL: expected true: " << label << "\n";
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
    if (!std::isfinite(actual) || std::fabs(actual - expected) > tolerance) {
        std::cerr << "FAIL: " << label << ": actual=" << actual
                  << " expected=" << expected << " tolerance=" << tolerance << "\n";
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

void expect_near(
    const taiyin::SplitJulianDate& actual,
    double expected,
    double tolerance,
    const char* label,
    int* failures
) {
    expect_near(taiyin::split_julian_date_to_double(actual), expected, tolerance, label, failures);
}

bool custom_profile(const void* input, void* output) {
    const taiyin::runtime::HeliacalVisibilityModelInput* model_input =
        static_cast<const taiyin::runtime::HeliacalVisibilityModelInput*>(input);
    taiyin::runtime::HeliacalVisibilityResult* result =
        static_cast<taiyin::runtime::HeliacalVisibilityResult*>(output);
    if (!model_input || !result) return false;
    *result = taiyin::runtime::HeliacalVisibilityResult();
    result->visible = 1;
    result->target_magnitude = model_input->target_magnitude;
    return true;
}

std::string opm2_major_body_root() {
    const char* root = std::getenv("TAIYIN_REPO_ROOT");
    if (root && root[0] != '\0') {
        return std::string(root) + "/data/ephemerides/opm2/major-bodies/600y";
    }
    return "../data/ephemerides/opm2/major-bodies/600y";
}

bool initialize_runtime(int* failures) {
    taiyin::runtime::EphemerisRuntimeConfig config;
    const std::string root = opm2_major_body_root();
    const char* source_paths[] = {root.c_str()};
    config.source_paths = source_paths;
    config.source_path_count = 1;
    config.load_packaged_data = false;
    config.segment_cache_max_entries = 64;
    const bool initialized = taiyin::runtime::initialize_global_ephemeris_runtime(config);
    expect_true(initialized, "initialize OPM2 runtime", failures);
    return initialized;
}

std::string make_temp_dir() {
    char templ[] = "/tmp/taiyin-heliacal-XXXXXX";
    char* path = mkdtemp(templ);
    return path ? std::string(path) : std::string();
}

bool install_test_star(int* failures) {
    const std::string root = make_temp_dir();
    expect_true(!root.empty(), "make heliacal temp dir", failures);
    if (root.empty()) return false;

    taiyin::internal::Tsf1StarEntry entry;
    entry.id = "heliacal_test_star";
    entry.name = "Heliacal Test Star";
    entry.magnitude = 0.0;
    entry.ra_deg = 0.0;
    entry.dec_deg = 0.0;
    entry.pm_ra_mas_yr = 0.0;
    entry.pm_dec_mas_yr = 0.0;
    entry.parallax_mas = 0.0;
    entry.radial_velocity_km_s = 0.0;
    entry.reference_epoch = 2000.0;
    const std::string path = root + "/stars.tsf1";
    expect_true(
        taiyin::internal::save_star_file(path, &entry, 1),
        "write heliacal star catalog",
        failures);
    const taiyin::Status status = taiyin::runtime::add_global_tsf1_star_catalog(path.c_str());
    expect_status(status, taiyin::TAIYIN_STATUS_OK, "load heliacal star catalog", failures);
    return status == taiyin::TAIYIN_STATUS_OK;
}

taiyin::runtime::NativeCalcContext make_context() {
    taiyin::runtime::NativeCalcContext context;
    taiyin::runtime::native_context_set_geocentric_observer(
        &context, taiyin::TAIYIN_BODY_EARTH, taiyin::TAIYIN_BODY_EARTH);
    taiyin::runtime::native_context_set_observer_location(
        &context, taiyin::runtime::native_observer_location_degrees(0.0, 0.0, 0.0));
    taiyin::runtime::native_context_set_atmosphere_policy_flags(
        &context, taiyin::runtime::TAIYIN_NATIVE_ATMOSPHERE_ALLOW_STANDARD_FALLBACK);
    taiyin::runtime::native_context_use_solar_deflector(&context);
    context.apparent_options.flags = taiyin::TAIYIN_APPARENT_SPHERICAL
        | taiyin::TAIYIN_APPARENT_LIGHT_TIME
        | taiyin::TAIYIN_APPARENT_ABERRATION
        | taiyin::TAIYIN_APPARENT_DEFLECTION;
    return context;
}

void test_profile_registry(int* failures) {
    using namespace taiyin;
    using namespace taiyin::dispatch;
    using namespace taiyin::runtime;

    const NativeCalcContext default_context;
    expect_true(
        default_context.heliacal_visibility_model_id == HELIACAL_VISIBILITY_SCHAEFER_1993,
        "Schaefer 1993 is the default heliacal profile",
        failures);

    HeliacalVisibilityModelEntry builtin;
    expect_true(
        find_heliacal_visibility_model(HELIACAL_VISIBILITY_BELOKRYLOV_2011, &builtin),
        "find Belokrylov 2011 profile",
        failures);
    expect_true(builtin.eval != nullptr, "Belokrylov profile has evaluator", failures);
    expect_true(
        builtin.default_extinction_mag_per_airmass == 0.25,
        "Belokrylov default extinction",
        failures);

    HeliacalVisibilityModelInput input;
    input.target_magnitude = 0.0;
    input.target_altitude_rad = 45.0 * TAIYIN_DEG_TO_RAD;
    input.target_azimuth_rad = 0.0;
    input.sun_altitude_rad = -3.0 * TAIYIN_DEG_TO_RAD;
    input.sun_azimuth_rad = 0.5 * TAIYIN_PI;
    input.target_sun_separation_rad = 90.0 * TAIYIN_DEG_TO_RAD;
    input.extinction_mag_per_airmass = 0.25;
    HeliacalVisibilityResult output;
    expect_true(
        eval_heliacal_visibility(HELIACAL_VISIBILITY_BELOKRYLOV_2011, &input, &output),
        "evaluate Belokrylov profile",
        failures);
    expect_true(output.visible != 0, "bright target visible after sunset", failures);
    expect_true(output.airmass > 1.0, "Belokrylov airmass finite", failures);
    expect_true(output.extinction_mag > 0.0, "Belokrylov extinction positive", failures);
    expect_true(
        output.visibility_margin_magnitude > 0.0,
        "Belokrylov visibility magnitude margin",
        failures);

    input.target_sun_separation_rad = 20.0 * TAIYIN_DEG_TO_RAD;
    HeliacalVisibilityResult nearby;
    expect_true(
        eval_heliacal_visibility(HELIACAL_VISIBILITY_BELOKRYLOV_2011, &input, &nearby),
        "evaluate near-Sun target",
        failures);
    expect_true(
        nearby.required_sun_altitude_rad < output.required_sun_altitude_rad,
        "near-Sun target needs deeper twilight",
        failures);

    HeliacalVisibilityModelEntry schaefer;
    expect_true(
        find_heliacal_visibility_model(HELIACAL_VISIBILITY_SCHAEFER_1993, &schaefer),
        "find Schaefer 1993 profile",
        failures);
    expect_true(
        schaefer.visual_threshold_model_id == HELIACAL_VISUAL_THRESHOLD_HECHT_1947,
        "Schaefer profile records Hecht threshold",
        failures);
    expect_true(
        schaefer.moonlight_model_id == HELIACAL_MOONLIGHT_KRISCIUNAS_SCHAEFER_1991,
        "Schaefer profile records Krisciunas-Schaefer moonlight",
        failures);
    input.target_sun_separation_rad = 90.0 * TAIYIN_DEG_TO_RAD;
    input.sky_brightness_nanolambert = 100.0;
    HeliacalVisibilityResult dark_sky;
    expect_true(
        eval_heliacal_visibility(HELIACAL_VISIBILITY_SCHAEFER_1993, &input, &dark_sky),
        "evaluate Schaefer 1993 profile under dark sky",
        failures);
    expect_true(dark_sky.visible != 0, "bright target visible under dark sky", failures);
    input.sky_brightness_nanolambert = 1.0e8;
    HeliacalVisibilityResult bright_sky;
    expect_true(
        eval_heliacal_visibility(HELIACAL_VISIBILITY_SCHAEFER_1993, &input, &bright_sky),
        "evaluate Schaefer 1993 profile under bright sky",
        failures);
    expect_true(bright_sky.visible == 0, "bright background hides target", failures);
    expect_true(
        bright_sky.limiting_magnitude < dark_sky.limiting_magnitude,
        "bright background lowers limiting magnitude",
        failures);
    input.sky_brightness_nanolambert = std::numeric_limits<double>::quiet_NaN();
    input.sun_altitude_rad = -12.0 * TAIYIN_DEG_TO_RAD;
    HeliacalVisibilityResult deep_twilight;
    expect_true(
        eval_heliacal_visibility(HELIACAL_VISIBILITY_SCHAEFER_1993, &input, &deep_twilight),
        "evaluate Schaefer 1993 deep twilight background",
        failures);
    input.sun_altitude_rad = -3.0 * TAIYIN_DEG_TO_RAD;
    HeliacalVisibilityResult shallow_twilight;
    expect_true(
        eval_heliacal_visibility(HELIACAL_VISIBILITY_SCHAEFER_1993, &input, &shallow_twilight),
        "evaluate Schaefer 1993 shallow twilight background",
        failures);
    expect_true(
        shallow_twilight.sky_brightness_nanolambert > deep_twilight.sky_brightness_nanolambert,
        "shallow twilight has a brighter calculated background",
        failures);
    input.include_moonlight = 0;
    input.moon_altitude_rad = 50.0 * TAIYIN_DEG_TO_RAD;
    input.moon_azimuth_rad = TAIYIN_PI;
    input.moon_phase_angle_rad = 10.0 * TAIYIN_DEG_TO_RAD;
    HeliacalVisibilityResult moonless;
    expect_true(
        eval_heliacal_visibility(HELIACAL_VISIBILITY_SCHAEFER_1993, &input, &moonless),
        "evaluate moonless Schaefer profile",
        failures);
    input.include_moonlight = 1;
    HeliacalVisibilityResult moonlit;
    expect_true(
        eval_heliacal_visibility(HELIACAL_VISIBILITY_SCHAEFER_1993, &input, &moonlit),
        "evaluate Krisciunas-Schaefer moonlight",
        failures);
    expect_true(
        moonlit.moonlight_brightness_nanolambert > 0.0,
        "moonlight component is positive above the horizon",
        failures);
    expect_true(
        moonlit.sky_brightness_nanolambert > moonless.sky_brightness_nanolambert,
        "moonlight raises sky background",
        failures);
    expect_true(
        moonlit.limiting_magnitude < moonless.limiting_magnitude,
        "moonlight lowers limiting magnitude",
        failures);
    input.target_altitude_rad = 0.5 * TAIYIN_PI;
    input.target_azimuth_rad = 0.0;
    input.moon_altitude_rad = 30.0 * TAIYIN_DEG_TO_RAD;
    input.moon_azimuth_rad = 0.0;
    input.moon_phase_angle_rad = 30.0 * TAIYIN_DEG_TO_RAD;
    input.extinction_mag_per_airmass = 0.172;
    HeliacalVisibilityResult paper_table_2;
    expect_true(
        eval_heliacal_visibility(HELIACAL_VISIBILITY_SCHAEFER_1993, &input, &paper_table_2),
        "evaluate Krisciunas-Schaefer table 2 case",
        failures);
    expect_near(
        paper_table_2.moonlight_brightness_nanolambert,
        530.0,
        1.0,
        "Krisciunas-Schaefer 1991 table 2 moonlight",
        failures);

    const HeliacalVisibilityModelEntry custom(
        HELIACAL_VISIBILITY_CUSTOM_START,
        custom_profile,
        HELIACAL_EXTINCTION_CUSTOM_START,
        HELIACAL_TWILIGHT_CUSTOM_START,
        HELIACAL_VISUAL_THRESHOLD_CUSTOM_START,
        0.3);
    expect_true(add_heliacal_visibility_model(custom), "register custom heliacal profile", failures);
    expect_true(
        eval_heliacal_visibility(HELIACAL_VISIBILITY_CUSTOM_START, &input, &output)
            && output.visible != 0,
        "evaluate custom heliacal profile",
        failures);
}

void test_public_calculators(int* failures) {
    using namespace taiyin;
    using namespace taiyin::runtime;

    NativeCalcContext context = make_context();
    expect_status(
        native_context_set_heliacal_visibility_model(
            &context, dispatch::HELIACAL_VISIBILITY_BELOKRYLOV_2011),
        TAIYIN_STATUS_OK,
        "select built-in heliacal profile",
        failures);
    expect_status(
        native_context_set_heliacal_visibility_model(&context, dispatch::HELIACAL_VISIBILITY_SCHAEFER_1993),
        TAIYIN_STATUS_OK,
        "select Schaefer 1993 heliacal profile",
        failures);

    const SplitJulianDate jd_ut = split_jd(julian_day({2024, 4, 8, 0, 0, 0.0}));
    HeliacalVisibilityResult star;
    expect_status(
        calc_star_heliacal_visibility_ut(
            &context, "heliacal_test_star", jd_ut, 0u, nullptr, &star, nullptr),
        TAIYIN_STATUS_OK,
        "calculate star heliacal visibility",
        failures);
    expect_true(std::isfinite(star.target_magnitude), "star magnitude resolved", failures);
    expect_true(std::isfinite(star.limiting_magnitude), "star limiting magnitude finite", failures);
    expect_true(star.model_id == dispatch::HELIACAL_VISIBILITY_SCHAEFER_1993, "star profile id", failures);
    expect_true(
        std::isfinite(star.sky_brightness_nanolambert) && star.sky_brightness_nanolambert > 0.0,
        "Schaefer sky brightness finite",
        failures);

    HeliacalVisibilityConditions conditions;
    conditions.extinction_mag_per_airmass = 0.5;
    conditions.sky_brightness_nanolambert = 1234.0;
    HeliacalVisibilityResult venus;
    expect_status(
        calc_body_heliacal_visibility_ut(
            &context, TAIYIN_BODY_VENUS, jd_ut,
            TAIYIN_HELIACAL_VISIBILITY_INCLUDE_MOONLIGHT, &conditions, &venus, nullptr),
        TAIYIN_STATUS_OK,
        "calculate Venus heliacal visibility",
        failures);
    expect_true(
        venus.extinction_mag_per_airmass == conditions.extinction_mag_per_airmass,
        "explicit extinction used",
        failures);
    expect_true(
        venus.sky_brightness_nanolambert == conditions.sky_brightness_nanolambert,
        "explicit sky background used",
        failures);
    expect_true(
        venus.moonlight_brightness_nanolambert == 0.0,
        "measured sky background supersedes modeled moonlight",
        failures);
    HeliacalVisibilityConditions strict_conditions;
    expect_status(
        calc_body_heliacal_visibility_ut(
            &context, TAIYIN_BODY_VENUS, jd_ut,
            TAIYIN_HELIACAL_VISIBILITY_STRICT_METEOROLOGY,
            &strict_conditions, &venus, nullptr),
        TAIYIN_ERROR_INVALID_ARGUMENT,
        "strict meteorology rejects missing atmosphere fields",
        failures);
    NativeCalcContext strict_context = make_context();
    NativeAtmosphere atmosphere = native_standard_atmosphere();
    atmosphere.relative_humidity = 40.0;
    expect_status(
        native_context_set_atmosphere(&strict_context, atmosphere),
        TAIYIN_STATUS_OK,
        "install strict heliacal atmosphere",
        failures);
    expect_status(
        native_context_set_meteorological_range_km(&strict_context, 40.0),
        TAIYIN_STATUS_OK,
        "install strict heliacal meteorological range",
        failures);
    expect_status(
        calc_body_heliacal_visibility_ut(
            &strict_context, TAIYIN_BODY_VENUS, jd_ut,
            TAIYIN_HELIACAL_VISIBILITY_STRICT_METEOROLOGY,
            &strict_conditions, &venus, nullptr),
        TAIYIN_STATUS_OK,
        "strict meteorology accepts explicit atmosphere and range",
        failures);
    expect_true(
        std::isfinite(venus.extinction_mag_per_airmass)
            && venus.extinction_mag_per_airmass > 0.0,
        "Schaefer 2000 extinction is finite",
        failures);
    expect_status(
        native_context_set_meteorological_range_km(&strict_context, 1.0e9),
        TAIYIN_STATUS_OK,
        "install extremely clear meteorological range",
        failures);
    expect_status(
        calc_body_heliacal_visibility_ut(
            &strict_context, TAIYIN_BODY_VENUS, jd_ut,
            TAIYIN_HELIACAL_VISIBILITY_STRICT_METEOROLOGY,
            &strict_conditions, &venus, nullptr),
        TAIYIN_STATUS_OK,
        "extremely clear atmosphere keeps Schaefer extinction physical",
        failures);
    expect_true(
        std::isfinite(venus.extinction_mag_per_airmass)
            && venus.extinction_mag_per_airmass > 0.0,
        "extremely clear Schaefer extinction is finite",
        failures);
    atmosphere.relative_humidity = 0.0;
    expect_status(
        native_context_set_atmosphere(&strict_context, atmosphere),
        TAIYIN_STATUS_OK,
        "install dry Schaefer atmosphere",
        failures);
    expect_status(
        native_context_set_meteorological_range_km(&strict_context, 40.0),
        TAIYIN_STATUS_OK,
        "install dry-atmosphere meteorological range",
        failures);
    expect_status(
        calc_body_heliacal_visibility_ut(
            &strict_context, TAIYIN_BODY_VENUS, jd_ut,
            TAIYIN_HELIACAL_VISIBILITY_STRICT_METEOROLOGY,
            &strict_conditions, &venus, nullptr),
        TAIYIN_STATUS_OK,
        "dry atmosphere with meteorological range is valid",
        failures);
    expect_status(
        calc_body_heliacal_visibility_ut(
            &context, TAIYIN_BODY_VENUS, jd_ut, 1ull << 63,
            nullptr, &venus, nullptr),
        TAIYIN_ERROR_INVALID_ARGUMENT,
        "reject unknown heliacal option flag",
        failures);
    expect_status(
        calc_body_heliacal_visibility_ut(
            &context, TAIYIN_BODY_VENUS, jd_ut, TAIYIN_NATIVE_POSITION_XYZ,
            nullptr, &venus, nullptr),
        TAIYIN_ERROR_INVALID_ARGUMENT,
        "reject unsupported heliacal output flag",
        failures);
    expect_status(
        calc_body_heliacal_visibility_ut(
            &context, TAIYIN_BODY_SUN, jd_ut, 0u, nullptr, &venus, nullptr),
        TAIYIN_ERROR_INVALID_ARGUMENT,
        "reject Sun as heliacal target",
        failures);
    expect_status(
        calc_body_heliacal_visibility_ut(
            &context, TAIYIN_BODY_MOON, jd_ut, 0u, nullptr, &venus, nullptr),
        TAIYIN_ERROR_INVALID_ARGUMENT,
        "reject Moon from point-source heliacal model",
        failures);
    expect_status(
        native_context_set_heliacal_visibility_model(
            &context, dispatch::HELIACAL_VISIBILITY_BELOKRYLOV_2011),
        TAIYIN_STATUS_OK,
        "restore Belokrylov heliacal profile",
        failures);
    HeliacalVisibilityConditions moonlight_conditions;
    expect_status(
        calc_body_heliacal_visibility_ut(
            &context, TAIYIN_BODY_VENUS, jd_ut,
            TAIYIN_HELIACAL_VISIBILITY_INCLUDE_MOONLIGHT,
            &moonlight_conditions, &venus, nullptr),
        TAIYIN_ERROR_UNSUPPORTED,
        "reject moonlight request for profile without a moonlight component",
        failures);
}

void test_event_search(int* failures) {
    using namespace taiyin;
    using namespace taiyin::runtime;

    NativeCalcContext context = make_context();
    expect_status(
        native_context_set_heliacal_visibility_model(
            &context, dispatch::HELIACAL_VISIBILITY_BELOKRYLOV_2011),
        TAIYIN_STATUS_OK,
        "select Belokrylov profile for heliacal event search",
        failures);
    const SplitJulianDate start_jd_ut = split_jd(julian_day({2024, 1, 1, 0, 0, 0.0}));
    HeliacalVisibilitySearchResult result;
    HeliacalVisibilitySearchResult imprecise_date_result;
    expect_status(
        search_next_star_heliacal_visibility_ut(
            &context,
            "heliacal_test_star",
            start_jd_ut,
            TAIYIN_HELIACAL_VISIBILITY_EVENT_MORNING_FIRST,
            180.0,
            0u,
            nullptr,
            &result,
            nullptr),
        TAIYIN_STATUS_OK,
        "find next morning-first heliacal event",
        failures);
    expect_true(
        result.event_kind == TAIYIN_HELIACAL_VISIBILITY_EVENT_MORNING_FIRST,
        "heliacal event kind",
        failures);
    expect_true(
        result.jd_ut >= start_jd_ut && result.jd_ut <= start_jd_ut + 180.0,
        "heliacal event inside requested search range",
        failures);
    expect_true(
        result.window_end_jd_ut > result.window_start_jd_ut,
        "heliacal event has a twilight window",
        failures);
    expect_true(result.visibility.visible != 0, "heliacal event is visible", failures);
    expect_true(result.sampled_window_count > 0, "heliacal event sampled windows", failures);
    expect_true(result.visibility_evaluation_count > 0, "heliacal event evaluated visibility", failures);

    expect_status(
        search_next_star_heliacal_visibility_ut(
            &context,
            "heliacal_test_star",
            split_jd(1.0e20),
            TAIYIN_HELIACAL_VISIBILITY_EVENT_MORNING_FIRST,
            2.0,
            0u,
            nullptr,
            &imprecise_date_result,
            nullptr),
        TAIYIN_ERROR_INVALID_ARGUMENT,
        "heliacal search rejects dates without daily precision",
        failures);

    HeliacalVisibilitySearchResult bounded_result;
    expect_status(
        search_next_star_heliacal_visibility_ut(
            &context,
            "heliacal_test_star",
            result.jd_ut - 0.25,
            TAIYIN_HELIACAL_VISIBILITY_EVENT_MORNING_FIRST,
            0.01,
            0u,
            nullptr,
            &bounded_result,
            nullptr),
        TAIYIN_EVENT_ERROR_NOT_FOUND,
        "heliacal search respects the requested upper time bound",
        failures);

    NativeCalcContext no_observer;
    native_context_set_geocentric_observer(
        &no_observer, TAIYIN_BODY_EARTH, TAIYIN_BODY_EARTH);
    expect_status(
        search_next_star_heliacal_visibility_ut(
            &no_observer,
            "heliacal_test_star",
            start_jd_ut,
            TAIYIN_HELIACAL_VISIBILITY_EVENT_MORNING_FIRST,
            180.0,
            0u,
            nullptr,
            &result,
            nullptr),
        TAIYIN_ERROR_INVALID_ARGUMENT,
        "reject heliacal search without an observer location",
        failures);

    NativeCalcContext polar_context = make_context();
    native_context_set_observer_location(
        &polar_context,
        native_observer_location_degrees(0.0, 80.0, 0.0));
    expect_status(
        search_next_star_heliacal_visibility_ut(
            &polar_context,
            "heliacal_test_star",
            split_jd(julian_day({2024, 6, 20, 0, 0, 0.0})),
            TAIYIN_HELIACAL_VISIBILITY_EVENT_MORNING_FIRST,
            3.0,
            0u,
            nullptr,
            &result,
            nullptr),
        TAIYIN_EVENT_ERROR_NOT_FOUND,
        "heliacal search treats polar no-twilight days as not found",
        failures);
    expect_status(
        search_next_body_heliacal_visibility_ut(
            &context,
            TAIYIN_BODY_MOON,
            start_jd_ut,
            TAIYIN_HELIACAL_VISIBILITY_EVENT_MORNING_FIRST,
            180.0,
            0u,
            nullptr,
            &result,
            nullptr),
        TAIYIN_ERROR_INVALID_ARGUMENT,
        "reject Moon from heliacal event search",
        failures);

    HeliacalVisibilityConditions swiss_like_conditions;
    swiss_like_conditions.extinction_mag_per_airmass = 0.25;
    NativeCalcContext swiss_context = make_context();
    expect_true(
        swiss_context.heliacal_visibility_model_id == dispatch::HELIACAL_VISIBILITY_SCHAEFER_1993,
        "Swiss heliacal oracles use the default Schaefer profile",
        failures);
    struct SwissVenusHeliacalCase {
        int event_kind;
        double optimum_jd_ut;
        const char* label;
    };
    // Fixed from Swiss Ephemeris SWIEPH se1 via swe_heliacal_ut() with
    // geopos=(0, 0, 0), datm=(1013.25, 15, 40, 0.25), a naked-eye observer,
    // and HELFLAG_VISLIM_NOMOON. These are Swiss dret[1] optimum times.
    const SwissVenusHeliacalCase swiss_venus_cases[] = {
        { TAIYIN_HELIACAL_VISIBILITY_EVENT_MORNING_FIRST, 2460760.739317970350,
          "Schaefer Venus morning-first vs Swiss optimum" },
        { TAIYIN_HELIACAL_VISIBILITY_EVENT_MORNING_LAST, 2460430.731063851155,
          "Schaefer Venus morning-last vs Swiss optimum" },
        { TAIYIN_HELIACAL_VISIBILITY_EVENT_EVENING_FIRST, 2460497.270654550754,
          "Schaefer Venus evening-first vs Swiss optimum" },
        { TAIYIN_HELIACAL_VISIBILITY_EVENT_EVENING_LAST, 2460749.272280503064,
          "Schaefer Venus evening-last vs Swiss optimum" },
    };
    constexpr double kSwissHeliacalToleranceDays = 10.0 / 1440.0;
    for (size_t index = 0;
        index < sizeof(swiss_venus_cases) / sizeof(swiss_venus_cases[0]);
        ++index) {
        HeliacalVisibilitySearchResult venus;
        expect_status(
            search_next_body_heliacal_visibility_ut(
                &swiss_context,
                TAIYIN_BODY_VENUS,
                split_jd(swiss_venus_cases[index].optimum_jd_ut - 2.0),
                swiss_venus_cases[index].event_kind,
                5.0,
                0u,
                &swiss_like_conditions,
                &venus,
                nullptr),
            TAIYIN_STATUS_OK,
            swiss_venus_cases[index].label,
            failures);
        expect_near(
            venus.jd_ut,
            swiss_venus_cases[index].optimum_jd_ut,
            kSwissHeliacalToleranceDays,
            swiss_venus_cases[index].label,
            failures);
    }
}

}  // namespace

int main() {
    int failures = 0;
    test_profile_registry(&failures);
    if (initialize_runtime(&failures) && install_test_star(&failures)) {
        test_public_calculators(&failures);
        test_event_search(&failures);
    }
    taiyin::runtime::clear_global_star_catalogs();
    if (failures == 0) {
        std::cout << "test_heliacal_visibility: ALL TESTS PASSED\n";
        return 0;
    }
    std::cerr << failures << " heliacal visibility test(s) failed\n";
    return 1;
}
