#include "taiyin/c/chinese_calendar.h"

#include "c_api_internal.h"
#include "chinese_calendar_context_internal.h"
#include "taiyin/chinese_calendar/calendar.h"

#include <cstring>
#include <new>

namespace {

template <typename T>
void init_struct(T* value) noexcept {
    if (!value) return;
    std::memset(value, 0, sizeof(*value));
    value->struct_size = sizeof(*value);
}

bool valid_diagnostic(taiyin_ephemeris_diagnostic* diagnostic) noexcept {
    return !diagnostic || taiyin_c_internal::valid_struct(diagnostic);
}

taiyin::chinese_calendar::ChineseCalendarConfig to_cpp_config(
    const taiyin_chinese_calendar_config& source
) noexcept {
    taiyin::chinese_calendar::ChineseCalendarConfig out;
    out.mode = source.mode;
    out.day_boundary_mode = source.day_boundary_mode;
    out.utc_offset_minutes = source.utc_offset_minutes;
    out.reserved = 0;
    out.calendar_meridian_deg = source.calendar_meridian_deg;
    return out;
}

taiyin::chinese_calendar::SolarDate to_cpp_solar(
    const taiyin_solar_date& source
) noexcept {
    taiyin::chinese_calendar::SolarDate out;
    out.year = source.year;
    out.month = source.month;
    out.day = source.day;
    return out;
}

taiyin::chinese_calendar::LunarDate to_cpp_lunar(
    const taiyin_lunar_date& source
) noexcept {
    taiyin::chinese_calendar::LunarDate out;
    out.year = source.year;
    out.month = source.month;
    out.day = source.day;
    out.is_leap = source.is_leap;
    out.month_days = source.month_days;
    out.month_name = source.month_name;
    return out;
}

void copy_solar(
    const taiyin::chinese_calendar::SolarDate& source,
    taiyin_solar_date* out
) noexcept {
    init_struct(out);
    out->year = source.year;
    out->month = source.month;
    out->day = source.day;
}

void copy_lunar(
    const taiyin::chinese_calendar::LunarDate& source,
    taiyin_lunar_date* out
) noexcept {
    init_struct(out);
    out->year = source.year;
    out->month = source.month;
    out->day = source.day;
    out->is_leap = source.is_leap;
    out->month_days = source.month_days;
    out->month_name = source.month_name;
}

void copy_solar_term(
    const taiyin::chinese_calendar::SolarTermEvent& source,
    taiyin_chinese_solar_term_event* out
) noexcept {
    init_struct(out);
    out->index_from_winter_solstice = source.index_from_winter_solstice;
    out->target_longitude_rad = source.target_longitude_rad;
    taiyin_c_internal::from_cpp_split_jd(source.jd_ut, &out->jd_ut);
    out->civil_day_number = source.civil_day_number;
}

void copy_year(
    const taiyin::chinese_calendar::ChineseCalendarYear& source,
    taiyin_chinese_calendar_year* out
) noexcept {
    init_struct(out);
    for (std::size_t i = 0;
         i < taiyin::chinese_calendar::TAIYIN_CHINESE_CALENDAR_TERM_COUNT;
         ++i) {
        copy_solar_term(source.solar_terms[i], &out->solar_terms[i]);
    }
    for (std::size_t i = 0;
         i < taiyin::chinese_calendar::TAIYIN_CHINESE_CALENDAR_NEW_MOON_COUNT;
         ++i) {
        taiyin_chinese_new_moon_event& target = out->new_moons[i];
        init_struct(&target);
        taiyin_c_internal::from_cpp_split_jd(
            source.new_moons[i].jd_ut, &target.jd_ut);
        target.civil_day_number = source.new_moons[i].civil_day_number;
    }
    for (std::size_t i = 0;
         i < taiyin::chinese_calendar::TAIYIN_CHINESE_CALENDAR_MONTH_COUNT;
         ++i) {
        taiyin_chinese_calendar_month& target = out->months[i];
        init_struct(&target);
        target.lunar_year = source.months[i].lunar_year;
        target.month = source.months[i].month;
        target.is_leap = source.months[i].is_leap;
        target.day_count = source.months[i].day_count;
        target.month_name = source.months[i].month_name;
        target.month_building_branch = source.months[i].month_building_branch;
        target.first_civil_day_number =
            source.months[i].first_civil_day_number;
        taiyin_c_internal::from_cpp_split_jd(
            source.months[i].astronomical_new_moon_jd_ut,
            &target.astronomical_new_moon_jd_ut);
    }
    out->solar_term_count = source.solar_term_count;
    out->new_moon_count = source.new_moon_count;
    out->month_count = source.month_count;
    out->leap_month_index = source.leap_month_index;
    out->first_winter_solstice_day_number =
        source.first_winter_solstice_day_number;
    out->second_winter_solstice_day_number =
        source.second_winter_solstice_day_number;
}

using SolarTermQueryFn = taiyin::Status (*) (
    const taiyin::chinese_calendar::ChineseCalendarContext*,
    taiyin::SplitJulianDate,
    taiyin::chinese_calendar::SolarTermEvent*,
    taiyin::runtime::EphemerisEvalDiagnostic*
);

taiyin_status query_solar_term(
    const taiyin_chinese_calendar_context* context,
    const taiyin_split_julian_date* jd_ut,
    taiyin_chinese_solar_term_event* out,
    taiyin_ephemeris_diagnostic* diagnostic,
    SolarTermQueryFn query
) noexcept {
    if (!context || !taiyin_c_internal::valid_split_jd(jd_ut)
        || !taiyin_c_internal::valid_struct(out)
        || !valid_diagnostic(diagnostic)) {
        return taiyin_c_internal::invalid_argument();
    }
    taiyin::chinese_calendar::SolarTermEvent cpp_out;
    taiyin::runtime::EphemerisEvalDiagnostic cpp_diagnostic;
    const taiyin::Status status = query(
        &context->value,
        taiyin_c_internal::to_cpp_split_jd(*jd_ut),
        &cpp_out,
        diagnostic ? &cpp_diagnostic : 0);
    if (status == taiyin::TAIYIN_STATUS_OK) copy_solar_term(cpp_out, out);
    taiyin_c_internal::from_cpp_diagnostic(cpp_diagnostic, diagnostic);
    return status;
}

taiyin_status query_specific_solar_term(
    const taiyin_chinese_calendar_context* context,
    int32_t civil_year,
    uint8_t term_index_from_vernal_equinox,
    taiyin_chinese_solar_term_event* out,
    taiyin_ephemeris_diagnostic* diagnostic
) noexcept {
    if (!context || !taiyin_c_internal::valid_struct(out)
        || !valid_diagnostic(diagnostic)) {
        return taiyin_c_internal::invalid_argument();
    }
    taiyin::chinese_calendar::SolarTermEvent cpp_out;
    taiyin::runtime::EphemerisEvalDiagnostic cpp_diagnostic;
    const taiyin::Status status =
        taiyin::chinese_calendar::getSpecificJieQi(
            &context->value,
            civil_year,
            term_index_from_vernal_equinox,
            &cpp_out,
            diagnostic ? &cpp_diagnostic : 0);
    if (status == taiyin::TAIYIN_STATUS_OK) copy_solar_term(cpp_out, out);
    taiyin_c_internal::from_cpp_diagnostic(cpp_diagnostic, diagnostic);
    return status;
}

}  // namespace

