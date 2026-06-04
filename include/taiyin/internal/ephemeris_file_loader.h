#ifndef TAIYIN_INTERNAL_EPHEMERIS_FILE_LOADER_H
#define TAIYIN_INTERNAL_EPHEMERIS_FILE_LOADER_H

#include "ephemeris_block.h"

#include <cstdint>
#include <string>
#include <vector>

namespace taiyin {
namespace internal {

bool read_file_bytes(const std::string& path, std::vector<uint8_t>* out) noexcept;
bool decode_gzip_if_needed(const std::vector<uint8_t>& bytes, std::vector<uint8_t>* out) noexcept;
bool load_ephemeris_file_bytes(const std::string& path, std::vector<uint8_t>* out) noexcept;

bool compile_ephemeris_block_from_file(
    const std::string& path,
    const EphemerisBlockCompileOptions* options,
    StorageEphemerisBlock* out
) noexcept;

bool compile_opm4_ephemeris_block_from_file(
    const std::string& path,
    double jd_tdb_start,
    double jd_tdb_end,
    StorageEphemerisBlock* out
) noexcept;

}  // namespace internal
}  // namespace taiyin

#endif  // TAIYIN_INTERNAL_EPHEMERIS_FILE_LOADER_H
