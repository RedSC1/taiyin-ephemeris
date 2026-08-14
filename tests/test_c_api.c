#include "taiyin/c/taiyin.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fail(const char* message) {
    fprintf(stderr, "test_c_api: %s\n", message);
    return 1;
}

static unsigned char* read_file(
    const char* path,
    size_t* out_size
) {
    FILE* file;
    long length;
    unsigned char* data;
    if (!path || !out_size) return NULL;
    *out_size = 0;
    file = fopen(path, "rb");
    if (!file) return NULL;
    if (fseek(file, 0, SEEK_END) != 0
        || (length = ftell(file)) <= 0
        || fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return NULL;
    }
    data = (unsigned char*)malloc((size_t)length);
    if (!data) {
        fclose(file);
        return NULL;
    }
    if (fread(data, 1, (size_t)length, file) != (size_t)length) {
        free(data);
        fclose(file);
        return NULL;
    }
    fclose(file);
    *out_size = (size_t)length;
    return data;
}

static taiyin_status TAIYIN_C_CALL test_position_evaluator(
    const taiyin_context* context,
    int32_t target_id,
    const taiyin_split_julian_date* jd_tdb,
    const taiyin_split_julian_date* jd_tt,
    uint32_t flags,
    double out_position[6],
    taiyin_ephemeris_diagnostic* diagnostic,
    void* user_data
) {
    double offset = user_data ? *(const double*)user_data : 0.0;
    (void)context;
    (void)target_id;
    (void)jd_tdb;
    (void)jd_tt;
    (void)flags;
    (void)diagnostic;
    out_position[0] = 1.0 + offset;
    out_position[1] = 2.0;
    out_position[2] = 3.0;
    out_position[3] = 0.0;
    out_position[4] = 0.0;
    out_position[5] = 0.0;
    return TAIYIN_STATUS_OK;
}

static taiyin_status TAIYIN_C_CALL test_ayanamsha_evaluator(
    const taiyin_context* context,
    const taiyin_split_julian_date* jd_tt,
    uint64_t flags,
    double* out_ayanamsha_rad,
    void* user_data
) {
    (void)context;
    (void)jd_tt;
    (void)flags;
    *out_ayanamsha_rad = user_data ? *(const double*)user_data : 0.0;
    return TAIYIN_STATUS_OK;
}

static taiyin_bool TAIYIN_C_CALL test_house_evaluator(
    const taiyin_house_system_dispatch_data* data,
    double out_cusps[12],
    void* user_data
) {
    const double step = 6.283185307179586476925286766559 / 12.0;
    double offset = user_data ? *(const double*)user_data : 0.0;
    int i;
    if (!data || data->struct_size < sizeof(*data)) return 0u;
    for (i = 0; i < 12; ++i) {
        out_cusps[i] = fmod(data->ascendant_rad + offset + i * step,
                            6.283185307179586476925286766559);
    }
    return 1u;
}

