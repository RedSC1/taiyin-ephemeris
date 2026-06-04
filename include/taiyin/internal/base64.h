#ifndef TAIYIN_INTERNAL_BASE64_H
#define TAIYIN_INTERNAL_BASE64_H

#include <cstdint>
#include <vector>

namespace taiyin {
namespace internal {

bool base64_decode(const char* text, int size, std::vector<uint8_t>* out);

}  // namespace internal
}  // namespace taiyin

#endif
