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
    WriteLockGuard guard(lock_);

    if (max_entries_ == 0 || !data.data) {
        return false;
    }

    IndexMap::iterator existing = index_.find(key);
    if (existing != index_.end()) {
        return replace_slot(existing->second, key, data);
    }

    size_t slot_index = 0;
    if (find_empty_slot(&slot_index)) {
        return insert_into_empty_slot(slot_index, key, data);
    }

    try {
        if (slots_.size() < max_entries_) {
            Slot slot;
            slot.key = key;
            slot.data = data;
            slot.occupied = true;
            slot.used = true;
            slots_.push_back(slot);
            index_[key] = slots_.size() - 1;
            return true;
        }
    } catch (...) {
        if (!slots_.empty() && slots_.back().occupied && slots_.back().key == key) {
            slots_.pop_back();
        }
        return false;
    }

    if (!select_clock_victim(&slot_index)) {
        return false;
    }
    return replace_slot(slot_index, key, data);
}

bool EphemerisSegmentCache::with_data(
    const EphemerisSegmentCacheKey& key,
    EphemerisSegmentCacheReadFn fn,
    void* user
) noexcept {
    if (!fn) {
        return false;
    }

    ReadLockGuard guard(lock_);
    IndexMap::iterator it = index_.find(key);
    if (it == index_.end() || it->second >= slots_.size()) {
        return false;
    }

    const Slot& slot = slots_[it->second];
    if (!slot.occupied || !slot.data.data) {
        return false;
    }
    return fn(slot.data.data, user);
}

bool EphemerisSegmentCache::contains(const EphemerisSegmentCacheKey& key) const noexcept {
    ReadLockGuard guard(lock_);
    return index_.find(key) != index_.end();
}

bool EphemerisSegmentCache::erase(const EphemerisSegmentCacheKey& key) noexcept {
    WriteLockGuard guard(lock_);

    IndexMap::iterator it = index_.find(key);
    if (it == index_.end() || it->second >= slots_.size()) {
        return false;
    }
    Slot& slot = slots_[it->second];
    index_.erase(it);
    destroy_slot(&slot);
    return true;
}

void EphemerisSegmentCache::clear() noexcept {
    WriteLockGuard guard(lock_);

    for (size_t i = 0; i < slots_.size(); ++i) {
        destroy_slot(&slots_[i]);
    }
    slots_.clear();
    index_.clear();
    hand_ = 0;
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
    const EphemerisSegmentCacheData& data
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
    return true;
}

bool EphemerisSegmentCache::replace_slot(
    size_t slot_index,
    const EphemerisSegmentCacheKey& key,
    const EphemerisSegmentCacheData& data
) noexcept {
    if (slot_index >= slots_.size()) {
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

    destroy_data(&slot.data);
    slot.key = key;
    slot.data = data;
    slot.occupied = true;
    slot.used = true;
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

void EphemerisSegmentCache::destroy_slot(Slot* slot) noexcept {
    if (!slot) {
        return;
    }
    destroy_data(&slot->data);
    slot->key = EphemerisSegmentCacheKey();
    slot->occupied = false;
    slot->used = false;
}

void EphemerisSegmentCache::destroy_data(EphemerisSegmentCacheData* data) noexcept {
    if (!data || !data->data) {
        if (data) {
            data->destroy = 0;
        }
        return;
    }
    if (data->destroy) {
        data->destroy(data->data);
    }
    data->data = 0;
    data->destroy = 0;
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
