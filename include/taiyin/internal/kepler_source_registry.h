#ifndef TAIYIN_INTERNAL_KEPLER_SOURCE_REGISTRY_H
#define TAIYIN_INTERNAL_KEPLER_SOURCE_REGISTRY_H

#include "ephemeris_catalog.h"
#include "kepler.h"

#include <cstddef>
#include <cstdint>

namespace taiyin {
namespace internal {

const int TAIYIN_KEPLER_MEMORY_METHOD_ID = 3002;
const uint64_t TAIYIN_KEPLER_MEMORY_SOURCE_ID = 5;
const uint32_t TAIYIN_KEPLER_MEMORY_PURPOSE = 0;

bool register_memory_kepler_source(
    const KeplerElements* elements,
    size_t element_count,
    int method_id,
    EphemerisFrame frame,
    double jd_tdb_start,
    double jd_tdb_end,
    uint64_t* out_source_id
) noexcept;

bool unregister_memory_kepler_source(uint64_t source_id) noexcept;
void clear_memory_kepler_sources() noexcept;

bool make_memory_kepler_descriptor(
    uint64_t source_id,
    EphemerisBlockDescriptor* out
) noexcept;

bool compile_memory_kepler_source(
    const EphemerisBlockDescriptor& descriptor,
    StorageEphemerisBlock* out
) noexcept;

}  // namespace internal
}  // namespace taiyin

#endif  // TAIYIN_INTERNAL_KEPLER_SOURCE_REGISTRY_H
