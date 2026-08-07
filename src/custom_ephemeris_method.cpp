#include "taiyin/internal/custom_ephemeris_method.h"

#include <cstring>
#include <mutex>
#include <new>
#include <string>
#include <unordered_map>

namespace taiyin {
namespace internal {
namespace {

const uint64_t CUSTOM_METHOD_SOURCE_ID = 0x4353544d45544844ull;  // "CSTMETHOD"
const uint32_t CUSTOM_METHOD_GENERATION = 1;
const uint32_t CUSTOM_METHOD_PURPOSE = 0;

struct CustomMethodEntry {
    uint64_t id;
    int target_id;
    int center_id;
    int method_id;
    EphemerisFrame frame;
    double jd_tdb_start;
    double jd_tdb_end;
    void* data;
    size_t bytes;
    bool file_backed;
    std::string path;
    CustomEphemerisFileLoadFn load;
    EphemerisPositionFn position;
    EphemerisVelocityFn velocity;
    EphemerisAccelerationFn acceleration;
    EphemerisBlockCloneFn clone;
    EphemerisBlockDestroyFn destroy;

    CustomMethodEntry() noexcept
        : id(0),
          target_id(0),
          center_id(0),
          method_id(0),
          frame(EphemerisFrame::FrameUnknown),
          jd_tdb_start(0.0),
          jd_tdb_end(0.0),
          data(0),
          bytes(0),
          file_backed(false),
          path(),
          load(0),
          position(0),
          velocity(0),
          acceleration(0),
          clone(0),
          destroy(0) {}
};

struct CustomMethodTable {
    std::mutex mutex;
    uint64_t next_id;
    std::unordered_map<uint64_t, CustomMethodEntry> entries;

