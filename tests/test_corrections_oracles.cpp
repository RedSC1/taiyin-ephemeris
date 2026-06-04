#include "taiyin/corrections.h"
#include "taiyin/vector3.h"

#include <cmath>
#include <iostream>

namespace {

const double ORACLE_SOURCE_DISTANCE_AU = 1.0e9;
const double JUPITER_SOLAR_MASS_RATIO = 9.547919384243266e-4;

struct SofaDeflectionOracle {
    double observer_heliocentric_au[3];
    double source_heliocentric_au[3];
    double expected_direction[3];
};

struct SofaMultiDeflectionOracle {
    double observer_heliocentric_au[3];
    double source_heliocentric_au[3];
    double jupiter_heliocentric_au[3];
    double expected_direction[3];
    double ldn_expected_direction[3];
};

struct SofaAberrationOracle {
    double observer_heliocentric_au[3];
    double observer_velocity_au_per_day[3];
    double natural_direction[3];
    double expected_direction[3];
};

struct CorrectionAccelerationOracle {
    double expected_position_au[3];
    double expected_velocity_au_per_day[3];
    double expected_acceleration_au_per_day2[3];
};

const SofaDeflectionOracle SOFA_DEFLECTION_ORACLES[] = {
    { { 8.95306712607000010e-01, -4.30362177776999977e-01, -1.86583142291999987e-01 }, { 6.07574178804420710e+08, -7.21272083263068944e+07, 7.90981215910992384e+08 }, { 6.07574186541302907e-01, -7.21272133219377776e-02, 7.90981208972176497e-01 } },
    { { -2.03805932172999998e-01, 8.74512893612000020e-01, 3.79100443914999974e-01 }, { -1.05927396160609543e+08, -7.21050740872573733e+08, -3.29947290277735651e+08 }, { -1.32409360303183371e-01, -9.01313410344144472e-01, -4.12434113087060239e-01 } },
    { { 1.29502771981999998e-01, 9.23741812388000016e-01, 4.00514096127999986e-01 }, { 3.01772436998657286e+08, -9.47300714100171924e+08, 6.71978237891307831e+08 }, { 2.51477039620698006e-01, -7.89417242783020834e-01, 5.59981888403958239e-01 } },
};

const SofaMultiDeflectionOracle SOFA_MULTI_DEFLECTION_ORACLES[] = {
    { { 1.29502771981999998e-01, 9.23741812388000016e-01, 4.00514096127999986e-01 }, { 9.55551853245469332e+08, 2.78176089322995961e+08, 9.76663705057482421e+07 }, { 4.45123372180199972e+00, 2.18239817231199984e+00, 8.42117339102000040e-01 }, { 9.55551849554622135e-01, 2.78176098879497402e-01, 9.76663750987251666e-02 }, { 9.55551849554606925e-01, 2.78176098879543032e-01, 9.76663750987455115e-02 } },
    { { -2.03805932172999998e-01, 8.74512893612000020e-01, 3.79100443914999974e-01 }, { -6.58424031643098116e+08, 4.14439160137273073e+08, 1.86327609961809933e+08 }, { -3.90133721872099981e+00, 3.20144290137199983e+00, 1.42577910223100002e+00 }, { -8.23030034608330130e-01, 5.18048955520781340e-01, 2.32909514224813263e-01 }, { -8.23030034608310923e-01, 5.18048955520806875e-01, 2.32909514224824088e-01 } },
};

const SofaAberrationOracle SOFA_ABERRATION_ORACLES[] = {
    { { 8.95306712607000010e-01, -4.30362177776999977e-01, -1.86583142291999987e-01 }, { 7.71448410899999975e-03, 1.39330513049999993e-02, 6.04025884999999981e-03 }, { 6.07574149614460546e-01, -7.21270799542313573e-02, 7.90981249498078465e-01 }, { 6.07589015913398867e-01, -7.20430869961028730e-02, 7.90977484734843306e-01 } },
    { { -2.03805932172999998e-01, 8.74512893612000020e-01, 3.79100443914999974e-01 }, { -1.69228442250000007e-02, -3.41291840000000016e-03, -1.47925338100000003e-03 }, { -1.91110057334686556e-01, 8.12312471826485805e-01, 5.51022135762872622e-01 }, { -1.91208184670704412e-01, 8.12294414034453016e-01, 5.51014713998962513e-01 } },
    { { 1.29502771981999998e-01, 9.23741812388000016e-01, 4.00514096127999986e-01 }, { -1.70984122070000008e-02, 2.10492987800000014e-03, 9.12475116000000018e-04 }, { 8.70282500753548960e-01, 4.07140864181650020e-01, -2.77208740113958685e-01 }, { 8.70255503979075473e-01, 4.07186592707238337e-01, -2.77226327237502845e-01 } },
};

// Baked with pyerfa 2.0.1.5 from ERFA eraAb/eraLd at t={-2h,-h,0,h,2h};
// velocity and acceleration are five-point derivatives of the ERFA-corrected
// position.  observer_velocity uses eraAb with an effectively infinite
// heliocentric distance to disable the solar-potential term.
const CorrectionAccelerationOracle OBSERVER_VELOCITY_ABERRATION_ACCELERATION_ORACLE = {
    { 7.00001409270408104e-01, 1.19999898002477612e+00, 3.00000791597187788e-01 },
    { 9.99975168873105339e-04, -1.99998984558765348e-03, 5.00004510156637494e-04 },
    { -2.99936001961024560e-05, 1.00290146557805802e-05, 1.99974296381337028e-05 },
};

const CorrectionAccelerationOracle ANNUAL_ABERRATION_ACCELERATION_ORACLE = {
    { 6.99946202795786343e-01, 1.20003629363491404e+00, 2.99980344607338356e-01 },
    { 9.99892618610183749e-04, -1.99982318760394406e-03, 4.99997307122172275e-04 },
    { -3.00010016829332892e-05, 1.00419672577345392e-05, 1.99891029654490141e-05 },
};

const CorrectionAccelerationOracle DEFLECTION_ACCELERATION_ORACLE = {
    { 2.00000007210446640e-01, 9.99999998572188908e-01, 9.99999998572188992e-02 },
    { -8.39374865909311016e-11, 1.00000001871750977e-03, -1.06396373193244163e-11 },
    { 1.99925724124009677e-05, -3.00093283556179779e-05, 9.99940870845724211e-06 },
};

bool near(double actual, double expected, double tolerance) {
    return std::fabs(actual - expected) <= tolerance;
}

void expect_true(bool value, const char* message, int* failures) {
    if (!value) {
        std::cerr << "FAIL: " << message << "\n";
        ++(*failures);
    }
}

void expect_near(double actual, double expected, double tolerance, const char* message, int* failures) {
    if (!near(actual, expected, tolerance)) {
        std::cerr << "FAIL: " << message
                  << ": actual=" << actual
                  << " expected=" << expected
                  << " diff=" << std::fabs(actual - expected)
                  << " tolerance=" << tolerance << "\n";
        ++(*failures);
    }
}

taiyin::Vector3 vector_from_array(const double values[3]) {
    return taiyin::Vector3{ values[0], values[1], values[2] };
}

taiyin::Vector3 zero_vector() {
    return taiyin::Vector3{ 0.0, 0.0, 0.0 };
}

void expect_vector_near(
    const taiyin::Vector3& actual,
    const double expected[3],
    double tolerance,
    const char* message,
    int* failures
) {
    expect_near(actual.x, expected[0], tolerance, message, failures);
    expect_near(actual.y, expected[1], tolerance, message, failures);
    expect_near(actual.z, expected[2], tolerance, message, failures);
}

}  // namespace

