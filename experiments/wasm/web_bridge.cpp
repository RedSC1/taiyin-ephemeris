// Versioned wasm32 transport adapter. Astronomy remains in the existing C ABI.
// No native public ABI symbols or data formats are changed by this adapter.
#include "taiyin/c/taiyin.h"
#include <cstdint>
#include <vector>
#include <new>
#include "transport_internal.h"

namespace {
void web_clear_target_registrations();
// Match the native C runtime's successful-initialization lifetime. Its first
// initialization preserves evaluators registered during setup; resets do not.
bool web_runtime_initialized = false;
int finish(taiyin_call_result code, double* out) {
    out[0] = taiyin_call_result_flags(code);
    return taiyin_call_result_status(code);
}
void diagnostic(const taiyin_ephemeris_diagnostic& d, double* out) {
    out[10] = d.status;
    out[11] = d.target_id; out[12] = d.center_id; out[13] = d.frame;
    out[14] = static_cast<double>(d.jd_tdb.day_number); out[15] = d.jd_tdb.day_fraction;
    out[16] = d.candidate_count; out[17] = d.attempted_method_id;
    out[18] = d.nearest_coverage_start; out[19] = d.nearest_coverage_end;
    out[20] = d.component_target_id; out[21] = d.component_center_id;
    out[22] = d.component_method_id; out[23] = d.time_scale_route;
    out[24] = d.time_scale_fallback_reason; out[25] = d.time_scale_flags;
    out[26] = d.tai_minus_utc_seconds; out[27] = d.dut1_seconds;
    out[28] = d.delta_t_seconds;
}
}

