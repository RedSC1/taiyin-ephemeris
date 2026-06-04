#ifndef TAIYIN_INTERNAL_DESCRIPTOR_LOADER_H
#define TAIYIN_INTERNAL_DESCRIPTOR_LOADER_H

#include "ephemeris_block.h"
#include "ephemeris_cache.h"
#include "ephemeris_catalog.h"

namespace taiyin {
namespace internal {

bool load_descriptor_ephemeris_block(
    const EphemerisBlockDescriptor& descriptor,
    StorageEphemerisBlock* out
) noexcept;

bool load_descriptor_into_cache(
    const EphemerisBlockDescriptor& descriptor,
    EphemerisBlockCache* cache
) noexcept;

bool load_descriptor_into_cache(
    const EphemerisBlockDescriptor& descriptor,
    EphemerisBlockCache* cache,
    int priority_bias
) noexcept;

bool eval_descriptor_position(
    const EphemerisBlockDescriptor& descriptor,
    EphemerisBlockCache* cache,
    double jd_tdb,
    Vector3* out
) noexcept;

bool eval_descriptor_velocity(
    const EphemerisBlockDescriptor& descriptor,
    EphemerisBlockCache* cache,
    double jd_tdb,
    Vector3* out
) noexcept;

bool eval_descriptor_acceleration(
    const EphemerisBlockDescriptor& descriptor,
    EphemerisBlockCache* cache,
    double jd_tdb,
    Vector3* out
) noexcept;

bool eval_descriptor_state(
    const EphemerisBlockDescriptor& descriptor,
    EphemerisBlockCache* cache,
    double jd_tdb,
    CartesianState* out
) noexcept;

}  // namespace internal
}  // namespace taiyin

#endif  // TAIYIN_INTERNAL_DESCRIPTOR_LOADER_H
