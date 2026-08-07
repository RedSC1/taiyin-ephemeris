#ifndef TAIYIN_C_STAR_H
#define TAIYIN_C_STAR_H

#include "taiyin/c/observed.h"
#include "taiyin/c/position.h"

#ifdef __cplusplus
extern "C" {
#endif

TAIYIN_C_API taiyin_status TAIYIN_C_CALL taiyin_star_catalog_add_tsc1(
    const char* path
);
TAIYIN_C_API taiyin_status TAIYIN_C_CALL taiyin_star_catalog_add_tsc1_memory(
    const uint8_t* data,
    size_t size
);
TAIYIN_C_API taiyin_status TAIYIN_C_CALL taiyin_star_catalog_add_tsf1(
    const char* path
);
TAIYIN_C_API void TAIYIN_C_CALL taiyin_star_catalog_clear(void);
TAIYIN_C_API size_t TAIYIN_C_CALL taiyin_star_catalog_count(void);
TAIYIN_C_API taiyin_status TAIYIN_C_CALL taiyin_star_find_magnitude(
    const char* star_key,
    double* out_magnitude
);

/*
 * The TDB entry points accept a null jd_tt. In that case jd_tdb is also used
 * as the TT epoch, matching the ordinary position C API fallback.
 */
TAIYIN_C_API taiyin_status TAIYIN_C_CALL taiyin_calc_star_position_tdb(
    const taiyin_context* context,
    const char* star_key,
    const taiyin_split_julian_date* jd_tdb,
    const taiyin_split_julian_date* jd_tt,
    uint64_t flags,
    double out_position[6],
    taiyin_ephemeris_diagnostic* diagnostic
);
TAIYIN_C_API taiyin_status TAIYIN_C_CALL taiyin_calc_star_position_tt(
    const taiyin_context* context,
    const char* star_key,
    const taiyin_split_julian_date* jd_tt,
    uint64_t flags,
    double out_position[6],
    taiyin_ephemeris_diagnostic* diagnostic
);
TAIYIN_C_API taiyin_status TAIYIN_C_CALL taiyin_calc_star_position_ut(
    const taiyin_context* context,
    const char* star_key,
    const taiyin_split_julian_date* jd_ut,
    uint64_t flags,
    double out_position[6],
    taiyin_ephemeris_diagnostic* diagnostic
);
TAIYIN_C_API taiyin_status TAIYIN_C_CALL taiyin_calc_star_position_ut_delta_t(
    const taiyin_context* context,
    const char* star_key,
    const taiyin_split_julian_date* jd_ut1,
    double delta_t_seconds,
    uint64_t flags,
    double out_position[6],
    taiyin_ephemeris_diagnostic* diagnostic
);
TAIYIN_C_API taiyin_status TAIYIN_C_CALL taiyin_calc_star_positions_tdb(
    const taiyin_context* context,
    const char* const* star_keys,
    size_t star_count,
    const taiyin_split_julian_date* jd_tdb,
    const taiyin_split_julian_date* jd_tt,
    uint64_t flags,
    double* out_positions,
    taiyin_ephemeris_diagnostic* diagnostics
);
TAIYIN_C_API taiyin_status TAIYIN_C_CALL taiyin_calc_star_positions_tt(
    const taiyin_context* context,
    const char* const* star_keys,
    size_t star_count,
    const taiyin_split_julian_date* jd_tt,
    uint64_t flags,
    double* out_positions,
    taiyin_ephemeris_diagnostic* diagnostics
);
TAIYIN_C_API taiyin_status TAIYIN_C_CALL taiyin_calc_star_positions_ut(
    const taiyin_context* context,
    const char* const* star_keys,
    size_t star_count,
    const taiyin_split_julian_date* jd_ut,
    uint64_t flags,
    double* out_positions,
    taiyin_ephemeris_diagnostic* diagnostics
);
TAIYIN_C_API taiyin_status TAIYIN_C_CALL taiyin_calc_star_positions_ut_delta_t(
    const taiyin_context* context,
    const char* const* star_keys,
    size_t star_count,
    const taiyin_split_julian_date* jd_ut1,
    double delta_t_seconds,
    uint64_t flags,
    double* out_positions,
    taiyin_ephemeris_diagnostic* diagnostics
);

TAIYIN_C_API taiyin_status TAIYIN_C_CALL taiyin_calc_observed_star_ut(
    const taiyin_context* context,
    const char* star_key,
    const taiyin_split_julian_date* jd_ut,
    uint64_t flags,
    taiyin_observed_position* out_position,
    taiyin_ephemeris_diagnostic* diagnostic
);
TAIYIN_C_API taiyin_status TAIYIN_C_CALL taiyin_calc_observed_stars_ut(
    const taiyin_context* context,
    const char* const* star_keys,
    size_t star_count,
    const taiyin_split_julian_date* jd_ut,
    uint64_t flags,
    taiyin_observed_position* out_positions,
    taiyin_ephemeris_diagnostic* diagnostics
);

#ifdef __cplusplus
}
#endif

#endif
