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

    expect_body_id("solar_system_barycenter", 0);
    expect_body_id("ssb", 0);
    expect_eq_string(query_celestial_body_name(0), "solar_system_barycenter", "ssb canonical name");

    expect_body_id("sun", 10);
    expect_eq_string(query_celestial_body_name(10), "sun", "sun canonical name");

    expect_body_id("mercury_barycenter", 1);
    expect_body_id("venus_barycenter", 2);
    expect_body_id("earth_moon_barycenter", 3);
    expect_body_id("emb", 3);
    expect_body_id("mars_barycenter", 4);
    expect_body_id("jupiter_barycenter", 5);
    expect_body_id("saturn_barycenter", 6);
    expect_body_id("uranus_barycenter", 7);
    expect_body_id("neptune_barycenter", 8);
    expect_body_id("pluto_barycenter", 9);
    expect_eq_string(query_celestial_body_name(3), "earth_moon_barycenter", "emb canonical name");

    expect_body_id("mercury", 199);
    expect_body_id("venus", 299);
    expect_body_id("moon", 301);
    expect_body_id("luna", 301);
    expect_body_id("earth", 399);
    expect_body_id("mars", 499);
    expect_body_id("jupiter", 599);
    expect_body_id("saturn", 699);
    expect_body_id("uranus", 799);
    expect_body_id("neptune", 899);
    expect_body_id("pluto", 999);
    expect_eq_string(query_celestial_body_name(399), "earth", "earth canonical name");
    expect_eq_string(query_celestial_body_name(301), "moon", "moon canonical name");

    expect_body_id("phobos", 401);
    expect_body_id("deimos", 402);
    expect_body_id("io", 501);
    expect_body_id("europa", 502);
    expect_body_id("ganymede", 503);
    expect_body_id("callisto", 504);
    expect_body_id("titan", 606);
    expect_body_id("triton", 801);
    expect_body_id("charon", 901);

    expect_body_id("ceres", 2000001);
    expect_body_id("pallas", 2000002);
    expect_body_id("juno", 2000003);
    expect_body_id("vesta", 2000004);
    expect_body_id("eros", 2000433);
    expect_body_id("chiron", 20002060);
    expect_body_id("pholus", 20005145);
    expect_body_id("nessus", 20007066);
    expect_body_id("lilith", 20001181);
    expect_eq_string(query_celestial_body_name(2000001), "ceres", "ceres canonical name");

    expect_eq_int(register_celestial_body("earth"), 399, "register built-in earth");
    expect_eq_int(register_celestial_body("mars_barycenter"), 4, "register built-in mars barycenter");
    expect_eq_int(register_celestial_body("ceres"), 2000001, "register built-in ceres");

    const int dynamic_a = register_celestial_body("registry_test_custom_star_a");
    const int dynamic_b = register_celestial_body("registry_test_custom_star_b");
    expect_true(dynamic_a >= 1000000000, "dynamic private id range a");
    expect_true(dynamic_b >= 1000000000, "dynamic private id range b");
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

    register_celestial_body_alias("registry_test_earth_alias", 399);
    expect_eq_int(register_celestial_body("registry_test_earth_alias"), 399, "earth alias resolves");
    expect_eq_string(query_celestial_body_name(399), "earth", "earth alias does not replace canonical name");

    int unknown_id = -1;
    expect_false(query_celestial_body_id("registry_test_unknown_body", &unknown_id), "unknown body lookup");
    expect_false(query_celestial_body_id("earth", 0), "null output lookup");
    expect_eq_string(query_celestial_body_name(-123456789), "", "unknown reverse lookup");

    return 0;
}
