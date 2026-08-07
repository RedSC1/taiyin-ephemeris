#include "taiyin/c/bazi.h"

#include "c_api_internal.h"
#include "chinese_calendar_context_internal.h"

#ifdef TAIYIN_C_HAS_BAZI_EXTENSION
#include "taiyin/bazi/bazi.h"
#endif

#include <cstring>
#include <limits>
#include <new>
#include <vector>

#ifdef TAIYIN_C_HAS_BAZI_EXTENSION
struct taiyin_bazi_context {
    taiyin::bazi::BaziContext value;
};
#endif

namespace {

template <typename T>
void init_struct(T* value) noexcept {
    if (!value) return;
    std::memset(value, 0, sizeof(*value));
    value->struct_size = sizeof(*value);
}

#ifdef TAIYIN_C_HAS_BAZI_EXTENSION
taiyin::bazi::BaziContextConfig to_cpp_config(
    const taiyin_bazi_context_config& value
) noexcept {
    taiyin::bazi::BaziContextConfig out;
    out.earth_palace_mode = value.earth_palace_mode;
    out.qiyun_direction_mode = value.qiyun_direction_mode;
    out.qiyun_time_model = value.qiyun_time_model;
    out.dayun_boundary_model = value.dayun_boundary_model;
    return out;
}

taiyin::chinese_calendar::GanzhiFourPillars to_cpp_pillars(
    const taiyin_ganzhi_four_pillars& value
) noexcept {
    taiyin::chinese_calendar::GanzhiFourPillars out;
    out.year = value.year;
    out.month = value.month;
    out.day = value.day;
    out.hour = value.hour;
    return out;
}

taiyin::bazi::BaziChart to_cpp_chart(const taiyin_bazi_chart& value) noexcept {
    taiyin::bazi::BaziChart out;
    out.pillars.year = value.year_pillar;
    out.pillars.month = value.month_pillar;
    out.pillars.day = value.day_pillar;
    out.pillars.hour = value.hour_pillar;
    out.extra.ming_gong = value.ming_gong;
    out.extra.shen_gong = value.shen_gong;
    out.extra.tai_yuan = value.tai_yuan;
    out.extra.tai_xi = value.tai_xi;
    return out;
}

taiyin::bazi::BaziQiYunResult to_cpp_qiyun(
    const taiyin_bazi_qiyun_result& value
) noexcept {
    taiyin::bazi::BaziQiYunResult out;
    out.direction = value.direction;
    out.time_model = value.time_model;
    out.reference_jie_index = value.reference_jie_index;
    out.jie_interval_days = value.jie_interval_days;
    out.start_age_years = value.start_age_years;
    out.offset_years = value.offset_years;
    out.offset_months = value.offset_months;
    out.offset_days = value.offset_days;
    out.offset_hours = value.offset_hours;
    out.offset_minutes = value.offset_minutes;
    out.offset_seconds = value.offset_seconds;
    out.reference_jie_jd_ut = taiyin_c_internal::to_cpp_split_jd(
        value.reference_jie_jd_ut);
    out.start_jd_ut = taiyin_c_internal::to_cpp_split_jd(value.start_jd_ut);
    out.start_civil_time = taiyin_c_internal::to_cpp_datetime(
        value.start_civil_time);
    return out;
}

void copy_chart(const taiyin::bazi::BaziChart& source, taiyin_bazi_chart* out) noexcept {
    out->year_pillar = source.pillars.year;
    out->month_pillar = source.pillars.month;
    out->day_pillar = source.pillars.day;
    out->hour_pillar = source.pillars.hour;
    out->ming_gong = source.extra.ming_gong;
    out->shen_gong = source.extra.shen_gong;
    out->tai_yuan = source.extra.tai_yuan;
    out->tai_xi = source.extra.tai_xi;
    std::memcpy(out->hidden_stem_count, source.hidden_stem_count, sizeof(out->hidden_stem_count));
    std::memcpy(out->hidden_stems, source.hidden_stems, sizeof(out->hidden_stems));
    std::memcpy(out->visible_ten_gods, source.visible_ten_gods, sizeof(out->visible_ten_gods));
    std::memcpy(out->hidden_ten_gods, source.hidden_ten_gods, sizeof(out->hidden_ten_gods));
    std::memcpy(out->life_stages, source.life_stages, sizeof(out->life_stages));
    std::memcpy(out->nayin_ids, source.nayin_ids, sizeof(out->nayin_ids));
}

void copy_relation(
    const taiyin::bazi::BaziRelation& source,
    taiyin_bazi_relation* out
) noexcept {
    out->struct_size = sizeof(*out);
    out->kind = source.kind;
    out->pillar_mask = source.pillar_mask;
    out->combined_element_id = source.combined_element_id;
    std::memset(out->reserved, 0, sizeof(out->reserved));
}

void copy_qiyun(
    const taiyin::bazi::BaziQiYunResult& source,
    taiyin_bazi_qiyun_result* out
) noexcept {
    init_struct(out);
    out->direction = source.direction;
    out->time_model = source.time_model;
    out->reference_jie_index = source.reference_jie_index;
    out->jie_interval_days = source.jie_interval_days;
    out->start_age_years = source.start_age_years;
    out->offset_years = source.offset_years;
    out->offset_months = source.offset_months;
    out->offset_days = source.offset_days;
    out->offset_hours = source.offset_hours;
    out->offset_minutes = source.offset_minutes;
    out->offset_seconds = source.offset_seconds;
    taiyin_c_internal::from_cpp_split_jd(
        source.reference_jie_jd_ut, &out->reference_jie_jd_ut);
    taiyin_c_internal::from_cpp_split_jd(source.start_jd_ut, &out->start_jd_ut);
    init_struct(&out->start_civil_time);
    taiyin_c_internal::from_cpp_datetime(
        source.start_civil_time, &out->start_civil_time);
}

void copy_dayun(
    const taiyin::bazi::BaziDaYun& source,
    taiyin_bazi_dayun* out
) noexcept {
    init_struct(out);
    out->index = source.index;
    out->ganzhi = source.ganzhi;
    out->start_virtual_age = source.start_virtual_age;
    out->end_virtual_age = source.end_virtual_age;
    taiyin_c_internal::from_cpp_split_jd(source.start_jd_ut, &out->start_jd_ut);
    taiyin_c_internal::from_cpp_split_jd(source.end_jd_ut, &out->end_jd_ut);
    init_struct(&out->start_civil_time);
    init_struct(&out->end_civil_time);
    taiyin_c_internal::from_cpp_datetime(
        source.start_civil_time, &out->start_civil_time);
    taiyin_c_internal::from_cpp_datetime(
        source.end_civil_time, &out->end_civil_time);
}

void copy_xiaoyun(
    const taiyin::bazi::BaziXiaoYun& source,
    taiyin_bazi_xiaoyun* out
) noexcept {
    init_struct(out);
    out->age = source.age;
    out->ganzhi = source.ganzhi;
}

void copy_siling_segment(
    const taiyin::bazi::BaziRenyuanSilingSegment& source,
    taiyin_bazi_renyuan_siling_segment* out
) noexcept {
    init_struct(out);
    out->stem_id = source.stem_id;
    out->origin_kind = source.origin_kind;
    out->segment_index = source.segment_index;
    out->start_day = source.start_day;
    out->end_day = source.end_day;
}

void copy_siling_result(
    const taiyin::bazi::BaziRenyuanSilingResult& source,
    taiyin_bazi_renyuan_siling_result* out
) noexcept {
    init_struct(out);
    out->table_model = source.table_model;
    out->time_model = source.time_model;
    out->month_branch_id = source.month_branch_id;
    out->stem_id = source.stem_id;
    out->origin_kind = source.origin_kind;
    out->segment_index = source.segment_index;
    out->previous_jie_index = source.previous_jie_index;
    out->days_since_jie = source.days_since_jie;
    out->segment_start_day = source.segment_start_day;
    out->segment_end_day = source.segment_end_day;
    taiyin_c_internal::from_cpp_split_jd(
        source.previous_jie_jd_ut, &out->previous_jie_jd_ut);
}
#endif

}  // namespace

