#include "taiyin/angle.h"
#include "taiyin/physical_constants.h"
#include "taiyin/internal/descriptor_loader.h"
#include "taiyin/internal/ephemeris_discovery.h"

#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <vector>

namespace {

void push_u8(std::vector<uint8_t>* data, uint8_t value) {
    data->push_back(value);
}

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
    for (int i = 0; i < 8; ++i) {
        data->push_back(bytes[i]);
    }
}

void push_i8_coeffs(std::vector<uint8_t>* data, const int8_t* values, int count) {
    const int width_byte_count = (count + 3) / 4;
    for (int i = 0; i < width_byte_count; ++i) {
        data->push_back(0);
    }
    for (int i = 0; i < count; ++i) {
        data->push_back(static_cast<uint8_t>(values[i]));
    }
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
    push_u8(&data, 'O');
    push_u8(&data, 'P');
    push_u8(&data, 'M');
    push_u8(&data, '4');
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
    for (int i = 0; i < 7; ++i) {
        push_u8(&data, 0);
    }
    push_f64_le(&data, 0.005);
    while (data.size() < 128) {
        push_u8(&data, 0);
    }
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
        std::fprintf(
            stderr,
            "%s mismatch: actual %.17g expected %.17g diff %.17g tolerance %.17g\n",
            label,
            actual,
            expected,
            actual - expected,
            tolerance);
        assert(false);
    }
}

void write_bytes(const char* path, const std::vector<uint8_t>& bytes) {
    std::ofstream file(path, std::ios::binary);
    file.write(reinterpret_cast<const char*>(&bytes[0]), static_cast<std::streamsize>(bytes.size()));
    expect_true(static_cast<bool>(file), "write descriptor loader fixture");
}

taiyin::internal::EphemerisBlockDescriptor make_descriptor(const char* path) {
    taiyin::internal::EphemerisBlockDescriptor descriptor;
    descriptor.route_key = taiyin::internal::EphemerisRouteKey(201, 10, 1, 77);
    descriptor.source_key = taiyin::internal::EphemerisBlockKey(1, 77, 1, 0);
    descriptor.target_id = 201;
    descriptor.center_id = 10;
    descriptor.method_id = 1;
    descriptor.frame = taiyin::internal::EphemerisFrame::IcrfJ2000Equatorial;
    descriptor.format = taiyin::internal::EphemerisBlockFormat::Opm4;
    descriptor.jd_tdb_start = 100.0;
    descriptor.jd_tdb_end = 110.0;
    descriptor.path = path;
    return descriptor;
}

bool vector_is_finite(const taiyin::Vector3& vector) {
    return std::isfinite(vector.x) && std::isfinite(vector.y) && std::isfinite(vector.z);
}

bool state_is_finite(const taiyin::CartesianState& state) {
    return vector_is_finite(state.position_au) && vector_is_finite(state.velocity_au_per_day);
}

}  // namespace

