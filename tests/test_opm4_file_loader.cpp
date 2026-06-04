#include "taiyin/angle.h"
#include "taiyin/physical_constants.h"
#include "taiyin/internal/ephemeris_block.h"
#include "taiyin/internal/ephemeris_file_loader.h"

#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <vector>

namespace {

void push_u8(std::vector<uint8_t>* data, uint8_t value) { data->push_back(value); }
void push_u16_le(std::vector<uint8_t>* data, uint16_t value) {
    data->push_back(static_cast<uint8_t>(value & 0xFF));
    data->push_back(static_cast<uint8_t>((value >> 8) & 0xFF));
}
void push_u32_le(std::vector<uint8_t>* data, uint32_t value) {
    data->push_back(static_cast<uint8_t>(value & 0xFF));
    data->push_back(static_cast<uint8_t>((value >> 8) & 0xFF));
    data->push_back(static_cast<uint8_t>((value >> 16) & 0xFF));
    data->push_back(static_cast<uint8_t>((value >> 24) & 0xFF));
}
void push_i32_le(std::vector<uint8_t>* data, int32_t value) { push_u32_le(data, static_cast<uint32_t>(value)); }
void push_u64_le(std::vector<uint8_t>* data, uint64_t value) {
    for (int i = 0; i < 8; ++i) data->push_back(static_cast<uint8_t>((value >> (i * 8)) & 0xFF));
}
void push_f64_le(std::vector<uint8_t>* data, double value) {
    uint8_t bytes[8];
    std::memcpy(bytes, &value, sizeof(bytes));
    for (int i = 0; i < 8; ++i) data->push_back(bytes[i]);
}
void push_i8_coeffs(std::vector<uint8_t>* data, const int8_t* values, int count) {
    const int width_byte_count = (count + 3) / 4;
    for (int i = 0; i < width_byte_count; ++i) data->push_back(0);
    for (int i = 0; i < count; ++i) data->push_back(static_cast<uint8_t>(values[i]));
}

std::vector<uint8_t> make_test_opm4() {
    std::vector<uint8_t> payload;
    push_f64_le(&payload, 100.0);
    push_f64_le(&payload, 110.0);
    push_f64_le(&payload, 0.0);
    push_f64_le(&payload, taiyin::TAIYIN_PI / 2.0);
    push_f64_le(&payload, 0.0);
    const int8_t cx[] = { 1, 2 };
    const int8_t cy[] = { 3, 4 };
    const int8_t cz[] = { 5 };
    push_i8_coeffs(&payload, cx, 2);
    push_i8_coeffs(&payload, cy, 2);
    push_i8_coeffs(&payload, cz, 1);

    std::vector<uint8_t> data;
    push_u8(&data, 'O'); push_u8(&data, 'P'); push_u8(&data, 'M'); push_u8(&data, '4');
    push_u8(&data, 1);
    push_u8(&data, 0);
    push_u16_le(&data, 128);
    push_u64_le(&data, 128);
    push_u64_le(&data, static_cast<uint64_t>(payload.size()));
    push_i32_le(&data, 201);
    push_i32_le(&data, 10);
    push_i32_le(&data, 1);
    push_i32_le(&data, 1);
    push_f64_le(&data, 100.0);
    push_f64_le(&data, 110.0);
    push_u32_le(&data, 1);
    push_u8(&data, 1);
    push_u8(&data, 0);
    push_u8(&data, 1);
    push_u8(&data, 1);
    push_u8(&data, 1);
    for (int i = 0; i < 7; ++i) push_u8(&data, 0);
    push_f64_le(&data, 0.005);
    while (data.size() < 128) push_u8(&data, 0);
    data.insert(data.end(), payload.begin(), payload.end());
    return data;
}

void expect_true(bool value, const char* label) {
    if (!value) {
        std::fprintf(stderr, "expected true: %s\n", label);
        assert(false);
    }
}

void expect_near(double actual, double expected, double tolerance, const char* label) {
    if (std::fabs(actual - expected) > tolerance) {
        std::fprintf(stderr, "%s mismatch: actual %.17g expected %.17g\n", label, actual, expected);
        assert(false);
    }
}

}  // namespace

int main() {
    using taiyin::CartesianState;
    using taiyin::internal::CompiledEphemerisBlock;
    using taiyin::internal::EphemerisBlockFormat;
    using taiyin::internal::StorageEphemerisBlock;
    using taiyin::internal::compile_ephemeris_block_from_file;
    using taiyin::internal::compile_opm4_ephemeris_block_from_file;
    using taiyin::internal::destroy_storage_ephemeris_block;
    using taiyin::internal::eval_compiled_ephemeris_block;
    using taiyin::internal::get_compiled_block_from_storage;

    const std::vector<uint8_t> bytes = make_test_opm4();
    const char* path = "test_opm4_file_loader.tmp.opm4";
    {
        std::ofstream file(path, std::ios::binary);
        file.write(reinterpret_cast<const char*>(&bytes[0]), static_cast<std::streamsize>(bytes.size()));
        expect_true(static_cast<bool>(file), "write opm4 test file");
    }

    StorageEphemerisBlock storage;
    CompiledEphemerisBlock view;
    CartesianState state;
    expect_true(compile_ephemeris_block_from_file(path, 0, &storage), "compile opm4 file block");
    expect_true(storage.format == EphemerisBlockFormat::Opm4, "opm4 file format");
    expect_true(get_compiled_block_from_storage(&storage, 0, &view), "get view from opm4 file storage");
    expect_true(eval_compiled_ephemeris_block(105.0, &view, &state), "eval opm4 file block");
    expect_near(state.velocity_au_per_day.x, 0.002 / taiyin::TAIYIN_AU_KM, 1e-20, "opm4 velocity x");
    destroy_storage_ephemeris_block(&storage);

    expect_true(compile_opm4_ephemeris_block_from_file(path, 102.0, 108.0, &storage), "compile opm4 file covering required range");
    expect_true(get_compiled_block_from_storage(&storage, 0, &view), "get view from opm4 range storage");
    expect_true(eval_compiled_ephemeris_block(105.0, &view, &state), "eval opm4 inside required range");
    expect_true(!eval_compiled_ephemeris_block(101.0, &view, &state), "reject opm4 eval outside required range");
    destroy_storage_ephemeris_block(&storage);

    expect_true(!compile_opm4_ephemeris_block_from_file(path, 99.0, 108.0, &storage), "reject opm4 file outside required range");

    const char legacy_path[] = "test_legacy_opm_file_loader.tmp";
    {
        const uint8_t legacy[] = { 'O', 'P', 'M', '3' };
        std::ofstream file(legacy_path, std::ios::binary);
        file.write(reinterpret_cast<const char*>(legacy), static_cast<std::streamsize>(sizeof(legacy)));
        expect_true(static_cast<bool>(file), "write legacy magic file");
    }
    expect_true(!compile_ephemeris_block_from_file(legacy_path, 0, &storage), "generic runtime rejects legacy opm file");

    std::remove(path);
    std::remove(legacy_path);
    return 0;
}
