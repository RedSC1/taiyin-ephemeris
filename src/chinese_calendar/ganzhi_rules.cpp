#include "chinese_calendar/ganzhi_rules_internal.h"

namespace taiyin {
namespace chinese_calendar {
namespace rules {
namespace {

constexpr uint8_t kNayinElementBySexagenaryIndex[60] = {
    2, 2, 4, 4, 1, 1, 3, 3, 2, 2,
    4, 4, 0, 0, 3, 3, 2, 2, 1, 1,
    0, 0, 3, 3, 4, 4, 1, 1, 0, 0,
    2, 2, 4, 4, 1, 1, 3, 3, 2, 2,
    4, 4, 0, 0, 3, 3, 2, 2, 1, 1,
    0, 0, 3, 3, 4, 4, 1, 1, 0, 0,
};

bool valid_stem(uint8_t value) noexcept { return value < 10u; }
bool valid_branch(uint8_t value) noexcept { return value < 12u; }
uint8_t stem_of(uint8_t value) noexcept { return static_cast<uint8_t>(value >> 4); }
uint8_t branch_of(uint8_t value) noexcept { return static_cast<uint8_t>(value & 0x0fu); }

bool valid_ganzhi(uint8_t value) noexcept {
    return valid_stem(stem_of(value)) && valid_branch(branch_of(value))
        && ((stem_of(value) & 1u) == (branch_of(value) & 1u));
}

int32_t index_of(uint8_t value, int32_t* out_index) noexcept {
    if (!out_index || !valid_ganzhi(value)) return -1;
    *out_index = (6 * static_cast<int32_t>(stem_of(value))
        - 5 * static_cast<int32_t>(branch_of(value)) + 60) % 60;
    return 0;
}

}  // namespace

int32_t make(uint8_t stem_id, uint8_t branch_id, uint8_t* out_value) noexcept {
    if (!out_value || !valid_stem(stem_id) || !valid_branch(branch_id)
        || ((stem_id & 1u) != (branch_id & 1u))) {
        return -1;
    }
    *out_value = static_cast<uint8_t>((stem_id << 4) | branch_id);
    return 0;
}

int32_t advance(uint8_t value, int32_t delta, uint8_t* out_value) noexcept {
    if (!out_value) return -1;
    int32_t index = 0;
    if (index_of(value, &index) != 0) return -1;
    int32_t next_index = (index + delta % 60) % 60;
    if (next_index < 0) next_index += 60;
    *out_value = static_cast<uint8_t>(((next_index % 10) << 4) | (next_index % 12));
    return 0;
}

int32_t month(uint8_t year_stem_id, uint8_t month_index, uint8_t* out_value) noexcept {
    if (!valid_stem(year_stem_id) || month_index >= 12u) return -1;
    const uint8_t start_stem_id = static_cast<uint8_t>(((year_stem_id % 5u) * 2u + 2u) % 10u);
    return make(static_cast<uint8_t>((start_stem_id + month_index) % 10u),
        static_cast<uint8_t>((month_index + 2u) % 12u), out_value);
}

int32_t hour(uint8_t day_stem_id, uint8_t hour_index, uint8_t* out_value) noexcept {
    if (!valid_stem(day_stem_id) || hour_index >= 12u) return -1;
    const uint8_t start_stem_id = static_cast<uint8_t>((day_stem_id % 5u) * 2u);
    return make(static_cast<uint8_t>((start_stem_id + hour_index) % 10u), hour_index, out_value);
}

int32_t nayin_element(uint8_t value, uint8_t* out_element_id) noexcept {
    if (!out_element_id) return -1;
    int32_t index = 0;
    if (index_of(value, &index) != 0) return -1;
    *out_element_id = kNayinElementBySexagenaryIndex[index];
    return 0;
}

int32_t nayin_id(uint8_t value, uint8_t* out_nayin_id) noexcept {
    if (!out_nayin_id) return -1;
    int32_t index = 0;
    if (index_of(value, &index) != 0) return -1;
    *out_nayin_id = static_cast<uint8_t>(index / 2);
    return 0;
}

}  // namespace rules
}  // namespace chinese_calendar
}  // namespace taiyin