extern "C" {
int taiyin_web_bridge_version() { return 1; }

int taiyin_web_solar_time(const taiyin_context* context, int operation,
                          const double* in, double* out) {
    if (!web_valid_jd_day(in[0]))
        return finish(taiyin_make_call_result(TAIYIN_ERROR_INVALID_ARGUMENT, 0), out);
    const taiyin_split_julian_date jd={web_jd_day(in[0]),in[1]};
    taiyin_ephemeris_diagnostic diag;
    taiyin_ephemeris_diagnostic_init(&diag);
    taiyin_call_result result=taiyin_make_call_result(TAIYIN_ERROR_INVALID_ARGUMENT,0);
    if(operation==0 || operation==1) {
        taiyin_equation_of_time_result value;
        taiyin_equation_of_time_result_init(&value);
        result=operation==0 ? taiyin_calc_equation_of_time_ut(context,&jd,&value,&diag)
                            : taiyin_calc_equation_of_time_tt(context,&jd,&value,&diag);
        out[1]=static_cast<double>(value.jd_ut.day_number); out[2]=value.jd_ut.day_fraction;
        out[3]=static_cast<double>(value.jd_tt.day_number); out[4]=value.jd_tt.day_fraction;
        out[5]=value.equation_days; out[6]=value.equation_seconds;
        out[7]=value.apparent_sun_right_ascension_rad; out[8]=value.gast_rad;
    } else if(operation==2 || operation==3) {
        taiyin_split_julian_date value={0,0};
        result=operation==2 ? taiyin_local_mean_to_apparent_solar_time(context,&jd,in[2],&value,&diag)
                            : taiyin_local_apparent_to_mean_solar_time(context,&jd,in[2],&value,&diag);
        out[1]=static_cast<double>(value.day_number); out[2]=value.day_fraction;
    }
    diagnostic(diag,out);
    return finish(result,out);
}

int taiyin_web_time(const taiyin_context* context, int operation,
                    const double* in, double* out) try {
    if ((operation == 1 || operation == 3) && !web_valid_jd_day(in[1]))
        return finish(taiyin_make_call_result(TAIYIN_ERROR_INVALID_ARGUMENT, 0), out);
    // Scalar operations can contain an arbitrary finite double in slot 1;
    // do not narrow that unrelated value to int64 while building a JD.
    const taiyin_split_julian_date jd = {
        (operation == 1 || operation == 3) ? web_jd_day(in[1]) : 0,
        in[2]};
    taiyin_split_julian_date result = {0, 0};
    taiyin_calendar_datetime calendar;
    taiyin_calendar_datetime_init(&calendar);
    if (operation == 0 || operation >= 4) {
        calendar.year = web_integer<int32_t>(in[5]);
        calendar.month = web_integer<int32_t>(in[6]);
        calendar.day = web_integer<int32_t>(in[7]);
        calendar.hour = web_integer<int32_t>(in[8]);
        calendar.minute = web_integer<int32_t>(in[9]);
        calendar.second = in[10];
    }
    taiyin_call_result code = taiyin_make_call_result(TAIYIN_ERROR_INVALID_ARGUMENT, 0);
    if (operation == 0) {
        code = taiyin_julian_day_split(&calendar, &result);
    } else if (operation == 1) {
        code = taiyin_reverse_julian_day_split(&jd, &calendar);
        out[1] = calendar.year; out[2] = calendar.month; out[3] = calendar.day;
        out[4] = calendar.hour; out[5] = calendar.minute; out[6] = calendar.second;
    } else if (operation == 2) {
        switch (web_integer<int>(in[0])) {
        case 0: out[1] = taiyin_decimal_year_from_jd(in[1]); break;
        case 1: out[1] = taiyin_julian_centuries_from_j2000(in[1]); break;
        case 2: out[1] = taiyin_julian_millennia_from_j2000(in[1]); break;
        case 3: out[1] = taiyin_estimated_delta_t_seconds_for_decimal_year(in[1]); break;
        case 4: out[1] = taiyin_estimated_delta_t_seconds_from_ut1(in[1]); break;
        case 5: out[1] = taiyin_estimated_delta_t_seconds_from_tt(in[1]); break;
        case 6: out[1] = taiyin_delta_t_from_tai_minus_utc_and_dut1(in[1], in[2]); break;
        default: return finish(code, out);
        }
        code = taiyin_make_call_result(TAIYIN_STATUS_OK, 0);
    } else if (operation == 3) {
        switch (web_integer<int>(in[0])) {
        case 0: code = taiyin_tt_to_tdb_split(&jd, web_integer<int>(in[4]), &result); break;
        case 1: code = taiyin_tdb_to_tt_split(&jd, web_integer<int>(in[4]), &result); break;
        case 2: code = taiyin_utc_to_tai_split(&jd, in[3], &result); break;
        case 3: code = taiyin_tai_to_tt_split(&jd, &result); break;
        case 4: code = taiyin_utc_to_tt_split(&jd, in[3], &result); break;
        case 5: code = taiyin_utc_to_ut1_split(&jd, in[3], &result); break;
        case 6: code = taiyin_tt_to_ut1_split(&jd, in[3], &result); break;
        case 7: code = taiyin_ut1_to_tt_split(&jd, in[3], &result); break;
        }
    } else if (operation == 4) {
        code = taiyin_tai_minus_utc_seconds(&calendar, out + 1);
    } else if (operation == 5 || operation == 6) {
        taiyin_split_precise_time_scales scales;
        taiyin_split_precise_time_scales_init(&scales);
        taiyin_time_scale_diagnostic diag;
        taiyin_time_scale_diagnostic_init(&diag);
        code = operation == 5
            ? taiyin_make_split_precise_time_scales_from_utc(&calendar, in[11], in[12], web_integer<int>(in[4]), &scales)
            : taiyin_make_split_time_scales_from_utc(context, &calendar, &scales, &diag);
        const taiyin_split_julian_date dates[] = {scales.utc, scales.tai, scales.tt, scales.ut1, scales.tdb};
        for (int i = 0; i < 5; ++i) {
            out[1 + i * 2] = static_cast<double>(dates[i].day_number);
            out[2 + i * 2] = dates[i].day_fraction;
        }
        out[11] = scales.tai_minus_utc_seconds; out[12] = scales.dut1_seconds; out[13] = scales.delta_t_seconds;
        out[14] = diag.route; out[15] = diag.fallback_reason; out[16] = diag.flags;
        out[17] = diag.tdb_model_id; out[18] = diag.delta_t_model_id; out[19] = diag.ephemeris_family_id;
        out[20] = diag.tai_minus_utc_seconds; out[21] = diag.dut1_seconds; out[22] = diag.delta_t_seconds;
    } else if (operation == 7) {
        taiyin_split_estimated_time_scales scales;
        taiyin_split_estimated_time_scales_init(&scales);
        code = web_integer<uint8_t>(in[3]) != 0
            ? taiyin_make_split_time_scales_from_ut_delta_t(&calendar, in[11], web_integer<int>(in[4]), &scales)
            : taiyin_make_split_estimated_time_scales_from_ut(&calendar, web_integer<int>(in[4]), &scales);
        const taiyin_split_julian_date dates[] = {scales.ut1, scales.tt, scales.tdb};
        for (int i = 0; i < 3; ++i) {
            out[1 + i * 2] = static_cast<double>(dates[i].day_number);
            out[2 + i * 2] = dates[i].day_fraction;
        }
        out[7] = scales.delta_t_seconds;
    }
    if (operation == 0 || operation == 3) {
        out[1] = static_cast<double>(result.day_number); out[2] = result.day_fraction;
    }
    return finish(code, out);
} catch (const std::invalid_argument&) {
    return finish(taiyin_make_call_result(TAIYIN_ERROR_INVALID_ARGUMENT, 0), out);
}

int taiyin_web_initialize(const double* in, double* out) try {
    taiyin_runtime_config config;
    taiyin_runtime_config_init(&config);
    config.load_packaged_data = web_integer<uint8_t>(in[0]) != 0;
    config.load_builtin_eop = web_integer<uint8_t>(in[1]) != 0;
    if (in[2] != 0) config.segment_cache_max_entries = web_integer<size_t>(in[2]);
    config.strict_discovery = web_integer<uint8_t>(in[3]) != 0;
    // A reset clears native callbacks even if rebuilding the runtime fails.
    // A first initialization does not. Retain generations across both cases.
    if (web_runtime_initialized) web_clear_target_registrations();
    const auto result = taiyin_runtime_initialize(&config);
    if (taiyin_call_result_status(result) == TAIYIN_STATUS_OK)
        web_runtime_initialized = true;
    return finish(result, out);
} catch (const std::invalid_argument&) {
    return finish(taiyin_make_call_result(TAIYIN_ERROR_INVALID_ARGUMENT, 0), out);
}
int taiyin_web_add_source(const char* path, double* out) {
    return finish(taiyin_runtime_add_source_path(path), out);
}
int taiyin_web_load_file(int kind, const char* path, double* out) {
    switch (kind) {
    case 0: return finish(taiyin_runtime_add_source_path(path), out);
    case 1: return finish(taiyin_runtime_load_eop_table(path), out);
    case 2: return finish(taiyin_runtime_load_lunar_limb_model(path), out);
    case 3: return finish(taiyin_star_catalog_add_tsf1(path), out);
    default: return finish(taiyin_make_call_result(TAIYIN_ERROR_INVALID_ARGUMENT, 0), out);
    }
}
int taiyin_web_resource(int operation, double* out) {
    switch (operation) {
    case 0: return finish(taiyin_runtime_load_builtin_eop_table(), out);
    case 1: taiyin_runtime_clear_eop_table(); break;
    case 2: out[1] = taiyin_runtime_has_eop_table(); break;
    case 3: taiyin_runtime_clear_lunar_limb_model(); break;
    case 4: out[1] = taiyin_runtime_has_lunar_limb_model(); break;
    case 5: taiyin_star_catalog_clear(); break;
    case 6: out[1] = static_cast<double>(taiyin_star_catalog_count()); break;
    default: return finish(taiyin_make_call_result(TAIYIN_ERROR_INVALID_ARGUMENT, 0), out);
    }
    return finish(taiyin_make_call_result(TAIYIN_STATUS_OK, 0), out);
}
int taiyin_web_star_memory(const uint8_t* bytes, size_t length, double* out) {
    return finish(taiyin_star_catalog_add_tsc1_memory(bytes, length), out);
}
int taiyin_web_star_magnitude(const char* key, double* out) {
    return finish(taiyin_star_find_magnitude(key, out + 1), out);
}
int taiyin_web_context_create(double* out) {
    taiyin_context* context = 0;
    const taiyin_call_result code = taiyin_context_create(&context);
    out[1] = static_cast<double>(reinterpret_cast<uintptr_t>(context));
    return finish(code, out);
}
int taiyin_web_context_clone(const taiyin_context* source, double* out) {
    taiyin_context* context = 0;
    const taiyin_call_result code = taiyin_context_clone(source, &context);
    out[1] = static_cast<double>(reinterpret_cast<uintptr_t>(context));
    return finish(code, out);
}

int taiyin_web_calendar_create(const taiyin_context* astronomy,
                               const double* in, double* out) try {
    taiyin_chinese_calendar_config config;
    taiyin_chinese_calendar_config_init(&config);
    config.mode = web_integer<int32_t>(in[0]);
    config.day_boundary_mode = web_integer<int32_t>(in[1]);
    config.utc_offset_minutes = web_integer<int32_t>(in[2]);
    config.calendar_meridian_deg = in[3];
    taiyin_chinese_calendar_context* calendar = 0;
    const taiyin_call_result code = taiyin_chinese_calendar_context_create(astronomy, &config, &calendar);
    out[1] = static_cast<double>(reinterpret_cast<uintptr_t>(calendar));
    return finish(code, out);
} catch (const std::invalid_argument&) {
    return finish(taiyin_make_call_result(TAIYIN_ERROR_INVALID_ARGUMENT, 0), out);
}

// Calendar output contains 512 doubles, independent of C struct layout.
// Header: 1..6 counts/leap/solstices; terms: 16 + 5*i;
// moons: 160 + 3*i; months: 224 + 10*i; diagnostic: 490..508.
int taiyin_web_calendar(const taiyin_chinese_calendar_context* calendar,
                        int operation, const double* in, double* out) try {
    const bool uses_jd = operation == 0 || (operation >= 2 && operation <= 7)
        || operation == 9 || operation == 12;
    if (uses_jd && !web_valid_jd_day(in[0]))
        return finish(taiyin_make_call_result(TAIYIN_ERROR_INVALID_ARGUMENT, 0), out);
    const taiyin_split_julian_date jd = {uses_jd ? web_jd_day(in[0]) : 0, in[1]};
    taiyin_ephemeris_diagnostic diag;
    taiyin_ephemeris_diagnostic_init(&diag);
    taiyin_call_result code = taiyin_make_call_result(TAIYIN_ERROR_INVALID_ARGUMENT, 0);
    taiyin_chinese_solar_term_event term;
    taiyin_chinese_solar_term_event_init(&term);
    taiyin_lunar_date lunar;
    taiyin_lunar_date_init(&lunar);
    taiyin_solar_date solar;
    taiyin_solar_date_init(&solar);
    if (operation == 0) {
        taiyin_chinese_calendar_year year;
        taiyin_chinese_calendar_year_init(&year);
        code = taiyin_chinese_calendar_calc_year_ut(calendar, &jd, &year, &diag);
        if (taiyin_call_result_status(code) == TAIYIN_STATUS_OK) {
            out[1] = year.solar_term_count; out[2] = year.new_moon_count;
            out[3] = year.month_count; out[4] = year.leap_month_index;
            out[5] = static_cast<double>(year.first_winter_solstice_day_number);
            out[6] = static_cast<double>(year.second_winter_solstice_day_number);
            for (int i = 0; i < year.solar_term_count; ++i) {
                const auto& t = year.solar_terms[i]; double* v = out + 16 + i * 5;
                v[0] = t.index_from_winter_solstice; v[1] = t.target_longitude_rad;
                v[2] = static_cast<double>(t.jd_ut.day_number); v[3] = t.jd_ut.day_fraction;
                v[4] = static_cast<double>(t.civil_day_number);
            }
            for (int i = 0; i < year.new_moon_count; ++i) {
                const auto& m = year.new_moons[i]; double* v = out + 160 + i * 3;
                v[0] = static_cast<double>(m.jd_ut.day_number); v[1] = m.jd_ut.day_fraction;
                v[2] = static_cast<double>(m.civil_day_number);
            }
            for (int i = 0; i < year.month_count; ++i) {
                const auto& m = year.months[i]; double* v = out + 224 + i * 10;
                v[0] = m.lunar_year; v[1] = m.month; v[2] = m.is_leap;
                v[3] = m.day_count; v[4] = m.month_name; v[5] = m.month_building_branch;
                v[6] = static_cast<double>(m.first_civil_day_number);
                v[7] = static_cast<double>(m.astronomical_new_moon_jd_ut.day_number);
                v[8] = m.astronomical_new_moon_jd_ut.day_fraction;
                v[9] = m.historical_year;
            }
        }
    } else if (operation >= 1 && operation <= 7) {
        switch (operation) {
        case 1: code = taiyin_chinese_calendar_get_specific_jie_qi_ut(calendar, web_integer<int32_t>(in[0]), web_integer<uint8_t>(in[1]), &term, &diag); break;
        case 2: code = taiyin_chinese_calendar_get_prev_jie_qi_ut(calendar, &jd, &term, &diag); break;
        case 3: code = taiyin_chinese_calendar_get_next_jie_qi_ut(calendar, &jd, &term, &diag); break;
        case 4: code = taiyin_chinese_calendar_get_prev_jie_ut(calendar, &jd, &term, &diag); break;
        case 5: code = taiyin_chinese_calendar_get_next_jie_ut(calendar, &jd, &term, &diag); break;
        case 6: code = taiyin_chinese_calendar_get_prev_qi_ut(calendar, &jd, &term, &diag); break;
        case 7: code = taiyin_chinese_calendar_get_next_qi_ut(calendar, &jd, &term, &diag); break;
        }
        out[1] = term.index_from_winter_solstice; out[2] = term.target_longitude_rad;
        out[3] = static_cast<double>(term.jd_ut.day_number); out[4] = term.jd_ut.day_fraction;
        out[5] = static_cast<double>(term.civil_day_number);
    } else if (operation == 8 || operation == 9) {
        if (operation == 8) {
            solar.year = web_integer<int32_t>(in[0]); solar.month = web_integer<uint8_t>(in[1]); solar.day = web_integer<uint8_t>(in[2]);
            code = taiyin_chinese_calendar_from_solar(calendar, &solar, &lunar, &diag);
        } else code = taiyin_chinese_calendar_from_instant_ut(calendar, &jd, &lunar, &diag);
        out[1] = lunar.year; out[2] = lunar.month; out[3] = lunar.day;
        out[4] = lunar.is_leap; out[5] = lunar.month_days; out[6] = lunar.month_name;
        out[7] = lunar.historical_year;
    } else if (operation == 10) {
        lunar.year = web_integer<int32_t>(in[0]); lunar.month = web_integer<uint8_t>(in[1]); lunar.day = web_integer<uint8_t>(in[2]);
        lunar.is_leap = web_integer<uint8_t>(in[3]) != 0; lunar.month_days = web_integer<uint8_t>(in[4]); lunar.month_name = web_integer<uint8_t>(in[5]);
        code = taiyin_chinese_calendar_from_lunar(calendar, &lunar, &solar, &diag);
        out[1] = solar.year; out[2] = solar.month; out[3] = solar.day;
    } else if (operation == 11) {
        uint8_t days = 0;
        code = taiyin_chinese_calendar_get_month_days(calendar, web_integer<int32_t>(in[0]), web_integer<uint8_t>(in[1]), web_integer<uint8_t>(in[2]) != 0, &days, &diag);
        out[1] = days;
    } else if (operation == 12) {
        taiyin_calendar_datetime virtual_time;
        taiyin_calendar_datetime_init(&virtual_time);
        virtual_time.year = web_integer<int32_t>(in[3]); virtual_time.month = web_integer<int32_t>(in[4]); virtual_time.day = web_integer<int32_t>(in[5]);
        virtual_time.hour = web_integer<int32_t>(in[6]); virtual_time.minute = web_integer<int32_t>(in[7]); virtual_time.second = in[8];
        taiyin_ganzhi_four_pillars pillars;
        taiyin_ganzhi_four_pillars_init(&pillars);
        code = taiyin_chinese_calendar_calc_four_pillars_ut(calendar, &jd, &virtual_time, web_integer<int32_t>(in[2]), &pillars, &diag);
        out[1] = pillars.year; out[2] = pillars.month; out[3] = pillars.day; out[4] = pillars.hour;
    }
    diagnostic(diag, out + 480);
    return finish(code, out);
} catch (const std::invalid_argument&) {
    return finish(taiyin_make_call_result(TAIYIN_ERROR_INVALID_ARGUMENT, 0), out);
}

// Input layout is owned by bridge version 1, not by native struct packing.
// Both transport buffers contain 32 doubles. Output[0] is uint32 result_flags;
// status is an int32 return value. No int64 is narrowed through a JS Number.
int taiyin_web_position(const taiyin_context* context, int state,
                        const double* in, double* out) try {
    for (int i = 0; i < 32; ++i) out[i] = 0;
    const int mode = web_integer<int>(in[0]);
    const int target = web_integer<int>(in[1]);
    if ((mode != 4 && !web_valid_jd_day(in[2]))
        || (mode == 2 && !web_valid_jd_day(in[4])))
        return finish(taiyin_make_call_result(TAIYIN_ERROR_INVALID_ARGUMENT, 0), out);
    const taiyin_split_julian_date jd = {mode != 4 ? web_jd_day(in[2]) : 0, in[3]};
    const taiyin_split_julian_date tt = {mode == 2 ? web_jd_day(in[4]) : 0, in[5]};
    const uint32_t flags = web_integer<uint32_t>(in[7]);
    taiyin_calendar_datetime utc;
    taiyin_calendar_datetime_init(&utc);
    if (mode == 4) {
        utc.year = web_integer<int32_t>(in[8]);
        utc.month = web_integer<int32_t>(in[9]);
        utc.day = web_integer<int32_t>(in[10]);
        utc.hour = web_integer<int32_t>(in[11]);
        utc.minute = web_integer<int32_t>(in[12]);
        utc.second = in[13];
    }
    taiyin_ephemeris_diagnostic diag;
    taiyin_ephemeris_diagnostic_init(&diag);
    taiyin_call_result code = taiyin_make_call_result(TAIYIN_ERROR_INVALID_ARGUMENT, 0);
    if (state) {
        taiyin_cartesian_state value;
        taiyin_cartesian_state_init(&value);
        switch (mode) {
        case 0: code = taiyin_calc_state_tt(context, target, &jd, flags, &value, &diag); break;
        case 1: code = taiyin_calc_state_ut(context, target, &jd, flags, &value, &diag); break;
        case 2: code = taiyin_calc_state_tdb(context, target, &jd, &tt, flags, &value, &diag); break;
        case 3: code = taiyin_calc_state_ut_delta_t(context, target, &jd, in[6], flags, &value, &diag); break;
        case 4: code = taiyin_calc_state_utc(context, target, &utc, flags, &value, &diag); break;
        }
        out[1] = value.position_au.x; out[2] = value.position_au.y; out[3] = value.position_au.z;
        out[4] = value.velocity_au_per_day.x; out[5] = value.velocity_au_per_day.y; out[6] = value.velocity_au_per_day.z;
        out[7] = value.acceleration_au_per_day2.x; out[8] = value.acceleration_au_per_day2.y; out[9] = value.acceleration_au_per_day2.z;
    } else {
        switch (mode) {
        case 0: code = taiyin_calc_position_tt(context, target, &jd, flags, out + 1, &diag); break;
        case 1: code = taiyin_calc_position_ut(context, target, &jd, flags, out + 1, &diag); break;
        case 2: code = taiyin_calc_position_tdb(context, target, &jd, &tt, flags, out + 1, &diag); break;
        case 3: code = taiyin_calc_position_ut_delta_t(context, target, &jd, in[6], flags, out + 1, &diag); break;
        case 4: code = taiyin_calc_position_utc(context, target, &utc, flags, out + 1, &diag); break;
        }
    }
    diagnostic(diag, out);
    return finish(code, out);
} catch (const std::invalid_argument&) {
    return finish(taiyin_make_call_result(TAIYIN_ERROR_INVALID_ARGUMENT, 0), out);
}

int taiyin_web_positions(const taiyin_context* context, int count,
                         const double* in, double* out) try {
    const int mode = web_integer<int>(in[0]);
    if ((mode != 4 && !web_valid_jd_day(in[2]))
        || (mode == 2 && !web_valid_jd_day(in[4])))
        return finish(taiyin_make_call_result(TAIYIN_ERROR_INVALID_ARGUMENT, 0), out);
    if (count < 1 || count > 100000) {
        return finish(taiyin_make_call_result(TAIYIN_ERROR_INVALID_ARGUMENT, 0), out);
    }
    try {
        std::vector<int32_t> targets(count);
        std::vector<double> values(count * 6, 0);
        std::vector<taiyin_ephemeris_diagnostic> diags(count);
        for (int i = 0; i < count; ++i) {
            targets[i] = web_integer<int32_t>(in[32 + i]);
            taiyin_ephemeris_diagnostic_init(&diags[i]);
        }
        const taiyin_split_julian_date jd = {mode != 4 ? web_jd_day(in[2]) : 0, in[3]};
        const taiyin_split_julian_date tt = {mode == 2 ? web_jd_day(in[4]) : 0, in[5]};
        const uint32_t flags = web_integer<uint32_t>(in[7]);
        taiyin_calendar_datetime utc;
        taiyin_calendar_datetime_init(&utc);
        if (mode == 4) {
            utc.year = web_integer<int32_t>(in[8]); utc.month = web_integer<int32_t>(in[9]);
            utc.day = web_integer<int32_t>(in[10]); utc.hour = web_integer<int32_t>(in[11]);
            utc.minute = web_integer<int32_t>(in[12]); utc.second = in[13];
        }
        taiyin_call_result code = taiyin_make_call_result(TAIYIN_ERROR_INVALID_ARGUMENT, 0);
        switch (web_integer<int>(in[0])) {
        case 0: code = taiyin_calc_positions_tt(context, targets.data(), count, &jd, flags, values.data(), diags.data()); break;
        case 1: code = taiyin_calc_positions_ut(context, targets.data(), count, &jd, flags, values.data(), diags.data()); break;
        case 2: code = taiyin_calc_positions_tdb(context, targets.data(), count, &jd, &tt, flags, values.data(), diags.data()); break;
        case 3: code = taiyin_calc_positions_ut_delta_t(context, targets.data(), count, &jd, in[6], flags, values.data(), diags.data()); break;
        case 4: code = taiyin_calc_positions_utc(context, targets.data(), count, &utc, flags, values.data(), diags.data()); break;
        }
        for (int i = 0; i < count; ++i) {
            for (int k = 0; k < 32; ++k) out[i * 32 + k] = 0;
            for (int k = 0; k < 6; ++k) out[i * 32 + 1 + k] = values[i * 6 + k];
            diagnostic(diags[i], out + i * 32);
        }
        return finish(code, out);
    } catch (const std::bad_alloc&) {
        return finish(taiyin_make_call_result(TAIYIN_ERROR_OUT_OF_MEMORY, 0), out);
    }
} catch (const std::invalid_argument&) {
    return finish(taiyin_make_call_result(TAIYIN_ERROR_INVALID_ARGUMENT, 0), out);
}

int taiyin_web_configure(taiyin_context* context, int operation,
                         const double* in, double* out) try {
    taiyin_call_result code = taiyin_make_call_result(TAIYIN_ERROR_UNSUPPORTED, 0);
    switch (operation) {
    case 0: code = taiyin_context_set_route_rule(context, web_integer<uint64_t>(in[0])); break;
    case 1: code = taiyin_context_set_allow_utc_out_of_range_estimate(context, web_integer<uint8_t>(in[0]) != 0); break;
    case 2: code = taiyin_context_set_geocentric_observer(context, web_integer<int32_t>(in[0]), web_integer<int32_t>(in[1])); break;
    case 3: {
        taiyin_observer_location location;
        taiyin_observer_location_init(&location);
        location.longitude_deg = in[0]; location.latitude_deg = in[1]; location.height_m = in[2];
        code = taiyin_context_set_observer_location(context, &location); break;
    }
    case 4: code = taiyin_context_clear_observer_location(context); break;
    case 5: {
        taiyin_astro_model_config model;
        taiyin_astro_model_config_init(&model);
        model.tdb_model_id = web_integer<int32_t>(in[0]);
        model.precession_model_id = web_integer<int32_t>(in[1]);
        model.nutation_model_id = web_integer<int32_t>(in[2]);
        model.obliquity_model_id = web_integer<int32_t>(in[3]);
        model.frame_route_id = web_integer<int32_t>(in[4]);
        code = taiyin_context_set_astro_models(context, &model); break;
    }
    case 6: {
        taiyin_apparent_config config;
        taiyin_apparent_config_init(&config);
        config.flags = web_integer<uint32_t>(in[0]);
        config.output_frame_id = web_integer<int32_t>(in[1]);
        config.light_time_method_id = web_integer<int32_t>(in[2]);
        config.shapiro_delay_model_id = web_integer<int32_t>(in[3]);
        config.aberration_model_id = web_integer<int32_t>(in[4]);
        config.deflection_model_id = web_integer<int32_t>(in[5]);
        config.max_light_time_iterations = web_integer<uint32_t>(in[6]);
        config.light_time_tolerance_days = in[7]; config.matrix_derivative_step_days = in[8];
        code = taiyin_context_set_apparent_config(context, &config); break;
    }
    case 7: code = taiyin_context_use_solar_deflector(context); break;
    case 8: code = taiyin_context_clear_deflectors(context); break;
    case 9: code = taiyin_context_reset(context); break;
    case 10: code = taiyin_context_set_light_time_iteration(context, web_integer<uint32_t>(in[0]), in[1]); break;
    case 11: code = taiyin_context_set_delta_t_model(context, web_integer<int32_t>(in[0]), web_integer<int32_t>(in[1])); break;
    case 12: code = taiyin_context_set_tdb_model(context, web_integer<int32_t>(in[0])); break;
    }
    return finish(code, out);
} catch (const std::invalid_argument&) {
    return finish(taiyin_make_call_result(TAIYIN_ERROR_INVALID_ARGUMENT, 0), out);
}
}

#include "eclipse_bridge.inc"

#include "bazi_bridge.inc"
#include "ziwei_bridge.inc"
#include "astronomy_bridge.generated.inc"
#include "astronomy_extra.inc"
#include "astronomy_callbacks.inc"
