#include "taiyin/internal/event_frame.h"

#include "taiyin/angle.h"
#include "taiyin/time.h"

#include <cmath>
#include <iostream>

namespace {

const int CUSTOM_PRECESSION = taiyin::dispatch::PRECESSION_CUSTOM_START + 701;
const int CUSTOM_NUTATION = taiyin::dispatch::NUTATION_CUSTOM_START + 701;

taiyin::SplitJulianDate split_jd(double value) {
    taiyin::SplitJulianDate result;
    taiyin::split_julian_date_from_double(value, &result);
    return result;
}

void expect_true(bool value, const char* label, int* failures) {
    if (!value) {
        std::cerr << "FAIL: expected true: " << label << "\n";
        ++(*failures);
    }
}

void expect_false(bool value, const char* label, int* failures) {
    if (value) {
        std::cerr << "FAIL: expected false: " << label << "\n";
        ++(*failures);
    }
}

void expect_int(int actual, int expected, const char* label, int* failures) {
    if (actual != expected) {
        std::cerr << "FAIL: " << label << ": actual=" << actual << " expected=" << expected << "\n";
        ++(*failures);
    }
}

void expect_near(double actual, double expected, double tolerance, const char* label, int* failures) {
    if (std::fabs(actual - expected) > tolerance) {
        std::cerr << "FAIL: " << label << ": actual=" << actual << " expected=" << expected
                  << " tolerance=" << tolerance << "\n";
        ++(*failures);
    }
}

bool finite_matrix(const taiyin::Matrix3x3& value) {
    for (int row = 0; row < 3; ++row) {
        for (int col = 0; col < 3; ++col) {
            if (!std::isfinite(value.m[row][col])) {
                return false;
            }
        }
    }
    return true;
}

bool custom_precession(
    const taiyin::SplitJulianDate& /*jd_tt*/,
    const void*,
    taiyin::Matrix3x3* out,
    double* out_mean_obliquity_rad
) {
    if (!out) {
        return false;
    }
    *out = taiyin::matrix3x3_identity();
    if (out_mean_obliquity_rad) {
        *out_mean_obliquity_rad = 0.4;
    }
    return true;
}

bool custom_nutation(
    const taiyin::SplitJulianDate& /*jd_tt*/,
    const void*,
    taiyin::NutationAngles* out
) {
    if (!out) {
        return false;
    }
    out->dpsi_rad = 1.0e-7;
    out->deps_rad = 2.0e-7;
    out->mean_obliquity_rad = 0.4;
    out->true_obliquity_rad = 0.4000002;
    return true;
}

bool failing_nutation(
    const taiyin::SplitJulianDate& /*jd_tt*/,
    const void*,
    taiyin::NutationAngles* /*out*/
) {
    return false;
}

void test_builtin_models_resolve_to_user_selected_entries(int* failures) {
    taiyin::dispatch::PrecessionModelEntry precession;
    taiyin::dispatch::NutationModelEntry nutation;

    expect_true(
        taiyin::internal::resolve_event_frame_models(
            taiyin::dispatch::PRECESSION_IAU2006,
            taiyin::dispatch::NUTATION_IAU2000B,
            &precession,
            &nutation),
        "resolve builtin IAU2000B event frame models",
        failures);
    expect_int(precession.model_id, taiyin::dispatch::PRECESSION_IAU2006, "precession id", failures);
    expect_int(nutation.model_id, taiyin::dispatch::NUTATION_IAU2000B, "nutation id", failures);

    expect_true(
        taiyin::internal::resolve_event_frame_models(
            taiyin::dispatch::PRECESSION_IAU2006,
            taiyin::dispatch::NUTATION_IAU2000A,
            &precession,
            &nutation),
        "resolve builtin IAU2000A event frame models",
        failures);
    expect_int(nutation.model_id, taiyin::dispatch::NUTATION_IAU2000A, "IAU2000A nutation id", failures);
}

void test_custom_models_are_not_rewritten(int* failures) {
    expect_true(
        taiyin::dispatch::add_precession_model(
            taiyin::dispatch::PrecessionModelEntry(CUSTOM_PRECESSION, &custom_precession)),
        "add custom event precession",
        failures);
    expect_true(
        taiyin::dispatch::add_nutation_model(
            taiyin::dispatch::NutationModelEntry(CUSTOM_NUTATION, &custom_nutation)),
        "add custom event nutation",
        failures);

    taiyin::dispatch::PrecessionModelEntry precession;
    taiyin::dispatch::NutationModelEntry nutation;
    expect_true(
        taiyin::internal::resolve_event_frame_models(
            CUSTOM_PRECESSION,
            CUSTOM_NUTATION,
            &precession,
            &nutation),
        "resolve custom event frame models",
        failures);
    expect_true(precession.eval == &custom_precession, "custom precession preserved", failures);
    expect_true(nutation.eval == &custom_nutation, "custom nutation preserved", failures);
}

void test_output_frame_eval(int* failures) {
    taiyin::dispatch::PrecessionModelEntry precession;
    taiyin::dispatch::NutationModelEntry nutation;
    expect_true(
        taiyin::internal::resolve_event_frame_models(
            taiyin::dispatch::PRECESSION_VONDRAK2011,
            taiyin::dispatch::NUTATION_IAU2000B,
            &precession,
            &nutation),
        "resolve long-range event frame models",
        failures);

    const int years[] = { -13000, -5000, 2000, 5000, 17000 };
    for (int i = 0; i < static_cast<int>(sizeof(years) / sizeof(years[0])); ++i) {
        const taiyin::SplitJulianDate jd_tt = split_jd(
            taiyin::JD_J2000 + (years[i] - 2000.0) * taiyin::DAYS_PER_JULIAN_YEAR);
        taiyin::Matrix3x3 matrix;
        taiyin::NutationAngles angles;
        expect_true(
            taiyin::internal::eval_event_output_frame_matrix(
                jd_tt,
                taiyin::TAIYIN_APPARENT_FRAME_TRUE_ECLIPTIC_OF_DATE,
                precession,
                nutation,
                0,
                &angles,
                &matrix),
            "eval true ecliptic frame",
            failures);
        expect_true(finite_matrix(matrix), "event frame matrix finite", failures);
        expect_true(std::isfinite(angles.mean_obliquity_rad), "event frame mean obliquity finite", failures);
    }
}

void test_mean_j2000_and_icrf_frames_do_not_call_nutation(int* failures) {
    taiyin::dispatch::PrecessionModelEntry precession(CUSTOM_PRECESSION, &custom_precession);
    taiyin::dispatch::NutationModelEntry nutation(CUSTOM_NUTATION + 1, &failing_nutation);
    taiyin::Matrix3x3 matrix;
    taiyin::NutationAngles angles;

    expect_true(
        taiyin::internal::eval_event_output_frame_matrix(
            split_jd(taiyin::JD_J2000 + 123.0),
            taiyin::TAIYIN_APPARENT_FRAME_MEAN_ECLIPTIC_OF_DATE,
            precession,
            nutation,
            0,
            &angles,
            &matrix),
        "mean ecliptic event frame does not call nutation",
        failures);
    expect_near(angles.dpsi_rad, 0.0, 0.0, "mean ecliptic dpsi is zero", failures);
    expect_near(angles.deps_rad, 0.0, 0.0, "mean ecliptic deps is zero", failures);

    expect_true(
        taiyin::internal::eval_event_output_frame_matrix(
            split_jd(taiyin::JD_J2000 + 456.0),
            taiyin::TAIYIN_APPARENT_FRAME_J2000_ECLIPTIC,
            precession,
            nutation,
            0,
            &angles,
            &matrix),
        "J2000 ecliptic event frame does not call nutation",
        failures);
    expect_near(angles.dpsi_rad, 0.0, 0.0, "J2000 ecliptic dpsi is zero", failures);
    expect_near(angles.deps_rad, 0.0, 0.0, "J2000 ecliptic deps is zero", failures);

    expect_true(
        taiyin::internal::eval_event_output_frame_matrix(
            split_jd(taiyin::JD_J2000 + 789.0),
            taiyin::TAIYIN_APPARENT_FRAME_ICRF,
            taiyin::dispatch::PrecessionModelEntry(),
            nutation,
            0,
            &angles,
            &matrix),
        "ICRF event frame does not call precession or nutation",
        failures);

    expect_false(
        taiyin::internal::eval_event_output_frame_matrix(
            split_jd(taiyin::JD_J2000 + 123.0),
            taiyin::TAIYIN_APPARENT_FRAME_TRUE_ECLIPTIC_OF_DATE,
            precession,
            nutation,
            0,
            &angles,
            &matrix),
        "true ecliptic event frame calls nutation",
        failures);
}

void test_rejects_bad_inputs(int* failures) {
    taiyin::dispatch::PrecessionModelEntry precession;
    taiyin::dispatch::NutationModelEntry nutation;
    expect_false(
        taiyin::internal::resolve_event_frame_models(
            999999,
            taiyin::dispatch::NUTATION_IAU2000B,
            &precession,
            &nutation),
        "reject unknown precession model",
        failures);
    expect_false(
        taiyin::internal::resolve_event_frame_models(
            taiyin::dispatch::PRECESSION_IAU2006,
            999999,
            &precession,
            &nutation),
        "reject unknown nutation model",
        failures);
    expect_false(
        taiyin::internal::eval_event_output_frame_matrix(
            split_jd(taiyin::JD_J2000),
            taiyin::TAIYIN_APPARENT_FRAME_TRUE_ECLIPTIC_OF_DATE,
            taiyin::dispatch::PrecessionModelEntry(),
            taiyin::dispatch::NutationModelEntry(),
            0,
            0,
            0),
        "reject empty frame eval entries",
        failures);
}

}  // namespace

int main() {
    int failures = 0;
    test_builtin_models_resolve_to_user_selected_entries(&failures);
    test_custom_models_are_not_rewritten(&failures);
    test_output_frame_eval(&failures);
    test_mean_j2000_and_icrf_frames_do_not_call_nutation(&failures);
    test_rejects_bad_inputs(&failures);

    if (failures == 0) {
        std::cout << "test_event_frame: ALL TESTS PASSED\n";
        return 0;
    }
    std::cerr << failures << " test_event_frame failure(s)\n";
    return 1;
}
