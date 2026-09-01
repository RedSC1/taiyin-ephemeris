#include "taiyin/c/chinese_calendar.h"
#include "taiyin/c/position.h"
#include "taiyin/c/runtime.h"
#include "taiyin/c/time.h"
#include "taiyin/c/ziwei.h"

#include <stdio.h>
#include <string.h>

static int fail(const char* message) {
    fprintf(stderr, "test_ziwei_c_api: %s\n", message);
    return 1;
}

static int encode_china_standard(
    int32_t year,
    uint8_t month,
    uint8_t day,
    uint8_t hour,
    uint8_t minute,
    taiyin_calendar_datetime* out_clock,
    taiyin_split_julian_date* out_instant
) {
    taiyin_split_julian_date local;
    taiyin_calendar_datetime_init(out_clock);
    out_clock->year = year;
    out_clock->month = month;
    out_clock->day = day;
    out_clock->hour = hour;
    out_clock->minute = minute;
    out_clock->second = 0.0;
    return taiyin_julian_day_split(out_clock, &local) >= 0
        && taiyin_add_seconds_to_split_jd(
            &local, -8.0 * 3600.0, out_instant) >= 0;
}

int main(int argc, char** argv) {
    taiyin_runtime_config runtime_config;
    taiyin_context* astronomy = NULL;
    taiyin_chinese_calendar_context* calendar = NULL;
    taiyin_chinese_calendar_config calendar_config;
    taiyin_ziwei_data_catalog* catalog = NULL;
    taiyin_ziwei_data_catalog* alternate_catalog = NULL;
    taiyin_ziwei_context* context = NULL;
    taiyin_ziwei_context* alternate_context = NULL;
    taiyin_ziwei_context* reloaded_context = NULL;
    taiyin_ziwei_context* custom_context = NULL;
    taiyin_ziwei_context* removed_context = NULL;
    taiyin_ziwei_ruleset* ruleset = NULL;
    taiyin_ziwei_chart* chart = NULL;
    taiyin_ziwei_option_override option_override;
    char source_path[2048];
    const char* source_paths[1];

    if (argc != 4
        || snprintf(source_path, sizeof(source_path),
            "%s/ephemerides/opm2/major-bodies/600y", argv[1]) < 0) {
        return fail("missing data root and Ziwei profile path");
    }
    source_paths[0] = source_path;
    taiyin_runtime_config_init(&runtime_config);
    runtime_config.source_paths = source_paths;
    runtime_config.source_path_count = 1u;
    runtime_config.load_packaged_data = 0u;
    taiyin_chinese_calendar_config_init_china_standard_historical(
        &calendar_config, 8 * 60);
    if (taiyin_runtime_initialize(&runtime_config) < 0
        || taiyin_context_create(&astronomy) < 0
        || taiyin_context_set_geocentric_observer(
            astronomy, TAIYIN_BODY_EARTH, TAIYIN_BODY_EARTH)
            < 0
        || taiyin_chinese_calendar_context_create(
            astronomy, &calendar_config, &calendar) < 0) {
        return fail("initialize calendar dependencies");
    }

    taiyin_ziwei_option_override_init(&option_override);
    option_override.component = TAIYIN_ZIWEI_OPTION_PLACEMENT;
    option_override.key = "ziwei";
    option_override.option = "option1";
    {
        taiyin_ziwei_data_catalog* missing_catalog = NULL;
        if (taiyin_call_result_status(taiyin_ziwei_data_catalog_create(
                "definitely-not-a-taiyin-ziwei-profile.toml",
                &missing_catalog)) != TAIYIN_FILE_ERROR_NOT_FOUND
            || missing_catalog != NULL) {
            return fail("report a missing Ziwei profile as not found");
        }
    }
    if (taiyin_ziwei_data_catalog_create(argv[2], &catalog)
            < 0
        || !catalog
        || taiyin_ziwei_context_create(
            catalog, &option_override, 1u, &context)
            < 0
        || !context
        || taiyin_ziwei_star_count(context) != 159u) {
        return fail("create Ziwei catalog and context");
    }
    if (taiyin_ziwei_data_catalog_create(argv[3], &alternate_catalog)
            < 0
        || !alternate_catalog
        || taiyin_ziwei_context_create(
            alternate_catalog, NULL, 0u, &alternate_context)
            < 0
        || !alternate_context) {
        return fail("create incompatible Ziwei context");
    }

    {
        taiyin_ziwei_json_rule_module module;
        taiyin_ziwei_option_override module_defaults[2];
        uint16_t custom_star = TAIYIN_ZIWEI_INVALID_STAR_ID;
        uint8_t is_natal = 0u;
        taiyin_ziwei_json_rule_module_init(&module);
        taiyin_ziwei_option_override_init(&module_defaults[0]);
        module_defaults[0].component = TAIYIN_ZIWEI_OPTION_PLACEMENT;
        module_defaults[0].option = "option1";
        taiyin_ziwei_option_override_init(&module_defaults[1]);
        module_defaults[1].component = TAIYIN_ZIWEI_OPTION_BRIGHTNESS;
        module_defaults[1].option = "option1";
        module.label = "c-json-module";
        module.stars_json =
            "[{\"key\":\"c_custom_star\",\"type\":\"minor\","
            "\"rule\":{\"type\":\"constant\",\"value\":11}}]";
        if (taiyin_ziwei_ruleset_create(&ruleset) < 0
            || taiyin_ziwei_ruleset_add_json_module(ruleset, &module) < 0) {
            return fail("compile a C JSON Ziwei ruleset");
        }
        if (taiyin_call_result_status(
                taiyin_ziwei_ruleset_add_json_module(ruleset, &module))
                != TAIYIN_ERROR_INVALID_ARGUMENT) {
            return fail("report a duplicate C JSON module label as invalid input");
        }
        if (taiyin_ziwei_context_create_with_ruleset(
                catalog, module_defaults, 2u, ruleset, &custom_context) < 0
            ) {
            return fail("preserve C JSON star options under component defaults");
        }
        if (taiyin_ziwei_ruleset_remove_module(
                ruleset, "c-json-module") < 0
            || taiyin_ziwei_context_create_with_ruleset(
                catalog, NULL, 0u, ruleset, &removed_context) < 0
            || taiyin_ziwei_star_count(removed_context) != 159u) {
            return fail("remove every option contributed by a C JSON module");
        }
        if (taiyin_call_result_status(taiyin_ziwei_ruleset_remove_module(
                ruleset, "c-json-module")) != TAIYIN_ERROR_INVALID_ARGUMENT) {
            return fail("reject removal of a missing C JSON module");
        }
        taiyin_ziwei_ruleset_destroy(ruleset);
        ruleset = NULL;
        if (taiyin_ziwei_star_count(custom_context) != 160u
            || taiyin_ziwei_find_star(
                custom_context, "c_custom_star", &custom_star) < 0
            || custom_star != 159u
            || taiyin_ziwei_star_is_natal(
                custom_context, custom_star, &is_natal) < 0
            || is_natal != 1u
            || taiyin_ziwei_star_count(context) != 159u) {
            return fail("isolate a C JSON Ziwei ruleset after its builder is destroyed");
        }
    }

    {
        uint16_t ziwei = TAIYIN_ZIWEI_INVALID_STAR_ID;
        int32_t category = -1;
        size_t required = 0u;
        char key[16];
        if (taiyin_ziwei_find_star(context, "ziwei", &ziwei)
                < 0
            || ziwei == TAIYIN_ZIWEI_INVALID_STAR_ID
            || taiyin_ziwei_get_star_metadata(
                context, ziwei, &category, NULL, 0u, &required)
                < 0
            || required != 6u
            || taiyin_ziwei_get_star_metadata(
                context, ziwei, &category, key, sizeof(key), &required)
                < 0
            || strcmp(key, "ziwei") != 0
            || category != TAIYIN_ZIWEI_STAR_MAJOR) {
            return fail("query stable star metadata");
        }
    }

    {
        taiyin_calendar_datetime birth_clock;
        taiyin_split_julian_date birth_instant;
    taiyin_ziwei_birth_options birth_options;
        taiyin_ephemeris_diagnostic diagnostic;
        uint8_t anchors[TAIYIN_ZIWEI_ANCHOR_COUNT];
        uint8_t gender = 0xffu;
        uint8_t bureau = 0xffu;
        uint8_t body_palace = 0xffu;
        uint16_t life_master = TAIYIN_ZIWEI_INVALID_STAR_ID;
        uint16_t body_master = TAIYIN_ZIWEI_INVALID_STAR_ID;
        taiyin_ziwei_transform_set transforms;
        uint16_t ziwei = TAIYIN_ZIWEI_INVALID_STAR_ID;
        uint16_t lucun = TAIYIN_ZIWEI_INVALID_STAR_ID;
        uint16_t hongluan = TAIYIN_ZIWEI_INVALID_STAR_ID;
        uint16_t zuofu = TAIYIN_ZIWEI_INVALID_STAR_ID;
        uint8_t position = TAIYIN_ZIWEI_INVALID_POSITION;
        uint8_t lucun_position = TAIYIN_ZIWEI_INVALID_POSITION;
        uint8_t hongluan_position = TAIYIN_ZIWEI_INVALID_POSITION;
        uint8_t zuofu_position = TAIYIN_ZIWEI_INVALID_POSITION;
        uint8_t palace = TAIYIN_ZIWEI_INVALID_POSITION;
        uint8_t has_transform_mark = 0u;
        uint16_t transformation_mask = 0u;
        int32_t brightness = TAIYIN_ZIWEI_BRIGHTNESS_NONE;
        size_t palace_star_count = 0u;

        taiyin_ziwei_birth_options_init(&birth_options);
        if (birth_options.leap_month_strategy
                != TAIYIN_ZIWEI_LEAP_SPLIT_AFTER_FIFTEENTH) {
            return fail(
                "birth options preserve the default split leap-month policy");
        }
        taiyin_ephemeris_diagnostic_init(&diagnostic);
        taiyin_ziwei_transform_set_init(&transforms);
        if (!encode_china_standard(
                2003, 3u, 13u, 14u, 15u, &birth_clock, &birth_instant)) {
            return fail("encode chart birth instant");
        }
        {
            taiyin_ziwei_birth_options invalid_birth_options = birth_options;
            taiyin_ziwei_chart* rejected_chart = NULL;
            invalid_birth_options.chart_mode = 256;
            if (taiyin_call_result_status(taiyin_ziwei_chart_create(
                    context, calendar, &birth_instant, &birth_clock,
                    TAIYIN_ZIWEI_GENDER_FEMALE, &invalid_birth_options,
                    &rejected_chart, &diagnostic)) != TAIYIN_ERROR_INVALID_ARGUMENT
                || rejected_chart != NULL) {
                return fail("reject out-of-range C birth options before enum narrowing");
            }
        }
        if (taiyin_ziwei_chart_create(
                context, calendar, &birth_instant, &birth_clock,
                TAIYIN_ZIWEI_GENDER_FEMALE, &birth_options,
                &chart, &diagnostic) < 0
            || !chart
            || taiyin_ziwei_chart_get_anchors(chart, anchors)
                < 0
            || taiyin_ziwei_chart_get_summary(
                chart, &gender, &bureau, &body_palace,
                &life_master, &body_master, &transforms)
                < 0
            || gender != TAIYIN_ZIWEI_GENDER_FEMALE
            || bureau > 4u || body_palace > 11u
            || life_master == TAIYIN_ZIWEI_INVALID_STAR_ID
            || body_master == TAIYIN_ZIWEI_INVALID_STAR_ID
            || taiyin_ziwei_find_star(context, "ziwei", &ziwei)
                < 0
            || taiyin_ziwei_find_star(context, "lucun", &lucun)
                < 0
            || taiyin_ziwei_find_star(context, "hongluan", &hongluan)
                < 0
            || taiyin_ziwei_find_star(context, "zuofu", &zuofu)
                < 0
            || taiyin_ziwei_chart_get_star_position(chart, ziwei, &position)
                < 0
            || taiyin_ziwei_chart_get_star_position(chart, lucun, &lucun_position)
                < 0
            || taiyin_ziwei_chart_get_star_position(chart, hongluan, &hongluan_position)
                < 0
            || taiyin_ziwei_chart_get_star_position(chart, zuofu, &zuofu_position)
                < 0
            || position == TAIYIN_ZIWEI_INVALID_POSITION
            || taiyin_ziwei_chart_get_star_palace(chart, ziwei, &palace)
                < 0
            || palace == TAIYIN_ZIWEI_INVALID_POSITION
            || taiyin_ziwei_chart_has_star_transform_mark(
                chart, TAIYIN_ZIWEI_CENTRIFUGAL_LU, ziwei,
                &has_transform_mark) < 0
            || has_transform_mark > 1u
            || taiyin_ziwei_chart_get_star_transformation_mask(
                chart, ziwei, &transformation_mask) < 0
            || transformation_mask >= (1u << 12)
            || taiyin_ziwei_chart_get_brightness(
                context, chart, ziwei, &brightness) < 0
            || taiyin_call_result_status(taiyin_ziwei_chart_get_brightness(
                alternate_context, chart, ziwei, &brightness))
                != TAIYIN_ERROR_INVALID_ARGUMENT
            || brightness < TAIYIN_ZIWEI_BRIGHTNESS_NONE
            || brightness > TAIYIN_ZIWEI_BRIGHTNESS_MIAO
            || taiyin_ziwei_chart_get_palace_stars(
                chart, position, NULL, 0u, &palace_star_count)
                < 0
            || palace_star_count == 0u) {
            return fail("construct and query natal chart");
        }

        {
            taiyin_ziwei_reverse_request reverse_request;
            taiyin_ziwei_reverse_candidate candidate;
            size_t candidate_count = 0u;
            taiyin_ziwei_reverse_request_init(&reverse_request);
            taiyin_ziwei_reverse_candidate_init(&candidate);
            reverse_request.start_instant_utc = birth_instant;
            reverse_request.end_instant_utc = birth_instant;
            reverse_request.start_virtual_time = birth_clock;
            reverse_request.gender = TAIYIN_ZIWEI_GENDER_FEMALE;
            reverse_request.query.lucun_branch = lucun_position;
            reverse_request.query.hongluan_branch = hongluan_position;
            reverse_request.query.zuofu_branch = zuofu_position;
            if (taiyin_ziwei_reverse_lookup_tier1(
                    context, calendar, &reverse_request,
                    NULL, 0u, &candidate_count, &diagnostic)
                    < 0
                || candidate_count != 1u
                || taiyin_ziwei_reverse_lookup_tier1(
                    context, calendar, &reverse_request,
                    &candidate, 1u, &candidate_count, &diagnostic)
                    < 0
                || candidate_count != 1u
                || candidate.virtual_time.year != birth_clock.year
                || candidate.virtual_time.struct_size
                    != sizeof(candidate.virtual_time)
                || candidate.virtual_time.month != birth_clock.month
                || candidate.virtual_time.day != birth_clock.day
                || candidate.virtual_time.hour != birth_clock.hour) {
                return fail("reverse lookup returns the matching logical slot");
            }
        }

        {
            taiyin_calendar_datetime target_clock;
            taiyin_split_julian_date target_instant;
            taiyin_ziwei_flow_options flow_options;
            taiyin_ziwei_flow_summary flow_summary;
            taiyin_ziwei_transform_set flow_transforms;
            uint8_t flow_position = TAIYIN_ZIWEI_INVALID_POSITION;
            uint8_t flow_life = TAIYIN_ZIWEI_INVALID_POSITION;
            uint8_t flow_stem = 0xffu;
            uint8_t flow_branch = TAIYIN_ZIWEI_INVALID_POSITION;
            size_t flow_palace_star_count = 0u;
            taiyin_split_julian_date next_instant;
            taiyin_calendar_datetime next_clock;
            taiyin_ziwei_flow_options_init(&flow_options);
            taiyin_ziwei_flow_summary_init(&flow_summary);
            taiyin_ziwei_transform_set_init(&flow_transforms);
            taiyin_calendar_datetime_init(&next_clock);
            if (sizeof(taiyin_ziwei_flow_summary) != 44u) {
                return fail("flow summary exposes effective month metadata");
            }
            if (!encode_china_standard(
                    2023, 3u, 25u, 10u, 30u,
                    &target_clock, &target_instant)) {
                return fail("encode flow target instant");
            }
            {
                taiyin_ziwei_flow_options invalid_flow_options = flow_options;
                invalid_flow_options.boundary = 256;
                if (taiyin_call_result_status(taiyin_ziwei_chart_set_flow(
                        context, calendar, &target_instant, &target_clock,
                        &invalid_flow_options, TAIYIN_ZIWEI_FLOW_HOUR,
                        chart, &flow_summary, &diagnostic))
                        != TAIYIN_ERROR_INVALID_ARGUMENT) {
                    return fail("reject out-of-range C flow options before enum narrowing");
                }
                invalid_flow_options = flow_options;
                invalid_flow_options.flow_month_palace_strategy = 256;
                if (taiyin_call_result_status(taiyin_ziwei_chart_set_flow(
                        context, calendar, &target_instant, &target_clock,
                        &invalid_flow_options, TAIYIN_ZIWEI_FLOW_HOUR,
                        chart, &flow_summary, &diagnostic))
                        != TAIYIN_ERROR_INVALID_ARGUMENT) {
                    return fail("reject out-of-range flow-month palace strategy");
                }
            }
            if (taiyin_ziwei_chart_set_flow(
                    context, calendar, &target_instant, &target_clock,
                    &flow_options, TAIYIN_ZIWEI_FLOW_HOUR,
                    chart, &flow_summary, &diagnostic) < 0
                || taiyin_call_result_status(taiyin_ziwei_chart_set_flow(
                    alternate_context, calendar, &target_instant, &target_clock,
                    &flow_options, TAIYIN_ZIWEI_FLOW_HOUR,
                    chart, &flow_summary, &diagnostic))
                    != TAIYIN_ERROR_INVALID_ARGUMENT
                || taiyin_ziwei_chart_flow_layer_count(chart) != 5u
                || flow_summary.effective_target_year != 2023
                || flow_summary.target_month != 2u
                || flow_summary.target_month_sequence != 3u
                || flow_summary.target_month_building_branch != 3u
                || flow_summary.target_effective_month != 2u
                || flow_summary.target_month_name
                    != TAIYIN_C_CHINESE_MONTH_NAME_NORMAL
                || flow_summary.target_palace_month_index != 3u
                || flow_summary.target_lunar_year != 2023
                || !flow_summary.target_month_is_leap
                || taiyin_ziwei_chart_get_flow_star_position(
                    chart, TAIYIN_ZIWEI_FLOW_YEAR, ziwei, &flow_position)
                    < 0
                || taiyin_ziwei_chart_get_flow_layer_summary(
                    chart, TAIYIN_ZIWEI_FLOW_YEAR, &flow_life,
                    &flow_stem, &flow_branch) < 0
                || flow_life > 11u || flow_stem > 9u || flow_branch > 11u
                || taiyin_ziwei_chart_get_flow_palace_stars(
                    chart, TAIYIN_ZIWEI_FLOW_YEAR, flow_branch,
                    NULL, 0u, &flow_palace_star_count) < 0
                || taiyin_ziwei_chart_get_flow_transforms(
                    chart, TAIYIN_ZIWEI_FLOW_YEAR, &flow_transforms)
                    < 0
                || taiyin_ziwei_chart_truncate_flow(
                    chart, TAIYIN_ZIWEI_FLOW_MONTH) < 0
                || taiyin_ziwei_chart_flow_layer_count(chart) != 2u
                || taiyin_ziwei_step_flow_day_target(
                    &target_instant, &target_clock, 1,
                    &next_instant, &next_clock) < 0
                || next_clock.year != 2023 || next_clock.month != 3u
                || next_clock.day != 26u || next_clock.hour != 10u
                || next_clock.minute != 30u) {
                return fail("construct and query flow stack");
            }
        }
    }

    {
        const uint64_t old_generation =
            taiyin_ziwei_context_generation(context);
        if (taiyin_ziwei_data_catalog_reload(catalog) < 0
            || taiyin_ziwei_data_catalog_generation(catalog) == old_generation
            || taiyin_ziwei_context_generation(context) != old_generation
            || taiyin_ziwei_context_create(
                catalog, NULL, 0u, &reloaded_context) < 0
            || taiyin_ziwei_context_generation(reloaded_context)
                != taiyin_ziwei_data_catalog_generation(catalog)) {
            return fail("reload immutable catalog snapshot");
        }
    }

    taiyin_ziwei_chart_destroy(chart);
    taiyin_ziwei_context_destroy(custom_context);
    taiyin_ziwei_context_destroy(removed_context);
    taiyin_ziwei_ruleset_destroy(ruleset);
    taiyin_ziwei_context_destroy(reloaded_context);
    taiyin_ziwei_context_destroy(alternate_context);
    taiyin_ziwei_context_destroy(context);
    taiyin_ziwei_data_catalog_destroy(alternate_catalog);
    taiyin_ziwei_data_catalog_destroy(catalog);
    taiyin_chinese_calendar_context_destroy(calendar);
    taiyin_context_destroy(astronomy);
    return 0;
}
