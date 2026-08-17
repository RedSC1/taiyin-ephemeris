#ifndef TAIYIN_ZIWEI_RULES_H
#define TAIYIN_ZIWEI_RULES_H

#include "taiyin/ziwei/anchors.h"
#include "taiyin/ziwei/star_registry.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace taiyin {
namespace ziwei {

enum class RuleInputSource : uint8_t {
    SolarYearStem = 0,
    SolarYearBranch,
    SolarMonthStem,
    SolarMonthBranch,
    SolarDayStem,
    SolarDayBranch,
    SolarHourStem,
    SolarHourBranch,
    LunarYearStem,
    LunarYearBranch,
    LunarMonthStem,
    LunarMonthBranch,
    LunarDayStem,
    LunarDayBranch,
    LunarHourStem,
    LunarHourBranch,
    Bureau,
    Ziwei,
    Tianfu,
    Life,
    Body,
    SolarZhengKong,
    SolarFuKong,
    LunarZhengKong,
    LunarFuKong,
    SolarMonthIndex,
    LunarMonthIndex,
    LunarDayIndex,
    SolarDayIndex,
    BirthGender,
    Count,
};

struct TransformSet {
    StarId lu;
    StarId quan;
    StarId ke;
    StarId ji;
};

// Stable numeric encoding used by the bundled rule resources. Presentation
// layers may localize these values; the core deliberately carries no labels.
enum class Brightness : int8_t {
    None = -1,
    Xian = 0,
    Bu = 1,
    Ping = 2,
    Li = 3,
    De = 4,
    Wang = 5,
    Miao = 6,
};

constexpr bool is_valid(Brightness value) noexcept {
    return static_cast<int8_t>(value)
            >= static_cast<int8_t>(Brightness::None)
        && static_cast<int8_t>(value)
            <= static_cast<int8_t>(Brightness::Miao);
}

constexpr std::size_t kMaxPlacementInputs = 3u;
// A Jie-bounded solar month can have 32 labeled days, so the largest
// supported two-dimensional month/day table is 12 x 32.
constexpr std::size_t kMaxPlacementTableEntries = 12u * 32u;

struct PlacementRule {
    StarId star_id;
    uint8_t input_count;
    std::array<RuleInputSource, kMaxPlacementInputs> inputs;
    // Row-major strides compiled at the TOML loading boundary.
    std::array<uint16_t, kMaxPlacementInputs> strides;
    uint16_t table_size;
    std::array<uint8_t, kMaxPlacementTableEntries> table;
};

struct CompiledPlacementRules {
    std::vector<PlacementRule> natal;
    std::vector<PlacementRule> flow;
};

struct CompiledBrightnessRules {
    // Indexed first by StarId, then by Branch. -1 means not applicable.
    std::vector<std::array<int8_t, kBranchCount> > values;
};

struct CompiledTransformationRules {
    std::array<TransformSet, kStemCount> by_stem;
};

struct CompiledMasterRules {
    bool enabled;
    std::array<StarId, kBranchCount> life;
    std::array<StarId, kBranchCount> body;
};

struct CompiledRules {
    uint32_t format_version;
    std::size_t star_count;
    uint64_t registry_fingerprint;
    std::size_t natal_star_count;
    CompiledPlacementRules placement;
    CompiledBrightnessRules brightness;
    CompiledTransformationRules sihua;
    CompiledMasterRules masters;
};

std::size_t rule_input_domain_size(RuleInputSource source) noexcept;

bool read_rule_input(
    RuleInputSource source,
    const CalendarFacts& facts,
    const Anchors& anchors,
    Branch body_palace,
    uint8_t* out
) noexcept;

bool evaluate_placement(
    const PlacementRule& rule,
    const CalendarFacts& facts,
    const Anchors& anchors,
    Branch body_palace,
    Branch* out
) noexcept;

// Flow stars use only the layer coordinate's stem/branch plus natal gender.
// Keeping this evaluator separate avoids pretending that a hybrid flow
// coordinate is a valid sexagenary Ganzhi.
bool evaluate_flow_placement(
    const PlacementRule& rule,
    const FlowCoordinate& coordinate,
    Gender natal_gender,
    Branch* out
) noexcept;

bool validate_compiled_rules(
    const CompiledRules& rules,
    std::size_t registry_size
) noexcept;

bool compiled_rules_match_registry(
    const CompiledRules& rules,
    const StarRegistry& registry
) noexcept;

Status brightness_at(
    const CompiledRules& rules,
    StarId star,
    Branch branch,
    Brightness* out
) noexcept;

}  // namespace ziwei
}  // namespace taiyin

#endif  // TAIYIN_ZIWEI_RULES_H
