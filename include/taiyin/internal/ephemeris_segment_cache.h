#ifndef TAIYIN_INTERNAL_EPHEMERIS_SEGMENT_CACHE_H
#define TAIYIN_INTERNAL_EPHEMERIS_SEGMENT_CACHE_H

#include "ephemeris_catalog.h"
#include "writer_preferred_rwlock.h"

#include <cstddef>
#include <cstdint>
#include <unordered_map>
#include <vector>

namespace taiyin {
namespace internal {

enum EphemerisSegmentCacheKind {
    EphemerisSegmentCacheKindUnknown = 0,
    EphemerisSegmentCacheKindOpm2Segment = 1,
    EphemerisSegmentCacheKindSpkSegment = 2,
    EphemerisSegmentCacheKindKeplerElements = 3,
    EphemerisSegmentCacheKindStarRecord = 4,
    EphemerisSegmentCacheKindCustomMethod = 8,
};

struct EphemerisSegmentCacheKey {
    uint32_t kind;
    int target_id;
    int center_id;
    int method_id;
    EphemerisFrame frame;
    EphemerisBlockKey source_key;
    int64_t item_id;

    EphemerisSegmentCacheKey()
        : kind(EphemerisSegmentCacheKindUnknown),
          target_id(0),
          center_id(0),
          method_id(0),
          frame(EphemerisFrame::FrameUnknown),
          source_key(),
          item_id(0) {}

    EphemerisSegmentCacheKey(
        uint32_t kind_value,
        int target_id_value,
        int center_id_value,
        int method_id_value,
        EphemerisFrame frame_value,
        const EphemerisBlockKey& source_key_value,
        int64_t item_id_value)
        : kind(kind_value),
          target_id(target_id_value),
          center_id(center_id_value),
          method_id(method_id_value),
          frame(frame_value),
          source_key(source_key_value),
          item_id(item_id_value) {}

    bool operator==(const EphemerisSegmentCacheKey& other) const noexcept {
        return kind == other.kind
            && target_id == other.target_id
            && center_id == other.center_id
            && method_id == other.method_id
            && frame == other.frame
            && source_key == other.source_key
            && item_id == other.item_id;
    }
};

struct EphemerisSegmentCacheKeyHash {
    size_t operator()(const EphemerisSegmentCacheKey& key) const noexcept;
};

typedef void (*EphemerisSegmentCacheDestroyFn)(void* data);
typedef bool (*EphemerisSegmentCacheReadFn)(const void* data, void* user);

struct EphemerisSegmentCacheData {
    void* data;
    EphemerisSegmentCacheDestroyFn destroy;

    EphemerisSegmentCacheData()
        : data(0), destroy(0) {}

    EphemerisSegmentCacheData(void* data_value, EphemerisSegmentCacheDestroyFn destroy_value)
        : data(data_value), destroy(destroy_value) {}
};

class EphemerisSegmentCache {
public:
    explicit EphemerisSegmentCache(size_t max_entries) noexcept;
    ~EphemerisSegmentCache() noexcept;

    EphemerisSegmentCache(const EphemerisSegmentCache&) = delete;
    EphemerisSegmentCache& operator=(const EphemerisSegmentCache&) = delete;

    bool insert(const EphemerisSegmentCacheKey& key, const EphemerisSegmentCacheData& data) noexcept;
    // Invokes fn() while the cache read lock is held; the returned data pointer
    // is only valid for the duration of the call. fn must NOT acquire the cache
    // write lock (insert/erase/clear) or call a runtime entry point that takes a
    // write lock, or the writer-preferring rwlock self-deadlocks against the
    // read lock this thread already holds. Slow callbacks also stall writers.
    bool with_data(
        const EphemerisSegmentCacheKey& key,
        EphemerisSegmentCacheReadFn fn,
        void* user
    ) noexcept;
    bool contains(const EphemerisSegmentCacheKey& key) const noexcept;
    bool erase(const EphemerisSegmentCacheKey& key) noexcept;
    void clear() noexcept;

    size_t max_entries() const noexcept;
    size_t entry_count() const noexcept;

private:
    struct Slot {
        EphemerisSegmentCacheKey key;
        EphemerisSegmentCacheData data;
        bool occupied;
        bool used;

        Slot()
            : key(), data(), occupied(false), used(false) {}
    };

    typedef std::unordered_map<EphemerisSegmentCacheKey, size_t, EphemerisSegmentCacheKeyHash> IndexMap;

    bool insert_into_empty_slot(
        size_t slot_index,
        const EphemerisSegmentCacheKey& key,
        const EphemerisSegmentCacheData& data
    ) noexcept;
    bool replace_slot(
        size_t slot_index,
        const EphemerisSegmentCacheKey& key,
        const EphemerisSegmentCacheData& data
    ) noexcept;
    bool find_empty_slot(size_t* out_slot_index) const noexcept;
    bool select_clock_victim(size_t* out_slot_index) noexcept;
    void destroy_slot(Slot* slot) noexcept;
    void destroy_data(EphemerisSegmentCacheData* data) noexcept;
    void advance_hand() noexcept;

    mutable WriterPreferredRwLock lock_;
    size_t max_entries_;
    std::vector<Slot> slots_;
    IndexMap index_;
    size_t hand_;
};

}  // namespace internal
}  // namespace taiyin

#endif  // TAIYIN_INTERNAL_EPHEMERIS_SEGMENT_CACHE_H
