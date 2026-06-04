#include "taiyin/internal/ephemeris_file_loader.h"

#include "taiyin/internal/gzip.h"

#include <cmath>
#include <fstream>
#include <limits>

namespace taiyin {
namespace internal {
namespace {

bool has_gzip_magic(const std::vector<uint8_t>& bytes) noexcept {
    return bytes.size() >= 2 && bytes[0] == 0x1f && bytes[1] == 0x8b;
}

}  // namespace

bool read_file_bytes(const std::string& path, std::vector<uint8_t>* out) noexcept {
    if (path.empty() || !out) {
        return false;
    }
    out->clear();

    std::ifstream file(path.c_str(), std::ios::binary | std::ios::ate);
    if (!file) {
        return false;
    }

    const std::ifstream::pos_type end_pos = file.tellg();
    if (end_pos < 0) {
        return false;
    }
    const uint64_t byte_count = static_cast<uint64_t>(end_pos);
    if (byte_count > static_cast<uint64_t>(std::numeric_limits<size_t>::max())
        || byte_count > static_cast<uint64_t>(std::numeric_limits<std::streamsize>::max())) {
        return false;
    }

    out->resize(static_cast<size_t>(byte_count));
    file.seekg(0, std::ios::beg);
    if (!out->empty()) {
        file.read(reinterpret_cast<char*>(&(*out)[0]), static_cast<std::streamsize>(out->size()));
        if (!file) {
            out->clear();
            return false;
        }
    }

    return true;
}

bool decode_gzip_if_needed(const std::vector<uint8_t>& bytes, std::vector<uint8_t>* out) noexcept {
    if (!out) {
        return false;
    }

    if (!has_gzip_magic(bytes)) {
        *out = bytes;
        return true;
    }
    if (bytes.size() > static_cast<size_t>(std::numeric_limits<int>::max())) {
        return false;
    }

    return gzip_decompress(
        bytes.empty() ? 0 : &bytes[0],
        static_cast<int>(bytes.size()),
        out);
}

bool load_ephemeris_file_bytes(const std::string& path, std::vector<uint8_t>* out) noexcept {
    if (!out) {
        return false;
    }

    std::vector<uint8_t> file_bytes;
    if (!read_file_bytes(path, &file_bytes)) {
        return false;
    }
    return decode_gzip_if_needed(file_bytes, out);
}

bool compile_ephemeris_block_from_file(
    const std::string& path,
    const EphemerisBlockCompileOptions* options,
    StorageEphemerisBlock* out
) noexcept {
    if (!out) {
        return false;
    }
    *out = StorageEphemerisBlock();

    std::vector<uint8_t> bytes;
    if (!load_ephemeris_file_bytes(path, &bytes) || bytes.empty()) {
        return false;
    }
    return compile_ephemeris_block(&bytes[0], bytes.size(), options, out);
}

bool compile_opm4_ephemeris_block_from_file(
    const std::string& path,
    double jd_tdb_start,
    double jd_tdb_end,
    StorageEphemerisBlock* out
) noexcept {
    if (!out) {
        return false;
    }
    *out = StorageEphemerisBlock();
    if (!std::isfinite(jd_tdb_start) || !std::isfinite(jd_tdb_end) || jd_tdb_end < jd_tdb_start) {
        return false;
    }

    EphemerisBlockCompileOptions options;
    options.has_required_jd_tdb_range = true;
    options.required_jd_tdb_start = jd_tdb_start;
    options.required_jd_tdb_end = jd_tdb_end;
    return compile_ephemeris_block_from_file(path, &options, out);
}

}  // namespace internal
}  // namespace taiyin
