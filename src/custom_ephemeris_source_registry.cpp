#include "taiyin/internal/custom_ephemeris_source_registry.h"

#include <cmath>
#include <cstring>
#include <limits>
#include <mutex>
#include <new>
#include <string>
#include <unordered_map>

namespace taiyin {
namespace internal {
namespace {

struct CustomEphemerisSource {
    bool file_backed;
    int target_id;
    int center_id;
    int method_id;
    EphemerisFrame frame;
    double jd_tdb_start;
    double jd_tdb_end;
    std::string path;
    void* data;
    size_t bytes;
    EphemerisBlockFileLoadFn load;
    EphemerisPositionFn position;
    EphemerisVelocityFn velocity;
    EphemerisAccelerationFn acceleration;
    EphemerisBlockCloneFn clone;
    EphemerisBlockDestroyFn destroy;
    uint32_t generation;

    CustomEphemerisSource()
        : file_backed(false),
          target_id(0),
          center_id(0),
          method_id(0),
          frame(EphemerisFrame::FrameUnknown),
          jd_tdb_start(0.0),
          jd_tdb_end(0.0),
          path(),
          data(0),
          bytes(0),
          load(0),
          position(0),
          velocity(0),
          acceleration(0),
          clone(0),
          destroy(0),
          generation(TAIYIN_CUSTOM_EPHEMERIS_GENERATION) {}
};

struct CustomEphemerisRegistry {
    std::mutex mutex;
    uint64_t next_id;
    std::unordered_map<uint64_t, CustomEphemerisSource> sources;

