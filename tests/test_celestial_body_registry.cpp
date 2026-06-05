#include "taiyin/body_id.h"
#include "taiyin/internal/ephemeris_block.h"

#include <cassert>
#include <cstdio>
#include <string>

namespace {

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

void expect_eq_int(int actual, int expected, const char* label) {
    if (actual != expected) {
        std::fprintf(stderr, "%s mismatch: actual %d expected %d\n", label, actual, expected);
        assert(false);
    }
}

void expect_eq_string(const std::string& actual, const std::string& expected, const char* label) {
    if (actual != expected) {
        std::fprintf(
            stderr,
            "%s mismatch: actual %s expected %s\n",
            label,
            actual.c_str(),
            expected.c_str());
        assert(false);
    }
}

void expect_body_id(const char* name, int expected_id) {
    int id = 0;
    expect_true(taiyin::internal::query_celestial_body_id(name, &id), name);
    expect_eq_int(id, expected_id, name);
}

}  // namespace

int main() {
    using taiyin::internal::query_celestial_body_id;
    using taiyin::internal::query_celestial_body_name;
    using taiyin::internal::register_celestial_body;
    using taiyin::internal::register_celestial_body_alias;

    expect_body_id("solar_system_barycenter", taiyin::TAIYIN_BODY_SOLAR_SYSTEM_BARYCENTER);
    expect_body_id("ssb", taiyin::TAIYIN_BODY_SSB);
    expect_eq_string(query_celestial_body_name(taiyin::TAIYIN_BODY_SSB), "solar_system_barycenter", "ssb canonical name");

    expect_body_id("sun", taiyin::TAIYIN_BODY_SUN);
    expect_eq_string(query_celestial_body_name(taiyin::TAIYIN_BODY_SUN), "sun", "sun canonical name");

    expect_body_id("mercury_barycenter", taiyin::TAIYIN_BODY_MERCURY_BARYCENTER);
    expect_body_id("venus_barycenter", taiyin::TAIYIN_BODY_VENUS_BARYCENTER);
    expect_body_id("earth_moon_barycenter", taiyin::TAIYIN_BODY_EARTH_MOON_BARYCENTER);
    expect_body_id("emb", taiyin::TAIYIN_BODY_EMB);
    expect_body_id("mars_barycenter", taiyin::TAIYIN_BODY_MARS_BARYCENTER);
    expect_body_id("jupiter_barycenter", taiyin::TAIYIN_BODY_JUPITER_BARYCENTER);
    expect_body_id("saturn_barycenter", taiyin::TAIYIN_BODY_SATURN_BARYCENTER);
    expect_body_id("uranus_barycenter", taiyin::TAIYIN_BODY_URANUS_BARYCENTER);
    expect_body_id("neptune_barycenter", taiyin::TAIYIN_BODY_NEPTUNE_BARYCENTER);
    expect_body_id("pluto_barycenter", taiyin::TAIYIN_BODY_PLUTO_BARYCENTER);
    expect_eq_string(query_celestial_body_name(taiyin::TAIYIN_BODY_EMB), "earth_moon_barycenter", "emb canonical name");

    expect_body_id("mercury", taiyin::TAIYIN_BODY_MERCURY);
    expect_body_id("venus", taiyin::TAIYIN_BODY_VENUS);
    expect_body_id("moon", taiyin::TAIYIN_BODY_MOON);
    expect_body_id("luna", taiyin::TAIYIN_BODY_MOON);
    expect_body_id("earth", taiyin::TAIYIN_BODY_EARTH);
    expect_body_id("mars", taiyin::TAIYIN_BODY_MARS);
    expect_body_id("jupiter", taiyin::TAIYIN_BODY_JUPITER);
    expect_body_id("saturn", taiyin::TAIYIN_BODY_SATURN);
    expect_body_id("uranus", taiyin::TAIYIN_BODY_URANUS);
    expect_body_id("neptune", taiyin::TAIYIN_BODY_NEPTUNE);
    expect_body_id("pluto", taiyin::TAIYIN_BODY_PLUTO);
    expect_eq_string(query_celestial_body_name(taiyin::TAIYIN_BODY_EARTH), "earth", "earth canonical name");
    expect_eq_string(query_celestial_body_name(taiyin::TAIYIN_BODY_MOON), "moon", "moon canonical name");

    expect_body_id("phobos", taiyin::TAIYIN_BODY_PHOBOS);
    expect_body_id("deimos", taiyin::TAIYIN_BODY_DEIMOS);
    expect_body_id("io", taiyin::TAIYIN_BODY_IO);
    expect_body_id("europa", taiyin::TAIYIN_BODY_EUROPA);
    expect_body_id("ganymede", taiyin::TAIYIN_BODY_GANYMEDE);
    expect_body_id("callisto", taiyin::TAIYIN_BODY_CALLISTO);
    expect_body_id("titan", taiyin::TAIYIN_BODY_TITAN);
    expect_body_id("triton", taiyin::TAIYIN_BODY_TRITON);
    expect_body_id("charon", taiyin::TAIYIN_BODY_CHARON);

    expect_body_id("ceres", taiyin::TAIYIN_BODY_CERES);
    expect_body_id("pallas", taiyin::TAIYIN_BODY_PALLAS);
    expect_body_id("juno", taiyin::TAIYIN_BODY_JUNO);
    expect_body_id("vesta", taiyin::TAIYIN_BODY_VESTA);
    expect_body_id("eros", taiyin::TAIYIN_BODY_EROS);
    expect_body_id("chiron", taiyin::TAIYIN_BODY_CHIRON);
    expect_body_id("pholus", taiyin::TAIYIN_BODY_PHOLUS);
    expect_body_id("nessus", taiyin::TAIYIN_BODY_NESSUS);
    expect_body_id("lilith", taiyin::TAIYIN_BODY_LILITH);
    expect_eq_string(query_celestial_body_name(taiyin::TAIYIN_BODY_CERES), "ceres", "ceres canonical name");

    expect_eq_int(register_celestial_body("earth"), taiyin::TAIYIN_BODY_EARTH, "register built-in earth");
    expect_eq_int(register_celestial_body("mars_barycenter"), taiyin::TAIYIN_BODY_MARS_BARYCENTER, "register built-in mars barycenter");
    expect_eq_int(register_celestial_body("ceres"), taiyin::TAIYIN_BODY_CERES, "register built-in ceres");

    const int dynamic_a = register_celestial_body("registry_test_custom_star_a");
    const int dynamic_b = register_celestial_body("registry_test_custom_star_b");
    expect_true(dynamic_a >= taiyin::TAIYIN_PRIVATE_CELESTIAL_BODY_ID_START, "dynamic private id range a");
    expect_true(dynamic_b >= taiyin::TAIYIN_PRIVATE_CELESTIAL_BODY_ID_START, "dynamic private id range b");
    expect_true(dynamic_a != dynamic_b, "dynamic ids are distinct");
    expect_eq_int(
        register_celestial_body("registry_test_custom_star_a"),
        dynamic_a,
        "dynamic id is stable");
    expect_eq_string(
        query_celestial_body_name(dynamic_a),
        "registry_test_custom_star_a",
        "dynamic canonical name");

    register_celestial_body_alias("registry_test_custom_star_alias", dynamic_a);
    expect_eq_int(
        register_celestial_body("registry_test_custom_star_alias"),
        dynamic_a,
        "dynamic alias resolves");
    expect_eq_string(
        query_celestial_body_name(dynamic_a),
        "registry_test_custom_star_a",
        "dynamic alias does not replace canonical name");

    register_celestial_body_alias("registry_test_earth_alias", taiyin::TAIYIN_BODY_EARTH);
    expect_eq_int(register_celestial_body("registry_test_earth_alias"), taiyin::TAIYIN_BODY_EARTH, "earth alias resolves");
    expect_eq_string(query_celestial_body_name(taiyin::TAIYIN_BODY_EARTH), "earth", "earth alias does not replace canonical name");

    int unknown_id = -1;
    expect_false(query_celestial_body_id("registry_test_unknown_body", &unknown_id), "unknown body lookup");
    expect_false(query_celestial_body_id("earth", 0), "null output lookup");
    expect_eq_string(query_celestial_body_name(-123456789), "", "unknown reverse lookup");

    return 0;
}
