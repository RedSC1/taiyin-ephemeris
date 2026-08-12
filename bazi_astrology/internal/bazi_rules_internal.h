#ifndef TAIYIN_BAZI_RULES_INTERNAL_H
#define TAIYIN_BAZI_RULES_INTERNAL_H

#include "taiyin/bazi/bazi.h"

#include <cstddef>
#include <cstdint>

#if defined(__GNUC__) || defined(__clang__)
#define TAIYIN_BAZI_RULES_INTERNAL_API __attribute__((visibility("hidden")))
#else
#define TAIYIN_BAZI_RULES_INTERNAL_API
#endif

namespace taiyin {
namespace bazi {
namespace rules {

TAIYIN_BAZI_RULES_INTERNAL_API int32_t kong_wang(
    uint8_t value, uint8_t out_branches[2]) noexcept;
TAIYIN_BAZI_RULES_INTERNAL_API int32_t ten_god(
    uint8_t day_stem_id, uint8_t target_stem_id,
    uint8_t* out_ten_god_id) noexcept;
TAIYIN_BAZI_RULES_INTERNAL_API int32_t stem_relation(
    uint8_t stem_a, uint8_t stem_b, uint32_t* out_flags,
    uint8_t* out_combined_element_id) noexcept;
TAIYIN_BAZI_RULES_INTERNAL_API int32_t branch_relation(
    uint8_t branch_a, uint8_t branch_b, uint32_t* out_flags,
    uint8_t* out_combined_element_id) noexcept;
TAIYIN_BAZI_RULES_INTERNAL_API int32_t branch_triple_relation(
    uint8_t branch_a, uint8_t branch_b, uint8_t branch_c,
    uint32_t* out_flags, uint8_t* out_combined_element_id) noexcept;
TAIYIN_BAZI_RULES_INTERNAL_API int32_t collect_relations(
    const uint8_t pillars[8], uint32_t pillar_mask, uint32_t relation_mask,
    BaziRelation* out_relations, std::size_t capacity,
    std::size_t* out_count) noexcept;
TAIYIN_BAZI_RULES_INTERNAL_API int32_t life_stage(
    uint8_t stem_id, uint8_t branch_id, int32_t earth_palace_mode,
    uint8_t* out_life_stage_id) noexcept;
TAIYIN_BAZI_RULES_INTERNAL_API int32_t hidden_stems(
    uint8_t branch_id, uint8_t out_stems[3], uint8_t* out_count) noexcept;
TAIYIN_BAZI_RULES_INTERNAL_API int32_t extra_pillars(
    uint8_t year_pillar, uint8_t month_pillar, uint8_t day_pillar,
    uint8_t hour_pillar, uint8_t* out_ming_gong, uint8_t* out_shen_gong,
    uint8_t* out_tai_yuan, uint8_t* out_tai_xi) noexcept;
TAIYIN_BAZI_RULES_INTERNAL_API int32_t qiyun_direction(
    uint8_t year_pillar, int32_t gender, int32_t direction_mode,
    int32_t* out_direction) noexcept;
TAIYIN_BAZI_RULES_INTERNAL_API int32_t dayun_ganzhi(
    uint8_t month_pillar, int32_t direction, uint32_t one_based_index,
    uint8_t* out_ganzhi) noexcept;
TAIYIN_BAZI_RULES_INTERNAL_API int32_t siling_segment(
    int32_t table_model, uint8_t month_branch_id, uint8_t segment_index,
    uint8_t* out_segment_count, uint8_t* out_stem_id,
    uint8_t* out_origin_kind, double* out_duration_days) noexcept;
TAIYIN_BAZI_RULES_INTERNAL_API int32_t select_siling(
    int32_t table_model, uint8_t month_branch_id, double day_coordinate,
    uint8_t* out_segment_index, uint8_t* out_stem_id,
    uint8_t* out_origin_kind, double* out_start_day,
    double* out_end_day) noexcept;
TAIYIN_BAZI_RULES_INTERNAL_API int32_t collect_shen_sha(
    const uint8_t pillars[8], uint8_t target_ganzhi, int32_t target_kind,
    uint64_t* out_words, std::size_t word_capacity,
    std::size_t* out_word_count) noexcept;
TAIYIN_BAZI_RULES_INTERNAL_API int32_t collect_shen_sha_with_gender(
    const uint8_t pillars[8], uint8_t target_ganzhi, int32_t target_kind,
    int32_t gender, uint64_t* out_words, std::size_t word_capacity,
    std::size_t* out_word_count) noexcept;

}  // namespace rules
}  // namespace bazi
}  // namespace taiyin

#undef TAIYIN_BAZI_RULES_INTERNAL_API

#endif
