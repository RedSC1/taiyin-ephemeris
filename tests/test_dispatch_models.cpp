#include "taiyin/dispatch.h"

#include <cmath>
#include <iostream>

namespace {

const int CUSTOM_NUTATION = taiyin::dispatch::NUTATION_CUSTOM_START + 101;
const int CUSTOM_PRECESSION = taiyin::dispatch::PRECESSION_CUSTOM_START + 101;

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

bool custom_nutation(double jd_tt, const void*, taiyin::NutationAngles* out) {
    if (!out) {
        return false;
    }
    out->dpsi_rad = jd_tt * 1.0e-12;
    out->deps_rad = jd_tt * 2.0e-12;
    out->mean_obliquity_rad = 0.4;
    out->true_obliquity_rad = 0.4 + out->deps_rad;
    return true;
}

bool custom_precession(
    double jd_tt,
    const void*,
    taiyin::Matrix3x3* out,
    double* out_mean_obliquity_rad
) {
    if (!out) {
        return false;
    }
    *out = taiyin::matrix3x3_identity();
    out->m[0][0] = jd_tt;
    if (out_mean_obliquity_rad) {
        *out_mean_obliquity_rad = 0.5;
    }
    return true;
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
    expect_true(eval_selected_nutation(MODEL_SELECTION_DEFAULT, 2451545.0, 0, &nutation), "eval selected custom nutation", failures);
    expect_near(nutation.dpsi_rad, 2451545.0e-12, 1e-18, "custom nutation dpsi", failures);

    const int precession_order[] = { CUSTOM_PRECESSION, PRECESSION_IAU2006, PRECESSION_VONDRAK2011 };
    expect_true(set_precession_priority_order(precession_order, sizeof(precession_order) / sizeof(precession_order[0])), "set custom precession priority order", failures);
    PrecessionModelEntry selected_precession;
    expect_true(select_precession_model(MODEL_SELECTION_DEFAULT, &selected_precession), "select custom default precession", failures);
    expect_int(selected_precession.model_id, CUSTOM_PRECESSION, "custom precession priority first", failures);

    taiyin::Matrix3x3 precession;
    double mean_obliquity = 0.0;
    expect_true(eval_selected_precession(MODEL_SELECTION_DEFAULT, 123.0, 0, &precession, &mean_obliquity), "eval selected custom precession", failures);
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

}  // namespace

int main() {
    int failures = 0;
    test_builtin_find_and_default_selection(&failures);
    test_custom_model_add_and_priority_selection(&failures);
    test_priority_order_mutation(&failures);

    if (failures == 0) {
        std::cout << "test_dispatch_models: ALL TESTS PASSED\n";
        return 0;
    }
    std::cerr << failures << " test_dispatch_models failure(s)\n";
    return 1;
}
