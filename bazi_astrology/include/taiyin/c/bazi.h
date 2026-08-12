#ifndef TAIYIN_C_BAZI_H
#define TAIYIN_C_BAZI_H

#include "taiyin/c/chinese_calendar_ganzhi.h"

#ifdef __cplusplus
extern "C" {
#endif

#define TAIYIN_BAZI_HIDDEN_STEM_CAPACITY 3u
#define TAIYIN_BAZI_INVALID_WUXING 0xffu
#define TAIYIN_BAZI_RENYUAN_SILING_MAX_SEGMENTS 3u
#define TAIYIN_BAZI_SHEN_SHA_STABLE_ID_COUNT 66u
#define TAIYIN_BAZI_SHEN_SHA_WORD_COUNT 2u

typedef struct taiyin_bazi_context taiyin_bazi_context;

enum taiyin_bazi_earth_palace_mode {
    TAIYIN_BAZI_EARTH_PALACE_FIRE_EARTH = 0,
    TAIYIN_BAZI_EARTH_PALACE_WATER_EARTH = 1
};

enum taiyin_bazi_gender {
    TAIYIN_BAZI_GENDER_FEMALE = 0,
    TAIYIN_BAZI_GENDER_MALE = 1
};

enum taiyin_bazi_qiyun_direction_mode {
    TAIYIN_BAZI_QIYUN_DIRECTION_YEAR_STEM_GENDER = 0
};

enum taiyin_bazi_qiyun_time_model {
    TAIYIN_BAZI_QIYUN_TRADITIONAL_CALENDAR = 0,
    TAIYIN_BAZI_QIYUN_JULIAN_YEAR = 1,
    TAIYIN_BAZI_QIYUN_TROPICAL_YEAR = 2
};

enum taiyin_bazi_dayun_boundary_model {
    TAIYIN_BAZI_DAYUN_CIVIL_YEARS = 0,
    TAIYIN_BAZI_DAYUN_JULIAN_YEARS = 1,
    TAIYIN_BAZI_DAYUN_TROPICAL_YEARS = 2
};

enum taiyin_bazi_renyuan_siling_table_model {
    TAIYIN_BAZI_RENYUAN_SILING_SAN_MING_TONG_HUI = 0,
    TAIYIN_BAZI_RENYUAN_SILING_COMMON = 1
};

enum taiyin_bazi_renyuan_siling_time_model {
    TAIYIN_BAZI_RENYUAN_SILING_ELAPSED_24_HOURS = 0,
    TAIYIN_BAZI_RENYUAN_SILING_LOCAL_CIVIL_DAYS = 1
};

enum taiyin_bazi_renyuan_siling_origin_kind {
    TAIYIN_BAZI_RENYUAN_SILING_ORIGIN_STEM = 0,
    TAIYIN_BAZI_RENYUAN_SILING_ORIGIN_GEN_EARTH = 1,
    TAIYIN_BAZI_RENYUAN_SILING_ORIGIN_KUN_EARTH = 2
};

enum taiyin_bazi_wuxing {
    TAIYIN_BAZI_WUXING_WATER = 0,
    TAIYIN_BAZI_WUXING_WOOD = 1,
    TAIYIN_BAZI_WUXING_METAL = 2,
    TAIYIN_BAZI_WUXING_EARTH = 3,
    TAIYIN_BAZI_WUXING_FIRE = 4
};

enum taiyin_bazi_stem_relation_flags {
    TAIYIN_BAZI_STEM_RELATION_COMBINATION = 1u << 0,
    TAIYIN_BAZI_STEM_RELATION_CLASH = 1u << 1,
    TAIYIN_BAZI_STEM_RELATION_RESTRAINT = 1u << 2
};

enum taiyin_bazi_branch_relation_flags {
    TAIYIN_BAZI_BRANCH_RELATION_COMBINATION = 1u << 0,
    TAIYIN_BAZI_BRANCH_RELATION_CLASH = 1u << 1,
    TAIYIN_BAZI_BRANCH_RELATION_HARM = 1u << 2,
    TAIYIN_BAZI_BRANCH_RELATION_DESTRUCTION = 1u << 3,
    TAIYIN_BAZI_BRANCH_RELATION_PUNISHMENT = 1u << 4,
    TAIYIN_BAZI_BRANCH_RELATION_SELF_PUNISHMENT = 1u << 5,
    TAIYIN_BAZI_BRANCH_RELATION_HIDDEN_COMBINATION = 1u << 6,
    TAIYIN_BAZI_BRANCH_RELATION_SEVERANCE = 1u << 7
};

enum taiyin_bazi_branch_triple_relation_flags {
    TAIYIN_BAZI_BRANCH_TRIPLE_RELATION_COMBINATION = 1u << 0,
    TAIYIN_BAZI_BRANCH_TRIPLE_RELATION_DIRECTION = 1u << 1,
    TAIYIN_BAZI_BRANCH_TRIPLE_RELATION_PUNISHMENT = 1u << 2
};

enum taiyin_bazi_shen_sha_target_kind {
    TAIYIN_BAZI_SHEN_SHA_TARGET_YEAR = 0,
    TAIYIN_BAZI_SHEN_SHA_TARGET_MONTH = 1,
    TAIYIN_BAZI_SHEN_SHA_TARGET_DAY = 2,
    TAIYIN_BAZI_SHEN_SHA_TARGET_HOUR = 3,
    TAIYIN_BAZI_SHEN_SHA_TARGET_MING_GONG = 4,
    TAIYIN_BAZI_SHEN_SHA_TARGET_SHEN_GONG = 5,
    TAIYIN_BAZI_SHEN_SHA_TARGET_TAI_YUAN = 6,
    TAIYIN_BAZI_SHEN_SHA_TARGET_TAI_XI = 7,
    TAIYIN_BAZI_SHEN_SHA_TARGET_DA_YUN = 8,
    TAIYIN_BAZI_SHEN_SHA_TARGET_FLOW_YEAR = 9,
    TAIYIN_BAZI_SHEN_SHA_TARGET_FLOW_MONTH = 10,
    TAIYIN_BAZI_SHEN_SHA_TARGET_FLOW_DAY = 11,
    TAIYIN_BAZI_SHEN_SHA_TARGET_FLOW_HOUR = 12
};

enum taiyin_bazi_shen_sha_id {
    TAIYIN_BAZI_SHEN_SHA_TIAN_YI_GUI_REN = 0,
    TAIYIN_BAZI_SHEN_SHA_YI_MA = 1,
    TAIYIN_BAZI_SHEN_SHA_KONG_WANG = 11,
    TAIYIN_BAZI_SHEN_SHA_TIAN_CHU_GUI_REN_XUN = 12,
    TAIYIN_BAZI_SHEN_SHA_XIAN_CHI_TAO_HUA = 2,
    TAIYIN_BAZI_SHEN_SHA_HONG_LUAN = 3,
    TAIYIN_BAZI_SHEN_SHA_TIAN_XI = 4,
    TAIYIN_BAZI_SHEN_SHA_YANG_REN = 5,
    TAIYIN_BAZI_SHEN_SHA_FEI_REN = 6,
    TAIYIN_BAZI_SHEN_SHA_FU_XING_GUI_REN = 7,
    TAIYIN_BAZI_SHEN_SHA_ZAI_SHA = 8,
    TAIYIN_BAZI_SHEN_SHA_JIE_SHA = 9,
    TAIYIN_BAZI_SHEN_SHA_WANG_SHEN = 10,
    TAIYIN_BAZI_SHEN_SHA_TIAN_CHU_GUI_REN = 13,
    TAIYIN_BAZI_SHEN_SHA_DE_XIU_GUI_REN = 14,
    TAIYIN_BAZI_SHEN_SHA_TIAN_YI_MEDICINE = 15,
    TAIYIN_BAZI_SHEN_SHA_XUE_REN = 16,
    TAIYIN_BAZI_SHEN_SHA_GOU_SHA = 18,
    TAIYIN_BAZI_SHEN_SHA_JIAO_SHA = 19,
    TAIYIN_BAZI_SHEN_SHA_YUAN_CHEN = 20,
    TAIYIN_BAZI_SHEN_SHA_GU_CHEN = 21,
    TAIYIN_BAZI_SHEN_SHA_GUA_SU = 22,
    TAIYIN_BAZI_SHEN_SHA_HONG_YAN_SHA = 23,
    TAIYIN_BAZI_SHEN_SHA_JIN_YU = 24,
    TAIYIN_BAZI_SHEN_SHA_JIN_SHEN = 25,
    TAIYIN_BAZI_SHEN_SHA_TONG_ZI = 31,
    TAIYIN_BAZI_SHEN_SHA_TIAN_DE_HE = 32,
    TAIYIN_BAZI_SHEN_SHA_SAN_QI_TIAN = 33,
    TAIYIN_BAZI_SHEN_SHA_SAN_QI_DI = 34,
    TAIYIN_BAZI_SHEN_SHA_SAN_QI_REN = 35,
    TAIYIN_BAZI_SHEN_SHA_YUE_DE_HE = 17,
    TAIYIN_BAZI_SHEN_SHA_TIAN_SHE_DAY = 26,
    TAIYIN_BAZI_SHEN_SHA_LIU_XIA = 27,
    TAIYIN_BAZI_SHEN_SHA_SANG_MEN = 28,
    TAIYIN_BAZI_SHEN_SHA_DIAO_KE = 29,
    TAIYIN_BAZI_SHEN_SHA_PI_MA = 30,
    TAIYIN_BAZI_SHEN_SHA_JIANG_XING = 36,
    TAIYIN_BAZI_SHEN_SHA_HUA_GAI = 37,
    TAIYIN_BAZI_SHEN_SHA_DI_ZHUAN = 50,
    TAIYIN_BAZI_SHEN_SHA_TIAN_ZHUAN = 51,
    TAIYIN_BAZI_SHEN_SHA_KUI_GANG = 38,
    TAIYIN_BAZI_SHEN_SHA_SHI_LING_DAY = 39,
    TAIYIN_BAZI_SHEN_SHA_BA_ZHUAN_DAY = 40,
    TAIYIN_BAZI_SHEN_SHA_LIU_XIU_DAY = 41,
    TAIYIN_BAZI_SHEN_SHA_JIU_CHOU_DAY = 42,
    TAIYIN_BAZI_SHEN_SHA_SI_FEI_DAY = 43,
    TAIYIN_BAZI_SHEN_SHA_SHI_E_DA_BAI = 44,
    TAIYIN_BAZI_SHEN_SHA_YIN_CHA_YANG_CUO = 46,
    TAIYIN_BAZI_SHEN_SHA_GU_LUAN_SHA = 47,
    TAIYIN_BAZI_SHEN_SHA_TIAN_LUO_DI_WANG = 45,
    TAIYIN_BAZI_SHEN_SHA_GONG_LU = 48,
    TAIYIN_BAZI_SHEN_SHA_GONG_GUI = 49,
    TAIYIN_BAZI_SHEN_SHA_TAI_JI_GUI_REN = 52,
    TAIYIN_BAZI_SHEN_SHA_WEN_CHANG_GUI_REN = 53,
    TAIYIN_BAZI_SHEN_SHA_GUO_YIN_GUI_REN = 54,
    TAIYIN_BAZI_SHEN_SHA_YUE_DE_GUI_REN = 56,
    TAIYIN_BAZI_SHEN_SHA_LU_SHEN = 57,
    TAIYIN_BAZI_SHEN_SHA_RI_GAN_XUE_TANG = 58,
    TAIYIN_BAZI_SHEN_SHA_RI_GAN_CI_GUAN = 59,
    TAIYIN_BAZI_SHEN_SHA_TIAN_DE_GUI_REN = 55,
    TAIYIN_BAZI_SHEN_SHA_ZHENG_XUE_TANG = 60,
    TAIYIN_BAZI_SHEN_SHA_ZHENG_CI_GUAN = 61,
    TAIYIN_BAZI_SHEN_SHA_GUAN_GUI_XUE_TANG = 62,
    TAIYIN_BAZI_SHEN_SHA_GUAN_GUI_CI_GUAN = 63,
    TAIYIN_BAZI_SHEN_SHA_GUAN_XING_XUE_TANG = 64,
    TAIYIN_BAZI_SHEN_SHA_XUE_TANG_HUI_GUI = 65
};

// Values inherit bazi_core's BaziInteraction ordering.
enum taiyin_bazi_relation_kind {
    TAIYIN_BAZI_RELATION_STEM_COMBINATION = 0,
    TAIYIN_BAZI_RELATION_STEM_CLASH = 1,
    TAIYIN_BAZI_RELATION_STEM_RESTRAINT = 2,
    TAIYIN_BAZI_RELATION_BRANCH_COMBINATION = 3,
    TAIYIN_BAZI_RELATION_BRANCH_CLASH = 4,
    TAIYIN_BAZI_RELATION_BRANCH_HARM = 5,
    TAIYIN_BAZI_RELATION_BRANCH_DESTRUCTION = 6,
    TAIYIN_BAZI_RELATION_BRANCH_TRIPLE_PUNISHMENT = 7,
    TAIYIN_BAZI_RELATION_BRANCH_PUNISHMENT = 8,
    TAIYIN_BAZI_RELATION_BRANCH_SELF_PUNISHMENT = 9,
    TAIYIN_BAZI_RELATION_BRANCH_TRIPLE_COMBINATION = 10,
    TAIYIN_BAZI_RELATION_BRANCH_TRIPLE_DIRECTION = 11,
    TAIYIN_BAZI_RELATION_BRANCH_HALF_COMBINATION = 12,
    TAIYIN_BAZI_RELATION_BRANCH_ARCHING_COMBINATION = 13,
    TAIYIN_BAZI_RELATION_BRANCH_HIDDEN_COMBINATION = 14,
    TAIYIN_BAZI_RELATION_BRANCH_SEVERANCE = 15
};

enum taiyin_bazi_relation_pillar_flags {
    TAIYIN_BAZI_RELATION_PILLAR_YEAR = 1u << 0,
    TAIYIN_BAZI_RELATION_PILLAR_MONTH = 1u << 1,
    TAIYIN_BAZI_RELATION_PILLAR_DAY = 1u << 2,
    TAIYIN_BAZI_RELATION_PILLAR_HOUR = 1u << 3,
    TAIYIN_BAZI_RELATION_PILLAR_MING_GONG = 1u << 4,
    TAIYIN_BAZI_RELATION_PILLAR_SHEN_GONG = 1u << 5,
    TAIYIN_BAZI_RELATION_PILLAR_TAI_YUAN = 1u << 6,
    TAIYIN_BAZI_RELATION_PILLAR_TAI_XI = 1u << 7,
    TAIYIN_BAZI_RELATION_PILLAR_PRIMARY = 0x0fu,
    TAIYIN_BAZI_RELATION_PILLAR_EXTRA = 0xf0u,
    TAIYIN_BAZI_RELATION_PILLAR_ALL = 0xffu
};

#define TAIYIN_BAZI_RELATION_KIND_MASK_ALL 0x0000ffffu

typedef struct taiyin_bazi_context_config {
    uint32_t struct_size;
    int32_t earth_palace_mode;
    int32_t qiyun_direction_mode;
    int32_t qiyun_time_model;
    int32_t dayun_boundary_model;
} taiyin_bazi_context_config;

typedef struct taiyin_bazi_chart {
    uint32_t struct_size;
    taiyin_ganzhi year_pillar;
    taiyin_ganzhi month_pillar;
    taiyin_ganzhi day_pillar;
    taiyin_ganzhi hour_pillar;
    taiyin_ganzhi ming_gong;
    taiyin_ganzhi shen_gong;
    taiyin_ganzhi tai_yuan;
    taiyin_ganzhi tai_xi;
    uint8_t hidden_stem_count[4];
    uint8_t hidden_stems[4][TAIYIN_BAZI_HIDDEN_STEM_CAPACITY];
    uint8_t visible_ten_gods[4];
    uint8_t hidden_ten_gods[4][TAIYIN_BAZI_HIDDEN_STEM_CAPACITY];
    uint8_t life_stages[4];
    uint8_t nayin_ids[4];
} taiyin_bazi_chart;

typedef struct taiyin_bazi_relation {
    uint32_t struct_size;
    int32_t kind;
    uint32_t pillar_mask;
    uint8_t combined_element_id;
    uint8_t reserved[3];
} taiyin_bazi_relation;

typedef struct taiyin_bazi_qiyun_result {
    uint32_t struct_size;
    int32_t direction;
    int32_t time_model;
    uint8_t reference_jie_index;
    uint8_t reserved[3];
    double jie_interval_days;
    double start_age_years;
    int32_t offset_years;
    int32_t offset_months;
    int32_t offset_days;
    int32_t offset_hours;
    int32_t offset_minutes;
    double offset_seconds;
    taiyin_split_julian_date reference_jie_jd_ut;
    taiyin_split_julian_date start_jd_ut;
    taiyin_calendar_datetime start_civil_time;
} taiyin_bazi_qiyun_result;

typedef struct taiyin_bazi_dayun {
    uint32_t struct_size;
    uint32_t index;
    taiyin_ganzhi ganzhi;
    uint8_t reserved[3];
    int32_t start_virtual_age;
    int32_t end_virtual_age;
    taiyin_split_julian_date start_jd_ut;
    taiyin_split_julian_date end_jd_ut;
    taiyin_calendar_datetime start_civil_time;
    taiyin_calendar_datetime end_civil_time;
} taiyin_bazi_dayun;

typedef struct taiyin_bazi_xiaoyun {
    uint32_t struct_size;
    uint32_t age;
    taiyin_ganzhi ganzhi;
    uint8_t reserved[3];
} taiyin_bazi_xiaoyun;

typedef struct taiyin_bazi_renyuan_siling_segment {
    uint32_t struct_size;
    uint8_t stem_id;
    uint8_t origin_kind;
    uint8_t segment_index;
    uint8_t reserved;
    double start_day;
    double end_day;
} taiyin_bazi_renyuan_siling_segment;

typedef struct taiyin_bazi_renyuan_siling_result {
    uint32_t struct_size;
    int32_t table_model;
    int32_t time_model;
    uint8_t month_branch_id;
    uint8_t stem_id;
    uint8_t origin_kind;
    uint8_t segment_index;
    uint8_t previous_jie_index;
    uint8_t reserved[3];
    double days_since_jie;
    double segment_start_day;
    double segment_end_day;
    taiyin_split_julian_date previous_jie_jd_ut;
} taiyin_bazi_renyuan_siling_result;

TAIYIN_C_BAZI_API void TAIYIN_C_CALL taiyin_bazi_context_config_init(
    taiyin_bazi_context_config* value
);
TAIYIN_C_BAZI_API void TAIYIN_C_CALL taiyin_bazi_chart_init(taiyin_bazi_chart* value);
TAIYIN_C_BAZI_API void TAIYIN_C_CALL taiyin_bazi_relation_init(taiyin_bazi_relation* value);
TAIYIN_C_BAZI_API void TAIYIN_C_CALL taiyin_bazi_qiyun_result_init(
    taiyin_bazi_qiyun_result* value
);
TAIYIN_C_BAZI_API void TAIYIN_C_CALL taiyin_bazi_dayun_init(taiyin_bazi_dayun* value);
TAIYIN_C_BAZI_API void TAIYIN_C_CALL taiyin_bazi_xiaoyun_init(
    taiyin_bazi_xiaoyun* value
);
TAIYIN_C_BAZI_API void TAIYIN_C_CALL taiyin_bazi_renyuan_siling_segment_init(
    taiyin_bazi_renyuan_siling_segment* value
);
TAIYIN_C_BAZI_API void TAIYIN_C_CALL taiyin_bazi_renyuan_siling_result_init(
    taiyin_bazi_renyuan_siling_result* value
);

TAIYIN_C_BAZI_API taiyin_status TAIYIN_C_CALL taiyin_bazi_context_create(
    const taiyin_bazi_context_config* bazi_config,
    taiyin_bazi_context** out_context
);
TAIYIN_C_BAZI_API void TAIYIN_C_CALL taiyin_bazi_context_destroy(
    taiyin_bazi_context* context
);

TAIYIN_C_BAZI_API taiyin_status TAIYIN_C_CALL taiyin_bazi_get_kong_wang(
    taiyin_ganzhi value,
    uint8_t out_branches[2]
);
TAIYIN_C_BAZI_API taiyin_status TAIYIN_C_CALL taiyin_bazi_get_ten_god(
    uint8_t day_stem_id,
    uint8_t target_stem_id,
    uint8_t* out_ten_god_id
);
TAIYIN_C_BAZI_API taiyin_status TAIYIN_C_CALL taiyin_bazi_get_hidden_stems(
    uint8_t branch_id,
    uint8_t out_stems[TAIYIN_BAZI_HIDDEN_STEM_CAPACITY],
    uint8_t* out_count
);
TAIYIN_C_BAZI_API taiyin_status TAIYIN_C_CALL taiyin_bazi_calc_stem_relation(
    uint8_t stem_a,
    uint8_t stem_b,
    uint32_t* out_flags,
    uint8_t* out_combined_element_id
);
TAIYIN_C_BAZI_API taiyin_status TAIYIN_C_CALL taiyin_bazi_calc_branch_relation(
    uint8_t branch_a,
    uint8_t branch_b,
    uint32_t* out_flags,
    uint8_t* out_combined_element_id
);
TAIYIN_C_BAZI_API taiyin_status TAIYIN_C_CALL taiyin_bazi_calc_branch_triple_relation(
    uint8_t branch_a,
    uint8_t branch_b,
    uint8_t branch_c,
    uint32_t* out_flags,
    uint8_t* out_combined_element_id
);
TAIYIN_C_BAZI_API taiyin_status TAIYIN_C_CALL taiyin_bazi_get_life_stage(
    uint8_t stem_id,
    uint8_t branch_id,
    int32_t earth_palace_mode,
    uint8_t* out_life_stage_id
);

TAIYIN_C_BAZI_API taiyin_status TAIYIN_C_CALL taiyin_bazi_calc_chart(
    const taiyin_bazi_context* context,
    const taiyin_ganzhi_four_pillars* pillars,
    taiyin_bazi_chart* out
);

TAIYIN_C_BAZI_API taiyin_status TAIYIN_C_CALL taiyin_bazi_calc_liunian(
    int32_t effective_year,
    taiyin_ganzhi* out
);
// month_branch is a Ganzhi branch ID: 2=Yin, ..., 0=Zi, 1=Chou.
TAIYIN_C_BAZI_API taiyin_status TAIYIN_C_CALL taiyin_bazi_calc_liuyue(
    taiyin_ganzhi year_pillar,
    uint8_t month_branch,
    taiyin_ganzhi* out
);
TAIYIN_C_BAZI_API taiyin_status TAIYIN_C_CALL taiyin_bazi_calc_liuri(
    const taiyin_calendar_datetime* civil_date,
    taiyin_ganzhi* out
);
// hour_index follows the branch sequence: 0=Zi, ..., 11=Hai.
TAIYIN_C_BAZI_API taiyin_status TAIYIN_C_CALL taiyin_bazi_calc_liushi(
    taiyin_ganzhi day_pillar,
    uint8_t hour_index,
    taiyin_ganzhi* out
);
TAIYIN_C_BAZI_API taiyin_status TAIYIN_C_CALL taiyin_bazi_calc_xiaoyun(
    const taiyin_bazi_chart* chart,
    int32_t direction,
    int32_t age,
    taiyin_ganzhi* out
);
TAIYIN_C_BAZI_API taiyin_status TAIYIN_C_CALL taiyin_bazi_fill_xiaoyun(
    const taiyin_bazi_chart* chart,
    int32_t direction,
    int32_t start_age,
    size_t requested_count,
    taiyin_bazi_xiaoyun* out,
    size_t capacity,
    size_t* out_count
);

// Collects the merged interaction graph. relation_mask uses
// (1u << taiyin_bazi_relation_kind); output may be NULL only when capacity is
// zero, which queries the required count.
TAIYIN_C_BAZI_API taiyin_status TAIYIN_C_CALL taiyin_bazi_collect_chart_relations(
    const taiyin_bazi_chart* chart,
    uint32_t pillar_mask,
    uint32_t relation_mask,
    taiyin_bazi_relation* out,
    size_t capacity,
    size_t* out_count
);

TAIYIN_C_BAZI_API taiyin_status TAIYIN_C_CALL taiyin_bazi_collect_target_shen_sha(
    const taiyin_bazi_chart* chart,
    taiyin_ganzhi target_ganzhi,
    int32_t target_kind,
    uint64_t* out_words,
    size_t word_capacity,
    size_t* out_word_count
);

// This variant enables the gender-dependent legacy Shen Sha rules. The
// gender-neutral entry above remains available for callers that do not have a
// gender profile.
TAIYIN_C_BAZI_API taiyin_status TAIYIN_C_CALL
taiyin_bazi_collect_target_shen_sha_with_gender(
    const taiyin_bazi_chart* chart,
    taiyin_ganzhi target_ganzhi,
    int32_t target_kind,
    int32_t gender,
    uint64_t* out_words,
    size_t word_capacity,
    size_t* out_word_count
);

TAIYIN_C_BAZI_API taiyin_status TAIYIN_C_CALL taiyin_bazi_calc_qiyun(
    const taiyin_bazi_context* context,
    const taiyin_chinese_calendar_context* calendar_context,
    const taiyin_split_julian_date* birth_jd_ut,
    const taiyin_calendar_datetime* birth_civil_time,
    const taiyin_bazi_chart* chart,
    int32_t gender,
    taiyin_bazi_qiyun_result* out,
    taiyin_ephemeris_diagnostic* diagnostic
);

// requested_count is the number of one-based da-yun entries to generate.
// Time boundaries form [start_jd_ut, end_jd_ut); virtual ages are an
// inclusive traditional display range. out may be NULL only when capacity is
// zero, which queries that count.
TAIYIN_C_BAZI_API taiyin_status TAIYIN_C_CALL taiyin_bazi_fill_dayun(
    const taiyin_bazi_context* context,
    const taiyin_calendar_datetime* birth_civil_time,
    const taiyin_bazi_chart* chart,
    const taiyin_bazi_qiyun_result* qiyun,
    size_t requested_count,
    taiyin_bazi_dayun* out,
    size_t capacity,
    size_t* out_count
);

TAIYIN_C_BAZI_API taiyin_status TAIYIN_C_CALL taiyin_bazi_get_renyuan_siling_segments(
    uint8_t month_branch_id,
    int32_t table_model,
    taiyin_bazi_renyuan_siling_segment* out,
    size_t capacity,
    size_t* out_count
);

TAIYIN_C_BAZI_API taiyin_status TAIYIN_C_CALL taiyin_bazi_calc_renyuan_siling(
    const taiyin_chinese_calendar_context* calendar_context,
    const taiyin_split_julian_date* instant_jd_ut,
    const taiyin_bazi_chart* chart,
    int32_t table_model,
    int32_t time_model,
    taiyin_bazi_renyuan_siling_result* out,
    taiyin_ephemeris_diagnostic* diagnostic
);

#ifdef __cplusplus
}
#endif

#endif
