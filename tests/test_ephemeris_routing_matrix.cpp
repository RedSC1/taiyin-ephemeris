#include "taiyin/body_id.h"
#include "taiyin/internal/custom_ephemeris_method.h"
#include "taiyin/internal/ephemeris_source_identity.h"
#include "taiyin/internal/opm2.h"
#include "taiyin/internal/spk_catalog_discovery.h"
#include "taiyin/runtime/ephemeris_route.h"
#include "taiyin/runtime/runtime.h"
#include "taiyin/status.h"
#include "taiyin/time.h"

#include <cassert>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace {

const double JD0 = taiyin::JD_J2000;
const int CUSTOM_SSB_METHOD = 991001;

struct ConstantStateData {
    taiyin::Vector3 position;
    taiyin::Vector3 velocity;
    taiyin::Vector3 acceleration;
};

void expect_true(bool value, const char* label) {
    if (!value) {
        std::fprintf(stderr, "expected true: %s\n", label);
        assert(false);
    }
}

void expect_false(bool value, const char* label) {
    if (value) {
        std::fprintf(stderr, "expected false: %s\n", label);
        assert(false);
    }
}

void expect_int(int actual, int expected, const char* label) {
    if (actual != expected) {
        std::fprintf(stderr, "expected %d got %d: %s\n", expected, actual, label);
        assert(false);
    }
}

void expect_near(double actual, double expected, double tolerance, const char* label) {
    if (std::fabs(actual - expected) > tolerance) {
        std::fprintf(
            stderr,
            "expected %.17g got %.17g (tol %.3g): %s\n",
            expected,
            actual,
            tolerance,
            label);
        assert(false);
    }
}

taiyin::Vector3 make_vector(double x, double y, double z) {
    taiyin::Vector3 out;
    out.x = x;
    out.y = y;
    out.z = z;
    return out;
}

bool constant_position(
    const taiyin::SplitJulianDate&,
    const void* raw,
    taiyin::Vector3* out
) {
    if (!raw || !out) return false;
    *out = static_cast<const ConstantStateData*>(raw)->position;
    return true;
}

bool constant_velocity(
    const taiyin::SplitJulianDate&,
    const void* raw,
    taiyin::Vector3* out
) {
    if (!raw || !out) return false;
    *out = static_cast<const ConstantStateData*>(raw)->velocity;
    return true;
}

bool constant_acceleration(
    const taiyin::SplitJulianDate&,
    const void* raw,
    taiyin::Vector3* out
) {
    if (!raw || !out) return false;
    *out = static_cast<const ConstantStateData*>(raw)->acceleration;
    return true;
}

taiyin::internal::EphemerisBlockDescriptor make_descriptor(
    int target_id,
    int center_id,
    taiyin::internal::EphemerisBlockFormat format,
    int method_id,
    uint64_t source_id,
    uint64_t block_id,
    const char* path
) {
    taiyin::internal::EphemerisBlockDescriptor descriptor;
    descriptor.target_id = target_id;
    descriptor.center_id = center_id;
    descriptor.method_id = method_id;
    descriptor.frame = taiyin::internal::IcrfJ2000Equatorial;
    descriptor.format = format;
    descriptor.jd_tdb_start = JD0 - 10.0;
    descriptor.jd_tdb_end = JD0 + 10.0;
    descriptor.source_key = taiyin::internal::EphemerisBlockKey(
        source_id, block_id, 1, 1);
    descriptor.path = path ? path : "";
    return descriptor;
}

taiyin::runtime::EphemerisRequest make_request(int target_id, int center_id) {
    taiyin::runtime::EphemerisRequest request;
    request.target_id = target_id;
    request.center_id = center_id;
    request.frame = taiyin::internal::IcrfJ2000Equatorial;
    request.jd_tdb = taiyin::SplitJulianDate(2451545, 0.0);
    return request;
}

