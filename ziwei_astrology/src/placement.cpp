#include "taiyin/ziwei/placement.h"
#include "plate_internal.h"

#include <cstdio>
#include <new>
#include <utility>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <bcrypt.h>
#endif

namespace taiyin { namespace ziwei { namespace {

Status caught_status() noexcept {
    try { throw; }
    catch (const std::bad_alloc&) { return TAIYIN_ERROR_OUT_OF_MEMORY; }
    catch (...) { return TAIYIN_ERROR_INTERNAL; }
}

bool valid_input(const PlacementInput& p) noexcept {
    return p.year_stem >= 0 && p.year_stem < 10
        && p.year_branch >= 0 && p.year_branch < 12
        && p.month >= 1 && p.month <= 12 && p.day >= 1 && p.day <= 30
        && p.hour_branch >= 0 && p.hour_branch < 12;
}
bool valid_patch(const PlacementPatch& p) noexcept {
    return p.year_stem >= -1 && p.year_stem < 10
        && p.year_branch >= -1 && p.year_branch < 12
        && (p.month == -1 || (p.month >= 1 && p.month <= 12))
        && (p.day == -1 || (p.day >= 1 && p.day <= 30))
        && p.hour_branch >= -1 && p.hour_branch < 12
        && p.update_bureau >= -1 && p.update_bureau <= 1;
}
PlacementPatch full_patch(const PlacementInput& p) noexcept {
    PlacementPatch r;
    r.year_stem = p.year_stem; r.year_branch = p.year_branch;
    r.month = p.month; r.day = p.day; r.hour_branch = p.hour_branch;
    return r;
}
void merge(PlacementPatch* p, const PlacementPatch& q) noexcept {
    if (q.year_stem != -1) p->year_stem = q.year_stem;
    if (q.year_branch != -1) p->year_branch = q.year_branch;
    if (q.month != -1) p->month = q.month;
    if (q.day != -1) p->day = q.day;
    if (q.hour_branch != -1) p->hour_branch = q.hour_branch;
}
PlacementInput apply(PlacementInput p, const PlacementPatch& q) noexcept {
    if (q.year_stem != -1) p.year_stem = q.year_stem;
    if (q.year_branch != -1) p.year_branch = q.year_branch;
    if (q.month != -1) p.month = q.month;
    if (q.day != -1) p.day = q.day;
    if (q.hour_branch != -1) p.hour_branch = q.hour_branch;
    return p;
}
uint8_t shifted(uint8_t old, int32_t steps) noexcept {
    return static_cast<uint8_t>((old + steps % 12 + 12) % 12);
}
PlacementInput base_input(const NatalChart& p) noexcept {
    PlacementInput r;
    r.year_stem = to_index(p.anchors.lunar.year.stem);
    r.year_branch = to_index(p.anchors.lunar.year.branch);
    r.month = p.birth_facts.effective_lunar_month;
    r.day = p.birth_facts.lunar_date.day;
    r.hour_branch = to_index(p.anchors.lunar.hour.branch);
    return r;
}
int master_branch(MasterLookupSource source, const PlacementResult& p) noexcept {
    return source == MasterLookupSource::LifePalace
        ? to_index(p.anchors.palace_positions[0]) : p.input.year_branch;
}
bool compatible(const NatalChart& p, const CompiledRules& r) noexcept {
    return p.rule_registry_fingerprint == r.registry_fingerprint
        && validate_anchors(p.anchors) && is_valid(p.body_palace)
        && is_valid(p.gender) && valid_input(base_input(p))
        && validate_compiled_rules(r, r.star_count);
}
bool valid_options(const AnchorOptions& o) noexcept {
    return static_cast<uint8_t>(o.chart_mode) <= 2u
        && static_cast<uint8_t>(o.rules.wu_hu_dun_year_boundary) <= 1u
        && static_cast<uint8_t>(o.rules.sihua_year_boundary) <= 1u
        && static_cast<uint8_t>(o.rules.body_master_year_boundary) <= 1u;
}

// The only evaluator is the existing finite row-major table. The value array
// distinguishes unavailable inputs from valid zero; no invented calendar facts.
Status arrange(const PlacementInput& input, const PlacementPatch& changes,
    Gender gender, const AnchorOptions& options, const CompiledRules& rules,
    const NatalChart* birth, const PlacementAnchors* fixed_frame,
    const Bureau* fixed_bureau, PlacementResult* out) {
    if (!out || !valid_input(input) || !valid_patch(changes) || !is_valid(gender)
        || !valid_options(options) || !validate_compiled_rules(rules, rules.star_count)) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    PlacementResult p;
    p.input = input; p.gender = gender;
    p.rule_registry_fingerprint = rules.registry_fingerprint;
    int palace_year = input.year_stem;
    if (birth && changes.year_stem == -1) {
        palace_year = to_index(options.rules.wu_hu_dun_year_boundary == PillarBoundary::SolarTerm
            ? birth->anchors.solar_term.year.stem : birth->anchors.lunar.year.stem);
    }
    Status status = compute_placement_anchors(input.month, input.day, input.hour_branch,
        palace_year, options.chart_mode, &p.anchors, fixed_bureau);
    if (status != TAIYIN_STATUS_OK) return status;
    std::array<int, static_cast<std::size_t>(RuleInputSource::Count)> v;
    v.fill(-1);
    if (birth) {
        for (std::size_t i = 0; i < v.size(); ++i) {
            uint8_t value = 0;
            if (read_rule_input(static_cast<RuleInputSource>(i), birth->birth_facts,
                    birth->anchors, birth->body_palace, &value)) v[i] = value;
        }
    }
    const auto set = [&v](RuleInputSource s, int x) { v[static_cast<std::size_t>(s)] = x; };
    set(RuleInputSource::Bureau, to_index(p.anchors.bureau));
    set(RuleInputSource::Ziwei, to_index(p.anchors.ziwei));
    set(RuleInputSource::Tianfu, to_index(p.anchors.tianfu));
    set(RuleInputSource::Life, to_index(fixed_frame ? fixed_frame->palace_positions[0]
        : birth ? birth->anchors.palace_positions[0] : p.anchors.palace_positions[0]));
    set(RuleInputSource::Body, to_index(fixed_frame ? fixed_frame->body_palace
        : birth ? birth->body_palace : p.anchors.body_palace));
    set(RuleInputSource::BirthGender, to_index(gender));
    for (int solar = 0; solar <= 1; ++solar) {
        const Pillars* pillars = birth ? (solar ? &birth->anchors.solar_term : &birth->anchors.lunar) : NULL;
        const int gan = changes.year_stem != -1 ? changes.year_stem
            : pillars ? to_index(pillars->year.stem) : input.year_stem;
        const int zhi = changes.year_branch != -1 ? changes.year_branch
            : pillars ? to_index(pillars->year.branch) : input.year_branch;
        const std::size_t offset = static_cast<std::size_t>(solar
            ? RuleInputSource::SolarYearStem : RuleInputSource::LunarYearStem);
        v[offset] = gan; v[offset + 1] = zhi;
        int main = -1, secondary = -1;
        if ((gan & 1) == (zhi & 1)) {
            const int index = (6 * gan - 5 * zhi + 60) % 60;
            const int first = (10 - index / 10 * 2 + 12) % 12;
            const int second = (first + 1) % 12;
            const bool first_main = (first & 1) == (gan & 1);
            main = first_main ? first : second;
            secondary = first_main ? second : first;
        }
        set(solar ? RuleInputSource::SolarZhengKong : RuleInputSource::LunarZhengKong, main);
        set(solar ? RuleInputSource::SolarFuKong : RuleInputSource::LunarFuKong, secondary);
        const int month = changes.month != -1 ? changes.month
            : solar && pillars ? (to_index(pillars->month.branch) + 10) % 12 + 1 : input.month;
        if (!birth || changes.month != -1 || changes.year_stem != -1) {
            v[offset + 2] = (gan % 5 * 2 + 1 + month) % 10;
            v[offset + 3] = (month + 1) % 12;
            set(solar ? RuleInputSource::SolarMonthIndex : RuleInputSource::LunarMonthIndex, month - 1);
        }
        if (!birth || changes.day != -1) {
            set(solar ? RuleInputSource::SolarDayIndex : RuleInputSource::LunarDayIndex, input.day - 1);
        }
        if (!birth || changes.hour_branch != -1) {
            v[offset + 7] = input.hour_branch;
            v[offset + 6] = pillars ? (to_index(pillars->day.stem) % 5 * 2 + input.hour_branch) % 10 : -1;
        }
    }
    p.star_positions.assign(rules.star_count, 0xffu);
    for (std::size_t b = 0; b < kBranchCount; ++b) p.palaces[b].stars.resize(rules.star_count, false);
    for (const PlacementRule& rule : rules.placement.natal) {
        OmittedPlacement omitted; omitted.star_id = rule.star_id;
        std::size_t index = 0;
        for (std::size_t i = 0; i < rule.inputs.size(); ++i) {
            const int value = v[static_cast<std::size_t>(rule.inputs[i])];
            if (value < 0) omitted.missing_inputs.push_back(rule.inputs[i]);
            else {
                if (static_cast<std::size_t>(value) >= rule_input_domain_size(rule.inputs[i]))
                    return TAIYIN_ERROR_INVALID_ARGUMENT;
                index += static_cast<std::size_t>(value) * rule.strides[i];
            }
        }
        if (!omitted.missing_inputs.empty()) { p.omitted_placements.push_back(std::move(omitted)); continue; }
        if (index >= rule.table.size()) return TAIYIN_ERROR_INVALID_ARGUMENT;
        const uint8_t b = rule.table[index];
        p.star_positions[rule.star_id] = b;
        p.palaces[b].stars.set(rule.star_id);
    }
    int sihua = input.year_stem;
    if (birth && changes.year_stem == -1) sihua = to_index(
        options.rules.sihua_year_boundary == PillarBoundary::SolarTerm
            ? birth->anchors.solar_term.year.stem : birth->anchors.lunar.year.stem);
    p.year_transform_stem = static_cast<Stem>(sihua);
    p.year_transformations = rules.sihua.by_stem[sihua];
    if (rules.masters.enabled) {
        p.life_master = rules.masters.life[master_branch(rules.masters.life_input, p)];
        p.body_master = rules.masters.body[master_branch(rules.masters.body_input, p)];
    }
    detail::decorate_transformations(p.palaces, p.anchors.palace_stems,
        p.year_transformations, rules, &p.transformation_masks);
    *out = std::move(p);
    return TAIYIN_STATUS_OK;
}

Status os_random(void*, uint32_t* out) {
#ifdef _WIN32
    return BCryptGenRandom(NULL, reinterpret_cast<PUCHAR>(out), sizeof(*out),
        BCRYPT_USE_SYSTEM_PREFERRED_RNG) == 0 ? TAIYIN_STATUS_OK : TAIYIN_ERROR_INTERNAL;
#else
    FILE* file = std::fopen("/dev/urandom", "rb");
    if (!file) return TAIYIN_ERROR_INTERNAL;
    const bool ok = std::fread(out, sizeof(*out), 1, file) == 1;
    std::fclose(file);
    return ok ? TAIYIN_STATUS_OK : TAIYIN_ERROR_INTERNAL;
#endif
}
Status sample_index(CastingRandomUint32 source, void* data, uint32_t* out) {
    const uint64_t limit = (UINT64_C(0x100000000) / kCastingSpaceSize) * kCastingSpaceSize;
    for (int attempt = 0; attempt < 128; ++attempt) {
        uint32_t value = 0;
        const Status s = source(data, &value);
        if (s != TAIYIN_STATUS_OK) return s < 0 ? s : TAIYIN_ERROR_INVALID_ARGUMENT;
        if (value < limit) { *out = value % kCastingSpaceSize; return TAIYIN_STATUS_OK; }
    }
    return TAIYIN_ERROR_INTERNAL;
}
Status number_random(void* data, uint32_t* out) {
    uint32_t& state = *static_cast<uint32_t*>(data);
    state += UINT32_C(0x6d2b79f5);
    uint32_t value = (state ^ (state >> 15)) * (state | 1u);
    value ^= value + (value ^ (value >> 7)) * (value | 61u);
    *out = value ^ (value >> 14);
    return TAIYIN_STATUS_OK;
}
} // namespace

PlacementInput natal_placement_input(const NatalChart& chart) noexcept {
    const NatalChart& root = chart.original_chart ? *chart.original_chart : chart;
    return apply(base_input(root), chart.modification.overrides);
}

Status arrange_ziwei_stars(const PlacementInput& input, Gender gender, ZiweiChartMode mode,
    const CompiledRules& rules, PlacementResult* out, const Bureau* fixed_bureau) noexcept {
    try {
        AnchorOptions options = default_anchor_options(); options.chart_mode = mode;
        return arrange(input, full_patch(input), gender, options, rules, NULL, NULL, fixed_bureau, out);
    } catch (...) { return caught_status(); }
}

Status modify_natal_chart(const NatalChart& source, const PlacementPatch& patch,
    const AnchorOptions& options, const CompiledRules& rules, NatalChart* out) noexcept {
    try {
        const NatalChart& root = source.original_chart ? *source.original_chart : source;
        if (!out || !valid_patch(patch) || !compatible(root, rules)
            || !compatible(source, rules) || !valid_options(options)) return TAIYIN_ERROR_INVALID_ARGUMENT;
        NatalChart result = root;
        result.original_chart = source.original_chart ? source.original_chart : std::make_shared<const NatalChart>(root);
        result.modification = source.modification;
        merge(&result.modification.overrides, patch);
        if (patch.update_bureau != -1) result.modification.update_bureau = patch.update_bureau != 0;
        PlacementResult p;
        const Status status = arrange(apply(base_input(root), result.modification.overrides),
            result.modification.overrides, root.gender, options, rules, &root, NULL,
            result.modification.update_bureau ? NULL : &root.anchors.bureau, &p);
        if (status != TAIYIN_STATUS_OK) return status;
        result.anchors.bureau = p.anchors.bureau;
        result.anchors.ziwei = p.anchors.ziwei;
        result.anchors.tianfu = p.anchors.tianfu;
        for (std::size_t i = 0; i < kPalaceCount; ++i)
            result.anchors.palace_positions[i] = advance_branch(root.anchors.palace_positions[i], result.modification.life_palace_shift);
        result.palaces = std::move(p.palaces);
        result.omitted_placements = std::move(p.omitted_placements);
        result.transformations.birth_year_stem = p.year_transform_stem;
        result.transformations.birth_year = p.year_transformations;
        detail::decorate_transformations(result.palaces, result.palace_stems,
            result.transformations.birth_year, rules, &result.transformations.marks_by_star);
        *out = std::move(result);
        return TAIYIN_STATUS_OK;
    } catch (...) { return caught_status(); }
}

Status shift_natal_life_palace(const NatalChart& source, int32_t steps, NatalChart* out) noexcept {
    if (!out || !validate_anchors(source.anchors)) return TAIYIN_ERROR_INVALID_ARGUMENT;
    try {
        NatalChart result = source;
        if (!result.original_chart) result.original_chart = std::make_shared<const NatalChart>(source);
        result.modification.life_palace_shift = shifted(result.modification.life_palace_shift, steps);
        for (Branch& b : result.anchors.palace_positions) b = advance_branch(b, steps % 12);
        *out = std::move(result); return TAIYIN_STATUS_OK;
    } catch (...) { return caught_status(); }
}
Status reset_natal_chart(const NatalChart& source, NatalChart* out) noexcept {
    if (!out) return TAIYIN_ERROR_INVALID_ARGUMENT;
    try { NatalChart result = source.original_chart ? *source.original_chart : source;
        *out = std::move(result); return TAIYIN_STATUS_OK;
    } catch (...) { return caught_status(); }
}

Status casting_input_from_index(uint32_t index, PlacementInput* out) noexcept {
    if (!out || index >= kCastingSpaceSize) return TAIYIN_ERROR_INVALID_ARGUMENT;
    PlacementInput p;
    p.hour_branch = index % 12; index /= 12;
    p.day = index % 30 + 1; index /= 30;
    p.month = index % 12 + 1; index /= 12;
    p.year_stem = index % 10; p.year_branch = index % 12;
    *out = p; return TAIYIN_STATUS_OK;
}
Status make_casting_chart(const PlacementInput& input, Gender gender, ZiweiChartMode mode,
    const CompiledRules& rules, CastingChart* out, const Bureau* fixed_bureau) noexcept {
    if (!out) return TAIYIN_ERROR_INVALID_ARGUMENT;
    try {
        CastingChart c; c.chart_mode = mode;
        const Status s = arrange_ziwei_stars(input, gender, mode, rules, &c.plate, fixed_bureau);
        if (s != TAIYIN_STATUS_OK) return s;
        *out = std::move(c); return TAIYIN_STATUS_OK;
    } catch (...) { return caught_status(); }
}
Status casting_chart_from_index(uint32_t index, Gender gender, ZiweiChartMode mode,
    const CompiledRules& rules, CastingChart* out) noexcept {
    if (!out) return TAIYIN_ERROR_INVALID_ARGUMENT;
    PlacementInput p;
    const Status s = casting_input_from_index(index, &p);
    if (s != TAIYIN_STATUS_OK) return s;
    const Status built = make_casting_chart(p, gender, mode, rules, out);
    if (built == TAIYIN_STATUS_OK) { out->index = index; out->method = CastingMethod::Index; }
    return built;
}
Status casting_chart_from_number(const std::string& number, Gender gender, ZiweiChartMode mode,
    const CompiledRules& rules, CastingChart* out) noexcept {
    if (!out || number.empty()) return TAIYIN_ERROR_INVALID_ARGUMENT;
    for (char c : number) if (c < '0' || c > '9') return TAIYIN_ERROR_INVALID_ARGUMENT;
    try {
        const std::size_t first = number.find_first_not_of('0');
        const std::string normalized = first == std::string::npos ? "0" : number.substr(first);
        const std::string text = "ziwei-casting-number-v1:" + normalized;
        uint32_t state = UINT32_C(0x811c9dc5);
        for (unsigned char c : text) state = (state ^ c) * UINT32_C(0x01000193);
        uint32_t index = 0;
        const Status s = sample_index(number_random, &state, &index);
        if (s != TAIYIN_STATUS_OK) return s;
        CastingChart result;
        const Status built = casting_chart_from_index(index, gender, mode, rules, &result);
        if (built != TAIYIN_STATUS_OK) return built;
        result.method = CastingMethod::Number; result.number = normalized;
        *out = std::move(result); return TAIYIN_STATUS_OK;
    } catch (...) { return caught_status(); }
}
Status random_casting_chart(Gender gender, ZiweiChartMode mode, const CompiledRules& rules,
    CastingChart* out, CastingRandomUint32 source, void* user_data) noexcept {
    if (!out || !is_valid(gender) || static_cast<uint8_t>(mode) > 2u
        || !validate_compiled_rules(rules, rules.star_count)) return TAIYIN_ERROR_INVALID_ARGUMENT;
    try {
        uint32_t index = 0;
        const Status s = sample_index(source ? source : os_random, user_data, &index);
        if (s != TAIYIN_STATUS_OK) return s;
        const Status built = casting_chart_from_index(index, gender, mode, rules, out);
        if (built == TAIYIN_STATUS_OK) out->method = CastingMethod::Random;
        return built;
    } catch (...) { return caught_status(); }
}

Status modify_casting_chart(const CastingChart& source, const PlacementPatch& patch,
    const CompiledRules& rules, CastingChart* out) noexcept {
    const CastingChart& root = source.original_chart ? *source.original_chart : source;
    if (!out || !valid_patch(patch) || source.plate.rule_registry_fingerprint != rules.registry_fingerprint
        || root.plate.rule_registry_fingerprint != rules.registry_fingerprint) return TAIYIN_ERROR_INVALID_ARGUMENT;
    try {
        CastingChart result = source;
        result.original_chart = source.original_chart ? source.original_chart : std::make_shared<const CastingChart>(root);
        merge(&result.modification.overrides, patch);
        if (patch.update_bureau != -1) result.modification.update_bureau = patch.update_bureau != 0;
        const PlacementInput p = apply(root.plate.input, result.modification.overrides);
        AnchorOptions o = default_anchor_options(); o.chart_mode = root.chart_mode;
        const Status s = arrange(p, full_patch(p), root.plate.gender, o, rules, NULL, &root.plate.anchors,
            result.modification.update_bureau ? NULL : &root.plate.anchors.bureau, &result.plate);
        if (s != TAIYIN_STATUS_OK) return s;
        result.plate.anchors.body_palace = root.plate.anchors.body_palace;
        result.plate.anchors.palace_stems = root.plate.anchors.palace_stems;
        for (std::size_t i = 0; i < kPalaceCount; ++i) result.plate.anchors.palace_positions[i] =
            advance_branch(root.plate.anchors.palace_positions[i], result.modification.life_palace_shift);
        result.plate.life_master = root.plate.life_master; result.plate.body_master = root.plate.body_master;
        detail::decorate_transformations(result.plate.palaces, result.plate.anchors.palace_stems,
            result.plate.year_transformations, rules, &result.plate.transformation_masks);
        *out = std::move(result); return TAIYIN_STATUS_OK;
    } catch (...) { return caught_status(); }
}
Status shift_casting_life_palace(const CastingChart& source, int32_t steps, CastingChart* out) noexcept {
    if (!out || source.plate.rule_registry_fingerprint == 0) return TAIYIN_ERROR_INVALID_ARGUMENT;
    try {
        CastingChart result = source;
        if (!result.original_chart) result.original_chart = std::make_shared<const CastingChart>(source);
        result.modification.life_palace_shift = shifted(result.modification.life_palace_shift, steps);
        for (Branch& b : result.plate.anchors.palace_positions) b = advance_branch(b, steps % 12);
        *out = std::move(result); return TAIYIN_STATUS_OK;
    } catch (...) { return caught_status(); }
}
Status reset_casting_chart(const CastingChart& source, CastingChart* out) noexcept {
    if (!out) return TAIYIN_ERROR_INVALID_ARGUMENT;
    try { CastingChart result = source.original_chart ? *source.original_chart : source;
        *out = std::move(result); return TAIYIN_STATUS_OK;
    } catch (...) { return caught_status(); }
}
} }
