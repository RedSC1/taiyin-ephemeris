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

struct PlacementRule {
    StarId star_id;
    std::vector<RuleInputSource> inputs;
    // Row-major strides compiled at the configuration loading boundary.
    std::vector<std::size_t> strides;
    std::vector<uint8_t> table;
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

enum class MasterLookupSource : uint8_t {
    LifePalace = 0,
    SelectedYearBranch = 1,
    LunarYearBranch = 2,
    SolarYearBranch = 3,
};

struct CompiledMasterRules {
    bool enabled;
    MasterLookupSource life_input = MasterLookupSource::LifePalace;
    MasterLookupSource body_input = MasterLookupSource::SelectedYearBranch;
    std::array<StarId, kBranchCount> life;
    std::array<StarId, kBranchCount> body;
};

struct CompiledRules {
    uint32_t format_version;
    std::size_t star_count;
    uint64_t registry_fingerprint;
    // Count only. It is not an ID boundary after custom stars are appended.
    std::size_t natal_star_count;
    // Indexed by StarId. Keeping scope per star allows a ruleset-local natal
    // star to be appended after the bundled flow-star range without changing
    // any stable built-in StarId.
    std::vector<uint8_t> natal_by_star;
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

// Bundled flow stars use the layer coordinate plus natal gender. Custom rules
// may additionally read immutable natal anchors/body metadata when supplied.
// Keeping this evaluator separate avoids pretending that a hybrid flow
// coordinate is a valid sexagenary Ganzhi.
bool evaluate_flow_placement(
    const PlacementRule& rule,
    const FlowCoordinate& coordinate,
    Gender natal_gender,
    Branch* out,
    const Anchors* natal_anchors = NULL,
    Branch body_palace = static_cast<Branch>(UINT8_C(0xff))
) noexcept;

bool validate_compiled_rules(
    const CompiledRules& rules,
    std::size_t registry_size
) noexcept;

// Builds the compatibility identity stored in registry_fingerprint. The
// registry fingerprint anchors StarId metadata; the compiled tables ensure
// that charts cannot cross contexts whose stars match but whose rules differ.
uint64_t compiled_rules_fingerprint(
    const CompiledRules& rules,
    uint64_t star_registry_fingerprint
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