const uint64_t* recognized_de_sources(size_t* out_count) {
    static const uint64_t sources[] = {
        taiyin::internal::SPK_SOURCE_JPL_DE442,
        taiyin::internal::SPK_SOURCE_JPL_DE441,
        taiyin::internal::SPK_SOURCE_JPL_DE440,
        taiyin::internal::SPK_SOURCE_JPL_DE438,
        taiyin::internal::SPK_SOURCE_JPL_DE435,
        taiyin::internal::SPK_SOURCE_JPL_DE432,
        taiyin::internal::SPK_SOURCE_JPL_DE431,
        taiyin::internal::SPK_SOURCE_JPL_DE430,
        taiyin::internal::SPK_SOURCE_JPL_DE423,
        taiyin::internal::SPK_SOURCE_JPL_DE421,
        taiyin::internal::SPK_SOURCE_JPL_DE418,
        taiyin::internal::SPK_SOURCE_JPL_DE414,
        taiyin::internal::SPK_SOURCE_JPL_DE413,
        taiyin::internal::SPK_SOURCE_JPL_DE410,
        taiyin::internal::SPK_SOURCE_JPL_DE408,
        taiyin::internal::SPK_SOURCE_JPL_DE406,
        taiyin::internal::SPK_SOURCE_JPL_DE405,
        taiyin::internal::SPK_SOURCE_JPL_DE403,
        taiyin::internal::SPK_SOURCE_JPL_DE245,
        taiyin::internal::SPK_SOURCE_JPL_DE202,
        taiyin::internal::SPK_SOURCE_JPL_DE200,
        taiyin::internal::SPK_SOURCE_JPL_DE130,
        taiyin::internal::SPK_SOURCE_JPL_DE125,
        taiyin::internal::SPK_SOURCE_JPL_DE118,
        taiyin::internal::SPK_SOURCE_JPL_DE102,
    };
    *out_count = sizeof(sources) / sizeof(sources[0]);
    return sources;
}

const uint64_t* recognized_satellite_sources(size_t* out_count) {
    static const uint64_t sources[] = {
        taiyin::internal::SPK_SOURCE_JPL_MAR099,
        taiyin::internal::SPK_SOURCE_JPL_JUP365,
        taiyin::internal::SPK_SOURCE_JPL_JUP349,
        taiyin::internal::SPK_SOURCE_JPL_JUP348,
        taiyin::internal::SPK_SOURCE_JPL_JUP347,
        taiyin::internal::SPK_SOURCE_JPL_SAT480,
        taiyin::internal::SPK_SOURCE_JPL_SAT459,
        taiyin::internal::SPK_SOURCE_JPL_SAT458,
        taiyin::internal::SPK_SOURCE_JPL_SAT457,
        taiyin::internal::SPK_SOURCE_JPL_SAT456,
        taiyin::internal::SPK_SOURCE_JPL_SAT455,
        taiyin::internal::SPK_SOURCE_JPL_SAT441,
        taiyin::internal::SPK_SOURCE_JPL_SAT415,
        taiyin::internal::SPK_SOURCE_JPL_URA184,
        taiyin::internal::SPK_SOURCE_JPL_URA182,
        taiyin::internal::SPK_SOURCE_JPL_URA117,
        taiyin::internal::SPK_SOURCE_JPL_NEP105,
        taiyin::internal::SPK_SOURCE_JPL_NEP104,
        taiyin::internal::SPK_SOURCE_JPL_NEP098,
        taiyin::internal::SPK_SOURCE_JPL_NEP097,
        taiyin::internal::SPK_SOURCE_JPL_PLU060,
    };
    *out_count = sizeof(sources) / sizeof(sources[0]);
    return sources;
}

void test_all_recognized_de_products_are_reachable_in_auto() {
    taiyin::runtime::Runtime runtime;
    size_t source_count = 0;
    const uint64_t* sources = recognized_de_sources(&source_count);
    for (size_t i = 0; i < source_count; ++i) {
        const int target = 920000 + static_cast<int>(i);
        const std::string path = "/fixtures/de-product-" + std::to_string(i) + ".bsp";
        expect_true(
            runtime.ephemeris_catalog().add(make_descriptor(
                target,
                taiyin::TAIYIN_BODY_SSB,
                taiyin::internal::EphemerisBlockFormat::Spk,
                taiyin::internal::SPK_METHOD_ID,
                sources[i],
                i + 1,
                path.c_str())),
            "add recognized DE descriptor");

        taiyin::internal::EphemerisBlockDescriptor selected;
        expect_true(
            runtime.ephemeris_engine().find_descriptor(
                make_request(target, taiyin::TAIYIN_BODY_SSB), &selected),
            "AUTO reaches every recognized DE product");
        expect_true(selected.source_key.source_id == sources[i], "AUTO keeps DE source identity");
    }
}

