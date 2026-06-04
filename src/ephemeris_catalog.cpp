#include "taiyin/internal/ephemeris_catalog.h"

namespace taiyin {
namespace internal {
namespace {

bool priority_entry_less(
    const MethodPriorityEntry& lhs,
    const MethodPriorityEntry& rhs
) noexcept {
    if (lhs.priority != rhs.priority) {
        return lhs.priority > rhs.priority;
    }
    return lhs.method_id < rhs.method_id;
}

}  // namespace

bool ephemeris_block_key_equal(
    const EphemerisBlockKey& lhs,
    const EphemerisBlockKey& rhs
) noexcept {
    return lhs.source_id == rhs.source_id
        && lhs.block_id == rhs.block_id
        && lhs.generation == rhs.generation
        && lhs.purpose == rhs.purpose;
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
    return query.jd_tdb >= descriptor.jd_tdb_start
        && query.jd_tdb < descriptor.jd_tdb_end;
}

bool rank_ephemeris_descriptors(
    const std::vector<const EphemerisBlockDescriptor*>& candidates,
    const MethodPriorityEntry* priorities,
    size_t priority_count,
    std::vector<const EphemerisBlockDescriptor*>* out
) noexcept {
    if (!out) {
        return false;
    }

    try {
        out->clear();
        if (candidates.empty()) {
            return false;
        }

        std::vector<bool> used(candidates.size(), false);
        if (priorities && priority_count > 0) {
            for (size_t priority_index = 0; priority_index < priority_count; ++priority_index) {
                const int method_id = priorities[priority_index].method_id;
                for (size_t candidate_index = 0; candidate_index < candidates.size(); ++candidate_index) {
                    const EphemerisBlockDescriptor* candidate = candidates[candidate_index];
                    if (!used[candidate_index] && candidate && candidate->method_id == method_id) {
                        out->push_back(candidate);
                        used[candidate_index] = true;
                    }
                }
            }
        }

        for (size_t candidate_index = 0; candidate_index < candidates.size(); ++candidate_index) {
            if (!used[candidate_index] && candidates[candidate_index]) {
                out->push_back(candidates[candidate_index]);
                used[candidate_index] = true;
            }
        }
    } catch (...) {
        out->clear();
        return false;
    }

    return !out->empty();
}

bool select_ephemeris_descriptor(
    const std::vector<const EphemerisBlockDescriptor*>& candidates,
    const MethodPriorityEntry* priorities,
    size_t priority_count,
    const EphemerisBlockDescriptor** out
) noexcept {
    if (out) {
        *out = 0;
    }
    if (!out) {
        return false;
    }

    std::vector<const EphemerisBlockDescriptor*> ranked;
    if (!rank_ephemeris_descriptors(candidates, priorities, priority_count, &ranked)) {
        return false;
    }

    *out = ranked[0];
    return true;
}

bool find_first_ephemeris_descriptor(
    const EphemerisBlockDescriptor* descriptors,
    size_t descriptor_count,
    const EphemerisBlockQuery& query,
    const EphemerisBlockDescriptor** out
) noexcept {
    if (out) {
        *out = 0;
    }
    if (!descriptors || !out) {
        return false;
    }

    for (size_t i = 0; i < descriptor_count; ++i) {
        const EphemerisBlockDescriptor& candidate = descriptors[i];
        if (ephemeris_descriptor_may_cover(candidate, query)) {
            *out = &candidate;
            return true;
        }
    }

    return false;
}

bool EphemerisPriorityRegistry::set_global_method_priority(int method_id, int priority) {
    return set_priority(&global_method_priority_, method_id, priority);
}

bool EphemerisPriorityRegistry::set_target_method_priority(
    int target_id,
    int method_id,
    int priority
) {
    try {
        return set_priority(&target_method_priority_[target_id], method_id, priority);
    } catch (...) {
        return false;
    }
}

const MethodPriorityEntry* EphemerisPriorityRegistry::global_method_priorities(
    size_t* out_count
) const noexcept {
    if (out_count) {
        *out_count = global_method_priority_.size();
    }
    if (global_method_priority_.empty()) {
        return 0;
    }
    return &global_method_priority_[0];
}

const MethodPriorityEntry* EphemerisPriorityRegistry::target_method_priorities(
    int target_id,
    size_t* out_count
) const noexcept {
    std::unordered_map<int, std::vector<MethodPriorityEntry> >::const_iterator it =
        target_method_priority_.find(target_id);
    if (it == target_method_priority_.end() || it->second.empty()) {
        if (out_count) {
            *out_count = 0;
        }
        return 0;
    }

    if (out_count) {
        *out_count = it->second.size();
    }
    return &it->second[0];
}

const MethodPriorityEntry* EphemerisPriorityRegistry::method_priorities_for_target(
    int target_id,
    size_t* out_count
) const noexcept {
    size_t target_count = 0;
    const MethodPriorityEntry* target_entries = target_method_priorities(target_id, &target_count);
    if (target_entries && target_count > 0) {
        if (out_count) {
            *out_count = target_count;
        }
        return target_entries;
    }
    return global_method_priorities(out_count);
}

bool EphemerisPriorityRegistry::set_priority(
    std::vector<MethodPriorityEntry>* entries,
    int method_id,
    int priority
) {
    if (!entries) {
        return false;
    }

    try {
        for (std::vector<MethodPriorityEntry>::iterator it = entries->begin(); it != entries->end(); ++it) {
            if (it->method_id == method_id) {
                entries->erase(it);
                break;
            }
        }

        MethodPriorityEntry entry(method_id, priority);
        std::vector<MethodPriorityEntry>::iterator insert_at = entries->begin();
        while (insert_at != entries->end() && !priority_entry_less(entry, *insert_at)) {
            ++insert_at;
        }
        entries->insert(insert_at, entry);
    } catch (...) {
        return false;
    }

    return true;
}

bool EphemerisBlockCatalog::add(const EphemerisBlockDescriptor& descriptor) {
    try {
        descriptors_.push_back(descriptor);
    } catch (...) {
        return false;
    }
    return true;
}

size_t EphemerisBlockCatalog::size() const noexcept {
    return descriptors_.size();
}

const EphemerisBlockDescriptor* EphemerisBlockCatalog::at(size_t index) const noexcept {
    if (index >= descriptors_.size()) {
        return 0;
    }
    return &descriptors_[index];
}

bool EphemerisBlockCatalog::find_candidates(
    const EphemerisBlockQuery& query,
    std::vector<const EphemerisBlockDescriptor*>* out
) const {
    if (!out) {
        return false;
    }

    try {
        out->clear();
        for (size_t i = 0; i < descriptors_.size(); ++i) {
            if (ephemeris_descriptor_may_cover(descriptors_[i], query)) {
                out->push_back(&descriptors_[i]);
            }
        }
    } catch (...) {
        return false;
    }

    return !out->empty();
}

bool EphemerisBlockCatalog::rank_candidates(
    const EphemerisBlockQuery& query,
    const EphemerisPriorityRegistry& priorities,
    std::vector<const EphemerisBlockDescriptor*>* out
) const {
    if (!out) {
        return false;
    }

    std::vector<const EphemerisBlockDescriptor*> candidates;
    if (!find_candidates(query, &candidates)) {
        try {
            out->clear();
        } catch (...) {
        }
        return false;
    }

    size_t priority_count = 0;
    const MethodPriorityEntry* priority_entries =
        priorities.method_priorities_for_target(query.target_id, &priority_count);
    return rank_ephemeris_descriptors(candidates, priority_entries, priority_count, out);
}

const EphemerisBlockDescriptor* EphemerisBlockCatalog::find_first(
    const EphemerisBlockQuery& query
) const noexcept {
    const EphemerisBlockDescriptor* result = 0;
    if (descriptors_.empty()) {
        return 0;
    }
    find_first_ephemeris_descriptor(&descriptors_[0], descriptors_.size(), query, &result);
    return result;
}

const EphemerisBlockDescriptor* EphemerisBlockCatalog::select_best(
    const EphemerisBlockQuery& query,
    const EphemerisPriorityRegistry& priorities
) const {
    std::vector<const EphemerisBlockDescriptor*> ranked;
    if (!rank_candidates(query, priorities, &ranked)) {
        return 0;
    }
    return ranked[0];
}

}  // namespace internal
}  // namespace taiyin
