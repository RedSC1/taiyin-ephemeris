#ifndef TAIYIN_INTERNAL_EPHEMERIS_CATALOG_H
#define TAIYIN_INTERNAL_EPHEMERIS_CATALOG_H

#include "ephemeris_block.h"
#include "ephemeris_cache.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace taiyin {
namespace internal {

enum EphemerisFrame {
    FrameUnknown,
    IcrfJ2000Equatorial,
};

struct EphemerisBlockKey {
    uint64_t source_id;
    uint64_t block_id;
    uint32_t generation;
    uint32_t purpose;

    EphemerisBlockKey()
        : source_id(0),
          block_id(0),
          generation(0),
          purpose(0) {}

    EphemerisBlockKey(
        uint64_t source_id_value,
        uint64_t block_id_value,
        uint32_t generation_value,
        uint32_t purpose_value)
        : source_id(source_id_value),
          block_id(block_id_value),
          generation(generation_value),
          purpose(purpose_value) {}
};

struct EphemerisBlockDescriptor {
    EphemerisRouteKey route_key;
    EphemerisBlockKey source_key;
    int target_id;
    int center_id;
    int method_id;
    EphemerisFrame frame;
    EphemerisBlockFormat format;
    double jd_tdb_start;
    double jd_tdb_end;
    std::string path;

    EphemerisBlockDescriptor()
        : route_key(),
          source_key(),
          target_id(0),
          center_id(0),
          method_id(0),
          frame(EphemerisFrame::FrameUnknown),
          format(EphemerisBlockFormat::FormatUnknown),
          jd_tdb_start(0.0),
          jd_tdb_end(0.0),
          path() {}
};

struct EphemerisBlockQuery {
    int target_id;
    int center_id;
    EphemerisFrame frame;
    double jd_tdb;

    EphemerisBlockQuery()
        : target_id(0),
          center_id(0),
          frame(EphemerisFrame::FrameUnknown),
          jd_tdb(0.0) {}
};

struct MethodPriorityEntry {
    int method_id;
    int priority;

    MethodPriorityEntry()
        : method_id(0), priority(0) {}

    MethodPriorityEntry(int method_id_value, int priority_value)
        : method_id(method_id_value), priority(priority_value) {}
};

bool ephemeris_block_key_equal(
    const EphemerisBlockKey& lhs,
    const EphemerisBlockKey& rhs
) noexcept;

bool ephemeris_descriptor_may_cover(
    const EphemerisBlockDescriptor& descriptor,
    const EphemerisBlockQuery& query
) noexcept;

bool rank_ephemeris_descriptors(
    const std::vector<const EphemerisBlockDescriptor*>& candidates,
    const MethodPriorityEntry* priorities,
    size_t priority_count,
    std::vector<const EphemerisBlockDescriptor*>* out
) noexcept;

bool select_ephemeris_descriptor(
    const std::vector<const EphemerisBlockDescriptor*>& candidates,
    const MethodPriorityEntry* priorities,
    size_t priority_count,
    const EphemerisBlockDescriptor** out
) noexcept;

bool find_first_ephemeris_descriptor(
    const EphemerisBlockDescriptor* descriptors,
    size_t descriptor_count,
    const EphemerisBlockQuery& query,
    const EphemerisBlockDescriptor** out
) noexcept;

class EphemerisPriorityRegistry {
public:
    bool set_global_method_priority(int method_id, int priority);
    bool set_target_method_priority(int target_id, int method_id, int priority);

    const MethodPriorityEntry* global_method_priorities(size_t* out_count) const noexcept;
    const MethodPriorityEntry* target_method_priorities(int target_id, size_t* out_count) const noexcept;
    const MethodPriorityEntry* method_priorities_for_target(int target_id, size_t* out_count) const noexcept;

private:
    static bool set_priority(std::vector<MethodPriorityEntry>* entries, int method_id, int priority);

    std::vector<MethodPriorityEntry> global_method_priority_;
    std::unordered_map<int, std::vector<MethodPriorityEntry> > target_method_priority_;
};

class EphemerisBlockCatalog {
public:
    bool add(const EphemerisBlockDescriptor& descriptor);
    size_t size() const noexcept;
    const EphemerisBlockDescriptor* at(size_t index) const noexcept;

    bool find_candidates(
        const EphemerisBlockQuery& query,
        std::vector<const EphemerisBlockDescriptor*>* out
    ) const;

    bool rank_candidates(
        const EphemerisBlockQuery& query,
        const EphemerisPriorityRegistry& priorities,
        std::vector<const EphemerisBlockDescriptor*>* out
    ) const;

    const EphemerisBlockDescriptor* find_first(const EphemerisBlockQuery& query) const noexcept;
    const EphemerisBlockDescriptor* select_best(
        const EphemerisBlockQuery& query,
        const EphemerisPriorityRegistry& priorities
    ) const;

private:
    std::vector<EphemerisBlockDescriptor> descriptors_;
};

}  // namespace internal
}  // namespace taiyin

#endif  // TAIYIN_INTERNAL_EPHEMERIS_CATALOG_H
