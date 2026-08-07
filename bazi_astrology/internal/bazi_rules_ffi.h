#ifndef TAIYIN_BAZI_RULES_FFI_H
#define TAIYIN_BAZI_RULES_FFI_H

#include "taiyin/bazi/bazi.h"

#include <cstddef>
#include <cstdint>

#if defined(__GNUC__) && !defined(_WIN32)
#define TAIYIN_BAZI_RULES_INTERNAL __attribute__((visibility("hidden")))
#else
#define TAIYIN_BAZI_RULES_INTERNAL
#endif

extern "C" {

TAIYIN_BAZI_RULES_INTERNAL int32_t taiyin_bazi_rules_kong_wang(
    uint8_t value,
    uint8_t out_branches[2]
);
TAIYIN_BAZI_RULES_INTERNAL int32_t taiyin_bazi_rules_ten_god(
    uint8_t day_stem_id,
    uint8_t target_stem_id,
    uint8_t* out_ten_god_id
);

TAIYIN_BAZI_RULES_INTERNAL int32_t taiyin_bazi_rules_stem_relation(
    uint8_t stem_a,
    uint8_t stem_b,
    uint32_t* out_flags,
    uint8_t* out_combined_element_id
);

TAIYIN_BAZI_RULES_INTERNAL int32_t taiyin_bazi_rules_branch_relation(
    uint8_t branch_a,
    uint8_t branch_b,
    uint32_t* out_flags,
    uint8_t* out_combined_element_id
);

TAIYIN_BAZI_RULES_INTERNAL int32_t taiyin_bazi_rules_branch_triple_relation(
    uint8_t branch_a,
    uint8_t branch_b,
    uint8_t branch_c,
    uint32_t* out_flags,
    uint8_t* out_combined_element_id
);

TAIYIN_BAZI_RULES_INTERNAL int32_t taiyin_bazi_rules_collect_relations(
    const uint8_t pillars[8],
    uint32_t pillar_mask,
    uint32_t relation_mask,
    taiyin::bazi::BaziRelation* out_relations,
    size_t capacity,
    size_t* out_count
);

TAIYIN_BAZI_RULES_INTERNAL int32_t taiyin_bazi_rules_collect_shen_sha(
    const uint8_t pillars[8],
    uint8_t target_ganzhi,
    int32_t target_kind,
    uint64_t* out_words,
    size_t word_capacity,
    size_t* out_word_count
);

TAIYIN_BAZI_RULES_INTERNAL int32_t
taiyin_bazi_rules_collect_shen_sha_with_gender(
    const uint8_t pillars[8],
    uint8_t target_ganzhi,
    int32_t target_kind,
    int32_t gender,
    uint64_t* out_words,
    size_t word_capacity,
    size_t* out_word_count
);

TAIYIN_BAZI_RULES_INTERNAL int32_t taiyin_bazi_rules_life_stage(
    uint8_t stem_id,
    uint8_t branch_id,
    int32_t earth_palace_mode,
    uint8_t* out_life_stage_id
);
TAIYIN_BAZI_RULES_INTERNAL int32_t taiyin_bazi_rules_hidden_stems(
    uint8_t branch_id,
    uint8_t* out_stems,
    uint8_t* out_count
);
TAIYIN_BAZI_RULES_INTERNAL int32_t taiyin_bazi_rules_extra_pillars(
    uint8_t year_pillar,
    uint8_t month_pillar,
    uint8_t day_pillar,
    uint8_t hour_pillar,
    uint8_t* out_ming_gong,
    uint8_t* out_shen_gong,
    uint8_t* out_tai_yuan,
    uint8_t* out_tai_xi
);

TAIYIN_BAZI_RULES_INTERNAL int32_t taiyin_bazi_rules_qiyun_direction(
    uint8_t year_pillar,
    int32_t gender,
    int32_t direction_mode,
    int32_t* out_direction
);

TAIYIN_BAZI_RULES_INTERNAL int32_t taiyin_bazi_rules_dayun_ganzhi(
    uint8_t month_pillar,
    int32_t direction,
    uint32_t one_based_index,
    uint8_t* out_ganzhi
);

TAIYIN_BAZI_RULES_INTERNAL int32_t taiyin_bazi_rules_siling_segment(
    int32_t table_model,
    uint8_t month_branch_id,
    uint8_t segment_index,
    uint8_t* out_segment_count,
    uint8_t* out_stem_id,
    uint8_t* out_origin_kind,
    double* out_duration_days
);

TAIYIN_BAZI_RULES_INTERNAL int32_t taiyin_bazi_rules_select_siling(
    int32_t table_model,
    uint8_t month_branch_id,
    double day_coordinate,
    uint8_t* out_segment_index,
    uint8_t* out_stem_id,
    uint8_t* out_origin_kind,
    double* out_start_day,
    double* out_end_day
);

}

#undef TAIYIN_BAZI_RULES_INTERNAL

#endif
