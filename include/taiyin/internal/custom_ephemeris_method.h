#ifndef TAIYIN_INTERNAL_CUSTOM_EPHEMERIS_METHOD_H
#define TAIYIN_INTERNAL_CUSTOM_EPHEMERIS_METHOD_H

#include "ephemeris_block.h"
#include "ephemeris_catalog.h"

namespace taiyin {
namespace internal {

typedef bool (*CustomEphemerisFileLoadFn)(
    const char* path,
    void** out_data,
    size_t* out_bytes
);

struct CustomEphemerisMethodDefinition {
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
    const char* description;

    CustomEphemerisMethodDefinition() noexcept
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
          destroy(0),
          description(0) {}
};

struct CustomEphemerisFileMethodDefinition {
    int target_id;
    int center_id;
    int method_id;
    EphemerisFrame frame;
    double jd_tdb_start;
    double jd_tdb_end;
    const char* path;
    CustomEphemerisFileLoadFn load;
    EphemerisPositionFn position;
    EphemerisVelocityFn velocity;
    EphemerisAccelerationFn acceleration;
    EphemerisBlockDestroyFn destroy;
    const char* description;

    CustomEphemerisFileMethodDefinition() noexcept
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
          destroy(0),
          description(0) {}
};

bool register_custom_ephemeris_method(
    const CustomEphemerisMethodDefinition& definition,
    EphemerisBlockDescriptor* out_descriptor
) noexcept;

bool register_custom_ephemeris_file_method(
    const CustomEphemerisFileMethodDefinition& definition,
    EphemerisBlockDescriptor* out_descriptor
) noexcept;

bool load_custom_ephemeris_method_block(
    const EphemerisBlockDescriptor& descriptor,
    StorageEphemerisBlock* out
) noexcept;

void clear_custom_ephemeris_methods() noexcept;

}  // namespace internal
}  // namespace taiyin

#endif  // TAIYIN_INTERNAL_CUSTOM_EPHEMERIS_METHOD_H
