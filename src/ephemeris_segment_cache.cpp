#include "taiyin/internal/ephemeris_segment_cache.h"

namespace taiyin {
namespace internal {

size_t EphemerisSegmentCacheKeyHash::operator()(const EphemerisSegmentCacheKey& key) const noexcept {
    uint64_t h = 1469598103934665603ull;
    h ^= static_cast<uint64_t>(key.kind);
    h *= 1099511628211ull;
    h ^= static_cast<uint32_t>(key.target_id);
    h *= 1099511628211ull;
    h ^= static_cast<uint32_t>(key.center_id);
    h *= 1099511628211ull;
    h ^= static_cast<uint32_t>(key.method_id);
    h *= 1099511628211ull;
    h ^= static_cast<uint32_t>(key.frame);
    h *= 1099511628211ull;
    h ^= key.source_key.source_id;
    h *= 1099511628211ull;
    h ^= key.source_key.block_id;
    h *= 1099511628211ull;
    h ^= key.source_key.generation;
    h *= 1099511628211ull;
    h ^= key.source_key.purpose;
    h *= 1099511628211ull;
    h ^= static_cast<uint64_t>(key.item_id);
    h *= 1099511628211ull;
    return static_cast<size_t>(h);
}

EphemerisSegmentCache::EphemerisSegmentCache(size_t max_entries) noexcept
    : lock_(),
      max_entries_(max_entries),
      slots_(),
      index_(),
      hand_(0) {
    try {
        slots_.reserve(max_entries_);
        index_.reserve(max_entries_);
    } catch (...) {
    }
}

EphemerisSegmentCache::~EphemerisSegmentCache() noexcept {
    clear();
}

bool EphemerisSegmentCache::insert(
    const EphemerisSegmentCacheKey& key,
    const EphemerisSegmentCacheData& data
) noexcept {
    if (!data.data) {
        return false;
    }

    SharedDataPtr candidate;
    try {
        candidate = std::make_shared<SharedData>(data.data, data.destroy);
    } catch (...) {
        return false;
    }
    SharedDataPtr retired;
    bool inserted = false;

    {
        WriteLockGuard guard(lock_);

        if (max_entries_ == 0) {
            return false;
        }

        IndexMap::iterator existing = index_.find(key);
        if (existing != index_.end()) {
            inserted = replace_slot(existing->second, key, candidate, &retired);
        } else {
            size_t slot_index = 0;
            if (find_empty_slot(&slot_index)) {
                inserted = insert_into_empty_slot(slot_index, key, candidate);
            } else {
                try {
                    if (slots_.size() < max_entries_) {
                        Slot slot;
                        slot.key = key;
                        slot.data = candidate;
                        slot.occupied = true;
                        slot.used = true;
                        slots_.push_back(slot);
                        try {
                            index_[key] = slots_.size() - 1;
                            candidate->owned = true;
                            inserted = true;
                        } catch (...) {
                            slots_.pop_back();
                        }
                    }
                } catch (...) {
                }

                if (!inserted && slots_.size() >= max_entries_) {
                    if (!select_clock_victim(&slot_index)) {
                        return false;
                    }
                    inserted = replace_slot(slot_index, key, candidate, &retired);
                }
            }
        }
    }

    // retired is released after the cache write lock, so a user-supplied
    // destroy callback may safely call back into the cache/runtime.
    return inserted;
}

bool EphemerisSegmentCache::with_data(
    const EphemerisSegmentCacheKey& key,
    EphemerisSegmentCacheReadFn fn,
    void* user
) noexcept {
    if (!fn) {
        return false;
    }

    SharedDataPtr pinned;
    {
        ReadLockGuard guard(lock_);
        IndexMap::iterator it = index_.find(key);
        if (it == index_.end() || it->second >= slots_.size()) {
            return false;
        }

        const Slot& slot = slots_[it->second];
        if (!slot.occupied || !slot.data || !slot.data->data) {
            return false;
        }
        pinned = slot.data;
    }
    return fn(pinned->data, user);
}

bool EphemerisSegmentCache::contains(const EphemerisSegmentCacheKey& key) const noexcept {
    ReadLockGuard guard(lock_);
    return index_.find(key) != index_.end();
}

bool EphemerisSegmentCache::erase(const EphemerisSegmentCacheKey& key) noexcept {
    SharedDataPtr retired;
    bool erased = false;
    {
        WriteLockGuard guard(lock_);

        IndexMap::iterator it = index_.find(key);
        if (it == index_.end() || it->second >= slots_.size()) {
            return false;
        }
        Slot& slot = slots_[it->second];
        index_.erase(it);
        release_slot(&slot, &retired);
        erased = true;
    }
    return erased;
}

void EphemerisSegmentCache::clear() noexcept {
    std::vector<Slot> retired;
    {
        WriteLockGuard guard(lock_);

        retired.swap(slots_);
        index_.clear();
        hand_ = 0;
    }
    // Payload deleters run when retired is destroyed, after releasing lock_.
}

size_t EphemerisSegmentCache::max_entries() const noexcept {
    ReadLockGuard guard(lock_);
    return max_entries_;
}

size_t EphemerisSegmentCache::entry_count() const noexcept {
    ReadLockGuard guard(lock_);
    return index_.size();
}

bool EphemerisSegmentCache::insert_into_empty_slot(
    size_t slot_index,
    const EphemerisSegmentCacheKey& key,
    const SharedDataPtr& data
) noexcept {
    if (slot_index >= slots_.size()) {
        return false;
    }
    try {
        index_[key] = slot_index;
    } catch (...) {
        return false;
    }

    Slot& slot = slots_[slot_index];
    slot.key = key;
    slot.data = data;
    slot.occupied = true;
    slot.used = true;
    data->owned = true;
    return true;
}

bool EphemerisSegmentCache::replace_slot(
    size_t slot_index,
    const EphemerisSegmentCacheKey& key,
    const SharedDataPtr& data,
    SharedDataPtr* retired
) noexcept {
    if (slot_index >= slots_.size() || !retired) {
        return false;
    }

    Slot& slot = slots_[slot_index];
    const bool same_key = slot.occupied && slot.key == key;
    EphemerisSegmentCacheKey old_key = slot.key;

    if (!same_key && slot.occupied) {
        index_.erase(old_key);
    }

    try {
        index_[key] = slot_index;
    } catch (...) {
        if (!same_key && slot.occupied) {
            try {
                index_[old_key] = slot_index;
            } catch (...) {
            }
        }
        return false;
    }

    retired->swap(slot.data);
    slot.key = key;
    slot.data = data;
    slot.occupied = true;
    slot.used = true;
    data->owned = true;
    return true;
}

bool EphemerisSegmentCache::find_empty_slot(size_t* out_slot_index) const noexcept {
    if (out_slot_index) {
        *out_slot_index = 0;
    }
    if (!out_slot_index) {
        return false;
    }
    for (size_t i = 0; i < slots_.size(); ++i) {
        if (!slots_[i].occupied) {
            *out_slot_index = i;
            return true;
        }
    }
    return false;
}

bool EphemerisSegmentCache::select_clock_victim(size_t* out_slot_index) noexcept {
    if (out_slot_index) {
        *out_slot_index = 0;
    }
    if (!out_slot_index || slots_.empty()) {
        return false;
    }

    const size_t limit = slots_.size() * 2 + 1;
    for (size_t attempts = 0; attempts < limit; ++attempts) {
        if (hand_ >= slots_.size()) {
            hand_ = 0;
        }
        Slot& slot = slots_[hand_];
        if (!slot.occupied || !slot.used) {
            *out_slot_index = hand_;
            advance_hand();
            return true;
        }
        slot.used = false;
        advance_hand();
    }
    return false;
}

void EphemerisSegmentCache::release_slot(
    Slot* slot,
    SharedDataPtr* retired
) noexcept {
    if (!slot || !retired) {
        return;
    }
    retired->swap(slot->data);
    slot->key = EphemerisSegmentCacheKey();
    slot->occupied = false;
    slot->used = false;
}

void EphemerisSegmentCache::advance_hand() noexcept {
    if (slots_.empty()) {
        hand_ = 0;
        return;
    }
    ++hand_;
    if (hand_ >= slots_.size()) {
        hand_ = 0;
    }
}

}  // namespace internal
}  // namespace taiyin
