#ifndef TAIYIN_C_SOLAR_TIME_H
#define TAIYIN_C_SOLAR_TIME_H

#include "taiyin/c/base.h"
#include "taiyin/c/context.h"
#include "taiyin/c/time.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct taiyin_equation_of_time_result {
    uint32_t struct_size;
    taiyin_split_julian_date jd_ut;
    taiyin_split_julian_date jd_tt;
    double equation_days;
    double equation_seconds;
    double apparent_sun_right_ascension_rad;
    double gast_rad;
} taiyin_equation_of_time_result;

TAIYIN_C_API void TAIYIN_C_CALL taiyin_equation_of_time_result_init(
    taiyin_equation_of_time_result* value
);
TAIYIN_C_API taiyin_status TAIYIN_C_CALL taiyin_calc_equation_of_time_ut(
    const taiyin_context* context,
    const taiyin_split_julian_date* jd_ut,
    taiyin_equation_of_time_result* out,
    taiyin_ephemeris_diagnostic* diagnostic
);
TAIYIN_C_API taiyin_status TAIYIN_C_CALL taiyin_calc_equation_of_time_tt(
    const taiyin_context* context,
    const taiyin_split_julian_date* jd_tt,
    taiyin_equation_of_time_result* out,
    taiyin_ephemeris_diagnostic* diagnostic
);
TAIYIN_C_API taiyin_status TAIYIN_C_CALL taiyin_local_mean_to_apparent_solar_time(
    const taiyin_context* context,
    const taiyin_split_julian_date* jd_local_mean,
    double longitude_rad,
    taiyin_split_julian_date* out_jd_local_apparent,
    taiyin_ephemeris_diagnostic* diagnostic
);
TAIYIN_C_API taiyin_status TAIYIN_C_CALL taiyin_local_apparent_to_mean_solar_time(
    const taiyin_context* context,
    const taiyin_split_julian_date* jd_local_apparent,
    double longitude_rad,
    taiyin_split_julian_date* out_jd_local_mean,
    taiyin_ephemeris_diagnostic* diagnostic
);

#ifdef __cplusplus
}
#endif

#endif
