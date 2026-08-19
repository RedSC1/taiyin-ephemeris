#include "taiyin/c/bazi.h"
#include "taiyin/c/chinese_calendar.h"
#include "taiyin/c/position.h"
#include "taiyin/c/runtime.h"
#include "taiyin/c/time.h"

#include <math.h>
#include <stdio.h>

static int fail(const char* message) {
    fprintf(stderr, "test_bazi_c_api: %s\n", message);
    return 1;
}

int main(int argc, char** argv) {
    taiyin_bazi_context* context = NULL;
    taiyin_bazi_context_config config;
    taiyin_ganzhi_four_pillars pillars;
    taiyin_bazi_chart chart;

    taiyin_context* native_context = NULL;
    taiyin_chinese_calendar_context* calendar_context = NULL;
    taiyin_chinese_calendar_config calendar_config;
    taiyin_runtime_config runtime_config;
    char source_path[2048];
    const char* source_paths[1];

    if (argc != 2
        || snprintf(
            source_path,
            sizeof(source_path),
            "%s/ephemerides/opm2/major-bodies/600y",
            argv[1]) < 0) {
        return fail("missing or invalid data root");
    }
    source_paths[0] = source_path;
    taiyin_runtime_config_init(&runtime_config);
    runtime_config.source_paths = source_paths;
    runtime_config.source_path_count = 1;
    runtime_config.load_packaged_data = 0u;
    taiyin_chinese_calendar_config_init_utc_offset(&calendar_config, 8 * 60);
    if (taiyin_runtime_initialize(&runtime_config) < 0
        || taiyin_context_create(&native_context) < 0
        || taiyin_context_set_geocentric_observer(
            native_context, TAIYIN_BODY_EARTH, TAIYIN_BODY_EARTH)
            < 0
        || taiyin_chinese_calendar_context_create(
            native_context, &calendar_config, &calendar_context)
            < 0) {
        taiyin_chinese_calendar_context_destroy(calendar_context);
        taiyin_context_destroy(native_context);
        return fail("initialize BaZi C ABI integration context");
    }

#ifndef TAIYIN_TEST_MODULAR_C_API
    if ((taiyin_get_capabilities() & TAIYIN_CAPABILITY_BAZI) == 0u) {
        return fail("BaZi capability is not advertised");
    }
#endif

    taiyin_bazi_context_config_init(&config);
    if (config.qiyun_time_model != TAIYIN_BAZI_QIYUN_TRADITIONAL_CALENDAR
        || config.dayun_boundary_model != TAIYIN_BAZI_DAYUN_CIVIL_YEARS) {
        return fail("BaZi fortune profile defaults");
    }
    if (taiyin_bazi_context_create(&config, &context) < 0
        || context == NULL) {
        return fail("create BaZi context");
    }

    taiyin_ganzhi_four_pillars_init(&pillars);
    pillars.year = 0x26u;
    pillars.month = 0x62u;
    pillars.day = 0x42u;
    pillars.hour = 0x35u;
    taiyin_bazi_chart_init(&chart);
    if (taiyin_bazi_calc_chart(context, &pillars, &chart) < 0
        || chart.year_pillar != pillars.year
        || chart.month_pillar != pillars.month
        || chart.day_pillar != pillars.day
        || chart.hour_pillar != pillars.hour
        || chart.ming_gong != 0x4au
        || chart.shen_gong != 0x28u
        || chart.tai_yuan != 0x75u
        || chart.tai_xi != 0x9bu
        || chart.nayin_ids[0] != 21u
        || chart.nayin_ids[1] != 13u
        || chart.nayin_ids[2] != 7u
        || chart.nayin_ids[3] != 26u) {
        taiyin_bazi_context_destroy(context);
        return fail("interpret precomputed four pillars");
    }

    {
        uint64_t words[TAIYIN_BAZI_SHEN_SHA_WORD_COUNT] = {0u, 0u};
        uint64_t short_word = 0u;
        size_t word_count = 0u;
        if (taiyin_bazi_collect_target_shen_sha(
                &chart, chart.day_pillar, TAIYIN_BAZI_SHEN_SHA_TARGET_DAY,
                NULL, 0u, &word_count) < 0
            || word_count != TAIYIN_BAZI_SHEN_SHA_WORD_COUNT
            || taiyin_call_result_status(taiyin_bazi_collect_target_shen_sha(
                &chart, chart.day_pillar, TAIYIN_BAZI_SHEN_SHA_TARGET_DAY,
                &short_word, 1u, &word_count)) != TAIYIN_ERROR_OUT_OF_MEMORY
            || short_word != 0u
            || word_count != TAIYIN_BAZI_SHEN_SHA_WORD_COUNT
            || taiyin_bazi_collect_target_shen_sha(
                &chart, chart.day_pillar, TAIYIN_BAZI_SHEN_SHA_TARGET_DAY,
                words, TAIYIN_BAZI_SHEN_SHA_WORD_COUNT, &word_count)
                < 0
            || (words[TAIYIN_BAZI_SHEN_SHA_TIAN_SHE_DAY / 64u]
                & (UINT64_C(1) << (TAIYIN_BAZI_SHEN_SHA_TIAN_SHE_DAY % 64u))) == 0u
            || taiyin_call_result_status(taiyin_bazi_collect_target_shen_sha(
                &chart, chart.day_pillar, -1,
                words, TAIYIN_BAZI_SHEN_SHA_WORD_COUNT, &word_count))
                != TAIYIN_ERROR_INVALID_ARGUMENT) {
            taiyin_bazi_context_destroy(context);
            return fail("BaZi Shen Sha bitset C ABI");
        }
    }

    {
        uint64_t words[TAIYIN_BAZI_SHEN_SHA_WORD_COUNT] = {0u, 0u};
        size_t word_count = 0u;
        taiyin_bazi_chart invalid_hour_chart = chart;
        invalid_hour_chart.hour_pillar = 0x30u; /* Ding-Zi: invalid parity. */
        if (taiyin_bazi_collect_target_shen_sha_with_gender(
                &chart, 0x19u, TAIYIN_BAZI_SHEN_SHA_TARGET_YEAR,
                TAIYIN_BAZI_GENDER_MALE, words,
                TAIYIN_BAZI_SHEN_SHA_WORD_COUNT, &word_count)
                < 0
            || (words[TAIYIN_BAZI_SHEN_SHA_GOU_SHA / 64u]
                & (UINT64_C(1) << (TAIYIN_BAZI_SHEN_SHA_GOU_SHA % 64u))) == 0u
            || taiyin_call_result_status(taiyin_bazi_collect_target_shen_sha_with_gender(
                &chart, 0x19u, TAIYIN_BAZI_SHEN_SHA_TARGET_YEAR, 2,
                words, TAIYIN_BAZI_SHEN_SHA_WORD_COUNT, &word_count))
                != TAIYIN_ERROR_INVALID_ARGUMENT
            || taiyin_call_result_status(taiyin_bazi_collect_target_shen_sha_with_gender(
                &invalid_hour_chart, 0x19u, TAIYIN_BAZI_SHEN_SHA_TARGET_YEAR,
                TAIYIN_BAZI_GENDER_MALE, words,
                TAIYIN_BAZI_SHEN_SHA_WORD_COUNT, &word_count))
                != TAIYIN_ERROR_INVALID_ARGUMENT) {
            taiyin_bazi_context_destroy(context);
            return fail("gender-aware BaZi Shen Sha C ABI");
        }
    }

    {
        taiyin_bazi_renyuan_siling_segment segments[3];
        taiyin_bazi_renyuan_siling_result result;
        size_t segment_count = 0;
        taiyin_bazi_renyuan_siling_segment_init(&segments[0]);
        taiyin_bazi_renyuan_siling_result_init(&result);
        if (segments[0].struct_size != sizeof(segments[0])
            || segments[0].stem_id != TAIYIN_GANZHI_INVALID
            || result.struct_size != sizeof(result)
            || result.stem_id != TAIYIN_GANZHI_INVALID
            || taiyin_bazi_get_renyuan_siling_segments(
                2u, TAIYIN_BAZI_RENYUAN_SILING_SAN_MING_TONG_HUI,
                NULL, 0, &segment_count) < 0
            || segment_count != 3u
            || taiyin_call_result_status(taiyin_bazi_get_renyuan_siling_segments(
                2u, TAIYIN_BAZI_RENYUAN_SILING_SAN_MING_TONG_HUI,
                segments, 2, &segment_count)) != TAIYIN_ERROR_OUT_OF_MEMORY
            || segment_count != 3u
            || taiyin_bazi_get_renyuan_siling_segments(
                2u, TAIYIN_BAZI_RENYUAN_SILING_SAN_MING_TONG_HUI,
                segments, 3, &segment_count) < 0
            || segments[0].stem_id != 4u
            || segments[0].origin_kind
                != TAIYIN_BAZI_RENYUAN_SILING_ORIGIN_GEN_EARTH
            || segments[0].start_day != 0.0 || segments[0].end_day != 5.0
            || segments[1].stem_id != 2u
            || segments[1].start_day != 5.0 || segments[1].end_day != 10.0
            || segments[2].stem_id != 0u
            || segments[2].start_day != 10.0 || segments[2].end_day != 30.0
            || taiyin_call_result_status(taiyin_bazi_get_renyuan_siling_segments(
                12u, TAIYIN_BAZI_RENYUAN_SILING_COMMON,
                NULL, 0, &segment_count)) != TAIYIN_ERROR_INVALID_ARGUMENT) {
            taiyin_bazi_context_destroy(context);
            return fail("Renyuan Siling C ABI table and initializers");
        }
    }

    {
        uint32_t flags = 0;
        uint8_t element = TAIYIN_BAZI_INVALID_WUXING;
        uint8_t branches[2] = {TAIYIN_GANZHI_INVALID, TAIYIN_GANZHI_INVALID};
        if (taiyin_bazi_calc_stem_relation(0, 5, &flags, &element)
                < 0
            || (flags & TAIYIN_BAZI_STEM_RELATION_COMBINATION) == 0u
            || element != TAIYIN_BAZI_WUXING_EARTH
            || taiyin_bazi_get_kong_wang(0x00u, branches) < 0
            || branches[0] != 10u || branches[1] != 11u) {
            taiyin_bazi_context_destroy(context);
            return fail("BaZi rules");
        }
    }

    {
        taiyin_bazi_relation relation;
        taiyin_bazi_relation relations[32];
        size_t relation_count = 0;
        size_t i = 0;
        int found_triple = 0;
        taiyin_bazi_relation_init(&relation);
        chart.year_pillar = 0x68u;
        chart.month_pillar = 0x20u;
        chart.day_pillar = 0x44u;
        chart.hour_pillar = 0x55u;
        if (taiyin_bazi_collect_chart_relations(
                &chart, TAIYIN_BAZI_RELATION_PILLAR_PRIMARY,
                TAIYIN_BAZI_RELATION_KIND_MASK_ALL, NULL, 0, &relation_count)
                < 0
            || relation_count == 0
            || taiyin_call_result_status(taiyin_bazi_collect_chart_relations(
                &chart, TAIYIN_BAZI_RELATION_PILLAR_PRIMARY,
                TAIYIN_BAZI_RELATION_KIND_MASK_ALL, &relation, 1, &relation_count))
                != TAIYIN_ERROR_OUT_OF_MEMORY
            || relation_count <= 1
            || relation.struct_size != sizeof(relation)
            || taiyin_call_result_status(taiyin_bazi_collect_chart_relations(
                &chart, 0, TAIYIN_BAZI_RELATION_KIND_MASK_ALL, NULL, 0, &relation_count))
                != TAIYIN_ERROR_INVALID_ARGUMENT
            || taiyin_call_result_status(taiyin_bazi_collect_chart_relations(
                &chart, 0x100u, TAIYIN_BAZI_RELATION_KIND_MASK_ALL,
                NULL, 0, &relation_count))
                != TAIYIN_ERROR_INVALID_ARGUMENT
            || taiyin_call_result_status(taiyin_bazi_collect_chart_relations(
                &chart, TAIYIN_BAZI_RELATION_PILLAR_PRIMARY, 0x10000u,
                NULL, 0, &relation_count))
                != TAIYIN_ERROR_INVALID_ARGUMENT) {
            taiyin_bazi_context_destroy(context);
            return fail("BaZi aggregate relation C ABI");
        }
        if (relation_count > 32
            || taiyin_bazi_collect_chart_relations(
                &chart, TAIYIN_BAZI_RELATION_PILLAR_PRIMARY,
                TAIYIN_BAZI_RELATION_KIND_MASK_ALL, relations, 32, &relation_count)
                < 0) {
            taiyin_bazi_context_destroy(context);
            return fail("copy BaZi aggregate relation C ABI");
        }
        for (i = 0; i < relation_count; ++i) {
            if (relations[i].kind == TAIYIN_BAZI_RELATION_BRANCH_TRIPLE_COMBINATION
                && relations[i].pillar_mask == (TAIYIN_BAZI_RELATION_PILLAR_YEAR
                    | TAIYIN_BAZI_RELATION_PILLAR_MONTH
                    | TAIYIN_BAZI_RELATION_PILLAR_DAY)
                && relations[i].combined_element_id == TAIYIN_BAZI_WUXING_WATER
                && relations[i].struct_size == sizeof(taiyin_bazi_relation)) {
                found_triple = 1;
            }
        }
        if (!found_triple) {
            taiyin_bazi_context_destroy(context);
            return fail("copy Shen-Zi-Chen triple combination through C ABI");
        }
    }

    {
        taiyin_calendar_datetime birth_civil;
        taiyin_split_julian_date birth_local_jd;
        taiyin_split_julian_date birth_jd_ut;
        taiyin_ganzhi_four_pillars birth_pillars;
        taiyin_bazi_chart birth_chart;
        taiyin_bazi_qiyun_result qiyun;
        taiyin_bazi_dayun dayun[2];
        taiyin_chinese_solar_term_event previous_jie;
        taiyin_split_julian_date siling_boundary_jd;
        taiyin_bazi_renyuan_siling_result siling;
        size_t dayun_count = 0;

        taiyin_calendar_datetime_init(&birth_civil);
        taiyin_ganzhi_four_pillars_init(&birth_pillars);
        taiyin_bazi_chart_init(&birth_chart);
        taiyin_bazi_qiyun_result_init(&qiyun);
        taiyin_bazi_dayun_init(&dayun[0]);
        taiyin_bazi_dayun_init(&dayun[1]);
        taiyin_chinese_solar_term_event_init(&previous_jie);
        taiyin_bazi_renyuan_siling_result_init(&siling);
        birth_civil.year = 2026;
        birth_civil.month = 2;
        birth_civil.day = 19;
        birth_civil.hour = 23;
        birth_civil.minute = 28;
        if (taiyin_julian_day_split(&birth_civil, &birth_local_jd)
                < 0
            || taiyin_add_seconds_to_split_jd(
                &birth_local_jd, -8.0 * 3600.0, &birth_jd_ut)
                < 0
            || taiyin_chinese_calendar_calc_four_pillars_ut(
                calendar_context,
                &birth_jd_ut,
                &birth_civil,
                TAIYIN_GANZHI_RAT_HOUR_NO_SPLIT,
                &birth_pillars,
                NULL) < 0
            || taiyin_bazi_calc_chart(context, &birth_pillars, &birth_chart)
                < 0
            || taiyin_chinese_calendar_get_prev_jie_ut(
                calendar_context, &birth_jd_ut, &previous_jie, NULL)
                < 0
            || taiyin_add_seconds_to_split_jd(
                &previous_jie.jd_ut, 5.0 * 86400.0, &siling_boundary_jd)
                < 0
            || taiyin_bazi_calc_renyuan_siling(
                calendar_context,
                &siling_boundary_jd,
                &birth_chart,
                TAIYIN_BAZI_RENYUAN_SILING_SAN_MING_TONG_HUI,
                TAIYIN_BAZI_RENYUAN_SILING_ELAPSED_24_HOURS,
                &siling,
                NULL) < 0
            || siling.struct_size != sizeof(siling)
            || siling.month_branch_id != 2u
            || siling.stem_id != 2u
            || siling.origin_kind != TAIYIN_BAZI_RENYUAN_SILING_ORIGIN_STEM
            || siling.segment_index != 1u
            || siling.segment_start_day != 5.0
            || taiyin_call_result_status(taiyin_bazi_calc_renyuan_siling(
                calendar_context,
                &siling_boundary_jd,
                &birth_chart,
                99,
                TAIYIN_BAZI_RENYUAN_SILING_ELAPSED_24_HOURS,
                &siling,
                NULL)) != TAIYIN_ERROR_INVALID_ARGUMENT
            || taiyin_bazi_calc_qiyun(
                context,
                calendar_context,
                &birth_jd_ut,
                &birth_civil,
                &birth_chart,
                TAIYIN_BAZI_GENDER_MALE,
                &qiyun,
                NULL) < 0
            || qiyun.struct_size != sizeof(qiyun)
            || qiyun.direction != 1
            || qiyun.start_civil_time.year != 2030
            || qiyun.start_civil_time.month != 10
            || qiyun.start_civil_time.day != 12
            || qiyun.offset_years != 4
            || qiyun.offset_months != 7
            || qiyun.offset_days != 22
            || fabs(
                qiyun.offset_years * 360.0
                + qiyun.offset_months * 30.0
                + qiyun.offset_days
                + (qiyun.offset_hours * 3600.0
                    + qiyun.offset_minutes * 60.0
                    + qiyun.offset_seconds) / 86400.0
                - qiyun.jie_interval_days * 120.0) > 1.0e-10
            || taiyin_bazi_fill_dayun(
                context,
                &birth_civil,
                &birth_chart,
                &qiyun,
                2,
                NULL,
                0,
                &dayun_count) < 0
            || dayun_count != 2
            || taiyin_bazi_fill_dayun(
                context,
                &birth_civil,
                &birth_chart,
                &qiyun,
                2,
                dayun,
                2,
                &dayun_count) < 0
            || dayun[0].struct_size != sizeof(dayun[0])
            || dayun[0].ganzhi != 0x73u
            || dayun[1].ganzhi != 0x84u
            || dayun[0].start_virtual_age != 5
            || dayun[0].end_virtual_age != 14) {
            taiyin_bazi_context_destroy(context);
            taiyin_chinese_calendar_context_destroy(calendar_context);
            taiyin_context_destroy(native_context);
            return fail("BaZi qi-yun and da-yun C ABI");
        }

        {
            taiyin_calendar_datetime flow_date;
            taiyin_ganzhi flow_year = TAIYIN_GANZHI_INVALID;
            taiyin_ganzhi flow_month = TAIYIN_GANZHI_INVALID;
            taiyin_ganzhi flow_day = TAIYIN_GANZHI_INVALID;
            taiyin_ganzhi flow_hour = TAIYIN_GANZHI_INVALID;
            taiyin_ganzhi xiao_yun = TAIYIN_GANZHI_INVALID;
            taiyin_ganzhi expected_xiao_yun = TAIYIN_GANZHI_INVALID;
            taiyin_bazi_xiaoyun xiao_entries[3];
            size_t xiao_count = 0u;
            taiyin_calendar_datetime_init(&flow_date);
            taiyin_bazi_xiaoyun_init(&xiao_entries[0]);
            taiyin_bazi_xiaoyun_init(&xiao_entries[1]);
            taiyin_bazi_xiaoyun_init(&xiao_entries[2]);
            flow_date.year = 2000;
            flow_date.month = 1;
            flow_date.day = 1;
            flow_date.hour = 23;
            flow_date.minute = 59;
            if (taiyin_bazi_calc_liunian(2024, &flow_year) < 0
                || flow_year != 0x04u
                || taiyin_bazi_calc_liuyue(flow_year, 2u, &flow_month)
                    < 0
                || flow_month != 0x22u
                || taiyin_bazi_calc_liuri(&flow_date, &flow_day)
                    < 0
                || flow_day != 0x46u
                || taiyin_bazi_calc_liushi(flow_day, 0u, &flow_hour)
                    < 0
                || flow_hour != 0x80u
                || taiyin_bazi_calc_xiaoyun(
                    &birth_chart, -1, 7, &xiao_yun) < 0
                || taiyin_ganzhi_advance(
                    birth_chart.hour_pillar, -7, &expected_xiao_yun)
                    < 0
                || xiao_yun != expected_xiao_yun
                || taiyin_call_result_status(taiyin_bazi_calc_liushi(flow_day, 12u, &flow_hour))
                    != TAIYIN_ERROR_INVALID_ARGUMENT
                || taiyin_call_result_status(taiyin_bazi_calc_xiaoyun(
                    &birth_chart, 0, 1, &xiao_yun))
                    != TAIYIN_ERROR_INVALID_ARGUMENT
                || taiyin_bazi_fill_xiaoyun(
                    &birth_chart, 1, 1, 3, NULL, 0, &xiao_count)
                    < 0
                || xiao_count != 3u
                || taiyin_bazi_fill_xiaoyun(
                    &birth_chart, -1, 1, 3,
                    xiao_entries, 3, &xiao_count)
                    < 0
                || xiao_count != 3u
                || xiao_entries[0].struct_size != sizeof(xiao_entries[0])
                || xiao_entries[0].age != 1u
                || xiao_entries[2].age != 3u
                || xiao_entries[0].ganzhi == TAIYIN_GANZHI_INVALID) {
                taiyin_bazi_context_destroy(context);
                taiyin_chinese_calendar_context_destroy(calendar_context);
                taiyin_context_destroy(native_context);
                return fail("BaZi flow primitive C ABI");
            }
        }
        --birth_chart.struct_size;
        if (taiyin_call_result_status(taiyin_bazi_calc_renyuan_siling(
                calendar_context,
                &siling_boundary_jd,
                &birth_chart,
                TAIYIN_BAZI_RENYUAN_SILING_SAN_MING_TONG_HUI,
                TAIYIN_BAZI_RENYUAN_SILING_ELAPSED_24_HOURS,
                &siling,
                NULL)) != TAIYIN_ERROR_INVALID_ARGUMENT) {
            taiyin_bazi_context_destroy(context);
            taiyin_chinese_calendar_context_destroy(calendar_context);
            taiyin_context_destroy(native_context);
            return fail("reject an invalid chart in Renyuan Siling C ABI");
        }
    }

    taiyin_bazi_context_destroy(context);
    taiyin_chinese_calendar_context_destroy(calendar_context);
    taiyin_context_destroy(native_context);
    return 0;
}
