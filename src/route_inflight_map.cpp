#include "taiyin/internal/route_inflight_map.h"

namespace taiyin {
namespace internal {

RouteInflightMap::RouteInflightMap() noexcept
    : mutex_(), cv_(), inflight_() {}

RouteInflightMap::~RouteInflightMap() noexcept {}

RouteInflightAction RouteInflightMap::acquire(const EphemerisRouteKey& key) noexcept {
    std::unique_lock<std::mutex> lock(mutex_);

    EntryMap::iterator it = inflight_.find(key);
    if (it == inflight_.end()) {
        try {
            inflight_[key] = Entry();
        } catch (...) {
            return RouteInflightLoadNow;
        }
        return RouteInflightLoadNow;
    }

    ++it->second.waiter_count;
    while (!it->second.completed) {
        cv_.wait(lock);
        it = inflight_.find(key);
        if (it == inflight_.end()) {
            return RouteInflightLoadFinished;
        }
    }

    if (it->second.waiter_count > 0) {
        --it->second.waiter_count;
    }
    if (it->second.waiter_count == 0) {
        inflight_.erase(it);
    }
    return RouteInflightLoadFinished;
}

void RouteInflightMap::release(const EphemerisRouteKey& key, bool success) noexcept {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        EntryMap::iterator it = inflight_.find(key);
        if (it == inflight_.end()) {
            return;
        }

        it->second.completed = true;
        it->second.load_success = success;
        if (it->second.waiter_count == 0) {
            inflight_.erase(it);
        }
    }
    cv_.notify_all();
}

RouteInflightGuard::RouteInflightGuard(RouteInflightMap& map, const EphemerisRouteKey& key) noexcept
    : map_(map), key_(key), loader_(false), finished_(false) {}

RouteInflightGuard::~RouteInflightGuard() noexcept {
    if (loader_ && !finished_) {
        map_.release(key_, false);
    }
}

RouteInflightAction RouteInflightGuard::begin() noexcept {
    const RouteInflightAction action = map_.acquire(key_);
    loader_ = (action == RouteInflightLoadNow);
    finished_ = false;
    return action;
}

void RouteInflightGuard::end(bool success) noexcept {
    if (loader_ && !finished_) {
        map_.release(key_, success);
        finished_ = true;
    }
}

}  // namespace internal
}  // namespace taiyin
