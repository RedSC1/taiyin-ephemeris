#include "taiyin/internal/ephemeris_block.h"
#include "taiyin/internal/long_range_analytic.h"
#include "taiyin/internal/semi_analytic.h"
#include "taiyin/body_id.h"
#include "taiyin/physical_constants.h"
#include "taiyin/runtime/ephemeris_engine.h"
#include "taiyin/runtime/ephemeris_route.h"
#include "taiyin/runtime/runtime.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>

namespace {

int failures = 0;

void expect_true(bool condition, const char* label) {
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", label);
        ++failures;
    }
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

taiyin::SplitJulianDate split_jd(double jd) {
    taiyin::SplitJulianDate result;
    expect_true(taiyin::split_julian_date_from_double(jd, &result), "split JD");
    return result;
}

bool eval_direct(
    int target_id,
    int center_id,
    double jd,
    taiyin::CartesianState* out
) {
    double start = 0.0;
    double end = 0.0;
    taiyin::internal::StorageEphemerisBlock storage;
    taiyin::internal::CompiledEphemerisBlock block;
    const bool ok = taiyin::internal::get_builtin_long_range_analytic_coverage(
            target_id, center_id, &start, &end)
        && taiyin::internal::compile_builtin_long_range_analytic_ephemeris_block(
            target_id, center_id, start, end, &storage)
        && taiyin::internal::get_compiled_block_from_storage(
            &storage, target_id, &block)
        && taiyin::internal::eval_compiled_ephemeris_block(
            split_jd(jd), &block, out);
    taiyin::internal::destroy_storage_ephemeris_block(&storage);
    return ok;
}

void test_js_runtime_parity() {
    struct Case {
        int target_id;
        double jd;
        double position[3];
        double velocity[3];
    };
    // Independently emitted by js-ephemeris-lite commit recorded in the
    // generated include, after its native-theory -> J2000 -> ICRF rotations.
    const Case cases[] = {
        {1, 2451545.0,
            {-0.13009362215401613, -0.4005937310608239, -0.20048930119194636},
            {0.021366394946796922, -0.004926302850875012, -0.004847428177919624}},
        {2, 2451545.0,
            {-0.7183023269622616, -0.04627430468572406, 0.024640581683557178},
            {0.0007981219613477448, -0.018491838365726355, -0.008369735227049379}},
        {399, 2451545.0,
            {-0.17713508454963148, 0.8874285544892133, 0.3847428220595031},
            {-0.017207623549022576, -0.0028981684484054556, -0.0012563890658786308}},
        {4, 2000000.25,
            {-0.7417492506085965, -1.2046909841674482, -0.5295838418290886},
            {0.012820948673254527, -0.005082570404827847, -0.0027112013007416476}},
        {5, -400000.0,
            {4.535327653867119, -1.9829110245485067, -0.9929155836022935},
            {0.003287214303220912, 0.006501823036752115, 0.0027053314300294405}},
        {6, 5000000.0,
            {9.746728657009747, 0.041295895340955194, -0.4324039690824744},
            {-0.00004290004365888785, 0.005004129774375955,
                0.0021479197867380687}},
        {7, 2451545.0,
            {14.431852901767115, -12.506267741512211, -5.681691755725617},
            {0.0026781005179353436, 0.0024619918761475345, 0.001040407093377684}},
        {8, 2451545.0,
            {16.81205694676573, -22.98009431713926, -9.824426610045538},
            {0.002579281177352459, 0.0016684310193440153, 0.0006188204691558668}},
    };
    for (size_t index = 0; index < sizeof(cases) / sizeof(cases[0]); ++index) {
        taiyin::CartesianState state;
        expect_true(
            eval_direct(
                cases[index].target_id,
                taiyin::TAIYIN_BODY_SUN,
                cases[index].jd,
                &state),
            "evaluate long-range JS parity case");
        for (size_t axis = 0; axis < 3; ++axis) {
            const double position = axis == 0 ? state.position_au.x
                : (axis == 1 ? state.position_au.y : state.position_au.z);
            const double velocity = axis == 0 ? state.velocity_au_per_day.x
                : (axis == 1 ? state.velocity_au_per_day.y
                    : state.velocity_au_per_day.z);
            expect_near(position, cases[index].position[axis], 4.0e-13,
                "long-range JS position parity");
            expect_near(velocity, cases[index].velocity[axis], 4.0e-13,
                "long-range JS velocity parity");
        }
        expect_true(
            std::isfinite(state.acceleration_au_per_day2.x)
                && std::isfinite(state.acceleration_au_per_day2.y)
                && std::isfinite(state.acceleration_au_per_day2.z),
            "long-range analytic acceleration is finite");
    }
}

void test_js_moon_and_pluto_parity() {
    struct Case {
        int target_id;
        int center_id;
        double position[3];
        double velocity[3];
    };
    const Case cases[] = {
        {taiyin::TAIYIN_BODY_MOON, taiyin::TAIYIN_BODY_EARTH,
            {-0.0019492827576548194, -0.0017828918038918897,
                -0.0005087123687275711},
            {0.00037167045615278205, -0.0003846977850820615,
                -0.00017403000000689497}},
        {taiyin::TAIYIN_BODY_PLUTO_BARYCENTER, taiyin::TAIYIN_BODY_SUN,
            {-9.875351871587862, -27.978874590920004,
                -5.753694329913922},
            {0.003028816502935808, -0.001127550237506806,
                -0.0012651092332379875}},
    };
    for (size_t index = 0; index < sizeof(cases) / sizeof(cases[0]); ++index) {
        taiyin::CartesianState state;
        expect_true(
            eval_direct(
                cases[index].target_id,
                cases[index].center_id,
                2451545.0,
                &state),
            "evaluate Moon/Pluto JS parity case");
        for (size_t axis = 0; axis < 3; ++axis) {
            const double position = axis == 0 ? state.position_au.x
                : (axis == 1 ? state.position_au.y : state.position_au.z);
            const double velocity = axis == 0 ? state.velocity_au_per_day.x
                : (axis == 1 ? state.velocity_au_per_day.y
                    : state.velocity_au_per_day.z);
            expect_near(position, cases[index].position[axis], 4.0e-13,
                "Moon/Pluto JS position parity");
            expect_near(velocity, cases[index].velocity[axis], 4.0e-13,
                "Moon/Pluto JS velocity parity");
        }
    }
}

void test_coverage_and_routes() {
    double start = 0.0;
    double end = 0.0;
    expect_true(
        taiyin::internal::get_builtin_long_range_analytic_coverage(
            taiyin::TAIYIN_BODY_MARS_BARYCENTER,
            taiyin::TAIYIN_BODY_SUN,
            &start,
            &end),
        "long-range Mars coverage exists");
    expect_near(start, -470455.0, 0.0, "long-range coverage start");
    expect_near(end, 5373545.0, 0.0, "long-range coverage end");
    expect_true(
        taiyin::internal::get_builtin_long_range_analytic_coverage(
            taiyin::TAIYIN_BODY_MOON, taiyin::TAIYIN_BODY_EARTH, &start, &end),
        "long-range component provides the global lunar route");
    expect_true(
        taiyin::internal::get_builtin_long_range_analytic_coverage(
            taiyin::TAIYIN_BODY_PLUTO_BARYCENTER,
            taiyin::TAIYIN_BODY_SUN,
            &start,
            &end),
        "long-range component provides the Pluto route");
    expect_true(
        !taiyin::internal::get_builtin_long_range_analytic_coverage(
            taiyin::TAIYIN_BODY_SUN, taiyin::TAIYIN_BODY_SSB, &start, &end),
        "long-range component leaves Sun/SSB to the unified composite route");

    taiyin::internal::StorageEphemerisBlock storage;
    expect_true(
        !taiyin::internal::compile_builtin_long_range_analytic_ephemeris_block(
            taiyin::TAIYIN_BODY_MARS_BARYCENTER,
            taiyin::TAIYIN_BODY_SUN,
            -470455.0 - 1.0,
            5373545.0,
            &storage),
        "long-range analytic route refuses pre-coverage extrapolation");
    expect_true(
        !taiyin::internal::compile_builtin_long_range_analytic_ephemeris_block(
            taiyin::TAIYIN_BODY_MARS_BARYCENTER,
            taiyin::TAIYIN_BODY_SUN,
            -470455.0,
            5373545.0 + 1.0,
            &storage),
        "long-range analytic route refuses post-coverage extrapolation");

}

void test_nonfinite_epoch_rejection() {
    const taiyin::SplitJulianDate invalid_epochs[] = {
        {2451545, std::numeric_limits<double>::quiet_NaN()},
        {2451545, std::numeric_limits<double>::infinity()},
        {2451545, -std::numeric_limits<double>::infinity()},
    };
    taiyin::internal::StorageEphemerisBlock storage;
    taiyin::internal::CompiledEphemerisBlock block;
    const bool compiled =
        taiyin::internal::compile_builtin_long_range_analytic_ephemeris_block(
            taiyin::TAIYIN_BODY_MARS_BARYCENTER,
            taiyin::TAIYIN_BODY_SUN,
            -470455.0,
            5373545.0,
            &storage)
        && taiyin::internal::get_compiled_block_from_storage(
            &storage, taiyin::TAIYIN_BODY_MARS_BARYCENTER, &block);
    expect_true(compiled, "compile block for invalid-epoch checks");
    for (size_t index = 0;
         index < sizeof(invalid_epochs) / sizeof(invalid_epochs[0]);
         ++index) {
        taiyin::CartesianState state;
        expect_true(
            !taiyin::internal::eval_builtin_long_range_analytic_state(
                taiyin::TAIYIN_BODY_MARS_BARYCENTER,
                taiyin::TAIYIN_BODY_SUN,
                invalid_epochs[index],
                &state),
            "direct long-range API rejects non-finite epoch");
        if (compiled) {
            expect_true(
                !taiyin::internal::eval_compiled_ephemeris_block(
                    invalid_epochs[index], &block, &state),
                "compiled long-range API rejects non-finite epoch");
        }
        expect_true(
            !taiyin::internal::eval_builtin_long_range_pluto_near_state(
                invalid_epochs[index], &state),
            "direct Pluto near API rejects non-finite epoch");
        expect_true(
            !taiyin::internal::eval_builtin_long_range_pluto_fallback_state(
                invalid_epochs[index], &state),
            "direct Pluto fallback API rejects non-finite epoch");
    }
    taiyin::internal::destroy_storage_ephemeris_block(&storage);
}

}  // namespace

int main() {
    test_js_runtime_parity();
    test_js_moon_and_pluto_parity();
    test_coverage_and_routes();
    test_nonfinite_epoch_rejection();
    if (failures != 0) {
        std::fprintf(stderr, "%d long-range analytical test(s) failed\n", failures);
        return 1;
    }
    std::puts("long-range analytical tests passed");
    return 0;
}
