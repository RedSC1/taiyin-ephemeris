#ifndef TAIYIN_C_ZIWEI_H
#define TAIYIN_C_ZIWEI_H

#include "taiyin/c/chinese_calendar.h"

#ifdef __cplusplus
extern "C" {
#endif

#define TAIYIN_ZIWEI_ANCHOR_COUNT 31u
#define TAIYIN_ZIWEI_INVALID_STAR_ID 0xffffu
#define TAIYIN_ZIWEI_INVALID_POSITION 0xffu

typedef struct taiyin_ziwei_data_catalog taiyin_ziwei_data_catalog;
typedef struct taiyin_ziwei_context taiyin_ziwei_context;
typedef struct taiyin_ziwei_chart taiyin_ziwei_chart;

enum taiyin_ziwei_option_component {
    TAIYIN_ZIWEI_OPTION_PLACEMENT = 0,
    TAIYIN_ZIWEI_OPTION_BRIGHTNESS = 1,
    TAIYIN_ZIWEI_OPTION_SIHUA = 2,
    TAIYIN_ZIWEI_OPTION_MASTERS = 3,
    /* A whole-table choice for Changsheng through Yang. key must be NULL. */
    TAIYIN_ZIWEI_OPTION_LONGEVITY = 4
};

enum taiyin_ziwei_gender {
    TAIYIN_ZIWEI_GENDER_MALE = 0,
    TAIYIN_ZIWEI_GENDER_FEMALE = 1
};

enum taiyin_ziwei_pillar_boundary {
    TAIYIN_ZIWEI_PILLAR_SOLAR_TERM = 0,
    TAIYIN_ZIWEI_PILLAR_LUNAR = 1
};

enum taiyin_ziwei_chart_mode {
    TAIYIN_ZIWEI_CHART_TIAN_PAN = 0,
    TAIYIN_ZIWEI_CHART_DI_PAN = 1,
    TAIYIN_ZIWEI_CHART_REN_PAN = 2
};

enum taiyin_ziwei_leap_month_strategy {
    TAIYIN_ZIWEI_LEAP_AS_PREVIOUS = 0,
    TAIYIN_ZIWEI_LEAP_AS_NEXT = 1,
    TAIYIN_ZIWEI_LEAP_SPLIT_AFTER_FIFTEENTH = 2
};

enum taiyin_ziwei_flow_month_palace_strategy {
    TAIYIN_ZIWEI_FLOW_MONTH_PALACE_PHYSICAL_SEQUENCE = 0,
    TAIYIN_ZIWEI_FLOW_MONTH_PALACE_EFFECTIVE_MONTH = 1
};

enum taiyin_ziwei_flow_level {
    TAIYIN_ZIWEI_FLOW_DECADE = 0,
    TAIYIN_ZIWEI_FLOW_YEAR = 1,
    TAIYIN_ZIWEI_FLOW_MONTH = 2,
    TAIYIN_ZIWEI_FLOW_DAY = 3,
    TAIYIN_ZIWEI_FLOW_HOUR = 4
};

enum taiyin_ziwei_childhood_strategy {
    TAIYIN_ZIWEI_CHILDHOOD_SKIP = 0,
    TAIYIN_ZIWEI_CHILDHOOD_SEQUENTIAL = 1
};

enum taiyin_ziwei_star_category {
    TAIYIN_ZIWEI_STAR_MAJOR = 0,
    TAIYIN_ZIWEI_STAR_LUCKY = 1,
    TAIYIN_ZIWEI_STAR_MINOR = 2,
    TAIYIN_ZIWEI_STAR_MALEFIC = 3,
    TAIYIN_ZIWEI_STAR_CYCLE = 4,
    TAIYIN_ZIWEI_STAR_OTHER = 5
};

enum taiyin_ziwei_brightness {
    TAIYIN_ZIWEI_BRIGHTNESS_NONE = -1,
    TAIYIN_ZIWEI_BRIGHTNESS_XIAN = 0,
    TAIYIN_ZIWEI_BRIGHTNESS_BU = 1,
    TAIYIN_ZIWEI_BRIGHTNESS_PING = 2,
    TAIYIN_ZIWEI_BRIGHTNESS_LI = 3,
    TAIYIN_ZIWEI_BRIGHTNESS_DE = 4,
    TAIYIN_ZIWEI_BRIGHTNESS_WANG = 5,
    TAIYIN_ZIWEI_BRIGHTNESS_MIAO = 6
};

/* One transformation overlay is attached to each natal StarId. */
enum taiyin_ziwei_star_transform_mark {
    TAIYIN_ZIWEI_BIRTH_YEAR_LU = 0,
    TAIYIN_ZIWEI_BIRTH_YEAR_QUAN = 1,
    TAIYIN_ZIWEI_BIRTH_YEAR_KE = 2,
    TAIYIN_ZIWEI_BIRTH_YEAR_JI = 3,
    TAIYIN_ZIWEI_CENTRIFUGAL_LU = 4,
    TAIYIN_ZIWEI_CENTRIFUGAL_QUAN = 5,
    TAIYIN_ZIWEI_CENTRIFUGAL_KE = 6,
    TAIYIN_ZIWEI_CENTRIFUGAL_JI = 7,
    TAIYIN_ZIWEI_CENTRIPETAL_LU = 8,
    TAIYIN_ZIWEI_CENTRIPETAL_QUAN = 9,
    TAIYIN_ZIWEI_CENTRIPETAL_KE = 10,
    TAIYIN_ZIWEI_CENTRIPETAL_JI = 11
};

typedef struct taiyin_ziwei_option_override {
    uint32_t struct_size;
    int32_t component;
    /* NULL or empty means the component-wide default. Masters ignores key. */
    const char* key;
    const char* option;
} taiyin_ziwei_option_override;

typedef struct taiyin_ziwei_birth_options {
    uint32_t struct_size;
    int32_t rat_hour_mode;
    int32_t leap_month_strategy;
    int32_t chart_mode;
    int32_t wu_hu_dun_year_boundary;
    int32_t sihua_year_boundary;
    int32_t body_master_year_boundary;
} taiyin_ziwei_birth_options;

typedef struct taiyin_ziwei_flow_options {
    uint32_t struct_size;
    int32_t boundary;
    int32_t rat_hour_mode;
    int32_t childhood_strategy;
    int32_t flow_month_palace_strategy;
} taiyin_ziwei_flow_options;

typedef struct taiyin_ziwei_transform_set {
    uint32_t struct_size;
    uint16_t lu;
    uint16_t quan;
    uint16_t ke;
    uint16_t ji;
} taiyin_ziwei_transform_set;

typedef struct taiyin_ziwei_flow_summary {
    uint32_t struct_size;
    int32_t effective_birth_year;
    int32_t effective_target_year;
    uint8_t target_month;
    uint8_t target_month_sequence;
    uint8_t target_day;
    uint8_t target_hour_index;
    uint8_t target_rat_hour_segment;
    uint8_t target_month_is_leap;
    uint16_t decade_index;
    int32_t decade_start_age;
    int32_t decade_end_age;
    int32_t small_limit_virtual_age;
    uint8_t small_limit_stem;
    uint8_t small_limit_branch;
    // 0=Zi through 11=Hai; independently resolved calendar month building.
    uint8_t target_month_building_branch;
    uint8_t target_effective_month;
    uint8_t target_month_name;
    uint8_t target_palace_month_index;
    uint8_t reserved[2];
    int32_t target_lunar_year;
} taiyin_ziwei_flow_summary;

/* Physical-branch filters for direct birth-time reverse lookup.  Set a field
 * to -1 to omit it.  At least one field must be specified. */
typedef struct taiyin_ziwei_reverse_query {
    uint32_t struct_size;
    int32_t lucun_branch;
    int32_t hongluan_branch;
    int32_t zuofu_branch;
    int32_t youbi_branch;
    int32_t wenchang_branch;
    int32_t wenqu_branch;
    int32_t santai_branch;
    int32_t bazuo_branch;
    int32_t ziwei_branch;
} taiyin_ziwei_reverse_query;

typedef struct taiyin_ziwei_reverse_request {
    uint32_t struct_size;
    taiyin_split_julian_date start_instant_utc;
    taiyin_split_julian_date end_instant_utc;
    taiyin_calendar_datetime start_virtual_time;
    int32_t gender;
    taiyin_ziwei_birth_options birth_options;
    taiyin_ziwei_reverse_query query;
} taiyin_ziwei_reverse_request;

typedef struct taiyin_ziwei_reverse_candidate {
    uint32_t struct_size;
    taiyin_split_julian_date instant_utc;
    taiyin_calendar_datetime virtual_time;
    int32_t lunar_year;
    uint8_t lunar_month;
    uint8_t lunar_day;
    uint8_t lunar_is_leap;
    uint8_t hour_branch;
    uint8_t rat_hour_segment;
    uint8_t reserved[3];
} taiyin_ziwei_reverse_candidate;

TAIYIN_C_ZIWEI_API void TAIYIN_C_CALL taiyin_ziwei_option_override_init(
    taiyin_ziwei_option_override* value);
TAIYIN_C_ZIWEI_API void TAIYIN_C_CALL taiyin_ziwei_birth_options_init(
    taiyin_ziwei_birth_options* value);
TAIYIN_C_ZIWEI_API void TAIYIN_C_CALL taiyin_ziwei_flow_options_init(
    taiyin_ziwei_flow_options* value);
TAIYIN_C_ZIWEI_API void TAIYIN_C_CALL taiyin_ziwei_transform_set_init(
    taiyin_ziwei_transform_set* value);
TAIYIN_C_ZIWEI_API void TAIYIN_C_CALL taiyin_ziwei_flow_summary_init(
    taiyin_ziwei_flow_summary* value);
TAIYIN_C_ZIWEI_API void TAIYIN_C_CALL taiyin_ziwei_reverse_query_init(
    taiyin_ziwei_reverse_query* value);
TAIYIN_C_ZIWEI_API void TAIYIN_C_CALL taiyin_ziwei_reverse_request_init(
    taiyin_ziwei_reverse_request* value);
TAIYIN_C_ZIWEI_API void TAIYIN_C_CALL taiyin_ziwei_reverse_candidate_init(
    taiyin_ziwei_reverse_candidate* value);

TAIYIN_C_ZIWEI_API taiyin_call_result TAIYIN_C_CALL taiyin_ziwei_data_catalog_create(
    const char* profile_path,
    taiyin_ziwei_data_catalog** out_catalog);
TAIYIN_C_ZIWEI_API void TAIYIN_C_CALL taiyin_ziwei_data_catalog_destroy(
    taiyin_ziwei_data_catalog* catalog);
TAIYIN_C_ZIWEI_API taiyin_call_result TAIYIN_C_CALL taiyin_ziwei_data_catalog_reload(
    taiyin_ziwei_data_catalog* catalog);
TAIYIN_C_ZIWEI_API uint64_t TAIYIN_C_CALL taiyin_ziwei_data_catalog_generation(
    const taiyin_ziwei_data_catalog* catalog);

TAIYIN_C_ZIWEI_API taiyin_call_result TAIYIN_C_CALL taiyin_ziwei_context_create(
    const taiyin_ziwei_data_catalog* catalog,
    const taiyin_ziwei_option_override* overrides,
    size_t override_count,
    taiyin_ziwei_context** out_context);
TAIYIN_C_ZIWEI_API void TAIYIN_C_CALL taiyin_ziwei_context_destroy(
    taiyin_ziwei_context* context);
TAIYIN_C_ZIWEI_API uint64_t TAIYIN_C_CALL taiyin_ziwei_context_generation(
    const taiyin_ziwei_context* context);

TAIYIN_C_ZIWEI_API size_t TAIYIN_C_CALL taiyin_ziwei_star_count(
    const taiyin_ziwei_context* context);
TAIYIN_C_ZIWEI_API taiyin_call_result TAIYIN_C_CALL taiyin_ziwei_find_star(
    const taiyin_ziwei_context* context,
    const char* key,
    uint16_t* out_star_id);
/* out_required_size includes the trailing NUL. buffer may be NULL for query. */
TAIYIN_C_ZIWEI_API taiyin_call_result TAIYIN_C_CALL taiyin_ziwei_get_star_metadata(
    const taiyin_ziwei_context* context,
    uint16_t star_id,
    int32_t* out_category,
    char* buffer,
    size_t capacity,
    size_t* out_required_size);

TAIYIN_C_ZIWEI_API taiyin_call_result TAIYIN_C_CALL taiyin_ziwei_chart_create(
    const taiyin_ziwei_context* context,
    const taiyin_chinese_calendar_context* calendar_context,
    const taiyin_split_julian_date* instant_utc,
    const taiyin_calendar_datetime* virtual_time,
    int32_t gender,
    const taiyin_ziwei_birth_options* options,
    taiyin_ziwei_chart** out_chart,
    taiyin_ephemeris_diagnostic* diagnostic);
TAIYIN_C_ZIWEI_API void TAIYIN_C_CALL taiyin_ziwei_chart_destroy(
    taiyin_ziwei_chart* chart);

/* Enumerates matching logical birth-time slots. out_candidates may be NULL
 * for a count query; capacity is measured in candidate records. */
TAIYIN_C_ZIWEI_API taiyin_call_result TAIYIN_C_CALL
taiyin_ziwei_reverse_lookup_tier1(
    const taiyin_ziwei_context* context,
    const taiyin_chinese_calendar_context* calendar_context,
    const taiyin_ziwei_reverse_request* request,
    taiyin_ziwei_reverse_candidate* out_candidates,
    size_t capacity,
    size_t* out_count,
    taiyin_ephemeris_diagnostic* diagnostic);

TAIYIN_C_ZIWEI_API taiyin_call_result TAIYIN_C_CALL taiyin_ziwei_chart_get_anchors(
    const taiyin_ziwei_chart* chart,
    uint8_t out_anchors[TAIYIN_ZIWEI_ANCHOR_COUNT]);
TAIYIN_C_ZIWEI_API taiyin_call_result TAIYIN_C_CALL taiyin_ziwei_chart_get_summary(
    const taiyin_ziwei_chart* chart,
    uint8_t* out_gender,
    uint8_t* out_bureau,
    uint8_t* out_body_palace,
    uint16_t* out_life_master,
    uint16_t* out_body_master,
    taiyin_ziwei_transform_set* out_transforms);
TAIYIN_C_ZIWEI_API taiyin_call_result TAIYIN_C_CALL taiyin_ziwei_chart_get_palace_branch(
    const taiyin_ziwei_chart* chart,
    uint8_t palace_id,
    uint8_t* out_branch);
TAIYIN_C_ZIWEI_API taiyin_call_result TAIYIN_C_CALL taiyin_ziwei_chart_get_palace_stem(
    const taiyin_ziwei_chart* chart,
    uint8_t branch,
    uint8_t* out_stem);
TAIYIN_C_ZIWEI_API taiyin_call_result TAIYIN_C_CALL taiyin_ziwei_chart_get_star_position(
    const taiyin_ziwei_chart* chart,
    uint16_t star_id,
    uint8_t* out_branch);
/* Resolves a natal star directly to its twelve-palace role.  A flow-only or
 * absent star returns TAIYIN_ZIWEI_INVALID_POSITION with success. */
TAIYIN_C_ZIWEI_API taiyin_call_result TAIYIN_C_CALL taiyin_ziwei_chart_get_star_palace(
    const taiyin_ziwei_chart* chart,
    uint16_t star_id,
    uint8_t* out_palace_id);
TAIYIN_C_ZIWEI_API taiyin_call_result TAIYIN_C_CALL taiyin_ziwei_chart_get_brightness(
    const taiyin_ziwei_context* context,
    const taiyin_ziwei_chart* chart,
    uint16_t star_id,
    int32_t* out_brightness);
TAIYIN_C_ZIWEI_API taiyin_call_result TAIYIN_C_CALL
taiyin_ziwei_chart_has_star_transform_mark(
    const taiyin_ziwei_chart* chart,
    int32_t mark,
    uint16_t star_id,
    uint8_t* out_has_mark);
/* Bits 0..3: birth-year Lu/Quan/Ke/Ji; 4..7: centrifugal/self;
 * 8..11: centripetal. */
TAIYIN_C_ZIWEI_API taiyin_call_result TAIYIN_C_CALL
taiyin_ziwei_chart_get_star_transformation_mask(
    const taiyin_ziwei_chart* chart,
    uint16_t star_id,
    uint16_t* out_mask);
TAIYIN_C_ZIWEI_API taiyin_call_result TAIYIN_C_CALL taiyin_ziwei_chart_get_palace_stars(
    const taiyin_ziwei_chart* chart,
    uint8_t branch,
    uint16_t* out_star_ids,
    size_t capacity,
    size_t* out_count);

/* Replaces the chart's contiguous flow stack through deepest_level. */
TAIYIN_C_ZIWEI_API taiyin_call_result TAIYIN_C_CALL taiyin_ziwei_chart_set_flow(
    const taiyin_ziwei_context* context,
    const taiyin_chinese_calendar_context* calendar_context,
    const taiyin_split_julian_date* target_instant_utc,
    const taiyin_calendar_datetime* target_virtual_time,
    const taiyin_ziwei_flow_options* options,
    int32_t deepest_level,
    taiyin_ziwei_chart* chart,
    taiyin_ziwei_flow_summary* out_summary,
    taiyin_ephemeris_diagnostic* diagnostic);
TAIYIN_C_ZIWEI_API taiyin_call_result TAIYIN_C_CALL taiyin_ziwei_chart_truncate_flow(
    taiyin_ziwei_chart* chart,
    int32_t first_removed_level);
TAIYIN_C_ZIWEI_API size_t TAIYIN_C_CALL taiyin_ziwei_chart_flow_layer_count(
    const taiyin_ziwei_chart* chart);
TAIYIN_C_ZIWEI_API taiyin_call_result TAIYIN_C_CALL
taiyin_ziwei_chart_get_flow_star_position(
    const taiyin_ziwei_chart* chart,
    int32_t level,
    uint16_t star_id,
    uint8_t* out_branch);
TAIYIN_C_ZIWEI_API taiyin_call_result TAIYIN_C_CALL
taiyin_ziwei_chart_get_flow_layer_summary(
    const taiyin_ziwei_chart* chart,
    int32_t level,
    uint8_t* out_life_palace,
    uint8_t* out_coordinate_stem,
    uint8_t* out_coordinate_branch);
TAIYIN_C_ZIWEI_API taiyin_call_result TAIYIN_C_CALL
taiyin_ziwei_chart_get_flow_palace_stars(
    const taiyin_ziwei_chart* chart,
    int32_t level,
    uint8_t branch,
    uint16_t* out_star_ids,
    size_t capacity,
    size_t* out_count);
TAIYIN_C_ZIWEI_API taiyin_call_result TAIYIN_C_CALL
taiyin_ziwei_chart_get_flow_transforms(
    const taiyin_ziwei_chart* chart,
    int32_t level,
    taiyin_ziwei_transform_set* out_transforms);

TAIYIN_C_ZIWEI_API taiyin_call_result TAIYIN_C_CALL
taiyin_ziwei_step_flow_hour_target(
    const taiyin_split_julian_date* current_instant_utc,
    const taiyin_calendar_datetime* current_virtual_time,
    int32_t rat_hour_mode,
    int32_t direction,
    taiyin_split_julian_date* out_instant_utc,
    taiyin_calendar_datetime* out_virtual_time,
    uint8_t* out_rat_hour_segment);
TAIYIN_C_ZIWEI_API taiyin_call_result TAIYIN_C_CALL
taiyin_ziwei_step_flow_day_target(
    const taiyin_split_julian_date* current_instant_utc,
    const taiyin_calendar_datetime* current_virtual_time,
    int32_t direction,
    taiyin_split_julian_date* out_instant_utc,
    taiyin_calendar_datetime* out_virtual_time);

#ifdef __cplusplus
}
#endif

#endif
