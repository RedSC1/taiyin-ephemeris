#include "bazi_rules_internal.h"

#include <cmath>
#include <cstring>

namespace taiyin {
namespace bazi {
namespace rules {
namespace {

constexpr uint8_t kInvalid = 0xffu;
constexpr std::size_t kMaxRelationNodes = 8u;
constexpr std::size_t kMaxRelations = 512u;

constexpr uint8_t kHiddenStemCount[12] = {1, 3, 3, 1, 3, 3, 2, 3, 3, 1, 3, 2};
constexpr uint8_t kHiddenStems[12][3] = {
    {9, kInvalid, kInvalid}, {5, 9, 7}, {0, 2, 4}, {1, kInvalid, kInvalid},
    {4, 1, 9}, {2, 6, 4}, {3, 5, kInvalid}, {5, 3, 1},
    {6, 8, 4}, {7, kInvalid, kInvalid}, {4, 7, 3}, {8, 0, kInvalid},
};
constexpr uint8_t kBranchCombinationPartner[12] = {1, 0, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2};
constexpr uint8_t kStemCombinationPartner[10] = {5, 6, 7, 8, 9, 0, 1, 2, 3, 4};
constexpr uint8_t kStemCombinationElement[10] = {3, 2, 0, 1, 4, 3, 2, 0, 1, 4};
constexpr int8_t kStemClashPartner[10] = {6, 7, 8, 9, -1, -1, 0, 1, 2, 3};
constexpr uint8_t kBranchCombinationElement[12] = {3, 3, 1, 4, 2, 0, 3, 3, 0, 2, 4, 1};
constexpr uint8_t kBranchClashPartner[12] = {6, 7, 8, 9, 10, 11, 0, 1, 2, 3, 4, 5};
constexpr uint8_t kBranchHarmPartner[12] = {7, 6, 5, 4, 3, 2, 1, 0, 11, 10, 9, 8};
constexpr uint8_t kBranchDestructionPartner[12] = {9, 4, 11, 6, 1, 8, 3, 10, 5, 0, 7, 2};
constexpr int8_t kBranchSeverancePartner[12] = {5, -1, 9, 8, -1, 0, 11, -1, 3, 2, -1, 6};
constexpr int8_t kBranchHiddenCombinationPartner[12] = {5, 2, 1, 8, -1, 0, 11, -1, 3, -1, -1, 6};
constexpr uint8_t kBranchTripleCombination[4][3] = {{8, 0, 4}, {11, 3, 7}, {2, 6, 10}, {5, 9, 1}};
constexpr uint8_t kBranchTripleDirection[4][3] = {{11, 0, 1}, {2, 3, 4}, {5, 6, 7}, {8, 9, 10}};
constexpr uint8_t kTripleElement[4] = {0, 1, 4, 2};
constexpr uint8_t kBranchTriplePunishment[2][3] = {{2, 5, 8}, {1, 10, 7}};
constexpr uint8_t kLifeStageStartFireEarth[10] = {11, 6, 2, 9, 2, 9, 5, 0, 8, 3};
constexpr uint8_t kNayinElementByPair[30] = {
    2, 4, 1, 3, 2, 4, 0, 3, 2, 1, 0, 3, 4, 1, 0,
    2, 4, 1, 3, 2, 4, 0, 3, 2, 1, 0, 3, 4, 1, 0,
};
constexpr uint8_t kStemElement[10] = {1, 1, 4, 4, 3, 3, 2, 2, 0, 0};
constexpr uint8_t kNayinZhangSheng[5] = {8, 11, 5, 8, 2};
constexpr uint8_t kNayinLinGuan[5] = {11, 2, 8, 11, 5};
constexpr uint8_t kNayinDiWang[5] = {0, 3, 9, 0, 6};
constexpr uint8_t kOfficialElement[5] = {3, 2, 4, 1, 0};
constexpr uint8_t kOfficialStem[10] = {7, 6, 9, 8, 1, 0, 3, 2, 5, 4};
constexpr uint8_t kSilingSegmentCount[2][12] = {
    {2, 3, 3, 2, 3, 3, 2, 3, 3, 2, 3, 3},
    {2, 3, 3, 2, 3, 3, 3, 3, 3, 2, 3, 3},
};
constexpr uint8_t kSilingStem[2][12][3] = {
    {{8, 9, kInvalid}, {9, 6, 5}, {4, 2, 0}, {0, 1, kInvalid},
     {1, 8, 4}, {4, 6, 2}, {2, 3, kInvalid}, {3, 0, 5},
     {4, 8, 6}, {6, 7, kInvalid}, {7, 2, 4}, {4, 0, 8}},
    {{8, 9, kInvalid}, {9, 7, 5}, {4, 2, 0}, {0, 1, kInvalid},
     {1, 9, 4}, {4, 6, 2}, {2, 5, 3}, {3, 1, 5},
     {4, 8, 6}, {6, 7, kInvalid}, {7, 3, 4}, {4, 0, 8}},
};
constexpr uint8_t kSilingDuration[2][12][3] = {
    {{7, 23, 0}, {7, 5, 18}, {5, 5, 20}, {7, 23, 0},
     {7, 5, 18}, {7, 5, 18}, {7, 23, 0}, {7, 5, 18},
     {5, 5, 20}, {7, 23, 0}, {7, 5, 18}, {5, 5, 20}},
    {{10, 20, 0}, {9, 3, 18}, {7, 7, 16}, {10, 20, 0},
     {9, 3, 18}, {5, 9, 16}, {10, 9, 11}, {9, 3, 18},
     {10, 3, 17}, {10, 20, 0}, {9, 3, 18}, {7, 5, 18}},
};

bool valid_stem(uint8_t value) noexcept { return value < 10u; }
bool valid_branch(uint8_t value) noexcept { return value < 12u; }
uint8_t stem_of(uint8_t value) noexcept { return static_cast<uint8_t>(value >> 4); }
uint8_t branch_of(uint8_t value) noexcept { return static_cast<uint8_t>(value & 0x0fu); }
bool valid_ganzhi(uint8_t value) noexcept {
    return valid_stem(stem_of(value)) && valid_branch(branch_of(value))
        && ((stem_of(value) & 1u) == (branch_of(value) & 1u));
}

int32_t ganzhi_index(uint8_t value, int32_t* out_index) noexcept {
    if (!out_index || !valid_ganzhi(value)) return -1;
    *out_index = (6 * static_cast<int32_t>(stem_of(value))
        - 5 * static_cast<int32_t>(branch_of(value)) + 60) % 60;
    return 0;
}

bool is_pair(uint8_t a, uint8_t b, uint8_t left, uint8_t right) noexcept {
    return (a == left && b == right) || (a == right && b == left);
}

bool is_branch_punishment(uint8_t a, uint8_t b) noexcept {
    return is_pair(a, b, 0, 3) || is_pair(a, b, 2, 5) || is_pair(a, b, 2, 8)
        || is_pair(a, b, 5, 8) || is_pair(a, b, 1, 10) || is_pair(a, b, 1, 7)
        || is_pair(a, b, 7, 10);
}

bool is_self_punishment(uint8_t value) noexcept {
    return value == 4u || value == 6u || value == 9u || value == 11u;
}

bool matches_triple(uint8_t a, uint8_t b, uint8_t c, const uint8_t group[3]) noexcept {
    return a != b && a != c && b != c
        && (a == group[0] || a == group[1] || a == group[2])
        && (b == group[0] || b == group[1] || b == group[2])
        && (c == group[0] || c == group[1] || c == group[2]);
}

}  // namespace

int32_t kong_wang(uint8_t value, uint8_t out_branches[2]) noexcept {
    if (!out_branches) return -1;
    int32_t index = 0;
    if (ganzhi_index(value, &index) != 0) return -1;
    const uint8_t first = static_cast<uint8_t>((10 - (index / 10) * 2 + 12) % 12);
    const uint8_t second = static_cast<uint8_t>((first + 1u) % 12u);
    if ((stem_of(value) & 1u) == (first & 1u)) {
        out_branches[0] = first;
        out_branches[1] = second;
    } else {
        out_branches[0] = second;
        out_branches[1] = first;
    }
    return 0;
}

int32_t ten_god(uint8_t day_stem_id, uint8_t target_stem_id, uint8_t* out_ten_god_id) noexcept {
    if (!out_ten_god_id || !valid_stem(day_stem_id) || !valid_stem(target_stem_id)) return -1;
    const uint8_t delta = static_cast<uint8_t>(((target_stem_id >> 1) + 5u - (day_stem_id >> 1)) % 5u);
    *out_ten_god_id = static_cast<uint8_t>((delta << 1) | ((day_stem_id ^ target_stem_id) & 1u));
    return 0;
}

int32_t stem_relation(uint8_t stem_a, uint8_t stem_b, uint32_t* out_flags, uint8_t* out_combined_element_id) noexcept {
    if (!out_flags || !out_combined_element_id || !valid_stem(stem_a) || !valid_stem(stem_b)) return -1;
    uint32_t flags = 0u;
    *out_combined_element_id = kInvalid;
    if (kStemCombinationPartner[stem_a] == stem_b) {
        flags |= BaziStemRelationCombination;
        *out_combined_element_id = kStemCombinationElement[stem_a];
    }
    if (kStemClashPartner[stem_a] >= 0 && kStemClashPartner[stem_a] == stem_b) flags |= BaziStemRelationClash;
    if ((stem_a + 4u) % 10u == stem_b || (stem_b + 4u) % 10u == stem_a) flags |= BaziStemRelationRestraint;
    *out_flags = flags;
    return 0;
}

int32_t branch_relation(uint8_t branch_a, uint8_t branch_b, uint32_t* out_flags, uint8_t* out_combined_element_id) noexcept {
    if (!out_flags || !out_combined_element_id || !valid_branch(branch_a) || !valid_branch(branch_b)) return -1;
    uint32_t flags = 0u;
    *out_combined_element_id = kInvalid;
    if (kBranchCombinationPartner[branch_a] == branch_b) {
        flags |= BaziBranchRelationCombination;
        *out_combined_element_id = kBranchCombinationElement[branch_a];
    }
    if (kBranchClashPartner[branch_a] == branch_b) flags |= BaziBranchRelationClash;
    if (kBranchHarmPartner[branch_a] == branch_b) flags |= BaziBranchRelationHarm;
    if (kBranchDestructionPartner[branch_a] == branch_b) flags |= BaziBranchRelationDestruction;
    if (branch_a != branch_b && is_branch_punishment(branch_a, branch_b)) flags |= BaziBranchRelationPunishment;
    if (branch_a == branch_b && is_self_punishment(branch_a)) flags |= BaziBranchRelationSelfPunishment;
    if (kBranchHiddenCombinationPartner[branch_a] >= 0 && kBranchHiddenCombinationPartner[branch_a] == branch_b) flags |= BaziBranchRelationHiddenCombination;
    if (kBranchSeverancePartner[branch_a] >= 0 && kBranchSeverancePartner[branch_a] == branch_b) flags |= BaziBranchRelationSeverance;
    *out_flags = flags;
    return 0;
}

int32_t branch_triple_relation(uint8_t branch_a, uint8_t branch_b, uint8_t branch_c, uint32_t* out_flags, uint8_t* out_combined_element_id) noexcept {
    if (!out_flags || !out_combined_element_id || !valid_branch(branch_a)
        || !valid_branch(branch_b) || !valid_branch(branch_c)) return -1;
    uint32_t flags = 0u;
    *out_combined_element_id = kInvalid;
    for (std::size_t i = 0; i < 4u; ++i) {
        if (matches_triple(branch_a, branch_b, branch_c, kBranchTripleCombination[i])) {
            flags |= BaziBranchTripleRelationCombination;
            *out_combined_element_id = kTripleElement[i];
        }
        if (matches_triple(branch_a, branch_b, branch_c, kBranchTripleDirection[i])) {
            flags |= BaziBranchTripleRelationDirection;
            *out_combined_element_id = kTripleElement[i];
        }
    }
    for (std::size_t i = 0; i < 2u; ++i) {
        if (matches_triple(branch_a, branch_b, branch_c, kBranchTriplePunishment[i])) flags |= BaziBranchTripleRelationPunishment;
    }
    *out_flags = flags;
    return 0;
}

int32_t life_stage(uint8_t stem_id, uint8_t branch_id, int32_t earth_palace_mode, uint8_t* out_life_stage_id) noexcept {
    if (!out_life_stage_id || !valid_stem(stem_id) || !valid_branch(branch_id)
        || earth_palace_mode < BaziEarthPalaceFireEarth || earth_palace_mode > BaziEarthPalaceWaterEarth) return -1;
    uint8_t start = kLifeStageStartFireEarth[stem_id];
    if (earth_palace_mode == BaziEarthPalaceWaterEarth) {
        if (stem_id == 4u) start = 8u;
        if (stem_id == 5u) start = 3u;
    }
    *out_life_stage_id = (stem_id & 1u) == 0u
        ? static_cast<uint8_t>((branch_id + 12u - start) % 12u)
        : static_cast<uint8_t>((start + 12u - branch_id) % 12u);
    return 0;
}

int32_t hidden_stems(uint8_t branch_id, uint8_t out_stems[3], uint8_t* out_count) noexcept {
    if (!out_stems || !out_count || !valid_branch(branch_id)) return -1;
    std::memcpy(out_stems, kHiddenStems[branch_id], 3u);
    *out_count = kHiddenStemCount[branch_id];
    return 0;
}

int32_t extra_pillars(uint8_t year_pillar, uint8_t month_pillar, uint8_t day_pillar, uint8_t hour_pillar, uint8_t* out_ming_gong, uint8_t* out_shen_gong, uint8_t* out_tai_yuan, uint8_t* out_tai_xi) noexcept {
    if (!out_ming_gong || !out_shen_gong || !out_tai_yuan || !out_tai_xi
        || !valid_ganzhi(year_pillar) || !valid_ganzhi(month_pillar)
        || !valid_ganzhi(day_pillar) || !valid_ganzhi(hour_pillar)) return -1;
    const uint8_t month_branch = branch_of(month_pillar);
    const uint8_t hour_branch = branch_of(hour_pillar);
    const uint8_t month_number = static_cast<uint8_t>(((month_branch + 10u) % 12u) + 1u);
    const uint8_t month_position = static_cast<uint8_t>((12u - (month_number - 1u)) % 12u);
    const uint8_t ming_branch = static_cast<uint8_t>((month_position + ((3u + 12u - hour_branch) % 12u)) % 12u);
    const uint8_t shen_branch = static_cast<uint8_t>((month_branch + hour_branch + 1u) % 12u);
    const uint8_t start_stem = static_cast<uint8_t>(((stem_of(year_pillar) % 5u) * 2u + 2u) % 10u);
    const uint8_t ming_stem = static_cast<uint8_t>((start_stem + ((ming_branch + 10u) % 12u)) % 10u);
    const uint8_t shen_stem = static_cast<uint8_t>((start_stem + ((shen_branch + 10u) % 12u)) % 10u);
    if (chinese_calendar::make_ganzhi(ming_stem, ming_branch, out_ming_gong)
            != TAIYIN_STATUS_OK
        || chinese_calendar::make_ganzhi(shen_stem, shen_branch, out_shen_gong)
            != TAIYIN_STATUS_OK
        || chinese_calendar::advance_ganzhi(month_pillar, -9, out_tai_yuan)
            != TAIYIN_STATUS_OK
        || chinese_calendar::make_ganzhi(
            static_cast<uint8_t>((stem_of(day_pillar) + 5u) % 10u),
            kBranchCombinationPartner[branch_of(day_pillar)], out_tai_xi)
            != TAIYIN_STATUS_OK) return -1;
    return 0;
}

int32_t qiyun_direction(uint8_t year_pillar, int32_t gender, int32_t direction_mode, int32_t* out_direction) noexcept {
    if (!out_direction || !valid_ganzhi(year_pillar) || gender < BaziGenderFemale
        || gender > BaziGenderMale || direction_mode != BaziQiYunDirectionYearStemGender) return -1;
    *out_direction = ((stem_of(year_pillar) & 1u) == 0u) == (gender == BaziGenderMale) ? 1 : -1;
    return 0;
}

int32_t dayun_ganzhi(uint8_t month_pillar, int32_t direction, uint32_t one_based_index, uint8_t* out_ganzhi) noexcept {
    if (!out_ganzhi || !valid_ganzhi(month_pillar) || (direction != -1 && direction != 1) || one_based_index == 0u) return -1;
    return chinese_calendar::advance_ganzhi(
        month_pillar,
        direction * static_cast<int32_t>(one_based_index % 60u),
        out_ganzhi) == TAIYIN_STATUS_OK ? 0 : -1;
}

static uint8_t siling_origin(
    int32_t table_model, uint8_t branch_id, uint8_t segment_index) noexcept {
    if (table_model == BaziRenyuanSilingSanMingTongHui && branch_id == 2u && segment_index == 0u) return BaziRenyuanSilingOriginGenEarth;
    if (table_model == BaziRenyuanSilingSanMingTongHui && branch_id == 8u && segment_index == 0u) return BaziRenyuanSilingOriginKunEarth;
    return BaziRenyuanSilingOriginStem;
}

int32_t siling_segment(int32_t table_model, uint8_t month_branch_id, uint8_t segment_index, uint8_t* out_segment_count, uint8_t* out_stem_id, uint8_t* out_origin_kind, double* out_duration_days) noexcept {
    if (!out_segment_count || !out_stem_id || !out_origin_kind || !out_duration_days
        || table_model < BaziRenyuanSilingSanMingTongHui || table_model > BaziRenyuanSilingCommon
        || !valid_branch(month_branch_id) || segment_index >= kSilingSegmentCount[table_model][month_branch_id]) return -1;
    *out_segment_count = kSilingSegmentCount[table_model][month_branch_id];
    *out_stem_id = kSilingStem[table_model][month_branch_id][segment_index];
    *out_origin_kind = siling_origin(table_model, month_branch_id, segment_index);
    *out_duration_days = static_cast<double>(kSilingDuration[table_model][month_branch_id][segment_index]);
    return 0;
}

int32_t select_siling(int32_t table_model, uint8_t month_branch_id, double day_coordinate, uint8_t* out_segment_index, uint8_t* out_stem_id, uint8_t* out_origin_kind, double* out_start_day, double* out_end_day) noexcept {
    if (!out_segment_index || !out_stem_id || !out_origin_kind || !out_start_day || !out_end_day
        || table_model < BaziRenyuanSilingSanMingTongHui || table_model > BaziRenyuanSilingCommon
        || !valid_branch(month_branch_id) || !std::isfinite(day_coordinate) || day_coordinate < 0.0) return -1;
    const uint8_t count = kSilingSegmentCount[table_model][month_branch_id];
    double start = 0.0;
    for (uint8_t i = 0; i < count; ++i) {
        const double end = start + kSilingDuration[table_model][month_branch_id][i];
        if (day_coordinate < end || i + 1u == count) {
            *out_segment_index = i;
            *out_stem_id = kSilingStem[table_model][month_branch_id][i];
            *out_origin_kind = siling_origin(table_model, month_branch_id, i);
            *out_start_day = start;
            *out_end_day = end;
            return 0;
        }
        start = end;
    }
    return -1;
}

}  // namespace rules
}  // namespace bazi
}  // namespace taiyin

