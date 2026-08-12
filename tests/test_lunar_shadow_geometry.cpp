#include "runtime/eclipse/lunar_shadow_geometry.h"

#include "taiyin/apparent_position.h"
#include "taiyin/body_id.h"
#include "taiyin/runtime/native_context.h"
#include "taiyin/runtime/eclipse_search.h"
#include "taiyin/runtime/runtime.h"
#include "taiyin/vector3.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>

namespace {

int failures = 0;

void expect_true(bool value, const char* label) {
    if (!value) {
        std::printf("FAIL: %s\n", label);
        ++failures;
    }
}

void expect_near(double actual, double expected, double tolerance, const char* label) {
    if (!std::isfinite(actual) || std::fabs(actual - expected) > tolerance) {
        std::printf("FAIL: %s actual=%.17g expected=%.17g tolerance=%.3g\n",
                    label, actual, expected, tolerance);
        ++failures;
    }
}

std::string repository_root() {
    const char* root = std::getenv("TAIYIN_REPO_ROOT");
    return root && root[0] != '\0' ? root : "..";
}

taiyin::SplitJulianDate split_jd(double value) {
    taiyin::SplitJulianDate result;
    taiyin::split_julian_date_from_double(value, &result);
    return result;
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
    return context;
}

}  // namespace

