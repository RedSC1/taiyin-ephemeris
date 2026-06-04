#include "taiyin/angle.h"
#include "taiyin/coordinates.h"
#include "taiyin/earth_rotation.h"
#include "taiyin/geometry.h"

#include <cmath>
#include <iostream>

namespace {


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

}  // namespace

int main() {
    int failures = 0;

    {
        // Baked scalar oracle for the flat xyz -> ecliptic output surface.
        taiyin::EclipticPositionVelocity actual;
        expect_true(
            taiyin::cartesian_position_velocity_to_ecliptic(
                { 0.72, -1.31, 0.44 },
                { 0.0021, 0.0084, -0.0017 },
                &actual),
            "ecliptic oracle conversion succeeds",
            &failures);
        expect_near(actual.longitude_rad, 5.214939108085304, 1e-15, "ecliptic longitude oracle", &failures);
        expect_near(actual.latitude_rad, 0.28626431926069257, 1e-15, "ecliptic latitude oracle", &failures);
        expect_near(actual.radius_au, 1.5582361823549087, 1e-15, "ecliptic radius oracle", &failures);
        expect_near(actual.longitude_rate_rad_per_day, 0.003937793689863503, 1e-15, "ecliptic longitude rate oracle", &failures);
        expect_near(actual.latitude_rate_rad_per_day, 0.0001040973538305114, 1e-15, "ecliptic latitude rate oracle", &failures);
        expect_near(actual.radius_rate_au_per_day, -0.006571532682885493, 1e-15, "ecliptic radius rate oracle", &failures);
    }

    {
        struct NutationOracle {
            double jd_tt;
            double dpsi_rad;
            double deps_rad;
            double mean_obliquity_rad;
            double true_obliquity_rad;
        };
        const NutationOracle oracles[] = {
            { 2451545.0, -0.000067542612539922361, -0.000027970923310985653, 0.40909260060058289, 0.40906462967727192 },
            { 2460000.0, -0.000044811878657808338, 0.000037607177053908570, 0.40904003706375935, 0.40907764424081328 },
            { 2415020.5, 0.000084518702696893369, -0.000011103153586824906, 0.40931965795344111, 0.40930855479985429 },
        };
        for (int i = 0; i < static_cast<int>(sizeof(oracles) / sizeof(oracles[0])); ++i) {
            taiyin::NutationAngles actual;
            expect_true(taiyin::iau2000b_nutation(oracles[i].jd_tt, &actual), "IAU2000B oracle succeeds", &failures);
            expect_near(actual.dpsi_rad, oracles[i].dpsi_rad, 1e-14, "IAU2000B dpsi oracle", &failures);
            expect_near(actual.deps_rad, oracles[i].deps_rad, 1e-14, "IAU2000B deps oracle", &failures);
            expect_near(actual.mean_obliquity_rad, oracles[i].mean_obliquity_rad, 1e-14, "IAU2000B mean obliquity oracle", &failures);
            expect_near(actual.true_obliquity_rad, oracles[i].true_obliquity_rad, 1e-14, "IAU2000B true obliquity oracle", &failures);
        }
    }

    {
        // ERFA era00/gmst06 baked fixtures from the legacy strict observer route.
        struct EarthRotationOracle {
            double jd_ut1;
            double jd_tt;
            double era_rad;
            double gmst_rad;
        };
        const EarthRotationOracle oracles[] = {
            { 2451545.0, 2451545.0, 4.894961212823756, 4.8949612831508285 },
            { 2460000.0, 2460000.0008, 5.826127456378991, 5.831303984358957 },
            { 2440000.0, 2440000.0007, 1.0745425392021062, 1.0674755105895786 },
            { 2460409.26203588, 2460409.262837778, 1.9463758662917954, 1.9518029776572094 },
        };
        for (int i = 0; i < static_cast<int>(sizeof(oracles) / sizeof(oracles[0])); ++i) {
            expect_near(taiyin::earth_rotation_angle_rad(oracles[i].jd_ut1), oracles[i].era_rad, 1e-9, "ERFA ERA oracle", &failures);
            expect_near(taiyin::gmst_rad(oracles[i].jd_ut1, oracles[i].jd_tt), oracles[i].gmst_rad, 1e-9, "ERFA GMST oracle", &failures);
            expect_near(
                taiyin::gmst_minus_era_rad(oracles[i].jd_tt),
                taiyin::normalize_signed_radians(oracles[i].gmst_rad - oracles[i].era_rad),
                1e-9,
                "ERFA GMST-ERA oracle",
                &failures);
        }
    }

    {
        double tau_dot = 0.0;
        expect_true(
            taiyin::light_time_tau_dot_no_shapiro(0.01, -0.02, 0.005, &tau_dot),
            "light-time tau dot oracle succeeds",
            &failures);
        expect_near(tau_dot, 0.00014999250037498122, 1e-18, "light-time tau dot oracle", &failures);

        expect_true(
            taiyin::light_time_tau_dot_with_shapiro(
                0.01,
                -0.02,
                0.003,
                -0.004,
                0.005,
                -2e-10,
                5e-10,
                &tau_dot),
            "Shapiro tau dot oracle succeeds",
            &failures);
        expect_near(tau_dot, 0.00014999251557335134, 1e-18, "Shapiro tau dot oracle", &failures);
    }

    return failures == 0 ? 0 : 1;
}
