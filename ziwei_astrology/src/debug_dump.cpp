#include "taiyin/ziwei/debug_dump.h"

#include <new>
#include <utility>

namespace taiyin {
namespace ziwei {
namespace {

void append_transform(
    const TransformSet& value,
    std::vector<int64_t>* out
) {
    out->push_back(value.lu);
    out->push_back(value.quan);
    out->push_back(value.ke);
    out->push_back(value.ji);
}

bool valid_transform(
    const TransformSet& value,
    std::size_t star_count
) noexcept {
    return value.lu < star_count
        && value.quan < star_count
        && value.ke < star_count
        && value.ji < star_count;
}

bool append_positions(
    const std::array<PalaceState, kBranchCount>& palaces,
    std::size_t star_count,
    std::vector<int64_t>* out
) {
    std::vector<int64_t> positions(star_count, -1);
    for (std::size_t branch = 0u; branch < palaces.size(); ++branch) {
        if (palaces[branch].stars.size() != star_count) return false;
        for (std::size_t star = 0u; star < star_count; ++star) {
            if (!palaces[branch].stars.test(star)) continue;
            if (positions[star] != -1) return false;
            positions[star] = static_cast<int64_t>(branch);
        }
    }
    out->insert(out->end(), positions.begin(), positions.end());
    return true;
}

bool append_positions(
    const std::array<DynamicBitset, kBranchCount>& palaces,
    std::size_t star_count,
    std::vector<int64_t>* out
) {
    std::vector<int64_t> positions(star_count, -1);
    for (std::size_t branch = 0u; branch < palaces.size(); ++branch) {
        if (palaces[branch].size() != star_count) return false;
        for (std::size_t star = 0u; star < star_count; ++star) {
            if (!palaces[branch].test(star)) continue;
            if (positions[star] != -1) return false;
            positions[star] = static_cast<int64_t>(branch);
        }
    }
    out->insert(out->end(), positions.begin(), positions.end());
    return true;
}

bool valid_limit(const LimitCoordinate& value, FlowLevel level) noexcept {
    return value.level == level
        && is_valid(value.level)
        && is_valid(value.coordinate)
        && is_valid(value.natal_role);
}

void append_limit(
    const LimitCoordinate& value,
    std::vector<int64_t>* out
) {
    out->push_back(to_index(value.level));
    out->push_back(to_index(value.coordinate.stem));
    out->push_back(to_index(value.coordinate.branch));
    out->push_back(to_index(value.natal_role));
}

}  // namespace

Status dump_chart_numeric(
    const Chart& chart,
    std::vector<int64_t>* out
) noexcept {
    if (out == NULL
        || !validate_anchors(chart.natal.anchors)
        || !is_valid(chart.natal.body_palace)
        || !is_valid(chart.natal.gender)
        || chart.natal.rule_registry_fingerprint == 0u
        || chart.flow_stack.size() > kFlowLevelCount) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    try {
        const std::size_t star_count = chart.natal.palaces[0].stars.size();
        if (star_count == 0u
            || star_count >= static_cast<std::size_t>(kInvalidStarId)
            || !valid_transform(chart.natal.transformations.birth_year, star_count)
            || chart.natal.transformations.marks_by_star.size() != star_count
            || (chart.natal.life_master != kInvalidStarId
                && chart.natal.life_master >= star_count)
            || (chart.natal.body_master != kInvalidStarId
                && chart.natal.body_master >= star_count)) {
            return TAIYIN_ERROR_INVALID_ARGUMENT;
        }

        std::vector<int64_t> result;
        result.reserve(55u + star_count
            + chart.flow_stack.size() * (8u + star_count));
        result.push_back(kNumericDumpFormatVersion);
        result.push_back(static_cast<uint8_t>(NumericDumpKind::Chart));
        result.push_back(static_cast<int64_t>(star_count));
        result.push_back(static_cast<int64_t>(chart.flow_stack.size()));
        const std::array<uint8_t, kAnchorCount> anchors =
            flatten_anchors(chart.natal.anchors);
        for (std::size_t i = 0u; i < anchors.size(); ++i) {
            result.push_back(anchors[i]);
        }
        result.push_back(to_index(chart.natal.body_palace));
        result.push_back(to_index(chart.natal.gender));
        result.push_back(chart.natal.life_master == kInvalidStarId
            ? -1 : static_cast<int64_t>(chart.natal.life_master));
        result.push_back(chart.natal.body_master == kInvalidStarId
            ? -1 : static_cast<int64_t>(chart.natal.body_master));
        for (std::size_t branch = 0u;
             branch < chart.natal.palace_stems.size(); ++branch) {
            if (!is_valid(chart.natal.palace_stems[branch])) {
                return TAIYIN_ERROR_INVALID_ARGUMENT;
            }
            result.push_back(to_index(chart.natal.palace_stems[branch]));
        }
        append_transform(chart.natal.transformations.birth_year, &result);
        if (!append_positions(chart.natal.palaces, star_count, &result)) {
            return TAIYIN_ERROR_INVALID_ARGUMENT;
        }
        for (std::size_t star = 0u; star < star_count; ++star) {
            result.push_back(star_transform_mask(chart.natal,
                static_cast<StarId>(star)));
        }

        for (std::size_t i = 0u; i < chart.flow_stack.size(); ++i) {
            const FlowLayer& layer = chart.flow_stack[i];
            if (!is_valid(layer.level)
                || to_index(layer.level) != i
                || layer.rule_registry_fingerprint
                    != chart.natal.rule_registry_fingerprint
                || !is_valid(layer.life_palace)
                || !is_valid(layer.coordinate)
                || layer.life_palace != layer.coordinate.branch
                || !valid_transform(layer.transforms, star_count)) {
                return TAIYIN_ERROR_INVALID_ARGUMENT;
            }
            result.push_back(to_index(layer.level));
            result.push_back(to_index(layer.life_palace));
            result.push_back(to_index(layer.coordinate.stem));
            result.push_back(to_index(layer.coordinate.branch));
            append_transform(layer.transforms, &result);
            if (!append_positions(layer.stars, star_count, &result)) {
                return TAIYIN_ERROR_INVALID_ARGUMENT;
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

Status dump_resolved_flow_numeric(
    const ResolvedFlow& flow,
    std::vector<int64_t>* out
) noexcept {
    if (out == NULL
        || flow.target_month < 1u || flow.target_month > 12u
        || flow.target_month_sequence < 1u
        || flow.target_month_sequence > 15u
        || flow.target_month_name
            > chinese_calendar::TAIYIN_CHINESE_MONTH_NAME_LATER_SAME_NAME
        || !is_valid(flow.target_month_building_branch)
        || flow.target_day < 1u || flow.target_day > kMaxFlowDayIndex
        || flow.target_hour_index >= kBranchCount
        || !is_valid(flow.target_rat_hour_segment)
        || flow.effective_target_year < flow.effective_birth_year
        || !valid_limit(flow.decade.limit, FlowLevel::Decade)
        || !is_valid(flow.small_limit.coordinate)
        || !is_valid(flow.small_limit.natal_role)
        || !valid_limit(flow.year.limit, FlowLevel::Year)
        || !valid_limit(flow.month.limit, FlowLevel::Month)
        || !valid_limit(flow.day.limit, FlowLevel::Day)
        || !valid_limit(flow.hour.limit, FlowLevel::Hour)
        || !is_valid(flow.month.doujun)
        || !is_valid(flow.month.month_building_branch)
        || flow.month.effective_month < 1u
        || flow.month.effective_month > 12u
        || flow.month.palace_month_index < 1u
        || flow.month.palace_month_index > 15u
        || flow.year.year != flow.effective_target_year
        || flow.month.effective_year != flow.effective_target_year
        || flow.month.month != flow.target_month
        || flow.month.sequence != flow.target_month_sequence
        || flow.month.is_leap != flow.target_month_is_leap
        || flow.day.day != flow.target_day
        || flow.hour.hour_index != flow.target_hour_index
        || flow.hour.rat_hour_segment != flow.target_rat_hour_segment
        || flow.decade.is_childhood != (flow.decade.index == 0u)) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    try {
        std::vector<int64_t> result;
        result.reserve(54u);
        result.push_back(kNumericDumpFormatVersion);
        result.push_back(static_cast<uint8_t>(NumericDumpKind::ResolvedFlow));
        result.push_back(flow.effective_birth_year);
        result.push_back(flow.effective_target_year);
        result.push_back(flow.target_month);
        result.push_back(flow.target_month_sequence);
        result.push_back(to_index(flow.target_month_building_branch));
        result.push_back(flow.target_day);
        result.push_back(flow.target_hour_index);
        result.push_back(to_index(flow.target_rat_hour_segment));
        result.push_back(flow.target_month_is_leap ? 1 : 0);
        result.push_back(flow.target_month_name);
        result.push_back(flow.month.year);
        result.push_back(flow.month.effective_month);
        result.push_back(flow.month.palace_month_index);

        append_limit(flow.decade.limit, &result);
        result.push_back(flow.decade.index);
        result.push_back(flow.decade.start_age);
        result.push_back(flow.decade.end_age);
        result.push_back(flow.decade.start_year);
        result.push_back(flow.decade.end_year);
        result.push_back(flow.decade.is_childhood ? 1 : 0);

        result.push_back(to_index(flow.small_limit.coordinate.stem));
        result.push_back(to_index(flow.small_limit.coordinate.branch));
        result.push_back(to_index(flow.small_limit.natal_role));
        result.push_back(flow.small_limit.virtual_age);

        append_limit(flow.year.limit, &result);
        result.push_back(flow.year.year);

        append_limit(flow.month.limit, &result);
        result.push_back(flow.month.year);
        result.push_back(flow.month.month);
        result.push_back(flow.month.sequence);
        result.push_back(flow.month.is_leap ? 1 : 0);
        result.push_back(to_index(flow.month.doujun));

        append_limit(flow.day.limit, &result);
        result.push_back(flow.day.day);

        append_limit(flow.hour.limit, &result);
        result.push_back(flow.hour.hour_index);
        result.push_back(to_index(flow.hour.rat_hour_segment));
        *out = std::move(result);
        return TAIYIN_STATUS_OK;
    } catch (const std::bad_alloc&) {
        return TAIYIN_ERROR_OUT_OF_MEMORY;
    } catch (...) {
        return TAIYIN_ERROR_INTERNAL;
    }
}

}  // namespace ziwei
}  // namespace taiyin
