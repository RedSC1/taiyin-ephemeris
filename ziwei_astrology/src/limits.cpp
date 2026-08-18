#include "taiyin/ziwei/limits.h"

#include <cstdint>
#include <limits>
#include <utility>

namespace taiyin {
namespace ziwei {
namespace {

int normalized(int64_t value, int modulus) noexcept {
    const int64_t result = value % modulus;
    return static_cast<int>(result < 0 ? result + modulus : result);
}

Stem stem_for_year(int32_t year) noexcept {
    return static_cast<Stem>(normalized(static_cast<int64_t>(year) + 6, 10));
}

Branch branch_for_year(int32_t year) noexcept {
    return static_cast<Branch>(normalized(static_cast<int64_t>(year) + 8, 12));
}

bool checked_int32(int64_t value, int32_t* out) noexcept {
    if (out == NULL
        || value < std::numeric_limits<int32_t>::min()
        || value > std::numeric_limits<int32_t>::max()) {
        return false;
    }
    *out = static_cast<int32_t>(value);
    return true;
}

bool natal_is_valid(const NatalChart& natal) noexcept {
    if (!validate_anchors(natal.anchors)
        || !is_valid(natal.body_palace)
        || !is_valid(natal.gender)) {
        return false;
    }
    for (std::size_t i = 0u; i < natal.palace_stems.size(); ++i) {
        if (!is_valid(natal.palace_stems[i])) return false;
    }
    return true;
}

bool natal_role_for_branch(
    const NatalChart& natal,
    Branch branch,
    PalaceId* out
) noexcept {
    if (out == NULL || !is_valid(branch)) return false;
    for (std::size_t i = 0u; i < natal.anchors.palace_positions.size(); ++i) {
        if (natal.anchors.palace_positions[i] == branch) {
            *out = static_cast<PalaceId>(i);
            return true;
        }
    }
    return false;
}

bool make_limit_coordinate(
    const NatalChart& natal,
    FlowLevel level,
    FlowCoordinate coordinate,
    LimitCoordinate* out
) noexcept {
    if (out == NULL
        || !is_valid(level)
        || !is_valid(coordinate)) {
        return false;
    }
    PalaceId role = PalaceId::Life;
    if (!natal_role_for_branch(natal, coordinate.branch, &role)) return false;
    out->level = level;
    out->coordinate = coordinate;
    out->natal_role = role;
    return true;
}

FlowCoordinate palace_coordinate(
    const NatalChart& natal,
    Branch branch
) noexcept {
    FlowCoordinate result;
    result.stem = natal.palace_stems[to_index(branch)];
    result.branch = branch;
    return result;
}

Status make_flow_month_with_month_stem_offset(
    const NatalChart& natal,
    int32_t physical_year,
    uint8_t logical_month,
    uint8_t sequence,
    bool is_leap,
    int month_stem_offset,
    uint8_t birth_effective_month,
    Branch birth_hour,
    FlowMonthLimit* out
) noexcept {
    if (out == NULL
        || !natal_is_valid(natal)
        || logical_month < 1u || logical_month > 12u
        || sequence < 1u || sequence > 13u
        || month_stem_offset < 0 || month_stem_offset > 12
        || birth_effective_month < 1u || birth_effective_month > 12u
        || !is_valid(birth_hour)) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    const Stem year_stem = stem_for_year(physical_year);
    const Branch year_life = branch_for_year(physical_year);
    const Branch doujun = advance_branch(
        year_life,
        -static_cast<int>(birth_effective_month - 1u)
            + static_cast<int>(to_index(birth_hour)));
    const Branch target = advance_branch(doujun, sequence - 1u);
    const int start_tiger = (to_index(year_stem) % 5u) * 2u + 2u;
    const Stem month_stem = static_cast<Stem>(
        normalized(start_tiger + month_stem_offset, 10));

    FlowMonthLimit result;
    result.year = physical_year;
    result.month = logical_month;
    result.sequence = sequence;
    result.is_leap = is_leap;
    result.doujun = doujun;
    const FlowCoordinate coordinate = {month_stem, target};
    if (!make_limit_coordinate(
            natal, FlowLevel::Month, coordinate, &result.limit)) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    *out = result;
    return TAIYIN_STATUS_OK;
}

}  // namespace

Status make_decade_by_index(
    const NatalChart& natal,
    int32_t effective_birth_year,
    uint16_t index,
    DecadeLimit* out
) noexcept {
    if (out == NULL || index == 0u || !natal_is_valid(natal)) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    const uint8_t start_age = bureau_number(natal.anchors.bureau);
    if (start_age == 0u) return TAIYIN_ERROR_INVALID_ARGUMENT;

    const int64_t offset = static_cast<int64_t>(index) - 1;
    const bool forward = is_forward(stem_for_year(effective_birth_year), natal.gender);
    const Branch natal_life =
        natal.anchors.palace_positions[to_index(PalaceId::Life)];
    const Branch target = advance_branch(
        natal_life, static_cast<int>(forward ? offset : -offset));

    DecadeLimit result;
    result.index = index;
    result.is_childhood = false;
    if (!checked_int32(static_cast<int64_t>(start_age) + offset * 10,
            &result.start_age)
        || !checked_int32(static_cast<int64_t>(start_age) + offset * 10 + 9,
            &result.end_age)
        || !checked_int32(static_cast<int64_t>(effective_birth_year)
                + start_age - 1 + offset * 10,
            &result.start_year)
        || !checked_int32(static_cast<int64_t>(effective_birth_year)
                + start_age - 1 + offset * 10 + 9,
            &result.end_year)
        || !make_limit_coordinate(
            natal,
            FlowLevel::Decade,
            palace_coordinate(natal, target),
            &result.limit)) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    *out = result;
    return TAIYIN_STATUS_OK;
}

Status make_childhood_decade(
    const NatalChart& natal,
    int32_t effective_birth_year,
    int32_t target_year,
    ChildhoodStrategy strategy,
    DecadeLimit* out
) noexcept {
    if (out == NULL || !natal_is_valid(natal) || !is_valid(strategy)) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    const int64_t virtual_age64 = static_cast<int64_t>(target_year)
        - effective_birth_year + 1;
    const uint8_t start_age = bureau_number(natal.anchors.bureau);
    if (virtual_age64 < 1 || virtual_age64 >= start_age) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }

