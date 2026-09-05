#include "taiyin/ziwei/anchors.h"

#include <limits>

namespace taiyin {
namespace ziwei {
namespace {

uint8_t normalized_index(int value, int modulus) noexcept {
    return static_cast<uint8_t>(((value % modulus) + modulus) % modulus);
}

Bureau bureau_from_palace_ganzhi(const Ganzhi& palace) noexcept {
    const uint8_t stem_score = to_index(palace.stem) / 2u;
    const uint8_t branch_score = (to_index(palace.branch) / 2u) % 3u;
    switch ((stem_score + branch_score) % 5u) {
    case 0u: return Bureau::Metal4;
    case 1u: return Bureau::Water2;
    case 2u: return Bureau::Fire6;
    case 3u: return Bureau::Earth5;
    default: return Bureau::Wood3;
    }
}

Branch ziwei_position(uint8_t lunar_day, Bureau bureau) noexcept {
    const int bureau_value = bureau_number(bureau);
    int quotient = 0;
    int adjustment = 0;
    if (lunar_day % bureau_value == 0) {
        quotient = lunar_day / bureau_value;
    } else {
        const int to_add = bureau_value - (lunar_day % bureau_value);
        quotient = (lunar_day + to_add) / bureau_value;
        adjustment = (to_add % 2 == 1) ? -to_add : to_add;
    }
    return static_cast<Branch>(normalized_index(quotient + adjustment + 1, 12));
}

void flatten_ganzhi(
    const Ganzhi& value,
    std::array<uint8_t, kAnchorCount>* out,
    std::size_t* cursor
) noexcept {
    (*out)[(*cursor)++] = to_index(value.stem);
    (*out)[(*cursor)++] = to_index(value.branch);
}

void flatten_pillars(
    const Pillars& value,
    std::array<uint8_t, kAnchorCount>* out,
    std::size_t* cursor
) noexcept {
    flatten_ganzhi(value.year, out, cursor);
    flatten_ganzhi(value.month, out, cursor);
    flatten_ganzhi(value.day, out, cursor);
    flatten_ganzhi(value.hour, out, cursor);
}

}  // namespace

AnchorOptions default_anchor_options() noexcept {
    AnchorOptions result = {};
    result.rules = default_natal_rule_options();
    result.chart_mode = ZiweiChartMode::TianPan;
    return result;
}

NatalRuleOptions default_natal_rule_options() noexcept {
    NatalRuleOptions result = {};
    result.wu_hu_dun_year_boundary = PillarBoundary::Lunar;
    result.sihua_year_boundary = PillarBoundary::Lunar;
    result.body_master_year_boundary = PillarBoundary::Lunar;
    return result;
}

Status compute_palace_stems(
    Stem year_stem,
    std::array<Stem, kBranchCount>* out
) noexcept {
    if (out == NULL || !is_valid(year_stem)) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    std::array<Stem, kBranchCount> result = {};
    const int yin_stem = (to_index(year_stem) % 5u) * 2u + 2u;
    for (std::size_t step = 0u; step < kBranchCount; ++step) {
        const Branch branch = advance_branch(Branch::Yin, step);
        result[to_index(branch)] = static_cast<Stem>(
            normalized_index(yin_stem + static_cast<int>(step), 10));
    }
    *out = result;
    return TAIYIN_STATUS_OK;
}

Status resolve_effective_lunar_month(
    const LunarDateFacts& lunar_date,
    LeapMonthStrategy strategy,
    int32_t* out_year,
    uint8_t* out_month
) noexcept {
    if (out_year == NULL || out_month == NULL
        || lunar_date.month < 1u || lunar_date.month > 13u
        || lunar_date.day < 1u || lunar_date.day > 30u
        || lunar_date.is_leap > 1u
        || static_cast<uint8_t>(strategy)
            > static_cast<uint8_t>(LeapMonthStrategy::SplitAfterFifteenth)) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }

    int32_t year = lunar_date.year;
    uint8_t month = lunar_date.month == 13u ? 12u : lunar_date.month;
    bool advance = false;
    if (lunar_date.is_leap != 0u) {
        advance = strategy == LeapMonthStrategy::AsNext
            || (strategy == LeapMonthStrategy::SplitAfterFifteenth
                && lunar_date.day > 15u);
    }
    if (advance) {
        ++month;
        if (month > 12u) {
            month = 1u;
            if (year == std::numeric_limits<int32_t>::max()) {
                return TAIYIN_ERROR_INVALID_ARGUMENT;
            }
            ++year;
        }
    }
    *out_year = year;
    *out_month = month;
    return TAIYIN_STATUS_OK;
}

Status compute_anchors(
    const CalendarFacts& facts,
    const AnchorOptions& options,
    Anchors* out,
    Branch* out_body_palace
) noexcept {
    if (out == NULL || out_body_palace == NULL
        || !is_valid(facts.solar_term_pillars)
        || !is_valid(facts.lunar_pillars)
        || facts.effective_lunar_month < 1u
        || facts.effective_lunar_month > 12u
        || facts.lunar_date.day < 1u
        || facts.lunar_date.day > 30u
        || static_cast<uint8_t>(options.rules.wu_hu_dun_year_boundary) > 1u
        || static_cast<uint8_t>(options.rules.sihua_year_boundary) > 1u
        || static_cast<uint8_t>(options.rules.body_master_year_boundary) > 1u
        || static_cast<uint8_t>(options.chart_mode) > 2u) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }

