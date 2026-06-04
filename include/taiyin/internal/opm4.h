#ifndef TAIYIN_INTERNAL_OPM4_H
#define TAIYIN_INTERNAL_OPM4_H

#include "opm_core.h"

#include <cstddef>
#include <cstdint>

namespace taiyin {
namespace internal {

const uint8_t OPM4_VERSION = 1;
const uint16_t OPM4_HEADER_SIZE = 128;
const uint8_t OPM4_PAYLOAD_ENCODING_SEGMENT_STREAM_V1 = 1;
const uint8_t OPM4_COEFFICIENT_ENCODING_MIXED_SIGNED_INT = 1;
const uint8_t OPM4_SEGMENT_RECORD_ENCODING_ANGLES_V1 = 1;

struct OPM4Header {
    uint8_t version;
    uint8_t flags;
    uint16_t header_size;
    uint64_t payload_offset;
    uint64_t payload_size;
    int target_id;
    int center_id;
    int method_id;
    int frame_id;
    double jd_tdb_start;
    double jd_tdb_end;
    uint32_t segment_count;
    uint8_t degree_xy;
    uint8_t degree_z;
    uint8_t payload_encoding;
    uint8_t coefficient_encoding;
    uint8_t segment_record_encoding;
    double quant_unit_km;

    OPM4Header()
        : version(0),
          flags(0),
          header_size(0),
          payload_offset(0),
          payload_size(0),
          target_id(0),
          center_id(0),
          method_id(0),
          frame_id(0),
          jd_tdb_start(0.0),
          jd_tdb_end(0.0),
          segment_count(0),
          degree_xy(0),
          degree_z(0),
          payload_encoding(0),
          coefficient_encoding(0),
          segment_record_encoding(0),
          quant_unit_km(0.0) {}
};

bool parse_opm4_header(const void* bytes, size_t byte_count, OPM4Header* out) noexcept;
bool compile_opm4_ephemeris_data(const void* bytes, size_t byte_count, OpmEphemerisData** out) noexcept;
bool compile_opm4_ephemeris_data_for_range(
    const void* bytes,
    size_t byte_count,
    const OpmCompileRange& range,
    OpmEphemerisData** out
) noexcept;

}  // namespace internal
}  // namespace taiyin

#endif  // TAIYIN_INTERNAL_OPM4_H
