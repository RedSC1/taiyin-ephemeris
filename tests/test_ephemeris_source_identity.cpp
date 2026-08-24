#include "taiyin/internal/ephemeris_source_identity.h"
#include "taiyin/internal/ephemeris_source_priority.h"
#include "taiyin/internal/opm2.h"
#include "taiyin/internal/spk_catalog_discovery.h"
#include "taiyin/body_id.h"
#include "taiyin/runtime/ephemeris_route.h"
#include "taiyin/runtime/runtime.h"

#include <cassert>
#include <cstdio>
#include <cstdint>
#include <vector>

namespace {

void expect_source(uint64_t actual, uint64_t expected, const char* label) {
    if (actual != expected) {
        std::fprintf(
            stderr,
            "expected source 0x%llx got 0x%llx: %s\n",
            static_cast<unsigned long long>(expected),
            static_cast<unsigned long long>(actual),
            label);
        assert(false);
    }
}

void expect_int(int actual, int expected, const char* label) {
    if (actual != expected) {
        std::fprintf(stderr, "expected %d got %d: %s\n", expected, actual, label);
        assert(false);
    }
}

void expect_less(int actual, const char* label) {
    if (actual >= 0) {
        std::fprintf(stderr, "expected preferred candidate: %s\n", label);
        assert(false);
    }
}

}  // namespace

