#ifndef TAIYIN_CHINESE_CALENDAR_GANZHI_RULES_FFI_H
#define TAIYIN_CHINESE_CALENDAR_GANZHI_RULES_FFI_H

#include <cstdint>

#if defined(__GNUC__) && !defined(_WIN32)
#define TAIYIN_GANZHI_RULES_INTERNAL __attribute__((visibility("hidden")))
#else
#define TAIYIN_GANZHI_RULES_INTERNAL
#endif

extern "C" {

TAIYIN_GANZHI_RULES_INTERNAL int32_t taiyin_ganzhi_rules_make(
    uint8_t stem_id,
    uint8_t branch_id,
    uint8_t* out_value
);
TAIYIN_GANZHI_RULES_INTERNAL int32_t taiyin_ganzhi_rules_advance(
    uint8_t value,
    int32_t delta,
    uint8_t* out_value
);
TAIYIN_GANZHI_RULES_INTERNAL int32_t taiyin_ganzhi_rules_month(
    uint8_t year_stem_id,
    uint8_t month_index,
    uint8_t* out_value
);
TAIYIN_GANZHI_RULES_INTERNAL int32_t taiyin_ganzhi_rules_hour(
    uint8_t day_stem_id,
    uint8_t hour_index,
    uint8_t* out_value
);
TAIYIN_GANZHI_RULES_INTERNAL int32_t taiyin_ganzhi_rules_nayin_element(
    uint8_t value,
    uint8_t* out_element_id
);
TAIYIN_GANZHI_RULES_INTERNAL int32_t taiyin_ganzhi_rules_nayin_id(
    uint8_t value,
    uint8_t* out_nayin_id
);

}

#undef TAIYIN_GANZHI_RULES_INTERNAL

#endif
