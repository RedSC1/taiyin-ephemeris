#ifndef TAIYIN_ZIWEI_ANCHORS_H
#define TAIYIN_ZIWEI_ANCHORS_H

#include "taiyin/ziwei/types.h"
#include "taiyin/status.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace taiyin {
namespace ziwei {

constexpr std::size_t kAnchorCount = 31u;

enum class PillarBoundary : uint8_t {
    SolarTerm = 0,
    Lunar = 1,
};

enum class ZiweiChartMode : uint8_t {
    TianPan = 0,
    DiPan = 1,
    RenPan = 2,
};

enum class LeapMonthStrategy : uint8_t {
    AsPrevious = 0,
    AsNext = 1,
    SplitAfterFifteenth = 2,
};

struct NatalRuleOptions {
    // Selects the year stem used by 五虎遁 when assigning palace stems.
    PillarBoundary wu_hu_dun_year_boundary;
    PillarBoundary sihua_year_boundary;
    PillarBoundary body_master_year_boundary;
};

NatalRuleOptions default_natal_rule_options() noexcept;

struct AnchorOptions {
    NatalRuleOptions rules;
    ZiweiChartMode chart_mode;
};

// Stable scalar order used by differential tests and future C bindings.
enum class AnchorSlot : uint8_t {
    SolarYearStem = 0,
    SolarYearBranch = 1,
    SolarMonthStem = 2,
    SolarMonthBranch = 3,
    SolarDayStem = 4,
    SolarDayBranch = 5,
    SolarHourStem = 6,
    SolarHourBranch = 7,
    LunarYearStem = 8,
    LunarYearBranch = 9,
    LunarMonthStem = 10,
    LunarMonthBranch = 11,
    LunarDayStem = 12,
    LunarDayBranch = 13,
    LunarHourStem = 14,
    LunarHourBranch = 15,
    Bureau = 16,
    Ziwei = 17,
    Tianfu = 18,
    PalaceLife = 19,
    PalaceSiblings = 20,
    PalaceSpouse = 21,
    PalaceChildren = 22,
    PalaceWealth = 23,
    PalaceHealth = 24,
    PalaceTravel = 25,
    PalaceFriends = 26,
    PalaceCareer = 27,
    PalaceProperty = 28,
    PalaceFortune = 29,
    PalaceParents = 30,
};

struct Anchors {
    Pillars solar_term;
    Pillars lunar;
    Bureau bureau;
    Branch ziwei;
    Branch tianfu;
    // Indexed by PalaceId. A valid result is a permutation of all 12 branches.
    std::array<Branch, kPalaceCount> palace_positions;
};

bool validate_anchors(const Anchors& anchors) noexcept;

// The result contains raw zero-based enum indices in AnchorSlot order. It is
// intentionally language-neutral and contains no labels or presentation text.
std::array<uint8_t, kAnchorCount> flatten_anchors(
    const Anchors& anchors
) noexcept;

AnchorOptions default_anchor_options() noexcept;

Status compute_palace_stems(
    Stem year_stem,
    std::array<Stem, kBranchCount>* out
) noexcept;

Status resolve_effective_lunar_month(
    const LunarDateFacts& lunar_date,
    LeapMonthStrategy strategy,
    int32_t* out_year,
    uint8_t* out_month
) noexcept;

// Computes the fixed 31 anchors using the behavior of the author's Dart
// ziwei_core oracle. Body palace is useful chart metadata but is deliberately
// returned separately because it is not one of the formal 31 anchors.
Status compute_anchors(
    const CalendarFacts& facts,
    const AnchorOptions& options,
    Anchors* out,
    Branch* out_body_palace
) noexcept;

}  // namespace ziwei
}  // namespace taiyin

#endif  // TAIYIN_ZIWEI_ANCHORS_H
