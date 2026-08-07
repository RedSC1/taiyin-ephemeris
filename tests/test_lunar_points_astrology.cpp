#include "taiyin/angle.h"
#include "taiyin/astrology/lunar_points.h"
#include "taiyin/astrology/targets.h"
#include "taiyin/body_id.h"
#include "taiyin/dispatch.h"
#include "taiyin/physical_constants.h"
#include "taiyin/runtime/native_context.h"
#include "taiyin/runtime/runtime.h"
#include "taiyin/time.h"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <string>

namespace {

const int kTestExactStateTarget = -199999;

taiyin::SplitJulianDate split_jd(double jd) {
    taiyin::SplitJulianDate out;
    taiyin::split_julian_date_from_double(jd, &out);
    return out;
}

taiyin::Status calc_lunar_fitted_apogee_tt(
    const taiyin::runtime::NativeCalcContext* context,
    double jd_tt,
    uint32_t native_position_flags,
    taiyin::astrology::LunarApsisPosition* out,
    taiyin::runtime::EphemerisEvalDiagnostic* diagnostic
) {
    return taiyin::astrology::calc_lunar_fitted_apogee_tt(
        context, split_jd(jd_tt), native_position_flags, out, diagnostic);
}

taiyin::Status eval_test_exact_state_position(
    const taiyin::runtime::NativeCalcContext*,
    int,
    const taiyin::SplitJulianDate&,
    const taiyin::SplitJulianDate&,
    uint32_t,
    double out[6],
    taiyin::runtime::EphemerisEvalDiagnostic*
) noexcept {
    if (!out) return taiyin::TAIYIN_ERROR_INVALID_ARGUMENT;
    out[0] = 1.0;
    out[1] = 2.0;
    out[2] = 3.0;
    out[3] = 4.0;
    out[4] = 5.0;
    out[5] = 6.0;
    return taiyin::TAIYIN_STATUS_OK;
}

taiyin::Status eval_test_exact_state(
    const taiyin::runtime::NativeCalcContext*,
    int,
    const taiyin::SplitJulianDate&,
    const taiyin::SplitJulianDate&,
    uint32_t,
    taiyin::CartesianState* out,
    taiyin::runtime::EphemerisEvalDiagnostic*
) noexcept {
    if (!out) return taiyin::TAIYIN_ERROR_INVALID_ARGUMENT;
    out->position_au = taiyin::Vector3{1.0, 2.0, 3.0};
    out->velocity_au_per_day = taiyin::Vector3{4.0, 5.0, 6.0};
    out->acceleration_au_per_day2 = taiyin::Vector3{7.0, 8.0, 9.0};
    return taiyin::TAIYIN_STATUS_OK;
}

void expect_status(taiyin::Status actual, taiyin::Status expected, const char* label, int* failures) {
    if (actual != expected) {
        std::cerr << "FAIL: " << label << " actual=" << actual << " expected=" << expected << "\n";
        ++*failures;
    }
}

void expect_true(bool value, const char* label, int* failures) {
    if (!value) {
        std::cerr << "FAIL: " << label << "\n";
        ++*failures;
    }
}

void expect_near(double actual, double expected, double tolerance, const char* label, int* failures) {
    if (std::fabs(actual - expected) > tolerance) {
        std::cerr << "FAIL: " << label << " actual=" << actual << " expected=" << expected << "\n";
        ++*failures;
    }
}

bool initialize_runtime(int* failures) {
    const char* root = std::getenv("TAIYIN_REPO_ROOT");
    if (!root || root[0] == '\0') {
        std::cerr << "FAIL: TAIYIN_REPO_ROOT is required for lunar point tests\n";
        ++*failures;
        return false;
    }
    const std::string opm2_root = std::string(root) + "/data/ephemerides/opm2/major-bodies/600y";
    const char* paths[] = { opm2_root.c_str() };
    taiyin::runtime::EphemerisRuntimeConfig config;
    config.source_paths = paths;
    config.source_path_count = 1;
    config.load_packaged_data = false;
    config.segment_cache_max_entries = 64;
    if (!taiyin::runtime::initialize_global_ephemeris_runtime(config)) {
        std::cerr << "FAIL: initialize OPM2 runtime for lunar point tests\n";
        ++*failures;
        return false;
    }
    return true;
}

}  // namespace

