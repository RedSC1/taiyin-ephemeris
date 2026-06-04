#ifndef TAIYIN_INTERNAL_BUILTIN_LOADER_H
#define TAIYIN_INTERNAL_BUILTIN_LOADER_H

#include "eop.h"

#include <cstdint>
#include <vector>

namespace taiyin {
namespace internal {

bool decode_builtin_gzip_base64(const char* base64, std::vector<uint8_t>* out) noexcept;

bool load_builtin_eop_table(EarthOrientationTable* out) noexcept;

}  // namespace internal
}  // namespace taiyin

#endif  // TAIYIN_INTERNAL_BUILTIN_LOADER_H
