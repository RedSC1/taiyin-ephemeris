#ifndef TAIYIN_INTERNAL_EPHEMERIS_ROUTE_KEY_H
#define TAIYIN_INTERNAL_EPHEMERIS_ROUTE_KEY_H

#include <cstddef>
#include <cstdint>

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
    size_t operator()(const EphemerisRouteKey& key) const noexcept {
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
};

}  // namespace internal
}  // namespace taiyin

#endif  // TAIYIN_INTERNAL_EPHEMERIS_ROUTE_KEY_H
