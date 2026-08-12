#ifndef TAIYIN_INTERNAL_EPHEMERIS_SOURCE_PRIORITY_H
#define TAIYIN_INTERNAL_EPHEMERIS_SOURCE_PRIORITY_H

#include "ephemeris_catalog.h"

#include <string>
#include <unordered_map>

namespace taiyin {
namespace internal {

// Orders data files inside one provider.  The engine also uses explicit file
// priorities to reorder the provider's source-specific product rules while
// retaining the route table's provider slots and fallback boundaries.
class EphemerisSourcePriorityTable {
public:
    EphemerisSourcePriorityTable() noexcept;

    void clear() noexcept;
    // A value containing a path separator matches that normalized full path;
    // a bare filename matches all files with that basename. Higher wins. The
    // explicit value replaces that file's provider-default numeric value.
    bool set_path_priority(const char* path_or_basename, int priority) noexcept;
    bool clear_path_priority(const char* path_or_basename) noexcept;
    bool empty() const noexcept;
    bool explicit_priority(
        const EphemerisBlockDescriptor& descriptor,
        int* out_priority
    ) const noexcept;
    int64_t priority_for(
        const EphemerisBlockDescriptor& descriptor
    ) const noexcept;
    int compare(
        const EphemerisBlockDescriptor& lhs,
        const EphemerisBlockDescriptor& rhs
    ) const noexcept;

private:
    bool find_explicit_priority_unchecked(
        const EphemerisBlockDescriptor& descriptor,
        int* out_priority
    ) const;

    std::unordered_map<std::string, int> path_priorities_;
    std::unordered_map<std::string, int> basename_priorities_;
};

}  // namespace internal
}  // namespace taiyin

#endif  // TAIYIN_INTERNAL_EPHEMERIS_SOURCE_PRIORITY_H
