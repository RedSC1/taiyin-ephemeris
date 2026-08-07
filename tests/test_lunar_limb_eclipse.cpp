#include "taiyin/apparent_position.h"
#include "taiyin/body_id.h"
#include "taiyin/lunar_limb_tll1.h"
#include "taiyin/runtime/eclipse_search.h"
#include "taiyin/runtime/native_context.h"
#include "taiyin/runtime/runtime.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace {

std::string repository_root() {
    const char* root = std::getenv("TAIYIN_REPO_ROOT");
    return root && root[0] != '\0' ? root : "..";
}

int fail(const char* label, taiyin::Status status = taiyin::TAIYIN_STATUS_OK) {
    std::printf("FAIL: %s status=%d\n", label, static_cast<int>(status));
    return 1;
}

taiyin::runtime::NativeCalcContext make_context() {
    taiyin::runtime::NativeCalcContext context;
    taiyin::runtime::native_context_set_geocentric_observer(
        &context, taiyin::TAIYIN_BODY_EARTH, taiyin::TAIYIN_BODY_EARTH);
    taiyin::runtime::native_context_use_solar_deflector(&context);
    context.apparent_options.flags = taiyin::TAIYIN_APPARENT_SPHERICAL
        | taiyin::TAIYIN_APPARENT_LIGHT_TIME
        | taiyin::TAIYIN_APPARENT_ABERRATION
        | taiyin::TAIYIN_APPARENT_DEFLECTION;
    context.apparent_options.output_frame_id =
        taiyin::TAIYIN_APPARENT_FRAME_TRUE_ECLIPTIC_OF_DATE;
    context.eclipse_shadow_model_id = static_cast<uint8_t>(
        taiyin::dispatch::ECLIPSE_SHADOW_CHAUVENET);
    return context;
}

taiyin::SplitJulianDate split_jd(double value) {
    taiyin::SplitJulianDate result;
    taiyin::split_julian_date_from_double(value, &result);
    return result;
}

bool contact_differences_are_plausible(
    const taiyin::SplitJulianDate* circular,
    const taiyin::SplitJulianDate* corrected,
    size_t count,
    const char* label
) {
    bool changed = false;
    for (size_t index = 0; index < count; ++index) {
        if (!taiyin::split_julian_date_is_finite(circular[index])
            || !taiyin::split_julian_date_is_finite(corrected[index])) continue;
        const double difference_seconds = (corrected[index] - circular[index]) * 86400.0;
        std::printf("%s[%zu] limb-minus-circular=%+.6f s\n", label, index, difference_seconds);
        if (std::fabs(difference_seconds) > 0.01) changed = true;
        if (std::fabs(difference_seconds) > 15.0) return false;
    }
    return changed;
}

bool contact_matches_oracle(
    const taiyin::SplitJulianDate& actual_jd,
    double oracle_jd,
    double tolerance_seconds,
    const char* label
) {
    const double error_seconds = (actual_jd - split_jd(oracle_jd)) * 86400.0;
    std::printf("%s error=%+.6f s\n", label, error_seconds);
    if (!std::isfinite(error_seconds) || std::fabs(error_seconds) > tolerance_seconds) {
        std::printf("FAIL: %s error=%+.6f s tolerance=%.3f s\n",
                    label, error_seconds, tolerance_seconds);
        return false;
    }
    return true;
}

}  // namespace

