#ifndef TAIYIN_INTERNAL_EPHEMERIS_BLOCK_H
#define TAIYIN_INTERNAL_EPHEMERIS_BLOCK_H

#include "../state.h"
#include <cstddef>
#include <cstdint>
#include <vector>
#include <string>
#include <unordered_map>

namespace taiyin {
namespace internal {

typedef bool (*CartesianStateEvalFn)(
    double jd_tdb,
    const void* data,
    CartesianState* out
);

typedef bool (*EphemerisPositionFn)(
    double jd_tdb,
    const void* data,
    Vector3* out_position_au
);

typedef bool (*EphemerisVelocityFn)(
    double jd_tdb,
    const void* data,
    Vector3* out_velocity_au_per_day
);

typedef bool (*EphemerisAccelerationFn)(
    double jd_tdb,
    const void* data,
    Vector3* out_acceleration_au_per_day2
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
    FormatUnknown,
    Opm4,
    Spk,
    Kepler,
    Moshier,
    FixedStar,
    Tsc1,
    Tkc1,
    Custom,
};

struct CompiledEphemerisBlock {
    const void* data;
    size_t bytes;
    EphemerisPositionFn position;
    EphemerisVelocityFn velocity;
    EphemerisAccelerationFn acceleration;
    EphemerisBlockFormat format;

    CompiledEphemerisBlock()
        : data(0),
          bytes(0),
          position(0),
          velocity(0),
          acceleration(0),
          format(EphemerisBlockFormat::FormatUnknown) {}
};

struct StorageEphemerisBlock {
    int cache_id;
    EphemerisBlockFormat format;
    EphemerisPositionFn position;
    EphemerisVelocityFn velocity;
    EphemerisAccelerationFn acceleration;
    std::vector<void*> data_vector;
    std::unordered_map<int, size_t> id_to_index;
    size_t total_bytes;
    EphemerisBlockDestroyFn destroy_element;

    StorageEphemerisBlock()
        : cache_id(0),
          format(EphemerisBlockFormat::FormatUnknown),
          position(0),
          velocity(0),
          acceleration(0),
          total_bytes(0),
          destroy_element(0) {}
};

int register_celestial_body(const std::string& name) noexcept;
void register_celestial_body_alias(const std::string& alias, int id) noexcept;
bool query_celestial_body_id(const std::string& name, int* out_id) noexcept;
std::string query_celestial_body_name(int id) noexcept;
void destroy_storage_ephemeris_block(StorageEphemerisBlock* storage) noexcept;
bool get_compiled_block_from_storage(const StorageEphemerisBlock* storage, int target_id, CompiledEphemerisBlock* out) noexcept;

struct EphemerisBlockCacheKey {
    int target_id;
    int center_id;
    int source_id;

    EphemerisBlockCacheKey()
        : target_id(0),
          center_id(0),
          source_id(0) {}

    EphemerisBlockCacheKey(int target, int center, int source)
        : target_id(target),
          center_id(center),
          source_id(source) {}
};

struct EphemerisBlockCacheMetadata {
    EphemerisBlockCacheKey key;
    double jd_tdb_start;
    double jd_tdb_end;
    int priority;
    size_t bytes;

    EphemerisBlockCacheMetadata()
        : key(),
          jd_tdb_start(0.0),
          jd_tdb_end(0.0),
          priority(0),
          bytes(0) {}
};

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
    EphemerisAccelerationFn acceleration,
    CompiledEphemerisBlock* out
) noexcept;

bool make_compiled_ephemeris_block_from_state_eval(
    const void* data,
    size_t bytes,
    CartesianStateEvalFn eval,
    uint32_t available_components,
    CompiledEphemerisBlock* out
) noexcept;

bool make_ephemeris_block_cache_metadata(
    const EphemerisBlockCacheKey& key,
    double jd_tdb_start,
    double jd_tdb_end,
    int priority,
    const CompiledEphemerisBlock* block,
    EphemerisBlockCacheMetadata* out
) noexcept;

bool compile_ephemeris_block(
    const void* bytes,
    size_t byte_count,
    const EphemerisBlockCompileOptions* options,
    StorageEphemerisBlock* out
) noexcept;

bool eval_compiled_ephemeris_block(
    double jd_tdb,
    const CompiledEphemerisBlock* block,
    CartesianState* out
) noexcept;

bool eval_compiled_ephemeris_block_position(
    double jd_tdb,
    const CompiledEphemerisBlock* block,
    Vector3* out
) noexcept;

bool eval_compiled_ephemeris_block_velocity(
    double jd_tdb,
    const CompiledEphemerisBlock* block,
    Vector3* out
) noexcept;

bool eval_compiled_ephemeris_block_acceleration(
    double jd_tdb,
    const CompiledEphemerisBlock* block,
    Vector3* out
) noexcept;

}  // namespace internal
}  // namespace taiyin

#endif  // TAIYIN_INTERNAL_EPHEMERIS_BLOCK_H