void test_all_recognized_satellite_products_are_reachable_in_auto() {
    taiyin::runtime::Runtime runtime;
    const taiyin::internal::EphemerisRouteRuleTable* automatic =
        runtime.ephemeris_route_rule(
            taiyin::runtime::TAIYIN_EPHEMERIS_ROUTE_AUTO);
    expect_true(automatic != 0, "AUTO route table exists");

    size_t source_count = 0;
    const uint64_t* sources = recognized_satellite_sources(&source_count);
    for (size_t i = 0; i < source_count; ++i) {
        bool has_exact_fallback = false;
        const std::vector<taiyin::internal::EphemerisRouteRule>& rules =
            automatic->rules();
        for (size_t rule_index = 0; rule_index < rules.size(); ++rule_index) {
            const taiyin::internal::EphemerisRouteRule& rule = rules[rule_index];
            if (rule.source_id == sources[i]
                && rule.method_id == taiyin::internal::SPK_METHOD_ID
                && !rule.allow_non_de_spk_auxiliary) {
                has_exact_fallback = true;
                break;
            }
        }
        expect_true(
            has_exact_fallback,
            "recognized satellite product has an independent AUTO fallback");

        const int target = 925000 + static_cast<int>(i);
        const std::string path =
            "/fixtures/satellite-product-" + std::to_string(i) + ".bsp";
        expect_true(
            runtime.ephemeris_catalog().add(make_descriptor(
                target,
                taiyin::TAIYIN_BODY_JUPITER,
                taiyin::internal::EphemerisBlockFormat::Spk,
                taiyin::internal::SPK_METHOD_ID,
                sources[i],
                5000 + i,
                path.c_str())),
            "add recognized satellite descriptor");

        taiyin::internal::EphemerisBlockDescriptor selected;
        expect_true(
            runtime.ephemeris_engine().find_descriptor(
                make_request(target, taiyin::TAIYIN_BODY_JUPITER), &selected),
            "AUTO reaches every recognized satellite product directly");
        expect_true(
            selected.source_key.source_id == sources[i],
            "AUTO keeps satellite source identity");
    }
}

void test_named_de_descriptor_filtering_and_route_anchoring() {
    size_t source_count = 0;
    const uint64_t* sources = recognized_de_sources(&source_count);
    for (size_t i = 0; i < source_count; ++i) {
        taiyin::runtime::Runtime runtime;
        const int auxiliary_target = 930000 + static_cast<int>(i) * 2;
        const int mismatched_de_target = auxiliary_target + 1;
        const uint64_t other_de = sources[(i + 1) % source_count];

        expect_true(
            runtime.ephemeris_catalog().add(make_descriptor(
                auxiliary_target,
                taiyin::TAIYIN_BODY_JUPITER,
                taiyin::internal::EphemerisBlockFormat::Spk,
                taiyin::internal::SPK_METHOD_ID,
                taiyin::internal::SPK_SOURCE_JPL_JUP365,
                1000 + i,
                "/fixtures/jup365.bsp")),
            "add non-DE satellite auxiliary");
        expect_true(
            runtime.ephemeris_catalog().add(make_descriptor(
                mismatched_de_target,
                taiyin::TAIYIN_BODY_SSB,
                taiyin::internal::EphemerisBlockFormat::Spk,
                taiyin::internal::SPK_METHOD_ID,
                other_de,
                2000 + i,
                "/fixtures/other-de.bsp")),
            "add mismatched DE descriptor");

        taiyin::internal::EphemerisRouteRuleTable named_de;
        expect_true(
            named_de.upsert_source_method(
                sources[i],
                taiyin::internal::SPK_METHOD_ID,
                100,
                "named DE fixture",
                true),
            "create named DE route");

        taiyin::runtime::EphemerisRequest auxiliary_request =
            make_request(auxiliary_target, taiyin::TAIYIN_BODY_JUPITER);
        auxiliary_request.route_rules = &named_de;
        taiyin::internal::EphemerisBlockDescriptor selected;
        expect_true(
            runtime.ephemeris_engine().find_descriptor(auxiliary_request, &selected),
            "named DE matcher exposes a satellite-system SPK auxiliary");
        expect_true(
            selected.source_key.source_id == taiyin::internal::SPK_SOURCE_JPL_JUP365,
            "satellite auxiliary source is retained");
        const taiyin::internal::EphemerisRouteRule& rule =
            named_de.rules().front();
        expect_false(
            taiyin::internal::ephemeris_route_source_usage_is_anchored(
                rule, false, true),
            "an auxiliary-only named-DE route is rejected");
        expect_true(
            taiyin::internal::ephemeris_route_source_usage_is_anchored(
                rule, true, true),
            "a named-DE route accepts auxiliaries after an exact-source anchor");

        taiyin::runtime::EphemerisRequest mismatch_request =
            make_request(mismatched_de_target, taiyin::TAIYIN_BODY_SSB);
        mismatch_request.route_rules = &named_de;
        expect_false(
            runtime.ephemeris_engine().find_descriptor(mismatch_request, &selected),
            "named DE does not mix with a different DE product");
    }
}

