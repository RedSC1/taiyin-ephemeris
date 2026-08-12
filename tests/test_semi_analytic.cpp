#include "taiyin/internal/ephemeris_block.h"
#include "taiyin/internal/semi_analytic.h"
#include "taiyin/physical_constants.h"
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
        {399, 10, 2451545.0, {-0.1771298914135801, 0.88742994506633999, 0.38474349080421483}},
        {399, 10, 2816700.0, {1.0044223378677171, -0.019527185094630098, -0.0087388645273331518}},
        {301, 399, 2451545.0, {-0.0019492815368800627, -0.0017828931751430715, -0.00050871257014439451}},
        {301, 399, 700000.0, {-0.00068273572820657443, 0.0020519544880880742, 0.0010627972998754826}},
        {301, 399, 2816700.0, {-0.0016410348718894716, -0.0019949715110956531, -0.00077264155074353402}},
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

void test_charon_and_pluto_cob_routes() {
    struct Case {
        int target_id;
        int center_id;
        double jd;
        double expected_km[3];
        double tolerance_km;
        const char* label;
    };
    // PLU060 and NEP098 direct barycenter-to-target vectors, sampled
    // independently with jplephem. The Mars and Pluto physical-center routes
    // sum every mass-bearing satellite modeled by their source kernels;
    // Neptune remains deliberately Triton-dominant.
    const Case cases[] = {
        {401, 499, 2451545.0,
            {-1988.977928515696, -8743.1602252503581, -3182.2616495026705},
            200.0, "Phobos MAR099 residual state"},
        {402, 499, 2500000.0,
            {10324.336540234461, -16001.192049068239, -13705.453497488968},
            30.0, "Deimos MAR099 residual state"},
        {499, 4, 2451545.0,
            {0.000009640920933, 0.000180043780302, 0.000083971596235},
            0.001, "Mars complete-system COB state"},
        {499, 4, 2500000.0,
            {-0.000110302048081, -0.000092876351896, 0.000009921356384},
            0.001, "Mars complete-system COB state 2132"},
        {901, 999, 2451545.0,
            {-6837.721052183022, -8791.382868678273, -16126.27409447914},
            0.030, "Charon relative state"},
        {901, 999, 2469807.5,
            {-13616.805487051643, -11478.774833547746, 8178.907704696217},
            0.030, "Charon relative state 2050"},
        {901, 999, 2506332.5,
            {14399.858588399833, 13115.496184777934, -2125.0170357130087},
            0.030, "Charon relative state 2150"},
        {902, 999, 2451545.0,
            {-3191.6373911550245, 3994.5785556577875, 46539.632693610045},
            300.0, "Nix two-angle relative state"},
        {903, 999, 2469807.5,
            {18206.961189308357, 7838.1714036404464, -60408.82486525178},
            1000.0, "Hydra two-angle relative state"},
        {904, 999, 2506332.5,
            {-32137.988108465808, -34857.536814958395, -30092.000343465213},
            1000.0, "Kerberos two-angle relative state"},
        {905, 999, 2451545.0,
            {30203.420371973123, 26698.009339959724, -9675.0164273554838},
            1000.0, "Styx two-angle relative state"},
        {999, 9, 2451545.0,
            {743.7022701247653, 956.2186368660449, 1754.1542239321088},
            0.005, "Pluto complete-system COB state"},
        {999, 9, 2469807.5,
            {1481.1659591758366, 1248.619970807618, -889.5406273663918},
            0.005, "Pluto complete-system COB state 2050"},
        {999, 9, 2506332.5,
            {-1566.3584333779022, -1426.6283413074102, 231.30599816907932},
            0.005, "Pluto complete-system COB state 2150"},
        {801, 899, 2451545.0,
            {-205696.47446793687, 10004.077126660104, 288812.3684286066},
            60.0, "Triton relative state"},
        {801, 899, 2469807.5,
            {143434.22786050473, -48487.20380808989, -320869.9828722861},
            60.0, "Triton relative state 2050"},
        {801, 899, 2506332.5,
            {89850.25117098121, -206680.48400603488, -274017.72332165355},
            60.0, "Triton relative state 2150"},
        {899, 8, 2451545.0,
            {42.96082365738436, -2.064742769082248, -60.312506339096764},
            0.100, "Neptune Triton-dominant COB state"},
        {899, 8, 2469807.5,
            {-30.01626368170534, 10.120030803635498, 67.05990633161736},
            0.100, "Neptune Triton-dominant COB state 2050"},
        {899, 8, 2506332.5,
            {-18.7539495747309, 43.162907800094324, 57.22385526353068},
            0.100, "Neptune Triton-dominant COB state 2150"},
    };
    for (size_t index = 0; index < sizeof(cases) / sizeof(cases[0]); ++index) {
        StorageEphemerisBlock storage;
        CompiledEphemerisBlock block;
        expect_true(
            compile_route(
                cases[index].target_id, cases[index].center_id, cases[index].jd,
                &storage, &block),
            cases[index].label);
        CartesianState state;
        if (block.data) {
            expect_true(
                taiyin::internal::eval_compiled_ephemeris_block(
                    split_jd(cases[index].jd), &block, &state),
                "evaluate Pluto-system semi-analytic route");
            const double actual_km[3] = {
                state.position_au.x * taiyin::TAIYIN_AU_KM,
                state.position_au.y * taiyin::TAIYIN_AU_KM,
                state.position_au.z * taiyin::TAIYIN_AU_KM,
            };
            for (size_t axis = 0; axis < 3; ++axis) {
                expect_near(
                    actual_km[axis], cases[index].expected_km[axis],
                    cases[index].tolerance_km, cases[index].label);
            }
        }
        taiyin::internal::destroy_storage_ephemeris_block(&storage);
    }
}

