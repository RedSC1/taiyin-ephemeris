#include "taiyin/internal/opm4.h"

#include <cmath>
#include <cstring>
#include <limits>

namespace taiyin {
namespace internal {
namespace {

uint8_t read_u8(const uint8_t* data, size_t offset) noexcept {
    return data[offset];
}

uint16_t read_u16_le(const uint8_t* data, size_t offset) noexcept {
    return static_cast<uint16_t>(data[offset])
        | (static_cast<uint16_t>(data[offset + 1]) << 8);
}

uint32_t read_u32_le(const uint8_t* data, size_t offset) noexcept {
    return static_cast<uint32_t>(data[offset])
        | (static_cast<uint32_t>(data[offset + 1]) << 8)
        | (static_cast<uint32_t>(data[offset + 2]) << 16)
        | (static_cast<uint32_t>(data[offset + 3]) << 24);
}

uint64_t read_u64_le(const uint8_t* data, size_t offset) noexcept {
    return static_cast<uint64_t>(data[offset])
        | (static_cast<uint64_t>(data[offset + 1]) << 8)
        | (static_cast<uint64_t>(data[offset + 2]) << 16)
        | (static_cast<uint64_t>(data[offset + 3]) << 24)
        | (static_cast<uint64_t>(data[offset + 4]) << 32)
        | (static_cast<uint64_t>(data[offset + 5]) << 40)
        | (static_cast<uint64_t>(data[offset + 6]) << 48)
        | (static_cast<uint64_t>(data[offset + 7]) << 56);
}

int32_t read_i32_le(const uint8_t* data, size_t offset) noexcept {
    return static_cast<int32_t>(read_u32_le(data, offset));
}

double read_f64_le(const uint8_t* data, size_t offset) noexcept {
    const uint64_t bits = read_u64_le(data, offset);
    double value = 0.0;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}

bool payload_range_is_valid(const OPM4Header& header, size_t byte_count) noexcept {
    if (header.payload_offset < header.header_size) {
        return false;
    }
    if (header.payload_offset > static_cast<uint64_t>(std::numeric_limits<size_t>::max())
        || header.payload_size > static_cast<uint64_t>(std::numeric_limits<size_t>::max())) {
        return false;
    }
    if (header.payload_offset > static_cast<uint64_t>(byte_count)) {
        return false;
    }
    return header.payload_size <= static_cast<uint64_t>(byte_count) - header.payload_offset;
}

}  // namespace

bool parse_opm4_header(const void* bytes, size_t byte_count, OPM4Header* out) noexcept {
    if (!bytes || !out || byte_count < OPM4_HEADER_SIZE) {
        return false;
    }

    const uint8_t* data = static_cast<const uint8_t*>(bytes);
    if (std::memcmp(data, "OPM4", 4) != 0) {
        return false;
    }

    OPM4Header header;
    header.version = read_u8(data, 4);
    header.flags = read_u8(data, 5);
    header.header_size = read_u16_le(data, 6);
    header.payload_offset = read_u64_le(data, 8);
    header.payload_size = read_u64_le(data, 16);
    header.target_id = read_i32_le(data, 24);
    header.center_id = read_i32_le(data, 28);
    header.method_id = read_i32_le(data, 32);
    header.frame_id = read_i32_le(data, 36);
    header.jd_tdb_start = read_f64_le(data, 40);
    header.jd_tdb_end = read_f64_le(data, 48);
    header.segment_count = read_u32_le(data, 56);
    header.degree_xy = read_u8(data, 60);
    header.degree_z = read_u8(data, 61);
    header.payload_encoding = read_u8(data, 62);
    header.coefficient_encoding = read_u8(data, 63);
    header.segment_record_encoding = read_u8(data, 64);
    header.quant_unit_km = read_f64_le(data, 72);

    if (header.version != OPM4_VERSION
        || header.flags != 0
        || header.header_size != OPM4_HEADER_SIZE
        || header.payload_encoding != OPM4_PAYLOAD_ENCODING_SEGMENT_STREAM_V1
        || header.coefficient_encoding != OPM4_COEFFICIENT_ENCODING_MIXED_SIGNED_INT
        || header.segment_record_encoding != OPM4_SEGMENT_RECORD_ENCODING_ANGLES_V1
        || !std::isfinite(header.jd_tdb_start)
        || !std::isfinite(header.jd_tdb_end)
        || !std::isfinite(header.quant_unit_km)
        || header.jd_tdb_end <= header.jd_tdb_start
        || header.quant_unit_km <= 0.0
        || header.segment_count == 0
        || !payload_range_is_valid(header, byte_count)) {
        return false;
    }

    const size_t payload_offset = static_cast<size_t>(header.payload_offset);
    const char legacy_magic[4] = { 'O', 'P', 'M', '3' };
    if (header.payload_size >= 4 && std::memcmp(data + payload_offset, legacy_magic, 4) == 0) {
        return false;
    }

    *out = header;
    return true;
}

bool compile_opm4_ephemeris_data(const void* bytes, size_t byte_count, OpmEphemerisData** out) noexcept {
    return compile_opm4_ephemeris_data_for_range(bytes, byte_count, OpmCompileRange(), out);
}

bool compile_opm4_ephemeris_data_for_range(
    const void* bytes,
    size_t byte_count,
    const OpmCompileRange& range,
    OpmEphemerisData** out
) noexcept {
    if (!bytes || !out) {
        return false;
    }
    *out = 0;

    OPM4Header header;
    if (!parse_opm4_header(bytes, byte_count, &header)) {
        return false;
    }

    const uint8_t* data = static_cast<const uint8_t*>(bytes);
    const size_t payload_offset = static_cast<size_t>(header.payload_offset);
    const size_t payload_size = static_cast<size_t>(header.payload_size);
    return compile_opm_segment_stream(
        data + payload_offset,
        payload_size,
        header.target_id,
        header.jd_tdb_start,
        header.jd_tdb_end,
        header.quant_unit_km,
        header.segment_count,
        header.degree_xy,
        header.degree_z,
        range,
        out);
}

}  // namespace internal
}  // namespace taiyin
