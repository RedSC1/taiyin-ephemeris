#ifndef TAIYIN_ZIWEI_PLATE_INTERNAL_H
#define TAIYIN_ZIWEI_PLATE_INTERNAL_H
#include "taiyin/ziwei/chart.h"
namespace taiyin { namespace ziwei { namespace detail {
// Shared by birth and calendar-free plates. Positions remain physical branches.
void decorate_transformations(
    const std::array<PalaceState, kBranchCount>& palaces,
    const std::array<Stem, kBranchCount>& stems,
    const TransformSet& year, const CompiledRules& rules,
    std::vector<StarTransformMask>* masks);
} } }
#endif