namespace taiyin {
namespace bazi {
namespace rules {
namespace {

#include "../generated/taiyin_bazi_shen_sha_tables.h"

constexpr std::size_t kShenShaStableIdCount = 66u;
constexpr std::size_t kShenShaWordCount = 2u;

bool mask_contains_branch(uint16_t mask, uint8_t branch_id) noexcept {
    return (mask & static_cast<uint16_t>(1u << branch_id)) != 0u;
}

bool mask_contains_stem(uint16_t mask, uint8_t stem_id) noexcept {
    return (mask & static_cast<uint16_t>(1u << stem_id)) != 0u;
}

bool mask_contains_ganzhi(uint64_t mask, uint8_t value) noexcept {
    int32_t index = 0;
    return ganzhi_index(value, &index) == 0 && (mask & (UINT64_C(1) << index)) != 0u;
}

uint8_t season_of_month_branch(uint8_t branch_id) noexcept {
    return static_cast<uint8_t>(((branch_id + 10u) % 12u) / 3u);
}

bool same_xun(uint8_t first, uint8_t second) noexcept {
    int32_t first_index = 0;
    int32_t second_index = 0;
    return ganzhi_index(first, &first_index) == 0 && ganzhi_index(second, &second_index) == 0
        && first_index / 10 == second_index / 10;
}

bool kong_wang_contains(uint8_t base, uint8_t target) noexcept {
    int32_t index = 0;
    if (ganzhi_index(base, &index) != 0) return false;
    const uint8_t first = static_cast<uint8_t>((10 - (index / 10) * 2 + 12) % 12);
    return branch_of(target) == first || branch_of(target) == static_cast<uint8_t>((first + 1u) % 12u);
}

bool is_xun_food_god(uint8_t base, uint8_t target) noexcept {
    return same_xun(base, target)
        && stem_of(target) == static_cast<uint8_t>((stem_of(base) + 2u) % 10u);
}

bool is_tian_de_he(uint8_t month_branch, uint8_t target) noexcept {
    switch (month_branch) {
        case 2: return stem_of(target) == 8u;
        case 3: return branch_of(target) == 5u;
        case 4: return stem_of(target) == 3u;
        case 5: return stem_of(target) == 2u;
        case 6: return branch_of(target) == 2u;
        case 7: return stem_of(target) == 5u;
        case 8: return stem_of(target) == 4u;
        case 9: return branch_of(target) == 11u;
        case 10: return stem_of(target) == 7u;
        case 11: return stem_of(target) == 6u;
        case 0: return branch_of(target) == 8u;
        case 1: return stem_of(target) == 1u;
        default: return false;
    }
}

bool is_tian_de_gui_ren(uint8_t month_branch, uint8_t target) noexcept {
    switch (month_branch) {
        case 2: return stem_of(target) == 3u;
        case 3: return branch_of(target) == 8u;
        case 4: return stem_of(target) == 8u;
        case 5: return stem_of(target) == 7u;
        case 6: return branch_of(target) == 11u;
        case 7: return stem_of(target) == 0u;
        case 8: return stem_of(target) == 9u;
        case 9: return branch_of(target) == 2u;
        case 10: return stem_of(target) == 2u;
        case 11: return stem_of(target) == 1u;
        case 0: return branch_of(target) == 5u;
        case 1: return stem_of(target) == 6u;
        default: return false;
    }
}

uint8_t nayin_element_of(uint8_t value) noexcept {
    int32_t index = 0;
    return ganzhi_index(value, &index) == 0 ? kNayinElementByPair[index / 2] : kInvalid;
}

bool is_nayin_school_stage(uint8_t base, uint8_t target, uint8_t stage) noexcept {
    const uint8_t base_element = nayin_element_of(base);
    const uint8_t target_element = nayin_element_of(target);
    if (base_element == kInvalid || target_element == kInvalid || base_element != target_element) return false;
    const uint8_t expected = stage == 0u ? kNayinZhangSheng[base_element] : kNayinLinGuan[base_element];
    return branch_of(target) == expected;
}

bool is_official_school_stage(uint8_t day_stem, uint8_t target, uint8_t stage) noexcept {
    const uint8_t element = kOfficialElement[kStemElement[day_stem]];
    return branch_of(target) == (stage == 0u ? kNayinZhangSheng[element] : kNayinLinGuan[element]);
}

bool is_official_star_school(uint8_t day_stem, uint8_t target) noexcept {
    return branch_of(target) == kNayinZhangSheng[kStemElement[day_stem]]
        && stem_of(target) == kOfficialStem[day_stem];
}

bool is_nayin_noble(uint8_t year, uint8_t day, uint8_t target) noexcept {
    const uint8_t branch = branch_of(target);
    return branch == kNayinDiWang[nayin_element_of(year)]
        && (mask_contains_branch(kShenShaTianYiGuiRenMasks[stem_of(year)], branch)
            || mask_contains_branch(kShenShaTianYiGuiRenMasks[stem_of(day)], branch));
}

bool unordered_branch_pair(uint8_t first, uint8_t second, uint8_t left, uint8_t right) noexcept {
    return (first == left && second == right) || (first == right && second == left);
}

void set_bit(uint64_t words[kShenShaWordCount], uint8_t id) noexcept {
    words[id / 64u] |= UINT64_C(1) << (id % 64u);
}

bool has_stem(uint8_t first, uint8_t second, uint8_t third, uint8_t fourth, uint8_t value) noexcept {
    return first == value || second == value || third == value || fourth == value;
}

int32_t collect_shen_sha_impl(const uint8_t pillars[8], uint8_t target, int32_t target_kind,
                              int32_t gender, uint64_t* out_words,
                              std::size_t word_capacity, std::size_t* out_word_count) noexcept {
    if (out_word_count) *out_word_count = 0u;
    if (!pillars || !out_word_count || (!out_words && word_capacity != 0u)
        || target_kind < BaziShenShaTargetYear || target_kind > BaziShenShaTargetFlowHour
        || gender < -1 || gender > BaziGenderMale || !valid_ganzhi(target)) return -1;
    const uint8_t year = pillars[0];
    const uint8_t month = pillars[1];
    const uint8_t day = pillars[2];
    const uint8_t hour = pillars[3];
    if (!valid_ganzhi(year) || !valid_ganzhi(month) || !valid_ganzhi(day)
        || (gender >= 0 && !valid_ganzhi(hour))) return -1;
    *out_word_count = kShenShaWordCount;
    if (!out_words) return 0;
    if (word_capacity < kShenShaWordCount) return -2;
    out_words[0] = 0u;
    out_words[1] = 0u;
    const uint8_t target_branch = branch_of(target);
    const uint8_t target_stem = stem_of(target);
    const uint8_t year_stem = stem_of(year);
    const uint8_t day_stem = stem_of(day);
    const uint8_t year_branch = branch_of(year);
    const uint8_t month_branch = branch_of(month);
    const uint8_t season = season_of_month_branch(month_branch);

    if (mask_contains_branch(kShenShaTianYiGuiRenMasks[year_stem], target_branch)
        || mask_contains_branch(kShenShaTianYiGuiRenMasks[day_stem], target_branch)) set_bit(out_words, 0);
    if (mask_contains_branch(kShenShaYiMaMasks[year_branch], target_branch)
        || mask_contains_branch(kShenShaYiMaMasks[branch_of(day)], target_branch)) set_bit(out_words, 1);
    if (mask_contains_branch(kShenShaXianChiTaoHuaMasks[year_branch], target_branch)
        || mask_contains_branch(kShenShaXianChiTaoHuaMasks[branch_of(day)], target_branch)) set_bit(out_words, 2);
    if (mask_contains_branch(kShenShaHongLuanMasks[year_branch], target_branch)) set_bit(out_words, 3);
    if (mask_contains_branch(kShenShaTianXiMasks[year_branch], target_branch)) set_bit(out_words, 4);
    if (mask_contains_branch(kShenShaYangRenMasks[day_stem], target_branch)) set_bit(out_words, 5);
    if (mask_contains_branch(kShenShaFeiRenMasks[day_stem], target_branch)) set_bit(out_words, 6);
    if (mask_contains_branch(kShenShaFuXingGuiRenMasks[year_stem], target_branch)
        || mask_contains_branch(kShenShaFuXingGuiRenMasks[day_stem], target_branch)) set_bit(out_words, 7);
    if (mask_contains_branch(kShenShaZaiShaMasks[year_branch], target_branch)
        || mask_contains_branch(kShenShaZaiShaMasks[branch_of(day)], target_branch)) set_bit(out_words, 8);
    if (mask_contains_branch(kShenShaJieShaMasks[year_branch], target_branch)
        || mask_contains_branch(kShenShaJieShaMasks[branch_of(day)], target_branch)) set_bit(out_words, 9);
    if (mask_contains_branch(kShenShaWangShenMasks[year_branch], target_branch)
        || mask_contains_branch(kShenShaWangShenMasks[branch_of(day)], target_branch)) set_bit(out_words, 10);
    if (kong_wang_contains(year, target) || kong_wang_contains(day, target)) set_bit(out_words, 11);
    if (is_xun_food_god(year, target) || is_xun_food_god(day, target)) set_bit(out_words, 12);
    if (mask_contains_branch(kShenShaTianChuGuiRenMasks[year_stem], target_branch)
        || mask_contains_branch(kShenShaTianChuGuiRenMasks[day_stem], target_branch)) set_bit(out_words, 13);
    if (mask_contains_stem(kShenShaDeXiuGuiRenMasks[month_branch], target_stem)) set_bit(out_words, 14);
    if (mask_contains_branch(kShenShaTianYiMedicineMasks[month_branch], target_branch)) set_bit(out_words, 15);
    if (mask_contains_branch(kShenShaXueRenMasks[month_branch], target_branch)) set_bit(out_words, 16);
    if (mask_contains_stem(kShenShaYueDeHeMasks[month_branch], target_stem)) set_bit(out_words, 17);

    if (gender >= 0) {
        const bool forward = (gender == BaziGenderMale) == ((year_stem & 1u) == 0u);
        const uint8_t plus_three = static_cast<uint8_t>((year_branch + 3u) % 12u);
        const uint8_t plus_nine = static_cast<uint8_t>((year_branch + 9u) % 12u);
        if ((forward && target_branch == plus_three) || (!forward && target_branch == plus_nine)) set_bit(out_words, 18);
        if ((forward && target_branch == plus_nine) || (!forward && target_branch == plus_three)) set_bit(out_words, 19);
        if (target_branch == static_cast<uint8_t>((year_branch + (forward ? 7u : 5u)) % 12u)) set_bit(out_words, 20);

        const uint8_t day_branch = branch_of(day);
        const uint8_t hour_stem = stem_of(hour);
        const uint8_t hour_branch = branch_of(hour);
        const uint8_t year_nayin = nayin_element_of(year);
        if (target_kind == BaziShenShaTargetHour && (day_stem == 0u || day_stem == 5u)
            && ((target_stem == 9u && target_branch == 9u)
                || (target_stem == 5u && target_branch == 5u)
                || (target_stem == 1u && target_branch == 1u))) set_bit(out_words, 25);
        if ((target_kind == BaziShenShaTargetDay || target_kind == BaziShenShaTargetHour)
            && (((season == 0u || season == 2u) && (target_branch == 2u || target_branch == 0u))
                || ((season == 1u || season == 3u) && (target_branch == 3u || target_branch == 7u || target_branch == 4u))
                || ((year_nayin == 2u || year_nayin == 1u) && (target_branch == 6u || target_branch == 3u))
                || ((year_nayin == 0u || year_nayin == 4u) && (target_branch == 9u || target_branch == 10u))
                || (year_nayin == 3u && (target_branch == 4u || target_branch == 5u)))) set_bit(out_words, 31);
        if (target_kind == BaziShenShaTargetDay) {
            if (has_stem(year_stem, stem_of(month), day_stem, hour_stem, 0u)
                && has_stem(year_stem, stem_of(month), day_stem, hour_stem, 4u)
                && has_stem(year_stem, stem_of(month), day_stem, hour_stem, 6u)) set_bit(out_words, 33);
            if (has_stem(year_stem, stem_of(month), day_stem, hour_stem, 1u)
                && has_stem(year_stem, stem_of(month), day_stem, hour_stem, 2u)
                && has_stem(year_stem, stem_of(month), day_stem, hour_stem, 3u)) set_bit(out_words, 34);
            if (has_stem(year_stem, stem_of(month), day_stem, hour_stem, 8u)
                && has_stem(year_stem, stem_of(month), day_stem, hour_stem, 9u)
                && has_stem(year_stem, stem_of(month), day_stem, hour_stem, 7u)) set_bit(out_words, 35);
        }
        if (year_nayin != 1u && year_nayin != 2u) {
            const uint8_t counterpart = target_branch == 10u ? 11u : target_branch == 11u ? 10u
                : target_branch == 4u ? 5u : target_branch == 5u ? 4u : kInvalid;
            const bool has_counterpart = counterpart != kInvalid
                && (year_branch == counterpart || month_branch == counterpart || day_branch == counterpart || hour_branch == counterpart);
            if (((target_branch == 10u || target_branch == 11u) && year_nayin == 4u && gender == BaziGenderMale && has_counterpart)
                || ((target_branch == 4u || target_branch == 5u) && (year_nayin == 0u || year_nayin == 3u)
                    && gender == BaziGenderFemale && has_counterpart)) set_bit(out_words, 45);
        }
        if (target_kind == BaziShenShaTargetDay && day_stem == hour_stem && day_branch != hour_branch) {
            if ((day_stem == 9u && unordered_branch_pair(day_branch, hour_branch, 11, 1))
                || (day_stem == 3u && unordered_branch_pair(day_branch, hour_branch, 5, 7))
                || (day_stem == 5u && unordered_branch_pair(day_branch, hour_branch, 7, 5))
                || (day_stem == 4u && unordered_branch_pair(day_branch, hour_branch, 4, 6))) set_bit(out_words, 48);
            if ((day_stem == 0u && (unordered_branch_pair(day_branch, hour_branch, 8, 10)
                    || unordered_branch_pair(day_branch, hour_branch, 2, 0)))
                || (day_stem == 1u && unordered_branch_pair(day_branch, hour_branch, 7, 9))
                || (day_stem == 4u && unordered_branch_pair(day_branch, hour_branch, 8, 6))
                || (day_stem == 7u && unordered_branch_pair(day_branch, hour_branch, 1, 3))) set_bit(out_words, 49);
        }
    }

    if (mask_contains_branch(kShenShaGuChenMasks[year_branch], target_branch)) set_bit(out_words, 21);
    if (mask_contains_branch(kShenShaGuaSuMasks[year_branch], target_branch)) set_bit(out_words, 22);
    if (mask_contains_branch(kShenShaHongYanShaMasks[day_stem], target_branch)) set_bit(out_words, 23);
    if (mask_contains_branch(kShenShaJinYuMasks[day_stem], target_branch)) set_bit(out_words, 24);
    if (mask_contains_branch(kShenShaLiuXiaMasks[day_stem], target_branch)) set_bit(out_words, 27);
    if (mask_contains_branch(kShenShaSangMenMasks[year_branch], target_branch)) set_bit(out_words, 28);
    if (mask_contains_branch(kShenShaDiaoKeMasks[year_branch], target_branch)) set_bit(out_words, 29);
    if (mask_contains_branch(kShenShaPiMaMasks[year_branch], target_branch)) set_bit(out_words, 30);
    if (mask_contains_branch(kShenShaJiangXingMasks[year_branch], target_branch)
        || mask_contains_branch(kShenShaJiangXingMasks[branch_of(day)], target_branch)) set_bit(out_words, 36);
    if (mask_contains_branch(kShenShaHuaGaiMasks[year_branch], target_branch)
        || mask_contains_branch(kShenShaHuaGaiMasks[branch_of(day)], target_branch)) set_bit(out_words, 37);
    if (mask_contains_branch(kShenShaTaiJiGuiRenMasks[year_stem], target_branch)
        || mask_contains_branch(kShenShaTaiJiGuiRenMasks[day_stem], target_branch)) set_bit(out_words, 52);
    if (mask_contains_branch(kShenShaWenChangGuiRenMasks[year_stem], target_branch)
        || mask_contains_branch(kShenShaWenChangGuiRenMasks[day_stem], target_branch)) set_bit(out_words, 53);
    if (mask_contains_branch(kShenShaGuoYinGuiRenMasks[year_stem], target_branch)
        || mask_contains_branch(kShenShaGuoYinGuiRenMasks[day_stem], target_branch)) set_bit(out_words, 54);
    if (mask_contains_stem(kShenShaYueDeGuiRenMasks[month_branch], target_stem)) set_bit(out_words, 56);
    if (is_tian_de_he(month_branch, target)) set_bit(out_words, 32);
    if (is_tian_de_gui_ren(month_branch, target)) set_bit(out_words, 55);
    if (mask_contains_branch(kShenShaLuShenMasks[day_stem], target_branch)) set_bit(out_words, 57);
    if (mask_contains_branch(kShenShaRiGanXueTangMasks[day_stem], target_branch)) set_bit(out_words, 58);
    if (mask_contains_branch(kShenShaRiGanCiGuanMasks[day_stem], target_branch)) set_bit(out_words, 59);
    if (is_nayin_school_stage(year, target, 0)) set_bit(out_words, 60);
    if (is_nayin_school_stage(year, target, 1)) set_bit(out_words, 61);
    if (is_official_school_stage(day_stem, target, 0)) set_bit(out_words, 62);
    if (is_official_school_stage(day_stem, target, 1)) set_bit(out_words, 63);
    if (is_official_star_school(day_stem, target)) set_bit(out_words, 64);
    if (is_nayin_noble(year, day, target)) set_bit(out_words, 65);
    if (mask_contains_ganzhi(kShenShaDiZhuanMasks[season], target)) set_bit(out_words, 50);
    if (mask_contains_ganzhi(kShenShaTianZhuanMasks[season], target)) set_bit(out_words, 51);
    if (target_kind == BaziShenShaTargetDay) {
        if (mask_contains_ganzhi(kShenShaTianSheDayMasks[season], target)) set_bit(out_words, 26);
        if (mask_contains_ganzhi(kShenShaKuiGangGanzhiMask, target)) set_bit(out_words, 38);
        if (mask_contains_ganzhi(kShenShaShiLingDayGanzhiMask, target)) set_bit(out_words, 39);
        if (mask_contains_ganzhi(kShenShaBaZhuanDayGanzhiMask, target)) set_bit(out_words, 40);
        if (mask_contains_ganzhi(kShenShaLiuXiuDayGanzhiMask, target)) set_bit(out_words, 41);
        if (mask_contains_ganzhi(kShenShaJiuChouDayGanzhiMask, target)) set_bit(out_words, 42);
        if (mask_contains_ganzhi(kShenShaSiFeiDayMasks[season], target)) set_bit(out_words, 43);
        if (mask_contains_ganzhi(kShenShaShiEDaBaiGanzhiMask, target)) set_bit(out_words, 44);
        if (mask_contains_ganzhi(kShenShaYinChaYangCuoGanzhiMask, target)) set_bit(out_words, 46);
        if (mask_contains_ganzhi(kShenShaGuLuanShaGanzhiMask, target)) set_bit(out_words, 47);
    }
    return 0;
}

}  // namespace

int32_t collect_shen_sha(const uint8_t pillars[8], uint8_t target_ganzhi, int32_t target_kind,
                         uint64_t* out_words, std::size_t word_capacity,
                         std::size_t* out_word_count) noexcept {
    return collect_shen_sha_impl(pillars, target_ganzhi, target_kind, -1,
        out_words, word_capacity, out_word_count);
}

int32_t collect_shen_sha_with_gender(const uint8_t pillars[8], uint8_t target_ganzhi,
                                     int32_t target_kind, int32_t gender, uint64_t* out_words,
                                     std::size_t word_capacity,
                                     std::size_t* out_word_count) noexcept {
    if (gender < BaziGenderFemale || gender > BaziGenderMale) {
        if (out_word_count) *out_word_count = 0u;
        return -1;
    }
    return collect_shen_sha_impl(pillars, target_ganzhi, target_kind, gender,
        out_words, word_capacity, out_word_count);
}

}  // namespace rules
}  // namespace bazi
}  // namespace taiyin

