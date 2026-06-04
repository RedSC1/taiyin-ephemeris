#include "taiyin/internal/kepler_source_registry.h"

#include <limits>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace taiyin {
namespace internal {
namespace {

struct MemoryKeplerSource {
    std::vector<KeplerElements> elements;
    int method_id;
    EphemerisFrame frame;
    double jd_tdb_start;
    double jd_tdb_end;
    uint32_t generation;

    MemoryKeplerSource()
        : elements(),
          method_id(0),
          frame(EphemerisFrame::FrameUnknown),
          jd_tdb_start(0.0),
          jd_tdb_end(0.0),
          generation(1) {}
};

struct MemoryKeplerRegistry {
    std::mutex mutex;
    uint64_t next_id;
    std::unordered_map<uint64_t, MemoryKeplerSource> sources;

    MemoryKeplerRegistry()
        : mutex(), next_id(1), sources() {}
};

MemoryKeplerRegistry& registry() noexcept {
    static MemoryKeplerRegistry instance;
    return instance;
}

bool validate_elements(
    const KeplerElements* elements,
    size_t element_count,
    double jd_tdb_start,
    double jd_tdb_end
) noexcept {
    KeplerEphemerisData* data = 0;
    if (!compile_kepler_ephemeris_data(elements, element_count, jd_tdb_start, jd_tdb_end, &data)) {
        return false;
    }
    kepler_ephemeris_data_destroy(data);
    return true;
}

bool make_descriptor_from_source(
    uint64_t source_id,
    const MemoryKeplerSource& source,
    EphemerisBlockDescriptor* out
) noexcept {
    if (!out
        || source.elements.empty()
        || source_id > static_cast<uint64_t>(std::numeric_limits<int>::max())
        || source.method_id == 0
        || source.frame == EphemerisFrame::FrameUnknown
        || source.jd_tdb_end <= source.jd_tdb_start) {
        return false;
    }

    const int target_id = source.elements[0].target_id;
    const int center_id = source.elements[0].center_id;
    EphemerisBlockDescriptor descriptor;
    descriptor.route_key = EphemerisRouteKey(target_id, center_id, source.method_id, static_cast<int>(source_id));
    descriptor.source_key = EphemerisBlockKey(
        TAIYIN_KEPLER_MEMORY_SOURCE_ID,
        source_id,
        source.generation,
        TAIYIN_KEPLER_MEMORY_PURPOSE);
    descriptor.target_id = target_id;
    descriptor.center_id = center_id;
    descriptor.method_id = source.method_id;
    descriptor.frame = source.frame;
    descriptor.format = EphemerisBlockFormat::Kepler;
    descriptor.jd_tdb_start = source.jd_tdb_start;
    descriptor.jd_tdb_end = source.jd_tdb_end;
    descriptor.path.clear();
    *out = descriptor;
    return true;
}

}  // namespace

bool register_memory_kepler_source(
    const KeplerElements* elements,
    size_t element_count,
    int method_id,
    EphemerisFrame frame,
    double jd_tdb_start,
    double jd_tdb_end,
    uint64_t* out_source_id
) noexcept {
    if (out_source_id) {
        *out_source_id = 0;
    }
    if (!elements || element_count == 0 || method_id == 0 || frame == EphemerisFrame::FrameUnknown
        || !validate_elements(elements, element_count, jd_tdb_start, jd_tdb_end)) {
        return false;
    }

    const int target_id = elements[0].target_id;
    const int center_id = elements[0].center_id;
    for (size_t i = 0; i < element_count; ++i) {
        if (elements[i].target_id != target_id || elements[i].center_id != center_id) {
            return false;
        }
    }

    try {
        MemoryKeplerSource source;
        source.elements.assign(elements, elements + element_count);
        source.method_id = method_id;
        source.frame = frame;
        source.jd_tdb_start = jd_tdb_start;
        source.jd_tdb_end = jd_tdb_end;
        source.generation = 1;

        MemoryKeplerRegistry& reg = registry();
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

bool unregister_memory_kepler_source(uint64_t source_id) noexcept {
    MemoryKeplerRegistry& reg = registry();
    std::lock_guard<std::mutex> lock(reg.mutex);
    return reg.sources.erase(source_id) > 0;
}

void clear_memory_kepler_sources() noexcept {
    MemoryKeplerRegistry& reg = registry();
    std::lock_guard<std::mutex> lock(reg.mutex);
    reg.sources.clear();
    reg.next_id = 1;
}

bool make_memory_kepler_descriptor(
    uint64_t source_id,
    EphemerisBlockDescriptor* out
) noexcept {
    if (out) {
        *out = EphemerisBlockDescriptor();
    }
    if (!out) {
        return false;
    }

    MemoryKeplerRegistry& reg = registry();
    std::lock_guard<std::mutex> lock(reg.mutex);
    std::unordered_map<uint64_t, MemoryKeplerSource>::const_iterator it = reg.sources.find(source_id);
    if (it == reg.sources.end()) {
        return false;
    }
    return make_descriptor_from_source(source_id, it->second, out);
}

bool compile_memory_kepler_source(
    const EphemerisBlockDescriptor& descriptor,
    StorageEphemerisBlock* out
) noexcept {
    if (!out
        || descriptor.format != EphemerisBlockFormat::Kepler
        || descriptor.source_key.source_id != TAIYIN_KEPLER_MEMORY_SOURCE_ID
        || descriptor.path.size() != 0) {
        return false;
    }

    std::vector<KeplerElements> elements;
    {
        MemoryKeplerRegistry& reg = registry();
        std::lock_guard<std::mutex> lock(reg.mutex);
        std::unordered_map<uint64_t, MemoryKeplerSource>::const_iterator it =
            reg.sources.find(descriptor.source_key.block_id);
        if (it == reg.sources.end()
            || it->second.generation != descriptor.source_key.generation
            || it->second.method_id != descriptor.method_id
            || it->second.frame != descriptor.frame
            || it->second.jd_tdb_start > descriptor.jd_tdb_start
            || it->second.jd_tdb_end < descriptor.jd_tdb_end
            || it->second.elements.empty()) {
            return false;
        }
        elements = it->second.elements;
    }

    return compile_kepler_ephemeris_block(
        &elements[0],
        elements.size(),
        descriptor.jd_tdb_start,
        descriptor.jd_tdb_end,
        out);
}

}  // namespace internal
}  // namespace taiyin
