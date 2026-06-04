#include "taiyin/internal/ephemeris_cache.h"

#include <cmath>
#include <limits>

namespace taiyin {
namespace internal {
namespace {

struct StorageBlockDeleter {
    void operator()(StorageEphemerisBlock* block) const noexcept {
        if (block) {
            destroy_storage_ephemeris_block(block);
            delete block;
        }
    }
};

}  // namespace

EphemerisCachePolicy::EphemerisCachePolicy() noexcept
    : recency_weight(1.0),
      frequency_weight(8.0),
      priority_weight(1.0),
      reload_cost_weight(0.001),
      size_penalty_weight(0.0) {}

size_t EphemerisRouteKeyHash::operator()(const EphemerisRouteKey& key) const noexcept {
    uint64_t h = 1469598103934665603ull;
    h ^= static_cast<uint32_t>(key.target_id);
    h *= 1099511628211ull;
    h ^= static_cast<uint32_t>(key.center_id);
    h *= 1099511628211ull;
    h ^= static_cast<uint32_t>(key.method_id);
    h *= 1099511628211ull;
    h ^= static_cast<uint32_t>(key.bucket_id);
    h *= 1099511628211ull;
    return static_cast<size_t>(h);
}

EphemerisBlockCache::EphemerisBlockCache(size_t max_bytes) noexcept
    : EphemerisBlockCache(max_bytes, EphemerisCachePolicy()) {}

EphemerisBlockCache::EphemerisBlockCache(size_t max_bytes, const EphemerisCachePolicy& policy) noexcept
    : next_cache_id_(1),
      max_bytes_(max_bytes),
      total_bytes_(0),
      access_counter_(0),
      policy_(policy),
      blocks_(),
      route_to_cache_id_(),
      cache_id_to_route_(),
      stats_(),
      coverage_() {}

EphemerisBlockCache::~EphemerisBlockCache() noexcept {
    clear();
}

bool EphemerisBlockCache::insert(
    const EphemerisRouteKey& key,
    double jd_tdb_start,
    double jd_tdb_end,
    StorageEphemerisBlock* storage,
    double compile_cost_us,
    int priority_bias
) noexcept {
    if (!storage || storage->data_vector.empty() || !storage->position || jd_tdb_end <= jd_tdb_start) {
        return false;
    }

    std::lock_guard<std::mutex> lock(mutex_);

    RouteMap::iterator existing_route = route_to_cache_id_.find(key);
    if (existing_route != route_to_cache_id_.end()) {
        evict_cache_id_locked(existing_route->second);
    }

    const size_t incoming_bytes = storage->total_bytes;
    if (!evict_until_budget_locked(incoming_bytes)) {
        return false;
    }

    const int cache_id = allocate_cache_id_locked();
    storage->cache_id = cache_id;

    StorageEphemerisBlock* raw_block = new (std::nothrow) StorageEphemerisBlock();
    if (!raw_block) {
        return false;
    }

    std::shared_ptr<StorageEphemerisBlock> block_ptr;
    try {
        block_ptr = std::shared_ptr<StorageEphemerisBlock>(raw_block, StorageBlockDeleter());
    } catch (...) {
        delete raw_block;
        return false;
    }
    move_storage_block(storage, block_ptr.get());

    try {
        blocks_[cache_id] = block_ptr;
        route_to_cache_id_[key] = cache_id;
        cache_id_to_route_[cache_id] = key;
    } catch (...) {
        return false;
    }

    EphemerisCacheStats stat;
    stat.last_access = ++access_counter_;
    stat.hit_count = 0;
    stat.compile_cost_us = compile_cost_us;
    stat.priority_bias = priority_bias;
    stats_[cache_id] = stat;
    coverage_[cache_id] = EphemerisCacheCoverage(jd_tdb_start, jd_tdb_end);

    total_bytes_ += incoming_bytes;
    return true;
}

bool EphemerisBlockCache::eval_position(const EphemerisRouteKey& key, double jd_tdb, Vector3* out) noexcept {
    if (!out) {
        return false;
    }

    std::shared_ptr<StorageEphemerisBlock> storage;
    CompiledEphemerisBlock block;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        int cache_id = 0;
        if (!get_block_locked(key, &storage, &cache_id) || !storage->position || !covers_locked(cache_id, jd_tdb)) {
            return false;
        }
        if (!get_compiled_block_from_storage(storage.get(), key.target_id, &block)) {
            return false;
        }
        touch_locked(cache_id);
    }
    return block.position(jd_tdb, block.data, out);
}

bool EphemerisBlockCache::eval_velocity(const EphemerisRouteKey& key, double jd_tdb, Vector3* out) noexcept {
    if (!out) {
        return false;
    }

    std::shared_ptr<StorageEphemerisBlock> storage;
    CompiledEphemerisBlock block;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        int cache_id = 0;
        if (!get_block_locked(key, &storage, &cache_id) || !storage->velocity || !covers_locked(cache_id, jd_tdb)) {
            return false;
        }
        if (!get_compiled_block_from_storage(storage.get(), key.target_id, &block) || !block.velocity) {
            return false;
        }
        touch_locked(cache_id);
    }
    return block.velocity(jd_tdb, block.data, out);
}

