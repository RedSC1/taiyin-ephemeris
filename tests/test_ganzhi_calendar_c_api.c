#include "taiyin/c/taiyin.h"

#include <stdio.h>

static int fail(const char* message) {
    fprintf(stderr, "test_ganzhi_calendar_c_api: %s\n", message);
    return 1;
}

int main(int argc, char** argv) {
    taiyin_ganzhi value = TAIYIN_GANZHI_INVALID;
    if (taiyin_ganzhi_make(4, 6, &value) < 0
        || value != 0x46u
        || taiyin_ganzhi_advance(value, 1, &value) < 0
        || value != 0x57u
        || taiyin_ganzhi_get_month(1, 11, &value) < 0
        || value != 0x51u
        || taiyin_ganzhi_get_hour(4, 0, &value) < 0
        || value != 0x80u
        || taiyin_ganzhi_get_nayin_id(0x00u, &value) < 0
        || value != 0u
        || taiyin_ganzhi_get_nayin_element(0x00u, &value) < 0
        || value != TAIYIN_GANZHI_WUXING_METAL) {
        return fail("enabled Ganzhi rules failed");
    }

    {
        taiyin_calendar_datetime date;
        taiyin_calendar_datetime_init(&date);
        date.year = 2000;
        date.month = 1;
        date.day = 1;
        date.hour = 23;
        date.minute = 59;
        if (taiyin_ganzhi_calc_day_pillar(&date, &value) < 0
            || value != 0x46u) {
            return fail("calculate a day pillar through the C ABI");
        }
    }

    {
        taiyin_runtime_config runtime_config;
        taiyin_context* astronomy = NULL;
        taiyin_chinese_calendar_config calendar_config;
        taiyin_chinese_calendar_context* calendar = NULL;
        taiyin_calendar_datetime clock;
        taiyin_split_julian_date instant;
        taiyin_ganzhi_four_pillars pillars;
        static char source_path[2048];
        static const char* source_paths[1];
        if (argc != 2
            || snprintf(source_path, sizeof(source_path),
                "%s/ephemerides/opm2/major-bodies/600y", argv[1]) < 0) {
            return fail("missing or invalid data root");
        }
        source_paths[0] = source_path;
        taiyin_runtime_config_init(&runtime_config);
        runtime_config.source_paths = source_paths;
        runtime_config.source_path_count = 1;
        runtime_config.load_packaged_data = 0u;
        if (taiyin_runtime_initialize(&runtime_config) < 0
            || taiyin_context_create(&astronomy) < 0
            || taiyin_context_set_geocentric_observer(
                   astronomy, TAIYIN_BODY_EARTH, TAIYIN_BODY_EARTH)
                < 0) {
            taiyin_context_destroy(astronomy);
            return fail("initialize calendar astronomy");
        }
        taiyin_chinese_calendar_config_init_utc_offset(&calendar_config, 8 * 60);
        if (taiyin_chinese_calendar_context_create(
                astronomy, &calendar_config, &calendar) < 0) {
            taiyin_context_destroy(astronomy);
            return fail("create Chinese calendar context");
        }
        taiyin_calendar_datetime_init(&clock);
        clock.year = 2026;
        clock.month = 3;
        clock.day = 5;
        clock.hour = 10;
        clock.minute = 4;
        taiyin_ganzhi_four_pillars_init(&pillars);
        if (taiyin_julian_day_split(&clock, &instant) < 0
            || taiyin_add_seconds_to_split_jd(
                   &instant, -8.0 * 3600.0, &instant) < 0
            || taiyin_chinese_calendar_calc_four_pillars_ut(
                   calendar,
                   &instant,
                   &clock,
                   TAIYIN_GANZHI_RAT_HOUR_NO_SPLIT,
                   &pillars,
                   NULL) < 0
            || pillars.year != 0x26u || pillars.month != 0x62u
            || pillars.day != 0x42u || pillars.hour != 0x35u) {
            taiyin_chinese_calendar_context_destroy(calendar);
            taiyin_context_destroy(astronomy);
            return fail("calculate four pillars through C ABI");
        }
        taiyin_chinese_calendar_context_destroy(calendar);
        taiyin_context_destroy(astronomy);
    }

    {
        /* End-to-end call-result flag checks. */
        taiyin_context* astronomy = NULL;
        taiyin_chinese_calendar_config config;
        taiyin_chinese_calendar_context* historical = NULL;
        taiyin_chinese_calendar_context* astronomical = NULL;
        taiyin_chinese_calendar_context* pillar_on = NULL;
        taiyin_chinese_calendar_context* pillar_off = NULL;
        taiyin_calendar_datetime clock;
        taiyin_split_julian_date instant;
        taiyin_split_julian_date local;
        taiyin_lunar_date lunar;
        taiyin_ganzhi_four_pillars pillars_on;
        taiyin_ganzhi_four_pillars pillars_off;
        taiyin_chinese_solar_term_event jie;
        taiyin_precise_time_scales scales;
        taiyin_call_result result;
        if (taiyin_context_create(&astronomy) < 0
            || taiyin_context_set_geocentric_observer(
                   astronomy, TAIYIN_BODY_EARTH, TAIYIN_BODY_EARTH)
                < 0) {
            taiyin_context_destroy(astronomy);
            return fail("create flag-test astronomy context");
        }

        /* 1950 is inside the historical profile range. */
        taiyin_calendar_datetime_init(&clock);
        clock.year = 1950;
        clock.month = 6;
        clock.day = 15;
        clock.hour = 12;
        if (taiyin_julian_day_split(&clock, &instant) < 0
            || taiyin_add_seconds_to_split_jd(
                   &instant, -8.0 * 3600.0, &instant) < 0) {
            taiyin_context_destroy(astronomy);
            return fail("build 1950 instant");
        }

        taiyin_chinese_calendar_config_init_china_standard_historical(
            &config, 8 * 60);
        if (taiyin_chinese_calendar_context_create(
                astronomy, &config, &historical) < 0) {
            taiyin_context_destroy(astronomy);
            return fail("create historical calendar context");
        }
        taiyin_chinese_calendar_config_init_china_standard_astronomical(
            &config, 8 * 60);
        if (taiyin_chinese_calendar_context_create(
                astronomy, &config, &astronomical) < 0) {
            taiyin_chinese_calendar_context_destroy(historical);
            taiyin_context_destroy(astronomy);
            return fail("create astronomical calendar context");
        }
        config.pillar_historical_mode = TAIYIN_C_GANZHI_PILLAR_HISTORICAL_ON;
        if (taiyin_chinese_calendar_context_create(
                astronomy, &config, &pillar_on) < 0) {
            taiyin_chinese_calendar_context_destroy(astronomical);
            taiyin_chinese_calendar_context_destroy(historical);
            taiyin_context_destroy(astronomy);
            return fail("create pillar-on calendar context");
        }
        config.pillar_historical_mode = TAIYIN_C_GANZHI_PILLAR_HISTORICAL_OFF;
        if (taiyin_chinese_calendar_context_create(
                astronomy, &config, &pillar_off) < 0) {
            taiyin_chinese_calendar_context_destroy(pillar_on);
            taiyin_chinese_calendar_context_destroy(astronomical);
            taiyin_chinese_calendar_context_destroy(historical);
            taiyin_context_destroy(astronomy);
            return fail("create pillar-off calendar context");
        }

        /* Historical arrangement lights the event-assignment flag only in
           historical mode. */
        taiyin_lunar_date_init(&lunar);
        result = taiyin_chinese_calendar_from_instant_ut(
            historical, &instant, &lunar, NULL);
        if (result < 0
            || (taiyin_call_result_flags(result)
                & TAIYIN_RESULT_FLAG_HISTORICAL_EVENT_ASSIGNMENT_APPLIED)
                == 0u) {
            return fail("historical from_instant_ut must light bit 4");
        }
        result = taiyin_chinese_calendar_from_instant_ut(
            astronomical, &instant, &lunar, NULL);
        if (result < 0
            || (taiyin_call_result_flags(result)
                & (TAIYIN_RESULT_FLAG_HISTORICAL_EVENT_ASSIGNMENT_APPLIED
                   | TAIYIN_RESULT_FLAG_HISTORICAL_CALENDAR_RULES_APPLIED
                   | TAIYIN_RESULT_FLAG_HISTORICAL_PILLAR_TERMS_APPLIED))
                != 0u) {
            return fail("astronomical from_instant_ut must stay clean");
        }

        /* Profile-range four pillars with pillar historical mode on lights
           the pillar-terms flag. */
        taiyin_ganzhi_four_pillars_init(&pillars_on);
        result = taiyin_chinese_calendar_calc_four_pillars_ut(
            pillar_on, &instant, &clock, TAIYIN_GANZHI_RAT_HOUR_NO_SPLIT,
            &pillars_on, NULL);
        if (result < 0
            || (taiyin_call_result_flags(result)
                & TAIYIN_RESULT_FLAG_HISTORICAL_PILLAR_TERMS_APPLIED)
                == 0u) {
            return fail("1950 four pillars must light bit 6");
        }

        /* Regression: one hour before a modern jie, where the profile is
           silent, pillar historical mode must agree with the precise
           instant rule instead of turning the month a day early. */
        taiyin_calendar_datetime_init(&clock);
        clock.year = 2026;
        clock.month = 3;
        clock.day = 1;
        if (taiyin_julian_day_split(&clock, &instant) < 0
            || taiyin_add_seconds_to_split_jd(
                   &instant, -8.0 * 3600.0, &instant) < 0
            || taiyin_chinese_calendar_get_next_jie_ut(
                   astronomical, &instant, &jie, NULL) < 0
            || taiyin_add_seconds_to_split_jd(
                   &jie.jd_ut, -3600.0, &instant) < 0
            || taiyin_add_seconds_to_split_jd(
                   &instant, 8.0 * 3600.0, &local) < 0
            || taiyin_reverse_julian_day_split(&local, &clock) < 0) {
            return fail("build pre-jie instant");
        }
        taiyin_ganzhi_four_pillars_init(&pillars_on);
        taiyin_ganzhi_four_pillars_init(&pillars_off);
        result = taiyin_chinese_calendar_calc_four_pillars_ut(
            pillar_on, &instant, &clock, TAIYIN_GANZHI_RAT_HOUR_NO_SPLIT,
            &pillars_on, NULL);
        if (result < 0
            || (taiyin_call_result_flags(result)
                & TAIYIN_RESULT_FLAG_HISTORICAL_PILLAR_TERMS_APPLIED)
                != 0u) {
            return fail("modern four pillars must not light bit 6");
        }
        if (taiyin_chinese_calendar_calc_four_pillars_ut(
                pillar_off, &instant, &clock, TAIYIN_GANZHI_RAT_HOUR_NO_SPLIT,
                &pillars_off, NULL) < 0
            || pillars_on.month != pillars_off.month
            || pillars_on.year != pillars_off.year) {
            return fail("pillar historical mode broke a modern pre-jie instant");
        }

        /* Out-of-EOP-range UTC with the estimate allowed lights the
           time-scale fallback flag. */
        if (taiyin_context_set_allow_utc_out_of_range_estimate(astronomy, 1u)
                < 0) {
            return fail("enable UTC out-of-range estimate");
        }
        taiyin_calendar_datetime_init(&clock);
        clock.year = 2500;
        clock.month = 1;
        clock.day = 1;
        taiyin_precise_time_scales_init(&scales);
        result = taiyin_make_time_scales_from_utc(
            astronomy, &clock, &scales, NULL);
        if (result < 0
            || (taiyin_call_result_flags(result)
                & TAIYIN_RESULT_FLAG_TIME_SCALE_FALLBACK)
                == 0u) {
            return fail("out-of-range UTC must light bit 3");
        }

        taiyin_chinese_calendar_context_destroy(pillar_off);
        taiyin_chinese_calendar_context_destroy(pillar_on);
        taiyin_chinese_calendar_context_destroy(astronomical);
        taiyin_chinese_calendar_context_destroy(historical);
        taiyin_context_destroy(astronomy);
    }
    return 0;
}
