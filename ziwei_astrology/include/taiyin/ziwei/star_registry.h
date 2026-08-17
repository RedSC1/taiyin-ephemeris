#ifndef TAIYIN_ZIWEI_STAR_REGISTRY_H
#define TAIYIN_ZIWEI_STAR_REGISTRY_H

#include "taiyin/ziwei/types.h"

#include <cstddef>
#include <string>
#include <unordered_map>
#include <vector>

namespace taiyin {
namespace ziwei {

enum class StarCategory : uint8_t {
    Major = 0,
    Lucky = 1,
    Minor = 2,
    Malefic = 3,
    Cycle = 4,
    Other = 5,
};

struct StarMetadata {
    std::string key;
    StarCategory category;
};

// String lookup is intentionally confined to rule compilation and inspection.
// Charts contain only StarId and never retain a StarRegistry copy.
class StarRegistry {
public:
    StarRegistry();

    StarId add(const std::string& key, StarCategory category);
    bool find(const std::string& key, StarId* out) const noexcept;
    const StarMetadata& at(StarId id) const;
    std::size_t size() const noexcept;
    bool empty() const noexcept;
    // Stable ordered identity used to reject rules compiled for a different
    // same-sized catalog after a reload or caller mix-up.
    uint64_t fingerprint() const noexcept;

private:
    std::vector<StarMetadata> stars_;
    std::unordered_map<std::string, StarId> ids_by_key_;
};

}  // namespace ziwei
}  // namespace taiyin

#endif  // TAIYIN_ZIWEI_STAR_REGISTRY_H