int main() {
    using namespace taiyin;
    using namespace taiyin::runtime;

    const std::string ephemeris_root = repository_root()
        + "/data/ephemerides/opm2/major-bodies/600y";
    const char* source_paths[] = {ephemeris_root.c_str()};
    EphemerisRuntimeConfig config;
    config.source_paths = source_paths;
    config.source_path_count = 1;
    config.load_packaged_data = false;
    if (!initialize_global_ephemeris_runtime(config)) {
        std::printf("FAIL: initialize runtime\n");
        return 1;
    }

    NativeCalcContext context = make_context();
    EphemerisEvalDiagnostic diagnostic;
    LunarShadowGeometry center;
    LunarShadowGeometry before;
    LunarShadowGeometry after;
    const SplitJulianDate greatest = split_jd(2460926.2590);
    Status status = evaluate_lunar_shadow_geometry(
        &context, greatest, 0, &center, &diagnostic);
    expect_true(status == TAIYIN_STATUS_OK, "evaluate center geometry");
    status = evaluate_lunar_shadow_geometry(
        &context, greatest - 0.1, 0, &before, &diagnostic);
    expect_true(status == TAIYIN_STATUS_OK, "evaluate before geometry");
    status = evaluate_lunar_shadow_geometry(
        &context, greatest + 0.1, 0, &after, &diagnostic);
    expect_true(status == TAIYIN_STATUS_OK, "evaluate after geometry");

    expect_near(vector3_norm(center.shadow_axis_unit), 1.0, 1.0e-14,
                "shadow axis is unit length");
    expect_near(vector3_dot(center.transverse_offset_km, center.shadow_axis_unit),
                0.0, 1.0e-8, "transverse offset is perpendicular to axis");
    expect_near(vector3_norm(center.transverse_offset_km), center.axis_distance_km,
                1.0e-10, "axis distance matches transverse norm");
    expect_true(center.axial_distance_km > 0.0, "Moon is behind Earth");
    expect_true(center.moon_distance_km >= center.axial_distance_km,
                "axial distance does not exceed Moon distance");
    expect_true(center.penumbra_radius_km > center.umbra_radius_km,
                "penumbra exceeds umbra");
    expect_true(center.umbra_radius_km > center.moon_radius_km,
                "2025 umbra exceeds Moon radius");
    expect_true(std::fabs(after.umbra_radius_km - before.umbra_radius_km) > 1.0e-6,
                "umbra radius changes through the event");
    expect_true(std::fabs(after.penumbra_radius_km - before.penumbra_radius_km) > 1.0e-6,
                "penumbra radius changes through the event");

    // Exercise the fitted moving-boundary solver across three decades rather
    // than validating only the September 2025 total eclipse. Every applicable
    // contact must be finite and ordered around greatest eclipse.
    LunarEclipseResult eclipses[128];
    size_t eclipse_count = 0;
    const uint64_t search_flags = TAIYIN_ECLIPSE_INCLUDE_CONTACTS;
    status = search_lunar_eclipses_tt(
        &context,
        split_jd(2451545.0),
        split_jd(2462502.5),
        0,
        search_flags,
        eclipses,
        128,
        &eclipse_count,
        &diagnostic);
    expect_true(status == TAIYIN_STATUS_OK, "search lunar eclipses 2000-2029");
    expect_true(eclipse_count > 40, "three-decade eclipse sample is populated");
    for (size_t index = 0; status == TAIYIN_STATUS_OK && index < eclipse_count; ++index) {
        const LunarEclipseResult& eclipse = eclipses[index];
        LunarShadowGeometry maximum_geometry;
        status = evaluate_lunar_shadow_geometry(
            &context,
            eclipse.maximum_jd_tt,
            search_flags,
            &maximum_geometry,
            &diagnostic);
        expect_true(status == TAIYIN_STATUS_OK, "evaluate maximum lunar geometry");
        if (status != TAIYIN_STATUS_OK) break;
        expect_near(
            eclipse.umbra_radius_rad,
            std::atan2(
                maximum_geometry.umbra_radius_km,
                maximum_geometry.moon_distance_km),
            1.0e-15,
            "returned umbra angle uses Moon distance");
        expect_near(
            eclipse.penumbra_radius_rad,
            std::atan2(
                maximum_geometry.penumbra_radius_km,
                maximum_geometry.moon_distance_km),
            1.0e-15,
            "returned penumbra angle uses Moon distance");
        const SplitJulianDate p1 = eclipse.contact_jd_tt[TAIYIN_LUNAR_ECLIPSE_CONTACT_P1];
        const SplitJulianDate greatest_contact =
            eclipse.contact_jd_tt[TAIYIN_LUNAR_ECLIPSE_CONTACT_GREATEST];
        const SplitJulianDate p4 = eclipse.contact_jd_tt[TAIYIN_LUNAR_ECLIPSE_CONTACT_P4];
        expect_true(split_julian_date_is_finite(p1)
                        && split_julian_date_is_finite(greatest_contact)
                        && split_julian_date_is_finite(p4),
                    "penumbral contacts are finite");
        expect_true(p1 < greatest_contact && greatest_contact < p4,
                    "penumbral contacts are ordered");

        if ((eclipse.kind & (TAIYIN_ECLIPSE_PARTIAL | TAIYIN_ECLIPSE_TOTAL)) != 0u) {
            const SplitJulianDate u1 =
                eclipse.contact_jd_tt[TAIYIN_LUNAR_ECLIPSE_CONTACT_U1];
            const SplitJulianDate u4 =
                eclipse.contact_jd_tt[TAIYIN_LUNAR_ECLIPSE_CONTACT_U4];
            expect_true(split_julian_date_is_finite(u1)
                            && split_julian_date_is_finite(u4),
                        "umbral contacts are finite");
            expect_true(p1 < u1 && u1 < greatest_contact
                            && greatest_contact < u4 && u4 < p4,
                        "umbral contacts are ordered");
        }
        if ((eclipse.kind & TAIYIN_ECLIPSE_TOTAL) != 0u) {
            const SplitJulianDate u1 =
                eclipse.contact_jd_tt[TAIYIN_LUNAR_ECLIPSE_CONTACT_U1];
            const SplitJulianDate u2 =
                eclipse.contact_jd_tt[TAIYIN_LUNAR_ECLIPSE_CONTACT_U2];
            const SplitJulianDate u3 =
                eclipse.contact_jd_tt[TAIYIN_LUNAR_ECLIPSE_CONTACT_U3];
            const SplitJulianDate u4 =
                eclipse.contact_jd_tt[TAIYIN_LUNAR_ECLIPSE_CONTACT_U4];
            expect_true(split_julian_date_is_finite(u2)
                            && split_julian_date_is_finite(u3),
                        "totality contacts are finite");
            expect_true(u1 < u2 && u2 < greatest_contact
                            && greatest_contact < u3 && u3 < u4,
                        "totality contacts are ordered");
        }
    }

    if (failures != 0) {
        std::printf("lunar shadow geometry failures: %d\n", failures);
        return 1;
    }
    std::printf("lunar shadow geometry tests passed\n");
    return 0;
}