void test_provider_routes_are_isolated() {
    taiyin::runtime::Runtime runtime;
    const int target = 940001;
    expect_true(
        runtime.ephemeris_catalog().add(make_descriptor(
            target,
            taiyin::TAIYIN_BODY_SSB,
            taiyin::internal::EphemerisBlockFormat::Spk,
            taiyin::internal::SPK_METHOD_ID,
            taiyin::internal::SPK_SOURCE_JPL_DE442,
            1,
            "/fixtures/de442.bsp")),
        "add SPK provider fixture");
    expect_true(
        runtime.ephemeris_catalog().add(make_descriptor(
            target,
            taiyin::TAIYIN_BODY_SSB,
            taiyin::internal::EphemerisBlockFormat::Opm2,
            static_cast<int>(taiyin::internal::OPM2_METHOD_ID),
            taiyin::internal::OPM2_SOURCE_TAIYIN_PRERELEASE,
            2,
            "/fixtures/default.opm2")),
        "add OPM2 provider fixture");

    taiyin::runtime::EphemerisRequest request =
        make_request(target, taiyin::TAIYIN_BODY_SSB);
    taiyin::internal::EphemerisBlockDescriptor selected;

    request.route_rules = runtime.ephemeris_route_rule(
        taiyin::runtime::TAIYIN_EPHEMERIS_ROUTE_SPK);
    expect_true(runtime.ephemeris_engine().find_descriptor(request, &selected), "SPK-only route resolves");
    expect_int(selected.method_id, taiyin::internal::SPK_METHOD_ID, "SPK-only route cannot jump to OPM2");

    expect_true(
        runtime.set_ephemeris_source_priority("default.opm2", 10000),
        "promote OPM2 fixture without crossing provider boundary");
    expect_true(runtime.ephemeris_engine().find_descriptor(request, &selected), "SPK-only route remains isolated after OPM2 override");
    expect_int(selected.method_id, taiyin::internal::SPK_METHOD_ID, "OPM2 override cannot enter SPK-only route");

    request.route_rules = runtime.ephemeris_route_rule(
        taiyin::runtime::TAIYIN_EPHEMERIS_ROUTE_OPM2);
    expect_true(runtime.ephemeris_engine().find_descriptor(request, &selected), "OPM2-only route resolves");
    expect_int(
        selected.method_id,
        static_cast<int>(taiyin::internal::OPM2_METHOD_ID),
        "OPM2-only route cannot jump to SPK");
    expect_true(
        runtime.set_ephemeris_source_priority("de442.bsp", 20000),
        "promote SPK fixture without crossing provider boundary");
    expect_true(runtime.ephemeris_engine().find_descriptor(request, &selected), "OPM2-only route remains isolated after SPK override");
    expect_int(
        selected.method_id,
        static_cast<int>(taiyin::internal::OPM2_METHOD_ID),
        "SPK override cannot enter OPM2-only route");
}

void test_shared_spk_centers_map_to_physical_primaries() {
    const int centers[] = {0, 3, 4, 5, 6, 7, 8, 9};
    const int primaries[] = {10, 399, 499, 599, 699, 799, 899, 999};
    for (size_t i = 0; i < sizeof(centers) / sizeof(centers[0]); ++i) {
        expect_int(
            taiyin::internal::spk_physical_primary_for_shared_center(centers[i]),
            primaries[i],
            "shared SPK center maps to the physical primary");
    }
    expect_int(
        taiyin::internal::spk_physical_primary_for_shared_center(1),
        0,
        "unsupported shared center has no synthesis primary");
    expect_int(
        taiyin::internal::spk_physical_primary_for_shared_center(10),
        0,
        "Sun is not treated as a system-barycenter primary");
}

