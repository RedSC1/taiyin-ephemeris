#include "taiyin/internal/gzip.h"

#include "miniz.h"

#include <cstring>

namespace taiyin {
namespace internal {
namespace {

uint16_t read_u16_le(const uint8_t* data) {
    return static_cast<uint16_t>(data[0]) | (static_cast<uint16_t>(data[1]) << 8);
}

uint32_t read_u32_le(const uint8_t* data) {
    return static_cast<uint32_t>(data[0])
        | (static_cast<uint32_t>(data[1]) << 8)
        | (static_cast<uint32_t>(data[2]) << 16)
        | (static_cast<uint32_t>(data[3]) << 24);
}

bool skip_zero_terminated(const uint8_t* data, int size, int* offset) {
    while (*offset < size) {
        if (data[*offset] == 0) {
            ++(*offset);
            return true;
        }
        ++(*offset);
    }
    return false;
}

bool parse_gzip_header(const uint8_t* data, int size, int* deflate_offset) {
    if (!data || size < 18 || !deflate_offset) {
        return false;
    }
    if (data[0] != 0x1F || data[1] != 0x8B || data[2] != 8) {
        return false;
    }

    const uint8_t flags = data[3];
    if ((flags & 0xE0) != 0) {
        return false;
    }

    int offset = 10;
    if (flags & 0x04) {
        if (offset + 2 > size - 8) return false;
        const int extra_len = read_u16_le(data + offset);
        offset += 2;
        if (extra_len < 0 || offset + extra_len > size - 8) return false;
        offset += extra_len;
    }
    if (flags & 0x08) {
        if (!skip_zero_terminated(data, size - 8, &offset)) return false;
    }
    if (flags & 0x10) {
        if (!skip_zero_terminated(data, size - 8, &offset)) return false;
    }
    if (flags & 0x02) {
        if (offset + 2 > size - 8) return false;
        offset += 2;
    }

    if (offset >= size - 8) {
        return false;
    }

    *deflate_offset = offset;
    return true;
}

}  // namespace

bool gzip_decompress(const uint8_t* data, int size, std::vector<uint8_t>* out) {
    if (!data || size < 0 || !out) {
        return false;
    }

    int deflate_offset = 0;
    if (!parse_gzip_header(data, size, &deflate_offset)) {
        return false;
    }

    const int footer_offset = size - 8;
    const uint32_t expected_crc = read_u32_le(data + footer_offset);
    const uint32_t expected_size = read_u32_le(data + footer_offset + 4);

    std::vector<uint8_t> result;
    result.resize(expected_size == 0 ? 1 : expected_size);

    mz_stream stream;
    std::memset(&stream, 0, sizeof(stream));
    if (mz_inflateInit2(&stream, -MZ_DEFAULT_WINDOW_BITS) != MZ_OK) {
        return false;
    }

    stream.next_in = const_cast<unsigned char*>(reinterpret_cast<const unsigned char*>(data + deflate_offset));
    stream.avail_in = static_cast<unsigned int>(footer_offset - deflate_offset);

    int status = MZ_OK;
    do {
        if (stream.total_out == result.size()) {
            result.resize(result.size() * 2 + 1);
        }
        stream.next_out = reinterpret_cast<unsigned char*>(&result[0] + stream.total_out);
        stream.avail_out = static_cast<unsigned int>(result.size() - stream.total_out);
        status = mz_inflate(&stream, MZ_FINISH);
    } while (status == MZ_BUF_ERROR || (status == MZ_OK && stream.avail_out == 0));

    const size_t total_out = stream.total_out;
    mz_inflateEnd(&stream);

    if (status != MZ_STREAM_END) {
        return false;
    }
    if ((total_out & 0xFFFFFFFFu) != expected_size) {
        return false;
    }

    result.resize(total_out);
    const mz_ulong actual_crc = mz_crc32(mz_crc32(0, 0, 0), result.empty() ? 0 : &result[0], result.size());
    if (static_cast<uint32_t>(actual_crc) != expected_crc) {
        return false;
    }

    *out = result;
    return true;
}

}  // namespace internal
}  // namespace taiyin
