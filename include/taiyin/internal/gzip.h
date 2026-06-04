#ifndef TAIYIN_INTERNAL_GZIP_H
#define TAIYIN_INTERNAL_GZIP_H

#include <cstdint>
#include <vector>

namespace taiyin {
namespace internal {

bool gzip_decompress(const uint8_t* data, int size, std::vector<uint8_t>* out);

}  // namespace internal
}  // namespace taiyin

#endif
