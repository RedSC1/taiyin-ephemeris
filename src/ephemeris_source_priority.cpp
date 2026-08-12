#include "taiyin/internal/ephemeris_source_priority.h"

#include "taiyin/internal/path_utils.h"
#include "taiyin/internal/ephemeris_source_identity.h"

namespace taiyin {
namespace internal {
namespace {

#if defined(_WIN32)
char ascii_lower(char value) noexcept {
    return value >= 'A' && value <= 'Z'
        ? static_cast<char>(value - 'A' + 'a')
        : value;
}
#endif

std::string normalize_priority_key(const std::string& path) {
    std::string result = path;
#if defined(_WIN32)
    for (size_t i = 0; i < result.size(); ++i) {
        result[i] = ascii_lower(result[i]);
    }
#endif
    return result;
}

std::string normalized_basename(const std::string& path) {
    size_t begin = 0;
    for (size_t i = 0; i < path.size(); ++i) {
        if (is_path_separator(path[i])) {
            begin = i + 1;
        }
    }
    return normalize_priority_key(path.substr(begin));
}

}  // namespace

EphemerisSourcePriorityTable::EphemerisSourcePriorityTable() noexcept
    : path_priorities_(),
      basename_priorities_() {}

void EphemerisSourcePriorityTable::clear() noexcept {
    path_priorities_.clear();
    basename_priorities_.clear();
}

bool EphemerisSourcePriorityTable::set_path_priority(
    const char* path_or_basename,
    int priority
) noexcept {
    if (!path_or_basename || path_or_basename[0] == '\0') {
        return false;
    }
    try {
        const std::string normalized = normalize_priority_key(path_or_basename);
        bool has_separator = false;
        for (size_t i = 0; i < normalized.size(); ++i) {
            if (is_path_separator(normalized[i])) {
                has_separator = true;
                break;
            }
        }
        if (has_separator) {
            path_priorities_[normalized] = priority;
        } else {
            basename_priorities_[normalized] = priority;
        }
    } catch (...) {
        return false;
    }
    return true;
}

bool EphemerisSourcePriorityTable::clear_path_priority(
    const char* path_or_basename
) noexcept {
    if (!path_or_basename || path_or_basename[0] == '\0') {
        return false;
    }
    try {
        const std::string normalized = normalize_priority_key(path_or_basename);
        bool has_separator = false;
        for (size_t i = 0; i < normalized.size(); ++i) {
            if (is_path_separator(normalized[i])) {
                has_separator = true;
                break;
            }
        }
        if (has_separator) {
            path_priorities_.erase(normalized);
        } else {
            basename_priorities_.erase(normalized);
        }
    } catch (...) {
        return false;
    }
    return true;
}

bool EphemerisSourcePriorityTable::empty() const noexcept {
    return path_priorities_.empty() && basename_priorities_.empty();
}

bool EphemerisSourcePriorityTable::find_explicit_priority_unchecked(
    const EphemerisBlockDescriptor& descriptor,
    int* out_priority
) const {
    if (out_priority) {
        *out_priority = 0;
    }
    if (!out_priority) {
        return false;
    }
    const std::string path = normalize_priority_key(descriptor.path);
    std::unordered_map<std::string, int>::const_iterator exact = path_priorities_.find(path);
    if (exact != path_priorities_.end()) {
        *out_priority = exact->second;
        return true;
    }
    const std::string basename = normalized_basename(descriptor.path);
    std::unordered_map<std::string, int>::const_iterator named = basename_priorities_.find(basename);
    if (named == basename_priorities_.end()) {
        return false;
    }
    *out_priority = named->second;
    return true;
}

bool EphemerisSourcePriorityTable::explicit_priority(
    const EphemerisBlockDescriptor& descriptor,
    int* out_priority
) const noexcept {
    try {
        return find_explicit_priority_unchecked(descriptor, out_priority);
    } catch (...) {
        if (out_priority) {
            *out_priority = 0;
        }
        return false;
    }
}

int64_t EphemerisSourcePriorityTable::priority_for(
    const EphemerisBlockDescriptor& descriptor
) const noexcept {
    int explicit_value = 0;
    if (explicit_priority(descriptor, &explicit_value)) {
        return explicit_value;
    }
    if (descriptor.format == EphemerisBlockFormat::Opm2) {
        return static_cast<int64_t>(descriptor.source_key.source_id);
    }
    if (descriptor.format == EphemerisBlockFormat::Spk) {
        return default_spk_source_priority(descriptor.source_key.source_id);
    }
    return 0;
}

int EphemerisSourcePriorityTable::compare(
    const EphemerisBlockDescriptor& lhs,
    const EphemerisBlockDescriptor& rhs
) const noexcept {
    try {
        const int64_t lhs_priority = priority_for(lhs);
        const int64_t rhs_priority = priority_for(rhs);
        if (lhs_priority > rhs_priority) return -1;
        if (lhs_priority < rhs_priority) return 1;
        return 0;
    } catch (...) {
        // Candidate ordering is an optimization/policy layer. On allocation
        // failure retain catalog discovery order instead of terminating a
        // noexcept ephemeris calculation.
        return 0;
    }
}

}  // namespace internal
}  // namespace taiyin