    CustomMethodTable()
        : mutex(),
          next_id(1),
          entries() {}
};

CustomMethodTable& custom_method_table() noexcept {
    static CustomMethodTable table;
    return table;
}

void raw_destroy(void* data) {
    ::operator delete(data);
}

bool raw_clone(
    const void* source_data,
    size_t source_bytes,
    void** out_data,
    size_t* out_bytes
) {
    if (out_data) {
        *out_data = 0;
    }
    if (out_bytes) {
        *out_bytes = 0;
    }
    if (!source_data || source_bytes == 0 || !out_data || !out_bytes) {
        return false;
    }

    void* clone = ::operator new(source_bytes, std::nothrow);
    if (!clone) {
        return false;
    }
    std::memcpy(clone, source_data, source_bytes);
    *out_data = clone;
    *out_bytes = source_bytes;
    return true;
}

bool clone_entry_data(
    const CustomMethodEntry& entry,
    void** out_data,
    size_t* out_bytes
) noexcept {
    if (entry.file_backed) {
        if (!entry.load || entry.path.empty()) {
            return false;
        }
        return entry.load(entry.path.c_str(), out_data, out_bytes);
    }
    if (entry.clone) {
        return entry.clone(entry.data, entry.bytes, out_data, out_bytes);
    }
    return raw_clone(entry.data, entry.bytes, out_data, out_bytes);
}

bool clone_definition_data(
    const CustomEphemerisMethodDefinition& definition,
    void** out_data,
    size_t* out_bytes
) noexcept {
    if (definition.clone) {
        return definition.clone(definition.data, definition.bytes, out_data, out_bytes);
    }
    return raw_clone(definition.data, definition.bytes, out_data, out_bytes);
}

void destroy_entry_data(const CustomMethodEntry& entry, void* data) noexcept {
    if (!data) {
        return;
    }
    if (entry.destroy) {
        entry.destroy(data);
    } else {
        raw_destroy(data);
    }
}

bool validate_definition(const CustomEphemerisMethodDefinition& definition) noexcept {
    if (definition.target_id == 0
        || definition.method_id == 0
        || definition.frame == EphemerisFrame::FrameUnknown
        || definition.jd_tdb_end <= definition.jd_tdb_start
        || !definition.data
        || definition.bytes == 0
        || !definition.position) {
        return false;
    }
    if (definition.destroy && !definition.clone) {
        return false;
    }
    return true;
}

bool validate_file_definition(const CustomEphemerisFileMethodDefinition& definition) noexcept {
    if (definition.target_id == 0
        || definition.method_id == 0
        || definition.frame == EphemerisFrame::FrameUnknown
        || definition.jd_tdb_end <= definition.jd_tdb_start
        || !definition.path
        || definition.path[0] == '\0'
        || !definition.load
        || !definition.position
        || !definition.destroy) {
        return false;
    }
    return true;
}

EphemerisBlockDescriptor make_descriptor(
    const CustomMethodEntry& entry
) noexcept {
    EphemerisBlockDescriptor descriptor;
    descriptor.route_key = EphemerisRouteKey(
        entry.target_id,
        entry.center_id,
        entry.method_id,
        0);
    descriptor.source_key = EphemerisBlockKey(
        CUSTOM_METHOD_SOURCE_ID,
        entry.id,
        CUSTOM_METHOD_GENERATION,
        CUSTOM_METHOD_PURPOSE);
    descriptor.target_id = entry.target_id;
    descriptor.center_id = entry.center_id;
    descriptor.method_id = entry.method_id;
    descriptor.frame = entry.frame;
    descriptor.format = EphemerisBlockFormat::Custom;
    descriptor.jd_tdb_start = entry.jd_tdb_start;
    descriptor.jd_tdb_end = entry.jd_tdb_end;
    descriptor.path = entry.path;
    descriptor.cache_policy.kind = CacheWholeEntry;
    return descriptor;
}

}  // namespace

bool register_custom_ephemeris_method(
    const CustomEphemerisMethodDefinition& definition,
    EphemerisBlockDescriptor* out_descriptor
) noexcept {
    if (out_descriptor) {
        *out_descriptor = EphemerisBlockDescriptor();
    }
    if (!out_descriptor || !validate_definition(definition)) {
        return false;
    }

    void* data = 0;
    size_t bytes = 0;
    if (!clone_definition_data(definition, &data, &bytes) || !data || bytes == 0) {
        return false;
    }

    CustomMethodEntry entry;
    entry.target_id = definition.target_id;
    entry.center_id = definition.center_id;
    entry.method_id = definition.method_id;
    entry.frame = definition.frame;
    entry.jd_tdb_start = definition.jd_tdb_start;
    entry.jd_tdb_end = definition.jd_tdb_end;
    entry.data = data;
    entry.bytes = bytes;
    entry.file_backed = false;
    entry.position = definition.position;
    entry.velocity = definition.velocity;
    entry.acceleration = definition.acceleration;
    entry.clone = definition.clone;
    entry.destroy = definition.destroy;

    CustomMethodTable& table = custom_method_table();
    std::lock_guard<std::mutex> lock(table.mutex);
    try {
        entry.id = table.next_id++;
        table.entries[entry.id] = entry;
        *out_descriptor = make_descriptor(entry);
    } catch (...) {
        destroy_entry_data(entry, data);
        return false;
    }
    return true;
}

bool register_custom_ephemeris_file_method(
    const CustomEphemerisFileMethodDefinition& definition,
    EphemerisBlockDescriptor* out_descriptor
) noexcept {
    if (out_descriptor) {
        *out_descriptor = EphemerisBlockDescriptor();
    }
    if (!out_descriptor || !validate_file_definition(definition)) {
        return false;
    }

    CustomMethodEntry entry;
    entry.target_id = definition.target_id;
    entry.center_id = definition.center_id;
    entry.method_id = definition.method_id;
    entry.frame = definition.frame;
    entry.jd_tdb_start = definition.jd_tdb_start;
    entry.jd_tdb_end = definition.jd_tdb_end;
    entry.file_backed = true;
    entry.path = definition.path;
    entry.load = definition.load;
    entry.position = definition.position;
    entry.velocity = definition.velocity;
    entry.acceleration = definition.acceleration;
    entry.destroy = definition.destroy;

    CustomMethodTable& table = custom_method_table();
    std::lock_guard<std::mutex> lock(table.mutex);
    try {
        entry.id = table.next_id++;
        table.entries[entry.id] = entry;
        *out_descriptor = make_descriptor(entry);
    } catch (...) {
        return false;
    }
    return true;
}

bool load_custom_ephemeris_method_block(
    const EphemerisBlockDescriptor& descriptor,
    StorageEphemerisBlock* out
) noexcept {
    if (!out || descriptor.format != EphemerisBlockFormat::Custom) {
        return false;
    }
    if (descriptor.source_key.source_id != CUSTOM_METHOD_SOURCE_ID
        || descriptor.source_key.generation != CUSTOM_METHOD_GENERATION
        || descriptor.source_key.purpose != CUSTOM_METHOD_PURPOSE) {
        return false;
    }

    CustomMethodEntry entry;
    {
        CustomMethodTable& table = custom_method_table();
        std::lock_guard<std::mutex> lock(table.mutex);
        std::unordered_map<uint64_t, CustomMethodEntry>::const_iterator it =
            table.entries.find(descriptor.source_key.block_id);
        if (it == table.entries.end()) {
            return false;
        }
        entry = it->second;
    }

    if (entry.target_id != descriptor.target_id
        || entry.center_id != descriptor.center_id
        || entry.method_id != descriptor.method_id
        || entry.frame != descriptor.frame) {
        return false;
    }

    void* data = 0;
    size_t bytes = 0;
    if (!clone_entry_data(entry, &data, &bytes) || !data || bytes == 0) {
        return false;
    }

    *out = StorageEphemerisBlock();
    out->format = EphemerisBlockFormat::Custom;
    out->position = entry.position;
    out->velocity = entry.velocity;
    out->acceleration = entry.acceleration;
    out->destroy_element = entry.destroy ? entry.destroy : raw_destroy;
    try {
        out->data_vector.push_back(data);
    } catch (...) {
        destroy_entry_data(entry, data);
        *out = StorageEphemerisBlock();
        return false;
    }
    out->total_bytes = bytes;
    return true;
}

void clear_custom_ephemeris_methods() noexcept {
    CustomMethodTable& table = custom_method_table();
    std::lock_guard<std::mutex> lock(table.mutex);
    for (std::unordered_map<uint64_t, CustomMethodEntry>::iterator it = table.entries.begin();
         it != table.entries.end();
         ++it) {
        if (!it->second.file_backed) {
            destroy_entry_data(it->second, it->second.data);
        }
    }
    table.entries.clear();
    table.next_id = 1;
}

}  // namespace internal
}  // namespace taiyin
