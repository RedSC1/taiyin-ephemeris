#ifndef TAIYIN_C_TIME_H
#define TAIYIN_C_TIME_H

#include "taiyin/c/base.h"
#include "taiyin/c/context.h"

#ifdef __cplusplus
extern "C" {
#endif

enum taiyin_tdb_model {
    TAIYIN_TDB_MODEL_FAST_PERIODIC = 0,
    TAIYIN_TDB_MODEL_SOFA_FULL = 1
};

enum taiyin_time_scale_route {
    TAIYIN_TIME_ROUTE_NONE = 0,
    TAIYIN_TIME_ROUTE_PRECISE_UTC_EOP = 1,
    TAIYIN_TIME_ROUTE_ESTIMATED_DELTA_T = 2
};

enum taiyin_time_scale_fallback_reason {
    TAIYIN_TIME_FALLBACK_NONE = 0,
    TAIYIN_TIME_FALLBACK_NULL_EOP_TABLE = 1,
    TAIYIN_TIME_FALLBACK_EOP_OUT_OF_RANGE = 2,
    TAIYIN_TIME_FALLBACK_LEAP_SECOND_UNAVAILABLE = 3
};

enum taiyin_time_scale_diagnostic_flags {
    TAIYIN_TIME_USED_LEAP_SECONDS = 1u << 0,
    TAIYIN_TIME_USED_EOP = 1u << 1,
    TAIYIN_TIME_USED_DELTA_T_MODEL = 1u << 2
};

typedef struct taiyin_precise_time_scales {
    uint32_t struct_size;
    double jd_utc;
    double jd_tai;
    double jd_tt;
    double jd_ut1;
    double jd_tdb;
    double tai_minus_utc_seconds;
    double dut1_seconds;
    double delta_t_seconds;
} taiyin_precise_time_scales;

typedef struct taiyin_split_precise_time_scales {
    uint32_t struct_size;
    taiyin_split_julian_date utc;
    taiyin_split_julian_date tai;
    taiyin_split_julian_date tt;
    taiyin_split_julian_date ut1;
    taiyin_split_julian_date tdb;
    double tai_minus_utc_seconds;
    double dut1_seconds;
    double delta_t_seconds;
} taiyin_split_precise_time_scales;

typedef struct taiyin_time_scale_diagnostic {
    uint32_t struct_size;
    int32_t route;
    int32_t fallback_reason;
    uint32_t flags;
    int32_t tdb_model_id;
    int32_t delta_t_model_id;
    int32_t ephemeris_family_id;
    double tai_minus_utc_seconds;
    double dut1_seconds;
    double delta_t_seconds;
} taiyin_time_scale_diagnostic;

typedef struct taiyin_estimated_time_scales {
    uint32_t struct_size;
    double jd_ut1;
    double jd_tt;
    double jd_tdb;
    double delta_t_seconds;
} taiyin_estimated_time_scales;

typedef struct taiyin_split_estimated_time_scales {
    uint32_t struct_size;
    taiyin_split_julian_date ut1;
    taiyin_split_julian_date tt;
    taiyin_split_julian_date tdb;
    double delta_t_seconds;
} taiyin_split_estimated_time_scales;

TAIYIN_C_API void TAIYIN_C_CALL taiyin_precise_time_scales_init(
    taiyin_precise_time_scales* value
);
TAIYIN_C_API void TAIYIN_C_CALL taiyin_split_precise_time_scales_init(
    taiyin_split_precise_time_scales* value
);
TAIYIN_C_API void TAIYIN_C_CALL taiyin_time_scale_diagnostic_init(
    taiyin_time_scale_diagnostic* value
);
TAIYIN_C_API void TAIYIN_C_CALL taiyin_estimated_time_scales_init(
    taiyin_estimated_time_scales* value
);
TAIYIN_C_API void TAIYIN_C_CALL taiyin_split_estimated_time_scales_init(
    taiyin_split_estimated_time_scales* value
);
TAIYIN_C_API taiyin_status TAIYIN_C_CALL
taiyin_split_julian_date_from_parts(
    int64_t day_number,
    double day_fraction,
    taiyin_split_julian_date* out
);
TAIYIN_C_API taiyin_status TAIYIN_C_CALL
taiyin_split_julian_date_from_double(
    double jd,
    taiyin_split_julian_date* out
);
TAIYIN_C_API taiyin_status TAIYIN_C_CALL
taiyin_split_julian_date_to_double(
    const taiyin_split_julian_date* jd,
    double* out_jd
);
TAIYIN_C_API taiyin_status TAIYIN_C_CALL taiyin_julian_day(
    const taiyin_calendar_datetime* datetime,
    double* out_jd
);
TAIYIN_C_API taiyin_status TAIYIN_C_CALL taiyin_julian_day_split(
    const taiyin_calendar_datetime* datetime,
    taiyin_split_julian_date* out_jd
);
TAIYIN_C_API taiyin_status TAIYIN_C_CALL taiyin_reverse_julian_day(
    double jd,
    taiyin_calendar_datetime* out_datetime
);
TAIYIN_C_API taiyin_status TAIYIN_C_CALL taiyin_reverse_julian_day_split(
    const taiyin_split_julian_date* jd,
    taiyin_calendar_datetime* out_datetime
);
TAIYIN_C_API double TAIYIN_C_CALL taiyin_decimal_year_from_jd(double jd);
TAIYIN_C_API double TAIYIN_C_CALL taiyin_julian_centuries_from_j2000(
    double jd
);
TAIYIN_C_API double TAIYIN_C_CALL taiyin_julian_millennia_from_j2000(
    double jd
);
TAIYIN_C_API double TAIYIN_C_CALL taiyin_add_seconds_to_jd(
    double jd,
    double seconds
);
TAIYIN_C_API double TAIYIN_C_CALL taiyin_seconds_between_jd(
    double jd_a,
    double jd_b
);
TAIYIN_C_API taiyin_status TAIYIN_C_CALL taiyin_add_seconds_to_split_jd(
    const taiyin_split_julian_date* jd,
    double seconds,
    taiyin_split_julian_date* out
);
TAIYIN_C_API taiyin_status TAIYIN_C_CALL taiyin_seconds_between_split_jd(
    const taiyin_split_julian_date* jd_a,
    const taiyin_split_julian_date* jd_b,
    double* out_seconds
);
TAIYIN_C_API double TAIYIN_C_CALL
taiyin_estimated_delta_t_seconds_for_decimal_year(double decimal_year);
TAIYIN_C_API double TAIYIN_C_CALL taiyin_estimated_delta_t_seconds_from_ut1(
    double jd_ut1
);
TAIYIN_C_API double TAIYIN_C_CALL taiyin_estimated_delta_t_seconds_from_tt(
    double jd_tt
);
TAIYIN_C_API double TAIYIN_C_CALL taiyin_tt_to_tdb(
    double jd_tt,
    int32_t tdb_model_id
);
TAIYIN_C_API double TAIYIN_C_CALL taiyin_tdb_to_tt(
    double jd_tdb,
    int32_t tdb_model_id
);
TAIYIN_C_API taiyin_status TAIYIN_C_CALL taiyin_tai_minus_utc_seconds(
    const taiyin_calendar_datetime* datetime_utc,
    double* out_seconds
);
TAIYIN_C_API double TAIYIN_C_CALL taiyin_utc_to_tai(
    double jd_utc,
    double tai_minus_utc_seconds
);
TAIYIN_C_API double TAIYIN_C_CALL taiyin_tai_to_tt(double jd_tai);
TAIYIN_C_API double TAIYIN_C_CALL taiyin_utc_to_tt(
    double jd_utc,
    double tai_minus_utc_seconds
);
TAIYIN_C_API double TAIYIN_C_CALL taiyin_utc_to_ut1(
    double jd_utc,
    double dut1_seconds
);
TAIYIN_C_API double TAIYIN_C_CALL
taiyin_delta_t_from_tai_minus_utc_and_dut1(
    double tai_minus_utc_seconds,
    double dut1_seconds
);
TAIYIN_C_API double TAIYIN_C_CALL taiyin_tt_to_ut1(
    double jd_tt,
    double delta_t_seconds
);
TAIYIN_C_API double TAIYIN_C_CALL taiyin_ut1_to_tt(
    double jd_ut1,
    double delta_t_seconds
);
TAIYIN_C_API taiyin_status TAIYIN_C_CALL taiyin_utc_to_tai_split(
    const taiyin_split_julian_date* jd_utc,
    double tai_minus_utc_seconds,
    taiyin_split_julian_date* out_jd_tai
);
TAIYIN_C_API taiyin_status TAIYIN_C_CALL taiyin_tai_to_tt_split(
    const taiyin_split_julian_date* jd_tai,
    taiyin_split_julian_date* out_jd_tt
);
TAIYIN_C_API taiyin_status TAIYIN_C_CALL taiyin_utc_to_tt_split(
    const taiyin_split_julian_date* jd_utc,
    double tai_minus_utc_seconds,
    taiyin_split_julian_date* out_jd_tt
);
TAIYIN_C_API taiyin_status TAIYIN_C_CALL taiyin_utc_to_ut1_split(
    const taiyin_split_julian_date* jd_utc,
    double dut1_seconds,
    taiyin_split_julian_date* out_jd_ut1
);
TAIYIN_C_API taiyin_status TAIYIN_C_CALL taiyin_tt_to_ut1_split(
    const taiyin_split_julian_date* jd_tt,
    double delta_t_seconds,
    taiyin_split_julian_date* out_jd_ut1
);
TAIYIN_C_API taiyin_status TAIYIN_C_CALL taiyin_ut1_to_tt_split(
    const taiyin_split_julian_date* jd_ut1,
    double delta_t_seconds,
    taiyin_split_julian_date* out_jd_tt
);
TAIYIN_C_API taiyin_status TAIYIN_C_CALL taiyin_tt_to_tdb_split(
    const taiyin_split_julian_date* jd_tt,
    int32_t tdb_model_id,
    taiyin_split_julian_date* out_jd_tdb
);
TAIYIN_C_API taiyin_status TAIYIN_C_CALL taiyin_tdb_to_tt_split(
    const taiyin_split_julian_date* jd_tdb,
    int32_t tdb_model_id,
    taiyin_split_julian_date* out_jd_tt
);
TAIYIN_C_API taiyin_status TAIYIN_C_CALL
taiyin_make_precise_time_scales_from_utc(
    const taiyin_calendar_datetime* datetime_utc,
    double tai_minus_utc_seconds,
    double dut1_seconds,
    int32_t tdb_model_id,
    taiyin_precise_time_scales* out
);
TAIYIN_C_API taiyin_status TAIYIN_C_CALL
taiyin_make_split_precise_time_scales_from_utc(
    const taiyin_calendar_datetime* datetime_utc,
    double tai_minus_utc_seconds,
    double dut1_seconds,
    int32_t tdb_model_id,
    taiyin_split_precise_time_scales* out
);
TAIYIN_C_API taiyin_status TAIYIN_C_CALL taiyin_make_time_scales_from_utc(
    const taiyin_context* context,
    const taiyin_calendar_datetime* datetime_utc,
    taiyin_precise_time_scales* out,
    taiyin_time_scale_diagnostic* diagnostic
);
TAIYIN_C_API taiyin_status TAIYIN_C_CALL
taiyin_make_split_time_scales_from_utc(
    const taiyin_context* context,
    const taiyin_calendar_datetime* datetime_utc,
    taiyin_split_precise_time_scales* out,
    taiyin_time_scale_diagnostic* diagnostic
);
TAIYIN_C_API taiyin_status TAIYIN_C_CALL
taiyin_make_time_scales_from_ut_delta_t(
    const taiyin_calendar_datetime* datetime_ut,
    double delta_t_seconds,
    int32_t tdb_model_id,
    taiyin_estimated_time_scales* out
);
TAIYIN_C_API taiyin_status TAIYIN_C_CALL
taiyin_make_split_time_scales_from_ut_delta_t(
    const taiyin_calendar_datetime* datetime_ut,
    double delta_t_seconds,
    int32_t tdb_model_id,
    taiyin_split_estimated_time_scales* out
);
TAIYIN_C_API taiyin_status TAIYIN_C_CALL taiyin_make_estimated_time_scales_from_ut(
    const taiyin_calendar_datetime* datetime_ut,
    int32_t tdb_model_id,
    taiyin_estimated_time_scales* out
);
TAIYIN_C_API taiyin_status TAIYIN_C_CALL
taiyin_make_split_estimated_time_scales_from_ut(
    const taiyin_calendar_datetime* datetime_ut,
    int32_t tdb_model_id,
    taiyin_split_estimated_time_scales* out
);

#ifdef __cplusplus
}
#endif

#endif
