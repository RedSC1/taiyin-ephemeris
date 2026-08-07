#ifndef TAIYIN_INTERNAL_DESCRIPTOR_LOADER_H
#define TAIYIN_INTERNAL_DESCRIPTOR_LOADER_H

#include "ephemeris_block.h"
#include "ephemeris_catalog.h"

namespace taiyin {
namespace internal {

bool load_descriptor_ephemeris_block(
    const EphemerisBlockDescriptor& descriptor,
    const EphemerisSourceIndex* source_index,
    StorageEphemerisBlock* out
) noexcept;

bool load_descriptor_ephemeris_block(
    const EphemerisBlockDescriptor& descriptor,
    StorageEphemerisBlock* out
) noexcept;

bool load_descriptor_source_index(
    const EphemerisBlockDescriptor& descriptor,
    EphemerisSourceIndex* out
) noexcept;

}  // namespace internal
}  // namespace taiyin

#endif  // TAIYIN_INTERNAL_DESCRIPTOR_LOADER_H
