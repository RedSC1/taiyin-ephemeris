// Regression tests for eclipse search.
#include "taiyin/apparent_position.h"
#include "taiyin/body_id.h"
#include "taiyin/internal/builtin_loader.h"
#include "taiyin/internal/eop.h"
#include "taiyin/runtime/eclipse_search.h"
#include "taiyin/runtime/native_context.h"
#include "taiyin/runtime/runtime.h"
#include "taiyin/status.h"
#include "taiyin/time.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace {

std::string repo_data_root() {
    const char* root = std::getenv("TAIYIN_REPO_ROOT");
    if (root && root[0] != '\0') {
        return std::string(root) + "/data/ephemerides/opm2/major-bodies/600y";
    }
    return "../data/ephemerides/opm2/major-bodies/600y";
}

double jd(int year, int month, int day) {
    return taiyin::julian_day({year, month, day, 0, 0, 0.0});
}

double tt(int year, int month, int day, int hour, int minute, double second = 0.0) {
    return taiyin::julian_day({year, month, day, hour, minute, second});
}

taiyin::SplitJulianDate split_jd(double value) {
    taiyin::SplitJulianDate result;
    taiyin::split_julian_date_from_double(value, &result);
    return result;
}

double scalar_jd(const taiyin::SplitJulianDate& value) {
    return taiyin::split_julian_date_to_double(value);
}

taiyin::runtime::NativeCalcContext make_context() {
    taiyin::runtime::NativeCalcContext context;
    taiyin::runtime::native_context_set_geocentric_observer(
        &context,
        taiyin::TAIYIN_BODY_EARTH,
        taiyin::TAIYIN_BODY_EARTH);
    taiyin::runtime::native_context_set_atmosphere(
        &context,
        taiyin::runtime::native_standard_atmosphere());
    taiyin::runtime::native_context_use_solar_deflector(&context);
    context.apparent_options.flags =
        taiyin::TAIYIN_APPARENT_SPHERICAL
        | taiyin::TAIYIN_APPARENT_LIGHT_TIME
        | taiyin::TAIYIN_APPARENT_ABERRATION
        | taiyin::TAIYIN_APPARENT_DEFLECTION;
    context.apparent_options.output_frame_id =
        taiyin::TAIYIN_APPARENT_FRAME_TRUE_ECLIPTIC_OF_DATE;
    context.eclipse_shadow_model_id =
        static_cast<uint8_t>(taiyin::dispatch::ECLIPSE_SHADOW_CHAUVENET);
    return context;
}

taiyin::runtime::NativeCalcContext make_context_with_observer(
    double longitude_deg,
    double latitude_deg,
    double height_m
) {
    taiyin::runtime::NativeCalcContext context = make_context();
    taiyin::runtime::native_context_set_observer_location(
        &context,
        taiyin::runtime::native_observer_location_degrees(longitude_deg, latitude_deg, height_m));
    return context;
}

int fail(const char* message) {
    std::printf("FAIL: %s\n", message);
    return 1;
}

int expect_status(taiyin::Status actual, const char* label) {
    if (actual != taiyin::TAIYIN_STATUS_OK) {
        std::printf("FAIL: %s returned status %d\n", label, actual);
        return 1;
    }
    return 0;
}

int expect_kind(uint32_t actual, uint32_t expected, const char* label) {
    if (actual != expected) {
        std::printf("FAIL: %s kind=%u expected=%u\n", label, actual, expected);
        return 1;
    }
    return 0;
}

int expect_kind_has(uint32_t actual, uint32_t expected_bits, const char* label) {
    if ((actual & expected_bits) != expected_bits) {
        std::printf("FAIL: %s kind=%u expected bits=%u\n", label, actual, expected_bits);
        return 1;
    }
    return 0;
}

int expect_bits_clear(uint32_t actual, uint32_t unexpected_bits, const char* label) {
    if ((actual & unexpected_bits) != 0u) {
        std::printf("FAIL: %s flags=%u unexpected bits=%u\n", label, actual, unexpected_bits);
        return 1;
    }
    return 0;
}

int expect_close_days(double actual, double expected, double tolerance_days, const char* label) {
    if (!std::isfinite(actual) || std::fabs(actual - expected) > tolerance_days) {
        std::printf(
            "FAIL: %s actual=%.9f expected=%.9f diff=%.3f seconds tolerance=%.3f seconds\n",
            label,
            actual,
            expected,
            (actual - expected) * 86400.0,
            tolerance_days * 86400.0);
        return 1;
    }
    return 0;
}

int expect_close_days(
    const taiyin::SplitJulianDate& actual,
    double expected,
    double tolerance_days,
    const char* label
) {
    return expect_close_days(scalar_jd(actual), expected, tolerance_days, label);
}

int expect_close_days(
    const taiyin::SplitJulianDate& actual,
    const taiyin::SplitJulianDate& expected,
    double tolerance_days,
    const char* label
) {
    return expect_close_days(scalar_jd(actual), scalar_jd(expected), tolerance_days, label);
}

int expect_close_value(double actual, double expected, double tolerance, const char* label) {
    if (!std::isfinite(actual) || std::fabs(actual - expected) > tolerance) {
        std::printf(
            "FAIL: %s actual=%.15g expected=%.15g tolerance=%.15g\n",
            label,
            actual,
            expected,
            tolerance);
        return 1;
    }
    return 0;
}

double deg_to_rad(double degrees) {
    return degrees * 3.141592653589793238462643383279502884 / 180.0;
}

int expect_close_geodetic_point(
    double actual_lat_deg,
    double actual_lon_deg,
    double expected_lat_deg,
    double expected_lon_deg,
    double tolerance_deg,
    const char* label
) {
    if (!std::isfinite(actual_lat_deg) || !std::isfinite(actual_lon_deg)
        || !std::isfinite(expected_lat_deg) || !std::isfinite(expected_lon_deg)) {
        std::printf("FAIL: %s contains non-finite coordinate\n", label);
        return 1;
    }
    const double lat0 = deg_to_rad(actual_lat_deg);
    const double lat1 = deg_to_rad(expected_lat_deg);
    const double dlat = lat0 - lat1;
    const double dlon = deg_to_rad(actual_lon_deg - expected_lon_deg);
    const double a = std::sin(0.5 * dlat) * std::sin(0.5 * dlat)
        + std::cos(lat0) * std::cos(lat1) * std::sin(0.5 * dlon) * std::sin(0.5 * dlon);
    const double distance_deg = 2.0 * std::asin(std::min(1.0, std::sqrt(std::max(0.0, a)))) * 180.0
        / 3.141592653589793238462643383279502884;
    if (!std::isfinite(distance_deg) || distance_deg > tolerance_deg) {
        std::printf(
            "FAIL: %s distance=%.9f deg tolerance=%.9f deg actual=(%.9f, %.9f) expected=(%.9f, %.9f)\n",
            label,
            distance_deg,
            tolerance_deg,
            actual_lat_deg,
            actual_lon_deg,
            expected_lat_deg,
            expected_lon_deg);
        return 1;
    }
    return 0;
}

int expect_nan(double actual, const char* label) {
    if (!std::isnan(actual)) {
        std::printf("FAIL: %s actual=%.9f expected=NAN\n", label, actual);
        return 1;
    }
    return 0;
}

int expect_nan(const taiyin::SplitJulianDate& actual, const char* label) {
    if (taiyin::split_julian_date_is_finite(actual)) {
        std::printf("FAIL: %s actual=%.9f expected=NAN\n", label, scalar_jd(actual));
        return 1;
    }
    return 0;
}

bool is_coverage_boundary_status(taiyin::Status status) {
    return status == taiyin::TAIYIN_EPHEMERIS_ERROR_COMPOSITE_COVERAGE_GAP
        || status == taiyin::TAIYIN_EPHEMERIS_ERROR_COVERAGE_GAP
        || status == taiyin::TAIYIN_EPHEMERIS_ERROR_COMPOSITE_MISSING_COMPONENT
        || status == taiyin::TAIYIN_EPHEMERIS_ERROR_NO_ROUTE;
}

struct CurveSummary {
    size_t count;
    double first_lat;
    double first_lon;
    double last_lat;
    double last_lon;
};

CurveSummary summarize_curve_kind(
    const taiyin::runtime::SolarEclipseRouteCurvePoint* points,
    size_t count,
    uint32_t kind
) {
    CurveSummary summary = {0, std::nan(""), std::nan(""), std::nan(""), std::nan("")};
    for (size_t i = 0; i < count; ++i) {
        if (points[i].curve_kind != kind) continue;
        if (summary.count == 0) {
            summary.first_lat = points[i].latitude_deg;
            summary.first_lon = points[i].longitude_deg;
        }
        summary.last_lat = points[i].latitude_deg;
        summary.last_lon = points[i].longitude_deg;
        ++summary.count;
    }
    return summary;
}

int expect_curve_summary(
    const taiyin::runtime::SolarEclipseRouteCurvePoint* points,
    size_t count,
    uint32_t kind,
    size_t expected_count,
    double expected_first_lat,
    double expected_first_lon,
    double expected_last_lat,
    double expected_last_lon,
    double tolerance_deg,
    const char* label
) {
    const CurveSummary summary = summarize_curve_kind(points, count, kind);
    if (summary.count != expected_count) {
        std::printf("FAIL: %s count=%zu expected=%zu\n", label, summary.count, expected_count);
        return 1;
    }
    if (expect_close_value(summary.first_lat, expected_first_lat, tolerance_deg, label)) return 1;
    if (expect_close_value(summary.first_lon, expected_first_lon, tolerance_deg, label)) return 1;
    if (expect_close_value(summary.last_lat, expected_last_lat, tolerance_deg, label)) return 1;
    if (expect_close_value(summary.last_lon, expected_last_lon, tolerance_deg, label)) return 1;
    return 0;
}

int expect_coverage_boundary_status(taiyin::Status status, const char* label) {
    if (!is_coverage_boundary_status(status) && status != taiyin::TAIYIN_EVENT_ERROR_NOT_FOUND) {
        std::printf("FAIL: %s status=%d expected coverage boundary\n", label, status);
        return 1;
    }
    return 0;
}

struct ContactFixture {
    const char* label;
    size_t index;
    double jd_ut;
};

int expect_contacts_ut(
    const taiyin::runtime::LunarEclipseResultUt& result,
    const ContactFixture* fixtures,
    size_t fixture_count,
    double tolerance_days
) {
    for (size_t i = 0; i < fixture_count; ++i) {
        if (expect_close_days(
                result.contact_jd_ut[fixtures[i].index],
                fixtures[i].jd_ut,
                tolerance_days,
                fixtures[i].label)) {
            return 1;
        }
    }
    return 0;
}

int initialize_runtime() {
    taiyin::runtime::EphemerisRuntimeConfig config;
    const std::string data_root = repo_data_root();
    const char* source_paths[] = { data_root.c_str() };
    config.source_paths = source_paths;
    config.source_path_count = 1;
    config.load_packaged_data = false;
    config.segment_cache_max_entries = 256;

    if (!taiyin::runtime::initialize_global_ephemeris_runtime(config)) {
        return fail("failed to initialize runtime");
    }
    return 0;
}

}  // namespace

