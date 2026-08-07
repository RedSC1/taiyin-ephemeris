#include "taiyin/runtime/body_registry.h"

namespace taiyin {
namespace runtime {

bool EphemerisBodyRegistry::mark_direct(int body_id) noexcept {
    if (body_id == 0) {
        return false;
    }
    try {
        entries_[body_id].has_direct = true;
    } catch (...) {
        return false;
    }
    return true;
}

bool EphemerisBodyRegistry::unmark_direct(int body_id) noexcept {
    std::unordered_map<int, EphemerisBodyRouteEntry>::iterator it = entries_.find(body_id);
    if (it == entries_.end()) {
        return false;
    }
    it->second.has_direct = false;
    if (!it->second.fallback) {
        entries_.erase(it);
    }
    return true;
}

bool EphemerisBodyRegistry::set_fallback(int body_id, EphemerisBodyFallbackFn fn) noexcept {
    if (body_id == 0 || !fn) {
        return false;
    }
    try {
        entries_[body_id].fallback = fn;
    } catch (...) {
        return false;
    }
    return true;
}

bool EphemerisBodyRegistry::remove_fallback(int body_id) noexcept {
    std::unordered_map<int, EphemerisBodyRouteEntry>::iterator it = entries_.find(body_id);
    if (it == entries_.end()) {
        return false;
    }
    it->second.fallback = 0;
    if (!it->second.has_direct) {
        entries_.erase(it);
    }
    return true;
}

bool EphemerisBodyRegistry::find(int body_id, EphemerisBodyRouteEntry* out) const noexcept {
    if (out) {
        *out = EphemerisBodyRouteEntry();
    }
    if (!out) {
        return false;
    }
    std::unordered_map<int, EphemerisBodyRouteEntry>::const_iterator it = entries_.find(body_id);
    if (it == entries_.end()) {
        return false;
    }
    *out = it->second;
    return true;
}

void EphemerisBodyRegistry::clear() noexcept {
    entries_.clear();
}

size_t EphemerisBodyRegistry::size() const noexcept {
    return entries_.size();
}

void EphemerisBodyRegistry::swap(EphemerisBodyRegistry& other) noexcept {
    entries_.swap(other.entries_);
}

}  // namespace runtime
}  // namespace taiyin
