#include "taiyin/lunar_limb_tll1.h"

#include <cmath>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

namespace {

int failures = 0;

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

std::vector<uint8_t> make_model() {
    const uint32_t longitude_count = 2;
    const uint32_t latitude_count = 2;
    const uint32_t angle_count = 4;
    const size_t payload_size = static_cast<size_t>(
        longitude_count * latitude_count * angle_count * sizeof(int16_t));
    std::vector<uint8_t> bytes(sizeof(taiyin::Tll1Header) + payload_size, 0);
    taiyin::Tll1Header* header = reinterpret_cast<taiyin::Tll1Header*>(&bytes[0]);
    std::memcpy(header->magic, "TLL1", 4);
    header->version = taiyin::TLL1_VERSION;
    header->header_size = sizeof(taiyin::Tll1Header);
    header->flags = taiyin::TLL1_FLAG_LITTLE_ENDIAN
        | taiyin::TLL1_FLAG_SIGNED_INT16_OFFSETS;
    header->longitude_count = longitude_count;
    header->latitude_count = latitude_count;
    header->position_angle_count = angle_count;
    header->payload_offset = sizeof(taiyin::Tll1Header);
    header->payload_size = payload_size;
    header->longitude_start_deg = -1.0;
    header->longitude_step_deg = 2.0;
    header->latitude_start_deg = -2.0;
    header->latitude_step_deg = 4.0;
    header->position_angle_start_deg = 0.0;
    header->position_angle_step_deg = 90.0;
    header->reference_radius_m = 1737400.0;
    header->mean_distance_m = 384398550.0;
    header->offset_scale_m = 0.5;

    int16_t* payload = reinterpret_cast<int16_t*>(
        &bytes[static_cast<size_t>(header->payload_offset)]);
    for (uint32_t latitude = 0; latitude < latitude_count; ++latitude) {
        for (uint32_t longitude = 0; longitude < longitude_count; ++longitude) {
            for (uint32_t angle = 0; angle < angle_count; ++angle) {
                const size_t index = (latitude * longitude_count + longitude)
                    * angle_count + angle;
                payload[index] = static_cast<int16_t>(
                    1000 * latitude + 100 * longitude + 10 * angle);
            }
        }
    }
    return bytes;
}

void test_memory_load_and_interpolation() {
    std::vector<uint8_t> bytes = make_model();
    taiyin::Tll1LunarLimbModel model;
    expect_true(
        taiyin::tll1_lunar_limb_load_from_memory(&model, &bytes[0], bytes.size())
            == taiyin::TAIYIN_STATUS_OK,
        "load synthetic model");

    double offset_m = 0.0;
    expect_true(
        taiyin::tll1_lunar_limb_offset_m(&model, -1.0, -2.0, 0.0, &offset_m)
            == taiyin::TAIYIN_STATUS_OK,
        "query exact first sample");
    expect_near(offset_m, 0.0, 1.0e-12, "exact first sample value");

    expect_true(
        taiyin::tll1_lunar_limb_offset_m(&model, 0.0, 0.0, 45.0, &offset_m)
            == taiyin::TAIYIN_STATUS_OK,
        "query trilinear midpoint");
    expect_near(offset_m, 277.5, 1.0e-12, "trilinear midpoint value");

    double wrapped = 0.0;
    expect_true(
        taiyin::tll1_lunar_limb_offset_m(&model, -1.0, -2.0, 315.0, &wrapped)
            == taiyin::TAIYIN_STATUS_OK,
        "query circular angle interpolation");
    expect_near(wrapped, 7.5, 1.0e-12, "circular angle interpolation value");

    double radius_m = 0.0;
    expect_true(
        taiyin::tll1_lunar_limb_radius_m(&model, 1.0, 2.0, 90.0, &radius_m)
            == taiyin::TAIYIN_STATUS_OK,
        "query effective radius");
    expect_near(radius_m, 1737955.0, 1.0e-12, "effective radius value");

    expect_true(
        taiyin::tll1_lunar_limb_offset_m(&model, 1.01, 0.0, 0.0, &offset_m)
            == taiyin::TAIYIN_EPHEMERIS_ERROR_COVERAGE_GAP,
        "reject longitude outside coverage");
    expect_true(std::isnan(offset_m), "coverage failure clears output");
    taiyin::tll1_lunar_limb_destroy(&model);
}

void test_extreme_finite_position_angles() {
    std::vector<uint8_t> bytes = make_model();
    taiyin::Tll1Header* header = reinterpret_cast<taiyin::Tll1Header*>(&bytes[0]);
    header->position_angle_start_deg = std::numeric_limits<double>::max();

    taiyin::Tll1LunarLimbModel model;
    expect_true(
        taiyin::tll1_lunar_limb_load_from_memory(&model, &bytes[0], bytes.size())
            == taiyin::TAIYIN_STATUS_OK,
        "load model with extreme finite angle origin");

    double offset_m = 0.0;
    expect_true(
        taiyin::tll1_lunar_limb_offset_m(
            &model,
            -1.0,
            -2.0,
            -std::numeric_limits<double>::max(),
            &offset_m) == taiyin::TAIYIN_STATUS_OK,
        "query extreme finite position angles");
    expect_true(std::isfinite(offset_m), "extreme angle interpolation is finite");
    taiyin::tll1_lunar_limb_destroy(&model);
}

void test_file_load_and_validation() {
    std::vector<uint8_t> bytes = make_model();
    const char* path = "/tmp/taiyin_test_lunar_limb.tll1";
    {
        std::ofstream stream(path, std::ios::binary);
        stream.write(reinterpret_cast<const char*>(&bytes[0]), bytes.size());
    }
    taiyin::Tll1LunarLimbModel model;
    expect_true(
        taiyin::tll1_lunar_limb_load_from_file(&model, path)
            == taiyin::TAIYIN_STATUS_OK,
        "load mapped model file");
    expect_true(model.file.is_open(), "mapped model owns file");
    taiyin::tll1_lunar_limb_destroy(&model);
    std::remove(path);

    bytes[0] = 'X';
    expect_true(
        taiyin::tll1_lunar_limb_load_from_memory(&model, &bytes[0], bytes.size())
            == taiyin::TAIYIN_FILE_ERROR_BAD_FORMAT,
        "reject bad magic");
}

void test_bundled_kaguya_model() {
    const char* root = std::getenv("TAIYIN_REPO_ROOT");
    expect_true(root != 0 && root[0] != '\0', "bundled model test has repository root");
    if (!root || root[0] == '\0') return;

    const std::string path = std::string(root)
        + "/data/lunar-limb/kaguya_lalt_16ppd.tll1";
    taiyin::Tll1LunarLimbModel model;
    expect_true(
        taiyin::tll1_lunar_limb_load_from_file(&model, path)
            == taiyin::TAIYIN_STATUS_OK,
        "load bundled Kaguya model");
    if (!model.header) return;
    expect_true(model.header->longitude_count == 37, "bundled longitude count");
    expect_true(model.header->latitude_count == 33, "bundled latitude count");
    expect_true(model.header->position_angle_count == 1800, "bundled angle count");
    expect_true(
        (model.header->flags & taiyin::TLL1_FLAG_KAGUYA_LALT_DERIVED) != 0,
        "bundled source flag");
    expect_true(
        model.header->source_id == taiyin::TLL1_SOURCE_KAGUYA_LALT,
        "bundled source id");

    double edge_offset_m = 0.0;
    expect_true(
        taiyin::tll1_lunar_limb_offset_m(&model, 9.0, 8.0, 123.4, &edge_offset_m)
            == taiyin::TAIYIN_STATUS_OK,
        "query bundled libration coverage edge");
    expect_true(std::isfinite(edge_offset_m), "bundled edge value is finite");
    expect_true(
        taiyin::tll1_lunar_limb_offset_m(&model, 9.01, 8.0, 123.4, &edge_offset_m)
            == taiyin::TAIYIN_EPHEMERIS_ERROR_COVERAGE_GAP,
        "reject query beyond bundled libration coverage");

    double offset_m = 0.0;
    expect_true(
        taiyin::tll1_lunar_limb_offset_m(&model, 0.0, 0.0, 0.0, &offset_m)
            == taiyin::TAIYIN_STATUS_OK,
        "query bundled north limb");
    expect_near(offset_m, 46.0, 1.0e-6, "bundled north limb value");
    expect_true(
        taiyin::tll1_lunar_limb_offset_m(&model, 0.0, 0.0, 200.6, &offset_m)
            == taiyin::TAIYIN_STATUS_OK,
        "query bundled high limb");
    expect_near(offset_m, 5803.0, 1.0e-6, "bundled high limb value");
    expect_true(
        taiyin::tll1_lunar_limb_offset_m(&model, 0.0, 0.0, 359.9, &offset_m)
            == taiyin::TAIYIN_STATUS_OK,
        "query bundled angle seam");
    expect_near(offset_m, 180.0, 1.0e-6, "bundled angle seam value");
    taiyin::tll1_lunar_limb_destroy(&model);
}

}  // namespace

int main() {
    test_memory_load_and_interpolation();
    test_extreme_finite_position_angles();
    test_file_load_and_validation();
    test_bundled_kaguya_model();
    if (failures != 0) {
        std::cerr << failures << " lunar-limb TLL1 test(s) failed\n";
        return 1;
    }
    std::cout << "lunar-limb TLL1 tests passed\n";
    return 0;
}
