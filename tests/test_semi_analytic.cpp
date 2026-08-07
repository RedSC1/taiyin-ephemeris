#include "taiyin/internal/ephemeris_block.h"
#include "taiyin/internal/semi_analytic.h"
#include "taiyin/runtime/ephemeris_route.h"
#include "taiyin/runtime/runtime.h"

#include <cmath>
#include <cstring>
#include <cstdio>

namespace {

using taiyin::CartesianState;
using taiyin::Vector3;
using taiyin::internal::CompiledEphemerisBlock;
using taiyin::internal::StorageEphemerisBlock;

static_assert(
    static_cast<int>(taiyin::internal::EphemerisBlockFormat::SemiAnalytic) == 4,
    "semi-analytical format must replace the former built-in fallback slot");
static_assert(
    taiyin::runtime::TAIYIN_EPHEMERIS_ROUTE_SEMI_ANALYTIC == 3,
    "semi-analytical route must retain the built-in fallback route id");

int failures = 0;

void expect_true(bool condition, const char* label) {
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", label);
        ++failures;
    }
}

taiyin::SplitJulianDate split_jd(double jd) {
    taiyin::SplitJulianDate out;
    expect_true(taiyin::split_julian_date_from_double(jd, &out), "split JD");
    return out;
}

void expect_near(double actual, double expected, double tolerance, const char* label) {
    if (!std::isfinite(actual) || std::fabs(actual - expected) > tolerance) {
        std::fprintf(
            stderr,
            "FAIL: %s actual=%.17g expected=%.17g diff=%.17g tolerance=%.17g\n",
            label,
            actual,
            expected,
            actual - expected,
            tolerance);
        ++failures;
    }
}

bool compile_route(
    int target_id,
    int center_id,
    double jd,
    StorageEphemerisBlock* storage,
    CompiledEphemerisBlock* block
) {
    double start = 0.0;
    double end = 0.0;
    return taiyin::internal::get_builtin_semi_analytic_coverage(
            target_id, center_id, &start, &end)
        && jd >= start && jd < end
        && taiyin::internal::compile_builtin_semi_analytic_ephemeris_block(
            target_id, center_id, start, end, storage)
        && taiyin::internal::get_compiled_block_from_storage(
            storage, target_id, block);
}

void test_python_position_oracles() {
    struct Case {
        int target_id;
        int center_id;
        double jd;
        double expected[3];
    };
    const Case cases[] = {
        {1, 10, 2451545.0, {-0.13009672007311779, -0.40059237312298285, -0.20048935633605455}},
        {4, 10, 2000000.25, {-0.74175245146111757, -1.2046755625578387, -0.5295804600880083}},
        {9, 10, 2750000.75, {43.977240932359457, 8.5112705566859717, -10.597727104737656}},
        {3, 10, 625400.0, {-0.5621882770636577, -0.77256208705910523, -0.34479448831055276}},
        {399, 10, 2451545.0, {-0.17712989144400712, 0.88742994510573259, 0.38474349080558146}},
        {399, 10, 2816700.0, {1.004422337927845, -0.019527185130096034, -0.0087388645115314065}},
        {301, 399, 2451545.0, {-0.0019492790327158378, -0.0017828964171667768, -0.00050871268262010977}},
        {301, 399, 700000.0, {-0.00068271518204465558, 0.0020519650721599028, 0.0010627941775929164}},
        {301, 399, 2816700.0, {-0.001641039820451488, -0.0019949685922289032, -0.00077264285123617577}},
    };

    for (size_t index = 0; index < sizeof(cases) / sizeof(cases[0]); ++index) {
        StorageEphemerisBlock storage;
        CompiledEphemerisBlock block;
        expect_true(
            compile_route(
                cases[index].target_id,
                cases[index].center_id,
                cases[index].jd,
                &storage,
                &block),
            "compile semi-analytical oracle route");
        CartesianState state;
        if (block.data) {
            expect_true(
                taiyin::internal::eval_compiled_ephemeris_block(
                    split_jd(cases[index].jd), &block, &state),
                "evaluate semi-analytical oracle route");
            expect_near(state.position_au.x, cases[index].expected[0], 2.0e-13, "Python oracle x");
            expect_near(state.position_au.y, cases[index].expected[1], 2.0e-13, "Python oracle y");
            expect_near(state.position_au.z, cases[index].expected[2], 2.0e-13, "Python oracle z");
        }
        taiyin::internal::destroy_storage_ephemeris_block(&storage);
    }
}

double component(const Vector3& vector, size_t axis) {
    return axis == 0 ? vector.x : (axis == 1 ? vector.y : vector.z);
}

