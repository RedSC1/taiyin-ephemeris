#ifndef TAIYIN_INTERNAL_EPHEMERIS_CACHE_H
#define TAIYIN_INTERNAL_EPHEMERIS_CACHE_H

#include "ephemeris_block.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <unordered_map>

namespace taiyin {
namespace internal {

struct EphemerisRouteKey {
    int target_id;
    int center_id;
    int method_id;
    int bucket_id;

    EphemerisRouteKey()
        : target_id(0), center_id(0), method_id(0), bucket_id(0) {}

    EphemerisRouteKey(int target, int center, int method, int bucket)
        : target_id(target), center_id(center), method_id(method), bucket_id(bucket) {}

    bool operator==(const EphemerisRouteKey& other) const noexcept {
        return target_id == other.target_id
            && center_id == other.center_id
            && method_id == other.method_id
            && bucket_id == other.bucket_id;
    }
};

struct EphemerisRouteKeyHash {
    size_t operator()(const EphemerisRouteKey& key) const noexcept;
};

struct EphemerisCacheStats {
    uint64_t last_access;
    uint64_t hit_count;
    double compile_cost_us;
    int priority_bias;

    EphemerisCacheStats()
        : last_access(0), hit_count(0), compile_cost_us(0.0), priority_bias(0) {}
};

struct EphemerisCacheCoverage {
    double jd_tdb_start;
    double jd_tdb_end;

    EphemerisCacheCoverage()
        : jd_tdb_start(0.0), jd_tdb_end(0.0) {}

    EphemerisCacheCoverage(double start, double end)
        : jd_tdb_start(start), jd_tdb_end(end) {}
};

struct EphemerisCachePolicy {
    double recency_weight;
    double frequency_weight;
    double priority_weight;
    double reload_cost_weight;
    double size_penalty_weight;

    EphemerisCachePolicy() noexcept;
};

class EphemerisBlockCache {
public:
    explicit EphemerisBlockCache(size_t max_bytes) noexcept;
    EphemerisBlockCache(size_t max_bytes, const EphemerisCachePolicy& policy) noexcept;
    ~EphemerisBlockCache() noexcept;

    EphemerisBlockCache(const EphemerisBlockCache&) = delete;
    EphemerisBlockCache& operator=(const EphemerisBlockCache&) = delete;

    bool insert(
        const EphemerisRouteKey& key,
        double jd_tdb_start,
        double jd_tdb_end,
        StorageEphemerisBlock* storage,
        double compile_cost_us = 0.0,
        int priority_bias = 0
    ) noexcept;

    bool eval_position(const EphemerisRouteKey& key, double jd_tdb, Vector3* out) noexcept;
    bool eval_velocity(const EphemerisRouteKey& key, double jd_tdb, Vector3* out) noexcept;
    bool eval_acceleration(const EphemerisRouteKey& key, double jd_tdb, Vector3* out) noexcept;
    bool eval_state(const EphemerisRouteKey& key, double jd_tdb, CartesianState* out) noexcept;

    bool contains(const EphemerisRouteKey& key) const noexcept;
    void clear() noexcept;

    void set_policy(const EphemerisCachePolicy& policy) noexcept;
    EphemerisCachePolicy policy() const noexcept;

    size_t total_bytes() const noexcept;
    size_t entry_count() const noexcept;
    size_t max_bytes() const noexcept;

private:
    typedef std::unordered_map<EphemerisRouteKey, int, EphemerisRouteKeyHash> RouteMap;

    void clear_locked() noexcept;
    void evict_cache_id_locked(int cache_id) noexcept;
    bool evict_until_budget_locked(size_t incoming_bytes) noexcept;
    int select_weighted_victim_locked() const noexcept;
    double keep_score_locked(int cache_id) const noexcept;
    void touch_locked(int cache_id) noexcept;
    bool covers_locked(int cache_id, double jd_tdb) const noexcept;
    bool get_block_locked(
        const EphemerisRouteKey& key,
        std::shared_ptr<StorageEphemerisBlock>* out,
        int* out_cache_id
    ) noexcept;
    int allocate_cache_id_locked() noexcept;
    static void move_storage_block(StorageEphemerisBlock* src, StorageEphemerisBlock* dst) noexcept;

    mutable std::mutex mutex_;
    int next_cache_id_;
    size_t max_bytes_;
    size_t total_bytes_;
    uint64_t access_counter_;
    EphemerisCachePolicy policy_;

    std::unordered_map<int, std::shared_ptr<StorageEphemerisBlock> > blocks_;
    RouteMap route_to_cache_id_;
    std::unordered_map<int, EphemerisRouteKey> cache_id_to_route_;
    std::unordered_map<int, EphemerisCacheStats> stats_;
    std::unordered_map<int, EphemerisCacheCoverage> coverage_;
};

}  // namespace internal
}  // namespace taiyin

#endif  // TAIYIN_INTERNAL_EPHEMERIS_CACHE_H