void test_galilean_l1_routes() {
    struct SourceCase {
        int target_id;
        double expected_au[3];
    };
    // Direct values emitted by Astronomy Engine's retained compact L1.2
    // implementation at TT J2000.0. This catches element-conversion or frame
    // regressions independently of the deliberately loose JUP365 accuracy
    // contract below.
    const SourceCase source_cases[] = {
        {501, {0.0026721026466794222, 0.00076430589263707944,
            0.00040882062939067907}},
        {502, {-0.0037512422429412857, -0.0021357390400536334,
            -0.0010567698616581480}},
        {503, {-0.0054895158200833514, -0.0041119014515566092,
            -0.0020338789777956802}},
        {504, {0.0021725808729460287, 0.011187996083737949,
            0.005323161129880986}},
    };
    for (size_t index = 0;
         index < sizeof(source_cases) / sizeof(source_cases[0]); ++index) {
        StorageEphemerisBlock storage;
        CompiledEphemerisBlock block;
        expect_true(
            compile_route(
                source_cases[index].target_id, 599, 2451545.0,
                &storage, &block),
            "compile compact L1.2 source regression route");
        CartesianState state;
        if (block.data) {
            expect_true(
                taiyin::internal::eval_compiled_ephemeris_block(
                    split_jd(2451545.0), &block, &state),
                "evaluate compact L1.2 source regression route");
            expect_near(
                state.position_au.x, source_cases[index].expected_au[0],
                6.0e-14, "compact L1.2 source regression x");
            expect_near(
                state.position_au.y, source_cases[index].expected_au[1],
                6.0e-14, "compact L1.2 source regression y");
            expect_near(
                state.position_au.z, source_cases[index].expected_au[2],
                6.0e-14, "compact L1.2 source regression z");
        }
        taiyin::internal::destroy_storage_ephemeris_block(&storage);
    }

    struct Case {
        int target_id;
        int center_id;
        double jd;
        double expected_km[3];
        double tolerance_km;
        const char* label;
    };
    // Independent JUP365 vectors from jplephem.  The compact L1.2 model is
    // deliberately kilometre-to-thousand-kilometre class for individual
    // moons; the physical Jupiter correction is far better after mass
    // weighting the four Galileans.
    const Case cases[] = {
        {501, 599, 2451545.0,
            {399714.236329573, 114358.233793476, 61202.666940873},
            2000.0, "Io compact L1.2 state"},
        {502, 599, 2469807.5,
            {5912.318807196, 600294.278726695, 289815.201072822},
            1200.0, "Europa compact L1.2 state"},
        {503, 599, 2506332.5,
            {-467797.056907286, 872015.542149202, 410741.880700012},
            1200.0, "Ganymede compact L1.2 state"},
        {504, 599, 2451545.0,
            {325079.730633136, 1673657.388398113, 796198.064855955},
            1200.0, "Callisto compact L1.2 state"},
        {599, 5, 2451545.0,
            {41.059135538, -44.132392576, -20.245438341},
            0.300, "Jupiter Galilean-dominant COB state"},
        {599, 5, 2506332.5,
            {-25.484595441, -97.981200444, -46.608615492},
            0.300, "Jupiter Galilean-dominant COB state 2150"},
    };
    for (size_t index = 0; index < sizeof(cases) / sizeof(cases[0]); ++index) {
        StorageEphemerisBlock storage;
        CompiledEphemerisBlock block;
        expect_true(
            compile_route(
                cases[index].target_id, cases[index].center_id, cases[index].jd,
                &storage, &block),
            cases[index].label);
        CartesianState state;
        if (block.data) {
            expect_true(
                taiyin::internal::eval_compiled_ephemeris_block(
                    split_jd(cases[index].jd), &block, &state),
                "evaluate Galilean compact L1.2 route");
            const double actual_km[3] = {
                state.position_au.x * taiyin::TAIYIN_AU_KM,
                state.position_au.y * taiyin::TAIYIN_AU_KM,
                state.position_au.z * taiyin::TAIYIN_AU_KM,
            };
            for (size_t axis = 0; axis < 3; ++axis) {
                expect_near(
                    actual_km[axis], cases[index].expected_km[axis],
                    cases[index].tolerance_km, cases[index].label);
            }
        }
        taiyin::internal::destroy_storage_ephemeris_block(&storage);
    }
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
        taiyin::internal::get_builtin_semi_analytic_coverage(10, 0, &start, &end)
            && start == 625295.0 && end == 2816795.0,
        "synthesized Sun-to-SSB coverage");
    expect_true(
        taiyin::internal::get_builtin_semi_analytic_coverage(301, 399, &start, &end)
            && start > 625300.0 && end < 2816800.0,
        "lunar coverage");
    expect_true(
        !taiyin::internal::get_builtin_semi_analytic_coverage(301, 10, &start, &end),
        "unsupported heliocentric Moon route is not advertised");
    expect_true(
        taiyin::internal::get_builtin_semi_analytic_coverage(401, 499, &start, &end)
            && start == 2305447.5 && end == 2670691.5,
        "Phobos MAR099 coverage is explicit");
    expect_true(
        taiyin::internal::get_builtin_semi_analytic_coverage(402, 499, &start, &end)
            && start == 2305447.5 && end == 2670691.5,
        "Deimos MAR099 coverage is explicit");
    expect_true(
        taiyin::internal::get_builtin_semi_analytic_coverage(499, 4, &start, &end)
            && start == 2305447.5 && end == 2670691.5,
        "Mars COB correction coverage is explicit");
    expect_true(
        taiyin::internal::get_builtin_semi_analytic_coverage(501, 599, &start, &end)
            && start == 2305456.5 && end == 2524602.5,
        "Io compact L1.2 coverage is explicit");
    expect_true(
        taiyin::internal::get_builtin_semi_analytic_coverage(599, 5, &start, &end)
            && start == 2305456.5 && end == 2524602.5,
        "Jupiter Galilean-dominant COB coverage is explicit");
    expect_true(
        taiyin::internal::get_builtin_semi_analytic_coverage(901, 999, &start, &end)
            && start == 2378497.5 && end == 2524591.5,
        "Charon PLU060 coverage is explicit");
    expect_true(
        taiyin::internal::get_builtin_semi_analytic_coverage(902, 999, &start, &end)
            && start == 2378497.5 && end == 2524591.5,
        "Nix PLU060 coverage is explicit");
    expect_true(
        taiyin::internal::get_builtin_semi_analytic_coverage(903, 999, &start, &end)
            && start == 2378497.5 && end == 2524591.5,
        "Hydra PLU060 coverage is explicit");
    expect_true(
        taiyin::internal::get_builtin_semi_analytic_coverage(904, 999, &start, &end)
            && start == 2378497.5 && end == 2524591.5,
        "Kerberos PLU060 coverage is explicit");
    expect_true(
        taiyin::internal::get_builtin_semi_analytic_coverage(905, 999, &start, &end)
            && start == 2378497.5 && end == 2524591.5,
        "Styx PLU060 coverage is explicit");
    expect_true(
        taiyin::internal::get_builtin_semi_analytic_coverage(999, 9, &start, &end)
            && start == 2378497.5 && end == 2524591.5,
        "Pluto COB correction coverage is explicit");
    expect_true(
        taiyin::internal::get_builtin_semi_analytic_coverage(801, 899, &start, &end)
            && start == 2378496.5 && end == 2524592.5,
        "Triton NEP098 coverage is explicit");
    expect_true(
        taiyin::internal::get_builtin_semi_analytic_coverage(899, 8, &start, &end)
            && start == 2378496.5 && end == 2524592.5,
        "Neptune COB correction coverage is explicit");
    expect_true(
        !taiyin::internal::get_builtin_semi_analytic_coverage(999, 10, &start, &end),
        "Pluto-to-Sun is composed through the barycenter, not registered directly");
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

    request.target_id = 901;
    request.center_id = 999;
    request.jd_tdb = split_jd(2451545.0);
    expect_true(
        taiyin::status_ok(runtime.eval_ephemeris_state(request, &result, &diagnostic))
            && result.descriptor.method_id == taiyin::internal::SEMI_ANALYTIC_METHOD_ID,
        "Charon PLU060 residual route is registered");

    request.target_id = 501;
    request.center_id = 599;
    expect_true(
        taiyin::status_ok(runtime.eval_ephemeris_state(
            request, &result, &diagnostic))
            && result.descriptor.method_id == taiyin::internal::SEMI_ANALYTIC_METHOD_ID,
        "Io compact L1.2 route is registered");

    request.target_id = 599;
    request.center_id = 5;
    expect_true(
        taiyin::status_ok(runtime.eval_ephemeris_state(
            request, &result, &diagnostic))
            && result.descriptor.method_id == taiyin::internal::SEMI_ANALYTIC_METHOD_ID,
        "Jupiter Galilean-dominant COB route is registered");

    request.target_id = 999;
    request.center_id = 10;
    expect_true(
        taiyin::status_ok(runtime.eval_ephemeris_state(request, &result, &diagnostic))
            && result.descriptor.method_id == taiyin::internal::SEMI_ANALYTIC_METHOD_ID,
        "Pluto COB-corrected semi-analytical route is registered");
    const CartesianState pluto_from_sun = result.state;

    request.target_id = 9;
    request.center_id = 10;
    taiyin::runtime::EphemerisResult pluto_barycenter_from_sun;
    expect_true(
        taiyin::status_ok(runtime.eval_ephemeris_state(
            request, &pluto_barycenter_from_sun, &diagnostic)),
        "Pluto barycenter-to-Sun component evaluates");

    request.target_id = 999;
    request.center_id = 9;
    taiyin::runtime::EphemerisResult pluto_from_barycenter;
    expect_true(
        taiyin::status_ok(runtime.eval_ephemeris_state(
            request, &pluto_from_barycenter, &diagnostic)),
        "Pluto center-to-barycenter component evaluates");
    for (size_t axis = 0; axis < 3; ++axis) {
        expect_near(
            component(pluto_from_sun.position_au, axis),
            component(pluto_barycenter_from_sun.state.position_au, axis)
                + component(pluto_from_barycenter.state.position_au, axis),
            2.0e-15,
            "Pluto-to-Sun route composes barycenter and COB components");
    }

    request.target_id = 801;
    request.center_id = 899;
    taiyin::runtime::EphemerisResult triton_from_neptune;
    expect_true(
        taiyin::status_ok(runtime.eval_ephemeris_state(
            request, &triton_from_neptune, &diagnostic))
            && triton_from_neptune.descriptor.method_id
                == taiyin::internal::SEMI_ANALYTIC_METHOD_ID,
        "Triton NEP098 residual route is registered");

    request.target_id = 899;
    request.center_id = 10;
    taiyin::runtime::EphemerisResult neptune_from_sun;
    expect_true(
        taiyin::status_ok(runtime.eval_ephemeris_state(
            request, &neptune_from_sun, &diagnostic))
            && neptune_from_sun.descriptor.method_id
                == taiyin::internal::SEMI_ANALYTIC_METHOD_ID,
        "Neptune Triton-dominant semi-analytical route is registered");

    request.target_id = 10;
    request.center_id = 0;
    taiyin::runtime::EphemerisResult semi_sun;
    expect_true(
        taiyin::status_ok(runtime.eval_ephemeris_state(
            request, &semi_sun, &diagnostic))
            && semi_sun.descriptor.method_id
                == taiyin::internal::SEMI_ANALYTIC_METHOD_ID,
        "semi-analytical Sun-to-SSB route is registered");

    request.route_rule_id = taiyin::runtime::TAIYIN_EPHEMERIS_ROUTE_OPM2;
    taiyin::runtime::EphemerisResult opm_sun;
    const bool opm_ok = taiyin::status_ok(runtime.eval_ephemeris_state(
        request, &opm_sun, &diagnostic));
    expect_true(opm_ok, "packaged Sun-to-SSB oracle evaluates");
    if (opm_ok) {
        const double dx = semi_sun.state.position_au.x
            - opm_sun.state.position_au.x;
        const double dy = semi_sun.state.position_au.y
            - opm_sun.state.position_au.y;
        const double dz = semi_sun.state.position_au.z
            - opm_sun.state.position_au.z;
        expect_true(
            std::sqrt(dx * dx + dy * dy + dz * dz) < 5.0e-5,
            "synthesized Sun-to-SSB state tracks packaged ephemeris");
    }
}

}  // namespace