int main() {
    using namespace taiyin;
    using namespace taiyin::runtime;

    const std::string root = repository_root();
    const std::string ephemeris_root = root + "/data/ephemerides/opm2/major-bodies/600y";
    const char* sources[] = {ephemeris_root.c_str()};
    EphemerisRuntimeConfig config;
    config.source_paths = sources;
    config.source_path_count = 1;
    config.load_packaged_data = false;
    if (!initialize_global_ephemeris_runtime(config)) {
        return fail("initialize ephemeris runtime");
    }

    const std::string limb_path = root + "/data/lunar-limb/kaguya_lalt_16ppd.tll1";
    Status status = TAIYIN_STATUS_OK;

    EphemerisEvalDiagnostic diagnostic;
    const uint64_t contact_flags = TAIYIN_ECLIPSE_INCLUDE_CONTACTS;
    const uint64_t corrected_flags = contact_flags | TAIYIN_ECLIPSE_LUNAR_LIMB_CORRECTION;
    if (valid_eclipse_flags(1ull << 36)) {
        return fail("retired pre-release flag bit must remain invalid");
    }

    NativeCalcContext lunar_context = make_context();
    LunarEclipseResultUt missing_lunar;
    status = solve_lunar_eclipse_at_ut(
        &lunar_context, split_jd(2460926.25), corrected_flags, &missing_lunar, &diagnostic);
    if (status != TAIYIN_ERROR_UNSUPPORTED) {
        return fail("lunar correction without model must fail", status);
    }

    LunarEclipseResultUt circular_lunar;
    status = solve_lunar_eclipse_at_ut(
        &lunar_context, split_jd(2460926.25), contact_flags, &circular_lunar, &diagnostic);
    if (status != TAIYIN_STATUS_OK) return fail("circular lunar eclipse", status);
    status = load_global_lunar_limb_model(limb_path.c_str());
    if (status != TAIYIN_STATUS_OK) return fail("load global TLL1 model", status);
    LunarEclipseResultUt attached_but_disabled_lunar;
    status = solve_lunar_eclipse_at_ut(
        &lunar_context, split_jd(2460926.25), contact_flags, &attached_but_disabled_lunar, &diagnostic);
    if (status != TAIYIN_STATUS_OK) return fail("disabled global lunar model", status);
    for (size_t index = 0; index < TAIYIN_LUNAR_ECLIPSE_CONTACT_COUNT; ++index) {
        const SplitJulianDate& a = circular_lunar.contact_jd_ut[index];
        const SplitJulianDate& b = attached_but_disabled_lunar.contact_jd_ut[index];
        if ((split_julian_date_is_finite(a) || split_julian_date_is_finite(b)) && a != b) {
            return fail("global model changed disabled lunar result");
        }
    }
    LunarEclipseResultUt corrected_lunar;
    status = solve_lunar_eclipse_at_ut(
        &lunar_context, split_jd(2460926.25), corrected_flags, &corrected_lunar, &diagnostic);
    if (status != TAIYIN_STATUS_OK) return fail("corrected lunar eclipse", status);
    if (corrected_lunar.kind != circular_lunar.kind
        || !contact_differences_are_plausible(
            circular_lunar.contact_jd_ut,
            corrected_lunar.contact_jd_ut,
            TAIYIN_LUNAR_ECLIPSE_CONTACT_COUNT,
            "lunar")) {
        return fail("lunar contact corrections are implausible");
    }
    const double lunar_pmo_jd[TAIYIN_LUNAR_ECLIPSE_CONTACT_COUNT] = {
        2460926.143680556,
        2460926.185277778,
        2460926.229444444,
        2460926.258194444,
        2460926.286944444,
        2460926.331180556,
        2460926.372638889,
    };
    for (size_t index = 0; index < TAIYIN_LUNAR_ECLIPSE_CONTACT_COUNT; ++index) {
        if (!contact_matches_oracle(
                corrected_lunar.contact_jd_ut[index],
                lunar_pmo_jd[index],
                7.0,
                "corrected 2025 lunar PMO contact")) {
            return 1;
        }
    }

    status = load_global_lunar_limb_model(nullptr);
    if (status != TAIYIN_STATUS_OK) return fail("clear global TLL1 model", status);
    NativeCalcContext global_solar_context = make_context();
    SolarEclipseRouteRow missing_route_row;
    status = compute_solar_eclipse_route_row_ut(
        &global_solar_context,
        split_jd(2460409.262039739),
        TAIYIN_ECLIPSE_LUNAR_LIMB_CORRECTION,
        &missing_route_row,
        &diagnostic);
    if (status != TAIYIN_ERROR_UNSUPPORTED) {
        return fail("route correction without model must fail", status);
    }
    SolarEclipseRouteRow unattached_route_row;
    status = compute_solar_eclipse_route_row_ut(
        &global_solar_context, split_jd(2460409.262039739), 0, &unattached_route_row, &diagnostic);
    if (status != TAIYIN_STATUS_OK) return fail("unattached circular route row", status);
    status = compute_solar_eclipse_route_row_ut(
        &global_solar_context,
        split_jd(2460409.262039739),
        TAIYIN_ECLIPSE_INCLUDE_CONTACTS,
        &unattached_route_row,
        &diagnostic);
    if (status != TAIYIN_ERROR_INVALID_ARGUMENT) {
        return fail("route row must reject non-route eclipse flags", status);
    }
    SolarEclipseResultUt missing_global_solar;
    status = solve_solar_eclipse_at_ut(
        &global_solar_context,
        split_jd(2460409.25),
        corrected_flags,
        &missing_global_solar,
        &diagnostic);
    if (status != TAIYIN_ERROR_UNSUPPORTED) {
        return fail("global solar correction without model must fail", status);
    }
    SolarEclipseResultUt circular_global_solar;
    status = solve_solar_eclipse_at_ut(
        &global_solar_context,
        split_jd(2460409.25),
        contact_flags,
        &circular_global_solar,
        &diagnostic);
    if (status != TAIYIN_STATUS_OK) return fail("circular global solar eclipse", status);
    status = load_global_lunar_limb_model(limb_path.c_str());
    if (status != TAIYIN_STATUS_OK) return fail("reload global TLL1 model", status);
    SolarEclipseRouteRow circular_route_row;
    SolarEclipseRouteRow corrected_route_row;
    status = compute_solar_eclipse_route_row_ut(
        &global_solar_context, split_jd(2460409.262039739), 0, &circular_route_row, &diagnostic);
    if (status != TAIYIN_STATUS_OK) return fail("circular route row", status);
    if (circular_route_row.north_limit.latitude_deg
            != unattached_route_row.north_limit.latitude_deg
        || circular_route_row.north_limit.longitude_deg
            != unattached_route_row.north_limit.longitude_deg
        || circular_route_row.south_limit.latitude_deg
            != unattached_route_row.south_limit.latitude_deg
        || circular_route_row.south_limit.longitude_deg
            != unattached_route_row.south_limit.longitude_deg
        || circular_route_row.path_width_km != unattached_route_row.path_width_km) {
        return fail("global model changed route while correction flag was disabled");
    }
    status = compute_solar_eclipse_route_row_ut(
        &global_solar_context,
        split_jd(2460409.262039739),
        TAIYIN_ECLIPSE_LUNAR_LIMB_CORRECTION,
        &corrected_route_row,
        &diagnostic);
    if (status != TAIYIN_STATUS_OK) return fail("corrected route row", status);
    if (corrected_route_row.center_line.latitude_deg
            != circular_route_row.center_line.latitude_deg
        || corrected_route_row.center_line.longitude_deg
            != circular_route_row.center_line.longitude_deg) {
        return fail("lunar limb must not move the shadow-axis center line");
    }
    const double north_route_shift_deg = std::hypot(
        corrected_route_row.north_limit.latitude_deg
            - circular_route_row.north_limit.latitude_deg,
        corrected_route_row.north_limit.longitude_deg
            - circular_route_row.north_limit.longitude_deg);
    const double south_route_shift_deg = std::hypot(
        corrected_route_row.south_limit.latitude_deg
            - circular_route_row.south_limit.latitude_deg,
        corrected_route_row.south_limit.longitude_deg
            - circular_route_row.south_limit.longitude_deg);
    const double penumbral_route_shift_deg = std::hypot(
        corrected_route_row.penumbral_north_limit.latitude_deg
            - circular_route_row.penumbral_north_limit.latitude_deg,
        corrected_route_row.penumbral_north_limit.longitude_deg
            - circular_route_row.penumbral_north_limit.longitude_deg);
    const double half_magnitude_route_shift_deg = std::hypot(
        corrected_route_row.half_magnitude_north_limit.latitude_deg
            - circular_route_row.half_magnitude_north_limit.latitude_deg,
        corrected_route_row.half_magnitude_north_limit.longitude_deg
            - circular_route_row.half_magnitude_north_limit.longitude_deg);
    std::printf(
        "global-solar route limb shifts core-north=%.9f deg core-south=%.9f deg penumbra=%.9f deg half=%.9f deg width=%+.6f km duration=%+.6f s\n",
        north_route_shift_deg,
        south_route_shift_deg,
        penumbral_route_shift_deg,
        half_magnitude_route_shift_deg,
        corrected_route_row.path_width_km - circular_route_row.path_width_km,
        corrected_route_row.duration_seconds - circular_route_row.duration_seconds);
    if (!std::isfinite(north_route_shift_deg) || !std::isfinite(south_route_shift_deg)
        || !std::isfinite(penumbral_route_shift_deg)
        || !std::isfinite(half_magnitude_route_shift_deg)
        || !(north_route_shift_deg > 1.0e-7)
        || !(south_route_shift_deg > 1.0e-7)
        || !(penumbral_route_shift_deg > 1.0e-7)
        || !(half_magnitude_route_shift_deg > 1.0e-7)
        || !std::isfinite(corrected_route_row.duration_seconds)
        || std::fabs(corrected_route_row.duration_seconds
            - circular_route_row.duration_seconds) <= 0.001) {
        return fail("lunar limb must move every non-center route layer");
    }
    NativeCalcContext center_local_context = global_solar_context;
    status = native_context_set_observer_location(
        &center_local_context,
        native_observer_location_degrees(
            corrected_route_row.center_line.longitude_deg,
            corrected_route_row.center_line.latitude_deg,
            0.0));
    if (status != TAIYIN_STATUS_OK) return fail("set corrected route-center observer", status);
    LocalSolarEclipseResultUt center_local_result;
    status = solve_local_solar_eclipse_at_ut(
        &center_local_context,
        corrected_route_row.jd_ut,
        TAIYIN_ECLIPSE_INCLUDE_CONTACTS | TAIYIN_ECLIPSE_LUNAR_LIMB_CORRECTION,
        &center_local_result,
        &diagnostic);
    if (status != TAIYIN_STATUS_OK) return fail("solve corrected route-center observer", status);
    if (!std::isfinite(center_local_result.duration_seconds)
        || std::fabs(center_local_result.duration_seconds
            - corrected_route_row.duration_seconds) > 0.05) {
        return fail("corrected route duration must match local contact refinement");
    }
    SolarEclipseResultUt annular_event;
    status = solve_solar_eclipse_at_ut(
        &global_solar_context,
        split_jd(2460232.25),
        TAIYIN_ECLIPSE_INCLUDE_CONTACTS,
        &annular_event,
        &diagnostic);
    if (status != TAIYIN_STATUS_OK
        || (annular_event.kind & TAIYIN_ECLIPSE_ANNULAR) == 0u) {
        return fail("solve 2023 annular route event", status);
    }
    SolarEclipseRouteRow circular_annular_route;
    SolarEclipseRouteRow corrected_annular_route;
    status = compute_solar_eclipse_route_row_ut(
        &global_solar_context,
        annular_event.maximum_jd_ut,
        0,
        &circular_annular_route,
        &diagnostic);
    if (status != TAIYIN_STATUS_OK) return fail("circular annular route row", status);
    status = compute_solar_eclipse_route_row_ut(
        &global_solar_context,
        annular_event.maximum_jd_ut,
        TAIYIN_ECLIPSE_LUNAR_LIMB_CORRECTION,
        &corrected_annular_route,
        &diagnostic);
    if (status != TAIYIN_STATUS_OK) return fail("corrected annular route row", status);
    if (!std::isfinite(corrected_annular_route.duration_seconds)
        || std::fabs(corrected_annular_route.duration_seconds
            - circular_annular_route.duration_seconds) <= 0.001) {
        return fail("annular route duration must use lunar limb");
    }
    NativeCalcContext circular_annular_local_context = global_solar_context;
    status = native_context_set_observer_location(
        &circular_annular_local_context,
        native_observer_location_degrees(
            circular_annular_route.center_line.longitude_deg,
            circular_annular_route.center_line.latitude_deg,
            0.0));
    if (status != TAIYIN_STATUS_OK) return fail("set circular annular route-center observer", status);
    LocalSolarEclipseResultUt circular_annular_local_result;
    status = solve_local_solar_eclipse_at_ut(
        &circular_annular_local_context,
        circular_annular_route.jd_ut,
        TAIYIN_ECLIPSE_INCLUDE_CONTACTS,
        &circular_annular_local_result,
        &diagnostic);
    if (status != TAIYIN_STATUS_OK
        || (circular_annular_local_result.kind & TAIYIN_ECLIPSE_ANNULAR) == 0u
        || !split_julian_date_is_finite(
            circular_annular_local_result.contact_jd_ut[TAIYIN_LOCAL_SOLAR_CONTACT_C2])
        || !split_julian_date_is_finite(
            circular_annular_local_result.contact_jd_ut[TAIYIN_LOCAL_SOLAR_CONTACT_C3])
        || !(circular_annular_local_result.duration_seconds > 0.0)) {
        return fail("circular local annular eclipse must include inner contacts", status);
    }
    NativeCalcContext annular_local_context = global_solar_context;
    status = native_context_set_observer_location(
        &annular_local_context,
        native_observer_location_degrees(
            corrected_annular_route.center_line.longitude_deg,
            corrected_annular_route.center_line.latitude_deg,
            0.0));
    if (status != TAIYIN_STATUS_OK) return fail("set annular route-center observer", status);
    LocalSolarEclipseResultUt annular_local_result;
    status = solve_local_solar_eclipse_at_ut(
        &annular_local_context,
        corrected_annular_route.jd_ut,
        TAIYIN_ECLIPSE_INCLUDE_CONTACTS | TAIYIN_ECLIPSE_LUNAR_LIMB_CORRECTION,
        &annular_local_result,
        &diagnostic);
    std::printf(
        "annular route circular-duration=%.6f corrected-duration=%.6f local-duration=%.6f local-kind=%u local-magnitude=%.9f maximum-delta=%+.6f s\n",
        circular_annular_route.duration_seconds,
        corrected_annular_route.duration_seconds,
        annular_local_result.duration_seconds,
        annular_local_result.kind,
        annular_local_result.magnitude,
        (annular_local_result.maximum_jd_ut - corrected_annular_route.jd_ut) * 86400.0);
    if (status != TAIYIN_STATUS_OK
        || (annular_local_result.kind & TAIYIN_ECLIPSE_ANNULAR) == 0u) {
        return fail("solve corrected annular route-center observer", status);
    }
    if (!std::isfinite(annular_local_result.duration_seconds)
        || std::fabs(annular_local_result.duration_seconds
            - corrected_annular_route.duration_seconds) > 0.05) {
        return fail("corrected annular route duration must match local contact refinement");
    }

    const SolarEclipsePathPoint corrected_limits[] = {
        corrected_route_row.north_limit,
        corrected_route_row.south_limit,
    };
    const SolarEclipsePathPoint circular_limits[] = {
        circular_route_row.north_limit,
        circular_route_row.south_limit,
    };
    for (size_t index = 0; index < 2; ++index) {
        NativeCalcContext circular_local_context = global_solar_context;
        status = native_context_set_observer_location(
            &circular_local_context,
            native_observer_location_degrees(
                circular_limits[index].longitude_deg,
                circular_limits[index].latitude_deg,
                0.0));
        if (status != TAIYIN_STATUS_OK) return fail("set circular route-limit observer", status);
        LocalSolarEclipseResultUt circular_local_limit;
        status = solve_local_solar_eclipse_at_ut(
            &circular_local_context,
            circular_route_row.jd_ut,
            TAIYIN_ECLIPSE_INCLUDE_CONTACTS,
            &circular_local_limit,
            &diagnostic);
        if (status != TAIYIN_STATUS_OK) return fail("solve circular route-limit observer", status);
        NativeCalcContext local_route_context = global_solar_context;
        status = native_context_set_observer_location(
            &local_route_context,
            native_observer_location_degrees(
                corrected_limits[index].longitude_deg,
                corrected_limits[index].latitude_deg,
                0.0));
        if (status != TAIYIN_STATUS_OK) return fail("set route-limit observer", status);
        LocalSolarEclipseResultUt local_limit;
        status = solve_local_solar_eclipse_at_ut(
            &local_route_context,
            corrected_route_row.jd_ut,
            TAIYIN_ECLIPSE_INCLUDE_CONTACTS | TAIYIN_ECLIPSE_LUNAR_LIMB_CORRECTION,
            &local_limit,
            &diagnostic);
        if (status != TAIYIN_STATUS_OK) return fail("solve corrected route-limit observer", status);
        std::printf(
            "global-solar route limit[%zu] circular-mag=%.9f corrected-mag=%.9f duration=%.6f s maximum-delta=%+.6f s\n",
            index,
            circular_local_limit.magnitude,
            local_limit.magnitude,
            local_limit.duration_seconds,
            (local_limit.maximum_jd_ut - corrected_route_row.jd_ut) * 86400.0);
        if (!std::isfinite(local_limit.duration_seconds)
            || std::fabs(local_limit.duration_seconds) > 10.0
            || std::fabs(local_limit.maximum_jd_ut - corrected_route_row.jd_ut) > 120.0 / 86400.0) {
            return fail("corrected route limit must agree with local apparent geometry");
        }
    }

    size_t circular_curve_count = 0;
    status = compute_solar_eclipse_route_curves_ut(
        &global_solar_context,
        split_jd(2460409.262039739),
        0,
        nullptr,
        0,
        &circular_curve_count,
        &diagnostic);
    if (status != TAIYIN_STATUS_OK || circular_curve_count == 0) {
        return fail("count circular route curves", status);
    }
    std::vector<SolarEclipseRouteCurvePoint> circular_curves(circular_curve_count);
    status = compute_solar_eclipse_route_curves_ut(
        &global_solar_context,
        split_jd(2460409.262039739),
        0,
        circular_curves.data(),
        circular_curves.size(),
        &circular_curve_count,
        &diagnostic);
    if (status != TAIYIN_STATUS_OK) return fail("fill circular route curves", status);

    size_t corrected_curve_count = 0;
    status = compute_solar_eclipse_route_curves_ut(
        &global_solar_context,
        split_jd(2460409.262039739),
        TAIYIN_ECLIPSE_LUNAR_LIMB_CORRECTION,
        nullptr,
        0,
        &corrected_curve_count,
        &diagnostic);
    if (status != TAIYIN_STATUS_OK || corrected_curve_count == 0) {
        return fail("count corrected route curves", status);
    }
    std::vector<SolarEclipseRouteCurvePoint> corrected_curves(corrected_curve_count);
    status = compute_solar_eclipse_route_curves_ut(
        &global_solar_context,
        split_jd(2460409.262039739),
        TAIYIN_ECLIPSE_LUNAR_LIMB_CORRECTION,
        corrected_curves.data(),
        corrected_curves.size(),
        &corrected_curve_count,
        &diagnostic);
    if (status != TAIYIN_STATUS_OK) return fail("fill corrected route curves", status);
    const SplitJulianDate comparison_jd_tt = corrected_route_row.jd_tt;
    const SolarEclipseRouteCurvePoint* circular_comparison = nullptr;
    const SolarEclipseRouteCurvePoint* corrected_comparison = nullptr;
    for (size_t index = 0; index < circular_curve_count; ++index) {
        if (circular_curves[index].curve_kind != TAIYIN_SOLAR_ROUTE_CURVE_CORE_NORTH) continue;
        if (!circular_comparison
            || std::fabs(circular_curves[index].jd_tt - comparison_jd_tt)
                < std::fabs(circular_comparison->jd_tt - comparison_jd_tt)) {
            circular_comparison = &circular_curves[index];
        }
    }
    for (size_t index = 0; index < corrected_curve_count; ++index) {
        if (corrected_curves[index].curve_kind != TAIYIN_SOLAR_ROUTE_CURVE_CORE_NORTH) continue;
        if (!corrected_comparison
            || std::fabs(corrected_curves[index].jd_tt - comparison_jd_tt)
                < std::fabs(corrected_comparison->jd_tt - comparison_jd_tt)) {
            corrected_comparison = &corrected_curves[index];
        }
    }
    const bool route_curve_changed = circular_comparison && corrected_comparison
        && (std::fabs(corrected_comparison->latitude_deg
                - circular_comparison->latitude_deg) > 1.0e-7
            || std::fabs(corrected_comparison->longitude_deg
                - circular_comparison->longitude_deg) > 1.0e-7);
    if (!route_curve_changed) return fail("lunar limb did not change route curves");
    SolarEclipseRouteRow corrected_curve_row;
    status = compute_solar_eclipse_route_row_tt(
        &global_solar_context,
        corrected_comparison->jd_tt,
        TAIYIN_ECLIPSE_LUNAR_LIMB_CORRECTION,
        &corrected_curve_row,
        &diagnostic);
    if (status != TAIYIN_STATUS_OK) return fail("sample corrected route curve row", status);
    const double longitude_delta = std::remainder(
        corrected_comparison->longitude_deg - corrected_curve_row.north_limit.longitude_deg,
        360.0);
    if (!std::isfinite(corrected_curve_row.north_limit.latitude_deg)
        || std::fabs(corrected_comparison->latitude_deg
                - corrected_curve_row.north_limit.latitude_deg) > 1.0e-6
        || !std::isfinite(longitude_delta) || std::fabs(longitude_delta) > 1.0e-6) {
        return fail("corrected route curve must use profiled route-row limit");
    }

    size_t circular_polygon_count = 0;
    SolarEclipseRouteProductSummary circular_route_summary;
    status = compute_solar_eclipse_route_map_product_ut(
        &global_solar_context,
        split_jd(2460409.262039739),
        0,
        nullptr,
        0,
        &circular_polygon_count,
        &circular_route_summary,
        &diagnostic);
    if (status != TAIYIN_STATUS_OK || circular_polygon_count == 0) {
        return fail("count circular route polygons", status);
    }
    size_t corrected_polygon_count = 0;
    SolarEclipseRouteProductSummary corrected_route_summary;
    status = compute_solar_eclipse_route_map_product_ut(
        &global_solar_context,
        split_jd(2460409.262039739),
        TAIYIN_ECLIPSE_LUNAR_LIMB_CORRECTION,
        nullptr,
        0,
        &corrected_polygon_count,
        &corrected_route_summary,
        &diagnostic);
    if (status != TAIYIN_STATUS_OK || corrected_polygon_count == 0
        || corrected_route_summary.core_polygon_point_count == 0
        || corrected_route_summary.penumbral_polygon_point_count == 0
        || corrected_route_summary.half_magnitude_polygon_point_count == 0) {
        return fail("corrected route map product", status);
    }
    std::vector<SolarEclipseRouteProductPoint> circular_polygons(circular_polygon_count);
    std::vector<SolarEclipseRouteProductPoint> corrected_polygons(corrected_polygon_count);
    status = compute_solar_eclipse_route_map_product_ut(
        &global_solar_context,
        split_jd(2460409.262039739),
        0,
        circular_polygons.data(),
        circular_polygons.size(),
        &circular_polygon_count,
        &circular_route_summary,
        &diagnostic);
    if (status != TAIYIN_STATUS_OK) return fail("fill circular route polygons", status);
    status = compute_solar_eclipse_route_map_product_ut(
        &global_solar_context,
        split_jd(2460409.262039739),
        TAIYIN_ECLIPSE_LUNAR_LIMB_CORRECTION,
        corrected_polygons.data(),
        corrected_polygons.size(),
        &corrected_polygon_count,
        &corrected_route_summary,
        &diagnostic);
    if (status != TAIYIN_STATUS_OK) return fail("fill corrected route polygons", status);
    const SolarEclipseRouteProductPoint* circular_polygon_comparison = nullptr;
    const SolarEclipseRouteProductPoint* corrected_polygon_comparison = nullptr;
    for (size_t index = 0; index < circular_polygon_count; ++index) {
        if (circular_polygons[index].point_kind
            != TAIYIN_SOLAR_ROUTE_PRODUCT_POINT_CORE_NORTH) continue;
        if (!circular_polygon_comparison
            || std::fabs(circular_polygons[index].jd_tt - comparison_jd_tt)
                < std::fabs(circular_polygon_comparison->jd_tt - comparison_jd_tt)) {
            circular_polygon_comparison = &circular_polygons[index];
        }
    }
    for (size_t index = 0; index < corrected_polygon_count; ++index) {
        if (corrected_polygons[index].point_kind
            != TAIYIN_SOLAR_ROUTE_PRODUCT_POINT_CORE_NORTH) continue;
        if (!corrected_polygon_comparison
            || std::fabs(corrected_polygons[index].jd_tt - comparison_jd_tt)
                < std::fabs(corrected_polygon_comparison->jd_tt - comparison_jd_tt)) {
            corrected_polygon_comparison = &corrected_polygons[index];
        }
    }
    const bool route_polygon_changed = circular_polygon_comparison && corrected_polygon_comparison
        && (std::fabs(corrected_polygon_comparison->latitude_deg
                - circular_polygon_comparison->latitude_deg) > 1.0e-7
            || std::fabs(corrected_polygon_comparison->longitude_deg
                - circular_polygon_comparison->longitude_deg) > 1.0e-7);
    if (!route_polygon_changed) return fail("lunar limb did not change route polygons");
    SolarEclipseResultUt corrected_global_solar;
    status = solve_solar_eclipse_at_ut(
        &global_solar_context,
        split_jd(2460409.25),
        corrected_flags,
        &corrected_global_solar,
        &diagnostic);
    if (status != TAIYIN_STATUS_OK) return fail("corrected global solar eclipse", status);
    if (corrected_global_solar.kind != circular_global_solar.kind
        || !contact_differences_are_plausible(
            circular_global_solar.contact_jd_ut,
            corrected_global_solar.contact_jd_ut,
            TAIYIN_SOLAR_ECLIPSE_CONTACT_COUNT,
            "global-solar")) {
        return fail("global solar contact corrections are implausible");
    }
    if (!contact_matches_oracle(
            corrected_global_solar.contact_jd_ut[TAIYIN_SOLAR_ECLIPSE_CONTACT_P1],
            2460409.154317129,
            3.1,
            "corrected 2024 solar PMO P1")
        || !contact_matches_oracle(
            corrected_global_solar.contact_jd_ut[TAIYIN_SOLAR_ECLIPSE_CONTACT_P4],
            2460409.369687500,
            3.1,
            "corrected 2024 solar PMO P4")) {
        return 1;
    }
    if (!contact_matches_oracle(
            corrected_global_solar.contact_jd_ut[TAIYIN_SOLAR_ECLIPSE_CONTACT_P1],
            2460409.154316584114,
            0.1,
            "coupled 2024 solar P1 fixture")
        || !contact_matches_oracle(
            corrected_global_solar.contact_jd_ut[TAIYIN_SOLAR_ECLIPSE_CONTACT_P4],
            2460409.369682914577,
            0.1,
            "coupled 2024 solar P4 fixture")) {
        return 1;
    }

    SolarEclipseResultUt circular_global_solar_2026;
    SolarEclipseResultUt corrected_global_solar_2026;
    status = solve_solar_eclipse_at_ut(
        &global_solar_context,
        split_jd(2461265.24),
        contact_flags,
        &circular_global_solar_2026,
        &diagnostic);
    if (status != TAIYIN_STATUS_OK) return fail("circular 2026 global solar eclipse", status);
    status = solve_solar_eclipse_at_ut(
        &global_solar_context,
        split_jd(2461265.24),
        corrected_flags,
        &corrected_global_solar_2026,
        &diagnostic);
    if (status != TAIYIN_STATUS_OK) return fail("corrected 2026 global solar eclipse", status);
    if (corrected_global_solar_2026.kind != circular_global_solar_2026.kind
        || !contact_differences_are_plausible(
            circular_global_solar_2026.contact_jd_ut,
            corrected_global_solar_2026.contact_jd_ut,
            TAIYIN_SOLAR_ECLIPSE_CONTACT_COUNT,
            "global-solar-2026")) {
        return fail("2026 global solar contact corrections are implausible");
    }
    if (!contact_matches_oracle(
            corrected_global_solar_2026.contact_jd_ut[TAIYIN_SOLAR_ECLIPSE_CONTACT_P1],
            2461265.148773148,
            3.1,
            "corrected 2026 solar PMO P1")
        || !contact_matches_oracle(
            corrected_global_solar_2026.contact_jd_ut[TAIYIN_SOLAR_ECLIPSE_CONTACT_P4],
            2461265.331932870,
            3.1,
            "corrected 2026 solar PMO P4")) {
        return 1;
    }
    if (!contact_matches_oracle(
            corrected_global_solar_2026.contact_jd_ut[TAIYIN_SOLAR_ECLIPSE_CONTACT_P1],
            2461265.148746889550,
            0.1,
            "coupled 2026 solar P1 fixture")
        || !contact_matches_oracle(
            corrected_global_solar_2026.contact_jd_ut[TAIYIN_SOLAR_ECLIPSE_CONTACT_P4],
            2461265.331927465741,
            0.1,
            "coupled 2026 solar P4 fixture")) {
        return 1;
    }

    NativeCalcContext solar_context = make_context();
    native_context_set_observer_location(
        &solar_context, native_observer_location_degrees(-106.4, 23.2, 0.0));
    status = load_global_lunar_limb_model(nullptr);
    if (status != TAIYIN_STATUS_OK) return fail("clear global TLL1 before local test", status);
    LocalSolarEclipseResultUt missing_solar;
    status = solve_local_solar_eclipse_at_ut(
        &solar_context, split_jd(2460409.262231433), corrected_flags, &missing_solar, &diagnostic);
    if (status != TAIYIN_ERROR_UNSUPPORTED) {
        return fail("solar correction without model must fail", status);
    }
    LocalSolarEclipseResultUt circular_solar;
    status = solve_local_solar_eclipse_at_ut(
        &solar_context, split_jd(2460409.262231433), contact_flags, &circular_solar, &diagnostic);
    if (status != TAIYIN_STATUS_OK) return fail("circular local solar eclipse", status);
    status = load_global_lunar_limb_model(limb_path.c_str());
    if (status != TAIYIN_STATUS_OK) return fail("reload global TLL1 for local test", status);
    LocalSolarEclipseResultUt corrected_solar;
    status = solve_local_solar_eclipse_at_ut(
        &solar_context, split_jd(2460409.262231433), corrected_flags, &corrected_solar, &diagnostic);
    if (status != TAIYIN_STATUS_OK) return fail("corrected local solar eclipse", status);
    if ((corrected_solar.kind & TAIYIN_ECLIPSE_TOTAL) == 0u
        || !contact_differences_are_plausible(
            circular_solar.contact_jd_ut,
            corrected_solar.contact_jd_ut,
            TAIYIN_LOCAL_SOLAR_CONTACT_COUNT,
            "solar")) {
        return fail("solar contact corrections are implausible");
    }

    load_global_lunar_limb_model(nullptr);
    std::printf("lunar-limb eclipse integration tests passed\n");
    return 0;
}
