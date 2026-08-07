#include "runtime/visibility/visibility_math_internal.h"

#include "taiyin/angle.h"
#include "taiyin/runtime/solar_visibility.h"

#include <cmath>
#include <iostream>

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

void test_sign_and_crossing(int* failures) {
    using namespace taiyin::runtime;
    expect_equal(visibility_sign(0.2, 0.1), TAIYIN_VISIBILITY_SIGN_POSITIVE, "positive sign", failures);
    expect_equal(visibility_sign(-0.2, 0.1), TAIYIN_VISIBILITY_SIGN_NEGATIVE, "negative sign", failures);
    expect_equal(visibility_sign(0.05, 0.1), TAIYIN_VISIBILITY_SIGN_ZERO, "zero sign within tolerance", failures);
    expect_equal(visibility_sign(std::nan(""), 0.1), TAIYIN_VISIBILITY_SIGN_INVALID, "nan sign invalid", failures);
    expect_equal(visibility_sign(1.0, -0.1), TAIYIN_VISIBILITY_SIGN_INVALID, "negative tolerance invalid", failures);
    expect_true(visibility_has_crossing(-1.0, 1.0, 0.0), "opposite sign crossing", failures);
    expect_true(visibility_has_crossing(0.0, 1.0, 0.0), "endpoint crossing", failures);
    expect_true(!visibility_has_crossing(1.0, 2.0, 0.0), "same sign no crossing", failures);
    expect_true(!visibility_has_crossing(std::nan(""), 2.0, 0.0), "invalid value no crossing", failures);
}

void test_roots_and_vertices(int* failures) {
    using namespace taiyin::runtime;
    expect_near(visibility_linear_root_time(10.0, 2.0, 14.0, -2.0), 12.0, 1.0e-15, "linear root", failures);
    expect_true(std::isnan(visibility_linear_root_time(10.0, 1.0, 14.0, 1.0)), "linear flat root invalid", failures);
    expect_near(
        visibility_quadratic_vertex_time(9.0, 1.0, 10.0, 0.0, 11.0, 1.0),
        10.0,
        1.0e-15,
        "quadratic vertex centered",
        failures);
    expect_near(
        visibility_quadratic_vertex_time(9.0, 4.0, 10.0, 1.0, 11.0, 0.0),
        11.0,
        1.0e-15,
        "quadratic vertex offset",
        failures);
    expect_near(
        visibility_quadratic_vertex_time(9.75, 0.1225, 10.0, 0.01, 10.2, 0.01),
        10.1,
        1.0e-15,
        "quadratic vertex unequal steps",
        failures);
}

void test_classification(int* failures) {
    using namespace taiyin::runtime;
    expect_equal(
        visibility_classify_altitude_range(0.1, 0.5, 0.01),
        TAIYIN_VISIBILITY_ALTITUDE_STATE_ALWAYS_ABOVE,
        "always above",
        failures);
    expect_equal(
        visibility_classify_altitude_range(-0.5, -0.1, 0.01),
        TAIYIN_VISIBILITY_ALTITUDE_STATE_ALWAYS_BELOW,
        "always below",
        failures);
    expect_equal(
        visibility_classify_altitude_range(-0.5, 0.5, 0.01),
        TAIYIN_VISIBILITY_ALTITUDE_STATE_CROSSES,
        "crosses",
        failures);
    expect_equal(
        visibility_classify_altitude_range(0.0, 0.5, 0.01),
        TAIYIN_VISIBILITY_ALTITUDE_STATE_TANGENT,
        "tangent lower bound",
        failures);
    expect_equal(
        visibility_classify_altitude_range(-0.5, 0.0, 0.01),
        TAIYIN_VISIBILITY_ALTITUDE_STATE_TANGENT,
        "tangent upper bound",
        failures);
    expect_equal(
        visibility_classify_altitude_range(1.0, -1.0, 0.01),
        TAIYIN_VISIBILITY_ALTITUDE_STATE_NOT_FOUND,
        "invalid range",
        failures);
}

void test_solar_constants(int* failures) {
    using namespace taiyin;
    using namespace taiyin::runtime;
    expect_near(
        visibility_solar_twilight_altitude_rad(TAIYIN_SOLAR_VISIBILITY_TWILIGHT_CIVIL),
        -6.0 * TAIYIN_DEG_TO_RAD,
        0.0,
        "civil twilight altitude",
        failures);
    expect_near(
        visibility_solar_twilight_altitude_rad(TAIYIN_SOLAR_VISIBILITY_TWILIGHT_NAUTICAL),
        -12.0 * TAIYIN_DEG_TO_RAD,
        0.0,
        "nautical twilight altitude",
        failures);
    expect_near(
        visibility_solar_twilight_altitude_rad(TAIYIN_SOLAR_VISIBILITY_TWILIGHT_ASTRONOMICAL),
        -18.0 * TAIYIN_DEG_TO_RAD,
        0.0,
        "astronomical twilight altitude",
        failures);
    expect_true(std::isnan(visibility_solar_twilight_altitude_rad(99)), "invalid twilight altitude", failures);
    expect_near(
        visibility_angular_radius_rad(695700.0, 1.0),
        std::asin(695700.0 / 149597870.7),
        1.0e-15,
        "solar angular radius 1 AU",
        failures);
    expect_near(
        visibility_angular_radius_rad(1737.4, 0.00257),
        std::asin(1737.4 / (0.00257 * 149597870.7)),
        1.0e-15,
        "generic lunar angular radius",
        failures);
    expect_true(std::isnan(visibility_angular_radius_rad(-1.0, 1.0)), "invalid negative radius", failures);
    expect_true(std::isnan(visibility_angular_radius_rad(1.0, 0.0)), "invalid zero distance", failures);
}

}  // namespace

int main() {
    int failures = 0;
    test_sign_and_crossing(&failures);
    test_roots_and_vertices(&failures);
    test_classification(&failures);
    test_solar_constants(&failures);
    if (failures != 0) {
        std::cerr << failures << " visibility math checks failed\n";
        return 1;
    }
    return 0;
}