    static const int kSkipOffsets[6] = {0, 4, 5, 2, 10, 8};
    const Branch natal_life =
        natal.anchors.palace_positions[to_index(PalaceId::Life)];
    int step = 0;
    if (strategy == ChildhoodStrategy::Skip) {
        step = -kSkipOffsets[static_cast<std::size_t>(virtual_age64 - 1)];
    } else {
        const bool forward = is_forward(
            stem_for_year(effective_birth_year), natal.gender);
        step = static_cast<int>((virtual_age64 - 1) * (forward ? 1 : -1));
    }
    const Branch target = advance_branch(natal_life, step);

    DecadeLimit result;
    result.index = 0u;
    result.start_age = static_cast<int32_t>(virtual_age64);
    result.end_age = result.start_age;
    result.start_year = target_year;
    result.end_year = target_year;
    result.is_childhood = true;
    if (!make_limit_coordinate(
            natal,
            FlowLevel::Decade,
            palace_coordinate(natal, target),
            &result.limit)) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    *out = result;
    return TAIYIN_STATUS_OK;
}

Status make_decade_for_year(
    const NatalChart& natal,
    int32_t effective_birth_year,
    int32_t target_year,
    ChildhoodStrategy strategy,
    DecadeLimit* out
) noexcept {
    if (out == NULL || !natal_is_valid(natal) || !is_valid(strategy)) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    const uint8_t start_age = bureau_number(natal.anchors.bureau);
    const int64_t start_year = static_cast<int64_t>(effective_birth_year)
        + start_age - 1;
    if (target_year < effective_birth_year) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    if (target_year < start_year) {
        return make_childhood_decade(
            natal, effective_birth_year, target_year, strategy, out);
    }
    const int64_t index64 = (static_cast<int64_t>(target_year) - start_year)
        / 10 + 1;
    if (index64 > std::numeric_limits<uint16_t>::max()) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    return make_decade_by_index(
        natal, effective_birth_year, static_cast<uint16_t>(index64), out);
}

Status make_small_limit(
    const NatalChart& natal,
    Branch birth_solar_year_branch,
    int32_t virtual_age,
    SmallLimit* out
) noexcept {
    if (out == NULL
        || !natal_is_valid(natal)
        || !is_valid(birth_solar_year_branch)
        || virtual_age < 1) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    static const Branch kStartByGroup[4] = {
        Branch::Xu, Branch::Wei, Branch::Chen, Branch::Chou,
    };
    const Branch start = kStartByGroup[to_index(birth_solar_year_branch) % 4u];
    const int64_t signed_steps = static_cast<int64_t>(virtual_age - 1)
        * (natal.gender == Gender::Male ? 1 : -1);
    const Branch target = static_cast<Branch>(normalized(
        static_cast<int64_t>(to_index(start)) + signed_steps, 12));

    SmallLimit result;
    result.coordinate = palace_coordinate(natal, target);
    result.virtual_age = virtual_age;
    if (!natal_role_for_branch(natal, target, &result.natal_role)) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    *out = result;
    return TAIYIN_STATUS_OK;
}

Status make_flow_year(
    const NatalChart& natal,
    int32_t physical_year,
    FlowYearLimit* out
) noexcept {
    if (out == NULL || !natal_is_valid(natal)) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    FlowYearLimit result;
    result.year = physical_year;
    const FlowCoordinate coordinate = {
        stem_for_year(physical_year), branch_for_year(physical_year),
    };
    if (!make_limit_coordinate(
            natal, FlowLevel::Year, coordinate, &result.limit)) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    *out = result;
    return TAIYIN_STATUS_OK;
}

Status make_flow_month(
    const NatalChart& natal,
    int32_t physical_year,
    uint8_t logical_month,
    uint8_t sequence,
    bool is_leap,
    uint8_t birth_effective_month,
    Branch birth_hour,
    FlowMonthLimit* out
) noexcept {
    // This legacy entry point intentionally retains the sequence-derived
    // Wu-Hu-Dun offset.  In particular sequence 13 represents the thirteenth
    // physical month and advances the stem by twelve positions; reducing it
    // to a twelve-branch value would incorrectly wrap it back to Yin.
    return make_flow_month_with_month_stem_offset(
        natal,
        physical_year,
        logical_month,
        sequence,
        is_leap,
        static_cast<int>(sequence) - 1,
        birth_effective_month,
        birth_hour,
        out);
}

Status make_flow_month_from_lunar_month_branch(
    const NatalChart& natal,
    int32_t physical_year,
    uint8_t logical_month,
    uint8_t sequence,
    bool is_leap,
    Branch lunar_month_branch,
    uint8_t birth_effective_month,
    Branch birth_hour,
    FlowMonthLimit* out
) noexcept {
    if (!is_valid(lunar_month_branch)) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    // Normalize in the twelve-branch cycle *before* applying the ten-stem
    // cycle.  Chou is one branch before Yin numerically, but is the twelfth
    // month-building position and therefore advances the Wu-Hu-Dun stem by
    // eleven steps rather than subtracting one.
    const int month_offset = normalized(
        static_cast<int>(to_index(lunar_month_branch))
            - static_cast<int>(to_index(Branch::Yin)),
        12);
    return make_flow_month_with_month_stem_offset(
        natal,
        physical_year,
        logical_month,
        sequence,
        is_leap,
        month_offset,
        birth_effective_month,
        birth_hour,
        out);
}

Status make_flow_day(
    const NatalChart& natal,
    const FlowMonthLimit& month,
    uint8_t day_index,
    Stem physical_day_stem,
    FlowDayLimit* out
) noexcept {
    if (out == NULL
        || !natal_is_valid(natal)
        || month.limit.level != FlowLevel::Month
        || !is_valid(month.limit.coordinate)
        || day_index < 1u || day_index > kMaxFlowDayIndex
        || !is_valid(physical_day_stem)) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    const Branch target = advance_branch(
        month.limit.coordinate.branch, day_index - 1u);
    FlowDayLimit result;
    result.day = day_index;
    const FlowCoordinate coordinate = {physical_day_stem, target};
    if (!make_limit_coordinate(
            natal, FlowLevel::Day, coordinate, &result.limit)) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    *out = result;
    return TAIYIN_STATUS_OK;
}

namespace {

Status make_flow_hour_with_stem(
    const NatalChart& natal,
    const FlowDayLimit& day,
    uint8_t hour_index,
    Stem hour_stem,
    RatHourSegment rat_hour_segment,
    FlowHourLimit* out
) noexcept {
    const bool is_rat = hour_index == to_index(Branch::Zi);
    const bool segment_matches = is_rat
        ? rat_hour_segment != RatHourSegment::None
        : rat_hour_segment == RatHourSegment::None;
    if (out == NULL
        || !natal_is_valid(natal)
        || day.limit.level != FlowLevel::Day
        || !is_valid(day.limit.coordinate)
        || hour_index >= kBranchCount
        || !is_valid(hour_stem)
        || !is_valid(rat_hour_segment)
        || !segment_matches) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    const Branch target = advance_branch(
        day.limit.coordinate.branch, hour_index);
    FlowHourLimit result;
    result.hour_index = hour_index;
    result.rat_hour_segment = rat_hour_segment;
    const FlowCoordinate coordinate = {hour_stem, target};
    if (!make_limit_coordinate(
            natal, FlowLevel::Hour, coordinate, &result.limit)) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    *out = result;
    return TAIYIN_STATUS_OK;
}

}  // namespace

Status make_flow_hour(
    const NatalChart& natal,
    const FlowDayLimit& day,
    uint8_t hour_index,
    FlowHourLimit* out
) noexcept {
    if (hour_index >= kBranchCount) return TAIYIN_ERROR_INVALID_ARGUMENT;
    const Stem hour_stem = static_cast<Stem>(
        ((to_index(day.limit.coordinate.stem) % 5u) * 2u + hour_index) % 10u);
    return make_flow_hour_with_stem(
        natal,
        day,
        hour_index,
        hour_stem,
        hour_index == 0u ? RatHourSegment::Unified : RatHourSegment::None,
        out);
}

Status make_flow_hour_from_pillar(
    const NatalChart& natal,
    const FlowDayLimit& day,
    const Ganzhi& physical_hour,
    RatHourSegment rat_hour_segment,
    FlowHourLimit* out
) noexcept {
    if (!is_valid(physical_hour)) return TAIYIN_ERROR_INVALID_ARGUMENT;
    return make_flow_hour_with_stem(
        natal,
        day,
        to_index(physical_hour.branch),
        physical_hour.stem,
        rat_hour_segment,
        out);
}

Status make_limit_flow_layer(
    const LimitCoordinate& limit,
    const NatalChart& natal,
    const CompiledRules& rules,
    FlowLayer* out
) noexcept {
    if (!is_valid(limit.level)
        || !is_valid(limit.coordinate)
        || !is_valid(limit.natal_role)) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    return make_flow_layer(
        limit.level,
        limit.coordinate,
        natal,
        rules,
        out);
}

Status push_limit_flow_layer(
    Chart* chart,
    const LimitCoordinate& limit,
    const CompiledRules& rules
) noexcept {
    if (chart == NULL) return TAIYIN_ERROR_INVALID_ARGUMENT;
    FlowLayer layer;
    const Status status = make_limit_flow_layer(
        limit, chart->natal, rules, &layer);
    if (status != TAIYIN_STATUS_OK) return status;
    return push_flow_layer(chart, std::move(layer));
}

}  // namespace ziwei
}  // namespace taiyin
