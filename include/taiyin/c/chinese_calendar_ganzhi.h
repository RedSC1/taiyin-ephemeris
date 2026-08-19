#ifndef TAIYIN_C_CHINESE_CALENDAR_GANZHI_H
#define TAIYIN_C_CHINESE_CALENDAR_GANZHI_H

#include "taiyin/c/chinese_calendar.h"

#ifdef __cplusplus
extern "C" {
#endif

#define TAIYIN_GANZHI_INVALID 0xffu
#define TAIYIN_GANZHI_INVALID_NAYIN 0xffu

typedef uint8_t taiyin_ganzhi;

enum taiyin_ganzhi_rat_hour_mode {
    TAIYIN_GANZHI_RAT_HOUR_NO_SPLIT = 0,
    TAIYIN_GANZHI_RAT_HOUR_TODAY_GAN = 1,
    TAIYIN_GANZHI_RAT_HOUR_TOMORROW_GAN = 2
};

enum taiyin_ganzhi_wuxing {
    TAIYIN_GANZHI_WUXING_WATER = 0,
    TAIYIN_GANZHI_WUXING_WOOD = 1,
    TAIYIN_GANZHI_WUXING_METAL = 2,
    TAIYIN_GANZHI_WUXING_EARTH = 3,
    TAIYIN_GANZHI_WUXING_FIRE = 4
};

typedef struct taiyin_ganzhi_four_pillars {
    uint32_t struct_size;
    taiyin_ganzhi year;
    taiyin_ganzhi month;
    taiyin_ganzhi day;
    taiyin_ganzhi hour;
} taiyin_ganzhi_four_pillars;

TAIYIN_C_GANZHI_API void TAIYIN_C_CALL taiyin_ganzhi_four_pillars_init(
    taiyin_ganzhi_four_pillars* value
);

TAIYIN_C_GANZHI_API taiyin_call_result TAIYIN_C_CALL taiyin_ganzhi_make(
    uint8_t stem_id,
    uint8_t branch_id,
    taiyin_ganzhi* out_value
);
TAIYIN_C_GANZHI_API taiyin_call_result TAIYIN_C_CALL taiyin_ganzhi_advance(
    taiyin_ganzhi value,
    int32_t delta,
    taiyin_ganzhi* out_value
);
// month_index follows 0=Yin, ..., 10=Zi, 11=Chou.
TAIYIN_C_GANZHI_API taiyin_call_result TAIYIN_C_CALL taiyin_ganzhi_get_month(
    uint8_t year_stem_id,
    uint8_t month_index,
    taiyin_ganzhi* out_value
);
// hour_index follows 0=Zi, ..., 11=Hai.
TAIYIN_C_GANZHI_API taiyin_call_result TAIYIN_C_CALL taiyin_ganzhi_get_hour(
    uint8_t day_stem_id,
    uint8_t hour_index,
    taiyin_ganzhi* out_value
);
TAIYIN_C_GANZHI_API taiyin_call_result TAIYIN_C_CALL taiyin_ganzhi_calc_day_pillar(
    const taiyin_calendar_datetime* civil_date,
    taiyin_ganzhi* out_value
);
TAIYIN_C_GANZHI_API taiyin_call_result TAIYIN_C_CALL taiyin_ganzhi_get_nayin_element(
    taiyin_ganzhi value,
    uint8_t* out_element_id
);
TAIYIN_C_GANZHI_API taiyin_call_result TAIYIN_C_CALL taiyin_ganzhi_get_nayin_id(
    taiyin_ganzhi value,
    uint8_t* out_nayin_id
);

TAIYIN_C_GANZHI_API taiyin_call_result TAIYIN_C_CALL
taiyin_chinese_calendar_calc_four_pillars_ut(
    const taiyin_chinese_calendar_context* context,
    const taiyin_split_julian_date* instant_utc,
    const taiyin_calendar_datetime* virtual_time,
    int32_t rat_hour_mode,
    taiyin_ganzhi_four_pillars* out,
    taiyin_ephemeris_diagnostic* diagnostic
);

#ifdef __cplusplus
}
#endif

#endif