int main() {
    expect_true(
        std::strcmp(
            taiyin::internal::builtin_semi_analytic_source_revision(),
            "a5bdf675f921804874dc4e0a0838beebfbcf2b32") == 0,
        "semi-analytical source revision is recorded");
    expect_true(
        std::strcmp(
            taiyin::internal::builtin_semi_analytic_coefficients_sha256(),
            "728d60ee0f5cb0a99b016608a33df9ba16d48f59053f19384922d6d7fc0f1270") == 0,
        "semi-analytical coefficient hash is recorded");
    test_python_position_oracles();
    test_analytic_derivatives(1, 10, 2451545.0, 1.0 / 64.0);
    test_analytic_derivatives(6, 10, 2300000.25, 1.0 / 16.0);
    test_analytic_derivatives(399, 10, 2451545.0, 1.0 / 64.0);
    test_analytic_derivatives(301, 399, 2451545.0, 1.0 / 512.0);
    test_analytic_derivatives(10, 0, 2451545.0, 1.0 / 64.0);
    test_analytic_derivatives(901, 999, 2451545.0, 1.0 / 512.0);
    test_analytic_derivatives(999, 9, 2451545.0, 1.0 / 512.0);
    test_analytic_derivatives(801, 899, 2451545.0, 1.0 / 512.0);
    test_analytic_derivatives(899, 8, 2451545.0, 1.0 / 512.0);
    test_analytic_derivatives(501, 599, 2451545.0, 1.0 / 512.0);
    test_analytic_derivatives(599, 5, 2451545.0, 1.0 / 512.0);
    test_charon_and_pluto_cob_routes();
    test_galilean_l1_routes();
    test_coverage_contract();
    test_runtime_routes();
    if (failures != 0) {
        std::fprintf(stderr, "%d semi-analytical test(s) failed\n", failures);
        return 1;
    }
    return 0;
}
