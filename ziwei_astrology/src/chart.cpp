#include "taiyin/ziwei/chart.h"
#include "plate_internal.h"

#include <algorithm>
#include <new>
#include <utility>

namespace taiyin {
namespace ziwei {
namespace {

void add_mark(
    std::vector<StarTransformMask>* masks,
    StarId star,
    StarTransformMark mark
) noexcept {
    if (star != kInvalidStarId && star < masks->size()) {
        (*masks)[star] = static_cast<StarTransformMask>(
            (*masks)[star] | (StarTransformMask(1u) << to_index(mark)));
    }
}

void add_transform_set(
    std::vector<StarTransformMask>* masks,
    const TransformSet& transforms,
    StarTransformMark lu_mark
) noexcept {
    const StarId ids[4] = {
        transforms.lu, transforms.quan, transforms.ke, transforms.ji,
    };
    for (std::size_t kind = 0u; kind < 4u; ++kind) {
        add_mark(masks, ids[kind], static_cast<StarTransformMark>(
            to_index(lu_mark) + kind));
    }
}

}  // namespace

void detail::decorate_transformations(
    const std::array<PalaceState, kBranchCount>& palaces,
    const std::array<Stem, kBranchCount>& stems,
    const TransformSet& year, const CompiledRules& rules,
    std::vector<StarTransformMask>* masks
) {
    masks->assign(rules.star_count, 0u);
    add_transform_set(masks, year, StarTransformMark::BirthYearLu);
    for (std::size_t branch = 0u; branch < kBranchCount; ++branch) {
        const Branch opposite = static_cast<Branch>((branch + 6u) % kBranchCount);
        const DynamicBitset& placed = palaces[branch].stars;
        const TransformSet& own = rules.sihua.by_stem[
            to_index(stems[branch])];
        const TransformSet& opposite_set = rules.sihua.by_stem[
            to_index(stems[to_index(opposite)])];
        const StarId own_ids[4] = {own.lu, own.quan, own.ke, own.ji};
        const StarId opposite_ids[4] = {
            opposite_set.lu, opposite_set.quan, opposite_set.ke, opposite_set.ji};
        for (std::size_t kind = 0u; kind < 4u; ++kind) {
            if (own_ids[kind] != kInvalidStarId && placed.test(own_ids[kind])) {
                add_mark(masks, own_ids[kind],
                    static_cast<StarTransformMark>(
                        to_index(StarTransformMark::CentrifugalLu) + kind));
            }
            if (opposite_ids[kind] != kInvalidStarId
                && placed.test(opposite_ids[kind])) {
                add_mark(masks, opposite_ids[kind],
                    static_cast<StarTransformMark>(
                        to_index(StarTransformMark::CentripetalLu) + kind));
            }
        }
    }
}

namespace {

Branch master_lookup_branch(
    MasterLookupSource source,
    const Anchors& anchors,
    const NatalRuleOptions& options
) noexcept {
    switch (source) {
        case MasterLookupSource::LifePalace:
            return anchors.palace_positions[to_index(PalaceId::Life)];
        case MasterLookupSource::SelectedYearBranch:
            return options.body_master_year_boundary == PillarBoundary::SolarTerm
                ? anchors.solar_term.year.branch : anchors.lunar.year.branch;
        case MasterLookupSource::LunarYearBranch:
            return anchors.lunar.year.branch;
        case MasterLookupSource::SolarYearBranch:
            return anchors.solar_term.year.branch;
    }
    return Branch::Zi;
}

}  // namespace

Status make_natal_chart(
    const CalendarFacts& facts,
    const Anchors& anchors,
    Branch body_palace,
    const NatalRuleOptions& options,
    const CompiledRules& rules,
    NatalChart* out
) noexcept {
    if (out == NULL
        || !validate_anchors(anchors)
        || !is_valid(body_palace)
        || !is_valid(facts.birth.gender)
        || static_cast<uint8_t>(options.wu_hu_dun_year_boundary) > 1u
        || static_cast<uint8_t>(options.sihua_year_boundary) > 1u
        || static_cast<uint8_t>(options.body_master_year_boundary) > 1u
        || !validate_compiled_rules(rules, rules.star_count)) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }

