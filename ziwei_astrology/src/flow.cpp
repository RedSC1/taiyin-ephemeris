#include "taiyin/ziwei/flow.h"

#include <new>
#include <utility>

namespace taiyin {
namespace ziwei {
namespace {

bool valid_transform_set(
    const TransformSet& transforms,
    std::size_t star_count
) noexcept {
    return transforms.lu < star_count
        && transforms.quan < star_count
        && transforms.ke < star_count
        && transforms.ji < star_count;
}

bool unique_star_positions(
    const std::array<DynamicBitset, kBranchCount>& palaces,
    std::size_t star_count
) {
    for (std::size_t branch = 0u; branch < palaces.size(); ++branch) {
        if (palaces[branch].size() != star_count) return false;
    }
    for (std::size_t star = 0u; star < star_count; ++star) {
        bool seen = false;
        for (std::size_t branch = 0u; branch < palaces.size(); ++branch) {
            if (!palaces[branch].test(star)) continue;
            if (seen) return false;
            seen = true;
        }
    }
    return true;
}

bool unique_star_positions(
    const std::array<PalaceState, kBranchCount>& palaces,
    std::size_t star_count
) {
    for (std::size_t branch = 0u; branch < palaces.size(); ++branch) {
        if (palaces[branch].stars.size() != star_count) return false;
    }
    for (std::size_t star = 0u; star < star_count; ++star) {
        bool seen = false;
        for (std::size_t branch = 0u; branch < palaces.size(); ++branch) {
            if (!palaces[branch].stars.test(star)) continue;
            if (seen) return false;
            seen = true;
        }
    }
    return true;
}

bool valid_layer_for_chart(
    const FlowLayer& layer,
    std::size_t expected_level,
    std::size_t star_count,
    uint64_t registry_fingerprint
) {
    return is_valid(layer.level)
        && to_index(layer.level) == expected_level
        && is_valid(layer.life_palace)
        && is_valid(layer.coordinate)
        && layer.life_palace == layer.coordinate.branch
        && layer.rule_registry_fingerprint == registry_fingerprint
        && valid_transform_set(layer.transforms, star_count)
        && unique_star_positions(layer.stars, star_count);
}

}  // namespace

Status initialize_flow_layer(
    FlowLevel level,
    FlowCoordinate coordinate,
    uint64_t rule_registry_fingerprint,
    std::size_t star_count,
    const TransformSet& transforms,
    FlowLayer* out
) noexcept {
    if (out == NULL
        || !is_valid(level)
        || !is_valid(coordinate)
        || rule_registry_fingerprint == 0u
        || !valid_transform_set(transforms, star_count)) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    try {
        FlowLayer result;
        result.rule_registry_fingerprint = rule_registry_fingerprint;
        result.level = level;
        result.life_palace = coordinate.branch;
        result.coordinate = coordinate;
        result.transforms = transforms;
        for (std::size_t branch = 0u; branch < result.stars.size(); ++branch) {
            result.stars[branch].resize(star_count, false);
        }
        *out = std::move(result);
        return TAIYIN_STATUS_OK;
    } catch (const std::bad_alloc&) {
        return TAIYIN_ERROR_OUT_OF_MEMORY;
    } catch (...) {
        return TAIYIN_ERROR_INTERNAL;
    }
}

Status make_flow_layer(
    FlowLevel level,
    FlowCoordinate coordinate,
    const NatalChart& natal,
    const CompiledRules& rules,
    FlowLayer* out
) noexcept {
    if (out == NULL
        || !is_valid(natal.body_palace)
        || !is_valid(natal.gender)
        || !validate_anchors(natal.anchors)
        || natal.rule_registry_fingerprint == 0u
        || natal.rule_registry_fingerprint != rules.registry_fingerprint
        || !validate_compiled_rules(rules, rules.star_count)) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }

    if (!is_valid(coordinate)) return TAIYIN_ERROR_INVALID_ARGUMENT;
    const TransformSet transforms =
        rules.sihua.by_stem[to_index(coordinate.stem)];
    FlowLayer result;
    Status status = initialize_flow_layer(
        level,
        coordinate,
        rules.registry_fingerprint,
        rules.star_count,
        transforms,
        &result);
    if (status != TAIYIN_STATUS_OK) return status;

    for (std::size_t i = 0u; i < rules.placement.flow.size(); ++i) {
        const PlacementRule& rule = rules.placement.flow[i];
        Branch position = Branch::Zi;
        if (!evaluate_flow_placement(
                rule, coordinate, natal.gender, &position,
                &natal.anchors, natal.body_palace)) {
            return TAIYIN_ERROR_INVALID_ARGUMENT;
        }
        result.stars[to_index(position)].set(rule.star_id);
    }
    *out = std::move(result);
    return TAIYIN_STATUS_OK;
}

Status dump_flow_star_positions(
    const FlowLayer& layer,
    std::vector<uint8_t>* out
) noexcept {
    if (out == NULL || !is_valid(layer.level) || !is_valid(layer.coordinate)) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    try {
        const std::size_t star_count = layer.stars[0].size();
        std::vector<uint8_t> result(star_count, 0xffu);
        for (std::size_t branch = 0u; branch < layer.stars.size(); ++branch) {
            if (layer.stars[branch].size() != star_count) {
                return TAIYIN_ERROR_INVALID_ARGUMENT;
            }
            for (std::size_t star = 0u; star < star_count; ++star) {
                if (!layer.stars[branch].test(star)) continue;
                if (result[star] != 0xffu) return TAIYIN_ERROR_INVALID_ARGUMENT;
                result[star] = static_cast<uint8_t>(branch);
            }
        }
        *out = std::move(result);
        return TAIYIN_STATUS_OK;
    } catch (const std::bad_alloc&) {
        return TAIYIN_ERROR_OUT_OF_MEMORY;
    } catch (...) {
        return TAIYIN_ERROR_INTERNAL;
    }
}

Status push_flow_layer(Chart* chart, FlowLayer layer) noexcept {
    if (chart == NULL || chart->natal.rule_registry_fingerprint == 0u) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    const std::size_t expected_level = chart->flow_stack.size();
    if (expected_level >= kFlowLevelCount
        || to_index(layer.level) != expected_level) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }

    const std::size_t star_count = chart->natal.palaces[0].stars.size();
    if (!unique_star_positions(chart->natal.palaces, star_count)) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    for (std::size_t prior = 0u; prior < chart->flow_stack.size(); ++prior) {
        if (!valid_layer_for_chart(
                chart->flow_stack[prior], prior, star_count,
                chart->natal.rule_registry_fingerprint)) {
            return TAIYIN_ERROR_INVALID_ARGUMENT;
        }
    }
    if (!valid_layer_for_chart(
            layer, expected_level, star_count,
            chart->natal.rule_registry_fingerprint)) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    try {
        chart->flow_stack.push_back(std::move(layer));
        return TAIYIN_STATUS_OK;
    } catch (const std::bad_alloc&) {
        return TAIYIN_ERROR_OUT_OF_MEMORY;
    } catch (...) {
        return TAIYIN_ERROR_INTERNAL;
    }
}

Status truncate_flow_stack(
    Chart* chart,
    FlowLevel first_removed
) noexcept {
    if (chart == NULL || !is_valid(first_removed)) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    const std::size_t keep_count = to_index(first_removed);
    while (chart->flow_stack.size() > keep_count) {
        chart->flow_stack.pop_back();
    }
    return TAIYIN_STATUS_OK;
}

}  // namespace ziwei
}  // namespace taiyin