int main(int argc, char** argv) {
    const taiyin_split_julian_date jd_2460409 = {2460409, 0.0};
    const taiyin_split_julian_date jd_2460409_0008 = {2460409, 0.0008};
    const taiyin_split_julian_date jd_2460409_25 = {2460409, 0.25};
    const taiyin_split_julian_date jd_2460380_5 = {2460380, 0.5};
    const taiyin_split_julian_date jd_2460395_5 = {2460395, 0.5};
    const taiyin_split_julian_date invalid_jd = {0, NAN};
    if (taiyin_get_c_abi_version() != TAIYIN_C_ABI_VERSION) {
        return fail("ABI version mismatch");
    }
    if (strcmp(taiyin_get_library_version(), TAIYIN_LIBRARY_VERSION_STRING) != 0) {
        return fail("library version mismatch");
    }
    if (strcmp(taiyin_get_library_codename(), TAIYIN_LIBRARY_CODENAME) != 0
        || strcmp(taiyin_get_library_codename(), "Singularity") != 0) {
        return fail("library codename mismatch");
    }
    {
        uint64_t expected_capabilities = TAIYIN_CAPABILITY_POSITION
            | TAIYIN_CAPABILITY_ECLIPSE
            | TAIYIN_CAPABILITY_SPLIT_TIME;
#ifndef TAIYIN_TEST_MODULAR_C_API
        expected_capabilities |= TAIYIN_CAPABILITY_ASTROLOGY;
#endif
        if ((taiyin_get_capabilities() & expected_capabilities)
            != expected_capabilities) {
            return fail("capability mask mismatch");
        }
    }
#if TAIYIN_TEST_EXPECT_BAZI && !defined(TAIYIN_TEST_MODULAR_C_API)
    if ((taiyin_get_capabilities() & TAIYIN_CAPABILITY_BAZI) == 0u) {
        return fail("BaZi-enabled build does not advertise its capability");
    }
#elif !TAIYIN_TEST_EXPECT_BAZI && !defined(TAIYIN_TEST_MODULAR_C_API)
    if ((taiyin_get_capabilities() & TAIYIN_CAPABILITY_BAZI) != 0u) {
        return fail("BaZi-disabled build advertises the BaZi capability");
    }
#endif
    if (strcmp(taiyin_status_name(TAIYIN_ERROR_INVALID_ARGUMENT),
               "TAIYIN_ERROR_INVALID_ARGUMENT") != 0) {
        return fail("status name mismatch");
    }
    {
        taiyin_ephemeris_diagnostic formatted_diagnostic;
        char formatted[512];
        size_t required_size = 0;
        taiyin_ephemeris_diagnostic_init(&formatted_diagnostic);
        formatted_diagnostic.status = TAIYIN_EPHEMERIS_ERROR_NO_ROUTE;
        formatted_diagnostic.target_id = 499;
        formatted_diagnostic.center_id = 399;
        if (taiyin_format_ephemeris_diagnostic(
                &formatted_diagnostic, NULL, 0, &required_size)
                != TAIYIN_STATUS_OK
            || required_size == 0
            || required_size > sizeof(formatted)) {
            return fail("diagnostic format count mismatch");
        }
        if (taiyin_format_ephemeris_diagnostic(
                &formatted_diagnostic,
                formatted,
                sizeof(formatted),
                &required_size) != TAIYIN_STATUS_OK
            || strstr(formatted, "target=499") == NULL
            || strstr(formatted, "TAIYIN_EPHEMERIS_ERROR_NO_ROUTE") == NULL) {
            return fail("diagnostic format mismatch");
        }
    }
    if (argc != 2) {
        return fail("missing data root argument");
    }

    taiyin_runtime_config runtime_config;
    taiyin_runtime_config_init(&runtime_config);
    {
        static char source_path[2048];
        static const char* source_paths[1];
        if (snprintf(
                source_path,
                sizeof(source_path),
                "%s/ephemerides/opm2/major-bodies/600y",
                argv[1]) < 0) {
            return fail("source path formatting failed");
        }
        source_paths[0] = source_path;
        runtime_config.source_paths = source_paths;
        runtime_config.source_path_count = 1;
        runtime_config.load_packaged_data = 0u;
    }
    {
        static double preinit_position_offset = 0.5;
        if (taiyin_register_native_position_evaluator(
                -200000,
                &test_position_evaluator,
                NULL,
                &preinit_position_offset) != TAIYIN_STATUS_OK) {
            return fail("pre-initialization evaluator registration failed");
        }
    }
    if (taiyin_runtime_initialize(&runtime_config) != TAIYIN_STATUS_OK) {
        return fail("runtime initialization failed");
    }
    if (taiyin_runtime_set_ephemeris_source_priority(
            "test-ephemeris-priority.bsp", 10) != TAIYIN_STATUS_OK) {
        return fail("ephemeris source priority setup failed");
    }
    if (taiyin_runtime_clear_ephemeris_source_priority(
            "test-ephemeris-priority.bsp") != TAIYIN_STATUS_OK) {
        return fail("ephemeris source priority clear failed");
    }
    if (taiyin_runtime_set_ephemeris_source_priority(
            "test-ephemeris-priority.bsp", -10) != TAIYIN_STATUS_OK) {
        return fail("ephemeris source priority reset failed");
    }
    taiyin_runtime_clear_all_ephemeris_source_priorities();
    {
        const size_t source_count =
            taiyin_runtime_registered_data_source_count();
        size_t i;
        int found_ephemeris = 0;
        int found_builtin_eop = 0;
        if (source_count < 2u) {
            return fail("registered runtime data source count is too small");
        }
        for (i = 0; i < source_count; ++i) {
            taiyin_runtime_registered_data_source source_info;
            char source[2048];
            size_t required_size = 0;
            taiyin_runtime_registered_data_source_init(&source_info);
            if (taiyin_runtime_get_registered_data_source(
                    i, &source_info, NULL, 0, &required_size)
                    != TAIYIN_STATUS_OK
                || required_size == 0u
                || required_size > sizeof(source)
                || taiyin_runtime_get_registered_data_source(
                    i,
                    &source_info,
                    source,
                    sizeof(source),
                    &required_size) != TAIYIN_STATUS_OK
                || source[0] == '\0') {
                return fail("registered runtime data source query failed");
            }
            if (source_info.kind
                    == TAIYIN_RUNTIME_DATA_SOURCE_EPHEMERIS
                && source_info.item_count > 0u
                && (source_info.flags
                    & TAIYIN_RUNTIME_DATA_SOURCE_HAS_COVERAGE) != 0u) {
                found_ephemeris = 1;
            }
            if (source_info.kind
                    == TAIYIN_RUNTIME_DATA_SOURCE_EARTH_ORIENTATION
                && source_info.format
                    == TAIYIN_RUNTIME_DATA_FORMAT_BUILTIN_EOP
                && strcmp(source, "builtin:eop") == 0) {
                found_builtin_eop = 1;
            }
        }
        if (!found_ephemeris || !found_builtin_eop) {
            return fail("registered runtime data sources are incomplete");
        }
    }
    {
        char star_catalog_path[2048];
        size_t star_catalog_size = 0;
        unsigned char* star_catalog_data;
        double magnitude = NAN;
        if (snprintf(
                star_catalog_path,
                sizeof(star_catalog_path),
                "%s/stars/catalogs/stars-fixed-traditional.tsc1",
                argv[1]) < 0) {
            return fail("star catalog path formatting failed");
        }
        star_catalog_data = read_file(star_catalog_path, &star_catalog_size);
        if (!star_catalog_data
            || taiyin_star_catalog_add_tsc1_memory(
                    star_catalog_data, star_catalog_size)
                != TAIYIN_STATUS_OK) {
            free(star_catalog_data);
            return fail("memory star catalog initialization failed");
        }
        memset(star_catalog_data, 0, star_catalog_size);
        free(star_catalog_data);
        if (taiyin_star_find_magnitude("spica", &magnitude)
                != TAIYIN_STATUS_OK
            || !isfinite(magnitude)) {
            return fail("memory star catalog did not retain its input");
        }
        taiyin_star_catalog_clear();
        if (taiyin_star_catalog_add_tsc1(star_catalog_path)
            != TAIYIN_STATUS_OK) {
            return fail("star catalog initialization failed");
        }
    }

    taiyin_calendar_datetime datetime;
    taiyin_calendar_datetime_init(&datetime);
    datetime.year = 2000;
    datetime.month = 1;
    datetime.day = 1;
    datetime.hour = 12;

    double jd = 0.0;
    if (taiyin_julian_day(&datetime, &jd) != TAIYIN_STATUS_OK
        || fabs(jd - 2451545.0) > 1.0e-12) {
        return fail("Julian day conversion failed");
    }

    taiyin_calendar_datetime roundtrip;
    taiyin_calendar_datetime_init(&roundtrip);
    if (taiyin_reverse_julian_day(jd, &roundtrip) != TAIYIN_STATUS_OK
        || roundtrip.year != 2000
        || roundtrip.month != 1
        || roundtrip.day != 1
        || roundtrip.hour != 12) {
        return fail("Julian day roundtrip failed");
    }

    {
        taiyin_split_julian_date first;
        taiyin_split_julian_date second;
        taiyin_split_julian_date first_tt;
        taiyin_split_julian_date second_tt;
        taiyin_split_julian_date first_tdb;
        taiyin_split_julian_date roundtrip_tt;
        taiyin_split_julian_date boundary_tt;
        taiyin_split_julian_date boundary_tdb;
        taiyin_split_julian_date boundary_roundtrip_tt;
        taiyin_split_julian_date calendar_jd;
        taiyin_calendar_datetime precise_calendar;
        taiyin_calendar_datetime precise_roundtrip;
        taiyin_split_precise_time_scales split_scales;
        double nanosecond_difference = 0.0;
        double converted_jd = 0.0;

        taiyin_calendar_datetime_init(&precise_calendar);
        taiyin_calendar_datetime_init(&precise_roundtrip);
        taiyin_split_precise_time_scales_init(&split_scales);
        precise_calendar.year = 2024;
        precise_calendar.month = 4;
        precise_calendar.day = 8;
        precise_calendar.hour = 18;
        precise_calendar.minute = 17;
        precise_calendar.second = 20.000000001;

        if (taiyin_split_julian_date_from_parts(
                2451545, 0.25, &first) != TAIYIN_STATUS_OK
            || taiyin_add_seconds_to_split_jd(
                &first, 1.0e-9, &second) != TAIYIN_STATUS_OK
            || taiyin_seconds_between_split_jd(
                &first, &second, &nanosecond_difference)
                != TAIYIN_STATUS_OK
            || fabs(nanosecond_difference - 1.0e-9) > 5.0e-12
            || taiyin_utc_to_tt_split(&first, 37.0, &first_tt)
                != TAIYIN_STATUS_OK
            || taiyin_utc_to_tt_split(&second, 37.0, &second_tt)
                != TAIYIN_STATUS_OK
            || taiyin_seconds_between_split_jd(
                &first_tt, &second_tt, &nanosecond_difference)
                != TAIYIN_STATUS_OK
            || fabs(nanosecond_difference - 1.0e-9) > 5.0e-12
            || taiyin_tt_to_tdb_split(
                &first_tt, TAIYIN_TDB_MODEL_SOFA_FULL, &first_tdb)
                != TAIYIN_STATUS_OK
            || taiyin_tdb_to_tt_split(
                &first_tdb, TAIYIN_TDB_MODEL_SOFA_FULL, &roundtrip_tt)
                != TAIYIN_STATUS_OK
            || taiyin_seconds_between_split_jd(
                &first_tt, &roundtrip_tt, &nanosecond_difference)
                != TAIYIN_STATUS_OK
            || fabs(nanosecond_difference) > 5.0e-12
            || taiyin_split_julian_date_from_parts(
                2460000, 0.0, &boundary_tt) != TAIYIN_STATUS_OK
            || taiyin_tt_to_tdb_split(
                &boundary_tt,
                TAIYIN_TDB_MODEL_FAST_PERIODIC,
                &boundary_tdb) != TAIYIN_STATUS_OK
            || taiyin_tdb_to_tt_split(
                &boundary_tdb,
                TAIYIN_TDB_MODEL_FAST_PERIODIC,
                &boundary_roundtrip_tt) != TAIYIN_STATUS_OK
            || taiyin_seconds_between_split_jd(
                &boundary_tt,
                &boundary_roundtrip_tt,
                &nanosecond_difference) != TAIYIN_STATUS_OK
            || fabs(nanosecond_difference) > 5.0e-12
            || taiyin_tt_to_tdb_split(
                &boundary_tt,
                TAIYIN_TDB_MODEL_SOFA_FULL,
                &boundary_tdb) != TAIYIN_STATUS_OK
            || taiyin_tdb_to_tt_split(
                &boundary_tdb,
                TAIYIN_TDB_MODEL_SOFA_FULL,
                &boundary_roundtrip_tt) != TAIYIN_STATUS_OK
            || taiyin_seconds_between_split_jd(
                &boundary_tt,
                &boundary_roundtrip_tt,
                &nanosecond_difference) != TAIYIN_STATUS_OK
            || fabs(nanosecond_difference) > 5.0e-12
            || taiyin_julian_day_split(&precise_calendar, &calendar_jd)
                != TAIYIN_STATUS_OK
            || taiyin_reverse_julian_day_split(
                &calendar_jd, &precise_roundtrip) != TAIYIN_STATUS_OK
            || fabs(precise_roundtrip.second - precise_calendar.second)
                > 1.0e-10
            || taiyin_make_split_precise_time_scales_from_utc(
                &precise_calendar,
                37.0,
                -0.1,
                TAIYIN_TDB_MODEL_SOFA_FULL,
                &split_scales) != TAIYIN_STATUS_OK
            || taiyin_seconds_between_split_jd(
                &split_scales.ut1,
                &split_scales.tt,
                &nanosecond_difference) != TAIYIN_STATUS_OK
            || fabs(nanosecond_difference - 69.284) > 1.0e-11
            || taiyin_split_julian_date_to_double(
                &calendar_jd, &converted_jd) != TAIYIN_STATUS_OK
            || taiyin_julian_day(&precise_calendar, &jd)
                != TAIYIN_STATUS_OK
            || fabs(converted_jd - jd) > 1.0e-12) {
            return fail("split Julian-date C API failed");
        }
        if (taiyin_split_julian_date_from_parts(
                0, NAN, &first) != TAIYIN_ERROR_INVALID_ARGUMENT
            || taiyin_tt_to_tdb_split(
                &first_tt, 99, &first_tdb)
                != TAIYIN_ERROR_INVALID_ARGUMENT) {
            return fail("invalid split Julian-date input must be rejected");
        }
    }

    taiyin_context* context = NULL;
    if (taiyin_context_create(&context) != TAIYIN_STATUS_OK || !context) {
        return fail("context creation failed");
    }
    if (taiyin_context_set_geocentric_observer(
            context, TAIYIN_BODY_EARTH, TAIYIN_BODY_EARTH)
        != TAIYIN_STATUS_OK) {
        taiyin_context_destroy(context);
        return fail("geocentric observer setup failed");
    }
    {
        taiyin_chinese_calendar_config calendar_config;
        taiyin_chinese_calendar_context* calendar_context = NULL;
        taiyin_solar_date solar;
        taiyin_solar_date roundtrip;
        taiyin_lunar_date lunar;
        taiyin_chinese_calendar_year calendar_year;
        taiyin_chinese_solar_term_event previous_term;
        taiyin_chinese_solar_term_event next_term;
        taiyin_chinese_solar_term_event specific_term;
        taiyin_chinese_solar_term_event previous_jie;
        taiyin_chinese_solar_term_event next_jie;
        taiyin_chinese_solar_term_event previous_qi;
        taiyin_chinese_solar_term_event next_qi;
        taiyin_chinese_solar_term_event undersized_term;
        taiyin_chinese_solar_term_event uninitialized_term = {0};
        taiyin_calendar_datetime probe;
        taiyin_split_julian_date probe_jd;

        taiyin_chinese_calendar_config_init_local_astronomical_utc_offset(
            &calendar_config, 8 * 60);
        taiyin_solar_date_init(&solar);
        taiyin_solar_date_init(&roundtrip);
        taiyin_lunar_date_init(&lunar);
        taiyin_chinese_solar_term_event_init(&previous_term);
        taiyin_chinese_solar_term_event_init(&next_term);
        taiyin_chinese_solar_term_event_init(&specific_term);
        taiyin_chinese_solar_term_event_init(&previous_jie);
        taiyin_chinese_solar_term_event_init(&next_jie);
        taiyin_chinese_solar_term_event_init(&previous_qi);
        taiyin_chinese_solar_term_event_init(&next_qi);
        taiyin_chinese_solar_term_event_init(&undersized_term);
        --undersized_term.struct_size;
        taiyin_chinese_calendar_year_init(&calendar_year);
        taiyin_calendar_datetime_init(&probe);
        solar.year = 2025;
        solar.month = 1;
        solar.day = 29;
        probe.year = 2034;
        probe.month = 1;
        probe.day = 15;
        probe.hour = 12;

        if (taiyin_chinese_calendar_context_create(
                context, &calendar_config, &calendar_context)
                != TAIYIN_STATUS_OK
            || !calendar_context
            || taiyin_chinese_calendar_from_solar(
                calendar_context, &solar, &lunar, NULL)
                != TAIYIN_STATUS_OK
            || lunar.year != 2025 || lunar.month != 1
            || lunar.day != 1 || lunar.is_leap != 0
            || taiyin_chinese_calendar_from_lunar(
                calendar_context, &lunar, &roundtrip, NULL)
                != TAIYIN_STATUS_OK
            || roundtrip.year != solar.year
            || roundtrip.month != solar.month
            || roundtrip.day != solar.day
            || taiyin_julian_day_split(&probe, &probe_jd)
                != TAIYIN_STATUS_OK
            || taiyin_chinese_calendar_calc_year_ut(
                calendar_context, &probe_jd, &calendar_year, NULL)
                != TAIYIN_STATUS_OK
            || taiyin_chinese_calendar_from_instant_ut(
                calendar_context, &probe_jd, &lunar, NULL)
                != TAIYIN_STATUS_OK
            || taiyin_chinese_calendar_get_prev_jie_qi_ut(
                calendar_context, &probe_jd, &previous_term, NULL)
                != TAIYIN_STATUS_OK
            || taiyin_chinese_calendar_get_next_jie_qi_ut(
                calendar_context, &previous_term.jd_ut, &next_term, NULL)
                != TAIYIN_STATUS_OK
            || taiyin_chinese_calendar_get_specific_jie_qi_ut(
                calendar_context, 2025, 21, &specific_term, NULL)
                != TAIYIN_STATUS_OK
            || taiyin_chinese_calendar_get_prev_jie_ut(
                calendar_context, &probe_jd, &previous_jie, NULL)
                != TAIYIN_STATUS_OK
            || taiyin_chinese_calendar_get_next_jie_ut(
                calendar_context, &probe_jd, &next_jie, NULL)
                != TAIYIN_STATUS_OK
            || taiyin_chinese_calendar_get_prev_qi_ut(
                calendar_context, &probe_jd, &previous_qi, NULL)
                != TAIYIN_STATUS_OK
            || taiyin_chinese_calendar_get_next_qi_ut(
                calendar_context, &probe_jd, &next_qi, NULL)
                != TAIYIN_STATUS_OK
            || taiyin_chinese_calendar_get_specific_jie_qi_ut(
                calendar_context, 2025, 24, &specific_term, NULL)
                != TAIYIN_ERROR_INVALID_ARGUMENT
            || taiyin_chinese_calendar_get_prev_jie_qi_ut(
                NULL, &probe_jd, &previous_term, NULL)
                != TAIYIN_ERROR_INVALID_ARGUMENT
            || taiyin_chinese_calendar_get_next_jie_qi_ut(
                calendar_context, &invalid_jd, &next_term, NULL)
                != TAIYIN_ERROR_INVALID_ARGUMENT
            || taiyin_chinese_calendar_get_prev_jie_ut(
                calendar_context, &probe_jd, &uninitialized_term, NULL)
                != TAIYIN_ERROR_INVALID_ARGUMENT
            || taiyin_chinese_calendar_get_next_jie_ut(
                calendar_context, &probe_jd, &undersized_term, NULL)
                != TAIYIN_ERROR_INVALID_ARGUMENT
            || taiyin_chinese_calendar_get_prev_qi_ut(
                calendar_context, &probe_jd, NULL, NULL)
                != TAIYIN_ERROR_INVALID_ARGUMENT
            || taiyin_chinese_calendar_get_next_qi_ut(
                calendar_context, &invalid_jd, &next_qi, NULL)
                != TAIYIN_ERROR_INVALID_ARGUMENT
            || previous_term.struct_size != sizeof(previous_term)
            || next_term.struct_size != sizeof(next_term)
            || specific_term.struct_size != sizeof(specific_term)
            || next_term.jd_ut.day_number < previous_term.jd_ut.day_number
            || (next_term.jd_ut.day_number == previous_term.jd_ut.day_number
                && next_term.jd_ut.day_fraction
                    <= previous_term.jd_ut.day_fraction)
            || specific_term.index_from_winter_solstice != 3
            || calendar_year.leap_month_index != 1
            || calendar_year.months[1].month != 11
            || calendar_year.months[1].is_leap == 0) {
            taiyin_chinese_calendar_context_destroy(calendar_context);
            taiyin_context_destroy(context);
            return fail("Chinese calendar C API failed");
        }
        taiyin_chinese_calendar_context_destroy(calendar_context);
    }
    {
        double preinit_position[6] = {0.0, 0.0, 0.0, 0.0, 0.0, 0.0};
        if (taiyin_calc_position_tt(
                context,
                -200000,
                &jd_2460409,
                TAIYIN_POSITION_XYZ,
                preinit_position,
                NULL) != TAIYIN_STATUS_OK
            || fabs(preinit_position[0] - 1.5) > 1.0e-15
            || taiyin_unregister_native_position_evaluator(-200000)
                != TAIYIN_STATUS_OK) {
            taiyin_context_destroy(context);
            return fail("first runtime initialization cleared evaluator");
        }
    }

    taiyin_observer_location observer;
    taiyin_observer_location_init(&observer);
    observer.longitude_deg = 116.391;
    observer.latitude_deg = 39.907;
    observer.height_m = 50.0;
    if (taiyin_context_set_observer_location(context, &observer)
        != TAIYIN_STATUS_OK) {
        taiyin_context_destroy(context);
        return fail("observer setup failed");
    }

    {
        taiyin_context* non_earth = NULL;
        taiyin_cartesian_state zero_offset;
        taiyin_observed_position observed;
        taiyin_ephemeris_diagnostic diagnostic;
        const int32_t body_id = TAIYIN_BODY_SUN;
        double position[6] = {0.0};
        taiyin_cartesian_state_init(&zero_offset);
        taiyin_observed_position_init(&observed);
        taiyin_ephemeris_diagnostic_init(&diagnostic);
        if (taiyin_context_clone(context, &non_earth) != TAIYIN_STATUS_OK
            || !non_earth
            || taiyin_context_set_geocentric_observer(
                    non_earth,
                    TAIYIN_BODY_MARS_BARYCENTER,
                    TAIYIN_BODY_SUN) != TAIYIN_STATUS_OK
            || taiyin_context_set_topocentric_observer_offset(
                    non_earth, &zero_offset) != TAIYIN_ERROR_UNSUPPORTED
            || taiyin_context_set_simple_topocentric_observer(
                    non_earth, &observer, &jd_2460409, &jd_2460409_0008)
                != TAIYIN_ERROR_UNSUPPORTED
            || taiyin_context_set_precise_topocentric_observer(
                    non_earth, &observer, &jd_2460409, &jd_2460409_0008)
                != TAIYIN_ERROR_UNSUPPORTED
            || taiyin_calc_position_ut(
                    non_earth,
                    TAIYIN_BODY_SUN,
                    &jd_2460409,
                    TAIYIN_POSITION_XYZ | TAIYIN_POSITION_TOPOCENTRIC,
                    position,
                    &diagnostic) != TAIYIN_ERROR_UNSUPPORTED
            || taiyin_calc_observed_bodies_ut(
                    non_earth,
                    &jd_2460409,
                    &body_id,
                    1,
                    TAIYIN_OBSERVED_TOPOCENTRIC,
                    &observed,
                    &diagnostic) != TAIYIN_ERROR_UNSUPPORTED
            || taiyin_calc_star_position_tt(
                    non_earth,
                    "spica",
                    &jd_2460409,
                    TAIYIN_POSITION_XYZ,
                    position,
                    &diagnostic) != TAIYIN_ERROR_UNSUPPORTED) {
            taiyin_context_destroy(non_earth);
            taiyin_context_destroy(context);
            return fail("non-Earth topocentric/star boundary failed");
        }
        taiyin_context_destroy(non_earth);
    }

    {
        const taiyin_split_julian_date unnormalized_jd = {2460408, 1.0};
        const taiyin_split_julian_date nan_fraction_jd = {2460409, NAN};
        const taiyin_split_julian_date overflowing_jd = {INT64_MAX, 1.0};
        const int32_t target_ids[1] = {TAIYIN_BODY_SUN};
        double canonical_position[6] = {0.0};
        double unnormalized_position[6] = {0.0};
        double scalar_tdb_position[6] = {0.0};
        double batch_tdb_position[6] = {0.0};
        taiyin_split_julian_date event_jd;
        taiyin_visibility_event_result visibility;
        taiyin_body_osculating_orbit orbit;
        size_t i;
        taiyin_visibility_event_result_init(&visibility);
        taiyin_body_osculating_orbit_init(&orbit);

        if (taiyin_calc_position_tt(
                context, TAIYIN_BODY_SUN, NULL, TAIYIN_POSITION_XYZ,
                canonical_position, NULL) != TAIYIN_ERROR_INVALID_ARGUMENT
            || taiyin_search_solar_longitude_ut(
                context, 0.0, NULL, 0u, &event_jd, NULL)
                != TAIYIN_ERROR_INVALID_ARGUMENT
            || taiyin_search_solar_transit_ut(
                context, NULL, &jd_2460409_25,
                TAIYIN_VISIBILITY_EVENT_UPPER_TRANSIT, &visibility, NULL)
                != TAIYIN_ERROR_INVALID_ARGUMENT
            || taiyin_calc_body_osculating_orbit_tt(
                context, TAIYIN_BODY_MARS_BARYCENTER, NULL,
                TAIYIN_FRAME_J2000_ECLIPTIC, 0u, &orbit, NULL)
                != TAIYIN_ERROR_INVALID_ARGUMENT) {
            taiyin_context_destroy(context);
            return fail("required split Julian-date pointers must be rejected");
        }
        if (taiyin_calc_position_tt(
                context, TAIYIN_BODY_SUN, &jd_2460409,
                TAIYIN_POSITION_XYZ, canonical_position, NULL)
                != TAIYIN_STATUS_OK
            || taiyin_calc_position_tt(
                context, TAIYIN_BODY_SUN, &unnormalized_jd,
                TAIYIN_POSITION_XYZ, unnormalized_position, NULL)
                != TAIYIN_STATUS_OK
            || taiyin_calc_position_tt(
                context, TAIYIN_BODY_SUN, &nan_fraction_jd,
                TAIYIN_POSITION_XYZ, unnormalized_position, NULL)
                != TAIYIN_ERROR_INVALID_ARGUMENT
            || taiyin_calc_position_tt(
                context, TAIYIN_BODY_SUN, &overflowing_jd,
                TAIYIN_POSITION_XYZ, unnormalized_position, NULL)
                != TAIYIN_ERROR_INVALID_ARGUMENT) {
            taiyin_context_destroy(context);
            return fail("split Julian-date normalization contract failed");
        }
        for (i = 0; i < 6; ++i) {
            if (canonical_position[i] != unnormalized_position[i]) {
                taiyin_context_destroy(context);
                return fail("unnormalized split Julian date changed position");
            }
        }
        if (taiyin_calc_position_tdb(
                context, TAIYIN_BODY_SUN, &jd_2460409, NULL,
                TAIYIN_POSITION_XYZ, scalar_tdb_position, NULL)
                != TAIYIN_STATUS_OK
            || taiyin_calc_positions_tdb(
                context, target_ids, 1, &jd_2460409, NULL,
                TAIYIN_POSITION_XYZ, batch_tdb_position, NULL)
                != TAIYIN_STATUS_OK) {
            taiyin_context_destroy(context);
            return fail("NULL TT fallback must match for scalar and batch TDB");
        }
        for (i = 0; i < 6; ++i) {
            if (scalar_tdb_position[i] != batch_tdb_position[i]) {
                taiyin_context_destroy(context);
                return fail("scalar and batch TDB fallback mismatch");
            }
        }
    }

    {
        const taiyin_split_julian_date overflowing_jd = {INT64_MAX, 1.0};
        const int32_t body_id = TAIYIN_BODY_SUN;
        double star_position[6] = {0.0};
        double ayanamsha_rad = 0.0;
        taiyin_split_julian_date converted_jd;
        taiyin_solar_eclipse_result_tt eclipse;
        taiyin_lunar_occultation_result occultation;
        taiyin_heliacal_visibility_conditions conditions;
        taiyin_heliacal_visibility_result heliacal;
        taiyin_equation_of_time_result equation_of_time;
        taiyin_body_phenomena phenomena;
        taiyin_observed_position observed;
        taiyin_solar_eclipse_result_tt_init(&eclipse);
        taiyin_lunar_occultation_result_init(&occultation);
        taiyin_heliacal_visibility_conditions_init(&conditions);
        taiyin_heliacal_visibility_result_init(&heliacal);
        taiyin_equation_of_time_result_init(&equation_of_time);
        taiyin_body_phenomena_init(&phenomena);
        taiyin_observed_position_init(&observed);

        if (taiyin_solve_solar_eclipse_at_tt(
                context, &invalid_jd, 0u, &eclipse, NULL)
                != TAIYIN_ERROR_INVALID_ARGUMENT
            || taiyin_search_next_geocentric_lunar_body_occultation_ut(
                context, TAIYIN_BODY_MERCURY_BARYCENTER, &invalid_jd,
                0u, &occultation, NULL) != TAIYIN_ERROR_INVALID_ARGUMENT
            || taiyin_calc_ayanamsha_tt(
                context, TAIYIN_C_AYANAMSHA_FAGAN_BRADLEY,
                &invalid_jd, 0u, &ayanamsha_rad)
                != TAIYIN_ERROR_INVALID_ARGUMENT
            || taiyin_calc_star_position_tt(
                context, "spica", &invalid_jd, TAIYIN_POSITION_XYZ,
                star_position, NULL) != TAIYIN_ERROR_INVALID_ARGUMENT
            || taiyin_calc_star_position_tt(
                context, "spica", &overflowing_jd, TAIYIN_POSITION_XYZ,
                star_position, NULL) != TAIYIN_ERROR_INVALID_ARGUMENT
            || taiyin_calc_body_heliacal_visibility_ut(
                context, TAIYIN_BODY_VENUS_BARYCENTER, &invalid_jd,
                0u, &conditions, &heliacal, NULL)
                != TAIYIN_ERROR_INVALID_ARGUMENT
            || taiyin_calc_equation_of_time_tt(
                context, &invalid_jd, &equation_of_time, NULL)
                != TAIYIN_ERROR_INVALID_ARGUMENT
            || taiyin_calc_body_phenomena_tt(
                context, TAIYIN_BODY_MARS_BARYCENTER, &invalid_jd,
                0u, &phenomena, NULL) != TAIYIN_ERROR_INVALID_ARGUMENT
            || taiyin_calc_observed_bodies_ut(
                context, &invalid_jd, &body_id, 1, 0u, &observed, NULL)
                != TAIYIN_ERROR_INVALID_ARGUMENT
            || taiyin_context_set_simple_topocentric_observer(
                context, &observer, &invalid_jd, &jd_2460409)
                != TAIYIN_ERROR_INVALID_ARGUMENT
            || taiyin_utc_to_tai_split(&invalid_jd, 37.0, &converted_jd)
                != TAIYIN_ERROR_INVALID_ARGUMENT) {
            taiyin_context_destroy(context);
            return fail("C modules must reject malformed split Julian dates");
        }
    }

    {
        taiyin_calendar_datetime utc;
        taiyin_precise_time_scales scales;
        taiyin_split_precise_time_scales split_scales;
        taiyin_time_scale_diagnostic time_diagnostic;
        double tai_minus_utc = 0.0;
        double split_delta_t = 0.0;
        taiyin_calendar_datetime_init(&utc);
        taiyin_precise_time_scales_init(&scales);
        taiyin_split_precise_time_scales_init(&split_scales);
        taiyin_time_scale_diagnostic_init(&time_diagnostic);
        utc.year = 2020;
        utc.month = 1;
        utc.day = 1;
        utc.hour = 18;
        if (taiyin_tai_minus_utc_seconds(&utc, &tai_minus_utc)
                != TAIYIN_STATUS_OK
            || fabs(tai_minus_utc - 37.0) > 1.0e-12
            || taiyin_make_time_scales_from_utc(
                    context, &utc, &scales, &time_diagnostic)
                != TAIYIN_STATUS_OK
            || time_diagnostic.route != TAIYIN_TIME_ROUTE_PRECISE_UTC_EOP
            || (time_diagnostic.flags & (
                    TAIYIN_TIME_USED_LEAP_SECONDS | TAIYIN_TIME_USED_EOP))
                != (TAIYIN_TIME_USED_LEAP_SECONDS | TAIYIN_TIME_USED_EOP)
            || !isfinite(scales.jd_tt)
            || taiyin_make_split_time_scales_from_utc(
                    context, &utc, &split_scales, &time_diagnostic)
                != TAIYIN_STATUS_OK
            || taiyin_seconds_between_split_jd(
                    &split_scales.ut1,
                    &split_scales.tt,
                    &split_delta_t) != TAIYIN_STATUS_OK
            || fabs(split_delta_t - split_scales.delta_t_seconds) > 1.0e-11
            || fabs(taiyin_seconds_between_jd(
                    scales.jd_utc,
                    taiyin_add_seconds_to_jd(scales.jd_utc, 10.0))
                    - 10.0) > 1.0e-4
            || fabs(taiyin_julian_centuries_from_j2000(2451545.0))
                > 1.0e-15) {
            taiyin_context_destroy(context);
            return fail("precise time-scale C API failed");
        }
    }

    {
        taiyin_context* configured = NULL;
        taiyin_astro_model_config models;
        taiyin_apparent_config apparent;
        taiyin_apparent_deflector deflector;
        taiyin_astro_model_config_init(&models);
        taiyin_apparent_config_init(&apparent);
        taiyin_apparent_deflector_init(&deflector);
        if ((apparent.flags & TAIYIN_APPARENT_FLAG_LIGHT_TIME) == 0u
            || (apparent.flags & TAIYIN_APPARENT_FLAG_ABERRATION) == 0u
            || (apparent.flags & TAIYIN_APPARENT_FLAG_DEFLECTION) == 0u
            || (apparent.flags & TAIYIN_APPARENT_FLAG_SHAPIRO_DELAY) != 0u) {
            taiyin_context_destroy(context);
            return fail("default apparent C config corrections");
        }
        models.precession_model_id = TAIYIN_PRECESSION_IAU_2006;
        models.nutation_model_id = TAIYIN_NUTATION_IAU_2000A;
        apparent.output_frame_id = TAIYIN_FRAME_TRUE_EQUATOR_OF_DATE;
        apparent.flags = TAIYIN_APPARENT_FLAG_LIGHT_TIME
            | TAIYIN_APPARENT_FLAG_SPHERICAL;
        deflector.body_id = TAIYIN_BODY_SUN;
        deflector.schwarzschild_radius_au = 1.0e-8;
        deflector.limit = 0.0;

        if (taiyin_context_clone(context, &configured) != TAIYIN_STATUS_OK
            || !configured
            || taiyin_context_set_astro_models(configured, &models)
                != TAIYIN_STATUS_OK
            || taiyin_context_set_apparent_config(configured, &apparent)
                != TAIYIN_STATUS_OK
            || taiyin_context_set_celestial_pole_offset(
                    configured, 1.0e-10, -2.0e-10, 0.0, 0.0)
                != TAIYIN_STATUS_OK
            || taiyin_context_set_delta_t_model(
                    configured,
                    TAIYIN_DELTA_T_ESTIMATED_DEFAULT,
                    TAIYIN_EPHEMERIS_FAMILY_DE441)
                != TAIYIN_STATUS_OK
            || taiyin_context_set_refraction_model(
                    configured, TAIYIN_REFRACTION_SOFA)
                != TAIYIN_STATUS_OK
            || taiyin_context_set_heliacal_visibility_model(
                    configured, TAIYIN_HELIACAL_VISIBILITY_SCHAEFER_1993)
                != TAIYIN_STATUS_OK
            || taiyin_context_set_eclipse_models(
                    configured,
                    TAIYIN_ECLIPSE_SHADOW_NASA_DANJON,
                    TAIYIN_ECLIPSE_MOON_ALMANAC)
                != TAIYIN_STATUS_OK
            || taiyin_context_set_simple_topocentric_observer(
                    configured, &observer, &jd_2460409, &jd_2460409_0008)
                != TAIYIN_STATUS_OK
            || taiyin_context_set_deflectors(configured, &deflector, 1, 0)
                != TAIYIN_STATUS_OK
            || taiyin_context_set_light_time_iteration(
                    configured, 6, 1.0e-12)
                != TAIYIN_STATUS_OK
            || taiyin_context_enable_shapiro_delay(configured, 0)
                != TAIYIN_STATUS_OK
            || taiyin_context_disable_shapiro_delay(configured)
                != TAIYIN_STATUS_OK) {
            taiyin_context_destroy(configured);
            taiyin_context_destroy(context);
            return fail("advanced context configuration failed");
        }

        models.tdb_model_id = 99;
        if (taiyin_context_set_tdb_model(configured, 99)
                != TAIYIN_ERROR_INVALID_ARGUMENT
            || taiyin_context_set_astro_models(configured, &models)
                != TAIYIN_ERROR_INVALID_ARGUMENT) {
            taiyin_context_destroy(configured);
            taiyin_context_destroy(context);
            return fail("invalid TDB model must be rejected");
        }
        models.tdb_model_id = TAIYIN_TDB_FAST_PERIODIC;

        apparent.output_frame_id = 999;
        if (taiyin_context_set_apparent_config(configured, &apparent)
            != TAIYIN_ERROR_INVALID_ARGUMENT) {
            taiyin_context_destroy(configured);
            taiyin_context_destroy(context);
            return fail("invalid apparent frame must be rejected");
        }
        apparent.output_frame_id = TAIYIN_FRAME_TRUE_EQUATOR_OF_DATE;
        apparent.flags = TAIYIN_APPARENT_FLAG_LIGHT_TIME
            | TAIYIN_APPARENT_FLAG_SPHERICAL
            | (1u << 1); /* Internal matrix bit is not part of the C ABI. */
        if (taiyin_context_set_apparent_config(configured, &apparent)
                != TAIYIN_ERROR_INVALID_ARGUMENT
            || taiyin_context_set_delta_t_model(
                    configured, 999, TAIYIN_EPHEMERIS_FAMILY_UNKNOWN)
                != TAIYIN_ERROR_INVALID_ARGUMENT
            || taiyin_context_set_delta_t_model(
                    configured, TAIYIN_DELTA_T_ESTIMATED_DEFAULT, 999)
                != TAIYIN_ERROR_INVALID_ARGUMENT) {
            taiyin_context_destroy(configured);
            taiyin_context_destroy(context);
            return fail("unsupported apparent and delta-T models must be rejected");
        }

        {
            taiyin_cartesian_state invalid_offset;
            taiyin_cartesian_state_init(&invalid_offset);
            invalid_offset.position_au.x = NAN;
            if (taiyin_context_set_topocentric_observer_offset(
                    configured, &invalid_offset)
                    != TAIYIN_ERROR_INVALID_ARGUMENT
                || taiyin_context_set_simple_topocentric_observer(
                    configured, &observer, &invalid_jd, &jd_2460409_0008)
                    != TAIYIN_ERROR_INVALID_ARGUMENT
                || taiyin_context_set_light_time_iteration(
                    configured, 6, NAN)
                    != TAIYIN_ERROR_INVALID_ARGUMENT
                || taiyin_context_enable_shapiro_delay(configured, 99)
                    != TAIYIN_ERROR_INVALID_ARGUMENT
                || taiyin_context_set_eclipse_models(
                    configured, 99, TAIYIN_ECLIPSE_MOON_ALMANAC)
                    != TAIYIN_ERROR_INVALID_ARGUMENT
                || taiyin_context_set_eclipse_models(
                    configured, TAIYIN_ECLIPSE_SHADOW_NASA_DANJON, 99)
                    != TAIYIN_ERROR_INVALID_ARGUMENT) {
                taiyin_context_destroy(configured);
                taiyin_context_destroy(context);
                return fail("invalid advanced context values must be rejected");
            }
        }

        taiyin_context* configured_clone = NULL;
        if (taiyin_context_clone(configured, &configured_clone)
                != TAIYIN_STATUS_OK
            || !configured_clone) {
            taiyin_context_destroy(configured);
            taiyin_context_destroy(context);
            return fail("configured context clone failed");
        }
        taiyin_context_destroy(configured);

        double sun[6] = {0.0, 0.0, 0.0, 0.0, 0.0, 0.0};
        if (taiyin_calc_position_tt(
                configured_clone,
                TAIYIN_BODY_SUN,
                &jd_2460409,
                TAIYIN_POSITION_XYZ,
                sun,
                NULL) != TAIYIN_STATUS_OK
            || !isfinite(sun[0])) {
            taiyin_context_destroy(configured_clone);
            taiyin_context_destroy(context);
            return fail("configured context clone lost owned state");
        }
        taiyin_context_destroy(configured_clone);
    }

    {
        double moon[6] = {0.0, 0.0, 0.0, 0.0, 0.0, 0.0};
        taiyin_ephemeris_diagnostic diagnostic;
        taiyin_ephemeris_diagnostic_init(&diagnostic);
        taiyin_status position_status = taiyin_calc_position_tt(
                context,
                TAIYIN_BODY_MOON,
                &jd_2460409,
                TAIYIN_POSITION_XYZ | TAIYIN_POSITION_TRUEPOS,
                moon,
                &diagnostic);
        if (position_status != TAIYIN_STATUS_OK
            || !isfinite(moon[0])
            || !isfinite(moon[1])
            || !isfinite(moon[2])) {
            fprintf(
                stderr,
                "position status=%d (%s), target=%d center=%d method=%d\n",
                position_status,
                taiyin_status_name(position_status),
                diagnostic.target_id,
                diagnostic.center_id,
                diagnostic.attempted_method_id);
            taiyin_context_destroy(context);
            return fail("native position calculation failed");
        }
    }

    {
        int32_t body_id = TAIYIN_BODY_SUN;
        taiyin_observed_position observed;
        taiyin_observed_position_init(&observed);
        if (taiyin_calc_observed_bodies_ut(
                context,
                &jd_2460409,
                &body_id,
                1,
                TAIYIN_OBSERVED_TRUEPOS,
                &observed,
                NULL) != TAIYIN_STATUS_OK
            || !isfinite(observed.apparent.longitude_rad)) {
            taiyin_context_destroy(context);
            return fail("observed position calculation failed");
        }
    }

    {
        const char* star_keys[2] = {"spica", "antares"};
        const char* invalid_star_keys[2] = {"spica", NULL};
        double star_positions[12] = {0.0};
        double scalar_tdb_position[6] = {0.0};
        double batch_tdb_position[6] = {0.0};
        taiyin_observed_position observed_stars[2];
        taiyin_ephemeris_diagnostic diagnostics[2];
        taiyin_split_julian_date body_star_start;
        taiyin_split_julian_date body_star_end;
        taiyin_body_star_angular_separation_result body_star_minimum;
        taiyin_observed_position_init(&observed_stars[0]);
        taiyin_observed_position_init(&observed_stars[1]);
        taiyin_ephemeris_diagnostic_init(&diagnostics[0]);
        taiyin_ephemeris_diagnostic_init(&diagnostics[1]);
        taiyin_body_star_angular_separation_result_init(&body_star_minimum);
        if (taiyin_split_julian_date_from_parts(
                2460634, 0.5, &body_star_start) != TAIYIN_STATUS_OK
            || taiyin_split_julian_date_from_parts(
                2460654, 0.5, &body_star_end) != TAIYIN_STATUS_OK
            || taiyin_search_minimum_body_star_angular_separation_ut(
                context,
                TAIYIN_BODY_SUN,
                "antares",
                &body_star_start,
                &body_star_end,
                1.0,
                0u,
                &body_star_minimum,
                NULL) != TAIYIN_STATUS_OK
            || body_star_minimum.body_id != TAIYIN_BODY_SUN
            || !isfinite(body_star_minimum.separation_rad)
            || !(body_star_minimum.jd.day_number > body_star_start.day_number
                || (body_star_minimum.jd.day_number == body_star_start.day_number
                    && body_star_minimum.jd.day_fraction > body_star_start.day_fraction))
            || taiyin_search_minimum_body_star_angular_separation_ut(
                context,
                TAIYIN_BODY_SUN,
                "",
                &body_star_start,
                &body_star_end,
                1.0,
                0u,
                &body_star_minimum,
                NULL) != TAIYIN_ERROR_INVALID_ARGUMENT) {
            taiyin_context_destroy(context);
            return fail("body-star angular-separation C API failed");
        }
        if (taiyin_calc_star_positions_tt(
                context,
                star_keys,
                2,
                &jd_2460409,
                TAIYIN_POSITION_XYZ | TAIYIN_POSITION_TRUEPOS,
                star_positions,
                diagnostics) != TAIYIN_STATUS_OK
            || !isfinite(star_positions[0])
            || !isfinite(star_positions[6])) {
            taiyin_context_destroy(context);
            return fail("TT batch star position calculation failed");
        }
        if (taiyin_calc_star_position_tdb(
                context, star_keys[0], &jd_2460409, NULL,
                TAIYIN_POSITION_XYZ | TAIYIN_POSITION_TRUEPOS,
                scalar_tdb_position, NULL) != TAIYIN_STATUS_OK
            || taiyin_calc_star_positions_tdb(
                context, star_keys, 1, &jd_2460409, NULL,
                TAIYIN_POSITION_XYZ | TAIYIN_POSITION_TRUEPOS,
                batch_tdb_position, NULL) != TAIYIN_STATUS_OK
            || memcmp(
                scalar_tdb_position,
                batch_tdb_position,
                sizeof(scalar_tdb_position)) != 0) {
            taiyin_context_destroy(context);
            return fail("star TDB NULL TT fallback mismatch");
        }
        if (taiyin_calc_star_positions_tt(
                context,
                invalid_star_keys,
                2,
                &jd_2460409,
                TAIYIN_POSITION_XYZ,
                star_positions,
                NULL) != TAIYIN_ERROR_INVALID_ARGUMENT
            || taiyin_calc_star_positions_tt(
                context,
                star_keys,
                2,
                &jd_2460409,
                UINT64_C(1) << 40,
                star_positions,
                NULL) != TAIYIN_ERROR_UNSUPPORTED
            || taiyin_calc_observed_stars_ut(
                context,
                invalid_star_keys,
                2,
                &jd_2460409,
                0u,
                observed_stars,
                NULL) != TAIYIN_ERROR_INVALID_ARGUMENT) {
            taiyin_context_destroy(context);
            return fail("batch star flags and keys were not validated");
        }
    }

    {
        static double custom_position_offset = 0.25;
        double custom_position[6] = {0.0, 0.0, 0.0, 0.0, 0.0, 0.0};
        if (taiyin_register_native_position_evaluator(
                -200001,
                &test_position_evaluator,
                NULL,
                &custom_position_offset) != TAIYIN_STATUS_OK
            || taiyin_calc_position_tt(
                context,
                -200001,
                &jd_2460409,
                TAIYIN_POSITION_XYZ,
                custom_position,
                NULL) != TAIYIN_STATUS_OK
            || fabs(custom_position[0] - 1.25) > 1.0e-15) {
            taiyin_context_destroy(context);
            return fail("custom position evaluator failed");
        }
        if (taiyin_register_native_position_evaluator(
                -200001,
                &test_position_evaluator,
                NULL,
                &custom_position_offset) != TAIYIN_ERROR_INVALID_ARGUMENT
            || taiyin_unregister_native_position_evaluator(-200001)
                != TAIYIN_STATUS_OK
            || taiyin_unregister_native_position_evaluator(-200001)
                != TAIYIN_ERROR_INVALID_ARGUMENT
            || taiyin_calc_position_tt(
                context,
                -200001,
                &jd_2460409,
                TAIYIN_POSITION_XYZ,
                custom_position,
                NULL) == TAIYIN_STATUS_OK) {
            taiyin_context_destroy(context);
            return fail("custom position evaluator unregister failed");
        }
        if (taiyin_register_native_position_evaluator(
                -200001,
                &test_position_evaluator,
                NULL,
                &custom_position_offset) != TAIYIN_STATUS_OK) {
            taiyin_context_destroy(context);
            return fail("custom position evaluator re-registration failed");
        }
        taiyin_clear_native_position_evaluators();
        if (taiyin_calc_position_tt(
                context,
                -200001,
                &jd_2460409,
                TAIYIN_POSITION_XYZ,
                custom_position,
                NULL) == TAIYIN_STATUS_OK) {
            taiyin_context_destroy(context);
            return fail("custom position evaluator clear failed");
        }
        if (taiyin_register_native_position_evaluator(
                -200001,
                &test_position_evaluator,
                NULL,
                &custom_position_offset) != TAIYIN_STATUS_OK
            || taiyin_runtime_initialize(&runtime_config) != TAIYIN_STATUS_OK
            || taiyin_calc_position_tt(
                context,
                -200001,
                &jd_2460409,
                TAIYIN_POSITION_XYZ,
                custom_position,
                NULL) == TAIYIN_STATUS_OK) {
            taiyin_context_destroy(context);
            return fail("runtime reset did not clear custom evaluator");
        }
        {
            taiyin_runtime_config failing_runtime_config = runtime_config;
            failing_runtime_config.eop_path =
                "/__taiyin_missing__/finals2000A.all";
            if (taiyin_register_native_position_evaluator(
                    -200001,
                    &test_position_evaluator,
                    NULL,
                    &custom_position_offset) != TAIYIN_STATUS_OK
                || taiyin_runtime_initialize(&failing_runtime_config)
                    != TAIYIN_ERROR_INTERNAL
                || taiyin_unregister_native_position_evaluator(-200001)
                    != TAIYIN_ERROR_INVALID_ARGUMENT
                || taiyin_calc_position_tt(
                    context,
                    -200001,
                    &jd_2460409,
                    TAIYIN_POSITION_XYZ,
                    custom_position,
                    NULL) == TAIYIN_STATUS_OK
                || taiyin_runtime_initialize(&runtime_config)
                    != TAIYIN_STATUS_OK) {
                taiyin_context_destroy(context);
                return fail(
                    "failed runtime reset retained custom evaluator");
            }
        }
    }

    {
        static double custom_ayanamsha = 0.123;
        static double custom_house_offset = 0.01;
        double ayanamsha = 0.0;
        uint64_t ayanamsha_token = 0;
        uint64_t house_token = 0;
        taiyin_house_result houses;
        taiyin_house_result_init(&houses);
        if (taiyin_unregister_ayanamsha_model(0)
                != TAIYIN_ERROR_INVALID_ARGUMENT
            || taiyin_unregister_house_system_model(0)
                != TAIYIN_ERROR_INVALID_ARGUMENT
            || taiyin_register_ayanamsha_model_with_token(
                10001,
                NULL,
                -1,
                &custom_ayanamsha,
                &ayanamsha_token) != TAIYIN_ERROR_INVALID_ARGUMENT
            || taiyin_register_house_system_model_with_token(
                10001,
                NULL,
                -1,
                &custom_house_offset,
                &house_token) != TAIYIN_ERROR_INVALID_ARGUMENT
            || taiyin_register_ayanamsha_model_with_token(
                10001,
                &test_ayanamsha_evaluator,
                -1,
                &custom_ayanamsha,
                &ayanamsha_token) != TAIYIN_STATUS_OK
            || ayanamsha_token == 0
            || taiyin_calc_ayanamsha_tt(
                context,
                10001,
                &jd_2460409,
                TAIYIN_C_SIDEREAL_RAW_REFERENCE_OFFSET
                    | TAIYIN_POSITION_NONUT,
                &ayanamsha) != TAIYIN_STATUS_OK
            || fabs(ayanamsha - custom_ayanamsha) > 1.0e-15
            || taiyin_register_house_system_model_with_token(
                10001,
                &test_house_evaluator,
                -1,
                &custom_house_offset,
                &house_token) != TAIYIN_STATUS_OK
            || house_token == 0
            || taiyin_calc_houses_from_armc(
                1.0,
                0.5,
                0.409,
                10001,
                &houses) != TAIYIN_STATUS_OK
            || houses.resolved_system_id != 10001
            || !isfinite(houses.cusp_longitude_rad[0])
            || taiyin_unregister_ayanamsha_model_with_token(
                10001, ayanamsha_token + 1)
                != TAIYIN_ERROR_INVALID_ARGUMENT
            || taiyin_unregister_house_system_model_with_token(
                10001, house_token + 1)
                != TAIYIN_ERROR_INVALID_ARGUMENT
            || taiyin_unregister_ayanamsha_model_with_token(
                10001, ayanamsha_token)
                != TAIYIN_STATUS_OK
            || taiyin_unregister_house_system_model_with_token(
                10001, house_token)
                != TAIYIN_STATUS_OK
            || taiyin_has_ayanamsha_model(10001)
            || taiyin_has_house_system_model(10001)
            || taiyin_unregister_ayanamsha_model(10001)
                != TAIYIN_ERROR_INVALID_ARGUMENT
            || taiyin_unregister_house_system_model(10001)
                != TAIYIN_ERROR_INVALID_ARGUMENT) {
            taiyin_context_destroy(context);
            return fail("custom astrology model token lifecycle failed");
        }
        taiyin_clear_ayanamsha_models();
        taiyin_clear_house_system_models();
    }

    {
        static double custom_ayanamsha = 0.456;
        static double custom_house_offset = 0.02;
        if (taiyin_register_ayanamsha_model(
                10002,
                &test_ayanamsha_evaluator,
                -1,
                &custom_ayanamsha) != TAIYIN_STATUS_OK
            || taiyin_register_house_system_model(
                10002,
                &test_house_evaluator,
                -1,
                &custom_house_offset) != TAIYIN_STATUS_OK
            || !taiyin_has_ayanamsha_model(10002)
            || !taiyin_has_house_system_model(10002)
            || taiyin_register_ayanamsha_model(
                10002,
                &test_ayanamsha_evaluator,
                -1,
                &custom_ayanamsha) != TAIYIN_ERROR_INVALID_ARGUMENT
            || taiyin_register_house_system_model(
                10002,
                &test_house_evaluator,
                -1,
                &custom_house_offset) != TAIYIN_ERROR_INVALID_ARGUMENT) {
            taiyin_context_destroy(context);
            return fail("custom astrology model duplicate registration failed");
        }
        taiyin_clear_ayanamsha_models();
        taiyin_clear_house_system_models();
        if (taiyin_has_ayanamsha_model(10002)
            || taiyin_has_house_system_model(10002)
            || taiyin_register_ayanamsha_model(
                10002,
                &test_ayanamsha_evaluator,
                -1,
                &custom_ayanamsha) != TAIYIN_STATUS_OK
            || taiyin_register_house_system_model(
                10002,
                &test_house_evaluator,
                -1,
                &custom_house_offset) != TAIYIN_STATUS_OK
            || taiyin_runtime_initialize(&runtime_config) != TAIYIN_STATUS_OK
            || taiyin_has_ayanamsha_model(10002)
            || taiyin_has_house_system_model(10002)) {
            taiyin_context_destroy(context);
            return fail("custom astrology model clear/reset failed");
        }
    }

    {
        taiyin_sidereal_coordinates ecliptic;
        taiyin_sidereal_coordinates equatorial;
        taiyin_sidereal_coordinates equatorial_xyz;
        taiyin_sidereal_coordinates_init(&ecliptic);
        taiyin_sidereal_coordinates_init(&equatorial);
        taiyin_sidereal_coordinates_init(&equatorial_xyz);
        if (taiyin_calc_sidereal_coordinates_tt(
                context,
                TAIYIN_C_AYANAMSHA_FAGAN_BRADLEY,
                TAIYIN_BODY_SUN,
                &jd_2460409,
                TAIYIN_POSITION_SPEED | TAIYIN_POSITION_RADIANS,
                NULL,
                &ecliptic,
                NULL) != TAIYIN_STATUS_OK
            || ecliptic.struct_size != sizeof(ecliptic)
            || ecliptic.coordinate_frame_id
                != TAIYIN_C_SIDEREAL_FRAME_MEAN_ECLIPTIC_OF_DATE
            || !isfinite(ecliptic.values[0])
            || !isfinite(ecliptic.values[3])
            || taiyin_calc_sidereal_coordinates_ut(
                context,
                TAIYIN_C_AYANAMSHA_FAGAN_BRADLEY,
                TAIYIN_BODY_SUN,
                &jd_2460409,
                TAIYIN_POSITION_EQUATORIAL | TAIYIN_POSITION_SPEED
                    | TAIYIN_POSITION_RADIANS | TAIYIN_POSITION_NONUT,
                NULL,
                &equatorial,
                NULL) != TAIYIN_STATUS_OK
            || equatorial.coordinate_frame_id
                != TAIYIN_C_SIDEREAL_FRAME_MEAN_EQUATOR_OF_DATE
            || !isfinite(equatorial.values[0])
            || taiyin_calc_sidereal_coordinates_tt(
                context,
                TAIYIN_C_AYANAMSHA_FAGAN_BRADLEY,
                TAIYIN_BODY_SUN,
                &jd_2460409,
                TAIYIN_POSITION_EQUATORIAL | TAIYIN_POSITION_XYZ,
                NULL,
                &equatorial_xyz,
                NULL) != TAIYIN_STATUS_OK
            || equatorial_xyz.coordinate_frame_id
                != TAIYIN_C_SIDEREAL_FRAME_TRUE_EQUATOR_OF_DATE
            || !isfinite(equatorial_xyz.values[0])) {
            taiyin_context_destroy(context);
            return fail("generic sidereal-coordinate C ABI failed");
        }
    }

    {
        taiyin_sidereal_position position;
        taiyin_sidereal_coordinates coordinates;
        const taiyin_split_julian_date reference_epoch = {2451545, 1.0e-10};
        const taiyin_split_julian_date invalid_reference_epoch = {2451545, NAN};
        taiyin_sidereal_position_init(&position);
        taiyin_sidereal_coordinates_init(&coordinates);
        if (taiyin_calc_sidereal_position_tt(
                context,
                TAIYIN_C_AYANAMSHA_FAGAN_BRADLEY,
                TAIYIN_BODY_SUN,
                &jd_2460409,
                TAIYIN_POSITION_SPEED | TAIYIN_POSITION_RADIANS
                    | TAIYIN_C_SIDEREAL_REFERENCE_J2000_ECLIPTIC,
                NULL,
                &position,
                NULL) != TAIYIN_STATUS_OK
            || position.coordinate_frame_id != TAIYIN_C_SIDEREAL_FRAME_J2000_ECLIPTIC
            || taiyin_calc_sidereal_coordinates_tt(
                context,
                TAIYIN_C_AYANAMSHA_FAGAN_BRADLEY,
                TAIYIN_BODY_SUN,
                &jd_2460409,
                TAIYIN_POSITION_SPEED | TAIYIN_POSITION_RADIANS
                    | TAIYIN_C_SIDEREAL_REFERENCE_J2000_ECLIPTIC,
                NULL,
                &coordinates,
                NULL) != TAIYIN_STATUS_OK
            || coordinates.coordinate_frame_id != TAIYIN_C_SIDEREAL_FRAME_J2000_ECLIPTIC
            || fabs(position.sidereal_longitude_rad - coordinates.values[0]) > 1.0e-14) {
            taiyin_context_destroy(context);
            return fail("sidereal reference-plane C ABI failed");
        }

        if (taiyin_calc_sidereal_position_tt(
                context,
                TAIYIN_C_AYANAMSHA_FAGAN_BRADLEY,
                TAIYIN_BODY_SUN,
                &jd_2460409,
                TAIYIN_POSITION_RADIANS | TAIYIN_C_SIDEREAL_REFERENCE_ECL_T0,
                NULL,
                &position,
                NULL) != TAIYIN_ERROR_INVALID_ARGUMENT) {
            taiyin_context_destroy(context);
            return fail("invalid sidereal reference-plane C ABI accepted missing epoch");
        }
        if (taiyin_calc_sidereal_position_tt(
                context,
                TAIYIN_C_AYANAMSHA_FAGAN_BRADLEY,
                TAIYIN_BODY_SUN,
                &jd_2460409,
                TAIYIN_POSITION_RADIANS | TAIYIN_C_SIDEREAL_REFERENCE_ECL_T0,
                &reference_epoch,
                &position,
                NULL) != TAIYIN_STATUS_OK
            || !isfinite(position.sidereal_longitude_rad)) {
            taiyin_context_destroy(context);
            return fail("split sidereal reference epoch C ABI failed");
        }
        if (taiyin_calc_sidereal_position_tt(
                context,
                TAIYIN_C_AYANAMSHA_FAGAN_BRADLEY,
                TAIYIN_BODY_SUN,
                &jd_2460409,
                TAIYIN_POSITION_RADIANS | TAIYIN_C_SIDEREAL_REFERENCE_ECL_T0,
                &invalid_reference_epoch,
                &position,
                NULL) != TAIYIN_ERROR_INVALID_ARGUMENT) {
            taiyin_context_destroy(context);
            return fail("invalid split sidereal reference epoch C ABI accepted");
        }
    }

    {
        taiyin_house_result houses;
        taiyin_house_result_init(&houses);
        houses.resolved_system_id = 777;
        houses.cusp_longitude_rad[0] = 42.0;
        if (taiyin_calc_houses_from_armc(
                1.0,
                0.5,
                0.409,
                9999,
                &houses) == TAIYIN_STATUS_OK
            || houses.resolved_system_id != 777
            || houses.cusp_longitude_rad[0] != 42.0) {
            taiyin_context_destroy(context);
            return fail("failed house calculation modified its output");
        }
    }

    {
        taiyin_solar_eclipse_result_ut eclipse;
        taiyin_solar_eclipse_result_ut_init(&eclipse);
        if (taiyin_solve_solar_eclipse_at_ut(
                context,
                &jd_2460409_25,
                TAIYIN_ECLIPSE_OPTION_INCLUDE_CONTACTS,
                &eclipse,
                NULL) != TAIYIN_STATUS_OK
            || eclipse.kind == TAIYIN_C_ECLIPSE_NONE
            || !isfinite(eclipse.maximum_jd_ut.day_fraction)) {
            taiyin_context_destroy(context);
            return fail("solar eclipse C ABI calculation failed");
        }
        {
            taiyin_solar_eclipse_where where;
            taiyin_solar_eclipse_where_init(&where);
            if (taiyin_compute_solar_eclipse_where_ut(
                    context, &eclipse.maximum_jd_ut, 0u, &where, NULL)
                    != TAIYIN_STATUS_OK
                || !isfinite(where.center_line.latitude_deg)
                || !isfinite(where.center_line.longitude_deg)
                || !isfinite(where.penumbral_north_limit.latitude_deg)
                || !isfinite(where.penumbral_south_limit.latitude_deg)
                || !isfinite(where.north_limit.latitude_deg)
                || !isfinite(where.south_limit.latitude_deg)
                || !isfinite(where.magnitude)
                || where.center_line.struct_size != sizeof(where.center_line)) {
                taiyin_context_destroy(context);
                return fail("lightweight solar eclipse C ABI calculation failed");
            }
        }
        {
            taiyin_solar_eclipse_route_curve_point curve_points[1024];
            size_t required_curve_count = 0;
            size_t curve_count = 0;
            const taiyin_status count_status =
                taiyin_compute_solar_eclipse_route_curves_ut(
                    context,
                    &eclipse.maximum_jd_ut,
                    0u,
                    32,
                    NULL,
                    0,
                    &required_curve_count,
                    NULL);
            const taiyin_status fill_status =
                required_curve_count > 0 && required_curve_count <= 1024
                ? taiyin_compute_solar_eclipse_route_curves_ut(
                    context,
                    &eclipse.maximum_jd_ut,
                    0u,
                    32,
                    curve_points,
                    1024,
                    &curve_count,
                    NULL)
                : TAIYIN_ERROR_OUT_OF_MEMORY;
            if (count_status != TAIYIN_STATUS_OK
                || required_curve_count == 0
                || required_curve_count > 1024
                || fill_status != TAIYIN_STATUS_OK
                || curve_count != required_curve_count
                || curve_points[0].struct_size != sizeof(curve_points[0])) {
                fprintf(
                    stderr,
                    "route curve ABI: count_status=%d required=%zu "
                    "fill_status=%d count=%zu struct_size=%u expected=%zu\n",
                    (int) count_status,
                    required_curve_count,
                    (int) fill_status,
                    curve_count,
                    (unsigned) curve_points[0].struct_size,
                    sizeof(curve_points[0]));
                taiyin_context_destroy(context);
                return fail("route curve array ABI failed");
            }
        }
        {
            taiyin_solar_eclipse_route_product_summary count_summary;
            taiyin_solar_eclipse_route_product_summary small_summary;
            taiyin_solar_eclipse_route_product_point point;
            size_t required_product_count = 0;
            size_t small_product_count = 0;
            taiyin_solar_eclipse_route_product_summary_init(&count_summary);
            taiyin_solar_eclipse_route_product_summary_init(&small_summary);
            memset(&point, 0xa5, sizeof(point));
            if (taiyin_compute_solar_eclipse_route_product_ut(
                    context,
                    &eclipse.maximum_jd_ut,
                    0u,
                    32,
                    NULL,
                    0,
                    &required_product_count,
                    &count_summary,
                    NULL) != TAIYIN_STATUS_OK
                || required_product_count <= 1
                || count_summary.polygon_point_count != required_product_count
                || taiyin_compute_solar_eclipse_route_product_ut(
                    context,
                    &eclipse.maximum_jd_ut,
                    0u,
                    32,
                    &point,
                    1,
                    &small_product_count,
                    &small_summary,
                    NULL) != TAIYIN_ERROR_OUT_OF_MEMORY
                || small_product_count != required_product_count
                || small_summary.polygon_point_count != required_product_count
                || point.struct_size != 0xa5a5a5a5u) {
                taiyin_context_destroy(context);
                return fail("route product small-buffer contract failed");
            }
        }
    }

    {
        taiyin_split_julian_date phase_events[4];
        size_t phase_count = 0;
        if (taiyin_search_lunar_phase_crossings_tt(
                context,
                1.57079632679489661923,
                &jd_2460380_5,
                &jd_2460395_5,
                0.5,
                0u,
                phase_events,
                4,
                &phase_count,
                NULL) != TAIYIN_STATUS_OK
            || phase_count != 1
            || !isfinite(phase_events[0].day_fraction)) {
            taiyin_context_destroy(context);
            return fail("TT lunar phase search failed");
        }
    }

    taiyin_context* clone = NULL;
    if (taiyin_context_clone(context, &clone) != TAIYIN_STATUS_OK || !clone) {
        taiyin_context_destroy(context);
        return fail("context clone failed");
    }

    observer.latitude_deg = 91.0;
    if (taiyin_context_set_observer_location(clone, &observer)
        != TAIYIN_ERROR_INVALID_ARGUMENT) {
        taiyin_context_destroy(clone);
        taiyin_context_destroy(context);
        return fail("invalid latitude was accepted");
    }

    taiyin_cartesian_state state;
    taiyin_cartesian_state_init(&state);
    if (state.struct_size != sizeof(state)) {
        taiyin_context_destroy(clone);
        taiyin_context_destroy(context);
        return fail("state initializer failed");
    }

    if (!taiyin_runtime_has_eop_table()) {
        taiyin_context_destroy(clone);
        taiyin_context_destroy(context);
        return fail("runtime EOP table was not installed");
    }
    if (taiyin_runtime_load_eop_table(
            "/taiyin/this/eop/file/does/not/exist")
        != TAIYIN_FILE_ERROR_NOT_FOUND) {
        taiyin_context_destroy(clone);
        taiyin_context_destroy(context);
        return fail("missing EOP file status was not preserved");
    }
    taiyin_runtime_clear_eop_table();
    if (taiyin_runtime_has_eop_table()
        || taiyin_runtime_load_builtin_eop_table() != TAIYIN_STATUS_OK
        || !taiyin_runtime_has_eop_table()) {
        taiyin_context_destroy(clone);
        taiyin_context_destroy(context);
        return fail("runtime EOP lifecycle failed");
    }
    taiyin_runtime_clear_lunar_limb_model();
    if (taiyin_runtime_has_lunar_limb_model()) {
        taiyin_context_destroy(clone);
        taiyin_context_destroy(context);
        return fail("runtime lunar-limb clear failed");
    }

#ifdef TAIYIN_TEST_MODULAR_C_API
    if (taiyin_astrology_module_shutdown() != TAIYIN_STATUS_OK
        || taiyin_astrology_module_shutdown() != TAIYIN_STATUS_OK
        || taiyin_register_ayanamsha_model(
               10003, &test_ayanamsha_evaluator, -1, NULL)
               != TAIYIN_ERROR_INTERNAL) {
        taiyin_context_destroy(clone);
        taiyin_context_destroy(context);
        return fail("modular astrology shutdown lifecycle failed");
    }
#endif

    taiyin_context_destroy(clone);
    taiyin_context_destroy(context);
    return 0;
}