taiyin::internal::CustomEphemerisMethodDefinition make_constant_definition(
    int target_id,
    int center_id,
    const ConstantStateData* data
) {
    taiyin::internal::CustomEphemerisMethodDefinition definition;
    definition.target_id = target_id;
    definition.center_id = center_id;
    definition.method_id = CUSTOM_SSB_METHOD;
    definition.frame = taiyin::internal::IcrfJ2000Equatorial;
    definition.jd_tdb_start = JD0 - 10.0;
    definition.jd_tdb_end = JD0 + 10.0;
    definition.data = data;
    definition.bytes = sizeof(*data);
    definition.position = constant_position;
    definition.velocity = constant_velocity;
    definition.acceleration = constant_acceleration;
    definition.description = "direct SSB fallback fixture";
    return definition;
}

void test_direct_satellite_ssb_route_remains_a_fallback() {
    taiyin::internal::clear_custom_ephemeris_methods();
    {
        taiyin::runtime::Runtime runtime;
        ConstantStateData phobos;
        phobos.position = make_vector(4.0, 5.0, 6.0);
        phobos.velocity = make_vector(0.4, 0.5, 0.6);
        phobos.acceleration = make_vector(0.04, 0.05, 0.06);
        ConstantStateData sun;
        sun.position = make_vector(1.0, 2.0, 3.0);
        sun.velocity = make_vector(0.1, 0.2, 0.3);
        sun.acceleration = make_vector(0.01, 0.02, 0.03);

        taiyin::internal::EphemerisBlockDescriptor phobos_descriptor;
        taiyin::internal::EphemerisBlockDescriptor sun_descriptor;
        expect_true(
            taiyin::internal::register_custom_ephemeris_method(
                make_constant_definition(
                    taiyin::TAIYIN_BODY_PHOBOS,
                    taiyin::TAIYIN_BODY_SSB,
                    &phobos),
                &phobos_descriptor),
            "register direct Phobos/SSB fixture");
        expect_true(
            taiyin::internal::register_custom_ephemeris_method(
                make_constant_definition(
                    taiyin::TAIYIN_BODY_SUN,
                    taiyin::TAIYIN_BODY_SSB,
                    &sun),
                &sun_descriptor),
            "register direct Sun/SSB fixture");
        expect_true(runtime.ephemeris_catalog().add(phobos_descriptor), "catalog direct Phobos/SSB fixture");
        expect_true(runtime.ephemeris_catalog().add(sun_descriptor), "catalog direct Sun/SSB fixture");

        taiyin::internal::EphemerisRouteRuleTable custom_route;
        expect_true(
            custom_route.upsert_source_method(
                phobos_descriptor.source_key.source_id,
                CUSTOM_SSB_METHOD,
                100,
                "direct SSB fixture"),
            "register direct SSB test route");

        taiyin::runtime::EphemerisRequest request =
            make_request(taiyin::TAIYIN_BODY_PHOBOS, taiyin::TAIYIN_BODY_SUN);
        request.route_rules = &custom_route;
        taiyin::runtime::EphemerisResult result;
        taiyin::runtime::EphemerisEvalDiagnostic diagnostic;
        expect_true(
            taiyin::status_ok(runtime.ephemeris_engine().eval_state(
                request, &result, &diagnostic)),
            "direct satellite/SSB route survives failed primary composition");
        expect_near(result.state.position_au.x, 3.0, 1e-15, "direct SSB fallback position x");
        expect_near(result.state.position_au.y, 3.0, 1e-15, "direct SSB fallback position y");
        expect_near(result.state.position_au.z, 3.0, 1e-15, "direct SSB fallback position z");
        expect_near(result.state.velocity_au_per_day.x, 0.3, 1e-15, "direct SSB fallback velocity x");
        expect_near(result.state.acceleration_au_per_day2.z, 0.03, 1e-15, "direct SSB fallback acceleration z");
    }
    taiyin::internal::clear_custom_ephemeris_methods();
}