extern "C" {

void TAIYIN_C_CALL taiyin_bazi_context_config_init(taiyin_bazi_context_config* value) {
    init_struct(value);
    if (!value) return;
    value->earth_palace_mode = TAIYIN_BAZI_EARTH_PALACE_FIRE_EARTH;
    value->qiyun_direction_mode = TAIYIN_BAZI_QIYUN_DIRECTION_YEAR_STEM_GENDER;
    value->qiyun_time_model = TAIYIN_BAZI_QIYUN_TRADITIONAL_CALENDAR;
    value->dayun_boundary_model = TAIYIN_BAZI_DAYUN_CIVIL_YEARS;
}

void TAIYIN_C_CALL taiyin_bazi_chart_init(taiyin_bazi_chart* value) {
    init_struct(value);
    if (!value) return;
    value->year_pillar = TAIYIN_GANZHI_INVALID;
    value->month_pillar = TAIYIN_GANZHI_INVALID;
    value->day_pillar = TAIYIN_GANZHI_INVALID;
    value->hour_pillar = TAIYIN_GANZHI_INVALID;
    value->ming_gong = TAIYIN_GANZHI_INVALID;
    value->shen_gong = TAIYIN_GANZHI_INVALID;
    value->tai_yuan = TAIYIN_GANZHI_INVALID;
    value->tai_xi = TAIYIN_GANZHI_INVALID;
    std::memset(value->hidden_stems, TAIYIN_GANZHI_INVALID, sizeof(value->hidden_stems));
    std::memset(value->visible_ten_gods, TAIYIN_GANZHI_INVALID, sizeof(value->visible_ten_gods));
    std::memset(value->hidden_ten_gods, TAIYIN_GANZHI_INVALID, sizeof(value->hidden_ten_gods));
    std::memset(value->life_stages, TAIYIN_GANZHI_INVALID, sizeof(value->life_stages));
    std::memset(value->nayin_ids, TAIYIN_GANZHI_INVALID_NAYIN, sizeof(value->nayin_ids));
}

void TAIYIN_C_CALL taiyin_bazi_relation_init(taiyin_bazi_relation* value) {
    init_struct(value);
    if (!value) return;
    value->kind = -1;
    value->combined_element_id = TAIYIN_BAZI_INVALID_WUXING;
}

void TAIYIN_C_CALL taiyin_bazi_qiyun_result_init(taiyin_bazi_qiyun_result* value) {
    init_struct(value);
    if (!value) return;
    value->reference_jie_index = 0xffu;
    value->jie_interval_days = std::numeric_limits<double>::quiet_NaN();
    value->start_age_years = std::numeric_limits<double>::quiet_NaN();
    value->offset_seconds = std::numeric_limits<double>::quiet_NaN();
    value->reference_jie_jd_ut.day_fraction =
        std::numeric_limits<double>::quiet_NaN();
    value->start_jd_ut.day_fraction = std::numeric_limits<double>::quiet_NaN();
    init_struct(&value->start_civil_time);
    value->start_civil_time.second = std::numeric_limits<double>::quiet_NaN();
}

void TAIYIN_C_CALL taiyin_bazi_dayun_init(taiyin_bazi_dayun* value) {
    init_struct(value);
    if (!value) return;
    value->ganzhi = TAIYIN_GANZHI_INVALID;
    value->start_jd_ut.day_fraction = std::numeric_limits<double>::quiet_NaN();
    value->end_jd_ut.day_fraction = std::numeric_limits<double>::quiet_NaN();
    init_struct(&value->start_civil_time);
    init_struct(&value->end_civil_time);
    value->start_civil_time.second = std::numeric_limits<double>::quiet_NaN();
    value->end_civil_time.second = std::numeric_limits<double>::quiet_NaN();
}

void TAIYIN_C_CALL taiyin_bazi_xiaoyun_init(taiyin_bazi_xiaoyun* value) {
    init_struct(value);
    if (!value) return;
    value->ganzhi = TAIYIN_GANZHI_INVALID;
}

void TAIYIN_C_CALL taiyin_bazi_renyuan_siling_segment_init(
    taiyin_bazi_renyuan_siling_segment* value
) {
    init_struct(value);
    if (!value) return;
    value->stem_id = TAIYIN_GANZHI_INVALID;
    value->segment_index = 0xffu;
    value->start_day = std::numeric_limits<double>::quiet_NaN();
    value->end_day = std::numeric_limits<double>::quiet_NaN();
}

void TAIYIN_C_CALL taiyin_bazi_renyuan_siling_result_init(
    taiyin_bazi_renyuan_siling_result* value
) {
    init_struct(value);
    if (!value) return;
    value->month_branch_id = 0xffu;
    value->stem_id = TAIYIN_GANZHI_INVALID;
    value->segment_index = 0xffu;
    value->previous_jie_index = 0xffu;
    value->days_since_jie = std::numeric_limits<double>::quiet_NaN();
    value->segment_start_day = std::numeric_limits<double>::quiet_NaN();
    value->segment_end_day = std::numeric_limits<double>::quiet_NaN();
    value->previous_jie_jd_ut.day_fraction =
        std::numeric_limits<double>::quiet_NaN();
}

taiyin_status TAIYIN_C_CALL taiyin_bazi_context_create(
    const taiyin_bazi_context_config* bazi_config,
    taiyin_bazi_context** out_context
) {
    if (out_context) *out_context = 0;
#ifndef TAIYIN_C_HAS_BAZI_EXTENSION
    (void)bazi_config;
    return TAIYIN_ERROR_UNSUPPORTED;
#else
    if (!taiyin_c_internal::valid_struct(bazi_config) || !out_context) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    taiyin_bazi_context* created = new (std::nothrow) taiyin_bazi_context();
    if (!created) return TAIYIN_ERROR_OUT_OF_MEMORY;
    const taiyin::bazi::BaziContextConfig config = to_cpp_config(*bazi_config);
    const taiyin::Status status = taiyin::bazi::initialize_context(
        &created->value, &config);
    if (status != taiyin::TAIYIN_STATUS_OK) {
        delete created;
        return status;
    }
    *out_context = created;
    return TAIYIN_STATUS_OK;
#endif
}

void TAIYIN_C_CALL taiyin_bazi_context_destroy(taiyin_bazi_context* context) {
#ifdef TAIYIN_C_HAS_BAZI_EXTENSION
    delete context;
#else
    (void)context;
#endif
}

taiyin_status TAIYIN_C_CALL taiyin_bazi_get_kong_wang(
    taiyin_ganzhi value,
    uint8_t out_branches[2]
) {
#ifndef TAIYIN_C_HAS_BAZI_EXTENSION
    (void)value;
    (void)out_branches;
    return TAIYIN_ERROR_UNSUPPORTED;
#else
    return taiyin::bazi::get_kong_wang(value, out_branches);
#endif
}

taiyin_status TAIYIN_C_CALL taiyin_bazi_get_ten_god(
    uint8_t day_stem_id,
    uint8_t target_stem_id,
    uint8_t* out_ten_god_id
) {
#ifndef TAIYIN_C_HAS_BAZI_EXTENSION
    (void)day_stem_id;
    (void)target_stem_id;
    (void)out_ten_god_id;
    return TAIYIN_ERROR_UNSUPPORTED;
#else
    return taiyin::bazi::get_ten_god(
        day_stem_id, target_stem_id, out_ten_god_id);
#endif
}

taiyin_status TAIYIN_C_CALL taiyin_bazi_get_hidden_stems(
    uint8_t branch_id,
    uint8_t out_stems[TAIYIN_BAZI_HIDDEN_STEM_CAPACITY],
    uint8_t* out_count
) {
#ifndef TAIYIN_C_HAS_BAZI_EXTENSION
    (void)branch_id;
    (void)out_stems;
    (void)out_count;
    return TAIYIN_ERROR_UNSUPPORTED;
#else
    return taiyin::bazi::get_hidden_stems(branch_id, out_stems, out_count);
#endif
}

taiyin_status TAIYIN_C_CALL taiyin_bazi_calc_stem_relation(
    uint8_t stem_a,
    uint8_t stem_b,
    uint32_t* out_flags,
    uint8_t* out_combined_element_id
) {
#ifndef TAIYIN_C_HAS_BAZI_EXTENSION
    (void)stem_a;
    (void)stem_b;
    (void)out_flags;
    (void)out_combined_element_id;
    return TAIYIN_ERROR_UNSUPPORTED;
#else
    return taiyin::bazi::calculate_stem_relation(
        stem_a, stem_b, out_flags, out_combined_element_id);
#endif
}

taiyin_status TAIYIN_C_CALL taiyin_bazi_calc_branch_relation(
    uint8_t branch_a,
    uint8_t branch_b,
    uint32_t* out_flags,
    uint8_t* out_combined_element_id
) {
#ifndef TAIYIN_C_HAS_BAZI_EXTENSION
    (void)branch_a;
    (void)branch_b;
    (void)out_flags;
    (void)out_combined_element_id;
    return TAIYIN_ERROR_UNSUPPORTED;
#else
    return taiyin::bazi::calculate_branch_relation(
        branch_a, branch_b, out_flags, out_combined_element_id);
#endif
}

taiyin_status TAIYIN_C_CALL taiyin_bazi_calc_branch_triple_relation(
    uint8_t branch_a,
    uint8_t branch_b,
    uint8_t branch_c,
    uint32_t* out_flags,
    uint8_t* out_combined_element_id
) {
#ifndef TAIYIN_C_HAS_BAZI_EXTENSION
    (void)branch_a;
    (void)branch_b;
    (void)branch_c;
    (void)out_flags;
    (void)out_combined_element_id;
    return TAIYIN_ERROR_UNSUPPORTED;
#else
    return taiyin::bazi::calculate_branch_triple_relation(
        branch_a, branch_b, branch_c, out_flags, out_combined_element_id);
#endif
}

taiyin_status TAIYIN_C_CALL taiyin_bazi_get_life_stage(
    uint8_t stem_id,
    uint8_t branch_id,
    int32_t earth_palace_mode,
    uint8_t* out_life_stage_id
) {
#ifndef TAIYIN_C_HAS_BAZI_EXTENSION
    (void)stem_id;
    (void)branch_id;
    (void)earth_palace_mode;
    (void)out_life_stage_id;
    return TAIYIN_ERROR_UNSUPPORTED;
#else
    return taiyin::bazi::get_life_stage(
        stem_id, branch_id, earth_palace_mode, out_life_stage_id);
#endif
}

taiyin_status TAIYIN_C_CALL taiyin_bazi_calc_chart(
    const taiyin_bazi_context* context,
    const taiyin_ganzhi_four_pillars* pillars,
    taiyin_bazi_chart* out
) {
#ifndef TAIYIN_C_HAS_BAZI_EXTENSION
    (void)context;
    (void)pillars;
    (void)out;
    return TAIYIN_ERROR_UNSUPPORTED;
#else
    if (!context || !taiyin_c_internal::valid_struct(pillars)
        || !taiyin_c_internal::valid_struct(out)) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    taiyin::bazi::BaziChart cpp_out;
    const taiyin::Status status = taiyin::bazi::calculate_chart(
        &context->value,
        to_cpp_pillars(*pillars),
        &cpp_out);
    if (status == taiyin::TAIYIN_STATUS_OK) copy_chart(cpp_out, out);
    return status;
#endif
}

taiyin_status TAIYIN_C_CALL taiyin_bazi_calc_liunian(
    int32_t effective_year,
    taiyin_ganzhi* out
) {
#ifndef TAIYIN_C_HAS_BAZI_EXTENSION
    (void)effective_year;
    (void)out;
    return TAIYIN_ERROR_UNSUPPORTED;
#else
    return taiyin::bazi::calculate_flow_year(effective_year, out);
#endif
}

taiyin_status TAIYIN_C_CALL taiyin_bazi_calc_liuyue(
    taiyin_ganzhi year_pillar,
    uint8_t month_branch,
    taiyin_ganzhi* out
) {
#ifndef TAIYIN_C_HAS_BAZI_EXTENSION
    (void)year_pillar;
    (void)month_branch;
    (void)out;
    return TAIYIN_ERROR_UNSUPPORTED;
#else
    return taiyin::bazi::calculate_flow_month(year_pillar, month_branch, out);
#endif
}

taiyin_status TAIYIN_C_CALL taiyin_bazi_calc_liuri(
    const taiyin_calendar_datetime* civil_date,
    taiyin_ganzhi* out
) {
#ifndef TAIYIN_C_HAS_BAZI_EXTENSION
    (void)civil_date;
    (void)out;
    return TAIYIN_ERROR_UNSUPPORTED;
#else
    if (!taiyin_c_internal::valid_struct(civil_date) || !out) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    return taiyin::bazi::calculate_flow_day(
        taiyin_c_internal::to_cpp_datetime(*civil_date), out);
#endif
}

taiyin_status TAIYIN_C_CALL taiyin_bazi_calc_liushi(
    taiyin_ganzhi day_pillar,
    uint8_t hour_index,
    taiyin_ganzhi* out
) {
#ifndef TAIYIN_C_HAS_BAZI_EXTENSION
    (void)day_pillar;
    (void)hour_index;
    (void)out;
    return TAIYIN_ERROR_UNSUPPORTED;
#else
    return taiyin::bazi::calculate_flow_hour(day_pillar, hour_index, out);
#endif
}

taiyin_status TAIYIN_C_CALL taiyin_bazi_calc_xiaoyun(
    const taiyin_bazi_chart* chart,
    int32_t direction,
    int32_t age,
    taiyin_ganzhi* out
) {
#ifndef TAIYIN_C_HAS_BAZI_EXTENSION
    (void)chart;
    (void)direction;
    (void)age;
    (void)out;
    return TAIYIN_ERROR_UNSUPPORTED;
#else
    if (!taiyin_c_internal::valid_struct(chart) || !out) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    const taiyin::bazi::BaziChart cpp_chart = to_cpp_chart(*chart);
    return taiyin::bazi::calculate_xiaoyun(
        &cpp_chart, direction, age, out);
#endif
}

taiyin_status TAIYIN_C_CALL taiyin_bazi_fill_xiaoyun(
    const taiyin_bazi_chart* chart,
    int32_t direction,
    int32_t start_age,
    size_t requested_count,
    taiyin_bazi_xiaoyun* out,
    size_t capacity,
    size_t* out_count
) {
    if (out_count) *out_count = 0u;
#ifndef TAIYIN_C_HAS_BAZI_EXTENSION
    (void)chart;
    (void)direction;
    (void)start_age;
    (void)requested_count;
    (void)out;
    (void)capacity;
    return TAIYIN_ERROR_UNSUPPORTED;
#else
    if (!taiyin_c_internal::valid_struct(chart) || !out_count
        || (capacity != 0u && !out)) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    try {
        const taiyin::bazi::BaziChart cpp_chart = to_cpp_chart(*chart);
        size_t required_count = 0u;
        taiyin::Status status = taiyin::bazi::fill_xiaoyun(
            &cpp_chart,
            direction,
            start_age,
            requested_count,
            nullptr,
            0u,
            &required_count);
        *out_count = required_count;
        if (status != taiyin::TAIYIN_STATUS_OK || !out) return status;
        if (capacity < required_count) return TAIYIN_ERROR_OUT_OF_MEMORY;
        std::vector<taiyin::bazi::BaziXiaoYun> cpp_xiaoyun(required_count);
        status = taiyin::bazi::fill_xiaoyun(
            &cpp_chart,
            direction,
            start_age,
            requested_count,
            cpp_xiaoyun.empty() ? nullptr : cpp_xiaoyun.data(),
            cpp_xiaoyun.size(),
            out_count);
        if (status != taiyin::TAIYIN_STATUS_OK) return status;
        for (size_t i = 0; i < *out_count; ++i) {
            copy_xiaoyun(cpp_xiaoyun[i], &out[i]);
        }
        return TAIYIN_STATUS_OK;
    } catch (const std::bad_alloc&) {
        *out_count = 0u;
        return TAIYIN_ERROR_OUT_OF_MEMORY;
    } catch (...) {
        *out_count = 0u;
        return TAIYIN_ERROR_INTERNAL;
    }
#endif
}

taiyin_status TAIYIN_C_CALL taiyin_bazi_collect_chart_relations(
    const taiyin_bazi_chart* chart,
    uint32_t pillar_mask,
    uint32_t relation_mask,
    taiyin_bazi_relation* out,
    size_t capacity,
    size_t* out_count
) {
    if (out_count) *out_count = 0u;
#ifndef TAIYIN_C_HAS_BAZI_EXTENSION
    (void)chart;
    (void)pillar_mask;
    (void)relation_mask;
    (void)out;
    (void)capacity;
    return TAIYIN_ERROR_UNSUPPORTED;
#else
    if (!taiyin_c_internal::valid_struct(chart) || !out_count
        || (capacity != 0u && !out)) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    try {
        const taiyin::bazi::BaziChart cpp_chart = to_cpp_chart(*chart);
        size_t required_count = 0u;
        taiyin::Status status = taiyin::bazi::collect_chart_relations(
            &cpp_chart, pillar_mask, relation_mask,
            nullptr, 0u, &required_count);
        *out_count = required_count;
        if (status != taiyin::TAIYIN_STATUS_OK || !out) return status;
        if (capacity < required_count) return TAIYIN_ERROR_OUT_OF_MEMORY;

        std::vector<taiyin::bazi::BaziRelation> cpp_relations(required_count);
        status = taiyin::bazi::collect_chart_relations(
            &cpp_chart, pillar_mask, relation_mask,
            cpp_relations.empty() ? nullptr : cpp_relations.data(),
            cpp_relations.size(), out_count);
        if (status != taiyin::TAIYIN_STATUS_OK) return status;
        for (size_t i = 0; i < *out_count; ++i) {
            copy_relation(cpp_relations[i], &out[i]);
        }
        return TAIYIN_STATUS_OK;
    } catch (const std::bad_alloc&) {
        *out_count = 0u;
        return TAIYIN_ERROR_OUT_OF_MEMORY;
    } catch (...) {
        *out_count = 0u;
        return TAIYIN_ERROR_INTERNAL;
    }
#endif
}

taiyin_status TAIYIN_C_CALL taiyin_bazi_collect_target_shen_sha(
    const taiyin_bazi_chart* chart,
    taiyin_ganzhi target_ganzhi,
    int32_t target_kind,
    uint64_t* out_words,
    size_t word_capacity,
    size_t* out_word_count
) {
    if (out_word_count) *out_word_count = 0u;
#ifndef TAIYIN_C_HAS_BAZI_EXTENSION
    (void)chart;
    (void)target_ganzhi;
    (void)target_kind;
    (void)out_words;
    (void)word_capacity;
    return TAIYIN_ERROR_UNSUPPORTED;
#else
    if (!taiyin_c_internal::valid_struct(chart) || !out_word_count
        || (word_capacity != 0u && !out_words)) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    const taiyin::bazi::BaziChart cpp_chart = to_cpp_chart(*chart);
    return taiyin::bazi::collect_target_shen_sha(
        &cpp_chart,
        target_ganzhi,
        target_kind,
        out_words,
        word_capacity,
        out_word_count);
#endif
}

taiyin_status TAIYIN_C_CALL
taiyin_bazi_collect_target_shen_sha_with_gender(
    const taiyin_bazi_chart* chart,
    taiyin_ganzhi target_ganzhi,
    int32_t target_kind,
    int32_t gender,
    uint64_t* out_words,
    size_t word_capacity,
    size_t* out_word_count
) {
    if (out_word_count) *out_word_count = 0u;
#ifndef TAIYIN_C_HAS_BAZI_EXTENSION
    (void)chart;
    (void)target_ganzhi;
    (void)target_kind;
    (void)gender;
    (void)out_words;
    (void)word_capacity;
    return TAIYIN_ERROR_UNSUPPORTED;
#else
    if (!taiyin_c_internal::valid_struct(chart) || !out_word_count
        || (word_capacity != 0u && !out_words)) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    const taiyin::bazi::BaziChart cpp_chart = to_cpp_chart(*chart);
    return taiyin::bazi::collect_target_shen_sha_with_gender(
        &cpp_chart,
        target_ganzhi,
        target_kind,
        gender,
        out_words,
        word_capacity,
        out_word_count);
#endif
}

taiyin_status TAIYIN_C_CALL taiyin_bazi_calc_qiyun(
    const taiyin_bazi_context* context,
    const taiyin_chinese_calendar_context* calendar_context,
    const taiyin_split_julian_date* birth_jd_ut,
    const taiyin_calendar_datetime* birth_civil_time,
    const taiyin_bazi_chart* chart,
    int32_t gender,
    taiyin_bazi_qiyun_result* out,
    taiyin_ephemeris_diagnostic* diagnostic
) {
#ifndef TAIYIN_C_HAS_BAZI_EXTENSION
    (void)context;
    (void)calendar_context;
    (void)birth_jd_ut;
    (void)birth_civil_time;
    (void)chart;
    (void)gender;
    (void)out;
    (void)diagnostic;
    return TAIYIN_ERROR_UNSUPPORTED;
#else
    if (!context || !calendar_context
        || !taiyin_c_internal::valid_split_jd(birth_jd_ut)
        || !taiyin_c_internal::valid_struct(birth_civil_time)
        || !taiyin_c_internal::valid_struct(chart)
        || !taiyin_c_internal::valid_struct(out)
        || (diagnostic && !taiyin_c_internal::valid_struct(diagnostic))) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    taiyin::bazi::BaziQiYunResult cpp_out;
    const taiyin::bazi::BaziChart cpp_chart = to_cpp_chart(*chart);
    taiyin::runtime::EphemerisEvalDiagnostic cpp_diagnostic;
    const taiyin::Status status = taiyin::bazi::calculate_qiyun(
        &context->value,
        &calendar_context->value,
        taiyin_c_internal::to_cpp_split_jd(*birth_jd_ut),
        taiyin_c_internal::to_cpp_datetime(*birth_civil_time),
        &cpp_chart,
        gender,
        &cpp_out,
        diagnostic ? &cpp_diagnostic : nullptr);
    if (diagnostic) taiyin_c_internal::from_cpp_diagnostic(cpp_diagnostic, diagnostic);
    if (status == taiyin::TAIYIN_STATUS_OK) copy_qiyun(cpp_out, out);
    return status;
#endif
}

taiyin_status TAIYIN_C_CALL taiyin_bazi_fill_dayun(
    const taiyin_bazi_context* context,
    const taiyin_calendar_datetime* birth_civil_time,
    const taiyin_bazi_chart* chart,
    const taiyin_bazi_qiyun_result* qiyun,
    size_t requested_count,
    taiyin_bazi_dayun* out,
    size_t capacity,
    size_t* out_count
) {
    if (out_count) *out_count = 0u;
#ifndef TAIYIN_C_HAS_BAZI_EXTENSION
    (void)context;
    (void)birth_civil_time;
    (void)chart;
    (void)qiyun;
    (void)requested_count;
    (void)out;
    (void)capacity;
    return TAIYIN_ERROR_UNSUPPORTED;
#else
    if (!context || !taiyin_c_internal::valid_struct(birth_civil_time)
        || !taiyin_c_internal::valid_struct(chart)
        || !taiyin_c_internal::valid_struct(qiyun) || !out_count
        || (capacity != 0u && !out)) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    try {
        const taiyin::bazi::BaziChart cpp_chart = to_cpp_chart(*chart);
        const taiyin::bazi::BaziQiYunResult cpp_qiyun = to_cpp_qiyun(*qiyun);
        size_t required_count = 0u;
        taiyin::Status status = taiyin::bazi::fill_dayun(
            &context->value,
            taiyin_c_internal::to_cpp_datetime(*birth_civil_time),
            &cpp_chart,
            &cpp_qiyun,
            requested_count,
            nullptr,
            0u,
            &required_count);
        *out_count = required_count;
        if (status != taiyin::TAIYIN_STATUS_OK || !out) return status;
        if (capacity < required_count) return TAIYIN_ERROR_OUT_OF_MEMORY;
        std::vector<taiyin::bazi::BaziDaYun> cpp_dayun(required_count);
        status = taiyin::bazi::fill_dayun(
            &context->value,
            taiyin_c_internal::to_cpp_datetime(*birth_civil_time),
            &cpp_chart,
            &cpp_qiyun,
            requested_count,
            cpp_dayun.empty() ? nullptr : cpp_dayun.data(),
            cpp_dayun.size(),
            out_count);
        if (status != taiyin::TAIYIN_STATUS_OK) return status;
        for (size_t i = 0; i < *out_count; ++i) copy_dayun(cpp_dayun[i], &out[i]);
        return TAIYIN_STATUS_OK;
    } catch (const std::bad_alloc&) {
        *out_count = 0u;
        return TAIYIN_ERROR_OUT_OF_MEMORY;
    } catch (...) {
        *out_count = 0u;
        return TAIYIN_ERROR_INTERNAL;
    }
#endif
}

taiyin_status TAIYIN_C_CALL taiyin_bazi_get_renyuan_siling_segments(
    uint8_t month_branch_id,
    int32_t table_model,
    taiyin_bazi_renyuan_siling_segment* out,
    size_t capacity,
    size_t* out_count
) {
    if (out_count) *out_count = 0u;
#ifndef TAIYIN_C_HAS_BAZI_EXTENSION
    (void)month_branch_id;
    (void)table_model;
    (void)out;
    (void)capacity;
    return TAIYIN_ERROR_UNSUPPORTED;
#else
    if (!out_count || (capacity != 0u && !out) || month_branch_id >= 12u) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    taiyin::bazi::BaziRenyuanSilingSegment cpp_segments[
        taiyin::bazi::kRenyuanSilingMaxSegments];
    const taiyin::Status status = taiyin::bazi::get_renyuan_siling_segments(
        month_branch_id,
        table_model,
        out ? cpp_segments : nullptr,
        out ? taiyin::bazi::kRenyuanSilingMaxSegments : 0u,
        out_count);
    if (status != taiyin::TAIYIN_STATUS_OK || !out) return status;
    if (capacity < *out_count) return TAIYIN_ERROR_OUT_OF_MEMORY;
    for (size_t i = 0; i < *out_count; ++i) {
        copy_siling_segment(cpp_segments[i], &out[i]);
    }
    return TAIYIN_STATUS_OK;
#endif
}

taiyin_status TAIYIN_C_CALL taiyin_bazi_calc_renyuan_siling(
    const taiyin_chinese_calendar_context* calendar_context,
    const taiyin_split_julian_date* instant_jd_ut,
    const taiyin_bazi_chart* chart,
    int32_t table_model,
    int32_t time_model,
    taiyin_bazi_renyuan_siling_result* out,
    taiyin_ephemeris_diagnostic* diagnostic
) {
#ifndef TAIYIN_C_HAS_BAZI_EXTENSION
    (void)calendar_context;
    (void)instant_jd_ut;
    (void)chart;
    (void)table_model;
    (void)time_model;
    (void)out;
    (void)diagnostic;
    return TAIYIN_ERROR_UNSUPPORTED;
#else
    if (!calendar_context || !taiyin_c_internal::valid_split_jd(instant_jd_ut)
        || !taiyin_c_internal::valid_struct(chart)
        || !taiyin_c_internal::valid_struct(out)
        || (diagnostic && !taiyin_c_internal::valid_struct(diagnostic))) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    const taiyin::bazi::BaziChart cpp_chart = to_cpp_chart(*chart);
    taiyin::bazi::BaziRenyuanSilingResult cpp_out;
    taiyin::runtime::EphemerisEvalDiagnostic cpp_diagnostic;
    const taiyin::Status status = taiyin::bazi::calculate_renyuan_siling(
        &calendar_context->value,
        taiyin_c_internal::to_cpp_split_jd(*instant_jd_ut),
        &cpp_chart,
        table_model,
        time_model,
        &cpp_out,
        diagnostic ? &cpp_diagnostic : nullptr);
    if (diagnostic) {
        taiyin_c_internal::from_cpp_diagnostic(cpp_diagnostic, diagnostic);
    }
    if (status == taiyin::TAIYIN_STATUS_OK) copy_siling_result(cpp_out, out);
    return status;
#endif
}

}  // extern "C"