extern "C" {

void TAIYIN_C_CALL taiyin_chinese_calendar_config_init(
    taiyin_chinese_calendar_config* config
) {
    init_struct(config);
    if (!config) return;
    config->mode =
        TAIYIN_C_CHINESE_CALENDAR_CHINA_STANDARD_HISTORICAL;
    config->day_boundary_mode =
        TAIYIN_C_CHINESE_CALENDAR_FIXED_UTC_OFFSET;
    config->utc_offset_minutes = 8 * 60;
    config->calendar_meridian_deg = 120.0;
}

void TAIYIN_C_CALL
taiyin_chinese_calendar_config_init_china_standard_historical(
    taiyin_chinese_calendar_config* config,
    int32_t local_utc_offset_minutes
) {
    init_struct(config);
    if (!config) return;
    config->mode =
        TAIYIN_C_CHINESE_CALENDAR_CHINA_STANDARD_HISTORICAL;
    config->day_boundary_mode =
        TAIYIN_C_CHINESE_CALENDAR_FIXED_UTC_OFFSET;
    config->utc_offset_minutes = local_utc_offset_minutes;
    config->calendar_meridian_deg =
        static_cast<double>(local_utc_offset_minutes) / 4.0;
}

void TAIYIN_C_CALL
taiyin_chinese_calendar_config_init_china_standard_astronomical(
    taiyin_chinese_calendar_config* config,
    int32_t local_utc_offset_minutes
) {
    taiyin_chinese_calendar_config_init_china_standard_historical(
        config, local_utc_offset_minutes);
    if (config) {
        config->mode =
            TAIYIN_C_CHINESE_CALENDAR_CHINA_STANDARD_ASTRONOMICAL;
    }
}

void TAIYIN_C_CALL
taiyin_chinese_calendar_config_init_local_astronomical_utc_offset(
    taiyin_chinese_calendar_config* config,
    int32_t utc_offset_minutes
) {
    init_struct(config);
    if (!config) return;
    config->mode = TAIYIN_C_CHINESE_CALENDAR_LOCAL_ASTRONOMICAL;
    config->day_boundary_mode =
        TAIYIN_C_CHINESE_CALENDAR_FIXED_UTC_OFFSET;
    config->utc_offset_minutes = utc_offset_minutes;
    config->calendar_meridian_deg =
        static_cast<double>(utc_offset_minutes) / 4.0;
}

void TAIYIN_C_CALL
taiyin_chinese_calendar_config_init_local_astronomical_meridian(
    taiyin_chinese_calendar_config* config,
    double longitude_deg
) {
    init_struct(config);
    if (!config) return;
    config->mode = TAIYIN_C_CHINESE_CALENDAR_LOCAL_ASTRONOMICAL;
    config->day_boundary_mode =
        TAIYIN_C_CHINESE_CALENDAR_MEAN_SOLAR_MERIDIAN;
    config->utc_offset_minutes = 0;
    config->calendar_meridian_deg = longitude_deg;
}

void TAIYIN_C_CALL
taiyin_chinese_calendar_config_init_utc_offset(
    taiyin_chinese_calendar_config* config,
    int32_t utc_offset_minutes
) {
    taiyin_chinese_calendar_config_init_local_astronomical_utc_offset(
        config, utc_offset_minutes);
}

void TAIYIN_C_CALL taiyin_chinese_calendar_config_init_meridian(
    taiyin_chinese_calendar_config* config,
    double longitude_deg
) {
    taiyin_chinese_calendar_config_init_local_astronomical_meridian(
        config, longitude_deg);
}

void TAIYIN_C_CALL taiyin_solar_date_init(taiyin_solar_date* value) {
    init_struct(value);
}

void TAIYIN_C_CALL taiyin_lunar_date_init(taiyin_lunar_date* value) {
    init_struct(value);
}

void TAIYIN_C_CALL taiyin_chinese_solar_term_event_init(
    taiyin_chinese_solar_term_event* value
) {
    init_struct(value);
}

void TAIYIN_C_CALL taiyin_chinese_calendar_year_init(
    taiyin_chinese_calendar_year* value
) {
    init_struct(value);
    if (!value) return;
    for (size_t i = 0; i < TAIYIN_C_CHINESE_CALENDAR_TERM_COUNT; ++i) {
        init_struct(&value->solar_terms[i]);
    }
    for (size_t i = 0; i < TAIYIN_C_CHINESE_CALENDAR_NEW_MOON_COUNT; ++i) {
        init_struct(&value->new_moons[i]);
    }
    for (size_t i = 0; i < TAIYIN_C_CHINESE_CALENDAR_MONTH_COUNT; ++i) {
        init_struct(&value->months[i]);
    }
}

taiyin_status TAIYIN_C_CALL taiyin_chinese_calendar_context_create(
    const taiyin_context* astronomy,
    const taiyin_chinese_calendar_config* config,
    taiyin_chinese_calendar_context** out_context
) {
    if (out_context) *out_context = 0;
    if (!astronomy || !taiyin_c_internal::valid_struct(config)
        || !out_context) {
        return taiyin_c_internal::invalid_argument();
    }
    taiyin_chinese_calendar_context* created =
        new (std::nothrow) taiyin_chinese_calendar_context();
    if (!created) return taiyin::TAIYIN_ERROR_OUT_OF_MEMORY;
    const taiyin::chinese_calendar::ChineseCalendarConfig cpp_config =
        to_cpp_config(*config);
    const taiyin::Status status =
        taiyin::chinese_calendar::initialize_context(
            &created->value, &astronomy->value, &cpp_config);
    if (status != taiyin::TAIYIN_STATUS_OK) {
        delete created;
        return status;
    }
    *out_context = created;
    return taiyin::TAIYIN_STATUS_OK;
}

void TAIYIN_C_CALL taiyin_chinese_calendar_context_destroy(
    taiyin_chinese_calendar_context* context
) {
    delete context;
}

taiyin_status TAIYIN_C_CALL taiyin_chinese_calendar_calc_year_ut(
    const taiyin_chinese_calendar_context* context,
    const taiyin_split_julian_date* jd_ut,
    taiyin_chinese_calendar_year* out,
    taiyin_ephemeris_diagnostic* diagnostic
) {
    if (!context || !taiyin_c_internal::valid_split_jd(jd_ut)
        || !taiyin_c_internal::valid_struct(out)
        || !valid_diagnostic(diagnostic)) {
        return taiyin_c_internal::invalid_argument();
    }
    taiyin::chinese_calendar::ChineseCalendarYear cpp_out;
    taiyin::runtime::EphemerisEvalDiagnostic cpp_diagnostic;
    const taiyin::Status status = taiyin::chinese_calendar::calcY(
        &context->value, taiyin_c_internal::to_cpp_split_jd(*jd_ut), &cpp_out,
        diagnostic ? &cpp_diagnostic : 0);
    if (status == taiyin::TAIYIN_STATUS_OK) copy_year(cpp_out, out);
    taiyin_c_internal::from_cpp_diagnostic(cpp_diagnostic, diagnostic);
    return status;
}

taiyin_status TAIYIN_C_CALL
taiyin_chinese_calendar_get_specific_jie_qi_ut(
    const taiyin_chinese_calendar_context* context,
    int32_t civil_year,
    uint8_t term_index_from_vernal_equinox,
    taiyin_chinese_solar_term_event* out,
    taiyin_ephemeris_diagnostic* diagnostic
) {
    return query_specific_solar_term(
        context, civil_year, term_index_from_vernal_equinox, out, diagnostic);
}

taiyin_status TAIYIN_C_CALL taiyin_chinese_calendar_get_prev_jie_qi_ut(
    const taiyin_chinese_calendar_context* context,
    const taiyin_split_julian_date* jd_ut,
    taiyin_chinese_solar_term_event* out,
    taiyin_ephemeris_diagnostic* diagnostic
) {
    return query_solar_term(
        context, jd_ut, out, diagnostic,
        &taiyin::chinese_calendar::getPrevJieQi);
}

taiyin_status TAIYIN_C_CALL taiyin_chinese_calendar_get_next_jie_qi_ut(
    const taiyin_chinese_calendar_context* context,
    const taiyin_split_julian_date* jd_ut,
    taiyin_chinese_solar_term_event* out,
    taiyin_ephemeris_diagnostic* diagnostic
) {
    return query_solar_term(
        context, jd_ut, out, diagnostic,
        &taiyin::chinese_calendar::getNextJieQi);
}

taiyin_status TAIYIN_C_CALL taiyin_chinese_calendar_get_prev_jie_ut(
    const taiyin_chinese_calendar_context* context,
    const taiyin_split_julian_date* jd_ut,
    taiyin_chinese_solar_term_event* out,
    taiyin_ephemeris_diagnostic* diagnostic
) {
    return query_solar_term(
        context, jd_ut, out, diagnostic,
        &taiyin::chinese_calendar::getPrevJie);
}

taiyin_status TAIYIN_C_CALL taiyin_chinese_calendar_get_next_jie_ut(
    const taiyin_chinese_calendar_context* context,
    const taiyin_split_julian_date* jd_ut,
    taiyin_chinese_solar_term_event* out,
    taiyin_ephemeris_diagnostic* diagnostic
) {
    return query_solar_term(
        context, jd_ut, out, diagnostic,
        &taiyin::chinese_calendar::getNextJie);
}

taiyin_status TAIYIN_C_CALL taiyin_chinese_calendar_get_prev_qi_ut(
    const taiyin_chinese_calendar_context* context,
    const taiyin_split_julian_date* jd_ut,
    taiyin_chinese_solar_term_event* out,
    taiyin_ephemeris_diagnostic* diagnostic
) {
    return query_solar_term(
        context, jd_ut, out, diagnostic,
        &taiyin::chinese_calendar::getPrevQi);
}

taiyin_status TAIYIN_C_CALL taiyin_chinese_calendar_get_next_qi_ut(
    const taiyin_chinese_calendar_context* context,
    const taiyin_split_julian_date* jd_ut,
    taiyin_chinese_solar_term_event* out,
    taiyin_ephemeris_diagnostic* diagnostic
) {
    return query_solar_term(
        context, jd_ut, out, diagnostic,
        &taiyin::chinese_calendar::getNextQi);
}

taiyin_status TAIYIN_C_CALL taiyin_chinese_calendar_from_solar(
    const taiyin_chinese_calendar_context* context,
    const taiyin_solar_date* solar,
    taiyin_lunar_date* out,
    taiyin_ephemeris_diagnostic* diagnostic
) {
    if (!context || !taiyin_c_internal::valid_struct(solar)
        || !taiyin_c_internal::valid_struct(out)
        || !valid_diagnostic(diagnostic)) {
        return taiyin_c_internal::invalid_argument();
    }
    const taiyin::chinese_calendar::SolarDate cpp_solar =
        to_cpp_solar(*solar);
    taiyin::chinese_calendar::LunarDate cpp_out;
    taiyin::runtime::EphemerisEvalDiagnostic cpp_diagnostic;
    const taiyin::Status status = taiyin::chinese_calendar::fromSolar(
        &context->value, &cpp_solar, &cpp_out,
        diagnostic ? &cpp_diagnostic : 0);
    if (status == taiyin::TAIYIN_STATUS_OK) copy_lunar(cpp_out, out);
    taiyin_c_internal::from_cpp_diagnostic(cpp_diagnostic, diagnostic);
    return status;
}

taiyin_status TAIYIN_C_CALL taiyin_chinese_calendar_from_instant_ut(
    const taiyin_chinese_calendar_context* context,
    const taiyin_split_julian_date* jd_ut,
    taiyin_lunar_date* out,
    taiyin_ephemeris_diagnostic* diagnostic
) {
    if (!context || !taiyin_c_internal::valid_split_jd(jd_ut)
        || !taiyin_c_internal::valid_struct(out)
        || !valid_diagnostic(diagnostic)) {
        return taiyin_c_internal::invalid_argument();
    }
    taiyin::chinese_calendar::LunarDate cpp_out;
    taiyin::runtime::EphemerisEvalDiagnostic cpp_diagnostic;
    const taiyin::Status status = taiyin::chinese_calendar::fromInstant(
        &context->value, taiyin_c_internal::to_cpp_split_jd(*jd_ut), &cpp_out,
        diagnostic ? &cpp_diagnostic : 0);
    if (status == taiyin::TAIYIN_STATUS_OK) copy_lunar(cpp_out, out);
    taiyin_c_internal::from_cpp_diagnostic(cpp_diagnostic, diagnostic);
    return status;
}

taiyin_status TAIYIN_C_CALL taiyin_chinese_calendar_from_lunar(
    const taiyin_chinese_calendar_context* context,
    const taiyin_lunar_date* lunar,
    taiyin_solar_date* out,
    taiyin_ephemeris_diagnostic* diagnostic
) {
    if (!context || !taiyin_c_internal::valid_struct(lunar)
        || !taiyin_c_internal::valid_struct(out)
        || !valid_diagnostic(diagnostic)) {
        return taiyin_c_internal::invalid_argument();
    }
    const taiyin::chinese_calendar::LunarDate cpp_lunar =
        to_cpp_lunar(*lunar);
    taiyin::chinese_calendar::SolarDate cpp_out;
    taiyin::runtime::EphemerisEvalDiagnostic cpp_diagnostic;
    const taiyin::Status status = taiyin::chinese_calendar::fromLunar(
        &context->value, &cpp_lunar, &cpp_out,
        diagnostic ? &cpp_diagnostic : 0);
    if (status == taiyin::TAIYIN_STATUS_OK) copy_solar(cpp_out, out);
    taiyin_c_internal::from_cpp_diagnostic(cpp_diagnostic, diagnostic);
    return status;
}

taiyin_status TAIYIN_C_CALL taiyin_chinese_calendar_get_month_days(
    const taiyin_chinese_calendar_context* context,
    int32_t lunar_year,
    uint8_t month,
    taiyin_bool is_leap,
    uint8_t* out_day_count,
    taiyin_ephemeris_diagnostic* diagnostic
) {
    if (!context || !out_day_count || is_leap > 1
        || !valid_diagnostic(diagnostic)) {
        return taiyin_c_internal::invalid_argument();
    }
    taiyin::runtime::EphemerisEvalDiagnostic cpp_diagnostic;
    const taiyin::Status status =
        taiyin::chinese_calendar::getLunarMonthNum(
            &context->value, lunar_year, month, is_leap != 0,
            out_day_count, diagnostic ? &cpp_diagnostic : 0);
    taiyin_c_internal::from_cpp_diagnostic(cpp_diagnostic, diagnostic);
    return status;
}

}  // extern "C"
