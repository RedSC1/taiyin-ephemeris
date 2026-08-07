#include "taiyin/apparent_position.h"
#include "taiyin/body_id.h"
#include "taiyin/internal/eop.h"
#include "taiyin/runtime/calc_spec.h"
#include "taiyin/runtime/runtime.h"

#include <iostream>

namespace {

using namespace taiyin;
using namespace taiyin::runtime;

void expect_true(bool value, const char* label, int* failures) {
    if (!value) {
        std::cerr << "FAIL: expected true: " << label << "\n";
        ++(*failures);
    }
}

void expect_status(Status actual, Status expected, const char* label, int* failures) {
    if (actual != expected) {
        std::cerr << "FAIL: " << label << ": actual=" << actual << " expected=" << expected << "\n";
        ++(*failures);
    }
}

void expect_int(int actual, int expected, const char* label, int* failures) {
    if (actual != expected) {
        std::cerr << "FAIL: " << label << ": actual=" << actual << " expected=" << expected << "\n";
        ++(*failures);
    }
}

void expect_uint(uint32_t actual, uint32_t expected, const char* label, int* failures) {
    if (actual != expected) {
        std::cerr << "FAIL: " << label << ": actual=" << actual << " expected=" << expected << "\n";
        ++(*failures);
    }
}

void test_default_geocentric_ecliptic(int* failures) {
    NativeCalcContext context;
    CalcSpec spec;
    expect_status(resolve_calc_spec(&context, 0, &spec), TAIYIN_STATUS_OK, "default spec", failures);
    expect_int(spec.form, CalcOutputSpherical, "default form", failures);
    expect_int(spec.frame, CalcFrameEcliptic, "default frame", failures);
    expect_int(spec.epoch, CalcEpochOfDate, "default epoch", failures);
    expect_int(spec.date_frame, CalcDateFrameTrue, "default true frame", failures);
    expect_int(spec.position, CalcPositionApparent, "default position", failures);
    expect_int(spec.observer.origin, CalcOriginGeocentric, "default origin", failures);
    expect_int(spec.observer.observer_id, TAIYIN_BODY_EARTH, "default observer", failures);
    expect_int(spec.observer.center_id, TAIYIN_BODY_SUN, "default center", failures);
    expect_true(!spec.observer.use_topocentric_offset, "default geocentric has no topo offset", failures);
    expect_int(spec.apparent_output_frame_id, TAIYIN_APPARENT_FRAME_TRUE_ECLIPTIC_OF_DATE, "default output frame", failures);
    expect_true((spec.apparent_flags & TAIYIN_APPARENT_SPHERICAL) != 0u, "default apparent spherical", failures);
}

void test_equatorial_speed_and_xyz(int* failures) {
    NativeCalcContext context;
    CalcSpec equatorial;
    expect_status(
        resolve_calc_spec(&context, TAIYIN_CALC_FRAME_EQUATOR | TAIYIN_CALC_SPEED, &equatorial),
        TAIYIN_STATUS_OK,
        "equatorial speed spec",
        failures);
    expect_int(equatorial.apparent_output_frame_id, TAIYIN_APPARENT_FRAME_TRUE_EQUATOR_OF_DATE, "equatorial output frame", failures);
    expect_true((equatorial.apparent_flags & TAIYIN_APPARENT_VELOCITY) != 0u, "speed sets apparent velocity", failures);

    CalcSpec xyz;
    expect_status(
        resolve_calc_spec(&context, TAIYIN_CALC_FRAME_ICRF | TAIYIN_CALC_FORM_XYZ, &xyz),
        TAIYIN_STATUS_OK,
        "ICRF xyz spec",
        failures);
    expect_int(xyz.form, CalcOutputCartesian, "xyz form", failures);
    expect_int(xyz.apparent_output_frame_id, TAIYIN_APPARENT_FRAME_ICRF, "ICRF output frame", failures);
    expect_true((xyz.apparent_flags & TAIYIN_APPARENT_SPHERICAL) == 0u, "xyz clears spherical flag", failures);
}

void test_frame_epoch_combinations(int* failures) {
    NativeCalcContext context;
    CalcSpec spec;
    expect_status(
        resolve_calc_spec(&context, TAIYIN_CALC_EPOCH_J2000, &spec),
        TAIYIN_STATUS_OK,
        "J2000 ecliptic spec",
        failures);
    expect_int(spec.apparent_output_frame_id, TAIYIN_APPARENT_FRAME_J2000_ECLIPTIC, "J2000 ecliptic frame", failures);

    expect_status(
        resolve_calc_spec(&context, TAIYIN_CALC_FRAME_EQUATOR | TAIYIN_CALC_EPOCH_J2000, &spec),
        TAIYIN_STATUS_OK,
        "J2000 equator spec",
        failures);
    expect_int(spec.apparent_output_frame_id, TAIYIN_APPARENT_FRAME_J2000_MEAN_EQUATOR, "J2000 equator frame", failures);

    expect_status(
        resolve_calc_spec(&context, TAIYIN_CALC_DATE_FRAME_MEAN, &spec),
        TAIYIN_STATUS_OK,
        "mean ecliptic of-date spec",
        failures);
    expect_int(spec.apparent_output_frame_id, TAIYIN_APPARENT_FRAME_MEAN_ECLIPTIC_OF_DATE, "mean ecliptic frame", failures);

    expect_status(
        resolve_calc_spec(&context, TAIYIN_CALC_FRAME_EQUATOR | TAIYIN_CALC_DATE_FRAME_MEAN, &spec),
        TAIYIN_STATUS_OK,
        "mean equator of-date spec",
        failures);
    expect_int(spec.apparent_output_frame_id, TAIYIN_APPARENT_FRAME_MEAN_EQUATOR_OF_DATE, "mean equator frame", failures);

    expect_status(
        resolve_calc_spec(&context, TAIYIN_CALC_FRAME_CIRS, &spec),
        TAIYIN_STATUS_OK,
        "CIRS of-date spec",
        failures);
    expect_int(spec.apparent_output_frame_id, TAIYIN_APPARENT_FRAME_CIRS, "CIRS frame", failures);
}

void test_position_modes(int* failures) {
    NativeCalcContext context;
    context.apparent_options.flags |= TAIYIN_APPARENT_ABERRATION | TAIYIN_APPARENT_DEFLECTION | TAIYIN_APPARENT_SHAPIRO_DELAY;

    CalcSpec astrometric;
    expect_status(
        resolve_calc_spec(&context, TAIYIN_CALC_POSITION_ASTROMETRIC, &astrometric),
        TAIYIN_STATUS_OK,
        "astrometric spec",
        failures);
    expect_true((astrometric.apparent_flags & TAIYIN_APPARENT_LIGHT_TIME) != 0u, "astrometric keeps light time", failures);
    expect_true((astrometric.apparent_flags & TAIYIN_APPARENT_ABERRATION) == 0u, "astrometric clears aberration", failures);
    expect_true((astrometric.apparent_flags & TAIYIN_APPARENT_DEFLECTION) == 0u, "astrometric clears deflection", failures);
    expect_true((astrometric.apparent_flags & TAIYIN_APPARENT_SHAPIRO_DELAY) == 0u, "astrometric clears Shapiro", failures);

    CalcSpec true_position;
    expect_status(
        resolve_calc_spec(&context, TAIYIN_CALC_POSITION_TRUE, &true_position),
        TAIYIN_STATUS_OK,
        "true position spec",
        failures);
    expect_uint(
        true_position.apparent_flags & (TAIYIN_APPARENT_LIGHT_TIME | TAIYIN_APPARENT_ABERRATION | TAIYIN_APPARENT_DEFLECTION | TAIYIN_APPARENT_SHAPIRO_DELAY),
        0u,
        "true position clears correction flags",
        failures);
}

void test_topocentric_requirements(int* failures) {
    NativeCalcContext missing;
    CalcSpec spec;
    expect_status(
        resolve_calc_spec(&missing, TAIYIN_CALC_ORIGIN_TOPOCENTRIC_SIMPLE, &spec),
        TAIYIN_ERROR_INVALID_ARGUMENT,
        "simple topo requires location or offset",
        failures);

    NativeCalcContext simple;
    expect_status(
        native_context_set_observer_location(&simple, native_observer_location_degrees(116.4074, 39.9042, 43.5)),
        TAIYIN_STATUS_OK,
        "set observer location",
        failures);
    expect_status(
        resolve_calc_spec(&simple, TAIYIN_CALC_ORIGIN_TOPOCENTRIC_SIMPLE, &spec),
        TAIYIN_STATUS_OK,
        "simple topo with location",
        failures);
    expect_true(spec.observer.use_topocentric_offset, "simple topo uses offset", failures);
    expect_true((spec.apparent_flags & TAIYIN_APPARENT_TOPOCENTRIC) != 0u, "simple topo apparent flag", failures);

    NativeCalcContext precise_missing_eop = simple;
    expect_true(set_global_earth_orientation_table(nullptr), "clear global EOP", failures);
    expect_status(
        resolve_calc_spec(&precise_missing_eop, TAIYIN_CALC_ORIGIN_TOPOCENTRIC_PRECISE, &spec),
        TAIYIN_ERROR_INVALID_ARGUMENT,
        "precise topo requires EOP",
        failures);

    internal::EarthOrientationSample sample = {};
    internal::EarthOrientationTable table = { &sample, 1 };
    NativeCalcContext precise = simple;
    expect_true(set_global_earth_orientation_table(&table), "set global EOP", failures);
    expect_status(
        resolve_calc_spec(&precise, TAIYIN_CALC_ORIGIN_TOPOCENTRIC_PRECISE, &spec),
        TAIYIN_STATUS_OK,
        "precise topo with EOP",
        failures);
    expect_int(spec.observer.origin, CalcOriginTopocentricPrecise, "precise topo origin", failures);
    expect_true(
        set_global_earth_orientation_table(nullptr),
        "restore empty global EOP",
        failures);
}

void test_unsupported_combinations(int* failures) {
    NativeCalcContext context;
    CalcSpec spec;
    expect_status(
        resolve_calc_spec(&context, TAIYIN_CALC_FRAME_CIRS | TAIYIN_CALC_EPOCH_J2000, &spec),
        TAIYIN_ERROR_UNSUPPORTED,
        "CIRS J2000 unsupported",
        failures);
    expect_status(
        resolve_calc_spec(&context, TAIYIN_CALC_FRAME_CIRS | TAIYIN_CALC_DATE_FRAME_MEAN, &spec),
        TAIYIN_ERROR_UNSUPPORTED,
        "CIRS mean unsupported",
        failures);
    expect_status(
        resolve_calc_spec(&context, TAIYIN_CALC_FRAME_ICRF | TAIYIN_CALC_DATE_FRAME_MEAN, &spec),
        TAIYIN_ERROR_UNSUPPORTED,
        "ICRF mean unsupported",
        failures);
    expect_status(
        resolve_calc_spec(&context, TAIYIN_CALC_ORIGIN_HELIOCENTRIC, &spec),
        TAIYIN_ERROR_UNSUPPORTED,
        "heliocentric origin unsupported until zero-origin handling exists",
        failures);
}

}  // namespace

int main() {
    int failures = 0;
    test_default_geocentric_ecliptic(&failures);
    test_equatorial_speed_and_xyz(&failures);
    test_frame_epoch_combinations(&failures);
    test_position_modes(&failures);
    test_topocentric_requirements(&failures);
    test_unsupported_combinations(&failures);
    if (failures != 0) {
        std::cerr << failures << " calc-spec test failure(s)\n";
        return 1;
    }
    std::cout << "Calc spec tests passed.\n";
    return 0;
}