void test_naif_satellite_id_families_compose_through_physical_primary() {
    taiyin::internal::clear_custom_ephemeris_methods();
    {
        taiyin::runtime::Runtime runtime;
        ConstantStateData jupiter;
        jupiter.position = make_vector(10.0, 20.0, 30.0);
        jupiter.velocity = make_vector(1.0, 2.0, 3.0);
        jupiter.acceleration = make_vector(0.1, 0.2, 0.3);
        taiyin::internal::EphemerisBlockDescriptor jupiter_descriptor;
        expect_true(
            taiyin::internal::register_custom_ephemeris_method(
                make_constant_definition(
                    taiyin::TAIYIN_BODY_JUPITER,
                    taiyin::TAIYIN_BODY_SUN,
                    &jupiter),
                &jupiter_descriptor),
            "register Jupiter/Sun composition fixture");
        expect_true(
            runtime.ephemeris_catalog().add(jupiter_descriptor),
            "catalog Jupiter/Sun composition fixture");

        const int satellite_ids[] = {
            505,    // Standard PNN: Amalthea.
            50099,  // Extended permanent P0NNN form.
            55063,  // Provisional P5NNN form.
        };
        ConstantStateData satellite_states[3];
        for (size_t i = 0; i < sizeof(satellite_ids) / sizeof(satellite_ids[0]); ++i) {
            const double scale = static_cast<double>(i + 1);
            satellite_states[i].position = make_vector(scale, 2.0 * scale, 3.0 * scale);
            satellite_states[i].velocity = make_vector(
                0.1 * scale, 0.2 * scale, 0.3 * scale);
            satellite_states[i].acceleration = make_vector(
                0.01 * scale, 0.02 * scale, 0.03 * scale);
            taiyin::internal::EphemerisBlockDescriptor satellite_descriptor;
            expect_true(
                taiyin::internal::register_custom_ephemeris_method(
                    make_constant_definition(
                        satellite_ids[i],
                        taiyin::TAIYIN_BODY_JUPITER,
                        &satellite_states[i]),
                    &satellite_descriptor),
                "register numeric satellite/Jupiter fixture");
            expect_true(
                runtime.ephemeris_catalog().add(satellite_descriptor),
                "catalog numeric satellite/Jupiter fixture");
        }

        taiyin::internal::EphemerisRouteRuleTable custom_route;
        expect_true(
            custom_route.upsert_source_method(
                0,
                CUSTOM_SSB_METHOD,
                100,
                "numeric NAIF satellite composition fixture"),
            "register numeric satellite composition route");

        for (size_t i = 0; i < sizeof(satellite_ids) / sizeof(satellite_ids[0]); ++i) {
            taiyin::runtime::EphemerisRequest request =
                make_request(satellite_ids[i], taiyin::TAIYIN_BODY_SUN);
            request.route_rules = &custom_route;
            taiyin::runtime::EphemerisResult result;
            taiyin::runtime::EphemerisEvalDiagnostic diagnostic;
            expect_true(
                taiyin::status_ok(runtime.ephemeris_engine().eval_state(
                    request, &result, &diagnostic)),
                "numeric NAIF satellite composes through its physical primary");
            expect_near(
                result.state.position_au.x,
                jupiter.position.x + satellite_states[i].position.x,
                1e-15,
                "numeric satellite composite position x");
            expect_near(
                result.state.velocity_au_per_day.y,
                jupiter.velocity.y + satellite_states[i].velocity.y,
                1e-15,
                "numeric satellite composite velocity y");
            expect_near(
                result.state.acceleration_au_per_day2.z,
                jupiter.acceleration.z + satellite_states[i].acceleration.z,
                1e-15,
                "numeric satellite composite acceleration z");
        }
    }
    taiyin::internal::clear_custom_ephemeris_methods();
}

}  // namespace

int main() {
    test_all_recognized_de_products_are_reachable_in_auto();
    test_all_recognized_satellite_products_are_reachable_in_auto();
    test_named_de_descriptor_filtering_and_route_anchoring();
    test_provider_routes_are_isolated();
    test_shared_spk_centers_map_to_physical_primaries();
    test_direct_satellite_ssb_route_remains_a_fallback();
    test_naif_satellite_id_families_compose_through_physical_primary();
    return 0;
}
