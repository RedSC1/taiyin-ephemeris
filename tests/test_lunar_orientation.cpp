#include "taiyin/lunar_orientation.h"
#include "taiyin/lunar_limb_tll1.h"
#include "taiyin/apparent_position.h"
#include "taiyin/runtime/lunar_limb.h"
#include "taiyin/runtime/runtime.h"

#include "taiyin/time.h"

#include <cmath>
#include <cstdlib>
#include <iostream>

namespace {

int failures = 0;

taiyin::SplitJulianDate split_jd(double value) {
    taiyin::SplitJulianDate result;
    taiyin::split_julian_date_from_double(value, &result);
    return result;
}

void expect_true(bool value, const char* label) {
    if (!value) {
        std::cerr << "FAIL: " << label << "\n";
        ++failures;
    }
}

void expect_near(double actual, double expected, double tolerance, const char* label) {
    if (!std::isfinite(actual) || std::fabs(actual - expected) > tolerance) {
        std::cerr << "FAIL: " << label << " actual=" << actual
                  << " expected=" << expected << "\n";
        ++failures;
    }
}

void expect_angle_near(double actual, double expected, double tolerance, const char* label) {
    const double difference = std::remainder(actual - expected, 360.0);
    if (!std::isfinite(difference) || std::fabs(difference) > tolerance) {
        std::cerr << "FAIL: " << label << " actual=" << actual
                  << " expected=" << expected << "\n";
        ++failures;
    }
}

void expect_matrix(
    double jd_tdb,
    const double expected[3][3],
    const char* label
) {
    taiyin::Matrix3x3 matrix;
    expect_true(
        taiyin::iau2009_moon_j2000_to_mean_earth_matrix(split_jd(jd_tdb), &matrix),
        label);
    for (int row = 0; row < 3; ++row) {
        for (int column = 0; column < 3; ++column) {
            expect_near(
                matrix.m[row][column],
                expected[row][column],
                5.0e-13,
                label);
        }
    }
}

void test_naif_orientation_oracles() {
    // Generated with SpiceyPy pxform("J2000", "IAU_MOON", et) after loading
    // the NAIF generic text kernel pck00011.tpc.
    const double j2000[3][3] = {
        { 0.7842270520919169, 0.55784711246016394, 0.27165148607559469 },
        { -0.62006191525085586, 0.72055666546681307, 0.31035675134719964 },
        { -0.022608671404182493, -0.41183090094261288, 0.91097977859342927 },
    };
    const double plus_1234_5[3][3] = {
        { -0.25110668392380764, 0.89558833486439227, 0.36724238282585836 },
        { -0.96768754636559284, -0.22327550864722631, -0.1171702174072442 },
        { -0.022940050081189715, -0.38479810510597329, 0.92271565089637708 },
    };
    expect_matrix(taiyin::JD_J2000, j2000, "NAIF J2000 lunar orientation");
    expect_matrix(
        taiyin::JD_J2000 + 1234.5,
        plus_1234_5,
        "NAIF offset lunar orientation");
}

void test_limb_view_coordinates() {
    taiyin::Matrix3x3 j2000_to_moon;
    expect_true(
        taiyin::iau2009_moon_j2000_to_mean_earth_matrix(
            split_jd(taiyin::JD_J2000), &j2000_to_moon),
        "build view test orientation");
    const taiyin::Matrix3x3 moon_to_j2000 = taiyin::matrix3x3_transpose(j2000_to_moon);
    const taiyin::Vector3 body_view = { 1.0, 0.0, 0.0 };
    const taiyin::Vector3 body_north = { 0.0, 0.0, 1.0 };
    const taiyin::Vector3 body_east = { 0.0, 1.0, 0.0 };

    taiyin::LunarLimbViewCoordinates result;
    expect_true(
        taiyin::lunar_limb_view_coordinates_j2000(
            split_jd(taiyin::JD_J2000),
            taiyin::matrix3x3_multiply_vector(moon_to_j2000, body_view),
            taiyin::matrix3x3_multiply_vector(moon_to_j2000, body_north),
            &result) == taiyin::TAIYIN_STATUS_OK,
        "resolve north limb view");
    expect_near(result.libration_longitude_deg, 0.0, 1.0e-12, "zero libration longitude");
    expect_near(result.libration_latitude_deg, 0.0, 1.0e-12, "zero libration latitude");
    expect_near(result.position_angle_deg, 0.0, 1.0e-12, "north position angle");

    expect_true(
        taiyin::lunar_limb_view_coordinates_j2000(
            split_jd(taiyin::JD_J2000),
            taiyin::matrix3x3_multiply_vector(moon_to_j2000, body_view),
            taiyin::matrix3x3_multiply_vector(moon_to_j2000, body_east),
            &result) == taiyin::TAIYIN_STATUS_OK,
        "resolve east limb view");
    expect_near(result.position_angle_deg, 90.0, 1.0e-12, "east position angle");
}

void test_runtime_limb_query() {
    const char* root = std::getenv("TAIYIN_REPO_ROOT");
    expect_true(root != 0 && root[0] != '\0', "runtime limb test has repository root");
    if (!root || root[0] == '\0') return;

    const std::string path = std::string(root)
        + "/data/lunar-limb/kaguya_lalt_16ppd.tll1";
    expect_true(
        taiyin::runtime::load_global_lunar_limb_model(path.c_str())
            == taiyin::TAIYIN_STATUS_OK,
        "load global runtime limb model");
    if (!taiyin::runtime::global_lunar_limb_model()) return;

    taiyin::Matrix3x3 j2000_to_moon;
    expect_true(
        taiyin::iau2009_moon_j2000_to_mean_earth_matrix(
            split_jd(taiyin::JD_J2000), &j2000_to_moon),
        "build runtime limb orientation");
    const taiyin::Matrix3x3 moon_to_j2000 = taiyin::matrix3x3_transpose(j2000_to_moon);
    const taiyin::Vector3 body_view = { 1.0, 0.0, 0.0 };
    const taiyin::Vector3 body_north = { 0.0, 0.0, 1.0 };

    taiyin::runtime::NativeCalcContext context;
    double radius_m = 0.0;
    taiyin::LunarLimbViewCoordinates view;
    expect_true(
        taiyin::runtime::eval_lunar_limb_radius_m(
            &context,
            split_jd(taiyin::JD_J2000),
            taiyin::matrix3x3_multiply_vector(moon_to_j2000, body_view),
            taiyin::matrix3x3_multiply_vector(moon_to_j2000, body_north),
            &radius_m,
            &view) == taiyin::TAIYIN_STATUS_OK,
        "evaluate runtime limb radius");
    expect_near(radius_m, 1737446.0, 1.0e-6, "runtime north limb radius");
    expect_near(view.position_angle_deg, 0.0, 1.0e-12, "runtime north limb PA");

    taiyin::runtime::PreparedLunarLimbQuery prepared;
    expect_true(
        taiyin::runtime::prepare_lunar_limb_query(
            &context,
            split_jd(taiyin::JD_J2000),
            false,
            taiyin::TAIYIN_APPARENT_FRAME_ICRF,
            &prepared) == taiyin::TAIYIN_STATUS_OK,
        "prepare runtime limb query");
    double prepared_radius_m = 0.0;
    taiyin::LunarLimbViewCoordinates prepared_view;
    expect_true(
        taiyin::runtime::eval_prepared_lunar_limb_radius_m(
            &prepared,
            taiyin::matrix3x3_multiply_vector(moon_to_j2000, body_view),
            taiyin::matrix3x3_multiply_vector(moon_to_j2000, body_north),
            &prepared_radius_m,
            &prepared_view) == taiyin::TAIYIN_STATUS_OK,
        "evaluate prepared runtime limb query");
    expect_near(prepared_radius_m, radius_m, 1.0e-6, "prepared runtime radius");
    expect_angle_near(
        prepared_view.position_angle_deg,
        view.position_angle_deg,
        1.0e-10,
        "prepared runtime position angle");

    expect_true(
        taiyin::runtime::load_global_lunar_limb_model(nullptr)
            == taiyin::TAIYIN_STATUS_OK,
        "unload global runtime limb model");
    expect_true(
        taiyin::runtime::global_lunar_limb_model() == nullptr,
        "global runtime limb model is cleared");
}

}  // namespace

int main() {
    test_naif_orientation_oracles();
    test_limb_view_coordinates();
    test_runtime_limb_query();
    if (failures != 0) {
        std::cerr << failures << " lunar orientation test(s) failed\n";
        return 1;
    }
    std::cout << "lunar orientation tests passed\n";
    return 0;
}
