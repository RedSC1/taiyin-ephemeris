#ifndef TAIYIN_INTERNAL_CUSTOM_EPHEMERIS_SOURCE_REGISTRY_H
#define TAIYIN_INTERNAL_CUSTOM_EPHEMERIS_SOURCE_REGISTRY_H

#include "ephemeris_catalog.h"

#include <cstddef>
#include <cstdint>

namespace taiyin {
namespace internal {

const uint64_t TAIYIN_CUSTOM_EPHEMERIS_SOURCE_ID = 8;
const uint32_t TAIYIN_CUSTOM_EPHEMERIS_GENERATION = 1;
const uint32_t TAIYIN_CUSTOM_EPHEMERIS_PURPOSE = 0;

typedef bool (*EphemerisBlockFileLoadFn)(
    const char* path,
    void** out_data,
    size_t* out_bytes
);

struct CustomEphemerisSourceDefinition {
    int target_id;
    int center_id;
    int method_id;
    EphemerisFrame frame;
    double jd_tdb_start;
    double jd_tdb_end;
    const void* data;
    size_t bytes;
    EphemerisPositionFn position;
    EphemerisVelocityFn velocity;
    EphemerisAccelerationFn acceleration;
    EphemerisBlockCloneFn clone;
    EphemerisBlockDestroyFn destroy;

    CustomEphemerisSourceDefinition()
        : target_id(0),
          center_id(0),
          method_id(0),
          frame(EphemerisFrame::FrameUnknown),
          jd_tdb_start(0.0),
          jd_tdb_end(0.0),
          data(0),
          bytes(0),
          position(0),
          velocity(0),
          acceleration(0),
          clone(0),
          destroy(0) {}
};

struct CustomEphemerisFileSourceDefinition {
    int target_id;
    int center_id;
    int method_id;
    EphemerisFrame frame;
    double jd_tdb_start;
    double jd_tdb_end;
    const char* path;
    EphemerisBlockFileLoadFn load;
    EphemerisPositionFn position;
    EphemerisVelocityFn velocity;
    EphemerisAccelerationFn acceleration;
    EphemerisBlockDestroyFn destroy;

    CustomEphemerisFileSourceDefinition()
        : target_id(0),
          center_id(0),
          method_id(0),
          frame(EphemerisFrame::FrameUnknown),
          jd_tdb_start(0.0),
          jd_tdb_end(0.0),
          path(0),
          load(0),
          position(0),
          velocity(0),
          acceleration(0),
          destroy(0) {}
};

bool register_custom_ephemeris_source(
    const CustomEphemerisSourceDefinition& definition,
    uint64_t* out_source_id
) noexcept;

bool register_custom_ephemeris_file_source(
    const CustomEphemerisFileSourceDefinition& definition,
    uint64_t* out_source_id
) noexcept;

bool unregister_custom_ephemeris_source(uint64_t source_id) noexcept;
void clear_custom_ephemeris_sources() noexcept;

bool make_custom_ephemeris_descriptor(
    uint64_t source_id,
    EphemerisBlockDescriptor* out
) noexcept;

bool compile_custom_ephemeris_source(
    const EphemerisBlockDescriptor& descriptor,
    StorageEphemerisBlock* out
) noexcept;

}  // namespace internal
}  // namespace taiyin

#endif  // TAIYIN_INTERNAL_CUSTOM_EPHEMERIS_SOURCE_REGISTRY_H
