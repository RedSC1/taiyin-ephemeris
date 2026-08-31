#ifndef TAIYIN_ZIWEI_RULE_MODULES_INTERNAL_H
#define TAIYIN_ZIWEI_RULE_MODULES_INTERNAL_H

#include "taiyin/ziwei/rule_modules.h"
#include "taiyin/ziwei/rules.h"
#include "taiyin/ziwei/star_registry.h"

#include <array>
#include <map>
#include <string>
#include <vector>

namespace taiyin {
namespace ziwei {
namespace detail {

struct RuleStarDefinition {
    std::string key;
    StarCategory category;
    bool has_category;
    bool natal;
};

struct RuleStarReference {
    bool by_id;
    StarId id;
    std::string key;
};

struct RuleTransformPatch {
    std::array<uint8_t, 4u> present;
    std::array<RuleStarReference, 4u> stars;
};

struct RuleMasterPatch {
    bool has_life;
    bool has_body;
    MasterLookupSource life_input;
    MasterLookupSource body_input;
    std::array<RuleStarReference, kBranchCount> life;
    std::array<RuleStarReference, kBranchCount> body;
};

struct ZiweiRuleModuleData {
    std::string label;
    std::vector<RuleStarDefinition> stars;
    std::map<std::string, PlacementRule> natal_placements;
    std::map<std::string, PlacementRule> flow_placements;
    std::map<std::string, std::array<int8_t, kBranchCount> > brightness;
    std::map<std::string, RuleTransformPatch> sihua;
    RuleMasterPatch masters;
};

struct ZiweiRuleModuleAccess {
    static const ZiweiRuleModuleData& get(const ZiweiRuleModule& module) {
        return *module.data_;
    }
};

}  // namespace detail
}  // namespace ziwei
}  // namespace taiyin

#endif  // TAIYIN_ZIWEI_RULE_MODULES_INTERNAL_H
