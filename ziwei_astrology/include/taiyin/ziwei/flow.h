#ifndef TAIYIN_ZIWEI_FLOW_H
#define TAIYIN_ZIWEI_FLOW_H

#include "taiyin/status.h"
#include "taiyin/ziwei/chart.h"
#include "taiyin/ziwei/dynamic_bitset.h"
#include "taiyin/ziwei/rules.h"
#include "taiyin/ziwei/types.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace taiyin {
namespace ziwei {

struct FlowLayer {
    // StarId values in this layer belong to one immutable rule registry.
    // Keeping the identity on the layer prevents same-sized catalogs from
    // being accidentally mixed when a layer is pushed into a chart.
    uint64_t rule_registry_fingerprint;
    FlowLevel level;
    Branch life_palace;
    FlowCoordinate coordinate;
    std::array<DynamicBitset, kBranchCount> stars;
    TransformSet transforms;
};

struct Chart {
    NatalChart natal;
    std::vector<FlowLayer> flow_stack;
};

Status initialize_flow_layer(
    FlowLevel level,
    FlowCoordinate coordinate,
    uint64_t rule_registry_fingerprint,
    std::size_t star_count,
    const TransformSet& transforms,
    FlowLayer* out
) noexcept;

// Builds one flow layer from the same flattened answer tables used by natal
// charts. The supplied coordinate exposes a transformation stem and a physical
// palace branch without imposing sexagenary-pair parity.
Status make_flow_layer(
    FlowLevel level,
    FlowCoordinate coordinate,
    const NatalChart& natal,
    const CompiledRules& rules,
    FlowLayer* out
) noexcept;

// One branch index per StarId. Stars not present in this layer are encoded as
// 0xff, matching dump_natal_star_positions() for differential tests.
Status dump_flow_star_positions(
    const FlowLayer& layer,
    std::vector<uint8_t>* out
) noexcept;

// Layers must be pushed in contiguous Decade->Year->Month->Day->Hour order.
Status push_flow_layer(Chart* chart, FlowLayer layer) noexcept;

// Removes first_removed and every more-specific layer. This is the common
// cascade operation used when an upper-level time selection changes.
Status truncate_flow_stack(
    Chart* chart,
    FlowLevel first_removed
) noexcept;

}  // namespace ziwei
}  // namespace taiyin

#endif  // TAIYIN_ZIWEI_FLOW_H