bool EphemerisBlockCache::eval_acceleration(const EphemerisRouteKey& key, double jd_tdb, Vector3* out) noexcept {
    if (!out) {
        return false;
    }

    std::shared_ptr<StorageEphemerisBlock> storage;
    CompiledEphemerisBlock block;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        int cache_id = 0;
        if (!get_block_locked(key, &storage, &cache_id) || !storage->acceleration || !covers_locked(cache_id, jd_tdb)) {
            return false;
        }
        if (!get_compiled_block_from_storage(storage.get(), key.target_id, &block) || !block.acceleration) {
            return false;
        }
        touch_locked(cache_id);
    }
    return block.acceleration(jd_tdb, block.data, out);
}

bool EphemerisBlockCache::eval_state(const EphemerisRouteKey& key, double jd_tdb, CartesianState* out) noexcept {
    if (!out) {
        return false;
    }

    std::shared_ptr<StorageEphemerisBlock> storage;
    CompiledEphemerisBlock block;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        int cache_id = 0;
        if (!get_block_locked(key, &storage, &cache_id) || !covers_locked(cache_id, jd_tdb)) {
            return false;
        }
        if (!get_compiled_block_from_storage(storage.get(), key.target_id, &block)) {
            return false;
        }
        touch_locked(cache_id);
    }
    return eval_compiled_ephemeris_block(jd_tdb, &block, out);
}

bool EphemerisBlockCache::contains(const EphemerisRouteKey& key) const noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    return route_to_cache_id_.find(key) != route_to_cache_id_.end();
}

void EphemerisBlockCache::clear() noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    clear_locked();
}

void EphemerisBlockCache::set_policy(const EphemerisCachePolicy& policy) noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    policy_ = policy;
}

EphemerisCachePolicy EphemerisBlockCache::policy() const noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    return policy_;
}

size_t EphemerisBlockCache::total_bytes() const noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    return total_bytes_;
}

size_t EphemerisBlockCache::entry_count() const noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    return blocks_.size();
}

size_t EphemerisBlockCache::max_bytes() const noexcept {
    return max_bytes_;
}

void EphemerisBlockCache::clear_locked() noexcept {
    blocks_.clear();
    route_to_cache_id_.clear();
    cache_id_to_route_.clear();
    stats_.clear();
    coverage_.clear();
    total_bytes_ = 0;
}

void EphemerisBlockCache::evict_cache_id_locked(int cache_id) noexcept {
    std::unordered_map<int, std::shared_ptr<StorageEphemerisBlock> >::iterator block_it = blocks_.find(cache_id);
    if (block_it != blocks_.end()) {
        const size_t bytes = block_it->second ? block_it->second->total_bytes : 0;
        if (total_bytes_ >= bytes) {
            total_bytes_ -= bytes;
        } else {
            total_bytes_ = 0;
        }
        blocks_.erase(block_it);
    }

    std::unordered_map<int, EphemerisRouteKey>::iterator reverse_it = cache_id_to_route_.find(cache_id);
    if (reverse_it != cache_id_to_route_.end()) {
        route_to_cache_id_.erase(reverse_it->second);
        cache_id_to_route_.erase(reverse_it);
    }
    stats_.erase(cache_id);
    coverage_.erase(cache_id);
}

bool EphemerisBlockCache::evict_until_budget_locked(size_t incoming_bytes) noexcept {
    if (max_bytes_ == 0) {
        clear_locked();
        return incoming_bytes == 0;
    }

    if (incoming_bytes > max_bytes_) {
        clear_locked();
        return true;
    }

    while (total_bytes_ + incoming_bytes > max_bytes_) {
        const int victim = select_weighted_victim_locked();
        if (victim == 0) {
            return false;
        }
        evict_cache_id_locked(victim);
    }
    return true;
}

