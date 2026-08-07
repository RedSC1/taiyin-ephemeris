#include "taiyin/internal/ephemeris_file_loader.h"

#include "taiyin/internal/gzip.h"

#include <cmath>
#include <fstream>
#include <limits>

namespace taiyin {
namespace internal {
namespace {

bool has_gzip_magic(const uint8_t* bytes, size_t size) noexcept {
    return bytes && size >= 2 && bytes[0] == 0x1f && bytes[1] == 0x8b;
}

}  // namespace

EphemerisFileView::EphemerisFileView() noexcept
    : source_(),
      decompressed_(),
      data_(0),
      size_(0),
      decompressed_input_(false) {}

EphemerisFileView::~EphemerisFileView() noexcept {
    close();
}

bool EphemerisFileView::open_readonly(const std::string& path) noexcept {
    close();
    if (!source_.open_readonly(path) || !source_.is_open()) {
        return false;
    }

    if (!has_gzip_magic(source_.data(), source_.size())) {
        data_ = source_.data();
        size_ = source_.size();
        return true;
    }
    if (source_.size() > static_cast<size_t>(std::numeric_limits<int>::max())) {
        close();
        return false;
    }

    try {
        if (!gzip_decompress(
                source_.data(),
                static_cast<int>(source_.size()),
                &decompressed_)
            || decompressed_.empty()) {
            close();
            return false;
        }
    } catch (...) {
        close();
        return false;
    }

    source_.close();
    data_ = &decompressed_[0];
    size_ = decompressed_.size();
    decompressed_input_ = true;
    return true;
}

void EphemerisFileView::close() noexcept {
    data_ = 0;
    size_ = 0;
    decompressed_input_ = false;
    source_.close();
    std::vector<uint8_t>().swap(decompressed_);
}

const uint8_t* EphemerisFileView::data() const noexcept {
    return data_;
}

size_t EphemerisFileView::size() const noexcept {
    return size_;
}

bool EphemerisFileView::is_open() const noexcept {
    return data_ != 0 && size_ > 0;
}

bool EphemerisFileView::is_mapped() const noexcept {
    return source_.is_mapped();
}

bool EphemerisFileView::is_decompressed() const noexcept {
    return decompressed_input_;
}

bool read_file_bytes(const std::string& path, std::vector<uint8_t>* out) noexcept {
    if (path.empty() || !out) {
        return false;
    }
    std::vector<uint8_t>().swap(*out);

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

    try {
        out->resize(static_cast<size_t>(byte_count));
    } catch (...) {
        std::vector<uint8_t>().swap(*out);
        return false;
    }
    file.seekg(0, std::ios::beg);
    if (!out->empty()) {
        file.read(reinterpret_cast<char*>(&(*out)[0]), static_cast<std::streamsize>(out->size()));
        if (!file) {
            std::vector<uint8_t>().swap(*out);
            return false;
        }
    }

    return true;
}

bool decode_gzip_if_needed(const std::vector<uint8_t>& bytes, std::vector<uint8_t>* out) noexcept {
    if (!out) {
        return false;
    }
    if (!has_gzip_magic(bytes.empty() ? 0 : &bytes[0], bytes.size())) {
        try {
            *out = bytes;
        } catch (...) {
            std::vector<uint8_t>().swap(*out);
            return false;
        }
        return true;
    }
    if (bytes.size() > static_cast<size_t>(std::numeric_limits<int>::max())) {
        return false;
    }
    try {
        return gzip_decompress(
            bytes.empty() ? 0 : &bytes[0],
            static_cast<int>(bytes.size()),
            out);
    } catch (...) {
        std::vector<uint8_t>().swap(*out);
        return false;
    }
}

bool load_ephemeris_file_bytes(const std::string& path, std::vector<uint8_t>* out) noexcept {
    if (!out) {
        return false;
    }
    std::vector<uint8_t> file_bytes;
    return read_file_bytes(path, &file_bytes)
        && decode_gzip_if_needed(file_bytes, out);
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

    EphemerisFileView bytes;
    if (!bytes.open_readonly(path)) {
        return false;
    }
    return compile_ephemeris_block(bytes.data(), bytes.size(), options, out);
}

bool compile_opm2_ephemeris_block_from_file(
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
