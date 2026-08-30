#ifndef TAIYIN_CHINESE_CALENDAR_GANZHI_RULES_INTERNAL_H
#define TAIYIN_CHINESE_CALENDAR_GANZHI_RULES_INTERNAL_H

#include <cstdint>

#if (defined(__GNUC__) || defined(__clang__)) && !defined(_WIN32)
#define TAIYIN_GANZHI_RULES_INTERNAL_API __attribute__((visibility("hidden")))
#else
#define TAIYIN_GANZHI_RULES_INTERNAL_API
#endif

namespace taiyin {
namespace chinese_calendar {
namespace rules {

TAIYIN_GANZHI_RULES_INTERNAL_API int32_t make(
    uint8_t stem_id, uint8_t branch_id, uint8_t* out_value) noexcept;
TAIYIN_GANZHI_RULES_INTERNAL_API int32_t advance(
    uint8_t value, int32_t delta, uint8_t* out_value) noexcept;
TAIYIN_GANZHI_RULES_INTERNAL_API int32_t month(
    uint8_t year_stem_id, uint8_t month_index, uint8_t* out_value) noexcept;
TAIYIN_GANZHI_RULES_INTERNAL_API int32_t hour(
    uint8_t day_stem_id, uint8_t hour_index, uint8_t* out_value) noexcept;
TAIYIN_GANZHI_RULES_INTERNAL_API int32_t nayin_element(
    uint8_t value, uint8_t* out_element_id) noexcept;
TAIYIN_GANZHI_RULES_INTERNAL_API int32_t nayin_id(
    uint8_t value, uint8_t* out_nayin_id) noexcept;

}  // namespace rules
}  // namespace chinese_calendar
}  // namespace taiyin

#undef TAIYIN_GANZHI_RULES_INTERNAL_API

#endif
