#include "taiyin/ziwei/rules.h"

#include <limits>
#include <vector>

namespace taiyin {
namespace ziwei {
namespace {

const uint64_t kFnvOffsetBasis = UINT64_C(14695981039346656037);
const uint64_t kFnvPrime = UINT64_C(1099511628211);

void hash_byte(uint8_t value, uint64_t* hash) noexcept {
    *hash ^= value;
    *hash *= kFnvPrime;
}

void hash_uint64(uint64_t value, uint64_t* hash) noexcept {
    // Use a fixed byte order so the identity is stable across architectures.
    for (unsigned int byte = 0u; byte < 8u; ++byte) {
        hash_byte(static_cast<uint8_t>(value & UINT64_C(0xff)), hash);
        value >>= 8u;
    }
}

void hash_placement_rule(
    const PlacementRule& rule,
    uint64_t* hash
) noexcept {
    hash_uint64(rule.star_id, hash);
    hash_uint64(rule.inputs.size(), hash);
    for (std::size_t i = 0u; i < rule.inputs.size(); ++i) {
        hash_byte(static_cast<uint8_t>(rule.inputs[i]), hash);
    }
    hash_uint64(rule.strides.size(), hash);
    for (std::size_t i = 0u; i < rule.strides.size(); ++i) {
        hash_uint64(rule.strides[i], hash);
    }
    hash_uint64(rule.table.size(), hash);
    for (std::size_t i = 0u; i < rule.table.size(); ++i) {
        hash_byte(rule.table[i], hash);
    }
}

bool accumulate_rule_index(
    uint8_t value,
    std::size_t stride,
    std::size_t* index
) noexcept {
    if (index == NULL) return false;
    const std::size_t maximum =
        (std::numeric_limits<std::size_t>::max)();
    if (value != 0u && stride > maximum / value) return false;
    const std::size_t term = static_cast<std::size_t>(value) * stride;
    if (*index > maximum - term) return false;
    *index += term;
    return true;
}

uint8_t normalized_branch_index(int value) noexcept {
    return static_cast<uint8_t>(((value % 12) + 12) % 12);
}

bool write_ganzhi_component(
    const Ganzhi& ganzhi,
    bool stem,
    uint8_t* out
) noexcept {
    if (out == NULL || !is_valid(ganzhi)) return false;
    *out = stem ? to_index(ganzhi.stem) : to_index(ganzhi.branch);
    return true;
}

bool write_kong_wang(
    const Ganzhi& ganzhi,
    bool secondary,
    uint8_t* out
) noexcept {
    if (out == NULL || !is_valid(ganzhi)) return false;

    // Ganzhi's sexagenary index is the unique solution shared by its stem
    // and branch indices. The two empty branches are ordered the same way as
    // the ziwei_core oracle: the Yang/Yin stem decides 正空/副空 order.
    const int index = (6 * to_index(ganzhi.stem)
        - 5 * to_index(ganzhi.branch) + 60) % 60;
    const uint8_t first = static_cast<uint8_t>(
        (10 - (index / 10) * 2 + 12) % 12);
    const uint8_t second = static_cast<uint8_t>((first + 1u) % 12u);
    const bool yang_stem = (to_index(ganzhi.stem) & 1u) == 0u;
    const bool first_is_zheng = yang_stem
        ? (first & 1u) == 0u
        : (first & 1u) != 0u;
    *out = secondary
        ? (first_is_zheng ? second : first)
        : (first_is_zheng ? first : second);
    return true;
}

bool read_flow_rule_input(
    RuleInputSource source,
    const FlowCoordinate& coordinate,
    Gender natal_gender,
    const Anchors* natal_anchors,
    Branch body_palace,
    uint8_t* out
) noexcept {
    if (out == NULL || !is_valid(coordinate) || !is_valid(natal_gender)) {
        return false;
    }
    switch (source) {
    case RuleInputSource::SolarYearStem:
    case RuleInputSource::LunarYearStem:
        *out = to_index(coordinate.stem);
        return true;
    case RuleInputSource::SolarYearBranch:
    case RuleInputSource::LunarYearBranch:
        *out = to_index(coordinate.branch);
        return true;
    case RuleInputSource::BirthGender:
        *out = to_index(natal_gender);
        return true;
    case RuleInputSource::Bureau:
        if (natal_anchors == NULL || !is_valid(natal_anchors->bureau)) return false;
        *out = to_index(natal_anchors->bureau);
        return true;
    case RuleInputSource::Ziwei:
        if (natal_anchors == NULL || !is_valid(natal_anchors->ziwei)) return false;
        *out = to_index(natal_anchors->ziwei);
        return true;
    case RuleInputSource::Tianfu:
        if (natal_anchors == NULL || !is_valid(natal_anchors->tianfu)) return false;
        *out = to_index(natal_anchors->tianfu);
        return true;
    case RuleInputSource::Life:
        if (natal_anchors == NULL
            || !is_valid(natal_anchors->palace_positions[to_index(PalaceId::Life)])) {
            return false;
        }
        *out = to_index(natal_anchors->palace_positions[to_index(PalaceId::Life)]);
        return true;
    case RuleInputSource::Body:
        if (!is_valid(body_palace)) return false;
        *out = to_index(body_palace);
        return true;
    default:
        return false;
    }
}

bool is_flow_rule_input(RuleInputSource source) noexcept {
    return source == RuleInputSource::SolarYearStem
        || source == RuleInputSource::SolarYearBranch
        || source == RuleInputSource::LunarYearStem
        || source == RuleInputSource::LunarYearBranch
        || source == RuleInputSource::BirthGender
        || source == RuleInputSource::Bureau
        || source == RuleInputSource::Ziwei
        || source == RuleInputSource::Tianfu
        || source == RuleInputSource::Life
        || source == RuleInputSource::Body;
}

}  // namespace

std::size_t rule_input_domain_size(RuleInputSource source) noexcept {
    switch (source) {
    case RuleInputSource::SolarYearStem:
    case RuleInputSource::SolarMonthStem:
    case RuleInputSource::SolarDayStem:
    case RuleInputSource::SolarHourStem:
    case RuleInputSource::LunarYearStem:
    case RuleInputSource::LunarMonthStem:
    case RuleInputSource::LunarDayStem:
    case RuleInputSource::LunarHourStem:
        return kStemCount;
    case RuleInputSource::Bureau:
        return 5u;
    case RuleInputSource::LunarDayIndex:
        return 30u;
    case RuleInputSource::SolarDayIndex:
        return 33u;
    case RuleInputSource::BirthGender:
        return 2u;
    case RuleInputSource::SolarYearBranch:
    case RuleInputSource::SolarMonthBranch:
    case RuleInputSource::SolarDayBranch:
    case RuleInputSource::SolarHourBranch:
    case RuleInputSource::LunarYearBranch:
    case RuleInputSource::LunarMonthBranch:
    case RuleInputSource::LunarDayBranch:
    case RuleInputSource::LunarHourBranch:
    case RuleInputSource::Ziwei:
    case RuleInputSource::Tianfu:
    case RuleInputSource::Life:
    case RuleInputSource::Body:
    case RuleInputSource::SolarZhengKong:
    case RuleInputSource::SolarFuKong:
    case RuleInputSource::LunarZhengKong:
    case RuleInputSource::LunarFuKong:
    case RuleInputSource::SolarMonthIndex:
    case RuleInputSource::LunarMonthIndex:
        return kBranchCount;
    case RuleInputSource::Count:
        return 0u;
    }
    return 0u;
}

bool read_rule_input(
    RuleInputSource source,
    const CalendarFacts& facts,
    const Anchors& anchors,
    Branch body_palace,
    uint8_t* out
) noexcept {
    if (out == NULL) return false;
    switch (source) {
    case RuleInputSource::SolarYearStem:
        return write_ganzhi_component(anchors.solar_term.year, true, out);
    case RuleInputSource::SolarYearBranch:
        return write_ganzhi_component(anchors.solar_term.year, false, out);
    case RuleInputSource::SolarMonthStem:
        return write_ganzhi_component(anchors.solar_term.month, true, out);
    case RuleInputSource::SolarMonthBranch:
        return write_ganzhi_component(anchors.solar_term.month, false, out);
    case RuleInputSource::SolarDayStem:
        return write_ganzhi_component(anchors.solar_term.day, true, out);
    case RuleInputSource::SolarDayBranch:
        return write_ganzhi_component(anchors.solar_term.day, false, out);
    case RuleInputSource::SolarHourStem:
        return write_ganzhi_component(anchors.solar_term.hour, true, out);
    case RuleInputSource::SolarHourBranch:
        return write_ganzhi_component(anchors.solar_term.hour, false, out);
    case RuleInputSource::LunarYearStem:
        return write_ganzhi_component(anchors.lunar.year, true, out);
    case RuleInputSource::LunarYearBranch:
        return write_ganzhi_component(anchors.lunar.year, false, out);
    case RuleInputSource::LunarMonthStem:
        return write_ganzhi_component(anchors.lunar.month, true, out);
    case RuleInputSource::LunarMonthBranch:
        return write_ganzhi_component(anchors.lunar.month, false, out);
    case RuleInputSource::LunarDayStem:
        return write_ganzhi_component(anchors.lunar.day, true, out);
    case RuleInputSource::LunarDayBranch:
        return write_ganzhi_component(anchors.lunar.day, false, out);
    case RuleInputSource::LunarHourStem:
        return write_ganzhi_component(anchors.lunar.hour, true, out);
    case RuleInputSource::LunarHourBranch:
        return write_ganzhi_component(anchors.lunar.hour, false, out);
    case RuleInputSource::Bureau:
        if (!is_valid(anchors.bureau)) return false;
        *out = to_index(anchors.bureau);
        return true;
    case RuleInputSource::Ziwei:
        if (!is_valid(anchors.ziwei)) return false;
        *out = to_index(anchors.ziwei);
        return true;
    case RuleInputSource::Tianfu:
        if (!is_valid(anchors.tianfu)) return false;
        *out = to_index(anchors.tianfu);
        return true;
    case RuleInputSource::Life:
        if (!is_valid(anchors.palace_positions[to_index(PalaceId::Life)])) {
            return false;
        }
        *out = to_index(anchors.palace_positions[to_index(PalaceId::Life)]);
        return true;
    case RuleInputSource::Body:
        if (!is_valid(body_palace)) return false;
        *out = to_index(body_palace);
        return true;
    case RuleInputSource::SolarZhengKong:
        return write_kong_wang(anchors.solar_term.year, false, out);
    case RuleInputSource::SolarFuKong:
        return write_kong_wang(anchors.solar_term.year, true, out);
    case RuleInputSource::LunarZhengKong:
        return write_kong_wang(anchors.lunar.year, false, out);
    case RuleInputSource::LunarFuKong:
        return write_kong_wang(anchors.lunar.year, true, out);
    case RuleInputSource::LunarMonthIndex:
        if (facts.effective_lunar_month < 1u
            || facts.effective_lunar_month > 12u) return false;
        *out = static_cast<uint8_t>(facts.effective_lunar_month - 1u);
        return true;
    case RuleInputSource::SolarMonthIndex:
        if (!is_valid(anchors.solar_term.month.branch)) return false;
        *out = normalized_branch_index(
            static_cast<int>(to_index(anchors.solar_term.month.branch)) - 2);
        return true;
    case RuleInputSource::LunarDayIndex:
        if (facts.lunar_date.day < 1u || facts.lunar_date.day > 30u) return false;
        *out = static_cast<uint8_t>(facts.lunar_date.day - 1u);
        return true;
    case RuleInputSource::SolarDayIndex:
        if (facts.solar_day_from_previous_jie < 1u
            || facts.solar_day_from_previous_jie > 33u) return false;
        *out = static_cast<uint8_t>(facts.solar_day_from_previous_jie - 1u);
        return true;
    case RuleInputSource::BirthGender:
        if (!is_valid(facts.birth.gender)) return false;
        *out = to_index(facts.birth.gender);
        return true;
    case RuleInputSource::Count:
        return false;
    }
    return false;
}

bool evaluate_placement(
    const PlacementRule& rule,
    const CalendarFacts& facts,
    const Anchors& anchors,
    Branch body_palace,
    Branch* out
) noexcept {
    if (out == NULL || rule.inputs.size() != rule.strides.size()) {
        return false;
    }
    std::size_t index = 0u;
    for (std::size_t i = 0u; i < rule.inputs.size(); ++i) {
        uint8_t value = 0u;
        if (!read_rule_input(
                rule.inputs[i], facts, anchors, body_palace, &value)
            || value >= rule_input_domain_size(rule.inputs[i])) {
            return false;
        }
        if (!accumulate_rule_index(value, rule.strides[i], &index)) {
            return false;
        }
    }
    if (index >= rule.table.size()
        || rule.table[index] >= kBranchCount) {
        return false;
    }
    *out = static_cast<Branch>(rule.table[index]);
    return true;
}

bool evaluate_flow_placement(
    const PlacementRule& rule,
    const FlowCoordinate& coordinate,
    Gender natal_gender,
    Branch* out,
    const Anchors* natal_anchors,
    Branch body_palace
) noexcept {
    if (out == NULL || !is_valid(coordinate) || !is_valid(natal_gender)
        || rule.inputs.size() != rule.strides.size()) {
        return false;
    }
    std::size_t index = 0u;
    for (std::size_t i = 0u; i < rule.inputs.size(); ++i) {
        uint8_t value = 0u;
        if (!read_flow_rule_input(
                rule.inputs[i], coordinate, natal_gender,
                natal_anchors, body_palace, &value)
            || value >= rule_input_domain_size(rule.inputs[i])) {
            return false;
        }
        if (!accumulate_rule_index(value, rule.strides[i], &index)) {
            return false;
        }
    }
    if (index >= rule.table.size()
        || rule.table[index] >= kBranchCount) {
        return false;
    }
    *out = static_cast<Branch>(rule.table[index]);
    return true;
}

bool validate_compiled_rules(
    const CompiledRules& rules,
    std::size_t registry_size
) noexcept {
    if (rules.format_version == 0u
        || rules.registry_fingerprint == 0u
        || rules.star_count != registry_size
        || rules.natal_star_count != rules.placement.natal.size()
        || rules.natal_star_count > registry_size
        || rules.natal_by_star.size() != registry_size
        || rules.placement.flow.size()
            != registry_size - rules.natal_star_count
        || rules.brightness.values.size() != registry_size
        || registry_size >= static_cast<std::size_t>(kInvalidStarId)) {
        return false;
    }
    const std::size_t placement_count =
        rules.placement.natal.size() + rules.placement.flow.size();
    for (std::size_t i = 0u; i < placement_count; ++i) {
        const PlacementRule& rule = i < rules.placement.natal.size()
            ? rules.placement.natal[i]
            : rules.placement.flow[i - rules.placement.natal.size()];
        const bool is_natal = i < rules.placement.natal.size();
        if (rule.star_id >= registry_size
            || (rules.natal_by_star[rule.star_id] != 0u) != is_natal) {
            return false;
        }
        // Keep validation noexcept: this is intentionally O(n²) rather than
        // allocating a temporary StarId bitmap. Rule catalogs are small and
        // validated only at load/context creation time.
        for (std::size_t earlier = 0u; earlier < i; ++earlier) {
            const PlacementRule& prior = earlier < rules.placement.natal.size()
                ? rules.placement.natal[earlier]
                : rules.placement.flow[
                    earlier - rules.placement.natal.size()];
            if (prior.star_id == rule.star_id) return false;
        }
        if (rule.inputs.size() != rule.strides.size()) return false;
        std::size_t expected = 1u;
        for (std::size_t input = rule.inputs.size(); input-- > 0u;) {
            const std::size_t domain =
                rule_input_domain_size(rule.inputs[input]);
            if (domain == 0u) return false;
            if (rule.strides[input] != expected) return false;
            if (expected > (std::numeric_limits<std::size_t>::max)() / domain) {
                return false;
            }
            expected *= domain;
        }
        if (rule.table.size() != expected) return false;
        for (std::size_t entry = 0u; entry < expected; ++entry) {
            if (rule.table[entry] >= kBranchCount) return false;
        }
    }

    for (std::size_t i = 0u; i < rules.placement.flow.size(); ++i) {
        const PlacementRule& rule = rules.placement.flow[i];
        for (std::size_t input = 0u; input < rule.inputs.size(); ++input) {
            if (!is_flow_rule_input(rule.inputs[input])) return false;
        }
    }

    for (std::size_t star = 0u;
         star < rules.brightness.values.size(); ++star) {
        for (std::size_t palace = 0u; palace < kBranchCount; ++palace) {
            const int value = rules.brightness.values[star][palace];
            if (value < -1 || value > 6) return false;
        }
    }

    for (std::size_t stem = 0u; stem < rules.sihua.by_stem.size(); ++stem) {
        const TransformSet& value = rules.sihua.by_stem[stem];
        if (value.lu >= registry_size || rules.natal_by_star[value.lu] == 0u
            || value.quan >= registry_size || rules.natal_by_star[value.quan] == 0u
            || value.ke >= registry_size || rules.natal_by_star[value.ke] == 0u
            || value.ji >= registry_size || rules.natal_by_star[value.ji] == 0u) {
            return false;
        }
    }
    if (rules.masters.enabled) {
        if (static_cast<uint8_t>(rules.masters.life_input)
                > static_cast<uint8_t>(MasterLookupSource::SolarYearBranch)
            || static_cast<uint8_t>(rules.masters.body_input)
                > static_cast<uint8_t>(MasterLookupSource::SolarYearBranch)) {
            return false;
        }
        for (std::size_t branch = 0u; branch < kBranchCount; ++branch) {
            if (rules.masters.life[branch] >= registry_size
                || rules.natal_by_star[rules.masters.life[branch]] == 0u
                || rules.masters.body[branch] >= registry_size
                || rules.natal_by_star[rules.masters.body[branch]] == 0u) {
                return false;
            }
        }
    }
    return true;
}

uint64_t compiled_rules_fingerprint(
    const CompiledRules& rules,
    uint64_t star_registry_fingerprint
) noexcept {
    uint64_t hash = kFnvOffsetBasis;
    hash_uint64(star_registry_fingerprint, &hash);
    hash_uint64(rules.format_version, &hash);
    hash_uint64(rules.star_count, &hash);
    hash_uint64(rules.natal_star_count, &hash);

    hash_uint64(rules.natal_by_star.size(), &hash);
    for (std::size_t i = 0u; i < rules.natal_by_star.size(); ++i) {
        hash_byte(rules.natal_by_star[i], &hash);
    }

    hash_uint64(rules.placement.natal.size(), &hash);
    for (std::size_t i = 0u; i < rules.placement.natal.size(); ++i) {
        hash_placement_rule(rules.placement.natal[i], &hash);
    }
    hash_uint64(rules.placement.flow.size(), &hash);
    for (std::size_t i = 0u; i < rules.placement.flow.size(); ++i) {
        hash_placement_rule(rules.placement.flow[i], &hash);
    }

    hash_uint64(rules.brightness.values.size(), &hash);
    for (std::size_t star = 0u;
         star < rules.brightness.values.size(); ++star) {
        for (std::size_t branch = 0u; branch < kBranchCount; ++branch) {
            hash_byte(static_cast<uint8_t>(
                rules.brightness.values[star][branch]), &hash);
        }
    }

    for (std::size_t stem = 0u; stem < kStemCount; ++stem) {
        const TransformSet& transform = rules.sihua.by_stem[stem];
        hash_uint64(transform.lu, &hash);
        hash_uint64(transform.quan, &hash);
        hash_uint64(transform.ke, &hash);
        hash_uint64(transform.ji, &hash);
    }

    hash_byte(rules.masters.enabled ? 1u : 0u, &hash);
    hash_byte(static_cast<uint8_t>(rules.masters.life_input), &hash);
    hash_byte(static_cast<uint8_t>(rules.masters.body_input), &hash);
    for (std::size_t branch = 0u; branch < kBranchCount; ++branch) {
        hash_uint64(rules.masters.life[branch], &hash);
        hash_uint64(rules.masters.body[branch], &hash);
    }

    // Zero is reserved as the uninitialized/invalid identity sentinel.
    return hash == 0u ? UINT64_C(1) : hash;
}

bool compiled_rules_match_registry(
    const CompiledRules& rules,
    const StarRegistry& registry
) noexcept {
    const uint64_t registry_fingerprint = registry.fingerprint();
    return rules.registry_fingerprint != 0u
        && rules.registry_fingerprint
            == compiled_rules_fingerprint(rules, registry_fingerprint)
        && validate_compiled_rules(rules, registry.size());
}

Status brightness_at(
    const CompiledRules& rules,
    StarId star,
    Branch branch,
    Brightness* out
) noexcept {
    if (out == NULL
        || !is_valid(branch)
        || star >= rules.brightness.values.size()) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    const Brightness value = static_cast<Brightness>(
        rules.brightness.values[star][to_index(branch)]);
    if (!is_valid(value)) return TAIYIN_ERROR_INTERNAL;
    *out = value;
    return TAIYIN_STATUS_OK;
}

}  // namespace ziwei
}  // namespace taiyin
