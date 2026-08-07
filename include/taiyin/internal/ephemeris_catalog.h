#ifndef TAIYIN_INTERNAL_EPHEMERIS_CATALOG_H
#define TAIYIN_INTERNAL_EPHEMERIS_CATALOG_H

#include "ephemeris_block.h"
#include "ephemeris_route_key.h"
#include "writer_preferred_rwlock.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace taiyin {
namespace internal {

enum EphemerisFrame {
    FrameUnknown,
    IcrfJ2000Equatorial,
};

enum EphemerisCachePolicyKind {
    CacheWholeEntry,
    CacheFixedSpan,
    CacheNaturalSegment,
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

    bool operator==(const EphemerisBlockKey& other) const noexcept {
        return source_id == other.source_id
            && block_id == other.block_id
            && generation == other.generation
            && purpose == other.purpose;
    }
};

struct EphemerisBlockKeyHash {
    size_t operator()(const EphemerisBlockKey& key) const noexcept;
};

struct EphemerisCachePolicy {
    EphemerisCachePolicyKind kind;
    double origin_jd;
    double span_days;
    int64_t first_index;
    uint64_t count;

    EphemerisCachePolicy()
        : kind(CacheWholeEntry),
          origin_jd(0.0),
          span_days(0.0),
          first_index(0),
          count(0) {}
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
    SplitJulianDate jd_tdb_start_split;
    SplitJulianDate jd_tdb_end_split;
    std::string path;
    EphemerisCachePolicy cache_policy;
    // For multi-object catalog formats (TKC1), the zero-based index of the
    // object inside the source file. The loader uses this to locate the object
    // independently of source_key.block_id, which file-identity rekeying may
    // rewrite to disambiguate two files that share the same logical key.
    uint32_t object_index;

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
          jd_tdb_start_split(),
          jd_tdb_end_split(),
          path(),
          cache_policy(),
          object_index(0) {}
};

struct EphemerisSourceIndex {
    EphemerisBlockKey source_key;
    EphemerisBlockFormat format;
    std::string path;
    std::shared_ptr<void> payload;
    std::weak_ptr<void> weak_payload;
    size_t byte_count;

    EphemerisSourceIndex()
        : source_key(),
          format(EphemerisBlockFormat::FormatUnknown),
          path(),
          payload(),
          weak_payload(),
          byte_count(0) {}
};

struct EphemerisBlockQuery {
    int target_id;
    int center_id;
    EphemerisFrame frame;
    SplitJulianDate jd_tdb;

    EphemerisBlockQuery()
        : target_id(0),
          center_id(0),
          frame(EphemerisFrame::FrameUnknown),
          jd_tdb() {}
};

bool ephemeris_block_key_equal(
    const EphemerisBlockKey& lhs,
    const EphemerisBlockKey& rhs
) noexcept;

bool ephemeris_descriptor_may_cover(
    const EphemerisBlockDescriptor& descriptor,
    const EphemerisBlockQuery& query
) noexcept;

bool ephemeris_descriptor_matches_method(
    const EphemerisBlockDescriptor& descriptor,
    const EphemerisBlockQuery& query,
    int method_id
) noexcept;

class EphemerisBlockCatalog {
public:
    EphemerisBlockCatalog() noexcept;
    EphemerisBlockCatalog(const EphemerisBlockCatalog& other);
    EphemerisBlockCatalog& operator=(const EphemerisBlockCatalog& other);

    bool add(const EphemerisBlockDescriptor& descriptor);
    bool add_source_index(const EphemerisSourceIndex& index);
    bool find_source_index(
        const EphemerisBlockKey& source_key,
        EphemerisSourceIndex* out
    ) const;
    size_t size() const noexcept;
    bool get(size_t index, EphemerisBlockDescriptor* out) const noexcept;
    void swap(EphemerisBlockCatalog& other) noexcept;

    bool find_method_candidates(
        const EphemerisBlockQuery& query,
        int method_id,
        std::vector<EphemerisBlockDescriptor>* out
    ) const;

private:
    typedef std::vector<size_t> DescriptorIndexList;

    struct MethodPage {
        int target_id;
        int center_id;
        EphemerisFrame frame;
        int method_id;
        DescriptorIndexList indexes;

        MethodPage()
            : target_id(0),
              center_id(0),
              frame(EphemerisFrame::FrameUnknown),
              method_id(0),
              indexes() {}
    };

    typedef std::vector<MethodPage> MethodPageSet;
    typedef std::unordered_map<uint64_t, MethodPageSet> MethodPageMap;
    typedef std::unordered_map<EphemerisBlockKey, EphemerisSourceIndex, EphemerisBlockKeyHash> SourceIndexMap;

    bool index_descriptor(size_t descriptor_index);
    bool rebuild_indexes() noexcept;
    const DescriptorIndexList* find_method_page_indexes(
        const EphemerisBlockQuery& query,
        int method_id
    ) const noexcept;

    std::vector<EphemerisBlockDescriptor> descriptors_;
    MethodPageMap method_pages_;
    SourceIndexMap source_indexes_;
    mutable WriterPreferredRwLock lock_;
};

}  // namespace internal
}  // namespace taiyin

#endif  // TAIYIN_INTERNAL_EPHEMERIS_CATALOG_H