void test_analytic_derivatives(int target_id, int center_id, double jd, double step) {
    StorageEphemerisBlock storage;
    CompiledEphemerisBlock block;
    expect_true(
        compile_route(target_id, center_id, jd, &storage, &block),
        "compile derivative route");
    if (!block.data) {
        return;
    }

    CartesianState center;
    CartesianState minus2;
    CartesianState minus1;
    CartesianState plus1;
    CartesianState plus2;
    const taiyin::SplitJulianDate center_jd = split_jd(jd);
    taiyin::SplitJulianDate minus2_jd;
    taiyin::SplitJulianDate minus1_jd;
    taiyin::SplitJulianDate plus1_jd;
    taiyin::SplitJulianDate plus2_jd;
    expect_true(taiyin::add_days_to_split_jd(center_jd, -2.0 * step, &minus2_jd), "minus two JD");
    expect_true(taiyin::add_days_to_split_jd(center_jd, -step, &minus1_jd), "minus one JD");
    expect_true(taiyin::add_days_to_split_jd(center_jd, step, &plus1_jd), "plus one JD");
    expect_true(taiyin::add_days_to_split_jd(center_jd, 2.0 * step, &plus2_jd), "plus two JD");
    expect_true(
        taiyin::internal::eval_compiled_ephemeris_block(center_jd, &block, &center),
        "evaluate analytic state");
    expect_true(taiyin::internal::eval_compiled_ephemeris_block(minus2_jd, &block, &minus2), "evaluate minus two");
    expect_true(taiyin::internal::eval_compiled_ephemeris_block(minus1_jd, &block, &minus1), "evaluate minus one");
    expect_true(taiyin::internal::eval_compiled_ephemeris_block(plus1_jd, &block, &plus1), "evaluate plus one");
    expect_true(taiyin::internal::eval_compiled_ephemeris_block(plus2_jd, &block, &plus2), "evaluate plus two");

    for (size_t axis = 0; axis < 3; ++axis) {
        const double p_minus2 = component(minus2.position_au, axis);
        const double p_minus1 = component(minus1.position_au, axis);
        const double p_plus1 = component(plus1.position_au, axis);
        const double p_plus2 = component(plus2.position_au, axis);
        const double velocity = (
            p_minus2 - 8.0 * p_minus1 + 8.0 * p_plus1 - p_plus2)
            / (12.0 * step);
        const double acceleration = (
            component(minus2.velocity_au_per_day, axis)
            - 8.0 * component(minus1.velocity_au_per_day, axis)
            + 8.0 * component(plus1.velocity_au_per_day, axis)
            - component(plus2.velocity_au_per_day, axis))
            / (12.0 * step);
        expect_near(
            component(center.velocity_au_per_day, axis),
            velocity,
            target_id == 301 ? 2.0e-9 : 3.0e-10,
            "analytic velocity matches five-point difference");
        expect_near(
            component(center.acceleration_au_per_day2, axis),
            acceleration,
            target_id == 301 ? 3.0e-9 : 3.0e-10,
            "analytic acceleration matches five-point difference");
    }
    taiyin::internal::destroy_storage_ephemeris_block(&storage);
}

void test_coverage_contract() {
    double start = 0.0;
    double end = 0.0;
    expect_true(
        taiyin::internal::get_builtin_semi_analytic_coverage(1, 10, &start, &end)
            && start == 625295.0 && end == 2816795.0,
        "planet coverage");
    expect_true(
        taiyin::internal::get_builtin_semi_analytic_coverage(301, 399, &start, &end)
            && start > 625300.0 && end < 2816800.0,
        "lunar coverage");
    expect_true(
        !taiyin::internal::get_builtin_semi_analytic_coverage(301, 10, &start, &end),
        "unsupported heliocentric Moon route is not advertised");
}

void test_runtime_routes() {
    taiyin::runtime::Runtime runtime;
    taiyin::runtime::EphemerisRuntimeConfig config;
    config.segment_cache_max_entries = 8;
    config.load_packaged_data = true;
    expect_true(runtime.initialize_ephemeris(config), "initialize semi-analytical runtime");

    taiyin::runtime::EphemerisRequest request;
    request.target_id = 4;
    request.center_id = 10;
    request.frame = taiyin::internal::IcrfJ2000Equatorial;
    request.jd_tdb = split_jd(625400.0);
    request.route_rule_id = taiyin::runtime::TAIYIN_EPHEMERIS_ROUTE_AUTO;
    taiyin::runtime::EphemerisResult result;
    taiyin::runtime::EphemerisEvalDiagnostic diagnostic;
    expect_true(
        taiyin::status_ok(runtime.eval_ephemeris_state(request, &result, &diagnostic)),
        "AUTO route evaluates ancient Mars through built-in fallback");
    expect_true(
        result.descriptor.method_id == taiyin::internal::SEMI_ANALYTIC_METHOD_ID
            && result.descriptor.source_key.source_id
                == taiyin::internal::SEMI_ANALYTIC_SOURCE_ID,
        "AUTO route selects the built-in semi-analytical fallback");

    request.jd_tdb = split_jd(2451545.0);
    request.route_rule_id = taiyin::runtime::TAIYIN_EPHEMERIS_ROUTE_SEMI_ANALYTIC;
    expect_true(
        taiyin::status_ok(runtime.eval_ephemeris_state(request, &result, &diagnostic))
            && result.descriptor.method_id == taiyin::internal::SEMI_ANALYTIC_METHOD_ID,
        "explicit semi-analytical route is registered");
}

}  // namespace

int main() {
    expect_true(
        std::strcmp(
            taiyin::internal::builtin_semi_analytic_source_revision(),
            "27d33df2089ee1213a13a68782d5eff4ca2b2681") == 0,
        "semi-analytical source revision is recorded");
    expect_true(
        std::strcmp(
            taiyin::internal::builtin_semi_analytic_coefficients_sha256(),
            "67beddfed388e5a8b934b8834a0f011dd69fa9888c6373a7a6becbd39eb01516") == 0,
        "semi-analytical coefficient hash is recorded");
    test_python_position_oracles();
    test_analytic_derivatives(1, 10, 2451545.0, 1.0 / 64.0);
    test_analytic_derivatives(6, 10, 2300000.25, 1.0 / 16.0);
    test_analytic_derivatives(399, 10, 2451545.0, 1.0 / 64.0);
    test_analytic_derivatives(301, 399, 2451545.0, 1.0 / 512.0);
    test_coverage_contract();
    test_runtime_routes();
    if (failures != 0) {
        std::fprintf(stderr, "%d semi-analytical test(s) failed\n", failures);
        return 1;
    }
    return 0;
}