    Anchors result = {};
    result.solar_term = facts.solar_term_pillars;
    result.lunar = facts.lunar_pillars;

    const Stem year_stem = options.rules.wu_hu_dun_year_boundary
            == PillarBoundary::SolarTerm
        ? facts.solar_term_pillars.year.stem
        : facts.lunar_pillars.year.stem;
    PlacementAnchors placed;
    const Status status = compute_placement_anchors(
        facts.effective_lunar_month, facts.lunar_date.day,
        to_index(facts.lunar_pillars.hour.branch), to_index(year_stem),
        options.chart_mode, &placed);
    if (status != TAIYIN_STATUS_OK) return status;
    result.bureau = placed.bureau;
    result.ziwei = placed.ziwei;
    result.tianfu = placed.tianfu;
    result.palace_positions = placed.palace_positions;
    *out = result;
    *out_body_palace = placed.body_palace;
    return TAIYIN_STATUS_OK;
}

Status compute_placement_anchors(
    int month, int day, int hour_branch, int year_stem,
    ZiweiChartMode mode, PlacementAnchors* out, const Bureau* fixed_bureau
) noexcept {
    if (!out || month < 1 || month > 12 || day < 1 || day > 30
        || hour_branch < 0 || hour_branch > 11 || year_stem < 0 || year_stem > 9
        || static_cast<uint8_t>(mode) > 2u
        || (fixed_bureau && !is_valid(*fixed_bureau))) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    PlacementAnchors result = {};
    const int month_offset = month - 1;
    const int hour_index = hour_branch;
    const Branch original_life = static_cast<Branch>(
        normalized_index(2 + month_offset - hour_index, 12));
    const Branch body = static_cast<Branch>(
        normalized_index(2 + month_offset + hour_index, 12));

    Branch life = original_life;
    if (mode == ZiweiChartMode::DiPan) {
        life = body;
    } else if (mode == ZiweiChartMode::RenPan) {
        life = advance_branch(original_life, 2);
    }
    for (std::size_t palace = 0u; palace < kPalaceCount; ++palace) {
        result.palace_positions[palace] =
            advance_branch(life, -static_cast<int>(palace));
    }

    if (compute_palace_stems(static_cast<Stem>(year_stem), &result.palace_stems)
            != TAIYIN_STATUS_OK) {
        return TAIYIN_ERROR_INTERNAL;
    }
    const Stem life_stem = result.palace_stems[to_index(life)];
    const Ganzhi life_ganzhi = {life_stem, life};
    if (!is_valid(life_ganzhi)) return TAIYIN_ERROR_INTERNAL;

    result.bureau = fixed_bureau ? *fixed_bureau : bureau_from_palace_ganzhi(life_ganzhi);
    result.ziwei = ziwei_position(static_cast<uint8_t>(day), result.bureau);
    result.tianfu = static_cast<Branch>(
        normalized_index(4 - to_index(result.ziwei), 12));
    result.body_palace = body;
    *out = result;
    return TAIYIN_STATUS_OK;
}

bool validate_anchors(const Anchors& anchors) noexcept {
    if (!is_valid(anchors.solar_term)
        || !is_valid(anchors.lunar)
        || !is_valid(anchors.bureau)
        || !is_valid(anchors.ziwei)
        || !is_valid(anchors.tianfu)) {
        return false;
    }

    bool seen[kBranchCount] = {};
    for (std::size_t i = 0u; i < anchors.palace_positions.size(); ++i) {
        const Branch position = anchors.palace_positions[i];
        if (!is_valid(position)) return false;
        const std::size_t index = to_index(position);
        if (seen[index]) return false;
        seen[index] = true;
    }
    return true;
}

std::array<uint8_t, kAnchorCount> flatten_anchors(
    const Anchors& anchors
) noexcept {
    std::array<uint8_t, kAnchorCount> result = {};
    std::size_t cursor = 0u;
    flatten_pillars(anchors.solar_term, &result, &cursor);
    flatten_pillars(anchors.lunar, &result, &cursor);
    result[cursor++] = to_index(anchors.bureau);
    result[cursor++] = to_index(anchors.ziwei);
    result[cursor++] = to_index(anchors.tianfu);
    for (std::size_t i = 0u; i < anchors.palace_positions.size(); ++i) {
        result[cursor++] = to_index(anchors.palace_positions[i]);
    }
    return result;
}

}  // namespace ziwei
}  // namespace taiyin
