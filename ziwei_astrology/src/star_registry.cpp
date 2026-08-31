#include "taiyin/ziwei/star_registry.h"

#include <limits>
#include <stdexcept>

namespace taiyin {
namespace ziwei {

StarRegistry::StarRegistry() : stars_(), ids_by_key_() {}

StarId StarRegistry::add(
    const std::string& key,
    StarCategory category,
    bool natal
) {
    if (key.empty()) {
        throw std::invalid_argument("star key must not be empty");
    }
    if (ids_by_key_.find(key) != ids_by_key_.end()) {
        throw std::invalid_argument("duplicate star key: " + key);
    }
    if (stars_.size() >= static_cast<std::size_t>(kInvalidStarId)) {
        throw std::overflow_error("star registry exceeds the StarId range");
    }

    const StarId id = static_cast<StarId>(stars_.size());
    stars_.push_back(StarMetadata{key, category, natal});
    try {
        ids_by_key_.insert(std::make_pair(key, id));
    } catch (...) {
        // Keep the public registry reusable after an allocation failure.
        stars_.pop_back();
        throw;
    }
    return id;
}

void StarRegistry::set_category(StarId id, StarCategory category) {
    stars_.at(static_cast<std::size_t>(id)).category = category;
}

bool StarRegistry::find(const std::string& key, StarId* out) const noexcept {
    if (out == NULL) return false;
    const std::unordered_map<std::string, StarId>::const_iterator found =
        ids_by_key_.find(key);
    if (found == ids_by_key_.end()) return false;
    *out = found->second;
    return true;
}

const StarMetadata& StarRegistry::at(StarId id) const {
    return stars_.at(static_cast<std::size_t>(id));
}

std::size_t StarRegistry::size() const noexcept {
    return stars_.size();
}

bool StarRegistry::empty() const noexcept {
    return stars_.empty();
}

uint64_t StarRegistry::fingerprint() const noexcept {
    // FNV-1a over the ordered public declarations.  This is an identity guard,
    // not a cryptographic checksum: it prevents a same-sized but differently
    // ordered registry from being paired with compiled StarId tables.
    uint64_t hash = UINT64_C(1469598103934665603);
    const uint64_t prime = UINT64_C(1099511628211);
    for (std::size_t index = 0u; index < stars_.size(); ++index) {
        const StarMetadata& star = stars_[index];
        const uint8_t category = static_cast<uint8_t>(star.category);
        hash ^= category;
        hash *= prime;
        hash ^= star.natal ? UINT64_C(1) : UINT64_C(0);
        hash *= prime;
        for (std::size_t byte = 0u; byte < star.key.size(); ++byte) {
            hash ^= static_cast<uint8_t>(star.key[byte]);
            hash *= prime;
        }
        hash ^= UINT64_C(0xff);
        hash *= prime;
    }
    return hash;
}

}  // namespace ziwei
}  // namespace taiyin
