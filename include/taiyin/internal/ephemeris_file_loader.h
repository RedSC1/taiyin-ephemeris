#ifndef TAIYIN_INTERNAL_EPHEMERIS_FILE_LOADER_H
#define TAIYIN_INTERNAL_EPHEMERIS_FILE_LOADER_H

#include "ephemeris_block.h"
#include "mapped_file.h"

#include <cstdint>
#include <string>
#include <vector>

namespace taiyin {
namespace internal {

class EphemerisFileView {
public:
    EphemerisFileView() noexcept;
    ~EphemerisFileView() noexcept;

    EphemerisFileView(const EphemerisFileView&) = delete;
    EphemerisFileView& operator=(const EphemerisFileView&) = delete;

    bool open_readonly(const std::string& path) noexcept;
    void close() noexcept;

    const uint8_t* data() const noexcept;
    size_t size() const noexcept;
    bool is_open() const noexcept;
    bool is_mapped() const noexcept;
    bool is_decompressed() const noexcept;

private:
    MappedFile source_;
    std::vector<uint8_t> decompressed_;
    const uint8_t* data_;
    size_t size_;
    bool decompressed_input_;
};

bool read_file_bytes(const std::string& path, std::vector<uint8_t>* out) noexcept;
bool decode_gzip_if_needed(const std::vector<uint8_t>& bytes, std::vector<uint8_t>* out) noexcept;
bool load_ephemeris_file_bytes(const std::string& path, std::vector<uint8_t>* out) noexcept;

bool compile_ephemeris_block_from_file(
    const std::string& path,
    const EphemerisBlockCompileOptions* options,
    StorageEphemerisBlock* out
) noexcept;

bool compile_opm2_ephemeris_block_from_file(
    const std::string& path,
    double jd_tdb_start,
    double jd_tdb_end,
    StorageEphemerisBlock* out
) noexcept;

}  // namespace internal
}  // namespace taiyin

#endif  // TAIYIN_INTERNAL_EPHEMERIS_FILE_LOADER_H
