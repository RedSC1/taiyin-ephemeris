#include "taiyin/c/taiyin.h"

#include <stdio.h>

static int fail(const char* message) {
    fprintf(stderr, "test_ganzhi_calendar_c_api: %s\n", message);
    return 1;
}

int main(int argc, char** argv) {
    taiyin_ganzhi value = TAIYIN_GANZHI_INVALID;
#ifndef TAIYIN_TEST_HAS_GANZHI_CALENDAR_EXTENSION
    (void)argc;
    (void)argv;
    if ((taiyin_get_capabilities() & TAIYIN_CAPABILITY_GANZHI_CALENDAR) != 0u
        || taiyin_ganzhi_make(0, 0, &value) != TAIYIN_ERROR_UNSUPPORTED
        || taiyin_ganzhi_advance(0, 0, &value) != TAIYIN_ERROR_UNSUPPORTED
        || taiyin_ganzhi_get_month(0, 0, &value) != TAIYIN_ERROR_UNSUPPORTED
        || taiyin_ganzhi_get_hour(0, 0, &value) != TAIYIN_ERROR_UNSUPPORTED
        || taiyin_ganzhi_calc_day_pillar(NULL, &value) != TAIYIN_ERROR_UNSUPPORTED
        || taiyin_ganzhi_get_nayin_element(0, &value) != TAIYIN_ERROR_UNSUPPORTED
        || taiyin_ganzhi_get_nayin_id(0, &value) != TAIYIN_ERROR_UNSUPPORTED
        || taiyin_chinese_calendar_calc_four_pillars_ut(
               NULL, NULL, NULL, 0, NULL, NULL) != TAIYIN_ERROR_UNSUPPORTED) {
        return fail("disabled Ganzhi calendar ABI must return UNSUPPORTED");
    }
    return 0;
#else
    if ((taiyin_get_capabilities() & TAIYIN_CAPABILITY_GANZHI_CALENDAR) == 0u
        || taiyin_ganzhi_make(4, 6, &value) != TAIYIN_STATUS_OK
        || value != 0x46u
        || taiyin_ganzhi_advance(value, 1, &value) != TAIYIN_STATUS_OK
        || value != 0x57u
        || taiyin_ganzhi_get_month(1, 11, &value) != TAIYIN_STATUS_OK
        || value != 0x51u
        || taiyin_ganzhi_get_hour(4, 0, &value) != TAIYIN_STATUS_OK
        || value != 0x80u
        || taiyin_ganzhi_get_nayin_id(0x00u, &value) != TAIYIN_STATUS_OK
        || value != 0u
        || taiyin_ganzhi_get_nayin_element(0x00u, &value) != TAIYIN_STATUS_OK
        || value != TAIYIN_GANZHI_WUXING_METAL) {
        return fail("enabled Ganzhi Pascal rules failed");
    }

    {
        taiyin_calendar_datetime date;
        taiyin_calendar_datetime_init(&date);
        date.year = 2000;
        date.month = 1;
        date.day = 1;
        date.hour = 23;
        date.minute = 59;
        if (taiyin_ganzhi_calc_day_pillar(&date, &value) != TAIYIN_STATUS_OK
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
        if (taiyin_runtime_initialize(&runtime_config) != TAIYIN_STATUS_OK
            || taiyin_context_create(&astronomy) != TAIYIN_STATUS_OK
            || taiyin_context_set_geocentric_observer(
                   astronomy, TAIYIN_BODY_EARTH, TAIYIN_BODY_EARTH)
                != TAIYIN_STATUS_OK) {
            taiyin_context_destroy(astronomy);
            return fail("initialize calendar astronomy");
        }
        taiyin_chinese_calendar_config_init_utc_offset(&calendar_config, 8 * 60);
        if (taiyin_chinese_calendar_context_create(
                astronomy, &calendar_config, &calendar) != TAIYIN_STATUS_OK) {
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
        if (taiyin_julian_day_split(&clock, &instant) != TAIYIN_STATUS_OK
            || taiyin_add_seconds_to_split_jd(
                   &instant, -8.0 * 3600.0, &instant) != TAIYIN_STATUS_OK
            || taiyin_chinese_calendar_calc_four_pillars_ut(
                   calendar,
                   &instant,
                   &clock,
                   TAIYIN_GANZHI_RAT_HOUR_NO_SPLIT,
                   &pillars,
                   NULL) != TAIYIN_STATUS_OK
            || pillars.year != 0x26u || pillars.month != 0x62u
            || pillars.day != 0x42u || pillars.hour != 0x35u) {
            taiyin_chinese_calendar_context_destroy(calendar);
            taiyin_context_destroy(astronomy);
            return fail("calculate four pillars through C ABI");
        }
        taiyin_chinese_calendar_context_destroy(calendar);
        taiyin_context_destroy(astronomy);
    }
    return 0;
#endif
}
