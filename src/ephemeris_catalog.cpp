#include "taiyin/internal/ephemeris_catalog.h"

namespace taiyin {
namespace internal {
namespace {

uint64_t mix_hash_u64(uint64_t h, uint64_t value) noexcept {
    h ^= static_cast<uint64_t>(value);
    h *= 1099511628211ull;
    return h;
}

uint64_t catalog_method_page_id(
    int target_id,
    int center_id,
    EphemerisFrame frame,
    int method_id
) noexcept {
    uint64_t h = 1469598103934665603ull;
    h = mix_hash_u64(h, static_cast<uint32_t>(target_id));
    h = mix_hash_u64(h, static_cast<uint32_t>(center_id));
    h = mix_hash_u64(h, static_cast<uint32_t>(frame));
    h = mix_hash_u64(h, static_cast<uint32_t>(method_id));
    return h;
}

}  // namespace

EphemerisBlockCatalog::EphemerisBlockCatalog() noexcept
    : descriptors_(),
      method_pages_(),
      source_indexes_(),
      lock_() {}

EphemerisBlockCatalog::EphemerisBlockCatalog(const EphemerisBlockCatalog& other)
    : descriptors_(),
      method_pages_(),
      source_indexes_(),
      lock_() {
    ReadLockGuard guard(other.lock_);
    descriptors_ = other.descriptors_;
    method_pages_ = other.method_pages_;
    source_indexes_ = other.source_indexes_;
}

EphemerisBlockCatalog& EphemerisBlockCatalog::operator=(const EphemerisBlockCatalog& other) {
    if (this == &other) {
        return *this;
    }

    std::vector<EphemerisBlockDescriptor> next_descriptors;
    MethodPageMap next_method_pages;
    SourceIndexMap next_source_indexes;
    {
        ReadLockGuard other_guard(other.lock_);
        next_descriptors = other.descriptors_;
        next_method_pages = other.method_pages_;
        next_source_indexes = other.source_indexes_;
    }

    WriteLockGuard guard(lock_);
    descriptors_ = next_descriptors;
    method_pages_ = next_method_pages;
    source_indexes_ = next_source_indexes;
    return *this;
}

void EphemerisBlockCatalog::swap(EphemerisBlockCatalog& other) noexcept {
    if (this == &other) {
        return;
    }
    // Lock both catalogs so concurrent readers of `other` cannot race the
    // container swaps. Stable address ordering avoids deadlock; STL
    // container swaps are noexcept, so this is safe inside noexcept.
    if (static_cast<const void*>(&lock_) < static_cast<const void*>(&other.lock_)) {
        WriteLockGuard first(lock_);
        WriteLockGuard second(other.lock_);
        descriptors_.swap(other.descriptors_);
        method_pages_.swap(other.method_pages_);
        source_indexes_.swap(other.source_indexes_);
    } else {
        WriteLockGuard first(other.lock_);
        WriteLockGuard second(lock_);
        descriptors_.swap(other.descriptors_);
        method_pages_.swap(other.method_pages_);
        source_indexes_.swap(other.source_indexes_);
    }
}

bool ephemeris_block_key_equal(
    const EphemerisBlockKey& lhs,
    const EphemerisBlockKey& rhs
) noexcept {
    return lhs == rhs;
}

size_t EphemerisBlockKeyHash::operator()(const EphemerisBlockKey& key) const noexcept {
    uint64_t h = 1469598103934665603ull;
    h = mix_hash_u64(h, key.source_id);
    h = mix_hash_u64(h, key.block_id);
    h = mix_hash_u64(h, key.generation);
    h = mix_hash_u64(h, key.purpose);
    return static_cast<size_t>(h);
}

bool ephemeris_descriptor_may_cover(
    const EphemerisBlockDescriptor& descriptor,
    const EphemerisBlockQuery& query
) noexcept {
    if (descriptor.target_id != query.target_id) {
        return false;
    }
    if (descriptor.center_id != query.center_id || descriptor.frame != query.frame) {
        return false;
    }
    if (descriptor.jd_tdb_end <= descriptor.jd_tdb_start) {
        return false;
    }
    return days_between_split_jd(descriptor.jd_tdb_start_split, query.jd_tdb) >= 0.0
        && days_between_split_jd(descriptor.jd_tdb_end_split, query.jd_tdb) < 0.0;
}

bool ephemeris_descriptor_matches_method(
    const EphemerisBlockDescriptor& descriptor,
    const EphemerisBlockQuery& query,
    int method_id
) noexcept {
    return descriptor.method_id == method_id
        && ephemeris_descriptor_may_cover(descriptor, query);
}

bool EphemerisBlockCatalog::add(const EphemerisBlockDescriptor& descriptor) {
    EphemerisBlockDescriptor normalized_descriptor = descriptor;
    if (!split_julian_date_from_double(
            normalized_descriptor.jd_tdb_start,
            &normalized_descriptor.jd_tdb_start_split)
        || !split_julian_date_from_double(
            normalized_descriptor.jd_tdb_end,
            &normalized_descriptor.jd_tdb_end_split)) {
        return false;
    }

    WriteLockGuard guard(lock_);
    const size_t descriptor_index = descriptors_.size();
    try {
        descriptors_.push_back(normalized_descriptor);
        if (!index_descriptor(descriptor_index)) {
            descriptors_.pop_back();
            rebuild_indexes();
            return false;
        }
    } catch (...) {
        if (descriptors_.size() > descriptor_index) {
            descriptors_.pop_back();
        }
        rebuild_indexes();
        return false;
    }
    return true;
}

bool EphemerisBlockCatalog::add_source_index(const EphemerisSourceIndex& index) {
    if (index.source_key.source_id == 0
        || index.source_key.generation == 0
        || index.format == EphemerisBlockFormat::FormatUnknown
        || (!index.payload
            && index.format != EphemerisBlockFormat::Opm2
            && index.format != EphemerisBlockFormat::Tkc1)) {
        return false;
    }

    WriteLockGuard guard(lock_);
    try {
        EphemerisSourceIndex stored = index;
        if (stored.format == EphemerisBlockFormat::Opm2) {
            stored.weak_payload = stored.payload;
            stored.payload.reset();
        }
        source_indexes_[index.source_key] = stored;
    } catch (...) {
        return false;
    }
    return true;
}

bool EphemerisBlockCatalog::find_source_index(
    const EphemerisBlockKey& source_key,
    EphemerisSourceIndex* out
) const {
    if (out) {
        *out = EphemerisSourceIndex();
    }
    if (!out || source_key.source_id == 0) {
        return false;
    }

    ReadLockGuard guard(lock_);
    SourceIndexMap::const_iterator it = source_indexes_.find(source_key);
    if (it == source_indexes_.end()) {
        return false;
    }
    *out = it->second;
    if (out->format == EphemerisBlockFormat::Opm2 && !out->payload) {
        out->payload = out->weak_payload.lock();
    }
    return true;
}

size_t EphemerisBlockCatalog::size() const noexcept {
    ReadLockGuard guard(lock_);
    return descriptors_.size();
}

bool EphemerisBlockCatalog::get(size_t index, EphemerisBlockDescriptor* out) const noexcept {
    if (out) {
        *out = EphemerisBlockDescriptor();
    }
    if (!out) {
        return false;
    }

    ReadLockGuard guard(lock_);
    if (index >= descriptors_.size()) {
        return false;
    }
    *out = descriptors_[index];
    return true;
}

bool EphemerisBlockCatalog::find_method_candidates(
    const EphemerisBlockQuery& query,
    int method_id,
    std::vector<EphemerisBlockDescriptor>* out
) const {
    if (!out) {
        return false;
    }

    try {
        ReadLockGuard guard(lock_);
        out->clear();
        const DescriptorIndexList* indexes = find_method_page_indexes(query, method_id);
        if (!indexes) {
            return false;
        }
        for (size_t i = 0; i < indexes->size(); ++i) {
            const size_t descriptor_index = (*indexes)[i];
            if (descriptor_index < descriptors_.size()
                && ephemeris_descriptor_matches_method(descriptors_[descriptor_index], query, method_id)) {
                out->push_back(descriptors_[descriptor_index]);
            }
        }
    } catch (...) {
        return false;
    }

    return !out->empty();
}

bool EphemerisBlockCatalog::index_descriptor(size_t descriptor_index) {
    if (descriptor_index >= descriptors_.size()) {
        return false;
    }

    const EphemerisBlockDescriptor& descriptor = descriptors_[descriptor_index];
    const uint64_t method_page_id = catalog_method_page_id(
        descriptor.target_id,
        descriptor.center_id,
        descriptor.frame,
        descriptor.method_id);

    try {
        MethodPageSet& method_set = method_pages_[method_page_id];
        MethodPage* method_page = 0;
        for (size_t i = 0; i < method_set.size(); ++i) {
            if (method_set[i].target_id == descriptor.target_id
                && method_set[i].center_id == descriptor.center_id
                && method_set[i].frame == descriptor.frame
                && method_set[i].method_id == descriptor.method_id) {
                method_page = &method_set[i];
                break;
            }
        }
        if (!method_page) {
            MethodPage page;
            page.target_id = descriptor.target_id;
            page.center_id = descriptor.center_id;
            page.frame = descriptor.frame;
            page.method_id = descriptor.method_id;
            method_set.push_back(page);
            method_page = &method_set.back();
        }
        method_page->indexes.push_back(descriptor_index);
    } catch (...) {
        throw;
    }
    return true;
}

bool EphemerisBlockCatalog::rebuild_indexes() noexcept {
    try {
        method_pages_.clear();
        for (size_t i = 0; i < descriptors_.size(); ++i) {
            if (!index_descriptor(i)) {
                method_pages_.clear();
                return false;
            }
        }
    } catch (...) {
        method_pages_.clear();
        return false;
    }
    return true;
}

const EphemerisBlockCatalog::DescriptorIndexList* EphemerisBlockCatalog::find_method_page_indexes(
    const EphemerisBlockQuery& query,
    int method_id
) const noexcept {
    const uint64_t page_id = catalog_method_page_id(
        query.target_id,
        query.center_id,
        query.frame,
        method_id);
    MethodPageMap::const_iterator it = method_pages_.find(page_id);
    if (it == method_pages_.end()) {
        return 0;
    }
    const MethodPageSet& pages = it->second;
    for (size_t i = 0; i < pages.size(); ++i) {
        if (pages[i].target_id == query.target_id
            && pages[i].center_id == query.center_id
            && pages[i].frame == query.frame
            && pages[i].method_id == method_id) {
            return &pages[i].indexes;
        }
    }
    return 0;
}

}  // namespace internal
}  // namespace taiyin