int main() {
    using taiyin::CartesianState;
    using taiyin::Vector3;
    using taiyin::internal::CompiledEphemerisBlock;
    using taiyin::internal::EphemerisBlockCache;
    using taiyin::internal::EphemerisBlockDescriptor;
    using taiyin::internal::EphemerisBlockFormat;
    using taiyin::internal::StorageEphemerisBlock;
    using taiyin::internal::destroy_storage_ephemeris_block;
    using taiyin::internal::eval_compiled_ephemeris_block;
    using taiyin::internal::eval_descriptor_acceleration;
    using taiyin::internal::eval_descriptor_position;
    using taiyin::internal::eval_descriptor_state;
    using taiyin::internal::eval_descriptor_velocity;
    using taiyin::internal::get_compiled_block_from_storage;
    using taiyin::internal::load_descriptor_ephemeris_block;
    using taiyin::internal::load_descriptor_into_cache;

    const char* path = "test_descriptor_loader_100_110.opm4";
    const std::vector<uint8_t> bytes = make_test_opm4();
    write_bytes(path, bytes);

    EphemerisBlockDescriptor descriptor = make_descriptor(path);

    StorageEphemerisBlock storage;
    CompiledEphemerisBlock view;
    CartesianState state;
    expect_true(load_descriptor_ephemeris_block(descriptor, &storage), "load descriptor storage");
    expect_true(storage.format == EphemerisBlockFormat::Opm4, "descriptor storage format");
    expect_true(get_compiled_block_from_storage(&storage, descriptor.target_id, &view), "get descriptor storage view");
    expect_true(eval_compiled_ephemeris_block(105.0, &view, &state), "eval descriptor storage state");
    expect_near(state.position_au.x, 0.005 / taiyin::TAIYIN_AU_KM, 1e-20, "descriptor storage position x");
    expect_near(state.velocity_au_per_day.x, 0.002 / taiyin::TAIYIN_AU_KM, 1e-20, "descriptor storage velocity x");
    destroy_storage_ephemeris_block(&storage);

    {
        EphemerisBlockCache cache(4096);
        EphemerisBlockDescriptor bucket_descriptor;
        expect_true(taiyin::internal::make_cache_bucket_descriptor_for_jd(descriptor, 105.0, &bucket_descriptor), "make manual lazy cache bucket");
        expect_true(load_descriptor_into_cache(bucket_descriptor, &cache), "load bucket descriptor into cache");
        expect_true(cache.contains(bucket_descriptor.route_key), "cache contains bucket route");

        Vector3 pos;
        expect_true(cache.eval_position(bucket_descriptor.route_key, 105.0, &pos), "eval cached bucket position");
        expect_near(pos.x, 0.005 / taiyin::TAIYIN_AU_KM, 1e-20, "cached bucket position x");
        expect_false(cache.eval_position(bucket_descriptor.route_key, 99.999, &pos), "cache rejects before bucket coverage");
        expect_false(cache.eval_position(bucket_descriptor.route_key, 110.0, &pos), "cache rejects bucket coverage end");

        std::remove(path);
        expect_true(eval_descriptor_position(descriptor, &cache, 105.0, &pos), "eval helper uses bucket cache hit without file");
        expect_near(pos.y, 0.015 / taiyin::TAIYIN_AU_KM, 1e-20, "bucket cache hit descriptor position y");
        expect_false(cache.contains(descriptor.route_key), "source route is not cached by eval helper");
    }

    write_bytes(path, bytes);
    {
        EphemerisBlockCache cache(4096);
        Vector3 pos;
        Vector3 vel;
        Vector3 acc;
        expect_true(eval_descriptor_position(descriptor, &cache, 105.0, &pos), "eval helper loads position on miss");
        EphemerisBlockDescriptor bucket_descriptor;
        expect_true(taiyin::internal::make_cache_bucket_descriptor_for_jd(descriptor, 105.0, &bucket_descriptor), "make lazy cache bucket");
        expect_true(cache.contains(bucket_descriptor.route_key), "eval helper inserted bucket route");
        expect_near(pos.z, 0.025 / taiyin::TAIYIN_AU_KM, 1e-20, "miss-loaded descriptor position z");
        expect_true(eval_descriptor_velocity(descriptor, &cache, 105.0, &vel), "eval helper velocity");
        expect_true(vector_is_finite(vel), "velocity finite");
        expect_true(eval_descriptor_acceleration(descriptor, &cache, 105.0, &acc), "eval helper acceleration");
        expect_true(vector_is_finite(acc), "acceleration finite");
        expect_true(eval_descriptor_state(descriptor, &cache, 105.0, &state), "eval helper state");
        expect_true(state_is_finite(state), "state finite");
        expect_false(eval_descriptor_position(descriptor, &cache, 110.0, &pos), "eval helper rejects coverage end");
    }

    EphemerisBlockDescriptor invalid = descriptor;
    invalid.path.clear();
    expect_false(load_descriptor_ephemeris_block(invalid, &storage), "reject empty path descriptor");

    invalid = descriptor;
    invalid.jd_tdb_end = invalid.jd_tdb_start;
    expect_false(load_descriptor_ephemeris_block(invalid, &storage), "reject invalid descriptor coverage");

    invalid = descriptor;
    invalid.format = EphemerisBlockFormat::FormatUnknown;
    expect_false(load_descriptor_ephemeris_block(invalid, &storage), "reject unsupported descriptor format");

    std::remove(path);
    return 0;
}