int main() {
    using namespace taiyin;
    using namespace taiyin::astrology;
    using namespace taiyin::runtime;

    int failures = 0;
    if (!initialize_runtime(&failures)) return 1;

    expect_status(
        register_builtin_astrology_targets(),
        TAIYIN_STATUS_OK,
        "register lunar-point target evaluators",
        &failures);
    expect_status(
        register_builtin_astrology_targets(),
        TAIYIN_STATUS_OK,
        "repeat lunar-point target evaluator registration",
        &failures);
    expect_true(
        register_global_native_position_evaluator(
            kTestExactStateTarget, eval_test_exact_state_position, eval_test_exact_state),
        "register target evaluator with an exact state callback",
        &failures);

    const SplitJulianDate jd_tt(2460409, 0.0);
    const SplitJulianDate jd_tdb = jd_tt;
    NativeCalcContext context;
    CartesianState exact_state;
    expect_status(
        calc_state_tt(&context, kTestExactStateTarget, jd_tt, 0u, &exact_state, nullptr),
        TAIYIN_STATUS_OK,
        "use an evaluator-provided exact state",
        &failures);
    expect_true(
        exact_state.position_au.x == 1.0 && exact_state.velocity_au_per_day.y == 5.0
            && exact_state.acceleration_au_per_day2.z == 9.0,
        "exact state callback bypasses the finite-difference fallback",
        &failures);
    LunarTrueNodePosition ascending;
    expect_status(
        calc_lunar_true_node_tt(
            &context, jd_tt, TAIYIN_LUNAR_NODE_ASCENDING, 0u, &ascending, nullptr),
        TAIYIN_STATUS_OK,
        "evaluate default true ascending node",
        &failures);
    expect_true(
        ascending.reference_frame_id == TAIYIN_APPARENT_FRAME_TRUE_ECLIPTIC_OF_DATE
            && std::isfinite(ascending.longitude_rad)
            && std::isfinite(ascending.longitude_rate_rad_per_day),
        "default true node has finite true-ecliptic result",
        &failures);

    double routed_true_node[6];
    EphemerisEvalDiagnostic routed_true_node_diagnostic;
    expect_status(
        calc_position_tt(
            &context,
            TAIYIN_ASTROLOGY_TARGET_TRUE_NODE,
            jd_tt,
            TAIYIN_NATIVE_POSITION_SPEED | TAIYIN_NATIVE_POSITION_RADIANS,
            routed_true_node,
            &routed_true_node_diagnostic),
        TAIYIN_STATUS_OK,
        "evaluate true node through standard position route",
        &failures);
    expect_near(
        routed_true_node[0],
        ascending.longitude_rad,
        1.0e-14,
        "routed true node longitude matches direct evaluator",
        &failures);
    expect_near(
        routed_true_node[3],
        ascending.longitude_rate_rad_per_day,
        1.0e-14,
        "routed true node rate matches direct evaluator",
        &failures);
    expect_true(
        routed_true_node[1] == 0.0 && std::isnan(routed_true_node[2])
            && routed_true_node[4] == 0.0 && std::isnan(routed_true_node[5])
            && routed_true_node_diagnostic.target_id == TAIYIN_ASTROLOGY_TARGET_TRUE_NODE
            && routed_true_node_diagnostic.component_target_id == TAIYIN_BODY_MOON,
        "routed true node exposes a direction-only result with Moon provenance",
        &failures);

    double tdb_only_true_node[6];
    expect_status(
        calc_position_tdb(
            &context,
            TAIYIN_ASTROLOGY_TARGET_TRUE_NODE,
            jd_tdb,
            SplitJulianDate(0, NAN),
            TAIYIN_NATIVE_POSITION_SPEED | TAIYIN_NATIVE_POSITION_RADIANS
                | TAIYIN_NATIVE_POSITION_ALLOW_BARYCENTER_APPROX,
            tdb_only_true_node,
            nullptr),
        TAIYIN_STATUS_OK,
        "evaluate true node through the TDB-only route",
        &failures);
    expect_near(
        tdb_only_true_node[0],
        routed_true_node[0],
        1.0e-14,
        "TDB-only true node normalizes its missing TT input",
        &failures);

    EphemerisEvalDiagnostic reused_diagnostic = routed_true_node_diagnostic;
    double routed_mean_node[6];
    expect_status(
        calc_position_tt(
            &context,
            TAIYIN_ASTROLOGY_TARGET_MEAN_NODE,
            jd_tt,
            TAIYIN_NATIVE_POSITION_RADIANS,
            routed_mean_node,
            &reused_diagnostic),
        TAIYIN_STATUS_OK,
        "evaluate mean node with a reused diagnostic",
        &failures);
    expect_true(
        reused_diagnostic.target_id == TAIYIN_ASTROLOGY_TARGET_MEAN_NODE
            && reused_diagnostic.component_target_id == 0,
        "mean node clears stale component provenance from a reused diagnostic",
        &failures);

    double cartesian_node[6];
    expect_status(
        calc_position_tt(
            &context,
            TAIYIN_ASTROLOGY_TARGET_TRUE_NODE,
            jd_tt,
            TAIYIN_NATIVE_POSITION_XYZ,
            cartesian_node,
            nullptr),
        TAIYIN_STATUS_OK,
        "allow Cartesian request for direction-only node",
        &failures);
    expect_true(
        std::isnan(cartesian_node[0]) && std::isnan(cartesian_node[1])
            && std::isnan(cartesian_node[2]),
        "direction-only node reports undefined Cartesian position",
        &failures);
    double topocentric_node[6];
    expect_status(
        calc_position_tt(
            &context,
            TAIYIN_ASTROLOGY_TARGET_TRUE_NODE,
            jd_tt,
            TAIYIN_NATIVE_POSITION_RADIANS | TAIYIN_NATIVE_POSITION_TOPOCENTRIC,
            topocentric_node,
            nullptr),
        TAIYIN_STATUS_OK,
        "allow topocentric request for conventional node",
        &failures);
    expect_near(
        topocentric_node[0],
        routed_true_node[0],
        1.0e-14,
        "conventional node retains its geocentric direction under topocentric presentation",
        &failures);
    CartesianState node_state;
    expect_status(
        calc_state_tt(
            &context,
            TAIYIN_ASTROLOGY_TARGET_TRUE_NODE,
            jd_tt,
            0u,
            &node_state,
            nullptr),
        TAIYIN_STATUS_OK,
        "allow Cartesian state request for direction-only node",
        &failures);
    expect_true(
        std::isnan(node_state.position_au.x) && std::isnan(node_state.velocity_au_per_day.x)
            && std::isnan(node_state.acceleration_au_per_day2.x),
        "direction-only node reports undefined Cartesian state",
        &failures);

    NativeCalcContext aberrated_context = context;
    expect_status(
        native_context_use_solar_deflector(&aberrated_context),
        TAIYIN_STATUS_OK,
        "configure solar deflector for aberrated lunar points",
        &failures);
    aberrated_context.apparent_options.flags = TAIYIN_APPARENT_SPHERICAL
        | TAIYIN_APPARENT_LIGHT_TIME
        | TAIYIN_APPARENT_ABERRATION;
    LunarTrueNodePosition aberrated_node;
    LunarTrueNodePosition unaberrated_node;
    expect_status(
        calc_lunar_true_node_tt(
            &aberrated_context, jd_tt, TAIYIN_LUNAR_NODE_ASCENDING,
            0u, &aberrated_node, nullptr),
        TAIYIN_STATUS_OK,
        "evaluate aberration-corrected true node",
        &failures);
    expect_status(
        calc_lunar_true_node_tt(
            &aberrated_context, jd_tt, TAIYIN_LUNAR_NODE_ASCENDING,
            TAIYIN_NATIVE_POSITION_NO_ABERR, &unaberrated_node, nullptr),
        TAIYIN_STATUS_OK,
        "evaluate true node without aberration",
        &failures);
    expect_true(
        std::fabs(normalize_signed_radians(
            aberrated_node.longitude_rad - unaberrated_node.longitude_rad)) > 1.0e-13,
        "annual aberration changes the true-node direction",
        &failures);
    expect_near(
        unaberrated_node.longitude_rad,
        ascending.longitude_rad,
        1.0e-14,
        "NO_ABERR matches the default unaberrated true-node path",
        &failures);

    const double rate_step_days = 1.0e-3;
    LunarTrueNodePosition before;
    LunarTrueNodePosition after;
    expect_status(
        calc_lunar_true_node_tt(
            &context, jd_tt - rate_step_days, TAIYIN_LUNAR_NODE_ASCENDING,
            0u, &before, nullptr),
        TAIYIN_STATUS_OK,
        "evaluate true node before rate check",
        &failures);
    expect_status(
        calc_lunar_true_node_tt(
            &context, jd_tt + rate_step_days, TAIYIN_LUNAR_NODE_ASCENDING,
            0u, &after, nullptr),
        TAIYIN_STATUS_OK,
        "evaluate true node after rate check",
        &failures);
    expect_near(
        ascending.longitude_rate_rad_per_day,
        normalize_signed_radians(after.longitude_rad - before.longitude_rad)
            / (2.0 * rate_step_days),
        1.0e-8,
        "analytic true-node longitude rate agrees with a centered difference",
        &failures);

    LunarTrueNodePosition descending;
    expect_status(
        calc_lunar_true_node_tt(
            &context, jd_tt, TAIYIN_LUNAR_NODE_DESCENDING, 0u, &descending, nullptr),
        TAIYIN_STATUS_OK,
        "evaluate default true descending node",
        &failures);
    expect_near(
        std::fabs(normalize_signed_radians(descending.longitude_rad - ascending.longitude_rad)),
        TAIYIN_PI,
        1.0e-13,
        "descending node is opposite ascending node",
        &failures);
    expect_near(
        descending.longitude_rate_rad_per_day,
        ascending.longitude_rate_rad_per_day,
        1.0e-15,
        "opposite node has the same longitude rate",
        &failures);

    LunarTrueNodePosition true_position;
    expect_status(
        calc_lunar_true_node_tt(
            &context, jd_tt, TAIYIN_LUNAR_NODE_ASCENDING,
            TAIYIN_NATIVE_POSITION_TRUEPOS, &true_position, nullptr),
        TAIYIN_STATUS_OK,
        "evaluate geometric true node",
        &failures);
    expect_true(
        std::fabs(normalize_signed_radians(
            ascending.longitude_rad - true_position.longitude_rad)) > 1.0e-11,
        "TRUEPOS changes the osculating node from the default light-time result",
        &failures);

    NativeCalcContext mean_context = context;
    mean_context.apparent_options.output_frame_id = TAIYIN_APPARENT_FRAME_MEAN_ECLIPTIC_OF_DATE;
    LunarTrueNodePosition nonut;
    LunarTrueNodePosition mean;
    expect_status(
        calc_lunar_true_node_tt(
            &context, jd_tt, TAIYIN_LUNAR_NODE_ASCENDING,
            TAIYIN_NATIVE_POSITION_NONUT, &nonut, nullptr),
        TAIYIN_STATUS_OK,
        "evaluate NONUT true node",
        &failures);
    expect_status(
        calc_lunar_true_node_tt(
            &mean_context, jd_tt, TAIYIN_LUNAR_NODE_ASCENDING,
            0u, &mean, nullptr),
        TAIYIN_STATUS_OK,
        "evaluate mean-ecliptic true node",
        &failures);
    expect_true(
        nonut.reference_frame_id == TAIYIN_APPARENT_FRAME_MEAN_ECLIPTIC_OF_DATE,
        "NONUT reports the effective mean ecliptic frame",
        &failures);
    expect_near(
        nonut.longitude_rad,
        mean.longitude_rad,
        1.0e-14,
        "NONUT agrees with direct mean-ecliptic node",
        &failures);

    LunarTrueNodePosition geometric_mean;
    expect_status(
        calc_lunar_true_node_tt(
            &mean_context, jd_tt, TAIYIN_LUNAR_NODE_ASCENDING,
            TAIYIN_NATIVE_POSITION_TRUEPOS, &geometric_mean, nullptr),
        TAIYIN_STATUS_OK,
        "evaluate geometric mean-ecliptic true node",
        &failures);
    // Swiss Ephemeris Moshier, swe_calc(2460409.0, SE_TRUE_NODE,
    // SEFLG_MOSEPH | SEFLG_SPEED | SEFLG_NONUT | SEFLG_TRUEPOS). The
    // osculating node uses both lunar position and velocity, so independent
    // OPM2 and Moshier lunar theories differ by a few arcseconds here.
    expect_near(
        geometric_mean.longitude_rad * TAIYIN_RAD_TO_DEG,
        15.627613595150201,
        5.0 / 3600.0,
        "Swiss Moshier true-node longitude oracle",
        &failures);

    const SplitJulianDate jd_ut(2460409, 0.0);
    SplitJulianDate matching_jd_tt;
    expect_true(
        ut1_to_tt_split_jd(
            jd_ut,
            dispatch::eval_delta_t_with_ephemeris_correction(
                context.delta_t_model_id, context.ephemeris_family_id,
                jd_ut, nullptr, nullptr),
            &matching_jd_tt),
        "convert matching UT epoch to TT",
        &failures);
    LunarTrueNodePosition ut_node;
    LunarTrueNodePosition tt_node;
    expect_status(
        calc_lunar_true_node_ut(
            &context, jd_ut, TAIYIN_LUNAR_NODE_ASCENDING, 0u, &ut_node, nullptr),
        TAIYIN_STATUS_OK,
        "evaluate UT true node",
        &failures);
    expect_status(
        calc_lunar_true_node_tt(
            &context, matching_jd_tt, TAIYIN_LUNAR_NODE_ASCENDING, 0u, &tt_node, nullptr),
        TAIYIN_STATUS_OK,
        "evaluate matching TT true node",
        &failures);
    expect_near(
        ut_node.longitude_rad,
        tt_node.longitude_rad,
        1.0e-14,
        "UT and TT true node agree with the active Delta-T model",
        &failures);

    NativeCalcContext topocentric_context = context;
    const NativeObserverLocation location = native_observer_location_degrees(116.4, 39.9, 45.0);
    expect_status(
        native_context_set_simple_topocentric_observer(
            &topocentric_context, location, jd_tt, jd_tt),
        TAIYIN_STATUS_OK,
        "install a topocentric observer",
        &failures);
    LunarTrueNodePosition from_topocentric_context;
    expect_status(
        calc_lunar_true_node_tt(
            &topocentric_context, jd_tt, TAIYIN_LUNAR_NODE_ASCENDING, 0u,
            &from_topocentric_context, nullptr),
        TAIYIN_STATUS_OK,
        "topocentric context normalizes to geocentric true node",
        &failures);
    expect_near(
        from_topocentric_context.longitude_rad,
        ascending.longitude_rad,
        1.0e-14,
        "observer location does not change a geocentric node",
        &failures);

    LunarTrueNodePosition unused;
    expect_status(
        calc_lunar_true_node_tt(
            &context, jd_tt, TAIYIN_LUNAR_NODE_ASCENDING,
            TAIYIN_NATIVE_POSITION_TOPOCENTRIC, &unused, nullptr),
        TAIYIN_ERROR_INVALID_ARGUMENT,
        "reject explicitly topocentric node request",
        &failures);
    expect_status(
        calc_lunar_true_node_tt(
            &context, jd_tt, TAIYIN_LUNAR_NODE_ASCENDING,
            TAIYIN_NATIVE_POSITION_XYZ, &unused, nullptr),
        TAIYIN_ERROR_INVALID_ARGUMENT,
        "reject Cartesian output-shape flag",
        &failures);

    NativeCalcContext equatorial_context = context;
    equatorial_context.apparent_options.output_frame_id = TAIYIN_APPARENT_FRAME_TRUE_EQUATOR_OF_DATE;
    LunarTrueNodePosition equatorial;
    expect_status(
        calc_lunar_true_node_tt(
            &equatorial_context, jd_tt, TAIYIN_LUNAR_NODE_ASCENDING, 0u, &equatorial, nullptr),
        TAIYIN_STATUS_OK,
        "evaluate true node on the true equator of date",
        &failures);
    expect_true(
        equatorial.reference_frame_id == TAIYIN_APPARENT_FRAME_TRUE_EQUATOR_OF_DATE
            && std::isfinite(equatorial.longitude_rad),
        "equatorial context selects an equatorial node",
        &failures);
    LunarTrueNodePosition equatorial_flag;
    expect_status(
        calc_lunar_true_node_tt(
            &context, jd_tt, TAIYIN_LUNAR_NODE_ASCENDING,
            TAIYIN_NATIVE_POSITION_EQUATORIAL, &equatorial_flag, nullptr),
        TAIYIN_STATUS_OK,
        "EQUATORIAL flag selects an equatorial node",
        &failures);
    expect_near(
        equatorial_flag.longitude_rad,
        equatorial.longitude_rad,
        1.0e-14,
        "EQUATORIAL flag agrees with an equatorial context",
        &failures);

    LunarNodePosition mean_ascending;
    expect_status(
        calc_lunar_mean_node_tt(
            &mean_context, jd_tt, TAIYIN_LUNAR_NODE_ASCENDING, 0u,
            &mean_ascending, nullptr),
        TAIYIN_STATUS_OK,
        "evaluate IERS mean ascending node",
        &failures);
    expect_true(
        mean_ascending.reference_frame_id == TAIYIN_APPARENT_FRAME_MEAN_ECLIPTIC_OF_DATE,
        "mean node reports its selected mean-ecliptic frame",
        &failures);
    // Direct IERS 2003 Delaunay Omega(T), in the mean ecliptic of date.
    expect_near(
        mean_ascending.longitude_rad * TAIYIN_RAD_TO_DEG,
        15.662505452962762,
        1.0e-11,
        "mean node matches IERS 2003 Omega",
        &failures);
    // Swiss Ephemeris Moshier, swe_calc(2460409.0, SE_MEAN_NODE,
    // SEFLG_MOSEPH | SEFLG_SPEED | SEFLG_NONUT | SEFLG_TRUEPOS).
    expect_near(
        mean_ascending.longitude_rad * TAIYIN_RAD_TO_DEG,
        15.66249222226472,
        0.1 / 3600.0,
        "Swiss Moshier mean-node sanity oracle",
        &failures);

    LunarNodePosition mean_descending;
    expect_status(
        calc_lunar_mean_node_tt(
            &mean_context, jd_tt, TAIYIN_LUNAR_NODE_DESCENDING, 0u,
            &mean_descending, nullptr),
        TAIYIN_STATUS_OK,
        "evaluate IERS mean descending node",
        &failures);
    expect_near(
        std::fabs(normalize_signed_radians(
            mean_descending.longitude_rad - mean_ascending.longitude_rad)),
        TAIYIN_PI,
        1.0e-13,
        "mean descending node is opposite ascending node",
        &failures);
    NativeCalcContext mean_equatorial_context = mean_context;
    mean_equatorial_context.apparent_options.output_frame_id =
        TAIYIN_APPARENT_FRAME_TRUE_EQUATOR_OF_DATE;
    LunarNodePosition mean_equatorial;
    LunarNodePosition mean_equatorial_flag;
    expect_status(
        calc_lunar_mean_node_tt(
            &mean_equatorial_context, jd_tt, TAIYIN_LUNAR_NODE_ASCENDING, 0u,
            &mean_equatorial, nullptr),
        TAIYIN_STATUS_OK,
        "evaluate mean node on the true equator of date",
        &failures);
    expect_status(
        calc_lunar_mean_node_tt(
            &mean_context, jd_tt, TAIYIN_LUNAR_NODE_ASCENDING,
            TAIYIN_NATIVE_POSITION_EQUATORIAL, &mean_equatorial_flag, nullptr),
        TAIYIN_STATUS_OK,
        "EQUATORIAL flag selects an equatorial mean node",
        &failures);
    expect_true(
        mean_equatorial.reference_frame_id == TAIYIN_APPARENT_FRAME_TRUE_EQUATOR_OF_DATE,
        "mean node reports its selected true-equator frame",
        &failures);
    expect_near(
        mean_equatorial.longitude_rad,
        mean_equatorial_flag.longitude_rad,
        1.0e-14,
        "EQUATORIAL flag agrees with an equatorial mean-node context",
        &failures);
    // Swiss Ephemeris Moshier, swe_calc(2460409.0, SE_MEAN_NODE,
    // SEFLG_MOSEPH | SEFLG_SPEED | SEFLG_EQUATORIAL | SEFLG_TRUEPOS).
    expect_near(
        mean_equatorial.longitude_rad * TAIYIN_RAD_TO_DEG,
        14.424934890481865,
        0.1 / 3600.0,
        "Swiss Moshier equatorial mean-node sanity oracle",
        &failures);
    expect_status(
        calc_lunar_mean_node_tt(
            &mean_context, jd_tt, TAIYIN_LUNAR_NODE_ASCENDING,
            TAIYIN_NATIVE_POSITION_TRUEPOS, &unused, nullptr),
        TAIYIN_ERROR_INVALID_ARGUMENT,
        "reject apparent correction flags for an IERS mean node",
        &failures);

    LunarApsisPosition mean_apogee;
    expect_status(
        calc_lunar_mean_apogee_tt(&mean_context, jd_tt, 0u, &mean_apogee, nullptr),
        TAIYIN_STATUS_OK,
        "evaluate Delaunay mean apogee",
        &failures);
    expect_true(
        mean_apogee.definition == TAIYIN_LUNAR_APSIS_DELAUNAY_MEAN
            && std::isnan(mean_apogee.distance_au)
            && std::isnan(mean_apogee.distance_rate_au_per_day),
        "mean apogee exposes a conventional direction rather than a made-up radius",
        &failures);
    // Direct IERS 2003 Delaunay arguments, with a 5.145396-degree mean lunar
    // orbital inclination rotated about the mean node.
    expect_near(
        mean_apogee.longitude_rad * TAIYIN_RAD_TO_DEG,
        170.92150432407695,
        1.0e-11,
        "mean apogee matches its explicit Delaunay definition",
        &failures);
    expect_near(
        mean_apogee.latitude_rad * TAIYIN_RAD_TO_DEG,
        2.1582226032549934,
        1.0e-11,
        "mean apogee includes the mean-orbit inclination",
        &failures);
    // Swiss Ephemeris Moshier, swe_calc(2460409.0, SE_MEAN_APOG,
    // SEFLG_MOSEPH | SEFLG_SPEED | SEFLG_NONUT | SEFLG_TRUEPOS).
    expect_near(
        mean_apogee.longitude_rad * TAIYIN_RAD_TO_DEG,
        170.92149105482432,
        0.1 / 3600.0,
        "Swiss Moshier mean-apogee longitude sanity oracle",
        &failures);
    expect_near(
        mean_apogee.latitude_rad * TAIYIN_RAD_TO_DEG,
        2.1582227749284364,
        0.1 / 3600.0,
        "Swiss Moshier mean-apogee latitude sanity oracle",
        &failures);

    LunarApsisPosition osculating_apogee;
    expect_status(
        calc_lunar_osculating_apogee_tt(
            &mean_context, jd_tt,
            TAIYIN_NATIVE_POSITION_TRUEPOS | TAIYIN_NATIVE_POSITION_NONUT,
            &osculating_apogee, nullptr),
        TAIYIN_STATUS_OK,
        "evaluate geometric osculating apogee",
        &failures);
    expect_true(
        osculating_apogee.definition == TAIYIN_LUNAR_APSIS_OSCULATING_TWO_BODY
            && osculating_apogee.distance_au > 0.0
            && std::isfinite(osculating_apogee.distance_rate_au_per_day),
        "osculating apogee exposes the instantaneous two-body apoapsis distance",
        &failures);
    CartesianState routed_osculating_state;
    expect_status(
        calc_state_tt(
            &mean_context,
            TAIYIN_ASTROLOGY_TARGET_OSCULATING_LILITH,
            jd_tt,
            TAIYIN_NATIVE_POSITION_TRUEPOS | TAIYIN_NATIVE_POSITION_NONUT,
            &routed_osculating_state,
            nullptr),
        TAIYIN_STATUS_OK,
        "evaluate osculating Lilith state through the standard target route",
        &failures);
    expect_true(
        std::isfinite(routed_osculating_state.position_au.x)
            && std::isfinite(routed_osculating_state.velocity_au_per_day.x)
            && std::isfinite(routed_osculating_state.acceleration_au_per_day2.x),
        "osculating Lilith state derives a finite-difference acceleration",
        &failures);
    LunarApsisPosition default_apparent_apogee;
    expect_status(
        calc_lunar_osculating_apogee_tt(
            &context, jd_tt, 0u, &default_apparent_apogee, nullptr),
        TAIYIN_STATUS_OK,
        "evaluate default apparent osculating apogee",
        &failures);
    LunarApsisPosition aberrated_apogee;
    LunarApsisPosition unaberrated_apogee;
    expect_status(
        calc_lunar_osculating_apogee_tt(
            &aberrated_context, jd_tt, 0u, &aberrated_apogee, nullptr),
        TAIYIN_STATUS_OK,
        "evaluate aberration-corrected osculating apogee",
        &failures);
    expect_status(
        calc_lunar_osculating_apogee_tt(
            &aberrated_context, jd_tt, TAIYIN_NATIVE_POSITION_NO_ABERR,
            &unaberrated_apogee, nullptr),
        TAIYIN_STATUS_OK,
        "evaluate osculating apogee without aberration",
        &failures);
    expect_true(
        std::fabs(normalize_signed_radians(
            aberrated_apogee.longitude_rad - unaberrated_apogee.longitude_rad)) > 1.0e-13,
        "annual aberration changes the osculating-apogee direction",
        &failures);
    expect_near(
        unaberrated_apogee.longitude_rad,
        default_apparent_apogee.longitude_rad,
        1.0e-14,
        "NO_ABERR matches the default unaberrated osculating-apogee path",
        &failures);
    // Swiss Ephemeris Moshier, swe_calc(2460409.0, SE_OSCU_APOG,
    // SEFLG_MOSEPH | SEFLG_SPEED | SEFLG_NONUT | SEFLG_TRUEPOS). Osculating
    // apsides amplify small lunar velocity-model differences; OPM2 differs
    // from Moshier by about 43 arcseconds for longitude at this epoch.
    expect_near(
        osculating_apogee.longitude_rad * TAIYIN_RAD_TO_DEG,
        182.7274859203948,
        1.0 / 60.0,
        "Swiss Moshier osculating-apogee longitude sanity oracle",
        &failures);
    expect_near(
        osculating_apogee.latitude_rad * TAIYIN_RAD_TO_DEG,
        1.1848330179481326,
        5.0 / 3600.0,
        "Swiss Moshier osculating-apogee latitude sanity oracle",
        &failures);

    LunarApsisPosition osculating_before;
    LunarApsisPosition osculating_after;
    expect_status(
        calc_lunar_osculating_apogee_tt(
            &mean_context, jd_tt - rate_step_days,
            TAIYIN_NATIVE_POSITION_TRUEPOS | TAIYIN_NATIVE_POSITION_NONUT,
            &osculating_before, nullptr),
        TAIYIN_STATUS_OK,
        "evaluate osculating apogee before rate check",
        &failures);
    expect_status(
        calc_lunar_osculating_apogee_tt(
            &mean_context, jd_tt + rate_step_days,
            TAIYIN_NATIVE_POSITION_TRUEPOS | TAIYIN_NATIVE_POSITION_NONUT,
            &osculating_after, nullptr),
        TAIYIN_STATUS_OK,
        "evaluate osculating apogee after rate check",
        &failures);
    expect_near(
        osculating_apogee.longitude_rate_rad_per_day,
        normalize_signed_radians(
            osculating_after.longitude_rad - osculating_before.longitude_rad)
            / (2.0 * rate_step_days),
        1.0e-7,
        "analytic osculating-apogee longitude rate agrees with a centered difference",
        &failures);
    expect_near(
        osculating_apogee.latitude_rate_rad_per_day,
        (osculating_after.latitude_rad - osculating_before.latitude_rad)
            / (2.0 * rate_step_days),
        1.0e-7,
        "analytic osculating-apogee latitude rate agrees with a centered difference",
        &failures);
    expect_status(
        calc_lunar_osculating_apogee_tt(
            &context, jd_tt, TAIYIN_NATIVE_POSITION_TOPOCENTRIC, &osculating_apogee, nullptr),
        TAIYIN_ERROR_INVALID_ARGUMENT,
        "reject explicitly topocentric osculating apogee request",
        &failures);

    const double de441_apogee_jd_tt = 2460420.5913274437;
    LunarApsisPosition fitted_apogee;
    expect_status(
        calc_lunar_fitted_apogee_tt(
            &mean_context, de441_apogee_jd_tt, 0u, &fitted_apogee, nullptr),
        TAIYIN_STATUS_OK,
        "evaluate DE441-fitted natural apogee",
        &failures);
    expect_true(
        fitted_apogee.definition == TAIYIN_LUNAR_APSIS_DE441_FITTED_NATURAL
            && !fitted_apogee.extrapolated
            && fitted_apogee.distance_au > 0.0
            && std::isfinite(fitted_apogee.longitude_rate_rad_per_day)
            && std::isfinite(fitted_apogee.latitude_rate_rad_per_day)
            && std::isfinite(fitted_apogee.distance_rate_au_per_day),
        "fitted natural apogee exposes a continuous position and velocity",
        &failures);
    // Independent jplephem extraction from NASA/JPL DE441:
    // r dot v = 0 at JD 2460420.5913274437 TDB, transformed with IAU 2006
    // precession into the mean ecliptic of date. The fit's event-time error
    // contributes to the angular tolerance as well as the direction series.
    expect_near(
        fitted_apogee.longitude_rad,
        2.927240809794924,
        1.0 / 60.0 * TAIYIN_DEG_TO_RAD,
        "DE441 fitted-apogee longitude oracle",
        &failures);
    expect_near(
        fitted_apogee.latitude_rad,
        0.043237146853955986,
        1.0 / 60.0 * TAIYIN_DEG_TO_RAD,
        "DE441 fitted-apogee latitude oracle",
        &failures);
    expect_near(
        fitted_apogee.distance_au,
        0.0027114252593244,
        12.1 / TAIYIN_AU_KM,
        "DE441 fitted-apogee distance oracle",
        &failures);
    LunarApsisPosition fitted_ut;
    LunarApsisPosition fitted_matching_tt;
    expect_status(
        calc_lunar_fitted_apogee_ut(
            &mean_context, jd_ut, 0u, &fitted_ut, nullptr),
        TAIYIN_STATUS_OK,
        "evaluate fitted apogee in UT",
        &failures);
    expect_status(
        calc_lunar_fitted_apogee_tt(
            &mean_context, matching_jd_tt, 0u, &fitted_matching_tt, nullptr),
        TAIYIN_STATUS_OK,
        "evaluate fitted apogee at matching TT",
        &failures);
    expect_near(
        fitted_ut.longitude_rad,
        fitted_matching_tt.longitude_rad,
        1.0e-14,
        "UT and TT fitted apogee agree with the active Delta-T model",
        &failures);
    expect_status(
        calc_lunar_fitted_apogee_tt(
            &mean_context, jd_tt, TAIYIN_NATIVE_POSITION_TRUEPOS,
            &fitted_matching_tt, nullptr),
        TAIYIN_ERROR_INVALID_ARGUMENT,
        "reject apparent correction flags for the fixed fitted-apogee model",
        &failures);

    LunarApsisPosition fitted_before;
    LunarApsisPosition fitted_after;
    expect_status(
        calc_lunar_fitted_apogee_tt(
            &mean_context, jd_tt - rate_step_days, 0u, &fitted_before, nullptr),
        TAIYIN_STATUS_OK,
        "evaluate fitted apogee before rate check",
        &failures);
    expect_status(
        calc_lunar_fitted_apogee_tt(
            &mean_context, jd_tt + rate_step_days, 0u, &fitted_after, nullptr),
        TAIYIN_STATUS_OK,
        "evaluate fitted apogee after rate check",
        &failures);
    LunarApsisPosition fitted_at_rate_epoch;
    expect_status(
        calc_lunar_fitted_apogee_tt(
            &mean_context, jd_tt, 0u, &fitted_at_rate_epoch, nullptr),
        TAIYIN_STATUS_OK,
        "evaluate fitted apogee at rate epoch",
        &failures);
    expect_near(
        fitted_at_rate_epoch.longitude_rate_rad_per_day,
        normalize_signed_radians(
            fitted_after.longitude_rad - fitted_before.longitude_rad)
            / (2.0 * rate_step_days),
        1.0e-8,
        "fitted-apogee longitude rate agrees with a centered difference",
        &failures);
    expect_near(
        fitted_at_rate_epoch.latitude_rate_rad_per_day,
        (fitted_after.latitude_rad - fitted_before.latitude_rad)
            / (2.0 * rate_step_days),
        1.0e-8,
        "fitted-apogee latitude rate agrees with a centered difference",
        &failures);
    expect_near(
        fitted_at_rate_epoch.distance_rate_au_per_day,
        (fitted_after.distance_au - fitted_before.distance_au)
            / (2.0 * rate_step_days),
        1.0e-10,
        "fitted-apogee distance rate agrees with a centered difference",
        &failures);

    LunarApsisPosition fitted_coverage_edge;
    LunarApsisPosition fitted_extrapolated;
    expect_status(
        calc_lunar_fitted_apogee_tt(
            &mean_context, -3100015.5, 0u, &fitted_coverage_edge, nullptr),
        TAIYIN_STATUS_OK,
        "evaluate fitted apogee at the DE441 coverage edge",
        &failures);
    expect_status(
        calc_lunar_fitted_apogee_tt(
            &mean_context, -3100016.5, 0u, &fitted_extrapolated, nullptr),
        TAIYIN_STATUS_OK,
        "extrapolate fitted apogee beyond the DE441 coverage edge",
        &failures);
    expect_true(
        !fitted_coverage_edge.extrapolated && fitted_extrapolated.extrapolated,
        "fitted apogee reports boundary-segment extrapolation",
        &failures);
    LunarApsisPosition fitted_far_extrapolated;
    expect_status(
        calc_lunar_fitted_apogee_tt(
            &mean_context, 9000016.5, 0u, &fitted_far_extrapolated, nullptr),
        TAIYIN_STATUS_OK,
        "extrapolate fitted apogee when the mean seed no longer brackets the event",
        &failures);
    expect_true(
        fitted_far_extrapolated.extrapolated
            && std::isfinite(fitted_far_extrapolated.longitude_rad)
            && std::isfinite(fitted_far_extrapolated.latitude_rad)
            && std::isfinite(fitted_far_extrapolated.distance_au),
        "far fitted-apogee extrapolation remains finite and reports its status",
        &failures);
    const double fitted_segment_boundary_jd = -2734765.5;
    LunarApsisPosition fitted_boundary_before;
    LunarApsisPosition fitted_boundary_after;
    expect_status(
        calc_lunar_fitted_apogee_tt(
            &mean_context, fitted_segment_boundary_jd - 1.0e-4, 0u,
            &fitted_boundary_before, nullptr),
        TAIYIN_STATUS_OK,
        "evaluate fitted apogee before a fit-segment boundary",
        &failures);
    expect_status(
        calc_lunar_fitted_apogee_tt(
            &mean_context, fitted_segment_boundary_jd + 1.0e-4, 0u,
            &fitted_boundary_after, nullptr),
        TAIYIN_STATUS_OK,
        "evaluate fitted apogee after a fit-segment boundary",
        &failures);
    expect_true(
        std::fabs(normalize_signed_radians(
            fitted_boundary_after.longitude_rad
                - fitted_boundary_before.longitude_rad)) < 1.0e-5
            && std::fabs(
                fitted_boundary_after.latitude_rad
                    - fitted_boundary_before.latitude_rad) < 1.0e-5
            && std::fabs(
                fitted_boundary_after.distance_au
                    - fitted_boundary_before.distance_au) < 1.0e-7,
        "fitted apogee remains continuous across blended fit segments",
        &failures);
    NativeCalcContext fitted_icrf_iau2006 = mean_context;
    fitted_icrf_iau2006.apparent_options.output_frame_id =
        TAIYIN_APPARENT_FRAME_ICRF;
    fitted_icrf_iau2006.model_context.precession_model_id =
        dispatch::PRECESSION_IAU2006;
    NativeCalcContext fitted_icrf_vondrak = fitted_icrf_iau2006;
    fitted_icrf_vondrak.model_context.precession_model_id =
        dispatch::PRECESSION_VONDRAK2011;
    LunarApsisPosition fitted_icrf_iau2006_result;
    LunarApsisPosition fitted_icrf_vondrak_result;
    const double fitted_reference_test_jd = -1000000.0;
    expect_status(
        calc_lunar_fitted_apogee_tt(
            &fitted_icrf_iau2006, fitted_reference_test_jd, 0u,
            &fitted_icrf_iau2006_result, nullptr),
        TAIYIN_STATUS_OK,
        "evaluate fitted apogee in ICRF with IAU 2006 selected",
        &failures);
    expect_status(
        calc_lunar_fitted_apogee_tt(
            &fitted_icrf_vondrak, fitted_reference_test_jd, 0u,
            &fitted_icrf_vondrak_result, nullptr),
        TAIYIN_STATUS_OK,
        "evaluate fitted apogee in ICRF with Vondrak selected",
        &failures);
    expect_near(
        fitted_icrf_iau2006_result.longitude_rad,
        fitted_icrf_vondrak_result.longitude_rad,
        1.0e-14,
        "fitted ICRF longitude is independent of the requested output precession model",
        &failures);
    expect_near(
        fitted_icrf_iau2006_result.latitude_rad,
        fitted_icrf_vondrak_result.latitude_rad,
        1.0e-14,
        "fitted ICRF latitude is independent of the requested output precession model",
        &failures);
    expect_near(
        fitted_icrf_iau2006_result.longitude_rate_rad_per_day,
        fitted_icrf_vondrak_result.longitude_rate_rad_per_day,
        1.0e-14,
        "fitted ICRF longitude rate is independent of output precession",
        &failures);
    expect_near(
        fitted_icrf_iau2006_result.latitude_rate_rad_per_day,
        fitted_icrf_vondrak_result.latitude_rate_rad_per_day,
        1.0e-14,
        "fitted ICRF latitude rate is independent of output precession",
        &failures);
    expect_near(
        fitted_icrf_iau2006_result.distance_au,
        fitted_icrf_vondrak_result.distance_au,
        1.0e-15,
        "fitted ICRF distance is independent of output precession",
        &failures);
    CartesianState routed_fitted_state;
    expect_status(
        calc_state_tt(
            &mean_context,
            TAIYIN_ASTROLOGY_TARGET_FITTED_LILITH,
            jd_tt,
            0u,
            &routed_fitted_state,
            nullptr),
        TAIYIN_STATUS_OK,
        "evaluate fitted Lilith state through the standard target route",
        &failures);
    expect_true(
        std::isfinite(routed_fitted_state.position_au.x)
            && std::isfinite(routed_fitted_state.velocity_au_per_day.x)
            && std::isfinite(routed_fitted_state.acceleration_au_per_day2.x),
        "fitted Lilith target supplies position, velocity, and fallback acceleration",
        &failures);

    const int mixed_target_ids[] = {
        TAIYIN_BODY_MOON,
        TAIYIN_ASTROLOGY_TARGET_TRUE_DESCENDING_NODE,
        TAIYIN_ASTROLOGY_TARGET_MEAN_NODE,
        TAIYIN_ASTROLOGY_TARGET_MEAN_DESCENDING_NODE,
        TAIYIN_ASTROLOGY_TARGET_MEAN_LILITH,
        TAIYIN_ASTROLOGY_TARGET_OSCULATING_LILITH,
        TAIYIN_ASTROLOGY_TARGET_FITTED_LILITH,
    };
    double mixed_positions[6 * (sizeof(mixed_target_ids) / sizeof(mixed_target_ids[0]))];
    EphemerisEvalDiagnostic mixed_diagnostics[
        sizeof(mixed_target_ids) / sizeof(mixed_target_ids[0])];
    expect_status(
        calc_positions_tt(
            &mean_context,
            mixed_target_ids,
            sizeof(mixed_target_ids) / sizeof(mixed_target_ids[0]),
            jd_tt,
            TAIYIN_NATIVE_POSITION_SPEED | TAIYIN_NATIVE_POSITION_RADIANS,
            mixed_positions,
            mixed_diagnostics),
        TAIYIN_STATUS_OK,
        "evaluate physical and astrology targets in one position batch",
        &failures);
    expect_true(
        std::isfinite(mixed_positions[0]) && std::isfinite(mixed_positions[1])
            && std::isfinite(mixed_positions[2])
            && mixed_diagnostics[0].target_id == TAIYIN_BODY_MOON,
        "mixed position batch preserves physical Moon route",
        &failures);
    for (size_t i = 1; i < sizeof(mixed_target_ids) / sizeof(mixed_target_ids[0]); ++i) {
        const double* position = mixed_positions + 6 * i;
        const bool direction_only = i < 5;
        expect_true(
            std::isfinite(position[0]) && std::isfinite(position[1])
                && std::isfinite(position[3]) && std::isfinite(position[4])
                && (direction_only
                    ? std::isnan(position[2]) && std::isnan(position[5])
                    : std::isfinite(position[2]) && std::isfinite(position[5]))
                && mixed_diagnostics[i].target_id == mixed_target_ids[i],
            "mixed position batch resolves registered astrology target",
            &failures);
    }
    expect_true(
        std::isnan(mixed_positions[6 * 1 + 2])
            && std::isnan(mixed_positions[6 * 2 + 2])
            && std::isnan(mixed_positions[6 * 3 + 2])
            && std::isnan(mixed_positions[6 * 4 + 2])
            && mixed_positions[6 * 5 + 2] > 0.0
            && mixed_positions[6 * 6 + 2] > 0.0,
        "direction-only astrology targets preserve undefined distance while apsis models provide distance",
        &failures);

    if (failures == 0) {
        std::cout << "lunar point astrology tests passed\n";
    }
    return failures == 0 ? 0 : 1;
}
