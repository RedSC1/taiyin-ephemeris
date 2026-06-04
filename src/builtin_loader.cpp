#include "taiyin/internal/builtin_loader.h"

#include "taiyin/internal/base64.h"
#include "taiyin/internal/eop.h"
#include "taiyin/internal/eop_finals2000a_data.h"
#include "taiyin/internal/gzip.h"

#include <cstring>
#include <limits>
#include <new>

namespace taiyin {
namespace internal {

bool decode_builtin_gzip_base64(const char* base64, std::vector<uint8_t>* out) noexcept {
    if (!base64 || !out) {
        return false;
    }

    const size_t text_size = std::strlen(base64);
    if (text_size > static_cast<size_t>(std::numeric_limits<int>::max())) {
        return false;
    }

    std::vector<uint8_t> compressed;
    if (!base64_decode(base64, static_cast<int>(text_size), &compressed)) {
        return false;
    }
    if (compressed.size() > static_cast<size_t>(std::numeric_limits<int>::max())) {
        return false;
    }

    return gzip_decompress(
        compressed.empty() ? 0 : &compressed[0],
        static_cast<int>(compressed.size()),
        out);
}

bool load_builtin_eop_table(EarthOrientationTable* out) noexcept {
    if (!out) {
        return false;
    }

    std::vector<uint8_t> raw;
    if (!gzip_decompress(
            builtin_finals2000a_all_gz(),
            builtin_finals2000a_all_gz_size(),
            &raw)) {
        return false;
    }
    if (raw.empty()) {
        return false;
    }

    size_t estimated_count = raw.size() / 80;
    if (estimated_count < 1) {
        estimated_count = 1;
    }
    EarthOrientationSample* samples = static_cast<EarthOrientationSample*>(
        ::operator new(estimated_count * sizeof(EarthOrientationSample), std::nothrow));
    if (!samples) {
        return false;
    }

    size_t count = 0;
    if (!parse_finals2000a_table(
            reinterpret_cast<const char*>(&raw[0]),
            static_cast<int>(raw.size()),
            samples,
            estimated_count,
            &count)) {
        ::operator delete(samples);
        return false;
    }

    out->samples = samples;
    out->count = count;
    return true;
}

}  // namespace internal
}  // namespace taiyin
