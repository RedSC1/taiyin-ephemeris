#include "taiyin/dispatch.h"

#include <cmath>
#include <iostream>

namespace {

const int CUSTOM_NUTATION = taiyin::dispatch::NUTATION_CUSTOM_START + 101;
const int CUSTOM_PRECESSION = taiyin::dispatch::PRECESSION_CUSTOM_START + 101;
const int CUSTOM_DELTA_T_CORRECTION = taiyin::dispatch::DELTA_T_EPHEMERIS_CORRECTION_CUSTOM_START + 101;
const int CUSTOM_DELTA_T_MODEL = taiyin::dispatch::DELTA_T_CUSTOM_START + 101;
const int CUSTOM_DELTA_T_MODEL_FOR_COMBINED = taiyin::dispatch::DELTA_T_CUSTOM_START + 102;
const int CUSTOM_EPHEMERIS_FAMILY = taiyin::dispatch::EPHEMERIS_FAMILY_CUSTOM_START + 101;

taiyin::SplitJulianDate split_jd(double value) {
    taiyin::SplitJulianDate result;
    taiyin::split_julian_date_from_double(value, &result);
    return result;
}

double scalar_jd(const taiyin::SplitJulianDate& value) {
    return taiyin::split_julian_date_to_double(value);
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

bool custom_nutation(
    const taiyin::SplitJulianDate& jd_tt_split,
    const void*,
    taiyin::NutationAngles* out
) {
    if (!out) {
        return false;
    }
    const double jd_tt = scalar_jd(jd_tt_split);
    out->dpsi_rad = jd_tt * 1.0e-12;
    out->deps_rad = jd_tt * 2.0e-12;
    out->mean_obliquity_rad = 0.4;
    out->true_obliquity_rad = 0.4 + out->deps_rad;
    return true;
}

bool custom_precession(
    const taiyin::SplitJulianDate& jd_tt_split,
    const void*,
    taiyin::Matrix3x3* out,
    double* out_mean_obliquity_rad
) {
    if (!out) {
        return false;
    }
    const double jd_tt = scalar_jd(jd_tt_split);
    *out = taiyin::matrix3x3_identity();
    out->m[0][0] = jd_tt;
    if (out_mean_obliquity_rad) {
        *out_mean_obliquity_rad = 0.5;
    }
    return true;
}

double custom_delta_t_model(const taiyin::SplitJulianDate& jd_ut_split, const void*) {
    const double jd_ut = scalar_jd(jd_ut_split);
    return 100.0 + jd_ut * 1.0e-5;
}

double custom_delta_t_correction(
    const taiyin::SplitJulianDate& jd_ut_split,
    int delta_t_model_id,
    int ephemeris_family_id,
    const void*
) {
    const double jd_ut = scalar_jd(jd_ut_split);
    return 10.0 + jd_ut * 1.0e-6 + delta_t_model_id * 1.0e-3 + ephemeris_family_id * 1.0e-4;
}

void test_builtin_find_and_default_selection(int* failures) {
    using namespace taiyin::dispatch;

    NutationModelEntry nutation;
    expect_true(find_nutation_model(NUTATION_IAU2000B, &nutation), "find builtin IAU2000B nutation", failures);
    expect_int(nutation.model_id, NUTATION_IAU2000B, "builtin nutation id", failures);
    expect_true(nutation.eval != 0, "builtin nutation fn", failures);

    PrecessionModelEntry precession;
    expect_true(find_precession_model(PRECESSION_IAU2006, &precession), "find builtin IAU2006 precession", failures);
    expect_int(precession.model_id, PRECESSION_IAU2006, "builtin precession id", failures);
    expect_true(precession.eval != 0, "builtin precession fn", failures);

    expect_true(select_nutation_model(MODEL_SELECTION_DEFAULT, &nutation), "select default nutation", failures);
    expect_int(nutation.model_id, NUTATION_IAU2000B, "default nutation priority", failures);

    expect_true(select_precession_model(MODEL_SELECTION_DEFAULT, &precession), "select default precession", failures);
    expect_int(precession.model_id, PRECESSION_IAU2006, "default precession priority", failures);

    expect_true(select_nutation_model(NUTATION_IAU2000B, &nutation), "select explicit zero-valued nutation id", failures);
    expect_int(nutation.model_id, NUTATION_IAU2000B, "explicit zero-valued nutation id", failures);
    expect_true(select_precession_model(PRECESSION_VONDRAK2011, &precession), "select explicit zero-valued precession id", failures);
    expect_int(precession.model_id, PRECESSION_VONDRAK2011, "explicit zero-valued precession id", failures);
}

void test_custom_model_add_and_priority_selection(int* failures) {
    using namespace taiyin::dispatch;

    expect_true(add_nutation_model(NutationModelEntry(CUSTOM_NUTATION, &custom_nutation)), "add custom nutation", failures);
    expect_false(add_nutation_model(NutationModelEntry(CUSTOM_NUTATION, &custom_nutation)), "duplicate custom nutation rejected", failures);
    expect_false(add_nutation_model(NutationModelEntry(NUTATION_IAU2000B, &custom_nutation)), "builtin nutation duplicate rejected", failures);

    expect_true(add_precession_model(PrecessionModelEntry(CUSTOM_PRECESSION, &custom_precession)), "add custom precession", failures);
    expect_false(add_precession_model(PrecessionModelEntry(CUSTOM_PRECESSION, &custom_precession)), "duplicate custom precession rejected", failures);
    expect_false(add_precession_model(PrecessionModelEntry(PRECESSION_IAU2006, &custom_precession)), "builtin precession duplicate rejected", failures);

    const int nutation_order[] = { CUSTOM_NUTATION, NUTATION_IAU2000B, NUTATION_IAU2000A };
    expect_true(set_nutation_priority_order(nutation_order, sizeof(nutation_order) / sizeof(nutation_order[0])), "set custom nutation priority order", failures);
    NutationModelEntry selected_nutation;
    expect_true(select_nutation_model(MODEL_SELECTION_DEFAULT, &selected_nutation), "select custom default nutation", failures);
    expect_int(selected_nutation.model_id, CUSTOM_NUTATION, "custom nutation priority first", failures);

    taiyin::NutationAngles nutation;
    expect_true(
        eval_selected_nutation(
            MODEL_SELECTION_DEFAULT, split_jd(2451545.0), 0, &nutation),
        "eval selected custom nutation",
        failures);
    expect_near(nutation.dpsi_rad, 2451545.0e-12, 1e-18, "custom nutation dpsi", failures);

    const int precession_order[] = { CUSTOM_PRECESSION, PRECESSION_IAU2006, PRECESSION_VONDRAK2011 };
    expect_true(set_precession_priority_order(precession_order, sizeof(precession_order) / sizeof(precession_order[0])), "set custom precession priority order", failures);
    PrecessionModelEntry selected_precession;
    expect_true(select_precession_model(MODEL_SELECTION_DEFAULT, &selected_precession), "select custom default precession", failures);
    expect_int(selected_precession.model_id, CUSTOM_PRECESSION, "custom precession priority first", failures);

    taiyin::Matrix3x3 precession;
    double mean_obliquity = 0.0;
    expect_true(
        eval_selected_precession(
            MODEL_SELECTION_DEFAULT,
            split_jd(123.0),
            0,
            &precession,
            &mean_obliquity),
        "eval selected custom precession",
        failures);
    expect_near(precession.m[0][0], 123.0, 0.0, "custom precession matrix", failures);
    expect_near(mean_obliquity, 0.5, 0.0, "custom precession mean obliquity", failures);
}

void test_priority_order_mutation(int* failures) {
    using namespace taiyin::dispatch;

    const int nutation_order[] = { NUTATION_IAU2000B };
    expect_true(set_nutation_priority_order(nutation_order, sizeof(nutation_order) / sizeof(nutation_order[0])), "reset nutation priority", failures);
    expect_true(push_nutation_priority_model(CUSTOM_NUTATION), "push custom nutation priority", failures);
    expect_false(push_nutation_priority_model(CUSTOM_NUTATION), "duplicate push custom nutation rejected", failures);
    expect_true(remove_nutation_priority_model(CUSTOM_NUTATION), "remove custom nutation priority", failures);
    expect_true(insert_nutation_priority_model(0, CUSTOM_NUTATION), "insert custom nutation priority first", failures);
    NutationModelEntry selected_nutation;
    expect_true(select_nutation_model(MODEL_SELECTION_DEFAULT, &selected_nutation), "select inserted custom nutation", failures);
    expect_int(selected_nutation.model_id, CUSTOM_NUTATION, "inserted custom nutation selected", failures);

    const int precession_order[] = { PRECESSION_IAU2006 };
    expect_true(set_precession_priority_order(precession_order, sizeof(precession_order) / sizeof(precession_order[0])), "reset precession priority", failures);
    expect_true(push_precession_priority_model(CUSTOM_PRECESSION), "push custom precession priority", failures);
    expect_false(push_precession_priority_model(CUSTOM_PRECESSION), "duplicate push custom precession rejected", failures);
    expect_true(remove_precession_priority_model(CUSTOM_PRECESSION), "remove custom precession priority", failures);
    expect_true(insert_precession_priority_model(0, CUSTOM_PRECESSION), "insert custom precession priority first", failures);
    PrecessionModelEntry selected_precession;
    expect_true(select_precession_model(MODEL_SELECTION_DEFAULT, &selected_precession), "select inserted custom precession", failures);
    expect_int(selected_precession.model_id, CUSTOM_PRECESSION, "inserted custom precession selected", failures);

    const int bad_order[] = { 987654321 };
    expect_false(set_nutation_priority_order(bad_order, 1), "reject missing nutation priority id", failures);
    expect_false(set_precession_priority_order(bad_order, 1), "reject missing precession priority id", failures);
}

void test_delta_t_ephemeris_correction_registry(int* failures) {
    using namespace taiyin::dispatch;

    DeltaTEphemerisCorrectionEntry builtin;
    expect_true(
        find_delta_t_ephemeris_correction(DELTA_T_EPHEMERIS_CORRECTION_NONE, &builtin),
        "find builtin Delta-T no-op correction",
        failures);
    expect_int(builtin.correction_id, DELTA_T_EPHEMERIS_CORRECTION_NONE, "builtin Delta-T no-op id", failures);
    expect_true(builtin.eval != 0, "builtin Delta-T no-op fn", failures);

    expect_int(
        find_delta_t_ephemeris_correction_binding(DELTA_T_ESTIMATED_DEFAULT, EPHEMERIS_FAMILY_DE441),
        DELTA_T_EPHEMERIS_CORRECTION_NONE,
        "builtin DE441 no-op binding",
        failures);
    expect_near(
        eval_delta_t_ephemeris_correction(
            DELTA_T_ESTIMATED_DEFAULT, EPHEMERIS_FAMILY_DE431, split_jd(2451545.0), 0),
        0.0,
        0.0,
        "builtin DE431 no-op correction",
        failures);
    expect_near(
        eval_delta_t_ephemeris_correction(
            CUSTOM_DELTA_T_MODEL, CUSTOM_EPHEMERIS_FAMILY, split_jd(2451545.0), 0),
        0.0,
        0.0,
        "missing Delta-T correction binding returns zero",
        failures);

    expect_false(
        bind_delta_t_ephemeris_correction(CUSTOM_DELTA_T_MODEL, CUSTOM_EPHEMERIS_FAMILY, CUSTOM_DELTA_T_CORRECTION),
        "reject binding to missing Delta-T correction",
        failures);
    expect_true(
        add_delta_t_ephemeris_correction(
            DeltaTEphemerisCorrectionEntry(CUSTOM_DELTA_T_CORRECTION, &custom_delta_t_correction)),
        "add custom Delta-T correction",
        failures);
    expect_false(
        add_delta_t_ephemeris_correction(
            DeltaTEphemerisCorrectionEntry(CUSTOM_DELTA_T_CORRECTION, &custom_delta_t_correction)),
        "duplicate custom Delta-T correction rejected",
        failures);
    expect_false(
        add_delta_t_ephemeris_correction(
            DeltaTEphemerisCorrectionEntry(DELTA_T_EPHEMERIS_CORRECTION_NONE, &custom_delta_t_correction)),
        "builtin Delta-T correction id rejected by add",
        failures);
    expect_true(
        bind_delta_t_ephemeris_correction(CUSTOM_DELTA_T_MODEL, CUSTOM_EPHEMERIS_FAMILY, CUSTOM_DELTA_T_CORRECTION),
        "bind custom Delta-T correction",
        failures);
    expect_int(
        find_delta_t_ephemeris_correction_binding(CUSTOM_DELTA_T_MODEL, CUSTOM_EPHEMERIS_FAMILY),
        CUSTOM_DELTA_T_CORRECTION,
        "find custom Delta-T correction binding",
        failures);

    const double jd = 1000.0;
    const double expected = 10.0 + jd * 1.0e-6 + CUSTOM_DELTA_T_MODEL * 1.0e-3 + CUSTOM_EPHEMERIS_FAMILY * 1.0e-4;
    expect_near(
        eval_delta_t_ephemeris_correction(
            CUSTOM_DELTA_T_MODEL, CUSTOM_EPHEMERIS_FAMILY, split_jd(jd), 0),
        expected,
        1e-15,
        "eval custom Delta-T correction",
        failures);
    expect_true(
        bind_delta_t_ephemeris_correction(CUSTOM_DELTA_T_MODEL, CUSTOM_EPHEMERIS_FAMILY, DELTA_T_EPHEMERIS_CORRECTION_NONE),
        "bind custom Delta-T correction to no-op",
        failures);
    expect_near(
        eval_delta_t_ephemeris_correction(
            CUSTOM_DELTA_T_MODEL, CUSTOM_EPHEMERIS_FAMILY, split_jd(jd), 0),
        0.0,
        0.0,
        "explicit no-op Delta-T correction returns zero",
        failures);
}

void test_delta_t_model_registry(int* failures) {
    using namespace taiyin::dispatch;

    DeltaTModelEntry builtin;
    expect_true(find_delta_t_model(DELTA_T_ESTIMATED_DEFAULT, &builtin), "find builtin Delta-T model", failures);
    expect_int(builtin.model_id, DELTA_T_ESTIMATED_DEFAULT, "builtin Delta-T model id", failures);
    expect_true(builtin.eval != 0, "builtin Delta-T model fn", failures);
    expect_near(
        eval_delta_t(DELTA_T_ESTIMATED_DEFAULT, split_jd(2451545.0), 0),
        builtin.eval(split_jd(2451545.0), 0),
        0.0,
        "builtin Delta-T eval dispatch",
        failures);

    expect_true(
        add_delta_t_model(DeltaTModelEntry(CUSTOM_DELTA_T_MODEL_FOR_COMBINED, &custom_delta_t_model)),
        "add custom Delta-T model",
        failures);
    expect_false(
        add_delta_t_model(DeltaTModelEntry(CUSTOM_DELTA_T_MODEL_FOR_COMBINED, &custom_delta_t_model)),
        "duplicate custom Delta-T model rejected",
        failures);
    expect_false(
        add_delta_t_model(DeltaTModelEntry(DELTA_T_ESTIMATED_DEFAULT, &custom_delta_t_model)),
        "builtin Delta-T model id rejected by add",
        failures);
    expect_near(
        eval_delta_t(CUSTOM_DELTA_T_MODEL_FOR_COMBINED, split_jd(2000.0), 0),
        100.0 + 2000.0 * 1.0e-5,
        1e-15,
        "custom Delta-T model eval",
        failures);
}

void test_delta_t_model_and_correction_combined(int* failures) {
    using namespace taiyin::dispatch;

    const int correction_id = DELTA_T_EPHEMERIS_CORRECTION_CUSTOM_START + 202;
    const int model_id = DELTA_T_CUSTOM_START + 202;
    const int family_id = EPHEMERIS_FAMILY_CUSTOM_START + 202;
    const double jd = 3000.0;

    expect_true(add_delta_t_model(DeltaTModelEntry(model_id, &custom_delta_t_model)), "add combined Delta-T model", failures);
    expect_true(
        add_delta_t_ephemeris_correction(
            DeltaTEphemerisCorrectionEntry(correction_id, &custom_delta_t_correction)),
        "add combined Delta-T correction",
        failures);
    expect_true(
        bind_delta_t_ephemeris_correction(model_id, family_id, correction_id),
        "bind combined Delta-T correction",
        failures);

    const double base = custom_delta_t_model(split_jd(jd), 0);
    const double correction = custom_delta_t_correction(split_jd(jd), model_id, family_id, 0);
    expect_near(
        eval_delta_t_with_ephemeris_correction(model_id, family_id, split_jd(jd), 0, 0),
        base + correction,
        1e-15,
        "Delta-T model and ephemeris correction combine",
        failures);
}

void test_tdb_inverse_options(int* failures) {
    using namespace taiyin;
    using namespace taiyin::dispatch;

    const SplitJulianDate jd_tt = split_jd(2460409.123456789);
    SplitJulianDate jd_tdb;
    expect_true(
        tt_to_tdb_split_jd(jd_tt, TdbModel::SofaFull, &jd_tdb),
        "construct TDB inverse fixture",
        failures);

    const TdbInverseDispatchData options = {1, 1.0};
    SplitJulianDate dispatched;
    SplitJulianDate direct;
    expect_true(
        eval_tdb_inverse(TDB_SOFA_FULL, jd_tdb, &options, &dispatched),
        "dispatch TDB inverse with custom options",
        failures);
    expect_true(
        tdb_to_tt_split_jd(
            jd_tdb, TdbModel::SofaFull, options.max_iterations,
            options.tolerance_days, &direct),
        "direct TDB inverse with custom options",
        failures);
    expect_near(
        seconds_between_split_jd(direct, dispatched),
        0.0,
        0.0,
        "TDB inverse dispatch honors iteration and tolerance options",
        failures);
}

}  // namespace

int main() {
    int failures = 0;
    test_builtin_find_and_default_selection(&failures);
    test_custom_model_add_and_priority_selection(&failures);
    test_priority_order_mutation(&failures);
    test_delta_t_model_registry(&failures);
    test_delta_t_ephemeris_correction_registry(&failures);
    test_delta_t_model_and_correction_combined(&failures);
    test_tdb_inverse_options(&failures);

    if (failures == 0) {
        std::cout << "test_dispatch_models: ALL TESTS PASSED\n";
        return 0;
    }
    std::cerr << failures << " test_dispatch_models failure(s)\n";
    return 1;
}
