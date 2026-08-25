#ifndef TAIYIN_C_CHINESE_CALENDAR_H
#define TAIYIN_C_CHINESE_CALENDAR_H

#include "taiyin/c/context.h"

#ifdef __cplusplus
extern "C" {
#endif

#define TAIYIN_C_CHINESE_CALENDAR_TERM_COUNT 25u
#define TAIYIN_C_CHINESE_CALENDAR_NEW_MOON_COUNT 15u
#define TAIYIN_C_CHINESE_CALENDAR_MONTH_COUNT 14u

typedef struct taiyin_chinese_calendar_context taiyin_chinese_calendar_context;

enum taiyin_chinese_calendar_mode {
    TAIYIN_C_CHINESE_CALENDAR_CHINA_STANDARD_HISTORICAL = 0,
    TAIYIN_C_CHINESE_CALENDAR_LOCAL_ASTRONOMICAL = 1,
    TAIYIN_C_CHINESE_CALENDAR_CHINA_STANDARD_ASTRONOMICAL = 2
};

enum taiyin_chinese_calendar_day_boundary_mode {
    TAIYIN_C_CHINESE_CALENDAR_FIXED_UTC_OFFSET = 0,
    TAIYIN_C_CHINESE_CALENDAR_MEAN_SOLAR_MERIDIAN = 1
};

enum taiyin_ganzhi_pillar_historical_mode {
    /* Four-pillars solar-term boundaries follow the calendar arrangement
     * mode. This is the default. */
    TAIYIN_C_GANZHI_PILLAR_HISTORICAL_FOLLOW_CALENDAR = 0,
    /* Always use modern-ephemeris solar-term instants for pillar
     * boundaries. */
    TAIYIN_C_GANZHI_PILLAR_HISTORICAL_OFF = 1,
    /* Assign pillar solar-term boundary days from the historical profile
     * and normalize each boundary to the assigned civil day's 00:00
     * (UTC+08, the profile's own convention). */
    TAIYIN_C_GANZHI_PILLAR_HISTORICAL_ON = 2
};

enum taiyin_chinese_calendar_month_name {
    TAIYIN_C_CHINESE_MONTH_NAME_NORMAL = 0,
    TAIYIN_C_CHINESE_MONTH_NAME_THIRTEEN = 1,
    TAIYIN_C_CHINESE_MONTH_NAME_LATER_NINE = 2,
    TAIYIN_C_CHINESE_MONTH_NAME_ALT_TWELVE = 3,
    TAIYIN_C_CHINESE_MONTH_NAME_ALT_ONE = 4,
    TAIYIN_C_CHINESE_MONTH_NAME_LATER_SAME_NAME = 5
};

typedef struct taiyin_chinese_calendar_config {
    /*
     * The versioned C ABI layout is intentionally independent of the native
     * C++ structs. Use the init functions and field-wise API conversions;
     * do not memcpy or reinterpret_cast this as a C++ calendar struct.
     */
    uint32_t struct_size;
    int32_t mode;
    int32_t day_boundary_mode;
    int32_t utc_offset_minutes;
    int32_t pillar_historical_mode;
    double calendar_meridian_deg;
} taiyin_chinese_calendar_config;

typedef struct taiyin_solar_date {
    uint32_t struct_size;
    int32_t year;
    uint8_t month;
    uint8_t day;
    uint8_t reserved[2];
} taiyin_solar_date;

typedef struct taiyin_lunar_date {
    uint32_t struct_size;
    int32_t year;
    uint8_t month;
    uint8_t day;
    uint8_t is_leap;
    uint8_t month_days;
    uint8_t month_name;
    uint8_t reserved[3];
    /* Actual calendar/ganzhi year; may differ from year at reforms. */
    int32_t historical_year;
} taiyin_lunar_date;

typedef struct taiyin_chinese_solar_term_event {
    uint32_t struct_size;
    uint8_t index_from_winter_solstice;
    uint8_t reserved[3];
    double target_longitude_rad;
    taiyin_split_julian_date jd_ut;
    int64_t civil_day_number;
} taiyin_chinese_solar_term_event;

typedef struct taiyin_chinese_new_moon_event {
    uint32_t struct_size;
    uint32_t reserved;
    taiyin_split_julian_date jd_ut;
    int64_t civil_day_number;
} taiyin_chinese_new_moon_event;

typedef struct taiyin_chinese_calendar_month {
    uint32_t struct_size;
    int32_t lunar_year;
    uint8_t month;
    uint8_t is_leap;
    uint8_t day_count;
    uint8_t month_name;
    // 0=Zi through 11=Hai.  Kept in the former alignment padding so existing
    // field offsets and the C ABI layout remain unchanged.
    uint8_t month_building_branch;
    uint8_t reserved[3];
    int64_t first_civil_day_number;
    taiyin_split_julian_date astronomical_new_moon_jd_ut;
    /* Actual calendar/ganzhi year; may differ from lunar_year at reforms. */
    int32_t historical_year;
    uint8_t reserved_tail[4];
} taiyin_chinese_calendar_month;

typedef struct taiyin_chinese_calendar_year {
    uint32_t struct_size;
    taiyin_chinese_solar_term_event
        solar_terms[TAIYIN_C_CHINESE_CALENDAR_TERM_COUNT];
    taiyin_chinese_new_moon_event
        new_moons[TAIYIN_C_CHINESE_CALENDAR_NEW_MOON_COUNT];
    taiyin_chinese_calendar_month
        months[TAIYIN_C_CHINESE_CALENDAR_MONTH_COUNT];
    uint8_t solar_term_count;
    uint8_t new_moon_count;
    uint8_t month_count;
    int8_t leap_month_index;
    int64_t first_winter_solstice_day_number;
    int64_t second_winter_solstice_day_number;
} taiyin_chinese_calendar_year;

TAIYIN_C_CHINESE_CALENDAR_API void TAIYIN_C_CALL taiyin_chinese_calendar_config_init(
    taiyin_chinese_calendar_config* config
);
TAIYIN_C_CHINESE_CALENDAR_API void TAIYIN_C_CALL
taiyin_chinese_calendar_config_init_china_standard_historical(
    taiyin_chinese_calendar_config* config,
    int32_t local_utc_offset_minutes
);
TAIYIN_C_CHINESE_CALENDAR_API void TAIYIN_C_CALL
taiyin_chinese_calendar_config_init_china_standard_astronomical(
    taiyin_chinese_calendar_config* config,
    int32_t local_utc_offset_minutes
);
TAIYIN_C_CHINESE_CALENDAR_API void TAIYIN_C_CALL
taiyin_chinese_calendar_config_init_local_astronomical_utc_offset(
    taiyin_chinese_calendar_config* config,
    int32_t utc_offset_minutes
);
TAIYIN_C_CHINESE_CALENDAR_API void TAIYIN_C_CALL
taiyin_chinese_calendar_config_init_local_astronomical_meridian(
    taiyin_chinese_calendar_config* config,
    double longitude_deg
);
/* Compatibility initializers: both select local astronomical mode. */
TAIYIN_C_CHINESE_CALENDAR_API void TAIYIN_C_CALL
taiyin_chinese_calendar_config_init_utc_offset(
    taiyin_chinese_calendar_config* config,
    int32_t utc_offset_minutes
);
TAIYIN_C_CHINESE_CALENDAR_API void TAIYIN_C_CALL
taiyin_chinese_calendar_config_init_meridian(
    taiyin_chinese_calendar_config* config,
    double longitude_deg
);
TAIYIN_C_CHINESE_CALENDAR_API void TAIYIN_C_CALL taiyin_solar_date_init(
    taiyin_solar_date* value
);
TAIYIN_C_CHINESE_CALENDAR_API void TAIYIN_C_CALL taiyin_lunar_date_init(
    taiyin_lunar_date* value
);
TAIYIN_C_CHINESE_CALENDAR_API void TAIYIN_C_CALL taiyin_chinese_solar_term_event_init(
    taiyin_chinese_solar_term_event* value
);
TAIYIN_C_CHINESE_CALENDAR_API void TAIYIN_C_CALL taiyin_chinese_calendar_year_init(
    taiyin_chinese_calendar_year* value
);

TAIYIN_C_CHINESE_CALENDAR_API taiyin_call_result TAIYIN_C_CALL
taiyin_chinese_calendar_context_create(
    const taiyin_context* astronomy,
    const taiyin_chinese_calendar_config* config,
    taiyin_chinese_calendar_context** out_context
);
TAIYIN_C_CHINESE_CALENDAR_API void TAIYIN_C_CALL taiyin_chinese_calendar_context_destroy(
    taiyin_chinese_calendar_context* context
);

TAIYIN_C_CHINESE_CALENDAR_API taiyin_call_result TAIYIN_C_CALL
taiyin_chinese_calendar_calc_year_ut(
    const taiyin_chinese_calendar_context* context,
    const taiyin_split_julian_date* jd_ut,
    taiyin_chinese_calendar_year* out,
    taiyin_ephemeris_diagnostic* diagnostic
);
// term_index_from_vernal_equinox uses a spring-equinox seasonal cycle: 0 is the
// spring equinox and 18 is the winter solstice in civil_year; 19 through 23
// are Xiaohan through Jingzhe earlier in the same civil year.
// For remote proleptic years this selects a seasonal crossing, not a guarantee
// that its rendered Gregorian date remains within civil_year.
TAIYIN_C_CHINESE_CALENDAR_API taiyin_call_result TAIYIN_C_CALL
taiyin_chinese_calendar_get_specific_jie_qi_ut(
    const taiyin_chinese_calendar_context* context,
    int32_t civil_year,
    uint8_t term_index_from_vernal_equinox,
    taiyin_chinese_solar_term_event* out,
    taiyin_ephemeris_diagnostic* diagnostic
);
// Prev includes a term exactly at jd_ut; Next advances to the subsequent term.
TAIYIN_C_CHINESE_CALENDAR_API taiyin_call_result TAIYIN_C_CALL
taiyin_chinese_calendar_get_prev_jie_qi_ut(
    const taiyin_chinese_calendar_context* context,
    const taiyin_split_julian_date* jd_ut,
    taiyin_chinese_solar_term_event* out,
    taiyin_ephemeris_diagnostic* diagnostic
);
TAIYIN_C_CHINESE_CALENDAR_API taiyin_call_result TAIYIN_C_CALL
taiyin_chinese_calendar_get_next_jie_qi_ut(
    const taiyin_chinese_calendar_context* context,
    const taiyin_split_julian_date* jd_ut,
    taiyin_chinese_solar_term_event* out,
    taiyin_ephemeris_diagnostic* diagnostic
);
TAIYIN_C_CHINESE_CALENDAR_API taiyin_call_result TAIYIN_C_CALL
taiyin_chinese_calendar_get_prev_jie_ut(
    const taiyin_chinese_calendar_context* context,
    const taiyin_split_julian_date* jd_ut,
    taiyin_chinese_solar_term_event* out,
    taiyin_ephemeris_diagnostic* diagnostic
);
TAIYIN_C_CHINESE_CALENDAR_API taiyin_call_result TAIYIN_C_CALL
taiyin_chinese_calendar_get_next_jie_ut(
    const taiyin_chinese_calendar_context* context,
    const taiyin_split_julian_date* jd_ut,
    taiyin_chinese_solar_term_event* out,
    taiyin_ephemeris_diagnostic* diagnostic
);
TAIYIN_C_CHINESE_CALENDAR_API taiyin_call_result TAIYIN_C_CALL
taiyin_chinese_calendar_get_prev_qi_ut(
    const taiyin_chinese_calendar_context* context,
    const taiyin_split_julian_date* jd_ut,
    taiyin_chinese_solar_term_event* out,
    taiyin_ephemeris_diagnostic* diagnostic
);
TAIYIN_C_CHINESE_CALENDAR_API taiyin_call_result TAIYIN_C_CALL
taiyin_chinese_calendar_get_next_qi_ut(
    const taiyin_chinese_calendar_context* context,
    const taiyin_split_julian_date* jd_ut,
    taiyin_chinese_solar_term_event* out,
    taiyin_ephemeris_diagnostic* diagnostic
);
TAIYIN_C_CHINESE_CALENDAR_API taiyin_call_result TAIYIN_C_CALL
taiyin_chinese_calendar_from_solar(
    const taiyin_chinese_calendar_context* context,
    const taiyin_solar_date* solar,
    taiyin_lunar_date* out,
    taiyin_ephemeris_diagnostic* diagnostic
);
TAIYIN_C_CHINESE_CALENDAR_API taiyin_call_result TAIYIN_C_CALL
taiyin_chinese_calendar_from_instant_ut(
    const taiyin_chinese_calendar_context* context,
    const taiyin_split_julian_date* jd_ut,
    taiyin_lunar_date* out,
    taiyin_ephemeris_diagnostic* diagnostic
);
TAIYIN_C_CHINESE_CALENDAR_API taiyin_call_result TAIYIN_C_CALL
taiyin_chinese_calendar_from_lunar(
    const taiyin_chinese_calendar_context* context,
    const taiyin_lunar_date* lunar,
    taiyin_solar_date* out,
    taiyin_ephemeris_diagnostic* diagnostic
);
TAIYIN_C_CHINESE_CALENDAR_API taiyin_call_result TAIYIN_C_CALL
taiyin_chinese_calendar_get_month_days(
    const taiyin_chinese_calendar_context* context,
    int32_t lunar_year,
    uint8_t month,
    taiyin_bool is_leap,
    uint8_t* out_day_count,
    taiyin_ephemeris_diagnostic* diagnostic
);

#ifdef __cplusplus
}
#endif

#endif
