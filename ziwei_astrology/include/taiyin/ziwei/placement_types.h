#ifndef TAIYIN_ZIWEI_PLACEMENT_TYPES_H
#define TAIYIN_ZIWEI_PLACEMENT_TYPES_H
#include "taiyin/ziwei/rules.h"
namespace taiyin { namespace ziwei {

// Wide integers are validated before narrowing. These are effective placement
// parameters, not dates: no leap-month, clock, or rat-hour conversion is done.
struct PlacementInput {
    int32_t year_stem = 0;
    int32_t year_branch = 0;
    int32_t month = 1;
    int32_t day = 1;
    int32_t hour_branch = 0;
};

// -1 means unchanged. update_bureau: -1=inherited, 0=original, 1=recompute.
struct PlacementPatch {
    int32_t year_stem = -1;
    int32_t year_branch = -1;
    int32_t month = -1;
    int32_t day = -1;
    int32_t hour_branch = -1;
    int32_t update_bureau = -1;
};

struct PlacementModification {
    PlacementPatch overrides;
    bool update_bureau = false;
    uint8_t life_palace_shift = 0;
};

struct OmittedPlacement {
    StarId star_id;
    std::vector<RuleInputSource> missing_inputs;
};
} }
#endif