    try {
        NatalChart result;
        result.birth_facts = facts;
        result.rule_registry_fingerprint = rules.registry_fingerprint;
        result.anchors = anchors;
        result.body_palace = body_palace;
        result.gender = facts.birth.gender;
        result.life_master = kInvalidStarId;
        result.body_master = kInvalidStarId;
        const Stem palace_year_stem = options.wu_hu_dun_year_boundary
                == PillarBoundary::SolarTerm
            ? anchors.solar_term.year.stem
            : anchors.lunar.year.stem;
        if (compute_palace_stems(palace_year_stem, &result.palace_stems)
            != TAIYIN_STATUS_OK) {
            return TAIYIN_ERROR_INVALID_ARGUMENT;
        }
        for (std::size_t branch = 0u; branch < result.palaces.size(); ++branch) {
            result.palaces[branch].stars.resize(rules.star_count, false);
        }

        for (std::size_t i = 0u; i < rules.placement.natal.size(); ++i) {
            const PlacementRule& rule = rules.placement.natal[i];
            Branch position = Branch::Zi;
            if (!evaluate_placement(
                    rule, facts, anchors, body_palace, &position)) {
                return TAIYIN_ERROR_INVALID_ARGUMENT;
            }
            result.palaces[to_index(position)].stars.set(rule.star_id);
        }

        const Stem sihua_stem = options.sihua_year_boundary
                == PillarBoundary::SolarTerm
            ? anchors.solar_term.year.stem
            : anchors.lunar.year.stem;
        result.transformations.birth_year_stem = sihua_stem;
        result.transformations.birth_year = rules.sihua.by_stem[to_index(sihua_stem)];
        detail::decorate_transformations(result.palaces, result.palace_stems,
            result.transformations.birth_year, rules, &result.transformations.marks_by_star);
        if (rules.masters.enabled) {
            const Branch life = master_lookup_branch(
                rules.masters.life_input, anchors, options);
            const Branch body = master_lookup_branch(
                rules.masters.body_input, anchors, options);
            result.life_master = rules.masters.life[to_index(life)];
            result.body_master = rules.masters.body[to_index(body)];
        }
        *out = std::move(result);
        return TAIYIN_STATUS_OK;
    } catch (const std::bad_alloc&) {
        return TAIYIN_ERROR_OUT_OF_MEMORY;
    } catch (...) {
        return TAIYIN_ERROR_INTERNAL;
    }
}

StarTransformMask star_transform_mask(
    const NatalChart& chart,
    StarId star
) noexcept {
    return star < chart.transformations.marks_by_star.size()
        ? chart.transformations.marks_by_star[star] : 0u;
}

bool has_star_transform_mark(
    const NatalChart& chart,
    StarTransformMark mark,
    StarId star
) noexcept {
    return is_valid(mark)
        && (star_transform_mask(chart, star)
            & (StarTransformMask(1u) << to_index(mark))) != 0u;
}

Status dump_natal_star_positions(
    const NatalChart& chart,
    std::vector<uint8_t>* out
) noexcept {
    if (out == NULL) return TAIYIN_ERROR_INVALID_ARGUMENT;
    try {
        const std::size_t star_count = chart.palaces[0].stars.size();
        for (std::size_t branch = 1u; branch < chart.palaces.size(); ++branch) {
            if (chart.palaces[branch].stars.size() != star_count) {
                return TAIYIN_ERROR_INVALID_ARGUMENT;
            }
        }
        std::vector<uint8_t> result(star_count, 0xffu);
        for (std::size_t branch = 0u; branch < chart.palaces.size(); ++branch) {
            for (std::size_t star = 0u; star < star_count; ++star) {
                if (!chart.palaces[branch].stars.test(star)) continue;
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

}  // namespace ziwei
}  // namespace taiyin
