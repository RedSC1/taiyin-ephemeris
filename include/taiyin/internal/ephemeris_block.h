#ifndef TAIYIN_INTERNAL_EPHEMERIS_BLOCK_H
#define TAIYIN_INTERNAL_EPHEMERIS_BLOCK_H

#include "../state.h"
#include "../time.h"
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>
#include <string>
#include <unordered_map>

namespace taiyin {
namespace internal {

typedef bool (*EphemerisPositionFn)(
    const SplitJulianDate& jd_tdb,
    const void* data,
    Vector3* out_position_au
);

typedef bool (*EphemerisVelocityFn)(
    const SplitJulianDate& jd_tdb,
    const void* data,
    Vector3* out_velocity_au_per_day
);

typedef bool (*EphemerisPositionVelocityFn)(
    const SplitJulianDate& jd_tdb,
    const void* data,
    Vector3* out_position_au,
    Vector3* out_velocity_au_per_day
);

typedef bool (*EphemerisAccelerationFn)(
    const SplitJulianDate& jd_tdb,
    const void* data,
    Vector3* out_acceleration_au_per_day2
);

typedef bool (*EphemerisStateFn)(
    const SplitJulianDate& jd_tdb,
    const void* data,
    CartesianState* out_state
);

typedef void (*EphemerisBlockDestroyFn)(void*);

typedef bool (*EphemerisBlockCloneFn)(
    const void* source_data,
    size_t source_bytes,
    void** out_data,
    size_t* out_bytes
);

const uint32_t EPHEMERIS_BLOCK_COMPONENT_POSITION = 1u << 0;
const uint32_t EPHEMERIS_BLOCK_COMPONENT_VELOCITY = 1u << 1;
const uint32_t EPHEMERIS_BLOCK_COMPONENT_ACCELERATION = 1u << 2;
const uint32_t EPHEMERIS_BLOCK_COMPONENT_STATE =
    EPHEMERIS_BLOCK_COMPONENT_POSITION
    | EPHEMERIS_BLOCK_COMPONENT_VELOCITY
    | EPHEMERIS_BLOCK_COMPONENT_ACCELERATION;

enum EphemerisBlockFormat {
    FormatUnknown = 0,
    Opm2 = 1,
    Spk = 2,
    Kepler = 3,
    SemiAnalytic = 4,
    FixedStar = 5,
    Tsc1 = 6,
    Tkc1 = 7,
    Custom = 8,
};

struct CompiledEphemerisBlock {
    const void* data;
    size_t bytes;
    EphemerisPositionFn position;
    EphemerisVelocityFn velocity;
    EphemerisPositionVelocityFn position_velocity;
    EphemerisAccelerationFn acceleration;
    EphemerisStateFn state;
    EphemerisBlockFormat format;

    CompiledEphemerisBlock()
        : data(0),
          bytes(0),
          position(0),
          velocity(0),
          position_velocity(0),
          acceleration(0),
          state(0),
          format(EphemerisBlockFormat::FormatUnknown) {}
};

struct StorageEphemerisBlock {
    int cache_id;
    EphemerisBlockFormat format;
    EphemerisPositionFn position;
    EphemerisVelocityFn velocity;
    EphemerisPositionVelocityFn position_velocity;
    EphemerisAccelerationFn acceleration;
    EphemerisStateFn state;
    std::vector<void*> data_vector;
    std::unordered_map<int, size_t> id_to_index;
    std::shared_ptr<void> source_owner;
    size_t total_bytes;
    EphemerisBlockDestroyFn destroy_element;

    StorageEphemerisBlock()
        : cache_id(0),
          format(EphemerisBlockFormat::FormatUnknown),
          position(0),
          velocity(0),
          position_velocity(0),
          acceleration(0),
          state(0),
          source_owner(),
          total_bytes(0),
          destroy_element(0) {}
};

int register_celestial_body(const std::string& name) noexcept;
void register_celestial_body_alias(const std::string& alias, int id) noexcept;
bool query_celestial_body_id(const std::string& name, int* out_id) noexcept;
std::string query_celestial_body_name(int id) noexcept;
void destroy_storage_ephemeris_block(StorageEphemerisBlock* storage) noexcept;
bool get_compiled_block_from_storage(const StorageEphemerisBlock* storage, int target_id, CompiledEphemerisBlock* out) noexcept;

struct EphemerisBlockCompileOptions {
    bool has_required_jd_tdb_range;
    double required_jd_tdb_start;
    double required_jd_tdb_end;

    EphemerisBlockCompileOptions()
        : has_required_jd_tdb_range(false),
          required_jd_tdb_start(0.0),
          required_jd_tdb_end(0.0) {}
};

bool make_compiled_ephemeris_block(
    const void* data,
    size_t bytes,
    EphemerisPositionFn position,
    EphemerisVelocityFn velocity,
    EphemerisPositionVelocityFn position_velocity,
    EphemerisAccelerationFn acceleration,
    CompiledEphemerisBlock* out
) noexcept;

bool compile_ephemeris_block(
    const void* bytes,
    size_t byte_count,
    const EphemerisBlockCompileOptions* options,
    StorageEphemerisBlock* out
) noexcept;

bool eval_compiled_ephemeris_block(
    const SplitJulianDate& jd_tdb,
    const CompiledEphemerisBlock* block,
    CartesianState* out
) noexcept;

bool eval_compiled_ephemeris_block_components(
    const SplitJulianDate& jd_tdb,
    const CompiledEphemerisBlock* block,
    uint32_t components,
    CartesianState* out
) noexcept;

bool eval_compiled_ephemeris_block_position(
    const SplitJulianDate& jd_tdb,
    const CompiledEphemerisBlock* block,
    Vector3* out
) noexcept;

bool eval_compiled_ephemeris_block_velocity(
    const SplitJulianDate& jd_tdb,
    const CompiledEphemerisBlock* block,
    Vector3* out
) noexcept;

bool eval_compiled_ephemeris_block_acceleration(
    const SplitJulianDate& jd_tdb,
    const CompiledEphemerisBlock* block,
    Vector3* out
) noexcept;

}  // namespace internal
}  // namespace taiyin

#endif  // TAIYIN_INTERNAL_EPHEMERIS_BLOCK_H
