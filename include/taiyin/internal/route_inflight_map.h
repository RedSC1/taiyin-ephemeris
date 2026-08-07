#ifndef TAIYIN_INTERNAL_ROUTE_INFLIGHT_MAP_H
#define TAIYIN_INTERNAL_ROUTE_INFLIGHT_MAP_H

#include "ephemeris_segment_cache.h"

#include <condition_variable>
#include <mutex>
#include <unordered_map>

namespace taiyin {
namespace internal {

enum RouteInflightAction {
    RouteInflightLoadNow,
    RouteInflightLoadFinished
};

class RouteInflightMap {
public:
    RouteInflightMap() noexcept;
    ~RouteInflightMap() noexcept;

    RouteInflightMap(const RouteInflightMap&) = delete;
    RouteInflightMap& operator=(const RouteInflightMap&) = delete;

    RouteInflightAction acquire(const EphemerisSegmentCacheKey& key) noexcept;
    void release(const EphemerisSegmentCacheKey& key, bool success) noexcept;

private:
    struct Entry {
        size_t waiter_count;
        bool completed;
        bool load_success;

        Entry() noexcept
            : waiter_count(0), completed(false), load_success(false) {}
    };

    typedef std::unordered_map<EphemerisSegmentCacheKey, Entry, EphemerisSegmentCacheKeyHash> EntryMap;

    std::mutex mutex_;
    std::condition_variable cv_;
    EntryMap inflight_;
};

class RouteInflightGuard {
public:
    RouteInflightGuard(RouteInflightMap& map, const EphemerisSegmentCacheKey& key) noexcept;
    ~RouteInflightGuard() noexcept;

    RouteInflightGuard(const RouteInflightGuard&) = delete;
    RouteInflightGuard& operator=(const RouteInflightGuard&) = delete;

    RouteInflightAction begin() noexcept;
    void end(bool success) noexcept;

private:
    RouteInflightMap& map_;
    EphemerisSegmentCacheKey key_;
    bool loader_;
    bool finished_;
};

}  // namespace internal
}  // namespace taiyin

#endif  // TAIYIN_INTERNAL_ROUTE_INFLIGHT_MAP_H