int EphemerisBlockCache::select_weighted_victim_locked() const noexcept {
    double worst_score = std::numeric_limits<double>::infinity();
    uint64_t worst_access = std::numeric_limits<uint64_t>::max();
    int worst_id = 0;
    for (std::unordered_map<int, EphemerisCacheStats>::const_iterator it = stats_.begin(); it != stats_.end(); ++it) {
        if (blocks_.find(it->first) == blocks_.end()) {
            continue;
        }

        const double score = keep_score_locked(it->first);
        const uint64_t last_access = it->second.last_access;
        if (score < worst_score
            || (score == worst_score && last_access < worst_access)
            || (score == worst_score && last_access == worst_access && (worst_id == 0 || it->first < worst_id))) {
            worst_score = score;
            worst_access = last_access;
            worst_id = it->first;
        }
    }
    return worst_id;
}

double EphemerisBlockCache::keep_score_locked(int cache_id) const noexcept {
    std::unordered_map<int, EphemerisCacheStats>::const_iterator stat_it = stats_.find(cache_id);
    std::unordered_map<int, std::shared_ptr<StorageEphemerisBlock> >::const_iterator block_it = blocks_.find(cache_id);
    if (stat_it == stats_.end() || block_it == blocks_.end() || !block_it->second) {
        return -std::numeric_limits<double>::infinity();
    }

    const EphemerisCacheStats& stat = stat_it->second;
    const uint64_t age = access_counter_ >= stat.last_access ? access_counter_ - stat.last_access : 0;
    const double recency_score = 1.0 / (1.0 + static_cast<double>(age));
    const double frequency_score = std::log(1.0 + static_cast<double>(stat.hit_count));
    const double priority_score = static_cast<double>(stat.priority_bias);
    const double reload_cost_score = stat.compile_cost_us;
    const double size_score = static_cast<double>(block_it->second->total_bytes);

    return policy_.recency_weight * recency_score
        + policy_.frequency_weight * frequency_score
        + policy_.priority_weight * priority_score
        + policy_.reload_cost_weight * reload_cost_score
        - policy_.size_penalty_weight * size_score;
}

void EphemerisBlockCache::touch_locked(int cache_id) noexcept {
    std::unordered_map<int, EphemerisCacheStats>::iterator it = stats_.find(cache_id);
    if (it == stats_.end()) {
        return;
    }
    it->second.last_access = ++access_counter_;
    ++it->second.hit_count;
}

bool EphemerisBlockCache::covers_locked(int cache_id, double jd_tdb) const noexcept {
    std::unordered_map<int, EphemerisCacheCoverage>::const_iterator it = coverage_.find(cache_id);
    if (it == coverage_.end()) {
        return false;
    }
    return jd_tdb >= it->second.jd_tdb_start && jd_tdb < it->second.jd_tdb_end;
}

bool EphemerisBlockCache::get_block_locked(
    const EphemerisRouteKey& key,
    std::shared_ptr<StorageEphemerisBlock>* out,
    int* out_cache_id
) noexcept {
    if (!out || !out_cache_id) {
        return false;
    }
    RouteMap::iterator route_it = route_to_cache_id_.find(key);
    if (route_it == route_to_cache_id_.end()) {
        return false;
    }
    std::unordered_map<int, std::shared_ptr<StorageEphemerisBlock> >::iterator block_it = blocks_.find(route_it->second);
    if (block_it == blocks_.end() || !block_it->second) {
        return false;
    }
    *out = block_it->second;
    *out_cache_id = route_it->second;
    return true;
}

int EphemerisBlockCache::allocate_cache_id_locked() noexcept {
    if (next_cache_id_ <= 0) {
        next_cache_id_ = 1;
    }
    const int id = next_cache_id_++;
    if (next_cache_id_ <= 0) {
        next_cache_id_ = 1;
    }
    return id;
}

void EphemerisBlockCache::move_storage_block(StorageEphemerisBlock* src, StorageEphemerisBlock* dst) noexcept {
    if (!src || !dst || src == dst) {
        return;
    }
    destroy_storage_ephemeris_block(dst);

    dst->cache_id = src->cache_id;
    dst->format = src->format;
    dst->position = src->position;
    dst->velocity = src->velocity;
    dst->acceleration = src->acceleration;
    dst->data_vector.swap(src->data_vector);
    dst->id_to_index.swap(src->id_to_index);
    dst->total_bytes = src->total_bytes;
    dst->destroy_element = src->destroy_element;

    src->cache_id = 0;
    src->format = EphemerisBlockFormat::FormatUnknown;
    src->position = 0;
    src->velocity = 0;
    src->acceleration = 0;
    src->total_bytes = 0;
    src->destroy_element = 0;
    src->data_vector.clear();
    src->id_to_index.clear();
}

}  // namespace internal
}  // namespace taiyin