int main() {
    using namespace taiyin;
    using namespace taiyin::runtime;

    if (initialize_runtime()) {
        return 1;
    }

    NativeCalcContext ctx = make_context();
    EphemerisEvalDiagnostic diag = {};
    const uint64_t flags = TAIYIN_ECLIPSE_INCLUDE_CONTACTS;

    // UT-facing eclipse APIs honor the context time-scale policy: Auto may
    // fallback to estimated Delta T when EOP is absent, while Precise must not.
    {
        EphemerisEvalDiagnostic auto_diag = {};
        SolarEclipseResultUt result;
        if (expect_status(
                solve_solar_eclipse_at_ut(&ctx, split_jd(2460409.0), 0, &result, &auto_diag),
                "auto solar UT without EOP")) {
            return 1;
        }
        if (auto_diag.time_scale_route != TimeScaleRouteEstimatedDeltaT
            || auto_diag.time_scale_fallback_reason != TimeScaleFallbackNullEopTable
            || (auto_diag.time_scale_flags & TAIYIN_TIME_DIAGNOSTIC_USED_DELTA_T_MODEL) == 0) {
            std::printf(
                "FAIL: auto solar UT diagnostic route=%u fallback=%u flags=%u\n",
                static_cast<unsigned>(auto_diag.time_scale_route),
                static_cast<unsigned>(auto_diag.time_scale_fallback_reason),
                static_cast<unsigned>(auto_diag.time_scale_flags));
            return 1;
        }

        if (!set_global_earth_orientation_table(nullptr)) {
            return fail("failed to clear global EOP table before precise rejection test");
        }
        NativeCalcContext precise_ctx = ctx;
        native_context_set_time_scale_policy(&precise_ctx, TimeScalePrecise);
        EphemerisEvalDiagnostic precise_diag = {};
        const Status status = solve_solar_eclipse_at_ut(
            &precise_ctx, split_jd(2460409.0), 0, &result, &precise_diag);
        if (status != TAIYIN_ERROR_INVALID_ARGUMENT) {
            std::printf("FAIL: precise solar UT without EOP status=%d expected invalid argument\n", status);
            return 1;
        }
        if (precise_diag.time_scale_fallback_reason != TimeScaleFallbackNullEopTable) {
            std::printf("FAIL: precise solar UT without EOP fallback=%u\n", static_cast<unsigned>(precise_diag.time_scale_fallback_reason));
            return 1;
        }
    }
    {
        taiyin::internal::EarthOrientationTable eop = {};
        if (!taiyin::internal::load_builtin_eop_table(&eop)) {
            return fail("failed to load builtin EOP table");
        }
        NativeCalcContext precise_ctx = ctx;
        if (!set_global_earth_orientation_table(&eop)) {
            taiyin::internal::destroy_earth_orientation_table(&eop);
            return fail("failed to install global EOP table");
        }
        native_context_set_time_scale_policy(&precise_ctx, TimeScalePrecise);
        SolarEclipseResultUt result;
        EphemerisEvalDiagnostic precise_diag = {};
        const Status status = solve_solar_eclipse_at_ut(
            &precise_ctx, split_jd(2460409.0), 0, &result, &precise_diag);
        taiyin::internal::destroy_earth_orientation_table(&eop);
        if (status != TAIYIN_STATUS_OK) {
            std::printf("FAIL: precise solar UT with EOP status=%d\n", status);
            return 1;
        }
        if (precise_diag.time_scale_route != TimeScaleRoutePreciseUtcEop
            || precise_diag.time_scale_fallback_reason != TimeScaleFallbackNone
            || (precise_diag.time_scale_flags & TAIYIN_TIME_DIAGNOSTIC_USED_EOP) == 0
            || (precise_diag.time_scale_flags & TAIYIN_TIME_DIAGNOSTIC_USED_LEAP_SECONDS) == 0) {
            std::printf(
                "FAIL: precise solar UT with EOP diagnostic route=%u fallback=%u flags=%u\n",
                static_cast<unsigned>(precise_diag.time_scale_route),
                static_cast<unsigned>(precise_diag.time_scale_fallback_reason),
                static_cast<unsigned>(precise_diag.time_scale_flags));
            return 1;
        }
        if (!split_julian_date_is_finite(result.maximum_jd_ut)
            || !std::isfinite(result.delta_t_seconds)) {
            return fail("precise solar UT with EOP should produce finite time scales");
        }
        if (!set_global_earth_orientation_table(nullptr)) {
            return fail("failed to clear global EOP table after precise eclipse test");
        }
    }

    // Non-eclipse solves should succeed with TAIYIN_ECLIPSE_NONE and no contacts.
    {
        LunarEclipseResult lunar;
        if (expect_status(
                solve_lunar_eclipse_at(&ctx, split_jd(2451594.0), flags, &lunar, &diag),
                "solve_lunar_eclipse_at none")) {
            return 1;
        }
        if (expect_kind(lunar.kind, TAIYIN_ECLIPSE_NONE, "lunar none")) return 1;
        if (expect_nan(lunar.contact_jd_tt[TAIYIN_LUNAR_ECLIPSE_CONTACT_P1], "lunar none P1")) return 1;

        SolarEclipseResult solar;
        if (expect_status(
                solve_solar_eclipse_at(&ctx, split_jd(2451550.0), flags, &solar, &diag),
                "solve_solar_eclipse_at none")) {
            return 1;
        }
        if (expect_kind(solar.kind, TAIYIN_ECLIPSE_NONE, "solar none")) return 1;
        if (expect_nan(solar.contact_jd_tt[TAIYIN_SOLAR_ECLIPSE_CONTACT_P1], "solar none P1")) return 1;
    }

    // Local search initializes output before early error returns.
    {
        LocalSolarEclipseResult local_tt;
        local_tt.kind = TAIYIN_ECLIPSE_TOTAL;
        local_tt.maximum_jd_tt = split_jd(1.0);
        local_tt.contact_jd_tt[TAIYIN_LOCAL_SOLAR_CONTACT_C1] = split_jd(1.0);
        Status status = search_next_local_solar_eclipse_tt(
            &ctx, split_jd(std::nan("")), 0, 0, &local_tt, &diag);
        if (status != TAIYIN_ERROR_INVALID_ARGUMENT) return fail("local TT invalid search should fail");
        if (expect_kind(local_tt.kind, TAIYIN_ECLIPSE_NONE, "local TT invalid initialized kind")) return 1;
        if (expect_nan(local_tt.maximum_jd_tt, "local TT invalid maximum")) return 1;
        if (expect_nan(local_tt.contact_jd_tt[TAIYIN_LOCAL_SOLAR_CONTACT_C1], "local TT invalid C1")) return 1;

        LocalSolarEclipseResultUt local_ut;
        local_ut.kind = TAIYIN_ECLIPSE_TOTAL;
        local_ut.maximum_jd_ut = split_jd(1.0);
        local_ut.contact_jd_ut[TAIYIN_LOCAL_SOLAR_CONTACT_C1] = split_jd(1.0);
        status = search_next_local_solar_eclipse_ut(
            &ctx, split_jd(std::nan("")), 0, 0, &local_ut, &diag);
        if (status != TAIYIN_ERROR_INVALID_ARGUMENT) return fail("local UT invalid search should fail");
        if (expect_kind(local_ut.kind, TAIYIN_ECLIPSE_NONE, "local UT invalid initialized kind")) return 1;
        if (expect_nan(local_ut.maximum_jd_ut, "local UT invalid maximum")) return 1;
        if (expect_nan(local_ut.contact_jd_ut[TAIYIN_LOCAL_SOLAR_CONTACT_C1], "local UT invalid C1")) return 1;

        status = search_next_local_solar_eclipse_ut(
            &ctx, split_jd(2460400.0), 0, 0, &local_ut, &diag);
        if (status != TAIYIN_ERROR_INVALID_ARGUMENT) {
            return fail("local solar search should require observer location");
        }

        LocalLunarEclipseResultUt local_lunar;
        status = search_next_local_lunar_eclipse_ut(
            &ctx, split_jd(jd(2025, 9, 7)), TAIYIN_ECLIPSE_TOTAL, 0, &local_lunar, &diag);
        if (status != TAIYIN_ERROR_INVALID_ARGUMENT) {
            return fail("local lunar search should require observer location");
        }

        taiyin::runtime::NativeCalcContext invalid_latitude_ctx =
            make_context_with_observer(0.0, 95.0, 0.0);
        taiyin::runtime::NativeCalcContext invalid_setter_ctx = make_context();
        status = taiyin::runtime::native_context_set_observer_location(
            &invalid_setter_ctx,
            taiyin::runtime::native_observer_location_degrees(0.0, 95.0, 0.0));
        if (status != TAIYIN_ERROR_INVALID_ARGUMENT) {
            return fail("native_context_set_observer_location should reject invalid latitude");
        }
        status = search_next_local_solar_eclipse_ut(
            &invalid_latitude_ctx, split_jd(2460400.0), 0, 0, &local_ut, &diag);
        if (status != TAIYIN_ERROR_INVALID_ARGUMENT) {
            return fail("local solar search should reject invalid observer latitude");
        }
        status = search_next_local_lunar_eclipse_ut(
            &invalid_latitude_ctx,
            split_jd(jd(2025, 9, 7)),
            TAIYIN_ECLIPSE_TOTAL,
            0,
            &local_lunar,
            &diag);
        if (status != TAIYIN_ERROR_INVALID_ARGUMENT) {
            return fail("local lunar search should reject invalid observer latitude");
        }

        taiyin::runtime::NativeCalcContext local_flag_ctx =
            make_context_with_observer(116.4074, 39.9042, 43.0);
        status = search_next_local_lunar_eclipse_ut(
            &local_flag_ctx,
            split_jd(jd(2025, 9, 7)),
            TAIYIN_ECLIPSE_TOTAL,
            TAIYIN_NATIVE_POSITION_XYZ,
            &local_lunar,
            &diag);
        if (status != TAIYIN_ERROR_INVALID_ARGUMENT) {
            return fail("local lunar search should reject native XYZ output flag");
        }

        LunarEclipseResultUt bad_lunar_flags;
        status = solve_lunar_eclipse_at_ut(
            &ctx,
            split_jd(jd(2025, 9, 7)),
            TAIYIN_NATIVE_POSITION_XYZ,
            &bad_lunar_flags,
            &diag);
        if (status != TAIYIN_ERROR_INVALID_ARGUMENT) {
            return fail("lunar eclipse should reject native XYZ output flag");
        }

        SolarEclipseResultUt bad_solar_flags;
        status = solve_solar_eclipse_at_ut(
            &ctx,
            split_jd(jd(2024, 4, 8)),
            TAIYIN_NATIVE_POSITION_SPEED,
            &bad_solar_flags,
            &diag);
        if (status != TAIYIN_ERROR_INVALID_ARGUMENT) {
            return fail("solar eclipse should reject native SPEED output flag");
        }

        status = solve_solar_eclipse_at_ut(
            &ctx,
            split_jd(jd(2024, 4, 8)),
            TAIYIN_NATIVE_POSITION_ASTROMETRIC,
            &bad_solar_flags,
            &diag);
        if (status != TAIYIN_ERROR_INVALID_ARGUMENT) {
            return fail("solar eclipse should reject unsupported astrometric position flag");
        }
    }

    // Searches beyond the ephemeris product boundary should report a boundary failure.
    {
        EphemerisEvalDiagnostic boundary_diag = {};
        SolarEclipseResultUt result;
        const Status status = search_next_solar_eclipse_ut(
            &ctx, split_jd(3000000.0), 0, 0, &result, &boundary_diag);
        if (expect_coverage_boundary_status(status, "solar search data boundary")) return 1;
        if (!is_coverage_boundary_status(boundary_diag.status)) {
            std::printf("FAIL: solar search data boundary diagnostic status=%d expected coverage boundary\n", boundary_diag.status);
            return 1;
        }
    }

    // Baseline TT solve: 2000-01-21 total lunar eclipse.
    {
        LunarEclipseResult result;
        if (expect_status(
                solve_lunar_eclipse_at(&ctx, split_jd(2451565.0), flags, &result, &diag),
                "solve_lunar_eclipse_at 2000")) {
            return 1;
        }
        if (expect_kind(result.kind, TAIYIN_ECLIPSE_TOTAL, "2000 total")) return 1;
        if (expect_close_days(result.maximum_jd_tt, 2451564.697622, 2.0 / 86400.0, "2000 maximum TT")) return 1;
        if (result.umbral_magnitude <= 1.0) return fail("2000 umbral magnitude should be total");
        if (!split_julian_date_is_finite(
                result.contact_jd_tt[TAIYIN_LUNAR_ECLIPSE_CONTACT_P1])) {
            return fail("2000 P1 contact should be finite");
        }
    }

    // search_next should find the same 2000 event after J2000.
    {
        LunarEclipseResult result;
        if (expect_status(
                search_next_lunar_eclipse_tt(
                    &ctx, split_jd(2451550.0), 0, flags, &result, &diag),
                "search_next_lunar_eclipse_tt")) {
            return 1;
        }
        if (expect_kind(result.kind, TAIYIN_ECLIPSE_TOTAL, "search_next 2000 total")) return 1;
        if (expect_close_days(result.maximum_jd_tt, 2451564.697622, 2.0 / 86400.0, "search_next 2000 maximum TT")) return 1;
    }

    // Range search regression: first two years from J2000 contain 5 lunar eclipses.
    {
        LunarEclipseResult results[20];
        size_t count = 0;
        if (expect_status(
                search_lunar_eclipses_tt(
                    &ctx,
                    split_jd(2451545.0),
                    split_jd(2451545.0 + 730.0),
                    0,
                    flags,
                    results,
                    20,
                    &count,
                    &diag),
                "search_lunar_eclipses_tt")) {
            return 1;
        }
        if (count != 5) {
            std::printf("FAIL: 2000-2001 range count=%zu expected=5\n", count);
            return 1;
        }
        if (expect_kind(results[0].kind, TAIYIN_ECLIPSE_TOTAL, "range[0] kind")) return 1;
        if (expect_kind(results[3].kind, TAIYIN_ECLIPSE_PARTIAL, "range[3] kind")) return 1;
        if (expect_kind(results[4].kind, TAIYIN_ECLIPSE_PENUMBRAL, "range[4] kind")) return 1;
    }

    // TS fixture: 2024-09-18 partial lunar eclipse, UT output.
    {
        LunarEclipseResultUt result;
        if (expect_status(
                solve_lunar_eclipse_at_ut(
                    &ctx, split_jd(2460571.61), flags, &result, &diag),
                "solve_lunar_eclipse_at_ut 2024")) {
            return 1;
        }
        if (expect_kind(result.kind, TAIYIN_ECLIPSE_PARTIAL, "2024 partial")) return 1;
        if (expect_close_value(result.umbral_magnitude, 0.091, 0.02, "2024 umbral magnitude")) return 1;
        const ContactFixture contacts[] = {
            {"2024 P1", TAIYIN_LUNAR_ECLIPSE_CONTACT_P1, 2460571.527291667},
            {"2024 U1", TAIYIN_LUNAR_ECLIPSE_CONTACT_U1, 2460571.591527778},
            {"2024 greatest", TAIYIN_LUNAR_ECLIPSE_CONTACT_GREATEST, 2460571.614097222},
            {"2024 U4", TAIYIN_LUNAR_ECLIPSE_CONTACT_U4, 2460571.636666667},
            {"2024 P4", TAIYIN_LUNAR_ECLIPSE_CONTACT_P4, 2460571.700833333},
        };
        if (expect_contacts_ut(result, contacts, sizeof(contacts) / sizeof(contacts[0]), 2.0 / 1440.0)) return 1;
        if (expect_nan(result.contact_jd_ut[TAIYIN_LUNAR_ECLIPSE_CONTACT_U2], "2024 U2")) return 1;
        if (expect_nan(result.contact_jd_ut[TAIYIN_LUNAR_ECLIPSE_CONTACT_U3], "2024 U3")) return 1;

        LunarEclipseResultUt truepos_result;
        if (expect_status(
                solve_lunar_eclipse_at_ut(
                    &ctx,
                    split_jd(2460571.61),
                    flags | TAIYIN_ECLIPSE_TRUEPOS,
                    &truepos_result,
                    &diag),
                "solve_lunar_eclipse_at_ut 2024 truepos")) {
            return 1;
        }
        if (expect_kind(truepos_result.kind, TAIYIN_ECLIPSE_PARTIAL, "2024 partial truepos")) return 1;
    }

    // NASA decade-table reference: 2024-09-18 partial lunar eclipse, greatest at 02:45:25 TD.
    {
        LunarEclipseResult result;
        if (expect_status(
                solve_lunar_eclipse_at(
                    &ctx, split_jd(jd(2024, 9, 18)), flags, &result, &diag),
                "solve_lunar_eclipse_at 2024 NASA TT")) {
            return 1;
        }
        if (expect_kind(result.kind, TAIYIN_ECLIPSE_PARTIAL, "2024 NASA partial")) return 1;
        if (expect_close_days(result.maximum_jd_tt, tt(2024, 9, 18, 2, 45, 25), 30.0 / 86400.0, "2024 NASA maximum TT")) return 1;
        if (expect_close_value(result.umbral_magnitude, 0.085, 0.02, "2024 NASA umbral magnitude")) return 1;
    }

    // TS behavior fixture: lunar shadow and Moon-radius model presets.
    {
        auto solve_with_models = [&](int shadow_model, int moon_radius_model, LunarEclipseResultUt* out) -> int {
            NativeCalcContext model_ctx = ctx;
            model_ctx.eclipse_shadow_model_id = static_cast<uint8_t>(shadow_model);
            model_ctx.eclipse_moon_radius_model_id = static_cast<uint8_t>(moon_radius_model);
            return expect_status(
                solve_lunar_eclipse_at_ut(
                    &model_ctx, split_jd(2460571.61), flags, out, &diag),
                "solve_lunar_eclipse_at_ut shadow model");
        };

        LunarEclipseResultUt geometric;
        LunarEclipseResultUt raw_danjon;
        LunarEclipseResultUt nasa_danjon;
        LunarEclipseResultUt mean_moon;
        if (solve_with_models(dispatch::ECLIPSE_SHADOW_GEOMETRIC, dispatch::ECLIPSE_MOON_ALMANAC, &geometric)) return 1;
        if (solve_with_models(dispatch::ECLIPSE_SHADOW_RAW_DANJON, dispatch::ECLIPSE_MOON_ALMANAC, &raw_danjon)) return 1;
        if (solve_with_models(dispatch::ECLIPSE_SHADOW_NASA_DANJON, dispatch::ECLIPSE_MOON_ALMANAC, &nasa_danjon)) return 1;
        if (solve_with_models(dispatch::ECLIPSE_SHADOW_NASA_DANJON, dispatch::ECLIPSE_MOON_MEAN, &mean_moon)) return 1;

        if (!(raw_danjon.umbral_magnitude > geometric.umbral_magnitude)) {
            return fail("raw Danjon umbral magnitude should exceed geometric");
        }
        if (!(nasa_danjon.umbral_magnitude > geometric.umbral_magnitude)) {
            return fail("NASA Danjon umbral magnitude should exceed geometric");
        }
        if (!(nasa_danjon.penumbra_radius_rad > geometric.penumbra_radius_rad)) {
            return fail("NASA Danjon penumbra should exceed geometric");
        }
        if (!(nasa_danjon.moon_radius_rad > mean_moon.moon_radius_rad)) {
            return fail("almanac Moon radius should exceed mean Moon radius");
        }
    }

    // TS fixture: 2025-09-07 total lunar eclipse, UT output.
    {
        LunarEclipseResultUt result;
        if (expect_status(
                solve_lunar_eclipse_at_ut(
                    &ctx, split_jd(2460926.25), flags, &result, &diag),
                "solve_lunar_eclipse_at_ut 2025")) {
            return 1;
        }
        if (expect_kind(result.kind, TAIYIN_ECLIPSE_TOTAL, "2025 total")) return 1;
        if (expect_close_value(result.umbral_magnitude, 1.367, 0.02, "2025 umbral magnitude")) return 1;
        if (result.delta_t_seconds < 60.0) return fail("2025 delta_t should be greater than 60 seconds");
        const ContactFixture contacts[] = {
            {"2025 P1", TAIYIN_LUNAR_ECLIPSE_CONTACT_P1, 2460926.143681},
            {"2025 U1", TAIYIN_LUNAR_ECLIPSE_CONTACT_U1, 2460926.185278},
            {"2025 U2", TAIYIN_LUNAR_ECLIPSE_CONTACT_U2, 2460926.229444},
            {"2025 greatest", TAIYIN_LUNAR_ECLIPSE_CONTACT_GREATEST, 2460926.258194},
            {"2025 U3", TAIYIN_LUNAR_ECLIPSE_CONTACT_U3, 2460926.286944},
            {"2025 U4", TAIYIN_LUNAR_ECLIPSE_CONTACT_U4, 2460926.331181},
            {"2025 P4", TAIYIN_LUNAR_ECLIPSE_CONTACT_P4, 2460926.372639},
        };
        if (expect_contacts_ut(result, contacts, sizeof(contacts) / sizeof(contacts[0]), 2.0 / 1440.0)) return 1;
    }

    // NASA decade-table reference: 2025-09-07 total lunar eclipse, greatest at 18:12:58 TD.
    {
        LunarEclipseResult result;
        if (expect_status(
                solve_lunar_eclipse_at(
                    &ctx, split_jd(jd(2025, 9, 7)), flags, &result, &diag),
                "solve_lunar_eclipse_at 2025 NASA TT")) {
            return 1;
        }
        if (expect_kind(result.kind, TAIYIN_ECLIPSE_TOTAL, "2025 NASA total")) return 1;
        if (expect_close_days(result.maximum_jd_tt, tt(2025, 9, 7, 18, 12, 58), 30.0 / 86400.0, "2025 NASA maximum TT")) return 1;
        if (expect_close_value(result.umbral_magnitude, 1.362, 0.02, "2025 NASA umbral magnitude")) return 1;
    }

    // UT search wrappers should return UT results in the requested range.
    {
        LunarEclipseResultUt next;
        if (expect_status(
                search_next_lunar_eclipse_ut(
                    &ctx, split_jd(2460926.0), 0, flags, &next, &diag),
                "search_next_lunar_eclipse_ut 2025")) {
            return 1;
        }
        if (expect_kind(next.kind, TAIYIN_ECLIPSE_TOTAL, "search_next_ut 2025 total")) return 1;
        if (expect_close_days(next.maximum_jd_ut, 2460926.258194, 2.0 / 1440.0, "search_next_ut 2025 maximum")) return 1;

        LunarEclipseResultUt previous;
        if (expect_status(
                search_next_lunar_eclipse_ut(
                    &ctx,
                    split_jd(2460927.0),
                    0,
                    flags | TAIYIN_ECLIPSE_BACKWARD,
                    &previous,
                    &diag),
                "search_previous_lunar_eclipse_ut 2025")) {
            return 1;
        }
        if (expect_kind(previous.kind, TAIYIN_ECLIPSE_TOTAL, "search_previous_ut 2025 total")) return 1;
        if (expect_close_days(previous.maximum_jd_ut, 2460926.258194, 2.0 / 1440.0, "search_previous_ut 2025 maximum")) return 1;

        LunarEclipseResultUt results[4];
        size_t count = 0;
        if (expect_status(
                search_lunar_eclipses_ut(
                    &ctx,
                    split_jd(2460926.0),
                    split_jd(2460927.0),
                    0,
                    flags,
                    results,
                    4,
                    &count,
                    &diag),
                "search_lunar_eclipses_ut 2025")) {
            return 1;
        }
        if (count != 1) {
            std::printf("FAIL: search_lunar_eclipses_ut count=%zu expected=1\n", count);
            return 1;
        }
        if (expect_kind(results[0].kind, TAIYIN_ECLIPSE_TOTAL, "range_ut 2025 total")) return 1;
    }

    // Penumbral exclusion should skip the 2002-05 penumbral event in this range.
    {
        LunarEclipseResult results[20];
        size_t count = 0;
        if (expect_status(
                search_lunar_eclipses_tt(
                    &ctx,
                    split_jd(2451545.0),
                    split_jd(2451545.0 + 730.0),
                    0,
                    flags | TAIYIN_ECLIPSE_EXCLUDE_PENUMBRAL,
                    results,
                    20,
                    &count,
                    &diag),
                "search_lunar_eclipses_tt exclude penumbral")) {
            return 1;
        }
        if (count != 4) {
            std::printf("FAIL: exclude penumbral count=%zu expected=4\n", count);
            return 1;
        }
    }

    {
        taiyin::runtime::NativeCalcContext beijing_ctx =
            make_context_with_observer(116.4074, 39.9042, 43.0);
        LocalLunarEclipseResultUt beijing;
        if (expect_status(
                search_next_local_lunar_eclipse_ut(
                    &beijing_ctx,
                    split_jd(jd(2025, 9, 7)),
                    TAIYIN_ECLIPSE_TOTAL,
                    TAIYIN_ECLIPSE_INCLUDE_CONTACTS,
                    &beijing,
                    &diag),
                "search_next_local_lunar_eclipse_ut Beijing 2025 total")) {
            return 1;
        }
        if (expect_kind(beijing.eclipse_kind, TAIYIN_ECLIPSE_TOTAL, "local lunar Beijing kind")) {
            return 1;
        }
        if (expect_kind_has(beijing.visibility_flags, TAIYIN_ECLIPSE_VISIBLE_AT_OBSERVER, "local lunar Beijing visible")) {
            return 1;
        }
        if (expect_kind_has(beijing.visibility_flags, TAIYIN_ECLIPSE_MAXIMUM_VISIBLE, "local lunar Beijing greatest visible")) {
            return 1;
        }
        if (!std::isfinite(beijing.contact_moon_altitude_deg[TAIYIN_LUNAR_ECLIPSE_CONTACT_GREATEST])
            || beijing.contact_moon_altitude_deg[TAIYIN_LUNAR_ECLIPSE_CONTACT_GREATEST] <= 0.0) {
            return fail("local lunar Beijing greatest altitude should be positive");
        }
        if (expect_nan(beijing.moonrise_jd_ut, "local lunar Beijing moonrise during eclipse")) {
            return 1;
        }

        taiyin::runtime::NativeCalcContext beijing_topocentric_ctx = make_context();
        const taiyin::runtime::NativeObserverLocation beijing_location =
            taiyin::runtime::native_observer_location_degrees(116.4074, 39.9042, 43.0);
        const Status topo_status = taiyin::runtime::native_context_set_simple_topocentric_observer(
            &beijing_topocentric_ctx,
            beijing_location,
            split_jd(jd(2025, 9, 7)),
            split_jd(jd(2025, 9, 7)));
        if (topo_status != TAIYIN_STATUS_OK) {
            return fail("set Beijing simple topocentric observer");
        }
        LocalLunarEclipseResultUt beijing_topocentric;
        if (expect_status(
                search_next_local_lunar_eclipse_ut(
                    &beijing_topocentric_ctx,
                    split_jd(jd(2025, 9, 7)),
                    TAIYIN_ECLIPSE_TOTAL,
                    TAIYIN_ECLIPSE_INCLUDE_CONTACTS,
                    &beijing_topocentric,
                    &diag),
                "search_next_local_lunar_eclipse_ut Beijing topocentric ctx")) {
            return 1;
        }
        if (expect_kind_has(
                beijing_topocentric.visibility_flags,
                TAIYIN_ECLIPSE_MAXIMUM_VISIBLE,
                "Beijing topocentric context local lunar greatest visible")) {
            return 1;
        }

        LunarEclipseResultUt global;
        if (expect_status(
                search_next_lunar_eclipse_ut(
                    &ctx,
                    split_jd(jd(2025, 9, 7)),
                    TAIYIN_ECLIPSE_TOTAL,
                    TAIYIN_ECLIPSE_INCLUDE_CONTACTS,
                    &global,
                    &diag),
                "search_next_lunar_eclipse_ut 2025 total for local compute")) {
            return 1;
        }
        LocalLunarEclipseResultUt computed;
        if (expect_status(
                compute_local_lunar_eclipse_visibility_ut(
                    &beijing_ctx,
                    &global,
                    0u,
                    &computed,
                    &diag),
                "compute_local_lunar_eclipse_visibility_ut Beijing 2025 total")) {
            return 1;
        }
        if (expect_close_days(
                computed.maximum_jd_ut,
                beijing.maximum_jd_ut,
                1.0e-12,
                "computed local lunar maximum matches search")) {
            return 1;
        }
        if (expect_close_value(
                computed.contact_moon_altitude_deg[TAIYIN_LUNAR_ECLIPSE_CONTACT_GREATEST],
                beijing.contact_moon_altitude_deg[TAIYIN_LUNAR_ECLIPSE_CONTACT_GREATEST],
                1.0e-12,
                "computed local lunar altitude matches search")) {
            return 1;
        }

        LocalLunarEclipseResult beijing_tt;
        if (expect_status(
                search_next_local_lunar_eclipse_tt(
                    &beijing_ctx,
                    split_jd(tt(2025, 9, 7, 0, 0)),
                    TAIYIN_ECLIPSE_TOTAL,
                    TAIYIN_ECLIPSE_INCLUDE_CONTACTS,
                    &beijing_tt,
                    &diag),
                "search_next_local_lunar_eclipse_tt Beijing 2025 total")) {
            return 1;
        }
        if (expect_kind(beijing_tt.eclipse_kind, TAIYIN_ECLIPSE_TOTAL, "local lunar Beijing TT kind")) {
            return 1;
        }
        if (expect_kind_has(beijing_tt.visibility_flags, TAIYIN_ECLIPSE_MAXIMUM_VISIBLE, "local lunar Beijing TT greatest visible")) {
            return 1;
        }
        if (expect_close_value(
                beijing_tt.contact_moon_altitude_deg[TAIYIN_LUNAR_ECLIPSE_CONTACT_GREATEST],
                beijing.contact_moon_altitude_deg[TAIYIN_LUNAR_ECLIPSE_CONTACT_GREATEST],
                1.0e-9,
                "local lunar Beijing TT altitude matches UT")) {
            return 1;
        }

        LunarEclipseResultUt missing_contacts = global;
        for (size_t i = 0; i < TAIYIN_LUNAR_ECLIPSE_CONTACT_COUNT; ++i) {
            missing_contacts.contact_jd_ut[i] = split_jd(std::nan(""));
        }
        LocalLunarEclipseResultUt rejected_missing_contacts;
        const Status missing_contact_status = compute_local_lunar_eclipse_visibility_ut(
            &beijing_ctx,
            &missing_contacts,
            0u,
            &rejected_missing_contacts,
            &diag);
        if (missing_contact_status != TAIYIN_ERROR_INVALID_ARGUMENT) {
            return fail("local lunar compute should reject non-empty eclipse without contacts");
        }

        LocalLunarEclipseResultUt beijing_refracted;
        if (expect_status(
                compute_local_lunar_eclipse_visibility_ut(
                    &beijing_ctx,
                    &global,
                    TAIYIN_LOCAL_LUNAR_ECLIPSE_REFRACTION,
                    &beijing_refracted,
                    &diag),
                "compute_local_lunar_eclipse_visibility_ut Beijing 2025 refraction")) {
            return 1;
        }
        if (!(beijing_refracted.contact_moon_altitude_deg[TAIYIN_LUNAR_ECLIPSE_CONTACT_GREATEST]
                > computed.contact_moon_altitude_deg[TAIYIN_LUNAR_ECLIPSE_CONTACT_GREATEST])) {
            return fail("local lunar refraction should raise greatest-eclipse altitude");
        }
    }

    {
        taiyin::runtime::NativeCalcContext new_york_ctx =
            make_context_with_observer(-74.0060, 40.7128, 10.0);
        LocalLunarEclipseResultUt new_york;
        if (expect_status(
                search_next_local_lunar_eclipse_ut(
                    &new_york_ctx,
                    split_jd(jd(2025, 9, 7)),
                    TAIYIN_ECLIPSE_TOTAL,
                    TAIYIN_ECLIPSE_INCLUDE_CONTACTS,
                    &new_york,
                    &diag),
                "search_next_local_lunar_eclipse_ut New York 2025 total")) {
            return 1;
        }
        if (expect_kind(new_york.eclipse_kind, TAIYIN_ECLIPSE_TOTAL, "local lunar New York kind")) {
            return 1;
        }
        if (expect_bits_clear(
                new_york.visibility_flags,
                TAIYIN_ECLIPSE_VISIBLE_AT_OBSERVER | TAIYIN_ECLIPSE_MAXIMUM_VISIBLE,
                "local lunar New York invisible")) {
            return 1;
        }
        if (!std::isfinite(new_york.contact_moon_altitude_deg[TAIYIN_LUNAR_ECLIPSE_CONTACT_GREATEST])
            || new_york.contact_moon_altitude_deg[TAIYIN_LUNAR_ECLIPSE_CONTACT_GREATEST] >= 0.0) {
            return fail("local lunar New York greatest altitude should be negative");
        }
    }

    {
        taiyin::runtime::NativeCalcContext london_ctx =
            make_context_with_observer(-0.1276, 51.5072, 15.0);
        LocalLunarEclipseResultUt london;
        if (expect_status(
                search_next_local_lunar_eclipse_ut(
                    &london_ctx,
                    split_jd(jd(2025, 9, 7)),
                    TAIYIN_ECLIPSE_TOTAL,
                    TAIYIN_ECLIPSE_INCLUDE_CONTACTS,
                    &london,
                    &diag),
                "search_next_local_lunar_eclipse_ut London 2025 moonrise during eclipse")) {
            return 1;
        }
        if (expect_kind(london.eclipse_kind, TAIYIN_ECLIPSE_TOTAL, "local lunar London kind")) {
            return 1;
        }
        if (expect_kind_has(london.visibility_flags, TAIYIN_ECLIPSE_VISIBLE_AT_OBSERVER, "local lunar London visible via moonrise")) {
            return 1;
        }
        if (expect_bits_clear(london.visibility_flags, TAIYIN_ECLIPSE_MAXIMUM_VISIBLE, "local lunar London greatest below horizon")) {
            return 1;
        }
        if (!split_julian_date_is_finite(london.moonrise_jd_ut)
            || !(london.moonrise_jd_ut > london.contact_jd_ut[TAIYIN_LUNAR_ECLIPSE_CONTACT_P1])
            || !(london.moonrise_jd_ut < london.contact_jd_ut[TAIYIN_LUNAR_ECLIPSE_CONTACT_P4])) {
            return fail("local lunar London moonrise should fall inside P1-P4");
        }
        if (!std::isfinite(london.contact_moon_altitude_deg[TAIYIN_LUNAR_ECLIPSE_CONTACT_GREATEST])
            || london.contact_moon_altitude_deg[TAIYIN_LUNAR_ECLIPSE_CONTACT_GREATEST] >= 0.0) {
            return fail("local lunar London greatest altitude should be negative");
        }
    }

    // Global solar eclipse solver: 2024-04-08 total solar eclipse.
    // The global greatest instant is the shadow-axis closest approach.  Do not
    // regress to optimizing the penumbral margin, whose radius varies in time.
    {
        SolarEclipseResultUt result;
        if (expect_status(
                solve_solar_eclipse_at_ut(
                    &ctx,
                    split_jd(2460409.25),
                    TAIYIN_ECLIPSE_INCLUDE_CONTACTS,
                    &result,
                    &diag),
                "solve_solar_eclipse_at_ut 2024 total")) {
            return 1;
        }
        if (expect_kind_has(result.kind, TAIYIN_ECLIPSE_TOTAL | TAIYIN_ECLIPSE_CENTRAL, "2024 solar total")) return 1;
        if (expect_close_days(result.maximum_jd_ut, 2460409.262039739, 2.0 / 86400.0, "2024 solar maximum UT")) return 1;
        if (expect_close_days(result.contact_jd_ut[TAIYIN_SOLAR_ECLIPSE_CONTACT_P1],
                              2460409.154338569, 2.0 / 1440.0, "2024 solar P1")) return 1;
        if (expect_close_days(result.contact_jd_ut[TAIYIN_SOLAR_ECLIPSE_CONTACT_C1],
                              2460409.194446871, 2.0 / 1440.0, "2024 solar C1")) return 1;
        if (expect_close_days(result.contact_jd_ut[TAIYIN_SOLAR_ECLIPSE_CONTACT_GREATEST],
                              2460409.262039739, 2.0 / 1440.0, "2024 solar greatest contact")) return 1;
        if (expect_close_days(result.contact_jd_ut[TAIYIN_SOLAR_ECLIPSE_CONTACT_C4],
                              2460409.329500663, 2.0 / 1440.0, "2024 solar C4")) return 1;
        if (expect_close_days(result.contact_jd_ut[TAIYIN_SOLAR_ECLIPSE_CONTACT_P4],
                              2460409.369681376, 2.0 / 1440.0, "2024 solar P4")) return 1;
        if (expect_close_value(result.maximum_latitude_deg, 25.0 + 17.1 / 60.0, 0.3, "2024 solar maximum latitude")) return 1;
        if (expect_close_value(result.maximum_longitude_deg, -(104.0 + 8.6 / 60.0), 0.3, "2024 solar maximum longitude")) return 1;
        if (!(result.penumbral_margin_km < 0.0)) return fail("2024 solar penumbra should touch Earth");
        if (!(result.core_radius_km > 0.0)) return fail("2024 solar core should be umbral");

        SolarEclipseResultUt truepos_result;
        if (expect_status(
                solve_solar_eclipse_at_ut(
                    &ctx,
                    split_jd(2460409.25),
                    TAIYIN_ECLIPSE_INCLUDE_CONTACTS | TAIYIN_ECLIPSE_TRUEPOS,
                    &truepos_result,
                    &diag),
                "solve_solar_eclipse_at_ut 2024 truepos")) {
            return 1;
        }
        if (expect_close_days(truepos_result.maximum_jd_ut, 2460409.262426750, 2.0 / 86400.0, "2024 solar truepos maximum UT")) return 1;
        if (expect_close_days(truepos_result.contact_jd_ut[TAIYIN_SOLAR_ECLIPSE_CONTACT_C1],
                              2460409.194432870, 2.0 / 1440.0, "2024 solar truepos C1")) return 1;
    }

    // NASA decade-table greatest-time reference: this is a second-level global
    // timing oracle; contact, duration, and magnitude model details are not
    // compared here.
    // 2024-04-08 total solar eclipse, greatest at 18:18:29 TD.
    {
        SolarEclipseResult result;
        if (expect_status(
                solve_solar_eclipse_at(
                    &ctx,
                    split_jd(jd(2024, 4, 8)),
                    TAIYIN_ECLIPSE_INCLUDE_CONTACTS,
                    &result,
                    &diag),
                "solve_solar_eclipse_at 2024 NASA TT")) {
            return 1;
        }
        if (expect_kind_has(result.kind, TAIYIN_ECLIPSE_TOTAL | TAIYIN_ECLIPSE_CENTRAL, "2024 NASA solar total")) return 1;
        if (expect_close_days(result.maximum_jd_tt, tt(2024, 4, 8, 18, 18, 29), 30.0 / 86400.0, "2024 NASA solar maximum TT")) return 1;
    }

    // PMO public almanac fixture: 2026-08-12 total solar eclipse.
    // Source: Purple Mountain Observatory, "2026年8月12日日全食概况".
    {
        SolarEclipseResultUt result;
        if (expect_status(
                solve_solar_eclipse_at_ut(
                    &ctx,
                    split_jd(jd(2026, 8, 12)),
                    TAIYIN_ECLIPSE_INCLUDE_CONTACTS,
                    &result,
                    &diag),
                "solve_solar_eclipse_at_ut 2026 PMO total")) {
            return 1;
        }
        const double p1 = tt(2026, 8, 12, 15, 34, 14);
        const double c1 = tt(2026, 8, 12, 17, 0, 6);
        const double greatest = tt(2026, 8, 12, 17, 45, 56);
        const double c4 = tt(2026, 8, 12, 18, 32, 12);
        const double p4 = tt(2026, 8, 12, 19, 57, 59);
        SolarEclipseRouteRow row;
        if (expect_status(
                compute_solar_eclipse_route_row_ut(&ctx, result.maximum_jd_ut, 0, &row, &diag),
                "compute_solar_eclipse_route_row_ut 2026 PMO greatest")) {
            return 1;
        }
        if (expect_kind_has(result.kind, TAIYIN_ECLIPSE_TOTAL | TAIYIN_ECLIPSE_CENTRAL, "2026 PMO solar total")) return 1;
        if (expect_close_days(result.contact_jd_ut[TAIYIN_SOLAR_ECLIPSE_CONTACT_P1], p1, 2.0 / 86400.0, "2026 PMO solar P1")) return 1;
        if (expect_close_days(result.contact_jd_ut[TAIYIN_SOLAR_ECLIPSE_CONTACT_C1], c1, 2.0 / 86400.0, "2026 PMO solar C1")) return 1;
        if (expect_close_days(result.maximum_jd_ut, greatest, 2.0 / 86400.0, "2026 PMO solar maximum UT")) return 1;
        if (expect_close_days(result.contact_jd_ut[TAIYIN_SOLAR_ECLIPSE_CONTACT_C4], c4, 2.0 / 86400.0, "2026 PMO solar C4")) return 1;
        if (expect_close_days(result.contact_jd_ut[TAIYIN_SOLAR_ECLIPSE_CONTACT_P4], p4, 2.0 / 86400.0, "2026 PMO solar P4")) return 1;
        if (expect_close_value(result.maximum_latitude_deg, 65.0 + 13.3 / 60.0, 0.02, "2026 PMO solar maximum latitude")) return 1;
        if (expect_close_value(result.maximum_longitude_deg, -(25.0 + 15.2 / 60.0), 0.02, "2026 PMO solar maximum longitude")) return 1;
        if (expect_close_geodetic_point(
                row.center_line.latitude_deg,
                row.center_line.longitude_deg,
                65.0 + 13.7 / 60.0,
                -(25.0 + 15.5 / 60.0),
                0.05,
                "2026 PMO route center point")) {
            return 1;
        }
        if (expect_close_geodetic_point(
                row.north_limit.latitude_deg,
                row.north_limit.longitude_deg,
                65.0 + 39.5 / 60.0,
                -(22.0 + 5.8 / 60.0),
                0.05,
                "2026 PMO route north point")) {
            return 1;
        }
        if (expect_close_geodetic_point(
                row.south_limit.latitude_deg,
                row.south_limit.longitude_deg,
                64.0 + 45.4 / 60.0,
                -(28.0 + 13.5 / 60.0),
                0.05,
                "2026 PMO route south point")) {
            return 1;
        }
        if (expect_close_value(row.duration_seconds, 141.2, 5.0, "2026 PMO solar total duration")) return 1;
        if (expect_close_value(row.path_width_km, 300.3, 12.0, "2026 PMO solar path width")) return 1;

        SolarEclipseRouteRow early_row;
        if (expect_status(
                compute_solar_eclipse_route_row_ut(
                    &ctx,
                    split_jd(tt(2026, 8, 12, 17, 3, 50)),
                    0,
                    &early_row,
                    &diag),
                "compute_solar_eclipse_route_row_ut 2026 PMO early route")) {
            return 1;
        }
        if (expect_close_geodetic_point(
                early_row.center_line.latitude_deg,
                early_row.center_line.longitude_deg,
                85.0 + 3.5 / 60.0,
                105.0 + 29.1 / 60.0,
                0.10,
                "2026 PMO early route center point")) {
            return 1;
        }
        if (expect_close_value(early_row.path_width_km, 280.7, 12.0, "2026 PMO early route path width")) return 1;

        // Route-table widths near central-path emergence/disappearance must
        // intersect the closed path polygon, including its horizon caps.
        // Extrapolating only the moving north/south limit curves is singular
        // here and used to overstate these two rows by roughly 80 km.
        const SplitJulianDate begin_start = split_jd(tt(2026, 8, 12, 17, 2, 5));
        const SplitJulianDate begin_end = split_jd(tt(2026, 8, 12, 17, 2, 15));
        SolarEclipseRouteRow begin_rows[3];
        size_t begin_count = 0;
        if (expect_status(
                compute_solar_eclipse_route_ut(
                    &ctx,
                    begin_start,
                    begin_end,
                    5.0 / 60.0,
                    0,
                    begin_rows,
                    3,
                    &begin_count,
                    &diag),
                "compute_solar_eclipse_route_ut 2026 PMO begin cap")) {
            return 1;
        }
        if (begin_count != 3
            || expect_close_value(
                seconds_between_split_jd(begin_rows[0].jd_ut, begin_rows[1].jd_ut),
                5.0,
                1.0e-9,
                "2026 PMO begin-cap UT step 1")
            || expect_close_value(
                seconds_between_split_jd(begin_rows[2].jd_ut, begin_end),
                0.0,
                1.0e-9,
                "2026 PMO begin-cap exact UT endpoint")
            || expect_close_value(
                begin_rows[0].path_width_km,
                280.2,
                8.0,
                "2026 PMO begin-cap path width")) {
            return 1;
        }

        // A zero-length batch is the batch form of the single-row API and
        // must receive the same closed-polygon width refinement.
        SolarEclipseRouteRow begin_single_row;
        if (expect_status(
                compute_solar_eclipse_route_row_ut(
                    &ctx,
                    begin_start,
                    0,
                    &begin_single_row,
                    &diag),
                "compute_solar_eclipse_route_row_ut 2026 PMO single row")
            || expect_status(
                compute_solar_eclipse_route_ut(
                    &ctx,
                    begin_start,
                    begin_start,
                    5.0 / 60.0,
                    0,
                    begin_rows,
                    3,
                    &begin_count,
                    &diag),
                "compute_solar_eclipse_route_ut 2026 PMO zero-length batch")) {
            return 1;
        }
        if (begin_count != 1
            || expect_close_value(
                begin_rows[0].path_width_km,
                begin_single_row.path_width_km,
                1.0e-9,
                "2026 PMO zero-length batch path width")) {
            return 1;
        }

        // A sparse batch may contain rows from separate eclipses.  Each event
        // must use its own closed path polygon rather than the polygon nearest
        // the middle row of the entire batch.
        const SplitJulianDate second_event = split_jd(2461443.2438330743);
        SolarEclipseRouteRow multi_event_rows[2];
        size_t multi_event_count = 0;
        if (expect_status(
                compute_solar_eclipse_route_ut(
                    &ctx,
                    begin_start,
                    second_event,
                    (second_event - begin_start) * 1440.0,
                    0,
                    multi_event_rows,
                    2,
                    &multi_event_count,
                    &diag),
                "compute_solar_eclipse_route_ut multi-event batch")) {
            return 1;
        }
        if (multi_event_count != 2
            || expect_close_value(
                multi_event_rows[0].path_width_km,
                begin_single_row.path_width_km,
                1.0e-9,
                "multi-event batch first eclipse path width")) {
            return 1;
        }

        const SplitJulianDate end_start = split_jd(tt(2026, 8, 12, 18, 30, 5));
        const SplitJulianDate end_end = split_jd(tt(2026, 8, 12, 18, 30, 15));
        SolarEclipseRouteRow end_rows[3];
        size_t end_count = 0;
        if (expect_status(
                compute_solar_eclipse_route_ut(
                    &ctx,
                    end_start,
                    end_end,
                    5.0 / 60.0,
                    0,
                    end_rows,
                    3,
                    &end_count,
                    &diag),
                "compute_solar_eclipse_route_ut 2026 PMO end cap")) {
            return 1;
        }
        if (end_count != 3
            || expect_close_value(
                seconds_between_split_jd(end_rows[0].jd_ut, end_rows[1].jd_ut),
                5.0,
                1.0e-9,
                "2026 PMO end-cap UT step 1")
            || expect_close_value(
                seconds_between_split_jd(end_rows[2].jd_ut, end_end),
                0.0,
                1.0e-9,
                "2026 PMO end-cap exact UT endpoint")
            || expect_close_value(
                end_rows[end_count - 1].path_width_km,
                300.1,
                8.0,
                "2026 PMO end-cap path width")) {
            return 1;
        }
    }

    // Single-time solar route row at greatest eclipse.
    {
        SolarEclipseRouteRow row;
        if (expect_status(
                compute_solar_eclipse_route_row_ut(
                    &ctx, split_jd(2460409.262039739), 0, &row, &diag),
                "compute_solar_eclipse_route_row_ut 2024")) {
            return 1;
        }
        if (expect_close_value(row.center_line.latitude_deg, 25.289608540, 1e-4, "route center latitude")) return 1;
        if (expect_close_value(row.center_line.longitude_deg, -104.147998749, 1e-4, "route center longitude")) return 1;
        if (expect_close_value(row.penumbral_north_limit.latitude_deg, 53.312922100, 0.05, "route penumbral north latitude")) return 1;
        if (expect_close_value(row.penumbral_north_limit.longitude_deg, -144.889452980, 0.05, "route penumbral north longitude")) return 1;
        if (expect_close_value(row.penumbral_south_limit.latitude_deg, 3.537775191, 0.05, "route penumbral south latitude")) return 1;
        if (expect_close_value(row.penumbral_south_limit.longitude_deg, -81.212813766, 0.05, "route penumbral south longitude")) return 1;
        if (expect_close_value(row.half_magnitude_north_limit.latitude_deg, 37.745914624, 0.05, "route half magnitude north latitude")) return 1;
        if (expect_close_value(row.half_magnitude_north_limit.longitude_deg, -119.359058152, 0.05, "route half magnitude north longitude")) return 1;
        if (expect_close_value(row.half_magnitude_south_limit.latitude_deg, 14.252052731, 0.05, "route half magnitude south latitude")) return 1;
        if (expect_close_value(row.half_magnitude_south_limit.longitude_deg, -91.755650581, 0.05, "route half magnitude south longitude")) return 1;
        if (expect_close_value(row.north_limit.latitude_deg, 25.901536146, 0.05, "route north latitude")) return 1;
        if (expect_close_value(row.north_limit.longitude_deg, -104.865324701, 0.05, "route north longitude")) return 1;
        if (expect_close_value(row.south_limit.latitude_deg, 24.678683194, 0.05, "route south latitude")) return 1;
        if (expect_close_value(row.south_limit.longitude_deg, -103.436308474, 0.05, "route south longitude")) return 1;
        if (expect_close_value(row.path_width_km, 197.862736, 5.0, "route path width")) return 1;
        if (expect_close_value(row.duration_seconds, 268.106442, 8.0, "route duration")) return 1;
        if (!(std::isfinite(row.north_limit.latitude_deg) && std::isfinite(row.south_limit.latitude_deg))) {
            return fail("route row should include both limits");
        }

        // The lightweight "where" API deliberately omits the expensive
        // center-line refinement and route metrics, but must still expose the
        // instantaneous global geometry needed by an eclipse map.
        SolarEclipseWhere where;
        if (expect_status(
                compute_solar_eclipse_where_ut(
                    &ctx, split_jd(2460409.262039739), 0, &where, &diag),
                "compute_solar_eclipse_where_ut 2024")) {
            return 1;
        }
        if (!(std::isfinite(where.center_line.latitude_deg)
              && std::isfinite(where.center_line.longitude_deg)
              && std::isfinite(where.penumbral_north_limit.latitude_deg)
              && std::isfinite(where.penumbral_south_limit.latitude_deg)
              && std::isfinite(where.north_limit.latitude_deg)
              && std::isfinite(where.south_limit.latitude_deg)
              && std::isfinite(where.magnitude)
              && std::isfinite(where.obscuration)
              && std::isfinite(where.center_line.sun_altitude_deg)
              && std::isfinite(where.center_line.sun_azimuth_deg))) {
            return fail("lightweight solar-eclipse geometry should be finite at 2024 maximum");
        }
        if (expect_close_value(
                where.center_line.latitude_deg, row.center_line.latitude_deg,
                0.1, "lightweight route center latitude")) return 1;
        if (expect_close_value(
                where.center_line.longitude_deg, row.center_line.longitude_deg,
                0.1, "lightweight route center longitude")) return 1;

        NativeCalcContext mean_moon_ctx = ctx;
        mean_moon_ctx.eclipse_moon_radius_model_id =
            static_cast<uint8_t>(taiyin::dispatch::ECLIPSE_MOON_MEAN);
        SolarEclipseRouteRow mean_row;
        if (expect_status(
                compute_solar_eclipse_route_row_ut(
                    &mean_moon_ctx, split_jd(2460409.262039739), 0, &mean_row, &diag),
                "compute_solar_eclipse_route_row_ut 2024 mean moon radius")) {
            return 1;
        }
        if (!(std::fabs(mean_row.north_limit.latitude_deg - row.north_limit.latitude_deg) > 1e-4
              || std::fabs(mean_row.south_limit.latitude_deg - row.south_limit.latitude_deg) > 1e-4
              || std::fabs(mean_row.path_width_km - row.path_width_km) > 0.01)) {
            return fail("route geometry should honor configured moon radius model");
        }
    }

    // Annular route row exercises the antumbral north/south swap branch.
    {
        SolarEclipseResultUt result;
        if (expect_status(
                solve_solar_eclipse_at_ut(
                    &ctx,
                    split_jd(2460232.25),
                    TAIYIN_ECLIPSE_INCLUDE_CONTACTS,
                    &result,
                    &diag),
                "solve_solar_eclipse_at_ut 2023 annular")) {
            return 1;
        }
        if (expect_kind_has(result.kind, TAIYIN_ECLIPSE_ANNULAR | TAIYIN_ECLIPSE_CENTRAL, "2023 solar annular")) return 1;

        SolarEclipseRouteRow row;
        if (expect_status(
                compute_solar_eclipse_route_row_ut(&ctx, result.maximum_jd_ut, 0, &row, &diag),
                "compute_solar_eclipse_route_row_ut 2023 annular")) {
            return 1;
        }
        if (expect_close_value(row.center_line.latitude_deg, 11.368144697, 1e-4, "annular route center latitude")) return 1;
        if (expect_close_value(row.center_line.longitude_deg, -83.110866449, 1e-4, "annular route center longitude")) return 1;
        if (expect_close_value(row.penumbral_north_limit.latitude_deg, 48.434251638, 0.05, "annular route penumbral north latitude")) return 1;
        if (expect_close_value(row.penumbral_north_limit.longitude_deg, -46.630953457, 0.05, "annular route penumbral north longitude")) return 1;
        if (expect_close_value(row.penumbral_south_limit.latitude_deg, -9.549704399, 0.05, "annular route penumbral south latitude")) return 1;
        if (expect_close_value(row.penumbral_south_limit.longitude_deg, -108.366493549, 0.05, "annular route penumbral south longitude")) return 1;
        if (expect_close_value(row.half_magnitude_north_limit.latitude_deg, 24.682958676, 0.05, "annular route half magnitude north latitude")) return 1;
        if (expect_close_value(row.half_magnitude_north_limit.longitude_deg, -69.864498785, 0.05, "annular route half magnitude north longitude")) return 1;
        if (expect_close_value(row.half_magnitude_south_limit.latitude_deg, 0.978636756, 0.05, "annular route half magnitude south latitude")) return 1;
        if (expect_close_value(row.half_magnitude_south_limit.longitude_deg, -95.517186687, 0.05, "annular route half magnitude south longitude")) return 1;
        if (expect_close_value(row.north_limit.latitude_deg, 11.945514713, 0.05, "annular route north latitude")) return 1;
        if (expect_close_value(row.north_limit.longitude_deg, -82.474442831, 0.05, "annular route north longitude")) return 1;
        if (expect_close_value(row.south_limit.latitude_deg, 10.801184717, 0.05, "annular route south latitude")) return 1;
        if (expect_close_value(row.south_limit.longitude_deg, -83.744494672, 0.05, "annular route south longitude")) return 1;
        if (expect_close_value(row.path_width_km, 187.934341, 5.0, "annular route path width")) return 1;
        if (expect_close_value(row.duration_seconds, 317.267568, 12.0, "annular route duration")) return 1;
    }

    // Near the 2027 annular endpoint, only the northern antumbral limit still
    // intersects Earth. Swapping only when both limits are valid relabels this
    // surviving branch as south and creates a hook in exported route curves.
    {
        SolarEclipseRouteRow row;
        if (expect_status(
                compute_solar_eclipse_route_row_ut(
                    &ctx, split_jd(2461443.2438330743), 0, &row, &diag),
                "compute_solar_eclipse_route_row_ut 2027 annular endpoint")) {
            return 1;
        }
        if (!std::isfinite(row.north_limit.latitude_deg)
            || !std::isfinite(row.north_limit.longitude_deg)) {
            return fail("2027 annular endpoint should retain the northern core limit");
        }
        if (std::isfinite(row.south_limit.latitude_deg)
            || std::isfinite(row.south_limit.longitude_deg)) {
            return fail("2027 annular endpoint should not relabel the northern core limit as south");
        }
        if (expect_close_value(
                row.north_limit.latitude_deg,
                3.956400943,
                0.05,
                "2027 annular endpoint north latitude")) {
            return 1;
        }
        if (expect_close_value(
                row.north_limit.longitude_deg,
                -7.549454668,
                0.05,
                "2027 annular endpoint north longitude")) {
            return 1;
        }

        size_t point_count = 0;
        if (expect_status(
                compute_solar_eclipse_route_curves_ut(
                    &ctx,
                    split_jd(2461443.1664192257),
                    0,
                    nullptr,
                    0,
                    &point_count,
                    &diag),
                "compute_solar_eclipse_route_curves_ut 2027 annular limits count")) {
            return 1;
        }
        std::vector<SolarEclipseRouteCurvePoint> points(point_count);
        if (expect_status(
                compute_solar_eclipse_route_curves_ut(
                    &ctx,
                    split_jd(2461443.1664192257),
                    0,
                    points.data(),
                    points.size(),
                    &point_count,
                    &diag),
                "compute_solar_eclipse_route_curves_ut 2027 annular limits")) {
            return 1;
        }
        points.resize(point_count);
        const SolarEclipseRouteCurvePoint* center_first = nullptr;
        const SolarEclipseRouteCurvePoint* north_first = nullptr;
        const SolarEclipseRouteCurvePoint* south_first = nullptr;
        const SolarEclipseRouteCurvePoint* center_last = nullptr;
        const SolarEclipseRouteCurvePoint* north_last = nullptr;
        const SolarEclipseRouteCurvePoint* south_last = nullptr;
        const SolarEclipseRouteCurvePoint* begin_horizon_first = nullptr;
        const SolarEclipseRouteCurvePoint* begin_horizon_last = nullptr;
        const SolarEclipseRouteCurvePoint* end_horizon_first = nullptr;
        const SolarEclipseRouteCurvePoint* end_horizon_last = nullptr;
        double previous_center_jd = -INFINITY;
        double previous_north_jd = -INFINITY;
        double previous_south_jd = -INFINITY;
        for (size_t i = 0; i < point_count; ++i) {
            if (points[i].curve_kind == TAIYIN_SOLAR_ROUTE_CURVE_CENTER_LINE) {
                if (!center_first) center_first = &points[i];
                center_last = &points[i];
                if (scalar_jd(points[i].jd_tt) < previous_center_jd) {
                    return fail("2027 center line epochs must be monotonic");
                }
                previous_center_jd = scalar_jd(points[i].jd_tt);
            }
            if (points[i].curve_kind == TAIYIN_SOLAR_ROUTE_CURVE_CORE_NORTH) {
                if (!north_first) north_first = &points[i];
                north_last = &points[i];
                if (scalar_jd(points[i].jd_tt) < previous_north_jd) {
                    return fail("2027 north core limit epochs must be monotonic");
                }
                previous_north_jd = scalar_jd(points[i].jd_tt);
            }
            if (points[i].curve_kind == TAIYIN_SOLAR_ROUTE_CURVE_CORE_SOUTH) {
                if (!south_first) south_first = &points[i];
                south_last = &points[i];
                if (scalar_jd(points[i].jd_tt) < previous_south_jd) {
                    return fail("2027 south core limit epochs must be monotonic");
                }
                previous_south_jd = scalar_jd(points[i].jd_tt);
            }
            if (points[i].curve_kind == TAIYIN_SOLAR_ROUTE_CURVE_CORE_BEGIN_HORIZON) {
                if (!begin_horizon_first) begin_horizon_first = &points[i];
                begin_horizon_last = &points[i];
            }
            if (points[i].curve_kind == TAIYIN_SOLAR_ROUTE_CURVE_CORE_END_HORIZON) {
                if (!end_horizon_first) end_horizon_first = &points[i];
                end_horizon_last = &points[i];
            }
        }
        if (!center_first || !north_first || !south_first
            || !center_last || !north_last || !south_last
            || !begin_horizon_first || !begin_horizon_last
            || !end_horizon_first || !end_horizon_last) {
            return fail("2027 annular route should include both endpoints of the center and limits");
        }
        if (expect_close_value(
                begin_horizon_first->latitude_deg, south_first->latitude_deg, 1e-12,
                "2027 begin horizon starts at south limit")) return 1;
        if (expect_close_value(
                begin_horizon_first->longitude_deg, south_first->longitude_deg, 1e-12,
                "2027 begin horizon longitude starts at south limit")) return 1;
        if (expect_close_value(
                begin_horizon_last->latitude_deg, north_first->latitude_deg, 1e-12,
                "2027 begin horizon ends at north limit")) return 1;
        if (expect_close_value(
                begin_horizon_last->longitude_deg, north_first->longitude_deg, 1e-12,
                "2027 begin horizon longitude ends at north limit")) return 1;
        if (expect_close_value(
                end_horizon_first->latitude_deg, north_last->latitude_deg, 1e-12,
                "2027 end horizon starts at north limit")) return 1;
        if (expect_close_value(
                end_horizon_first->longitude_deg, north_last->longitude_deg, 1e-12,
                "2027 end horizon longitude starts at north limit")) return 1;
        if (expect_close_value(
                end_horizon_last->latitude_deg, south_last->latitude_deg, 1e-12,
                "2027 end horizon ends at south limit")) return 1;
        if (expect_close_value(
                end_horizon_last->longitude_deg, south_last->longitude_deg, 1e-12,
                "2027 end horizon longitude ends at south limit")) return 1;
    }

    // Route density is caller-controlled rather than fixed at the original
    // jieX 400-sample default.
    {
        size_t default_point_count = 0;
        if (expect_status(
                compute_solar_eclipse_route_curves_ut_with_options(
                    &ctx,
                    split_jd(2463007.80202768),
                    0,
                    400,
                    nullptr,
                    0,
                    &default_point_count,
                    &diag),
                "compute_solar_eclipse_route_curves_ut 2031 annular count")) {
            return 1;
        }
        size_t dense_point_count = 0;
        if (expect_status(
                compute_solar_eclipse_route_curves_ut_with_options(
                    &ctx,
                    split_jd(2463007.80202768),
                    0,
                    800,
                    nullptr,
                    0,
                    &dense_point_count,
                    &diag),
                "compute_solar_eclipse_route_curves_ut 2031 annular dense count")) {
            return 1;
        }
        if (!(dense_point_count > default_point_count)) {
            return fail("denser jieX sampling should return more route points");
        }
    }

    // NASA decade-table greatest-time reference: 2023-10-14 annular solar
    // eclipse, greatest at 18:00:40 TD.
    {
        SolarEclipseResult result;
        if (expect_status(
                solve_solar_eclipse_at(
                    &ctx,
                    split_jd(jd(2023, 10, 14)),
                    TAIYIN_ECLIPSE_INCLUDE_CONTACTS,
                    &result,
                    &diag),
                "solve_solar_eclipse_at 2023 NASA annular TT")) {
            return 1;
        }
        if (expect_kind_has(result.kind, TAIYIN_ECLIPSE_ANNULAR | TAIYIN_ECLIPSE_CENTRAL, "2023 NASA annular solar")) return 1;
        if (expect_close_days(result.maximum_jd_tt, tt(2023, 10, 14, 18, 0, 40), 30.0 / 86400.0, "2023 NASA annular maximum TT")) return 1;
    }

    // 2023-04-20 is a hybrid eclipse: the central path changes between
    // annular and total near the contact endpoints.
    {
        SolarEclipseResultUt result;
        if (expect_status(
                solve_solar_eclipse_at_ut(
                    &ctx,
                    split_jd(2460054.65),
                    TAIYIN_ECLIPSE_INCLUDE_CONTACTS,
                    &result,
                    &diag),
                "solve_solar_eclipse_at_ut 2023 hybrid")) {
            return 1;
        }
        if (expect_kind_has(result.kind, TAIYIN_ECLIPSE_HYBRID | TAIYIN_ECLIPSE_CENTRAL, "2023 solar hybrid")) return 1;
    }

    // NASA decade-table greatest-time reference: 2023-04-20 hybrid solar
    // eclipse, greatest at 04:17:55 TD.
    {
        SolarEclipseResult result;
        if (expect_status(
                solve_solar_eclipse_at(
                    &ctx,
                    split_jd(jd(2023, 4, 20)),
                    TAIYIN_ECLIPSE_INCLUDE_CONTACTS,
                    &result,
                    &diag),
                "solve_solar_eclipse_at 2023 NASA hybrid TT")) {
            return 1;
        }
        if (expect_kind_has(result.kind, TAIYIN_ECLIPSE_HYBRID | TAIYIN_ECLIPSE_CENTRAL, "2023 NASA hybrid solar")) return 1;
        if (expect_close_days(result.maximum_jd_tt, tt(2023, 4, 20, 4, 17, 55), 30.0 / 86400.0, "2023 NASA hybrid maximum TT")) return 1;
    }

    // The core radius changes sign along this hybrid route. Every paired
    // sample must therefore retain its geographic north/south identity rather
    // than inheriting one L3/L4 mapping from greatest eclipse.
    {
        size_t hybrid_curve_count = 0;
        if (expect_status(
                compute_solar_eclipse_route_curves_ut(
                    &ctx,
                    split_jd(jd(2023, 4, 20)),
                    0,
                    nullptr,
                    0,
                    &hybrid_curve_count,
                    &diag),
                "count 2023 hybrid route curves")) {
            return 1;
        }
        std::vector<SolarEclipseRouteCurvePoint> hybrid_curves(hybrid_curve_count);
        if (expect_status(
                compute_solar_eclipse_route_curves_ut(
                    &ctx,
                    split_jd(jd(2023, 4, 20)),
                    0,
                    hybrid_curves.data(),
                    hybrid_curves.size(),
                    &hybrid_curve_count,
                    &diag),
                "fill 2023 hybrid route curves")) {
            return 1;
        }
        size_t paired_core_samples = 0;
        for (size_t north_index = 0; north_index < hybrid_curve_count; ++north_index) {
            const SolarEclipseRouteCurvePoint& north = hybrid_curves[north_index];
            if (north.curve_kind != TAIYIN_SOLAR_ROUTE_CURVE_CORE_NORTH) continue;
            const SolarEclipseRouteCurvePoint* south = nullptr;
            for (size_t south_index = 0; south_index < hybrid_curve_count; ++south_index) {
                const SolarEclipseRouteCurvePoint& candidate = hybrid_curves[south_index];
                if (candidate.curve_kind != TAIYIN_SOLAR_ROUTE_CURVE_CORE_SOUTH
                    || std::fabs(candidate.jd_tt - north.jd_tt) > 1.0e-9) {
                    continue;
                }
                south = &candidate;
                break;
            }
            if (!south) continue;
            ++paired_core_samples;
            if (north.latitude_deg + 1.0e-8 < south->latitude_deg) {
                std::printf(
                    "FAIL: 2023 hybrid core limits reversed at JD %.12f north=%.9f south=%.9f\n",
                    scalar_jd(north.jd_tt),
                    north.latitude_deg,
                    south->latitude_deg);
                return 1;
            }
        }
        if (paired_core_samples < 4) {
            return fail("2023 hybrid route needs paired core limits");
        }
    }

    // Full route table sampling around greatest eclipse.
    {
        SolarEclipseRouteRow rows[400];
        size_t count = 0;
        if (expect_status(
                compute_solar_eclipse_route_ut(
                    &ctx,
                    split_jd(2460409.262231433 - 3.0 / 24.0),
                    split_jd(2460409.262231433 + 3.0 / 24.0),
                    1.0,
                    0,
                    rows,
                    sizeof(rows) / sizeof(rows[0]),
                    &count,
                    &diag),
                "compute_solar_eclipse_route_ut 2024")) {
            return 1;
        }
        if (count < 100) {
            std::printf("FAIL: route row count=%zu expected at least 100\n", count);
            return 1;
        }
        bool found_greatest_center = false;
        bool found_penumbral_limits = false;
        for (size_t i = 0; i < count; ++i) {
            if (std::fabs(scalar_jd(rows[i].jd_ut) - 2460409.262231433) < 1.0 / 1440.0
                && std::isfinite(rows[i].center_line.latitude_deg)) {
                found_greatest_center = true;
            }
            if (std::isfinite(rows[i].penumbral_north_limit.latitude_deg)
                && std::isfinite(rows[i].penumbral_south_limit.latitude_deg)) {
                found_penumbral_limits = true;
            }
        }
        if (!found_greatest_center) return fail("sampled route should include greatest center line");
        if (!found_penumbral_limits) return fail("sampled route should include penumbral limits");
    }

    // Route curves derived from Taiyin route rows.
    {
        SolarEclipseRouteCurvePoint points[6000];
        size_t count = 0;
        if (expect_status(
                compute_solar_eclipse_route_curves_ut(
                    &ctx,
                    split_jd(2460409.262231433),
                    0,
                    points,
                    sizeof(points) / sizeof(points[0]),
                    &count,
                    &diag),
                "compute_solar_eclipse_route_curves_ut 2024")) {
            return 1;
        }
        if (count < 500) {
            std::printf("FAIL: route curve point count=%zu expected at least 500\n", count);
            return 1;
        }
        bool has_center = false;
        bool has_penumbra = false;
        bool has_core = false;
        bool has_half_magnitude = false;
        bool closed_curve_kind[TAIYIN_SOLAR_ROUTE_CURVE_HALF_MAGNITUDE_SUNSET_B + 1] = {};
        uint32_t previous_curve_kind = points[0].curve_kind;
        for (size_t i = 0; i < count; ++i) {
            if (!std::isfinite(points[i].latitude_deg) || !std::isfinite(points[i].longitude_deg)) {
                return fail("route curve point should be finite");
            }
            if (points[i].curve_kind > TAIYIN_SOLAR_ROUTE_CURVE_HALF_MAGNITUDE_SUNSET_B) {
                return fail("route curve kind should be known");
            }
            if (i > 0 && points[i].curve_kind != previous_curve_kind) {
                closed_curve_kind[previous_curve_kind] = true;
                if (closed_curve_kind[points[i].curve_kind]) {
                    return fail("route curve points should be grouped by curve kind");
                }
                previous_curve_kind = points[i].curve_kind;
            }
            has_center = has_center || points[i].curve_kind == TAIYIN_SOLAR_ROUTE_CURVE_CENTER_LINE;
            has_penumbra = has_penumbra
                || points[i].curve_kind == TAIYIN_SOLAR_ROUTE_CURVE_PENUMBRAL_NORTH
                || points[i].curve_kind == TAIYIN_SOLAR_ROUTE_CURVE_PENUMBRAL_SOUTH;
            has_core = has_core
                || points[i].curve_kind == TAIYIN_SOLAR_ROUTE_CURVE_CORE_NORTH
                || points[i].curve_kind == TAIYIN_SOLAR_ROUTE_CURVE_CORE_SOUTH;
            has_half_magnitude = has_half_magnitude
                || points[i].curve_kind == TAIYIN_SOLAR_ROUTE_CURVE_HALF_MAGNITUDE_NORTH
                || points[i].curve_kind == TAIYIN_SOLAR_ROUTE_CURVE_HALF_MAGNITUDE_SOUTH;
        }
        if (!has_center) return fail("route curves should include center line");
        if (!has_penumbra) return fail("route curves should include penumbral limits");
        if (!has_core) return fail("route curves should include core limits");
        if (!has_half_magnitude) return fail("route curves should include half-magnitude limits");

        SolarEclipseRouteProductSummary summary;
        size_t polygon_count = 0;
        if (expect_status(
                compute_solar_eclipse_route_product_ut(
                    &ctx,
                    split_jd(2460409.262231433),
                    0,
                    nullptr,
                    0,
                    &polygon_count,
                    &summary,
                    &diag),
                "compute_solar_eclipse_route_product_ut 2024 count")) {
            return 1;
        }
        if (polygon_count < 150) {
            std::printf("FAIL: route product polygon count=%zu expected at least 150\n", polygon_count);
            return 1;
        }
        if (summary.curve_point_count < polygon_count) return fail("route product summary should keep route-row curve count");
        if (summary.center_line_count < 50) return fail("route product center-line count");
        if (summary.core_north_count < 50) return fail("route product core north count");
        if (summary.core_south_count < 50) return fail("route product core south count");
        if (summary.core_begin_horizon_count < 2 || summary.core_end_horizon_count < 2) {
            return fail("smooth route product should include core horizon closures");
        }
        if (summary.core_polygon_point_count != polygon_count) return fail("route product polygon count summary");
        if ((summary.flags & TAIYIN_SOLAR_ROUTE_PRODUCT_HAS_CORE_POLYGON) == 0u) {
            return fail("route product should mark core polygon");
        }
        if ((summary.flags & TAIYIN_SOLAR_ROUTE_PRODUCT_CROSSES_ANTIMERIDIAN) != 0u) {
            return fail("2024 core route product should not cross antimeridian");
        }
        if (!std::isfinite(summary.min_latitude_deg)
            || !std::isfinite(summary.max_latitude_deg)
            || summary.min_latitude_deg >= summary.max_latitude_deg) {
            return fail("route product summary should include latitude bounds");
        }

        SolarEclipseRouteProductPoint too_small[8];
        size_t too_small_count = 0;
        const Status too_small_status = compute_solar_eclipse_route_product_ut(
            &ctx,
            split_jd(2460409.262231433),
            0,
            too_small,
            sizeof(too_small) / sizeof(too_small[0]),
            &too_small_count,
            &summary,
            &diag);
        if (too_small_status != TAIYIN_ERROR_OUT_OF_MEMORY || too_small_count != polygon_count) {
            std::printf(
                "FAIL: route product small buffer status=%d count=%zu expected status=%d count=%zu\n",
                too_small_status,
                too_small_count,
                TAIYIN_ERROR_OUT_OF_MEMORY,
                polygon_count);
            return 1;
        }
        if (summary.core_polygon_point_count != polygon_count
            || (summary.flags & TAIYIN_SOLAR_ROUTE_PRODUCT_HAS_CORE_POLYGON) == 0u
            || !std::isfinite(summary.min_latitude_deg)
            || !std::isfinite(summary.max_latitude_deg)) {
            return fail("route product small buffer should keep complete summary");
        }

        std::vector<SolarEclipseRouteProductPoint> polygon(polygon_count);
        size_t filled_polygon_count = 0;
        if (expect_status(
                compute_solar_eclipse_route_product_ut(
                    &ctx,
                    split_jd(2460409.262231433),
                    0,
                    polygon.data(),
                    polygon.size(),
                    &filled_polygon_count,
                    &summary,
                    &diag),
                "compute_solar_eclipse_route_product_ut 2024 polygon")) {
            return 1;
        }
        if (filled_polygon_count != polygon_count) return fail("route product filled polygon count");
        if (polygon[0].point_kind != TAIYIN_SOLAR_ROUTE_PRODUCT_POINT_CORE_NORTH) {
            return fail("route product first point should be core north");
        }
        if (polygon[summary.core_north_count].point_kind
            != TAIYIN_SOLAR_ROUTE_PRODUCT_POINT_CORE_END_HORIZON) {
            return fail("route product should append the core end horizon after north");
        }
        const size_t south_start = summary.core_north_count + summary.core_end_horizon_count;
        if (polygon[south_start].point_kind != TAIYIN_SOLAR_ROUTE_PRODUCT_POINT_CORE_SOUTH) {
            return fail("route product should append the reversed south path after the end horizon");
        }
        const size_t begin_start = south_start + summary.core_south_count;
        if (polygon[begin_start].point_kind
            != TAIYIN_SOLAR_ROUTE_PRODUCT_POINT_CORE_BEGIN_HORIZON) {
            return fail("route product should append the core begin horizon after south");
        }
        if (polygon[filled_polygon_count - 2].point_kind
            != TAIYIN_SOLAR_ROUTE_PRODUCT_POINT_CORE_BEGIN_HORIZON) {
            return fail("route product should finish the core begin horizon before closing");
        }
        if (polygon[filled_polygon_count - 1].point_kind != TAIYIN_SOLAR_ROUTE_PRODUCT_POINT_POLYGON_CLOSE) {
            return fail("route product last point should close polygon");
        }
        if (expect_close_value(
                polygon[filled_polygon_count - 1].latitude_deg,
                polygon[0].latitude_deg,
                1e-12,
                "route product polygon close latitude")) return 1;
        if (expect_close_value(
                polygon[filled_polygon_count - 1].longitude_deg,
                polygon[0].longitude_deg,
                1e-12,
                "route product polygon close longitude")) return 1;

        SolarEclipseRouteProductSummary map_summary;
        size_t map_polygon_count = 0;
        if (expect_status(
                compute_solar_eclipse_route_map_product_ut(
                    &ctx,
                    split_jd(2460409.262231433),
                    0,
                    nullptr,
                    0,
                    &map_polygon_count,
                    &map_summary,
                    &diag),
                "compute_solar_eclipse_route_map_product_ut 2024 count")) {
            return 1;
        }
        const size_t expected_map_polygon_count =
            map_summary.core_polygon_point_count
            + map_summary.penumbral_polygon_point_count
            + map_summary.half_magnitude_polygon_point_count;
        if (map_polygon_count != expected_map_polygon_count || map_polygon_count <= polygon_count) {
            return fail("route map product should include multiple polygons");
        }
        if ((map_summary.flags & TAIYIN_SOLAR_ROUTE_PRODUCT_HAS_CORE_POLYGON) == 0u
            || (map_summary.flags & TAIYIN_SOLAR_ROUTE_PRODUCT_HAS_PENUMBRAL_POLYGON) == 0u
            || (map_summary.flags & TAIYIN_SOLAR_ROUTE_PRODUCT_HAS_HALF_MAGNITUDE_POLYGON) == 0u) {
            return fail("route map product should mark all polygon layers");
        }
        SolarEclipseRouteProductPoint too_small_map[8];
        size_t too_small_map_count = 0;
        const Status too_small_map_status = compute_solar_eclipse_route_map_product_ut(
            &ctx,
            split_jd(2460409.262231433),
            0,
            too_small_map,
            sizeof(too_small_map) / sizeof(too_small_map[0]),
            &too_small_map_count,
            &map_summary,
            &diag);
        if (too_small_map_status != TAIYIN_ERROR_OUT_OF_MEMORY
            || too_small_map_count != map_polygon_count
            || map_summary.polygon_point_count != map_polygon_count
            || !std::isfinite(map_summary.min_latitude_deg)
            || !std::isfinite(map_summary.max_latitude_deg)) {
            return fail("route map product small buffer should keep complete summary");
        }
        std::vector<SolarEclipseRouteProductPoint> map_points(map_polygon_count);
        size_t filled_map_polygon_count = 0;
        if (expect_status(
                compute_solar_eclipse_route_map_product_ut(
                    &ctx,
                    split_jd(2460409.262231433),
                    0,
                    map_points.data(),
                    map_points.size(),
                    &filled_map_polygon_count,
                    &map_summary,
                    &diag),
                "compute_solar_eclipse_route_map_product_ut 2024 polygons")) {
            return 1;
        }
        if (filled_map_polygon_count != map_polygon_count) return fail("route map product filled polygon count");
        if (map_points[0].point_kind != TAIYIN_SOLAR_ROUTE_PRODUCT_POINT_CORE_NORTH) {
            return fail("route map product should start with core polygon");
        }
        if (map_points[map_summary.core_polygon_point_count].point_kind
            != TAIYIN_SOLAR_ROUTE_PRODUCT_POINT_PENUMBRAL_NORTH) {
            return fail("route map product should append penumbral polygon after core polygon");
        }
        const size_t half_start =
            map_summary.core_polygon_point_count + map_summary.penumbral_polygon_point_count;
        if (map_points[half_start].point_kind != TAIYIN_SOLAR_ROUTE_PRODUCT_POINT_HALF_MAGNITUDE_NORTH) {
            return fail("route map product should append half-magnitude polygon");
        }
        double core_min_jd = INFINITY;
        double core_max_jd = -INFINITY;
        double penumbral_min_jd = INFINITY;
        double penumbral_max_jd = -INFINITY;
        double half_magnitude_min_jd = INFINITY;
        double half_magnitude_max_jd = -INFINITY;
        for (size_t i = 0; i < filled_map_polygon_count; ++i) {
            if (map_points[i].point_kind == TAIYIN_SOLAR_ROUTE_PRODUCT_POINT_CORE_NORTH
                || map_points[i].point_kind == TAIYIN_SOLAR_ROUTE_PRODUCT_POINT_CORE_SOUTH
                || map_points[i].point_kind == TAIYIN_SOLAR_ROUTE_PRODUCT_POINT_CORE_BEGIN_HORIZON
                || map_points[i].point_kind == TAIYIN_SOLAR_ROUTE_PRODUCT_POINT_CORE_END_HORIZON) {
                core_min_jd = std::min(core_min_jd, scalar_jd(map_points[i].jd_ut));
                core_max_jd = std::max(core_max_jd, scalar_jd(map_points[i].jd_ut));
            } else if (map_points[i].point_kind == TAIYIN_SOLAR_ROUTE_PRODUCT_POINT_PENUMBRAL_NORTH
                       || map_points[i].point_kind == TAIYIN_SOLAR_ROUTE_PRODUCT_POINT_PENUMBRAL_SOUTH) {
                penumbral_min_jd = std::min(
                    penumbral_min_jd, scalar_jd(map_points[i].jd_ut));
                penumbral_max_jd = std::max(
                    penumbral_max_jd, scalar_jd(map_points[i].jd_ut));
            } else if (map_points[i].point_kind == TAIYIN_SOLAR_ROUTE_PRODUCT_POINT_HALF_MAGNITUDE_NORTH
                       || map_points[i].point_kind == TAIYIN_SOLAR_ROUTE_PRODUCT_POINT_HALF_MAGNITUDE_SOUTH) {
                half_magnitude_min_jd = std::min(
                    half_magnitude_min_jd, scalar_jd(map_points[i].jd_ut));
                half_magnitude_max_jd = std::max(
                    half_magnitude_max_jd, scalar_jd(map_points[i].jd_ut));
            }
        }
        if (!(penumbral_min_jd < core_min_jd - 2.0 / 1440.0
              && penumbral_max_jd > core_max_jd + 2.0 / 1440.0)) {
            return fail("route map product should sample penumbral layers over the wider partial span");
        }

        double curve_penumbral_min_jd = INFINITY;
        double curve_penumbral_max_jd = -INFINITY;
        double curve_half_magnitude_min_jd = INFINITY;
        double curve_half_magnitude_max_jd = -INFINITY;
        for (size_t i = 0; i < count; ++i) {
            if (points[i].curve_kind == TAIYIN_SOLAR_ROUTE_CURVE_PENUMBRAL_NORTH
                || points[i].curve_kind == TAIYIN_SOLAR_ROUTE_CURVE_PENUMBRAL_SOUTH
                || points[i].curve_kind == TAIYIN_SOLAR_ROUTE_CURVE_SUNRISE_MAX_A
                || points[i].curve_kind == TAIYIN_SOLAR_ROUTE_CURVE_SUNRISE_MAX_B
                || points[i].curve_kind == TAIYIN_SOLAR_ROUTE_CURVE_SUNSET_MAX_A
                || points[i].curve_kind == TAIYIN_SOLAR_ROUTE_CURVE_SUNSET_MAX_B) {
                curve_penumbral_min_jd = std::min(
                    curve_penumbral_min_jd, scalar_jd(points[i].jd_ut));
                curve_penumbral_max_jd = std::max(
                    curve_penumbral_max_jd, scalar_jd(points[i].jd_ut));
            } else if (points[i].curve_kind == TAIYIN_SOLAR_ROUTE_CURVE_HALF_MAGNITUDE_NORTH
                       || points[i].curve_kind == TAIYIN_SOLAR_ROUTE_CURVE_HALF_MAGNITUDE_SOUTH
                       || points[i].curve_kind == TAIYIN_SOLAR_ROUTE_CURVE_HALF_MAGNITUDE_SUNRISE_A
                       || points[i].curve_kind == TAIYIN_SOLAR_ROUTE_CURVE_HALF_MAGNITUDE_SUNRISE_B
                       || points[i].curve_kind == TAIYIN_SOLAR_ROUTE_CURVE_HALF_MAGNITUDE_SUNSET_A
                       || points[i].curve_kind == TAIYIN_SOLAR_ROUTE_CURVE_HALF_MAGNITUDE_SUNSET_B) {
                curve_half_magnitude_min_jd = std::min(
                    curve_half_magnitude_min_jd, scalar_jd(points[i].jd_ut));
                curve_half_magnitude_max_jd = std::max(
                    curve_half_magnitude_max_jd, scalar_jd(points[i].jd_ut));
            }
        }
        if (curve_penumbral_min_jd > penumbral_min_jd
            || curve_penumbral_max_jd < penumbral_max_jd) {
            return fail("public penumbral curves should cover the exported penumbral polygon span");
        }
        if (curve_half_magnitude_min_jd > half_magnitude_min_jd
            || curve_half_magnitude_max_jd < half_magnitude_max_jd) {
            return fail("public half-magnitude curves should cover the exported half-magnitude polygon span");
        }
    }

    // A noncentral partial eclipse has one physical penumbral limit. The other
    // edge of its complete visibility region is horizon/contact geometry, so
    // the route API must expose the real curve without fabricating a polygon.
    {
        struct PartialRouteCase {
            double maximum_jd_ut;
            uint32_t penumbral_kind;
            uint32_t half_magnitude_kind;
            bool expect_half_magnitude;
            const char* label;
        };
        const PartialRouteCase cases[] = {
            {
                2451727.3142343629,
                TAIYIN_SOLAR_ROUTE_CURVE_PENUMBRAL_NORTH,
                TAIYIN_SOLAR_ROUTE_CURVE_HALF_MAGNITUDE_NORTH,
                false,
                "2000-07-01 shallow partial"
            },
            {
                2460763.949617004,
                TAIYIN_SOLAR_ROUTE_CURVE_PENUMBRAL_SOUTH,
                TAIYIN_SOLAR_ROUTE_CURVE_HALF_MAGNITUDE_SOUTH,
                true,
                "2025-03-29 partial"
            },
            {
                2460940.3207763173,
                TAIYIN_SOLAR_ROUTE_CURVE_PENUMBRAL_NORTH,
                TAIYIN_SOLAR_ROUTE_CURVE_HALF_MAGNITUDE_NORTH,
                true,
                "2025-09-21 partial"
            },
        };
        for (size_t case_index = 0; case_index < sizeof(cases) / sizeof(cases[0]); ++case_index) {
            size_t count = 0;
            if (expect_status(
                    compute_solar_eclipse_route_curves_ut(
                        &ctx,
                        split_jd(cases[case_index].maximum_jd_ut),
                        0,
                        nullptr,
                        0,
                        &count,
                        &diag),
                    cases[case_index].label)) {
                return 1;
            }
            std::vector<SolarEclipseRouteCurvePoint> points(count);
            if (expect_status(
                    compute_solar_eclipse_route_curves_ut(
                        &ctx,
                        split_jd(cases[case_index].maximum_jd_ut),
                        0,
                        points.data(),
                        points.size(),
                        &count,
                        &diag),
                    cases[case_index].label)) {
                return 1;
            }
            points.resize(count);
            if (count == 0) return fail("partial route should expose a one-sided limit");

            size_t penumbral_count = 0;
            size_t opposite_penumbral_count = 0;
            size_t half_magnitude_count = 0;
            size_t opposite_half_magnitude_count = 0;
            size_t center_or_core_count = 0;
            const uint32_t opposite_penumbral_kind =
                cases[case_index].penumbral_kind == TAIYIN_SOLAR_ROUTE_CURVE_PENUMBRAL_NORTH
                    ? TAIYIN_SOLAR_ROUTE_CURVE_PENUMBRAL_SOUTH
                    : TAIYIN_SOLAR_ROUTE_CURVE_PENUMBRAL_NORTH;
            const uint32_t opposite_half_magnitude_kind =
                cases[case_index].half_magnitude_kind == TAIYIN_SOLAR_ROUTE_CURVE_HALF_MAGNITUDE_NORTH
                    ? TAIYIN_SOLAR_ROUTE_CURVE_HALF_MAGNITUDE_SOUTH
                    : TAIYIN_SOLAR_ROUTE_CURVE_HALF_MAGNITUDE_NORTH;
            for (size_t i = 0; i < count; ++i) {
                if (points[i].curve_kind == cases[case_index].penumbral_kind) ++penumbral_count;
                if (points[i].curve_kind == opposite_penumbral_kind) ++opposite_penumbral_count;
                if (points[i].curve_kind == cases[case_index].half_magnitude_kind) ++half_magnitude_count;
                if (points[i].curve_kind == opposite_half_magnitude_kind) {
                    ++opposite_half_magnitude_count;
                }
                if (points[i].curve_kind == TAIYIN_SOLAR_ROUTE_CURVE_CENTER_LINE
                    || points[i].curve_kind == TAIYIN_SOLAR_ROUTE_CURVE_CORE_NORTH
                    || points[i].curve_kind == TAIYIN_SOLAR_ROUTE_CURVE_CORE_SOUTH) {
                    ++center_or_core_count;
                }
            }
            if (penumbral_count == 0 || opposite_penumbral_count != 0
                || opposite_half_magnitude_count != 0
                || (half_magnitude_count > 0) != cases[case_index].expect_half_magnitude
                || center_or_core_count != 0) {
                return fail("partial route should contain only its physical one-sided wide limits");
            }

            SolarEclipseRouteProductSummary route_summary;
            size_t route_polygon_count = 0;
            if (expect_status(
                    compute_solar_eclipse_route_product_ut(
                        &ctx,
                        split_jd(cases[case_index].maximum_jd_ut),
                        0,
                        nullptr,
                        0,
                        &route_polygon_count,
                        &route_summary,
                        &diag),
                    cases[case_index].label)) {
                return 1;
            }
            if (route_polygon_count != 0
                || route_summary.core_polygon_point_count != 0
                || route_summary.polygon_point_count != 0
                || (route_summary.flags & TAIYIN_SOLAR_ROUTE_PRODUCT_HAS_CORE_POLYGON) != 0u) {
                return fail("partial route product must not synthesize a core polygon");
            }

            SolarEclipseRouteProductSummary summary;
            size_t polygon_count = 0;
            if (expect_status(
                    compute_solar_eclipse_route_map_product_ut(
                        &ctx,
                        split_jd(cases[case_index].maximum_jd_ut),
                        0,
                        nullptr,
                        0,
                        &polygon_count,
                        &summary,
                        &diag),
                    cases[case_index].label)) {
                return 1;
            }
            if (polygon_count == 0
                || summary.polygon_point_count != polygon_count
                || summary.core_polygon_point_count != 0
                || summary.penumbral_polygon_point_count == 0
                || (summary.flags & TAIYIN_SOLAR_ROUTE_PRODUCT_HAS_PENUMBRAL_POLYGON) == 0u
                || (summary.half_magnitude_polygon_point_count > 0)
                    != cases[case_index].expect_half_magnitude) {
                return fail("partial route should close wide layers with sunrise/sunset boundaries");
            }
            if (summary.curve_point_count == 0
                || (summary.penumbral_north_count == 0) == (summary.penumbral_south_count == 0)) {
                return fail("partial route summary should retain exactly one penumbral limit");
            }
        }
    }

    // Besselian polynomial foundation for local/path solar eclipse work.
    {
        SolarBesselianElements elements;
        const double center_tt = 2460409.262231433 + 69.0 / 86400.0;
        if (expect_status(
                compute_solar_besselian_elements_tt(
                    &ctx, split_jd(center_tt), 0.0, &elements, &diag),
                "compute_solar_besselian_elements_tt 2024")) {
            return 1;
        }
        // Taiyin-derived Besselian snapshots were repinned after split-JD
        // evaluation-order changes; independent external oracles remain unchanged.
        if (expect_close_value(elements.x, 0.158222777765101, 1e-9, "besselian x")) return 1;
        if (expect_close_value(elements.y, 0.304493849301692, 1e-9, "besselian y")) return 1;
        if (expect_close_value(elements.zeta, 56.410877306293, 1e-8, "besselian zeta")) return 1;
        if (expect_close_value(elements.d_deg, -7.590825680172, 1e-9, "besselian d")) return 1;
        if (expect_close_value(elements.mu_deg, 273.994309591481, 1e-8, "besselian mu")) return 1;
        if (expect_close_value(elements.l1, 0.535736741366, 1e-9, "besselian l1")) return 1;
        if (expect_close_value(elements.l2, 0.010590415175, 1e-9, "besselian l2")) return 1;

        SolarBesselianPolynomial polynomial;
        if (expect_status(
                compute_solar_besselian_polynomial_tt(
                    &ctx, split_jd(center_tt), 6.0, 1.0, 4, &polynomial, &diag),
                "compute_solar_besselian_polynomial_tt 2024")) {
            return 1;
        }
        SolarBesselianElements evaluated;
        if (expect_status(
                evaluate_solar_besselian_polynomial(&polynomial, 0.0, &evaluated),
                "evaluate_solar_besselian_polynomial 2024")) {
            return 1;
        }
        if (expect_close_value(evaluated.x, elements.x, 1e-8, "besselian polynomial center x")) return 1;
        if (expect_close_value(evaluated.y, elements.y, 1e-8, "besselian polynomial center y")) return 1;
        if (!(polynomial.max_residual.x < 1e-7 && polynomial.max_residual.y < 1e-7)) {
            return fail("besselian polynomial residual should be small");
        }
    }

    // Local solar eclipse: 2024-04-08 total at Mazatlan (near center line)
    {
        taiyin::runtime::NativeCalcContext local_ctx =
            make_context_with_observer(-106.4, 23.2, 0.0);
        LocalSolarEclipseResultUt result;
        if (expect_status(
                solve_local_solar_eclipse_at_ut(
                    &local_ctx, split_jd(2460409.262231433), 0, &result, &diag),
                "solve_local_solar_eclipse_at_ut Mazatlan")) {
            return 1;
        }
        if (expect_kind_has(result.kind, TAIYIN_ECLIPSE_TOTAL, "Mazatlan total")) return 1;
        if (result.magnitude < 1.0) return fail("Mazatlan magnitude should be >= 1 for total");
        if (expect_close_value(result.obscuration, 1.0, 1e-6, "Mazatlan obscuration")) return 1;
        if (expect_close_days(result.maximum_jd_ut,
                              2460409.256654905155, 10.0 / 86400.0, "Mazatlan maximum")) return 1;
        if (expect_close_value(result.magnitude, 1.057846292, 1e-6, "Mazatlan magnitude")) return 1;
        if (expect_close_days(result.contact_jd_ut[TAIYIN_LOCAL_SOLAR_CONTACT_C1],
                              2460409.202312638052, 10.0 / 86400.0, "Mazatlan C1")) return 1;
        if (expect_close_days(result.contact_jd_ut[TAIYIN_LOCAL_SOLAR_CONTACT_C2],
                              2460409.255112255923, 10.0 / 86400.0, "Mazatlan C2")) return 1;
        if (expect_close_days(result.contact_jd_ut[TAIYIN_LOCAL_SOLAR_CONTACT_C3],
                              2460409.258199470583, 10.0 / 86400.0, "Mazatlan C3")) return 1;
        if (expect_close_days(result.contact_jd_ut[TAIYIN_LOCAL_SOLAR_CONTACT_C4],
                              2460409.313970566727, 10.0 / 86400.0, "Mazatlan C4")) return 1;
        if (expect_close_value(result.duration_seconds, 266.735347, 1.0, "Mazatlan duration")) return 1;
        if (!std::isfinite(result.position_angle_c1_deg)
            || !std::isfinite(result.position_angle_c4_deg)
            || !std::isfinite(result.vertex_angle_c1_deg)
            || !std::isfinite(result.vertex_angle_c4_deg)) {
            return fail("Mazatlan contact angles should be finite");
        }
    }

    // NASA Science city table reference: Dallas 2024 total eclipse.  The source
    // rounds to minutes, so this is only a coarse local-circumstances sanity
    // check, not a second-level local oracle.
    {
        taiyin::runtime::NativeCalcContext local_ctx =
            make_context_with_observer(-96.7970, 32.7767, 131.0);
        LocalSolarEclipseResultUt result;
        if (expect_status(
                solve_local_solar_eclipse_at_ut(
                    &local_ctx, split_jd(2460409.279629630), 0, &result, &diag),
                "solve_local_solar_eclipse_at_ut Dallas NASA city table")) {
            return 1;
        }
        if (expect_kind_has(result.kind, TAIYIN_ECLIPSE_TOTAL, "Dallas NASA total")) return 1;
        if (expect_close_days(result.contact_jd_ut[TAIYIN_LOCAL_SOLAR_CONTACT_C1],
                              tt(2024, 4, 8, 17, 23, 0), 60.0 / 86400.0, "Dallas NASA C1")) return 1;
        if (expect_close_days(result.contact_jd_ut[TAIYIN_LOCAL_SOLAR_CONTACT_C2],
                              tt(2024, 4, 8, 18, 40, 0), 60.0 / 86400.0, "Dallas NASA C2")) return 1;
        if (expect_close_days(result.maximum_jd_ut,
                              tt(2024, 4, 8, 18, 42, 0), 60.0 / 86400.0, "Dallas NASA maximum")) return 1;
        if (expect_close_days(result.contact_jd_ut[TAIYIN_LOCAL_SOLAR_CONTACT_C3],
                              tt(2024, 4, 8, 18, 44, 0), 60.0 / 86400.0, "Dallas NASA C3")) return 1;
        if (expect_close_days(result.contact_jd_ut[TAIYIN_LOCAL_SOLAR_CONTACT_C4],
                              tt(2024, 4, 8, 20, 2, 0), 60.0 / 86400.0, "Dallas NASA C4")) return 1;
        if (expect_close_value(result.obscuration, 1.0, 1e-6, "Dallas NASA obscuration")) return 1;
    }

    // Local solar eclipse: 2024-04-08 partial at New York (outside totality path)
    {
        taiyin::runtime::NativeCalcContext local_ctx =
            make_context_with_observer(-74.0, 40.7, 0.0);
        LocalSolarEclipseResultUt result;
        if (expect_status(
                solve_local_solar_eclipse_at_ut(
                    &local_ctx, split_jd(2460409.262231433), 0, &result, &diag),
                "solve_local_solar_eclipse_at_ut NYC")) {
            return 1;
        }
        if (expect_kind_has(result.kind, TAIYIN_ECLIPSE_PARTIAL, "NYC partial")) return 1;
        if (result.magnitude <= 0.0 || result.magnitude >= 1.0) return fail("NYC magnitude should be 0-1 for partial");
        if (expect_close_days(result.maximum_jd_ut,
                              2460409.309436735231, 10.0 / 86400.0, "NYC maximum")) return 1;
        if (expect_close_value(result.magnitude, 0.910609821, 1e-6, "NYC magnitude")) return 1;
        if (expect_close_value(result.obscuration, 0.899247745, 1e-6, "NYC obscuration")) return 1;
        if (expect_nan(result.contact_jd_ut[TAIYIN_LOCAL_SOLAR_CONTACT_C2], "NYC C2 should be NAN")) return 1;
        if (expect_nan(result.contact_jd_ut[TAIYIN_LOCAL_SOLAR_CONTACT_C3], "NYC C3 should be NAN")) return 1;
        if (expect_close_days(result.contact_jd_ut[TAIYIN_LOCAL_SOLAR_CONTACT_C1],
                              2460409.257439830340, 10.0 / 86400.0, "NYC C1")) return 1;
        if (expect_close_days(result.contact_jd_ut[TAIYIN_LOCAL_SOLAR_CONTACT_C4],
                              2460409.358684573323, 10.0 / 86400.0, "NYC C4")) return 1;
        if (expect_close_value(result.duration_seconds, 0.0, 1e-9, "NYC duration")) return 1;
    }

    // Local boundary computation uses the production shadow/ellipsoid path.
    {
        LocalSolarEclipseBoundary boundary;
        if (expect_status(
                compute_local_solar_eclipse_boundary_ut(
                    &ctx,
                    split_jd(2460409.262231433),
                    -106.4,
                    23.2,
                    &boundary,
                    &diag),
                "compute_local_solar_eclipse_boundary_ut 2024")) {
            return 1;
        }
        if (expect_kind_has(boundary.center_kind, TAIYIN_ECLIPSE_TOTAL, "boundary center total")) return 1;
        if (!std::isfinite(boundary.center_longitude_deg) || !std::isfinite(boundary.center_latitude_deg)) {
            return fail("boundary center should be finite");
        }
        if (!std::isfinite(boundary.umbra_north_longitude_deg) || !std::isfinite(boundary.umbra_south_longitude_deg)) {
            return fail("boundary umbra limits should be finite");
        }
        if (!std::isfinite(boundary.penumbra_north_longitude_deg) || !std::isfinite(boundary.penumbra_south_longitude_deg)) {
            return fail("boundary penumbra limits should be finite");
        }
        if (!(boundary.umbra_width_km > 0.0)) return fail("boundary umbra width should be positive");
    }

    // Search wrappers should find total, annular, and partial global solar eclipses.
    {
        SolarEclipseResultUt next;
        if (expect_status(
                search_next_solar_eclipse_ut(
                    &ctx, split_jd(2460400.0), 0, 0, &next, &diag),
                "search_next_solar_eclipse_ut 2024")) {
            return 1;
        }
        if (expect_kind_has(next.kind, TAIYIN_ECLIPSE_TOTAL | TAIYIN_ECLIPSE_CENTRAL, "next solar total")) return 1;
        if (expect_close_days(next.maximum_jd_ut, 2460409.262039739, 2.0 / 86400.0, "next solar maximum UT")) return 1;

        SolarEclipseResultUt previous;
        if (expect_status(
                search_next_solar_eclipse_ut(
                    &ctx,
                    split_jd(2460410.0),
                    0,
                    TAIYIN_ECLIPSE_BACKWARD,
                    &previous,
                    &diag),
                "search_previous_solar_eclipse_ut 2024")) {
            return 1;
        }
        if (expect_kind_has(previous.kind, TAIYIN_ECLIPSE_TOTAL | TAIYIN_ECLIPSE_CENTRAL, "previous solar total")) return 1;
        if (expect_close_days(previous.maximum_jd_ut, 2460409.262039739, 2.0 / 86400.0, "previous solar maximum UT")) return 1;

        SolarEclipseResultUt results[10];
        size_t count = 0;
        if (expect_status(
                search_solar_eclipses_ut(
                    &ctx,
                    split_jd(2460300.0),
                    split_jd(2460800.0),
                    0,
                    0,
                    results,
                    10,
                    &count,
                    &diag),
                "search_solar_eclipses_ut 2024-2025")) {
            return 1;
        }
        if (count != 3) {
            std::printf("FAIL: search_solar_eclipses_ut count=%zu expected=3\n", count);
            return 1;
        }
        if (expect_kind_has(results[0].kind, TAIYIN_ECLIPSE_TOTAL | TAIYIN_ECLIPSE_CENTRAL, "range solar[0] total")) return 1;
        if (expect_kind_has(results[1].kind, TAIYIN_ECLIPSE_ANNULAR | TAIYIN_ECLIPSE_CENTRAL, "range solar[1] annular")) return 1;
        if (expect_kind_has(results[2].kind, TAIYIN_ECLIPSE_PARTIAL | TAIYIN_ECLIPSE_NONCENTRAL, "range solar[2] partial")) return 1;
        if (expect_close_days(results[1].maximum_jd_ut, 2460586.281297341, 2.0 / 86400.0, "2024 annular maximum UT")) return 1;
        if (expect_close_days(results[2].maximum_jd_ut, 2460763.949617004, 2.0 / 86400.0, "2025 partial maximum UT")) return 1;

        // A central-only search skips the two partial eclipses in 2025.  The
        // accepted event must still be fully completed after the early filter.
        SolarEclipseResultUt central;
        if (expect_status(
                search_next_solar_eclipse_ut(
                    &ctx,
                    split_jd(jd(2025, 1, 1)),
                    TAIYIN_ECLIPSE_TOTAL
                        | TAIYIN_ECLIPSE_ANNULAR
                        | TAIYIN_ECLIPSE_HYBRID,
                    flags,
                    &central,
                    &diag),
                "search_next_solar_eclipse_ut central filter")) {
            return 1;
        }
        if (expect_kind_has(central.kind, TAIYIN_ECLIPSE_CENTRAL, "central-filter solar")) return 1;
        const size_t central_contacts[] = {
            TAIYIN_SOLAR_ECLIPSE_CONTACT_P1,
            TAIYIN_SOLAR_ECLIPSE_CONTACT_C1,
            TAIYIN_SOLAR_ECLIPSE_CONTACT_C4,
            TAIYIN_SOLAR_ECLIPSE_CONTACT_P4,
        };
        for (size_t contact : central_contacts) {
            if (!split_julian_date_is_finite(central.contact_jd_ut[contact])) {
                return fail("central-filter solar contacts should be finite");
            }
        }

        // Hybrid is known only after the central kind refinement.  A
        // hybrid-only filter must not reject its preliminary total/annular
        // classification before that refinement runs.
        SolarEclipseResultUt hybrid;
        if (expect_status(
                search_next_solar_eclipse_ut(
                    &ctx,
                    split_jd(jd(2023, 1, 1)),
                    TAIYIN_ECLIPSE_HYBRID,
                    flags,
                    &hybrid,
                    &diag),
                "search_next_solar_eclipse_ut hybrid filter")) {
            return 1;
        }
        if (expect_kind_has(hybrid.kind, TAIYIN_ECLIPSE_HYBRID, "hybrid-filter solar")) return 1;
        if (!split_julian_date_is_finite(
                hybrid.contact_jd_ut[TAIYIN_SOLAR_ECLIPSE_CONTACT_P1])
            || !split_julian_date_is_finite(
                hybrid.contact_jd_ut[TAIYIN_SOLAR_ECLIPSE_CONTACT_P4])) {
            return fail("hybrid-filter solar contacts should be finite");
        }
    }

    // Local search wrappers must filter by the observer-local eclipse kind, not
    // only by the global eclipse type.  The 2024-04-08 eclipse is total globally
    // and at Dallas, but only partial in New York.
    {
        taiyin::runtime::NativeCalcContext dallas_ctx =
            make_context_with_observer(-96.7970, 32.7767, 131.0);
        LocalSolarEclipseResultUt dallas;
        if (expect_status(
                search_next_local_solar_eclipse_ut(
                    &dallas_ctx,
                    split_jd(2460400.0),
                    TAIYIN_ECLIPSE_TOTAL,
                    0,
                    &dallas,
                    &diag),
                "search_next_local_solar_eclipse_ut Dallas total")) {
            return 1;
        }
        if (expect_kind_has(dallas.kind, TAIYIN_ECLIPSE_TOTAL | TAIYIN_ECLIPSE_VISIBLE_AT_OBSERVER, "Dallas local total search")) return 1;
        if (expect_close_days(dallas.maximum_jd_ut, 2460409.279765, 120.0 / 86400.0, "Dallas local total search maximum")) return 1;

        // Refraction model validity must be checked before the above-horizon
        // completion shortcut, not only when rise/set evaluation is reached.
        NativeCalcContext invalid_refraction_ctx = dallas_ctx;
        invalid_refraction_ctx.refraction_model_id = 9999;
        LocalSolarEclipseResultUt invalid_refraction;
        const Status invalid_refraction_status = solve_local_solar_eclipse_at_ut(
            &invalid_refraction_ctx,
            dallas.maximum_jd_ut,
            TAIYIN_ECLIPSE_INCLUDE_CONTACTS | TAIYIN_ECLIPSE_LOCAL_REFRACTION,
            &invalid_refraction,
            &diag);
        if (invalid_refraction_status != TAIYIN_ERROR_INVALID_ARGUMENT) {
            std::printf(
                "FAIL: local eclipse accepted unavailable refraction model status %d\n",
                invalid_refraction_status);
            return 1;
        }

        taiyin::runtime::NativeCalcContext dallas_topocentric_ctx = make_context();
        const taiyin::runtime::NativeObserverLocation dallas_location =
            taiyin::runtime::native_observer_location_degrees(-96.7970, 32.7767, 131.0);
        const Status topo_status = taiyin::runtime::native_context_set_simple_topocentric_observer(
            &dallas_topocentric_ctx,
            dallas_location,
            split_jd(2460409.25),
            split_jd(2460409.25));
        if (topo_status != TAIYIN_STATUS_OK) {
            return fail("set Dallas simple topocentric observer");
        }
        LocalSolarEclipseResultUt dallas_topocentric;
        if (expect_status(
                search_next_local_solar_eclipse_ut(
                    &dallas_topocentric_ctx,
                    split_jd(2460400.0),
                    TAIYIN_ECLIPSE_TOTAL,
                    0,
                    &dallas_topocentric,
                    &diag),
                "search_next_local_solar_eclipse_ut Dallas topocentric ctx")) {
            return 1;
        }
        if (expect_kind_has(
                dallas_topocentric.kind,
                TAIYIN_ECLIPSE_TOTAL | TAIYIN_ECLIPSE_VISIBLE_AT_OBSERVER,
                "Dallas topocentric context local total search")) {
            return 1;
        }

        LocalSolarEclipseResultUt dallas_truepos;
        if (expect_status(
                search_next_local_solar_eclipse_ut(
                    &dallas_ctx,
                    split_jd(2460400.0),
                    TAIYIN_ECLIPSE_TOTAL,
                    TAIYIN_ECLIPSE_TRUEPOS,
                    &dallas_truepos,
                    &diag),
                "search_next_local_solar_eclipse_ut Dallas total truepos")) {
            return 1;
        }
        if (expect_kind_has(dallas_truepos.kind, TAIYIN_ECLIPSE_TOTAL | TAIYIN_ECLIPSE_VISIBLE_AT_OBSERVER, "Dallas local total truepos search")) return 1;

        taiyin::runtime::NativeCalcContext new_york_ctx =
            make_context_with_observer(-74.0, 40.7, 0.0);
        LocalSolarEclipseResultUt new_york;
        const Status ny_total_status = search_next_local_solar_eclipse_ut(
            &new_york_ctx,
            split_jd(2460400.0),
            TAIYIN_ECLIPSE_TOTAL,
            0,
            &new_york,
            &diag);
        if (ny_total_status == TAIYIN_STATUS_OK
            && (new_york.kind & TAIYIN_ECLIPSE_TOTAL) == 0) {
            return fail("NYC total-filter local search returned a non-total eclipse");
        }

        LocalSolarEclipseResultUt new_york_partial;
        if (expect_status(
                search_next_local_solar_eclipse_ut(
                    &new_york_ctx,
                    split_jd(2460400.0),
                    TAIYIN_ECLIPSE_PARTIAL,
                    0,
                    &new_york_partial,
                    &diag),
                "search_next_local_solar_eclipse_ut NYC partial")) {
            return 1;
        }
        if (expect_kind_has(new_york_partial.kind, TAIYIN_ECLIPSE_PARTIAL | TAIYIN_ECLIPSE_VISIBLE_AT_OBSERVER, "NYC local partial search")) return 1;
        if (expect_close_days(new_york_partial.maximum_jd_ut, 2460409.309436735231, 120.0 / 86400.0, "NYC local partial search maximum")) return 1;
    }

    // Local search should include eclipses visible at sunrise/sunset even when
    // the instant of maximum is below the local horizon.
    {
        taiyin::runtime::NativeCalcContext horizon_ctx =
            make_context_with_observer(25.0, -25.0, 0.0);
        LocalSolarEclipseResultUt horizon;
        if (expect_status(
                solve_local_solar_eclipse_at_ut(
                    &horizon_ctx,
                    split_jd(2459021.777847197372),
                    TAIYIN_ECLIPSE_INCLUDE_CONTACTS,
                    &horizon,
                    &diag),
                "solve_local_solar_eclipse_at_ut sunrise-visible partial")) {
            return 1;
        }
        if ((horizon.kind & TAIYIN_ECLIPSE_VISIBLE_AT_OBSERVER) != 0) {
            return fail("sunrise-visible sample should have maximum below horizon");
        }
        if (expect_kind_has(horizon.kind, TAIYIN_ECLIPSE_PARTIAL | TAIYIN_ECLIPSE_MAXIMUM_VISIBLE, "sunrise-visible partial")) return 1;
        if (!(horizon.sunrise_magnitude > 0.0)) return fail("sunrise-visible sample should have positive sunrise magnitude");

        LocalSolarEclipseResultUt horizon_search;
        if (expect_status(
                search_next_local_solar_eclipse_ut(
                    &horizon_ctx,
                    split_jd(2459015.0),
                    TAIYIN_ECLIPSE_PARTIAL,
                    0,
                    &horizon_search,
                    &diag),
                "search_next_local_solar_eclipse_ut sunrise-visible partial")) {
            return 1;
        }
        if (expect_kind_has(horizon_search.kind, TAIYIN_ECLIPSE_PARTIAL | TAIYIN_ECLIPSE_MAXIMUM_VISIBLE, "sunrise-visible local search")) return 1;
        if ((horizon_search.kind & TAIYIN_ECLIPSE_VISIBLE_AT_OBSERVER) != 0) {
            return fail("sunrise-visible search sample should not require maximum above horizon");
        }
        if (!(horizon_search.sunrise_magnitude > 0.0)) return fail("sunrise-visible search should preserve sunrise magnitude");
        if (expect_close_days(horizon_search.maximum_jd_ut, 2459021.697915830, 120.0 / 86400.0, "sunrise-visible local search maximum")) return 1;
    }

    // Local eclipse visibility-mode flag mapping: default is geometric
    // (reproducible), LOCAL_REFRACTION only shifts the visibility window,
    // strict-without-refraction is invalid.
    {
        const double eclipse_jd_ut = 2459021.777847197372;
        taiyin::runtime::NativeCalcContext vis_ctx =
            make_context_with_observer(25.0, -25.0, 0.0);
        LocalSolarEclipseResultUt geometric;
        if (expect_status(
                solve_local_solar_eclipse_at_ut(
                    &vis_ctx,
                    split_jd(eclipse_jd_ut),
                    TAIYIN_ECLIPSE_INCLUDE_CONTACTS,
                    &geometric,
                    &diag),
                "local eclipse default geometric mode")) {
            return 1;
        }
        LocalSolarEclipseResultUt apparent;
        if (expect_status(
                solve_local_solar_eclipse_at_ut(
                    &vis_ctx,
                    split_jd(eclipse_jd_ut),
                    TAIYIN_ECLIPSE_INCLUDE_CONTACTS | TAIYIN_ECLIPSE_LOCAL_REFRACTION,
                    &apparent,
                    &diag),
                "local eclipse apparent refraction mode")) {
            return 1;
        }
        // The refraction shift applies only to the visibility window: the
        // instant of maximum and the topocentric sun position never change.
        // magnitude/obscuration and the sunrise/sunset magnitudes are window
        // values, so they may change with the window.
        if (expect_close_days(apparent.maximum_jd_ut, geometric.maximum_jd_ut, 0.0, "local refraction keeps maximum instant")) return 1;
        if (expect_close_value(apparent.sun_altitude_deg, geometric.sun_altitude_deg, 0.0, "local refraction keeps sun altitude")) return 1;
        if (expect_close_value(apparent.sun_azimuth_deg, geometric.sun_azimuth_deg, 0.0, "local refraction keeps sun azimuth")) return 1;

        // Refraction at the sunrise/sunset threshold only ever widens the
        // visible window: apparent magnitudes at the shifted geometric-horizon
        // instants must be non-negative, and apparent must not lose visibility.
        if ((geometric.kind & TAIYIN_ECLIPSE_MAXIMUM_VISIBLE) != 0
            && (apparent.kind & TAIYIN_ECLIPSE_MAXIMUM_VISIBLE) == 0) {
            return fail("apparent mode lost visibility that geometric mode had");
        }

        // Strict meteorology without refraction is invalid on both entry points.
        LocalSolarEclipseResultUt strict_only;
        const Status strict_solve = solve_local_solar_eclipse_at_ut(
            &vis_ctx,
            split_jd(eclipse_jd_ut),
            TAIYIN_ECLIPSE_INCLUDE_CONTACTS | TAIYIN_ECLIPSE_LOCAL_STRICT_METEOROLOGY,
            &strict_only,
            &diag);
        if (strict_solve != TAIYIN_ERROR_INVALID_ARGUMENT) {
            std::printf("FAIL: strict-without-refraction solve returned status %d\n", strict_solve);
            return 1;
        }
        const Status strict_search = search_next_local_solar_eclipse_ut(
            &vis_ctx,
            split_jd(2459015.0),
            TAIYIN_ECLIPSE_PARTIAL,
            TAIYIN_ECLIPSE_LOCAL_STRICT_METEOROLOGY,
            &strict_only,
            &diag);
        if (strict_search != TAIYIN_ERROR_INVALID_ARGUMENT) {
            std::printf("FAIL: strict-without-refraction search returned status %d\n", strict_search);
            return 1;
        }

        // Local-only visibility bits remain invalid on global eclipse APIs.
        SolarEclipseResult global_solar;
        const Status global_solar_status = solve_solar_eclipse_at(
            &vis_ctx,
            split_jd(2460409.0),
            TAIYIN_ECLIPSE_LOCAL_REFRACTION,
            &global_solar,
            &diag);
        if (global_solar_status != TAIYIN_ERROR_INVALID_ARGUMENT) {
            std::printf("FAIL: global solar API accepted local refraction status %d\n", global_solar_status);
            return 1;
        }
        LunarEclipseResult global_lunar;
        const Status global_lunar_status = solve_lunar_eclipse_at(
            &vis_ctx,
            split_jd(2460409.0),
            TAIYIN_ECLIPSE_LOCAL_REFRACTION,
            &global_lunar,
            &diag);
        if (global_lunar_status != TAIYIN_ERROR_INVALID_ARGUMENT) {
            std::printf("FAIL: global lunar API accepted local refraction status %d\n", global_lunar_status);
            return 1;
        }

        // A requested apparent window must propagate missing-atmosphere errors.
        NativeCalcContext no_atmosphere = vis_ctx;
        no_atmosphere.fields.clear(TAIYIN_NATIVE_FIELD_ATMOSPHERE);
        no_atmosphere.fields.clear(TAIYIN_NATIVE_FIELD_ATMOSPHERE_PRESSURE);
        no_atmosphere.fields.clear(TAIYIN_NATIVE_FIELD_ATMOSPHERE_TEMPERATURE);
        LocalSolarEclipseResultUt missing_atmosphere;
        const Status missing_atmosphere_status = solve_local_solar_eclipse_at_ut(
            &no_atmosphere,
            split_jd(eclipse_jd_ut),
            TAIYIN_ECLIPSE_INCLUDE_CONTACTS | TAIYIN_ECLIPSE_LOCAL_REFRACTION,
            &missing_atmosphere,
            &diag);
        if (missing_atmosphere_status != TAIYIN_ERROR_INVALID_ARGUMENT) {
            std::printf("FAIL: local refraction without atmosphere returned status %d\n", missing_atmosphere_status);
            return 1;
        }

        // Strict + refraction with a real atmosphere matches the non-strict result.
        LocalSolarEclipseResultUt apparent_strict;
        if (expect_status(
                solve_local_solar_eclipse_at_ut(
                    &vis_ctx,
                    split_jd(eclipse_jd_ut),
                    TAIYIN_ECLIPSE_INCLUDE_CONTACTS
                        | TAIYIN_ECLIPSE_LOCAL_REFRACTION
                        | TAIYIN_ECLIPSE_LOCAL_STRICT_METEOROLOGY,
                    &apparent_strict,
                    &diag),
                "local eclipse apparent strict mode")) {
            return 1;
        }
        if (expect_close_days(apparent_strict.maximum_jd_ut, apparent.maximum_jd_ut, 0.0, "strict keeps maximum instant")) return 1;
        if (expect_close_value(apparent_strict.sun_altitude_deg, apparent.sun_altitude_deg, 0.0, "strict keeps sun altitude")) return 1;

        // Unknown option bits remain rejected.
        LocalSolarEclipseResultUt unknown;
        const Status unknown_status = solve_local_solar_eclipse_at_ut(
            &vis_ctx,
            split_jd(eclipse_jd_ut),
            TAIYIN_ECLIPSE_INCLUDE_CONTACTS | (1ull << 40),
            &unknown,
            &diag);
        if (unknown_status != TAIYIN_ERROR_INVALID_ARGUMENT) {
            std::printf("FAIL: unknown local eclipse option bit returned status %d\n", unknown_status);
            return 1;
        }
    }

    std::printf("eclipse regression tests passed\n");
    return 0;
}
