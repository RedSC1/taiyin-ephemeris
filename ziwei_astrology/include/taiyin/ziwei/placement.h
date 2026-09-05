#ifndef TAIYIN_ZIWEI_PLACEMENT_H
#define TAIYIN_ZIWEI_PLACEMENT_H
#include "taiyin/ziwei/chart.h"
#include <string>

namespace taiyin { namespace ziwei {

struct PlacementResult {
    uint64_t rule_registry_fingerprint = 0;
    PlacementInput input;
    PlacementAnchors anchors;
    Gender gender = Gender::Male;
    StarId life_master = kInvalidStarId;
    StarId body_master = kInvalidStarId;
    Stem year_transform_stem = Stem::Jia;
    TransformSet year_transformations;
    std::array<PalaceState, kBranchCount> palaces;
    // 0xff = not placed (including unavailable inputs and flow-only stars).
    std::vector<uint8_t> star_positions;
    std::vector<StarTransformMask> transformation_masks;
    std::vector<OmittedPlacement> omitted_placements;
};

Status arrange_ziwei_stars(
    const PlacementInput& input, Gender gender, ZiweiChartMode mode,
    const CompiledRules& rules, PlacementResult* out,
    const Bureau* fixed_bureau = NULL) noexcept;

// Each edit returns an independent result; no context ownership or mutable cache.
// Pass the original birth's anchor options, including Tian/Di/Ren chart mode.
Status modify_natal_chart(
    const NatalChart& source, const PlacementPatch& patch,
    const AnchorOptions& options, const CompiledRules& rules, NatalChart* out) noexcept;
Status shift_natal_life_palace(const NatalChart& source, int32_t steps, NatalChart* out) noexcept;
Status reset_natal_chart(const NatalChart& source, NatalChart* out) noexcept;
PlacementInput natal_placement_input(const NatalChart& chart) noexcept;

constexpr uint32_t kCastingSpaceSize = 259200u;
enum class CastingMethod : uint8_t { Manual = 0, Index, Number, Random };

// No CalendarFacts, birth date, or real-date flow API on a casting chart.
struct CastingChart {
    PlacementResult plate;
    ZiweiChartMode chart_mode = ZiweiChartMode::TianPan;
    CastingMethod method = CastingMethod::Manual;
    uint32_t index = UINT32_MAX; // manual inputs need not form a sexagenary pair
    std::string number;         // canonical decimal text for number-v1
    PlacementModification modification;
    std::shared_ptr<const CastingChart> original_chart;
};

// A source must produce uniformly distributed uint32 values; called at most
// 128 times by rejection sampling. Nonzero status propagates to the caller.
typedef Status (*CastingRandomUint32)(void* user_data, uint32_t* out);
Status casting_input_from_index(uint32_t index, PlacementInput* out) noexcept;
Status make_casting_chart(const PlacementInput& input, Gender gender,
    ZiweiChartMode mode, const CompiledRules& rules, CastingChart* out,
    const Bureau* fixed_bureau = NULL) noexcept;
Status casting_chart_from_index(uint32_t index, Gender gender,
    ZiweiChartMode mode, const CompiledRules& rules, CastingChart* out) noexcept;
// Same ASCII normalization, FNV-1a/Mulberry32 and rejection mapping as JS number-v1.
Status casting_chart_from_number(const std::string& number, Gender gender,
    ZiweiChartMode mode, const CompiledRules& rules, CastingChart* out) noexcept;
// NULL source uses the OS random source (never a silent PRNG fallback).
Status random_casting_chart(Gender gender, ZiweiChartMode mode,
    const CompiledRules& rules, CastingChart* out,
    CastingRandomUint32 source = NULL, void* user_data = NULL) noexcept;
Status modify_casting_chart(const CastingChart& source, const PlacementPatch& patch,
    const CompiledRules& rules, CastingChart* out) noexcept;
Status shift_casting_life_palace(const CastingChart& source, int32_t steps,
    CastingChart* out) noexcept;
Status reset_casting_chart(const CastingChart& source, CastingChart* out) noexcept;

} }
#endif
