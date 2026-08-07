#include "taiyin/angle.h"
#include "taiyin/chebyshev.h"
#include "taiyin/state.h"
#include "taiyin/vector3.h"

#include <cmath>
#include <cstring>
#include <iostream>

namespace {


bool near(double actual, double expected, double tolerance) {
    return std::fabs(actual - expected) <= tolerance;
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

void expect_vector_near(
    const taiyin::Vector3& actual,
    const taiyin::Vector3& expected,
    double tolerance,
    const char* message,
    int* failures
) {
    expect_near(actual.x, expected.x, tolerance, message, failures);
    expect_near(actual.y, expected.y, tolerance, message, failures);
    expect_near(actual.z, expected.z, tolerance, message, failures);
}

void expect_state_near(
    const taiyin::CartesianState& actual,
    const taiyin::CartesianState& expected,
    double tolerance,
    const char* message,
    int* failures
) {
    expect_vector_near(actual.position_au, expected.position_au, tolerance, message, failures);
    expect_vector_near(actual.velocity_au_per_day, expected.velocity_au_per_day, tolerance, message, failures);
    expect_vector_near(actual.acceleration_au_per_day2, expected.acceleration_au_per_day2, tolerance, message, failures);
}

}  // namespace

int main() {
    int failures = 0;

    {
        const taiyin::Vector3 a = { 1.0, 2.0, 3.0 };
        const taiyin::Vector3 b = { -4.0, 5.0, -6.0 };
        expect_vector_near(taiyin::vector3_add(a, b), { -3.0, 7.0, -3.0 }, 0.0, "vector add", &failures);
        expect_vector_near(taiyin::vector3_subtract(a, b), { 5.0, -3.0, 9.0 }, 0.0, "vector subtract", &failures);
        expect_vector_near(taiyin::vector3_scale(a, 2.5), { 2.5, 5.0, 7.5 }, 0.0, "vector scale", &failures);
        expect_vector_near(taiyin::vector3_negate(a), { -1.0, -2.0, -3.0 }, 0.0, "vector negate", &failures);
        expect_near(taiyin::vector3_dot(a, b), -12.0, 0.0, "vector dot", &failures);
        expect_vector_near(taiyin::vector3_cross(a, b), { -27.0, -6.0, 13.0 }, 0.0, "vector cross", &failures);
        expect_near(taiyin::vector3_norm({ 3.0, 4.0, 12.0 }), 13.0, 0.0, "vector norm", &failures);
        expect_vector_near(taiyin::vector3_normalize({ 3.0, 4.0, 0.0 }), { 0.6, 0.8, 0.0 }, 1e-15, "vector normalize", &failures);
        expect_vector_near(taiyin::vector3_normalize({ 0.0, 0.0, 0.0 }), { 0.0, 0.0, 0.0 }, 0.0, "zero vector normalize", &failures);
    }

    {
        const taiyin::Vector3 spherical = taiyin::spherical_to_cartesian(taiyin::TAIYIN_PI / 3.0, taiyin::TAIYIN_PI / 6.0, 2.0);
        expect_vector_near(
            spherical,
            { 0.86602540378443882, 1.5, 1.0 },
            1e-15,
            "spherical to cartesian",
            &failures);

        double lon = 0.0;
        double lat = 0.0;
        double radius = 0.0;
        taiyin::cartesian_to_spherical(spherical, &lon, &lat, &radius);
        expect_near(lon, taiyin::TAIYIN_PI / 3.0, 1e-15, "cartesian to spherical lon", &failures);
        expect_near(lat, taiyin::TAIYIN_PI / 6.0, 1e-15, "cartesian to spherical lat", &failures);
        expect_near(radius, 2.0, 1e-15, "cartesian to spherical radius", &failures);

        taiyin::cartesian_to_spherical({ 0.0, -1.0, 0.0 }, &lon, &lat, &radius);
        expect_near(lon, 3.0 * taiyin::TAIYIN_PI / 2.0, 1e-15, "cartesian to spherical positive lon", &failures);
        expect_near(lat, 0.0, 0.0, "cartesian to spherical equator lat", &failures);
        expect_near(radius, 1.0, 0.0, "cartesian to spherical unit radius", &failures);

        taiyin::cartesian_to_spherical({ 0.0, 0.0, 0.0 }, &lon, &lat, &radius);
        expect_near(lon, 0.0, 0.0, "zero spherical lon", &failures);
        expect_near(lat, 0.0, 0.0, "zero spherical lat", &failures);
        expect_near(radius, 0.0, 0.0, "zero spherical radius", &failures);
    }

    {
        expect_vector_near(taiyin::rotate_x({ 0.0, 1.0, 0.0 }, taiyin::TAIYIN_PI / 2.0), { 0.0, 0.0, 1.0 }, 1e-15, "rotate x", &failures);
        expect_vector_near(taiyin::rotate_y({ 0.0, 0.0, 1.0 }, taiyin::TAIYIN_PI / 2.0), { 1.0, 0.0, 0.0 }, 1e-15, "rotate y", &failures);
        expect_vector_near(taiyin::rotate_z({ 1.0, 0.0, 0.0 }, taiyin::TAIYIN_PI / 2.0), { 0.0, 1.0, 0.0 }, 1e-15, "rotate z", &failures);
        expect_vector_near(taiyin::rotate_z({ 1.0, 2.0, 3.0 }, taiyin::TAIYIN_TWO_PI), { 1.0, 2.0, 3.0 }, 1e-15, "rotate full turn", &failures);
    }

    {
        const taiyin::CartesianState a = {
            { 1.0, 2.0, 3.0 },
            { 0.1, 0.2, 0.3 },
            { 0.01, 0.02, 0.03 },
        };
        const taiyin::CartesianState b = {
            { -4.0, 5.0, -6.0 },
            { -0.4, 0.5, -0.6 },
            { -0.04, 0.05, -0.06 },
        };
        const taiyin::CartesianState add_expected = {
            { -3.0, 7.0, -3.0 },
            { -0.3, 0.7, -0.3 },
            { -0.03, 0.07, -0.03 },
        };
        const taiyin::CartesianState subtract_expected = {
            { 5.0, -3.0, 9.0 },
            { 0.5, -0.3, 0.9 },
            { 0.05, -0.03, 0.09 },
        };
        const taiyin::CartesianState scale_expected = {
            { 2.0, 4.0, 6.0 },
            { 0.2, 0.4, 0.6 },
            { 0.02, 0.04, 0.06 },
        };
        expect_state_near(taiyin::cartesian_state_add(a, b), add_expected, 1e-15, "cartesian state add", &failures);
        expect_state_near(taiyin::cartesian_state_subtract(a, b), subtract_expected, 1e-15, "cartesian state subtract", &failures);
        expect_state_near(taiyin::cartesian_state_scale(a, 2.0), scale_expected, 0.0, "cartesian state scale", &failures);
        expect_state_near(taiyin::cartesian_state_negate(a), taiyin::cartesian_state_scale(a, -1.0), 0.0, "cartesian state negate", &failures);
    }

    {
        // chebyshev_eval_with_first_derivative must produce bit-identical
        // value and derivative to chebyshev_eval_with_derivative, since the
        // second-derivative recurrence does not feed back into the value or
        // first-derivative recurrences.
        auto expect_bit_identical = [&failures](const char* label, const double* coeffs, int count, double x) {
            const taiyin::ChebyshevValue full =
                taiyin::chebyshev_eval_with_derivative(coeffs, count, x);
            const taiyin::ChebyshevValueWithDerivative first =
                taiyin::chebyshev_eval_with_first_derivative(coeffs, count, x);
            if (std::memcmp(&full.value, &first.value, sizeof(double)) != 0) {
                std::cerr << "FAIL: chebyshev first-derivative value bit mismatch at "
                          << label << " x=" << x
                          << " full=" << full.value << " first=" << first.value << "\n";
                ++failures;
            }
            if (std::memcmp(&full.derivative, &first.derivative, sizeof(double)) != 0) {
                std::cerr << "FAIL: chebyshev first-derivative derivative bit mismatch at "
                          << label << " x=" << x
                          << " full=" << full.derivative << " first=" << first.derivative << "\n";
                ++failures;
            }
        };

        double coeffs[30];
        for (int i = 0; i < 30; ++i) {
            coeffs[i] = 0.25 * std::sin(0.7 * static_cast<double>(i) + 0.3)
                + 0.05 * static_cast<double>(i);
        }
        for (int i = 0; i < 200; ++i) {
            // i=0 and i=199 give the exact endpoints x=-1.0 and x=+1.0.
            const double x = (static_cast<double>(i) / 99.5) - 1.0;
            expect_bit_identical("interior/endpoints", coeffs, 30, x);
        }

        // Degenerate recurrences: count=1 executes no loop iterations and
        // count=2 runs exactly one, both paths on the same combination step.
        double single[1] = { -0.75 };
        expect_bit_identical("count=1", single, 1, -1.0);
        expect_bit_identical("count=1", single, 1, 0.0);
        expect_bit_identical("count=1", single, 1, 1.0);
        double pair[2] = { 0.25, -1.5 };
        expect_bit_identical("count=2", pair, 2, -1.0);
        expect_bit_identical("count=2", pair, 2, 0.375);
        expect_bit_identical("count=2", pair, 2, 1.0);

        // All-zero coefficients keep every recurrence variable at zero.
        double zeros[30] = { 0.0 };
        expect_bit_identical("zero coeffs", zeros, 30, -0.5);
        expect_bit_identical("zero coeffs", zeros, 30, 0.5);

        // Null/empty guard paths return the same zero struct from both
        // functions, so the full structs match.
        const taiyin::ChebyshevValue full_zero =
            taiyin::chebyshev_eval_with_derivative(nullptr, 0, 0.0);
        const taiyin::ChebyshevValueWithDerivative first_zero =
            taiyin::chebyshev_eval_with_first_derivative(nullptr, 0, 0.0);
        if (std::memcmp(&full_zero.value, &first_zero.value, sizeof(double)) != 0
            || std::memcmp(&full_zero.derivative, &first_zero.derivative, sizeof(double)) != 0) {
            std::cerr << "FAIL: chebyshev first-derivative null guard value/derivative mismatch\n";
            ++failures;
        }
    }

    return failures == 0 ? 0 : 1;
}
