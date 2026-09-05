#ifndef TAIYIN_ZIWEI_CHART_H
#define TAIYIN_ZIWEI_CHART_H

#include "taiyin/status.h"
#include "taiyin/ziwei/anchors.h"
#include "taiyin/ziwei/dynamic_bitset.h"
#include "taiyin/ziwei/rules.h"
#include "taiyin/ziwei/placement_types.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>
#include <memory>

namespace taiyin {
namespace ziwei {

struct PalaceState {
    DynamicBitset stars;
};

// A natal transformation is a chart-level overlay indexed by StarId.  The
// palace bitsets remain exclusively about placement; they never carry
// transformation state.  The first four marks are the birth-year targets,
// followed by the star's own-palace (centrifugal/self) and opposite-palace
// (centripetal) decorations.
enum class StarTransformMark : uint8_t {
    BirthYearLu = 0,
    BirthYearQuan,
    BirthYearKe,
    BirthYearJi,
    CentrifugalLu,
    CentrifugalQuan,
    CentrifugalKe,
    CentrifugalJi,
    CentripetalLu,
    CentripetalQuan,
    CentripetalKe,
    CentripetalJi,
    Count,
};

constexpr std::size_t kStarTransformMarkCount =
    static_cast<std::size_t>(StarTransformMark::Count);

constexpr bool is_valid(StarTransformMark value) noexcept {
    return static_cast<std::size_t>(value) < kStarTransformMarkCount;
}

constexpr std::size_t to_index(StarTransformMark value) noexcept {
    return static_cast<std::size_t>(value);
}

typedef uint16_t StarTransformMask;

struct NatalTransformationOverlay {
    Stem birth_year_stem;
    TransformSet birth_year;
    // One 12-bit mask per StarId. This is a derived presentation/query index;
    // it is not another placement representation.
    std::vector<StarTransformMask> marks_by_star;
};

struct NatalChart {
    // Edits retain one immutable original, not a linked list of previous edits.
    // Birth facts/pillars remain untouched; only placement and palace roles vary.
    std::shared_ptr<const NatalChart> original_chart;
    PlacementModification modification;
    std::vector<OmittedPlacement> omitted_placements;
    // Preserve the complete normalized birth facts that produced this chart.
    // Flow resolution uses them to reject accidental birth/chart pairings.
    CalendarFacts birth_facts;
    // StarId values are meaningful only within the immutable rule catalog
    // that produced this chart. Flow layers must use the same registry.
    uint64_t rule_registry_fingerprint;
    Anchors anchors;
    // Body palace is deliberately outside the stable 31-anchor ABI, while
    // remaining first-class chart metadata for rules such as 天寿.
    Branch body_palace;
    Gender gender;
    StarId life_master;
    StarId body_master;
    // Indexed by physical Branch, independent of the twelve palace roles.
    std::array<Stem, kBranchCount> palace_stems;
    // Indexed by Branch, not PalaceId. Palace roles are resolved through
    // anchors.palace_positions.
    std::array<PalaceState, kBranchCount> palaces;
    NatalTransformationOverlay transformations;
};

Status make_natal_chart(
    const CalendarFacts& facts,
    const Anchors& anchors,
    Branch body_palace,
    const NatalRuleOptions& options,
    const CompiledRules& rules,
    NatalChart* out
) noexcept;

// One branch index per StarId. Flow-only stars are encoded as 0xff. This
// stable numeric form is intended for bulk differential tests and contains no
// localized labels.
Status dump_natal_star_positions(
    const NatalChart& chart,
    std::vector<uint8_t>* out
) noexcept;

StarTransformMask star_transform_mask(
    const NatalChart& chart,
    StarId star
) noexcept;

bool has_star_transform_mark(
    const NatalChart& chart,
    StarTransformMark mark,
    StarId star
) noexcept;

}  // namespace ziwei
}  // namespace taiyin

#endif  // TAIYIN_ZIWEI_CHART_H
