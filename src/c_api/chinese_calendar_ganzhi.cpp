#include "taiyin/c/chinese_calendar_ganzhi.h"

#include "c_api_internal.h"
#include "chinese_calendar_context_internal.h"

#include "taiyin/chinese_calendar/ganzhi.h"

#include <cstring>

namespace {

bool valid_diagnostic(const taiyin_ephemeris_diagnostic* value) noexcept {
    return !value || value->struct_size >= sizeof(*value);
}

}  // namespace

extern "C" {

void TAIYIN_C_CALL taiyin_ganzhi_four_pillars_init(
    taiyin_ganzhi_four_pillars* value
) {
    if (!value) return;
    std::memset(value, 0, sizeof(*value));
    value->struct_size = sizeof(*value);
    value->year = TAIYIN_GANZHI_INVALID;
    value->month = TAIYIN_GANZHI_INVALID;
    value->day = TAIYIN_GANZHI_INVALID;
    value->hour = TAIYIN_GANZHI_INVALID;
}

taiyin_call_result TAIYIN_C_CALL taiyin_ganzhi_make(
    uint8_t stem_id,
    uint8_t branch_id,
    taiyin_ganzhi* out_value
) {
    return taiyin_c_internal::pack_call_result(taiyin::chinese_calendar::make_ganzhi(stem_id, branch_id, out_value));
}

taiyin_call_result TAIYIN_C_CALL taiyin_ganzhi_advance(
    taiyin_ganzhi value,
    int32_t delta,
    taiyin_ganzhi* out_value
) {
    return taiyin_c_internal::pack_call_result(taiyin::chinese_calendar::advance_ganzhi(value, delta, out_value));
}

taiyin_call_result TAIYIN_C_CALL taiyin_ganzhi_get_month(
    uint8_t year_stem_id,
    uint8_t month_index,
    taiyin_ganzhi* out_value
) {
    return taiyin_c_internal::pack_call_result(taiyin::chinese_calendar::get_month_ganzhi(
        year_stem_id, month_index, out_value));
}

taiyin_call_result TAIYIN_C_CALL taiyin_ganzhi_get_hour(
    uint8_t day_stem_id,
    uint8_t hour_index,
    taiyin_ganzhi* out_value
) {
    return taiyin_c_internal::pack_call_result(taiyin::chinese_calendar::get_hour_ganzhi(
        day_stem_id, hour_index, out_value));
}

taiyin_call_result TAIYIN_C_CALL taiyin_ganzhi_calc_day_pillar(
    const taiyin_calendar_datetime* civil_date,
    taiyin_ganzhi* out_value
) {
    if (!taiyin_c_internal::valid_struct(civil_date) || !out_value) {
        return taiyin_c_internal::pack_call_result(TAIYIN_ERROR_INVALID_ARGUMENT);
    }
    return taiyin_c_internal::pack_call_result(taiyin::chinese_calendar::calculate_day_pillar(
        taiyin_c_internal::to_cpp_datetime(*civil_date), out_value));
}

taiyin_call_result TAIYIN_C_CALL taiyin_ganzhi_get_nayin_element(
    taiyin_ganzhi value,
    uint8_t* out_element_id
) {
    return taiyin_c_internal::pack_call_result(taiyin::chinese_calendar::get_nayin_element(value, out_element_id));
}

taiyin_call_result TAIYIN_C_CALL taiyin_ganzhi_get_nayin_id(
    taiyin_ganzhi value,
    uint8_t* out_nayin_id
) {
    return taiyin_c_internal::pack_call_result(taiyin::chinese_calendar::get_nayin_id(value, out_nayin_id));
}

taiyin_call_result TAIYIN_C_CALL taiyin_chinese_calendar_calc_four_pillars_ut(
    const taiyin_chinese_calendar_context* context,
    const taiyin_split_julian_date* instant_utc,
    const taiyin_calendar_datetime* virtual_time,
    int32_t rat_hour_mode,
    taiyin_ganzhi_four_pillars* out,
    taiyin_ephemeris_diagnostic* diagnostic
) {
    if (!context || !taiyin_c_internal::valid_split_jd(instant_utc)
        || !taiyin_c_internal::valid_struct(virtual_time)
        || !taiyin_c_internal::valid_struct(out) || !valid_diagnostic(diagnostic)) {
        return taiyin_c_internal::pack_call_result(TAIYIN_ERROR_INVALID_ARGUMENT);
    }
    taiyin_c_internal::TrackedCalendarContext tracked(context->value);
    taiyin::chinese_calendar::GanzhiFourPillars cpp_out;
    taiyin::runtime::EphemerisEvalDiagnostic cpp_diagnostic;
    const taiyin::Status status = taiyin::chinese_calendar::calculate_four_pillars(
        &tracked.value,
        taiyin_c_internal::to_cpp_split_jd(*instant_utc),
        taiyin_c_internal::to_cpp_datetime(*virtual_time),
        rat_hour_mode,
        &cpp_out,
        diagnostic ? &cpp_diagnostic : 0);
    if (status == taiyin::TAIYIN_STATUS_OK) {
        out->year = cpp_out.year;
        out->month = cpp_out.month;
        out->day = cpp_out.day;
        out->hour = cpp_out.hour;
    }
    taiyin_c_internal::from_cpp_diagnostic(cpp_diagnostic, diagnostic);
    return taiyin_c_internal::pack_call_result(status, tracked.flags);
}

}  // extern "C"
