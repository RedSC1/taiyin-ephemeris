#include "taiyin/angle.h"
#include "taiyin/physical_constants.h"
#include "taiyin/internal/ephemeris_block.h"
#include "taiyin/internal/opm4.h"

#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
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

void push_i32_le(std::vector<uint8_t>* data, int32_t value) {
    push_u32_le(data, static_cast<uint32_t>(value));
}

void push_u64_le(std::vector<uint8_t>* data, uint64_t value) {
    for (int i = 0; i < 8; ++i) {
        data->push_back(static_cast<uint8_t>((value >> (i * 8)) & 0xFF));
    }
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

std::vector<uint8_t> make_segment_stream_payload() {
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
    return payload;
}

std::vector<uint8_t> make_test_opm4() {
    std::vector<uint8_t> payload = make_segment_stream_payload();
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

void expect_false(bool value, const char* label) {
    if (value) {
        std::fprintf(stderr, "expected false: %s\n", label);
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
    using taiyin::internal::EphemerisBlockCompileOptions;
    using taiyin::internal::EphemerisBlockFormat;
    using taiyin::internal::OPM4Header;
    using taiyin::internal::StorageEphemerisBlock;
    using taiyin::internal::compile_ephemeris_block;
    using taiyin::internal::destroy_storage_ephemeris_block;
    using taiyin::internal::eval_compiled_ephemeris_block;
    using taiyin::internal::get_compiled_block_from_storage;
    using taiyin::internal::parse_opm4_header;

    std::vector<uint8_t> bytes = make_test_opm4();
    OPM4Header header;
    expect_true(parse_opm4_header(&bytes[0], bytes.size(), &header), "parse opm4 header");
    expect_true(header.target_id == 201, "target id parsed");
    expect_true(header.center_id == 10, "center id parsed");
    expect_true(header.payload_offset == 128, "payload offset parsed");

    StorageEphemerisBlock storage;
    expect_true(compile_ephemeris_block(&bytes[0], bytes.size(), 0, &storage), "compile opm4 block");
    expect_true(storage.format == EphemerisBlockFormat::Opm4, "storage format opm4");
    CompiledEphemerisBlock view;
    expect_true(get_compiled_block_from_storage(&storage, 0, &view), "get compiled view");
    CartesianState state;
    expect_true(eval_compiled_ephemeris_block(105.0, &view, &state), "eval opm4 state");
    expect_near(state.position_au.x, 0.005 / taiyin::TAIYIN_AU_KM, 1e-20, "position x");
    expect_near(state.velocity_au_per_day.x, 0.002 / taiyin::TAIYIN_AU_KM, 1e-20, "velocity x");
    destroy_storage_ephemeris_block(&storage);

    EphemerisBlockCompileOptions range;
    range.has_required_jd_tdb_range = true;
    range.required_jd_tdb_start = 102.0;
    range.required_jd_tdb_end = 108.0;
    expect_true(compile_ephemeris_block(&bytes[0], bytes.size(), &range, &storage), "compile range-gated opm4");
    destroy_storage_ephemeris_block(&storage);

    std::vector<uint8_t> bad = bytes;
    bad[3] = '3';
    expect_false(compile_ephemeris_block(&bad[0], bad.size(), 0, &storage), "reject legacy opm magic");

    bad = bytes;
    bad[8] = 0;
    expect_false(parse_opm4_header(&bad[0], bad.size(), &header), "reject invalid payload range");

    bad = bytes;
    for (int i = 0; i < 8; ++i) {
        bad[48 + i] = bad[40 + i];
    }
    expect_false(parse_opm4_header(&bad[0], bad.size(), &header), "reject invalid coverage");

    bad = bytes;
    bad[128] = 'O'; bad[129] = 'P'; bad[130] = 'M'; bad[131] = '3';
    expect_false(parse_opm4_header(&bad[0], bad.size(), &header), "reject nested legacy magic");

    return 0;
}