int main() {
    int failures = 0;

    {
        taiyin::Vector3 unit;
        taiyin::Vector3 unit_dot;
        double norm = 0.0;
        double norm_dot = 0.0;
        expect_true(
            taiyin::unit_vector_with_derivative({ 3.0, 4.0, 0.0 }, { 0.3, 0.4, 0.0 }, &unit, &unit_dot, &norm, &norm_dot),
            "unit vector oracle succeeds",
            &failures);
        expect_near(unit.x, 0.6, 1e-15, "unit vector x oracle", &failures);
        expect_near(unit.y, 0.8, 1e-15, "unit vector y oracle", &failures);
        expect_near(unit.z, 0.0, 1e-15, "unit vector z oracle", &failures);
        expect_near(unit_dot.x, 0.0, 1e-15, "unit derivative x oracle", &failures);
        expect_near(unit_dot.y, 0.0, 1e-15, "unit derivative y oracle", &failures);
        expect_near(unit_dot.z, 0.0, 1e-15, "unit derivative z oracle", &failures);
        expect_near(norm, 5.0, 1e-15, "unit norm oracle", &failures);
        expect_near(norm_dot, 0.5, 1e-15, "unit norm dot oracle", &failures);
    }

    {
        double delay = 0.0;
        double dr = 0.0;
        double dd = 0.0;
        expect_true(
            taiyin::solar_shapiro_delay_terms(
                { 1.0, 0.0, 0.0 },
                { 1.5, 0.2, 0.0 },
                { 0.5, 0.2, 0.0 },
                taiyin::TAIYIN_SOLAR_SCHWARZSCHILD_RADIUS_AU * taiyin::TAIYIN_LIGHT_TIME_DAYS_PER_AU,
                &delay,
                &dr,
                &dd),
            "Shapiro terms oracle succeeds",
            &failures);
        expect_near(delay, 4.96291942530341560e-11, 1e-24, "Shapiro delay oracle", &failures);
        expect_near(dr, -2.03763347805721340e-11, 1e-24, "Shapiro dr oracle", &failures);
        expect_near(dd, 9.50970422970877186e-11, 1e-24, "Shapiro dd oracle", &failures);
    }

    {
        const taiyin::Vector3 source_position = { 0.7, 1.2, 0.3 };
        const taiyin::Vector3 source_velocity = { 0.001, -0.002, 0.0005 };
        const taiyin::Vector3 observer_velocity = { 0.00011, -0.00023, 0.00007 };
        taiyin::Vector3 actual_position;
        taiyin::Vector3 actual_velocity;
        expect_true(
            taiyin::apply_observer_velocity_aberration(
                source_position,
                source_velocity,
                observer_velocity,
                zero_vector(),
                taiyin::TAIYIN_LIGHT_TIME_DAYS_PER_AU,
                &actual_position,
                &actual_velocity),
            "observer velocity aberration ERFA oracle succeeds",
            &failures);
        const double expected_position[3] = {
            7.00001409270408104e-01,
            1.19999898002477612e+00,
            3.00000791597187788e-01,
        };
        expect_vector_near(actual_position, expected_position, 1e-15, "observer velocity aberration ERFA oracle", &failures);
    }

    for (int i = 0; i < static_cast<int>(sizeof(SOFA_ABERRATION_ORACLES) / sizeof(SOFA_ABERRATION_ORACLES[0])); ++i) {
        const SofaAberrationOracle& oracle = SOFA_ABERRATION_ORACLES[i];
        const taiyin::Vector3 source_position = taiyin::vector3_scale(
            vector_from_array(oracle.natural_direction),
            ORACLE_SOURCE_DISTANCE_AU);
        taiyin::Vector3 actual_position;
        taiyin::Vector3 actual_velocity;
        expect_true(
            taiyin::apply_annual_aberration(
                source_position,
                zero_vector(),
                vector_from_array(oracle.observer_heliocentric_au),
                zero_vector(),
                vector_from_array(oracle.observer_velocity_au_per_day),
                zero_vector(),
                taiyin::TAIYIN_LIGHT_TIME_DAYS_PER_AU,
                taiyin::TAIYIN_SOLAR_SCHWARZSCHILD_RADIUS_AU,
                &actual_position,
                &actual_velocity),
            "annual aberration SOFA oracle succeeds",
            &failures);
        expect_vector_near(
            taiyin::vector3_normalize(actual_position),
            oracle.expected_direction,
            1e-15,
            "annual aberration SOFA iauAb oracle",
            &failures);
    }

    {
        taiyin::Vector3 actual_position;
        taiyin::Vector3 actual_velocity;
        taiyin::Vector3 actual_acceleration;
        expect_true(
            taiyin::apply_observer_velocity_aberration_acceleration(
                { 0.7, 1.2, 0.3 },
                { 0.001, -0.002, 0.0005 },
                { -3e-5, 1e-5, 2e-5 },
                { 0.00011, -0.00023, 0.00007 },
                { -2e-6, 3e-6, 1e-6 },
                taiyin::TAIYIN_LIGHT_TIME_DAYS_PER_AU,
                &actual_position,
                &actual_velocity,
                &actual_acceleration),
            "observer velocity aberration acceleration ERFA oracle succeeds",
            &failures);
        expect_vector_near(
            actual_position,
            OBSERVER_VELOCITY_ABERRATION_ACCELERATION_ORACLE.expected_position_au,
            1e-15,
            "observer velocity aberration acceleration ERFA position",
            &failures);
        expect_vector_near(
            actual_velocity,
            OBSERVER_VELOCITY_ABERRATION_ACCELERATION_ORACLE.expected_velocity_au_per_day,
            1e-11,
            "observer velocity aberration acceleration ERFA velocity",
            &failures);
        expect_vector_near(
            actual_acceleration,
            OBSERVER_VELOCITY_ABERRATION_ACCELERATION_ORACLE.expected_acceleration_au_per_day2,
            5e-8,
            "observer velocity aberration acceleration ERFA acceleration",
            &failures);
    }

    {
        taiyin::Vector3 actual_position;
        taiyin::Vector3 actual_velocity;
        taiyin::Vector3 actual_acceleration;
        expect_true(
            taiyin::apply_annual_aberration_acceleration(
                { 0.7, 1.2, 0.3 },
                { 0.001, -0.002, 0.0005 },
                { -3e-5, 1e-5, 2e-5 },
                { 1.0, 0.2, -0.1 },
                { -0.001, 0.017, 0.0003 },
                { -2e-5, -1e-6, 5e-7 },
                { 0.0002, 0.016, 0.0005 },
                { -1e-5, 3e-6, 2e-6 },
                taiyin::TAIYIN_LIGHT_TIME_DAYS_PER_AU,
                taiyin::TAIYIN_SOLAR_SCHWARZSCHILD_RADIUS_AU,
                &actual_position,
                &actual_velocity,
                &actual_acceleration),
            "annual aberration acceleration ERFA oracle succeeds",
            &failures);
        expect_vector_near(
            actual_position,
            ANNUAL_ABERRATION_ACCELERATION_ORACLE.expected_position_au,
            1e-15,
            "annual aberration acceleration ERFA position",
            &failures);
        expect_vector_near(
            actual_velocity,
            ANNUAL_ABERRATION_ACCELERATION_ORACLE.expected_velocity_au_per_day,
            1e-10,
            "annual aberration acceleration ERFA velocity",
            &failures);
        expect_vector_near(
            actual_acceleration,
            ANNUAL_ABERRATION_ACCELERATION_ORACLE.expected_acceleration_au_per_day2,
            5e-8,
            "annual aberration acceleration ERFA acceleration",
            &failures);
    }

    for (int i = 0; i < static_cast<int>(sizeof(SOFA_DEFLECTION_ORACLES) / sizeof(SOFA_DEFLECTION_ORACLES[0])); ++i) {
        const SofaDeflectionOracle& oracle = SOFA_DEFLECTION_ORACLES[i];
        const taiyin::Vector3 observer = vector_from_array(oracle.observer_heliocentric_au);
        const taiyin::Vector3 source_geocentric = taiyin::vector3_subtract(
            vector_from_array(oracle.source_heliocentric_au),
            observer);
        taiyin::Vector3 actual_position;
        taiyin::Vector3 actual_velocity;
        expect_true(
            taiyin::apply_gravitational_deflection_from_body(
                source_geocentric,
                zero_vector(),
                observer,
                zero_vector(),
                zero_vector(),
                zero_vector(),
                source_geocentric,
                zero_vector(),
                taiyin::TAIYIN_SOLAR_SCHWARZSCHILD_RADIUS_AU,
                taiyin::TAIYIN_SOLAR_DEFLECTION_LIMIT,
                &actual_position,
                &actual_velocity),
            "solar deflection SOFA oracle succeeds",
            &failures);
        expect_vector_near(
            taiyin::vector3_normalize(actual_position),
            oracle.expected_direction,
            1e-15,
            "solar deflection SOFA iauLd oracle",
            &failures);
    }

    {
        taiyin::Vector3 actual_position;
        taiyin::Vector3 actual_velocity;
        taiyin::Vector3 actual_acceleration;
        expect_true(
            taiyin::apply_gravitational_deflection_from_body_acceleration(
                { 0.2, 1.0, 0.1 },
                { 0.0, 0.001, 0.0 },
                { 2e-5, -3e-5, 1e-5 },
                { 1.0, 0.0, 0.0 },
                { 0.0, 0.017, 0.0 },
                { -1e-5, 0.0, 2e-6 },
                { 0.0, 0.0, 0.0 },
                { 0.0, 0.0, 0.0 },
                { 0.0, 0.0, 0.0 },
                { 0.2, 1.0, 0.1 },
                { 0.0, 0.001, 0.0 },
                { 2e-5, -3e-5, 1e-5 },
                taiyin::TAIYIN_SOLAR_SCHWARZSCHILD_RADIUS_AU,
                taiyin::TAIYIN_SOLAR_DEFLECTION_LIMIT,
                &actual_position,
                &actual_velocity,
                &actual_acceleration),
            "deflection acceleration ERFA oracle succeeds",
            &failures);
        expect_vector_near(
            actual_position,
            DEFLECTION_ACCELERATION_ORACLE.expected_position_au,
            1e-8,
            "deflection acceleration ERFA position",
            &failures);
        expect_vector_near(
            actual_velocity,
            DEFLECTION_ACCELERATION_ORACLE.expected_velocity_au_per_day,
            1e-9,
            "deflection acceleration ERFA velocity",
            &failures);
        expect_vector_near(
            actual_acceleration,
            DEFLECTION_ACCELERATION_ORACLE.expected_acceleration_au_per_day2,
            5e-8,
            "deflection acceleration ERFA acceleration",
            &failures);
    }

    for (int i = 0; i < static_cast<int>(sizeof(SOFA_MULTI_DEFLECTION_ORACLES) / sizeof(SOFA_MULTI_DEFLECTION_ORACLES[0])); ++i) {
        const SofaMultiDeflectionOracle& oracle = SOFA_MULTI_DEFLECTION_ORACLES[i];
        const taiyin::Vector3 observer = vector_from_array(oracle.observer_heliocentric_au);
        const taiyin::Vector3 source_geocentric = taiyin::vector3_subtract(
            vector_from_array(oracle.source_heliocentric_au),
            observer);
        taiyin::Vector3 apparent_position = source_geocentric;
        taiyin::Vector3 apparent_velocity = zero_vector();
        expect_true(
            taiyin::apply_gravitational_deflection_from_body(
                apparent_position,
                apparent_velocity,
                observer,
                zero_vector(),
                zero_vector(),
                zero_vector(),
                source_geocentric,
                zero_vector(),
                taiyin::TAIYIN_SOLAR_SCHWARZSCHILD_RADIUS_AU,
                taiyin::TAIYIN_SOLAR_DEFLECTION_LIMIT,
                &apparent_position,
                &apparent_velocity),
            "multi deflection Sun SOFA oracle succeeds",
            &failures);
        expect_true(
            taiyin::apply_gravitational_deflection_from_body(
                apparent_position,
                apparent_velocity,
                observer,
                zero_vector(),
                vector_from_array(oracle.jupiter_heliocentric_au),
                zero_vector(),
                source_geocentric,
                zero_vector(),
                taiyin::TAIYIN_SOLAR_SCHWARZSCHILD_RADIUS_AU * JUPITER_SOLAR_MASS_RATIO,
                taiyin::TAIYIN_SOLAR_DEFLECTION_LIMIT,
                &apparent_position,
                &apparent_velocity),
            "multi deflection Jupiter SOFA oracle succeeds",
            &failures);
        const taiyin::Vector3 actual_direction = taiyin::vector3_normalize(apparent_position);
        expect_vector_near(actual_direction, oracle.expected_direction, 1e-15, "multi deflection chained SOFA oracle", &failures);
        expect_vector_near(actual_direction, oracle.ldn_expected_direction, 1e-12, "multi deflection SOFA iauLdn oracle", &failures);
    }

    return failures == 0 ? 0 : 1;
}