    CustomEphemerisRegistry()
        : mutex(), next_id(1), sources() {}
};

CustomEphemerisRegistry& registry() noexcept {
    static CustomEphemerisRegistry instance;
    return instance;
}

void default_byte_destroy(void* data) noexcept {
    uint8_t* bytes = static_cast<uint8_t*>(data);
    delete[] bytes;
}

bool default_byte_clone(
    const void* source_data,
    size_t source_bytes,
    void** out_data,
    size_t* out_bytes
) noexcept {
    if (out_data) {
        *out_data = 0;
    }
    if (out_bytes) {
        *out_bytes = 0;
    }
    if (!source_data || source_bytes == 0 || !out_data || !out_bytes) {
        return false;
    }

    uint8_t* copy = new (std::nothrow) uint8_t[source_bytes];
    if (!copy) {
        return false;
    }
    std::memcpy(copy, source_data, source_bytes);
    *out_data = copy;
    *out_bytes = source_bytes;
    return true;
}

bool validate_common(
    int target_id,
    int center_id,
    int method_id,
    EphemerisFrame frame,
    double jd_tdb_start,
    double jd_tdb_end,
    EphemerisPositionFn position
) noexcept {
    return target_id != 0
        && center_id >= 0
        && method_id != 0
        && frame != EphemerisFrame::FrameUnknown
        && std::isfinite(jd_tdb_start)
        && std::isfinite(jd_tdb_end)
        && jd_tdb_end > jd_tdb_start
        && position != 0;
}

bool validate_definition(const CustomEphemerisSourceDefinition& definition) noexcept {
    return validate_common(
            definition.target_id,
            definition.center_id,
            definition.method_id,
            definition.frame,
            definition.jd_tdb_start,
            definition.jd_tdb_end,
            definition.position)
        && definition.data != 0
        && definition.bytes > 0
        && (definition.clone != 0 || definition.destroy == 0)
        && (definition.clone == 0 || definition.destroy != 0);
}

bool validate_file_definition(const CustomEphemerisFileSourceDefinition& definition) noexcept {
    return validate_common(
            definition.target_id,
            definition.center_id,
            definition.method_id,
            definition.frame,
            definition.jd_tdb_start,
            definition.jd_tdb_end,
            definition.position)
        && definition.path != 0
        && definition.path[0] != '\0'
        && definition.load != 0
        && definition.destroy != 0;
}

void destroy_source(CustomEphemerisSource* source) noexcept {
    if (!source) {
        return;
    }
    if (source->data && source->destroy) {
        source->destroy(source->data);
    }
    *source = CustomEphemerisSource();
}

bool make_descriptor_from_source(
    uint64_t source_id,
    const CustomEphemerisSource& source,
    EphemerisBlockDescriptor* out
) noexcept {
    if (!out
        || (!source.file_backed && !source.data)
        || (source.file_backed && (source.path.empty() || !source.load))
        || !source.position
        || source.method_id == 0
        || source.frame == EphemerisFrame::FrameUnknown
        || source.jd_tdb_end <= source.jd_tdb_start
        || source_id == 0
        || source_id > static_cast<uint64_t>(std::numeric_limits<int>::max())) {
        return false;
    }

    EphemerisBlockDescriptor descriptor;
    descriptor.route_key = EphemerisRouteKey(
        source.target_id,
        source.center_id,
        source.method_id,
        static_cast<int>(source_id));
    descriptor.source_key = EphemerisBlockKey(
        TAIYIN_CUSTOM_EPHEMERIS_SOURCE_ID,
        source_id,
        source.generation,
        TAIYIN_CUSTOM_EPHEMERIS_PURPOSE);
    descriptor.target_id = source.target_id;
    descriptor.center_id = source.center_id;
    descriptor.method_id = source.method_id;
    descriptor.frame = source.frame;
    descriptor.format = EphemerisBlockFormat::Custom;
    descriptor.jd_tdb_start = source.jd_tdb_start;
    descriptor.jd_tdb_end = source.jd_tdb_end;
    descriptor.path = source.file_backed ? source.path : std::string();
    *out = descriptor;
    return true;
}

bool descriptor_matches_source(
    const EphemerisBlockDescriptor& descriptor,
    const CustomEphemerisSource& source
) noexcept {
    return descriptor.source_key.source_id == TAIYIN_CUSTOM_EPHEMERIS_SOURCE_ID
        && descriptor.source_key.generation == source.generation
        && descriptor.target_id == source.target_id
        && descriptor.center_id == source.center_id
        && descriptor.method_id == source.method_id
        && descriptor.frame == source.frame
        && source.jd_tdb_start <= descriptor.jd_tdb_start
        && source.jd_tdb_end >= descriptor.jd_tdb_end
        && ((!source.file_backed && descriptor.path.empty())
            || (source.file_backed && descriptor.path == source.path));
}

bool finish_storage_block(
    void* cache_data,
    size_t cache_bytes,
    int target_id,
    EphemerisPositionFn position,
    EphemerisVelocityFn velocity,
    EphemerisAccelerationFn acceleration,
    EphemerisBlockDestroyFn destroy,
    StorageEphemerisBlock* out
) noexcept {
    if (!cache_data || cache_bytes == 0 || !position || !destroy || !out) {
        return false;
    }

    *out = StorageEphemerisBlock();
    try {
        out->format = EphemerisBlockFormat::Custom;
        out->position = position;
        out->velocity = velocity;
        out->acceleration = acceleration;
        out->destroy_element = destroy;
        out->data_vector.push_back(cache_data);
        out->id_to_index[target_id] = 0;
        out->total_bytes = cache_bytes;
        return true;
    } catch (...) {
        destroy(cache_data);
        *out = StorageEphemerisBlock();
        return false;
    }
}

}  // namespace

bool register_custom_ephemeris_source(
    const CustomEphemerisSourceDefinition& definition,
    uint64_t* out_source_id
) noexcept {
    if (out_source_id) {
        *out_source_id = 0;
    }
    if (!validate_definition(definition)) {
        return false;
    }

    EphemerisBlockCloneFn clone = definition.clone ? definition.clone : default_byte_clone;
    EphemerisBlockDestroyFn destroy = definition.destroy ? definition.destroy : default_byte_destroy;

    void* source_data = 0;
    size_t source_bytes = 0;
    if (!clone(definition.data, definition.bytes, &source_data, &source_bytes)
        || !source_data
        || source_bytes == 0) {
        if (source_data && destroy) {
            destroy(source_data);
        }
        return false;
    }

    try {
        CustomEphemerisSource source;
        source.file_backed = false;
        source.target_id = definition.target_id;
        source.center_id = definition.center_id;
        source.method_id = definition.method_id;
        source.frame = definition.frame;
        source.jd_tdb_start = definition.jd_tdb_start;
        source.jd_tdb_end = definition.jd_tdb_end;
        source.data = source_data;
        source.bytes = source_bytes;
        source.position = definition.position;
        source.velocity = definition.velocity;
        source.acceleration = definition.acceleration;
        source.clone = clone;
        source.destroy = destroy;
        source.generation = TAIYIN_CUSTOM_EPHEMERIS_GENERATION;

        CustomEphemerisRegistry& reg = registry();
        std::lock_guard<std::mutex> lock(reg.mutex);
        if (reg.next_id == 0 || reg.next_id > static_cast<uint64_t>(std::numeric_limits<int>::max())) {
            destroy_source(&source);
            return false;
        }
        const uint64_t id = reg.next_id++;
        reg.sources[id] = source;
        if (out_source_id) {
            *out_source_id = id;
        }
        return true;
    } catch (...) {
        CustomEphemerisSource source;
        source.data = source_data;
        source.destroy = destroy;
        destroy_source(&source);
        return false;
    }
}

bool register_custom_ephemeris_file_source(
    const CustomEphemerisFileSourceDefinition& definition,
    uint64_t* out_source_id
) noexcept {
    if (out_source_id) {
        *out_source_id = 0;
    }
    if (!validate_file_definition(definition)) {
        return false;
    }

    try {
        CustomEphemerisSource source;
        source.file_backed = true;
        source.target_id = definition.target_id;
        source.center_id = definition.center_id;
        source.method_id = definition.method_id;
        source.frame = definition.frame;
        source.jd_tdb_start = definition.jd_tdb_start;
        source.jd_tdb_end = definition.jd_tdb_end;
        source.path = definition.path;
        source.load = definition.load;
        source.position = definition.position;
        source.velocity = definition.velocity;
        source.acceleration = definition.acceleration;
        source.destroy = definition.destroy;
        source.generation = TAIYIN_CUSTOM_EPHEMERIS_GENERATION;

        CustomEphemerisRegistry& reg = registry();
        std::lock_guard<std::mutex> lock(reg.mutex);
        if (reg.next_id == 0 || reg.next_id > static_cast<uint64_t>(std::numeric_limits<int>::max())) {
            return false;
        }
        const uint64_t id = reg.next_id++;
        reg.sources[id] = source;
        if (out_source_id) {
            *out_source_id = id;
        }
        return true;
    } catch (...) {
        return false;
    }
}

bool unregister_custom_ephemeris_source(uint64_t source_id) noexcept {
    CustomEphemerisRegistry& reg = registry();
    std::lock_guard<std::mutex> lock(reg.mutex);
    std::unordered_map<uint64_t, CustomEphemerisSource>::iterator it = reg.sources.find(source_id);
    if (it == reg.sources.end()) {
        return false;
    }
    destroy_source(&it->second);
    reg.sources.erase(it);
    return true;
}

void clear_custom_ephemeris_sources() noexcept {
    CustomEphemerisRegistry& reg = registry();
    std::lock_guard<std::mutex> lock(reg.mutex);
    for (std::unordered_map<uint64_t, CustomEphemerisSource>::iterator it = reg.sources.begin();
         it != reg.sources.end();
         ++it) {
        destroy_source(&it->second);
    }
    reg.sources.clear();
    reg.next_id = 1;
}

bool make_custom_ephemeris_descriptor(
    uint64_t source_id,
    EphemerisBlockDescriptor* out
) noexcept {
    if (out) {
        *out = EphemerisBlockDescriptor();
    }
    if (!out) {
        return false;
    }

    CustomEphemerisRegistry& reg = registry();
    std::lock_guard<std::mutex> lock(reg.mutex);
    std::unordered_map<uint64_t, CustomEphemerisSource>::const_iterator it = reg.sources.find(source_id);
    if (it == reg.sources.end()) {
        return false;
    }
    return make_descriptor_from_source(source_id, it->second, out);
}

bool compile_custom_ephemeris_source(
    const EphemerisBlockDescriptor& descriptor,
    StorageEphemerisBlock* out
) noexcept {
    if (!out
        || descriptor.format != EphemerisBlockFormat::Custom
        || descriptor.source_key.source_id != TAIYIN_CUSTOM_EPHEMERIS_SOURCE_ID) {
        return false;
    }

    bool file_backed = false;
    std::string path;
    void* source_data = 0;
    size_t source_bytes = 0;
    EphemerisBlockCloneFn clone = 0;
    EphemerisBlockFileLoadFn load = 0;
    EphemerisPositionFn position = 0;
    EphemerisVelocityFn velocity = 0;
    EphemerisAccelerationFn acceleration = 0;
    EphemerisBlockDestroyFn destroy = 0;
    int target_id = 0;

    {
        CustomEphemerisRegistry& reg = registry();
        std::lock_guard<std::mutex> lock(reg.mutex);
        std::unordered_map<uint64_t, CustomEphemerisSource>::const_iterator it =
            reg.sources.find(descriptor.source_key.block_id);
        if (it == reg.sources.end()
            || !descriptor_matches_source(descriptor, it->second)
            || !it->second.destroy
            || !it->second.position) {
            return false;
        }

        file_backed = it->second.file_backed;
        path = it->second.path;
        source_data = it->second.data;
        source_bytes = it->second.bytes;
        clone = it->second.clone;
        load = it->second.load;
        position = it->second.position;
        velocity = it->second.velocity;
        acceleration = it->second.acceleration;
        destroy = it->second.destroy;
        target_id = it->second.target_id;
    }

    void* cache_data = 0;
    size_t cache_bytes = 0;
    if (file_backed) {
        if (!load || path.empty()
            || !load(descriptor.path.c_str(), &cache_data, &cache_bytes)
            || !cache_data
            || cache_bytes == 0) {
            if (cache_data && destroy) {
                destroy(cache_data);
            }
            return false;
        }
    } else {
        if (!clone
            || !source_data
            || source_bytes == 0
            || !clone(source_data, source_bytes, &cache_data, &cache_bytes)
            || !cache_data
            || cache_bytes == 0) {
            if (cache_data && destroy) {
                destroy(cache_data);
            }
            return false;
        }
    }

    return finish_storage_block(
        cache_data,
        cache_bytes,
        target_id,
        position,
        velocity,
        acceleration,
        destroy,
        out);
}

}  // namespace internal
}  // namespace taiyin
