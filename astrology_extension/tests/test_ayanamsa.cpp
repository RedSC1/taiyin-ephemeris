#include "../ayanamsa.h"
#include "taiyin/stars.h"
#include "taiyin/angle.h"
#include "taiyin/dispatch.h"
#include "taiyin/internal/ephemeris_block.h"

#include <cmath>
#include <iostream>

namespace {

bool near(double actual, double expected, double tolerance) {
    return std::fabs(actual - expected) <= tolerance;
}

void expect_near(double actual, double expected, double tolerance, const char* message, int* failures) {
    if (!near(actual, expected, tolerance)) {
        std::cerr.precision(15);
        std::cerr << "FAIL: " << message
                  << ": actual=" << actual
                  << " expected=" << expected
                  << " diff=" << std::fabs(actual - expected)
                  << " tolerance=" << tolerance << "\n";
        ++(*failures);
    }
}

// Custom identity precession model for testing dynamic ayanamsas with J2000 identity
bool test_identity_precession(double /*jd_tt*/, const void* /*data*/, taiyin::Matrix3x3* out, double* out_mean_obliquity_rad) {
    if (out) {
        *out = taiyin::matrix3x3_identity();
    }
    if (out_mean_obliquity_rad) {
        *out_mean_obliquity_rad = 23.4392911 * taiyin::TAIYIN_DEG_TO_RAD; // J2000 obliquity
    }
    return true;
}

}  // namespace

int main() {
    int failures = 0;

    const double J2000 = 2451545.0;
    const double J2024 = 2460310.5;

    // --- Test Ayanamsa J2000 (no precession difference) ---
    {
        double val = 0.0;
        
        taiyin::eval_ayanamsa(taiyin::AYANAMSA_FAGAN_BRADLEY, J2000, nullptr, &val);
        expect_near(val, 24.73658888888889, 1e-15, "Fagan-Bradley J2000", &failures);

        taiyin::eval_ayanamsa(taiyin::AYANAMSA_LAHIRI, J2000, nullptr, &val);
        expect_near(val, 23.862379882777777, 1e-15, "Lahiri J2000", &failures);

        taiyin::eval_ayanamsa(taiyin::AYANAMSA_RAMAN, J2000, nullptr, &val);
        expect_near(val, 22.412314799972222, 1e-15, "Raman J2000", &failures);

        taiyin::eval_ayanamsa(taiyin::AYANAMSA_KRISHNAMURTI, J2000, nullptr, &val);
        expect_near(val, 23.84649444334436, 1e-15, "Krishnamurti J2000", &failures);
    }

    // --- Test Ayanamsa J2024 (precession active) ---
    {
        double val = 0.0;

        taiyin::eval_ayanamsa(taiyin::AYANAMSA_LAHIRI, J2024, nullptr, &val);
        expect_near(val, 24.19828522125486, 1e-11, "Lahiri J2024", &failures);

        taiyin::eval_ayanamsa(taiyin::AYANAMSA_FAGAN_BRADLEY, J2024, nullptr, &val);
        expect_near(val, 25.072494227365972, 1e-11, "Fagan-Bradley J2024", &failures);

        taiyin::eval_ayanamsa(taiyin::AYANAMSA_RAMAN, J2024, nullptr, &val);
        expect_near(val, 22.74785121570053, 1e-11, "Raman J2024", &failures);

        taiyin::eval_ayanamsa(taiyin::AYANAMSA_KRISHNAMURTI, J2024, nullptr, &val);
        expect_near(val, 24.18140814182934, 1e-11, "Krishnamurti J2024", &failures);
    }

    // --- Test Dynamic/Object-Tracking Mode J2024 ---
    {
        double eps = 23.4392911 * taiyin::TAIYIN_DEG_TO_RAD;

        const auto* catalog = taiyin::get_builtin_star_catalog();

        // Register custom identity precession in the core registry for testing
        taiyin::dispatch::register_precession_model(1234, test_identity_precession);

        // True Chitrapaksha (Spica)
        int spica_id = taiyin::internal::register_celestial_body("spica");
        taiyin::internal::CompiledEphemerisBlock spica_block;
        taiyin::star_catalog_get_compiled_block(catalog, spica_id, &spica_block);

        taiyin::AyanamsaDispatchData spica_ctx;
        spica_ctx.precession_model_id = 1234;
        spica_ctx.true_obliquity_rad = eps;
        spica_ctx.dynamic_block = &spica_block;

        double ayan_spica = 0.0;
        taiyin::eval_ayanamsa(taiyin::AYANAMSA_TRUE_CHITRAPAKSHA, J2024, &spica_ctx, &ayan_spica);
        expect_near(ayan_spica, 23.8411713234182, 1e-8, "True Chitrapaksha J2024 (Identity Precession)", &failures);

        // Galactic Center
        int gc_id = taiyin::internal::register_celestial_body("gc");
        taiyin::internal::CompiledEphemerisBlock gc_block;
        taiyin::star_catalog_get_compiled_block(catalog, gc_id, &gc_block);

        taiyin::AyanamsaDispatchData gc_ctx;
        gc_ctx.precession_model_id = 1234;
        gc_ctx.true_obliquity_rad = eps;
        gc_ctx.dynamic_block = &gc_block;

        double ayan_gc = 0.0;
        taiyin::eval_ayanamsa(taiyin::AYANAMSA_GALACTIC_CENTER, J2024, &gc_ctx, &ayan_gc);
        expect_near(ayan_gc, 26.851731189340626, 1e-8, "Galactic Center J2024 (Identity Precession)", &failures);
    }

    if (failures == 0) {
        std::cout << "test_ayanamsa: ALL TESTS PASSED\n";
        return 0;
    } else {
        std::cerr << "test_ayanamsa: " << failures << " FAILURES\n";
        return 1;
    }
}