int main() {
    using namespace taiyin::internal;

    expect_source(
        classify_spk_source_id_from_path("/data/jpl/de442.bsp"),
        SPK_SOURCE_JPL_DE442,
        "DE442 BSP basename");
    expect_source(
        classify_spk_source_id_from_path("C:\\ephemerides\\DE441.SPK"),
        SPK_SOURCE_JPL_DE441,
        "case-insensitive DE441 SPK basename");
    expect_source(
        classify_spk_source_id_from_path("/data/custom/renamed-de441.bsp"),
        SPK_SOURCE_EXTERNAL,
        "unrecognized SPK remains external");
    expect_source(
        classify_spk_source_id_from_path("/data/satellites/mar099s.bsp"),
        SPK_SOURCE_JPL_MAR099,
        "MAR099 compact variant keeps the MAR099 model identity");
    expect_source(
        classify_spk_source_id_from_path("/data/satellites/nep098_part-2.bsp"),
        SPK_SOURCE_JPL_NEP098,
        "NEP098 part keeps the NEP098 model identity");

    expect_source(
        normalize_opm2_source_id(OPM2_SOURCE_UNDEFINED),
        OPM2_SOURCE_TAIYIN_DE441_DERIVED,
        "old zero-valued OPM2 headers remain compatible");
    expect_source(
        OPM2_SOURCE_TAIYIN_PRERELEASE,
        OPM2_SOURCE_TAIYIN_DE441_DERIVED,
        "pre-release OPM2 source name remains a compatibility alias");
    expect_source(
        normalize_opm2_source_id(OPM2_SOURCE_TAIYIN_DE442_REBUILT),
        OPM2_SOURCE_TAIYIN_DE442_REBUILT,
        "assigned OPM2 product ids are preserved");

    taiyin::runtime::Runtime runtime;
    const taiyin::internal::EphemerisRouteRuleTable* automatic =
        runtime.ephemeris_route_rule(taiyin::runtime::TAIYIN_EPHEMERIS_ROUTE_AUTO);
    assert(automatic != 0);
    assert(automatic->rules().size() >= 9);
    if (automatic && automatic->rules().size() >= 9) {
        const std::vector<taiyin::internal::EphemerisRouteRule>& rules = automatic->rules();
        expect_source(rules[0].source_id, SPK_SOURCE_JPL_DE442, "DE442 SPK has highest AUTO priority");
        expect_int(rules[0].method_id, SPK_METHOD_ID, "DE442 SPK method");
        expect_source(rules[1].source_id, OPM2_SOURCE_TAIYIN_DE442_REBUILT, "rebuilt DE442 OPM2 outranks DE441 SPK");
        expect_int(rules[1].method_id, static_cast<int>(OPM2_METHOD_ID), "rebuilt DE442 OPM2 method");
        expect_source(rules[2].source_id, SPK_SOURCE_JPL_DE441, "DE441 SPK follows DE442");

        const uint64_t expected_de_sources[] = {
            SPK_SOURCE_JPL_DE442, SPK_SOURCE_JPL_DE441,
            SPK_SOURCE_JPL_DE440, SPK_SOURCE_JPL_DE438,
            SPK_SOURCE_JPL_DE435, SPK_SOURCE_JPL_DE432,
            SPK_SOURCE_JPL_DE431, SPK_SOURCE_JPL_DE430,
            SPK_SOURCE_JPL_DE423, SPK_SOURCE_JPL_DE421,
            SPK_SOURCE_JPL_DE418, SPK_SOURCE_JPL_DE414,
            SPK_SOURCE_JPL_DE413, SPK_SOURCE_JPL_DE410,
            SPK_SOURCE_JPL_DE408, SPK_SOURCE_JPL_DE406,
            SPK_SOURCE_JPL_DE405, SPK_SOURCE_JPL_DE403,
            SPK_SOURCE_JPL_DE245, SPK_SOURCE_JPL_DE202,
            SPK_SOURCE_JPL_DE200, SPK_SOURCE_JPL_DE130,
            SPK_SOURCE_JPL_DE125, SPK_SOURCE_JPL_DE118,
            SPK_SOURCE_JPL_DE102,
        };
        for (size_t source_index = 0;
             source_index < sizeof(expected_de_sources) / sizeof(expected_de_sources[0]);
             ++source_index) {
            bool found = false;
            for (size_t rule_index = 0; rule_index < rules.size(); ++rule_index) {
                if (rules[rule_index].source_id == expected_de_sources[source_index]
                    && rules[rule_index].method_id == SPK_METHOD_ID
                    && rules[rule_index].allow_non_de_spk_auxiliary) {
                    found = true;
                    break;
                }
            }
            assert(found);
        }
    }

    EphemerisBlockDescriptor de441;
    de441.format = EphemerisBlockFormat::Spk;
    de441.path = "/data/de441.bsp";
    de441.source_key.source_id = SPK_SOURCE_JPL_DE441;
    EphemerisBlockDescriptor de442;
    de442.format = EphemerisBlockFormat::Spk;
    de442.path = "/data/de442.bsp";
    de442.source_key.source_id = SPK_SOURCE_JPL_DE442;
    EphemerisBlockDescriptor jup365;
    jup365.format = EphemerisBlockFormat::Spk;
    jup365.path = "/data/jup365.bsp";
    jup365.source_key.source_id = SPK_SOURCE_JPL_JUP365;
    EphemerisBlockDescriptor jup349;
    jup349.format = EphemerisBlockFormat::Spk;
    jup349.path = "/data/jup349.bsp";
    jup349.source_key.source_id = SPK_SOURCE_JPL_JUP349;
    EphemerisBlockDescriptor asteroid;
    asteroid.format = EphemerisBlockFormat::Spk;
    asteroid.path = "/data/2000001.bsp";

    EphemerisBlockDescriptor opm_prerelease;
    opm_prerelease.format = EphemerisBlockFormat::Opm2;
    opm_prerelease.path = "/data/prerelease.opm2";
    opm_prerelease.source_key.source_id = OPM2_SOURCE_TAIYIN_PRERELEASE;
    EphemerisBlockDescriptor opm_de442 = opm_prerelease;
    opm_de442.path = "/data/de442-rebuilt.opm2";
    opm_de442.source_key.source_id = OPM2_SOURCE_TAIYIN_DE442_REBUILT;

    EphemerisSourcePriorityTable priorities;
    expect_less(
        priorities.compare(de442, de441),
        "higher DE series wins by default");
    expect_int(
        priorities.compare(jup365, de442),
        1,
        "DE442 remains ahead of a satellite product when both are direct candidates");
    expect_less(
        priorities.compare(jup365, jup349),
        "JUP365 has a deliberate default model priority over JUP349");
    expect_int(
        priorities.compare(asteroid, de442),
        1,
        "unknown asteroid source does not receive inferred priority over DE442");
    expect_less(
        priorities.compare(opm_de442, opm_prerelease),
        "OPM2 provider uses the greater header source id by default");
    assert(priorities.set_path_priority("de442-rebuilt.opm2", 0));
    expect_less(
        priorities.compare(opm_prerelease, opm_de442),
        "an explicit zero places OPM2 below the prerelease source id");
    assert(priorities.clear_path_priority("de442-rebuilt.opm2"));
    expect_less(
        priorities.compare(opm_de442, opm_prerelease),
        "clearing the OPM2 override restores its header source id");
    assert(priorities.set_path_priority("jup349.bsp", 600));
    expect_less(
        priorities.compare(jup365, jup349),
        "an explicit priority can place a file below the built-in model");
    assert(priorities.set_path_priority("jup349.bsp", 900));
    expect_less(
        priorities.compare(jup349, jup365),
        "an updated explicit priority can place a file above the built-in model");
    assert(priorities.clear_path_priority("jup349.bsp"));
    expect_less(
        priorities.compare(jup365, jup349),
        "clearing one explicit priority restores the built-in model table");
    assert(priorities.set_path_priority("/data/de442.bsp", 900));
    expect_less(
        priorities.compare(de442, jup365),
        "exact-path priority overrides basename priority");
#if !defined(_WIN32)
    EphemerisBlockDescriptor upper_case_path = de442;
    upper_case_path.path = "/data/DE442.bsp";
    EphemerisBlockDescriptor lower_case_path = de442;
    lower_case_path.path = "/data/de442.bsp";
    EphemerisSourcePriorityTable case_sensitive_priorities;
    assert(case_sensitive_priorities.set_path_priority("/data/DE442.bsp", 700));
    expect_less(
        case_sensitive_priorities.compare(lower_case_path, upper_case_path),
        "POSIX exact-path priorities preserve case-distinct files");
#endif

    EphemerisBlockDescriptor route_jup348;
    route_jup348.target_id = taiyin::TAIYIN_BODY_JUPITER_BARYCENTER;
    route_jup348.center_id = taiyin::TAIYIN_BODY_SSB;
    route_jup348.method_id = SPK_METHOD_ID;
    route_jup348.frame = EphemerisFrame::IcrfJ2000Equatorial;
    route_jup348.format = EphemerisBlockFormat::Spk;
    route_jup348.jd_tdb_start = 2450000.0;
    route_jup348.jd_tdb_end = 2460000.0;
    route_jup348.path = "/data/jup348.bsp";
    route_jup348.source_key.source_id = SPK_SOURCE_JPL_JUP348;
    route_jup348.source_key.block_id = 1;
    route_jup348.source_key.generation = 1;
    EphemerisBlockDescriptor route_jup349 = route_jup348;
    route_jup349.path = "/data/jup349.bsp";
    route_jup349.source_key.source_id = SPK_SOURCE_JPL_JUP349;
    route_jup349.source_key.block_id = 2;

    taiyin::runtime::Runtime selection_runtime;
    assert(selection_runtime.ephemeris_catalog().add(route_jup348));
    assert(selection_runtime.ephemeris_catalog().add(route_jup349));
    taiyin::runtime::EphemerisRequest selection_request;
    selection_request.target_id = route_jup348.target_id;
    selection_request.center_id = route_jup348.center_id;
    selection_request.frame = route_jup348.frame;
    selection_request.jd_tdb = taiyin::SplitJulianDate(2451545, 0.0);
    EphemerisBlockDescriptor selected;
    assert(selection_runtime.ephemeris_engine().find_descriptor(selection_request, &selected));
    assert(selected.path == route_jup349.path);
    assert(selection_runtime.set_ephemeris_source_priority("jup349.bsp", 600));
    assert(selection_runtime.ephemeris_engine().find_descriptor(selection_request, &selected));
    assert(selected.path == route_jup348.path);
    assert(selection_runtime.set_ephemeris_source_priority("jup349.bsp", 800));
    assert(selection_runtime.ephemeris_engine().find_descriptor(selection_request, &selected));
    assert(selected.path == route_jup349.path);
    assert(selection_runtime.clear_ephemeris_source_priority("jup349.bsp"));
    assert(selection_runtime.ephemeris_engine().find_descriptor(selection_request, &selected));
    assert(selected.path == route_jup349.path);
    assert(selection_runtime.set_ephemeris_source_priority("jup349.bsp", 600));
    selection_runtime.clear_all_ephemeris_source_priorities();
    assert(selection_runtime.ephemeris_engine().find_descriptor(selection_request, &selected));
    assert(selected.path == route_jup349.path);

    // DE products cannot both enter one named-DE rule, so this verifies that
    // path overrides reorder the product rules themselves rather than merely
    // sorting candidates inside the first rule.
    EphemerisBlockDescriptor route_de442 = route_jup348;
    route_de442.path = "/data/de442.bsp";
    route_de442.source_key.source_id = SPK_SOURCE_JPL_DE442;
    route_de442.source_key.block_id = 3;
    EphemerisBlockDescriptor route_de441 = route_de442;
    route_de441.path = "/data/de441.bsp";
    route_de441.source_key.source_id = SPK_SOURCE_JPL_DE441;
    route_de441.source_key.block_id = 4;
    taiyin::runtime::Runtime de_runtime;
    assert(de_runtime.ephemeris_catalog().add(route_de442));
    assert(de_runtime.ephemeris_catalog().add(route_de441));
    assert(de_runtime.ephemeris_engine().find_descriptor(selection_request, &selected));
    assert(selected.path == route_de442.path);
    assert(de_runtime.set_ephemeris_source_priority("de442.bsp", 900));
    assert(de_runtime.ephemeris_engine().find_descriptor(selection_request, &selected));
    assert(selected.path == route_de441.path);
    assert(de_runtime.set_ephemeris_source_priority("de442.bsp", 1200));
    assert(de_runtime.ephemeris_engine().find_descriptor(selection_request, &selected));
    assert(selected.path == route_de442.path);

    EphemerisBlockDescriptor jup365_a = route_jup348;
    jup365_a.path = "/data/a/jup365.bsp";
    jup365_a.source_key.source_id = SPK_SOURCE_JPL_JUP365;
    jup365_a.source_key.block_id = 5;
    EphemerisBlockDescriptor jup365_b = jup365_a;
    jup365_b.path = "/data/b/jup365.bsp";
    jup365_b.source_key.block_id = 6;
    EphemerisBlockDescriptor competing_jup349 = route_jup349;
    competing_jup349.source_key.block_id = 7;
    taiyin::runtime::Runtime duplicate_runtime;
    assert(duplicate_runtime.ephemeris_catalog().add(jup365_a));
    assert(duplicate_runtime.ephemeris_catalog().add(jup365_b));
    assert(duplicate_runtime.ephemeris_catalog().add(competing_jup349));
    taiyin::internal::EphemerisRouteRuleTable product_rules;
    assert(product_rules.upsert_source_method(
        SPK_SOURCE_JPL_JUP365, SPK_METHOD_ID, 200, "JUP365"));
    assert(product_rules.upsert_source_method(
        SPK_SOURCE_JPL_JUP349, SPK_METHOD_ID, 100, "JUP349"));
    taiyin::runtime::EphemerisRequest duplicate_request = selection_request;
    duplicate_request.route_rules = &product_rules;
    assert(duplicate_runtime.set_ephemeris_source_priority(
        "/data/a/jup365.bsp", 600));
    assert(duplicate_runtime.ephemeris_engine().find_descriptor(
        duplicate_request, &selected));
    assert(selected.path == jup365_b.path);
    assert(duplicate_runtime.set_ephemeris_source_priority(
        "/data/b/jup365.bsp", 600));
    assert(duplicate_runtime.ephemeris_engine().find_descriptor(
        duplicate_request, &selected));
    assert(selected.path == competing_jup349.path);

    EphemerisBlockDescriptor unrelated_opm = opm_de442;
    unrelated_opm.target_id = taiyin::TAIYIN_BODY_MARS;
    unrelated_opm.center_id = taiyin::TAIYIN_BODY_SUN;
    unrelated_opm.jd_tdb_start = 2450000.0;
    unrelated_opm.jd_tdb_end = 2460000.0;
    unrelated_opm.source_key.block_id = 8;
    assert(duplicate_runtime.ephemeris_catalog().add(unrelated_opm));
    EphemerisRouteRuleTable provider_isolation_rules;
    assert(provider_isolation_rules.upsert_source_method(
        OPM2_SOURCE_TAIYIN_DE442_REBUILT,
        static_cast<int>(OPM2_METHOD_ID),
        300,
        "unrelated OPM2"));
    assert(provider_isolation_rules.upsert_source_method(
        SPK_SOURCE_JPL_JUP349, SPK_METHOD_ID, 200, "preferred JUP349"));
    assert(provider_isolation_rules.upsert_source_method(
        SPK_SOURCE_JPL_JUP365, SPK_METHOD_ID, 100, "fallback JUP365"));
    duplicate_request.route_rules = &provider_isolation_rules;
    assert(duplicate_runtime.set_ephemeris_source_priority(
        unrelated_opm.path.c_str(), 1));
    assert(duplicate_runtime.ephemeris_engine().find_descriptor(
        duplicate_request, &selected));
    assert(selected.path == competing_jup349.path);

    return 0;
}