namespace taiyin {
namespace bazi {
namespace rules {
namespace {

struct RelationNode {
    uint8_t value;
    uint8_t source_id;
    uint32_t pillar_flag;
};

struct PendingRelation {
    int32_t kind;
    uint32_t pillar_mask;
    uint16_t value_mask;
    uint8_t combined_element_id;
};

uint32_t node_mask(const RelationNode* nodes, std::size_t count) noexcept {
    uint32_t result = 0u;
    for (std::size_t i = 0; i < count; ++i) result |= nodes[i].pillar_flag;
    return result;
}

uint16_t value_mask(const RelationNode* nodes, std::size_t count) noexcept {
    uint16_t result = 0u;
    for (std::size_t i = 0; i < count; ++i) result |= static_cast<uint16_t>(1u << nodes[i].value);
    return result;
}

bool relation_enabled(uint32_t mask, int32_t kind) noexcept {
    return (mask & (1u << static_cast<uint32_t>(kind))) != 0u;
}

void build_nodes(const uint8_t pillars[8], uint32_t pillar_mask, bool use_stem,
                 RelationNode out_nodes[kMaxRelationNodes], std::size_t* out_count) noexcept {
    *out_count = 0u;
    for (uint8_t i = 0; i < 8u; ++i) {
        if ((pillar_mask & (1u << i)) == 0u) continue;
        RelationNode& node = out_nodes[(*out_count)++];
        node.value = use_stem ? stem_of(pillars[i]) : branch_of(pillars[i]);
        node.source_id = i;
        node.pillar_flag = 1u << i;
    }
}

std::size_t gather_nodes(const RelationNode* source, std::size_t source_count,
                         uint8_t first, uint8_t second, bool include_second,
                         RelationNode out_nodes[kMaxRelationNodes]) noexcept {
    std::size_t count = 0u;
    for (std::size_t i = 0; i < source_count; ++i) {
        if (source[i].value == first || (include_second && source[i].value == second)) {
            out_nodes[count++] = source[i];
        }
    }
    return count;
}

bool add_relation(PendingRelation relations[kMaxRelations], std::size_t* relation_count,
                  int32_t kind, const RelationNode* nodes, std::size_t node_count,
                  uint8_t combined_element_id) noexcept {
    const PendingRelation candidate = {
        kind, node_mask(nodes, node_count), value_mask(nodes, node_count), combined_element_id};
    for (std::size_t i = 0; i < *relation_count; ++i) {
        if (relations[i].kind == candidate.kind
            && relations[i].combined_element_id == candidate.combined_element_id
            && (relations[i].value_mask & candidate.value_mask) != 0u) {
            relations[i].pillar_mask |= candidate.pillar_mask;
            relations[i].value_mask |= candidate.value_mask;
            return true;
        }
    }
    if (*relation_count >= kMaxRelations) return false;
    relations[(*relation_count)++] = candidate;
    return true;
}

bool append_stem_relations(const RelationNode* stems, std::size_t stem_count,
                           uint32_t relation_mask, PendingRelation relations[kMaxRelations],
                           std::size_t* relation_count) noexcept {
    RelationNode matched[kMaxRelationNodes];
    for (uint8_t first = 0; first < 5u; ++first) {
        if (!relation_enabled(relation_mask, BaziRelationStemCombination)) continue;
        const uint8_t second = static_cast<uint8_t>(first + 5u);
        const std::size_t count = gather_nodes(stems, stem_count, first, second, true, matched);
        const uint16_t expected = static_cast<uint16_t>((1u << first) | (1u << second));
        if (value_mask(matched, count) == expected
            && !add_relation(relations, relation_count, BaziRelationStemCombination, matched, count,
                kStemCombinationElement[first])) return false;
    }
    for (uint8_t first = 0; first < 4u; ++first) {
        if (!relation_enabled(relation_mask, BaziRelationStemClash)) continue;
        const uint8_t second = static_cast<uint8_t>(first + 6u);
        const std::size_t count = gather_nodes(stems, stem_count, first, second, true, matched);
        const uint16_t expected = static_cast<uint16_t>((1u << first) | (1u << second));
        if (value_mask(matched, count) == expected
            && !add_relation(relations, relation_count, BaziRelationStemClash, matched, count, kInvalid)) return false;
    }
    for (uint8_t first = 0; first < 10u; ++first) {
        if (!relation_enabled(relation_mask, BaziRelationStemRestraint)) continue;
        const uint8_t second = static_cast<uint8_t>((first + 4u) % 10u);
        const std::size_t count = gather_nodes(stems, stem_count, first, second, true, matched);
        const uint16_t expected = static_cast<uint16_t>((1u << first) | (1u << second));
        if (value_mask(matched, count) == expected
            && !add_relation(relations, relation_count, BaziRelationStemRestraint, matched, count, kInvalid)) return false;
    }
    return true;
}

void suppress_pair(bool suppressed[8][8], uint8_t first, uint8_t second) noexcept {
    suppressed[first][second] = true;
    suppressed[second][first] = true;
}

bool value_in_triple(uint8_t value, const uint8_t group[3]) noexcept {
    return value == group[0] || value == group[1] || value == group[2];
}

bool append_branch_relations(const RelationNode* branches, std::size_t branch_count,
                             uint32_t relation_mask, PendingRelation relations[kMaxRelations],
                             std::size_t* relation_count) noexcept {
    bool suppressed[8][8] = {};
    RelationNode trio[3];
    RelationNode pair_nodes[2];
    RelationNode matched[kMaxRelationNodes];
    for (std::size_t i = 0; i < branch_count; ++i) {
        for (std::size_t j = i + 1u; j < branch_count; ++j) {
            for (std::size_t k = j + 1u; k < branch_count; ++k) {
                trio[0] = branches[i]; trio[1] = branches[j]; trio[2] = branches[k];
                for (std::size_t group = 0; group < 4u; ++group) {
                    if (matches_triple(trio[0].value, trio[1].value, trio[2].value, kBranchTripleDirection[group])) {
                        if (relation_enabled(relation_mask, BaziRelationBranchTripleDirection)
                            && !add_relation(relations, relation_count, BaziRelationBranchTripleDirection,
                                trio, 3u, kTripleElement[group])) return false;
                        suppress_pair(suppressed, branches[i].source_id, branches[j].source_id);
                        suppress_pair(suppressed, branches[i].source_id, branches[k].source_id);
                        suppress_pair(suppressed, branches[j].source_id, branches[k].source_id);
                    }
                    if (matches_triple(trio[0].value, trio[1].value, trio[2].value, kBranchTripleCombination[group])) {
                        if (relation_enabled(relation_mask, BaziRelationBranchTripleCombination)
                            && !add_relation(relations, relation_count, BaziRelationBranchTripleCombination,
                                trio, 3u, kTripleElement[group])) return false;
                        suppress_pair(suppressed, branches[i].source_id, branches[j].source_id);
                        suppress_pair(suppressed, branches[i].source_id, branches[k].source_id);
                        suppress_pair(suppressed, branches[j].source_id, branches[k].source_id);
                    }
                }
                for (std::size_t group = 0; group < 2u; ++group) {
                    if (matches_triple(trio[0].value, trio[1].value, trio[2].value, kBranchTriplePunishment[group])) {
                        if (relation_enabled(relation_mask, BaziRelationBranchTriplePunishment)
                            && !add_relation(relations, relation_count, BaziRelationBranchTriplePunishment,
                                trio, 3u, kInvalid)) return false;
                        suppress_pair(suppressed, branches[i].source_id, branches[j].source_id);
                        suppress_pair(suppressed, branches[i].source_id, branches[k].source_id);
                        suppress_pair(suppressed, branches[j].source_id, branches[k].source_id);
                    }
                }
            }
        }
    }
    for (std::size_t i = 0; i < branch_count; ++i) {
        for (std::size_t j = i + 1u; j < branch_count; ++j) {
            pair_nodes[0] = branches[i]; pair_nodes[1] = branches[j];
            if (!suppressed[branches[i].source_id][branches[j].source_id]) {
                for (std::size_t group = 0; group < 4u; ++group) {
                    if (branches[i].value != branches[j].value
                        && value_in_triple(branches[i].value, kBranchTripleCombination[group])
                        && value_in_triple(branches[j].value, kBranchTripleCombination[group])) {
                        const int32_t kind = branches[i].value == kBranchTripleCombination[group][1]
                            || branches[j].value == kBranchTripleCombination[group][1]
                            ? BaziRelationBranchHalfCombination : BaziRelationBranchArchingCombination;
                        if (relation_enabled(relation_mask, kind)
                            && !add_relation(relations, relation_count, kind, pair_nodes, 2u,
                                kTripleElement[group])) return false;
                    }
                }
            }
            uint32_t flags = 0u;
            uint8_t element = kInvalid;
            if (branch_relation(branches[i].value, branches[j].value, &flags, &element) != 0) return false;
            const struct Mapping { uint32_t flag; int32_t kind; bool suppressible; } mappings[] = {
                {BaziBranchRelationPunishment, BaziRelationBranchPunishment, true},
                {BaziBranchRelationCombination, BaziRelationBranchCombination, false},
                {BaziBranchRelationClash, BaziRelationBranchClash, false},
                {BaziBranchRelationHarm, BaziRelationBranchHarm, false},
                {BaziBranchRelationDestruction, BaziRelationBranchDestruction, false},
                {BaziBranchRelationHiddenCombination, BaziRelationBranchHiddenCombination, false},
                {BaziBranchRelationSeverance, BaziRelationBranchSeverance, false},
            };
            for (std::size_t m = 0; m < sizeof(mappings) / sizeof(mappings[0]); ++m) {
                if ((flags & mappings[m].flag) == 0u || !relation_enabled(relation_mask, mappings[m].kind)
                    || (mappings[m].suppressible && suppressed[branches[i].source_id][branches[j].source_id])) continue;
                if (!add_relation(relations, relation_count, mappings[m].kind, pair_nodes, 2u,
                    mappings[m].kind == BaziRelationBranchCombination ? element : kInvalid)) return false;
            }
        }
    }
    const uint8_t self_values[4] = {4, 6, 9, 11};
    if (relation_enabled(relation_mask, BaziRelationBranchSelfPunishment)) {
        for (std::size_t i = 0; i < 4u; ++i) {
            const std::size_t count = gather_nodes(branches, branch_count, self_values[i], 0u, false, matched);
            if (count >= 2u && !add_relation(relations, relation_count,
                BaziRelationBranchSelfPunishment, matched, count, kInvalid)) return false;
        }
    }
    return true;
}

}  // namespace

int32_t collect_relations(const uint8_t pillars[8], uint32_t pillar_mask, uint32_t relation_mask,
                          BaziRelation* out_relations, std::size_t capacity,
                          std::size_t* out_count) noexcept {
    if (out_count) *out_count = 0u;
    if (!pillars || !out_count || pillar_mask == 0u || (pillar_mask & ~static_cast<uint32_t>(BaziRelationPillarAll)) != 0u
        || (relation_mask & ~kBaziRelationKindMaskAll) != 0u || (!out_relations && capacity != 0u)) return -1;
    for (uint8_t i = 0; i < 8u; ++i) {
        if ((pillar_mask & (1u << i)) != 0u && !valid_ganzhi(pillars[i])) return -1;
    }
    RelationNode stems[kMaxRelationNodes];
    RelationNode branches[kMaxRelationNodes];
    std::size_t stem_count = 0u;
    std::size_t branch_count = 0u;
    build_nodes(pillars, pillar_mask, true, stems, &stem_count);
    build_nodes(pillars, pillar_mask, false, branches, &branch_count);
    PendingRelation relations[kMaxRelations];
    std::size_t count = 0u;
    if (!append_stem_relations(stems, stem_count, relation_mask, relations, &count)
        || !append_branch_relations(branches, branch_count, relation_mask, relations, &count)) return -2;
    *out_count = count;
    if (!out_relations && capacity == 0u) return 0;
    if (capacity < count) return -2;
    for (std::size_t i = 0; i < count; ++i) {
        out_relations[i].kind = relations[i].kind;
        out_relations[i].pillar_mask = relations[i].pillar_mask;
        out_relations[i].combined_element_id = relations[i].combined_element_id;
        out_relations[i].reserved[0] = 0u;
        out_relations[i].reserved[1] = 0u;
        out_relations[i].reserved[2] = 0u;
    }
    return 0;
}

}  // namespace rules
}  // namespace bazi
}  // namespace taiyin
