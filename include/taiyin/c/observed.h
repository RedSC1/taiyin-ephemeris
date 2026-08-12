#ifndef TAIYIN_C_OBSERVED_H
#define TAIYIN_C_OBSERVED_H

#include "taiyin/c/base.h"
#include "taiyin/c/context.h"
#include "taiyin/c/position.h"

#ifdef __cplusplus
extern "C" {
#endif

enum taiyin_observed_flags {
    TAIYIN_OBSERVED_SPEED = TAIYIN_POSITION_SPEED,
    TAIYIN_OBSERVED_TRUEPOS = TAIYIN_POSITION_TRUEPOS,
    TAIYIN_OBSERVED_NO_ABERR = TAIYIN_POSITION_NO_ABERR,
    TAIYIN_OBSERVED_NO_GDEFL = TAIYIN_POSITION_NO_GDEFL,
    TAIYIN_OBSERVED_ASTROMETRIC = TAIYIN_POSITION_ASTROMETRIC,
    TAIYIN_OBSERVED_TOPOCENTRIC = TAIYIN_POSITION_TOPOCENTRIC,
    TAIYIN_OBSERVED_ALLOW_BARYCENTER_APPROX = TAIYIN_POSITION_ALLOW_BARYCENTER_APPROX
};
/*
 * Observed calls accept only the position flags named above. XYZ,
 * EQUATORIAL, RADIANS, and NONUT are representation selectors for
 * taiyin_calc_position_*() and return TAIYIN_ERROR_UNSUPPORTED here.
 */
#ifdef __cplusplus
static constexpr uint64_t TAIYIN_OBSERVED_HORIZONTAL = UINT64_C(1) << 32;
static constexpr uint64_t TAIYIN_OBSERVED_REFRACTION = UINT64_C(1) << 33;
#else
#define TAIYIN_OBSERVED_HORIZONTAL (UINT64_C(1) << 32)
#define TAIYIN_OBSERVED_REFRACTION (UINT64_C(1) << 33)
#endif
#define TAIYIN_OBSERVED_OPTION_STRICT_METEOROLOGY (UINT64_C(1) << 34)

typedef struct taiyin_horizontal_coordinates {
    double azimuth_rad;
    double altitude_rad;
    double distance_au;
} taiyin_horizontal_coordinates;

typedef struct taiyin_horizontal_rates {
    double azimuth_rate_rad_per_day;
    double altitude_rate_rad_per_day;
    double distance_rate_au_per_day;
} taiyin_horizontal_rates;

typedef struct taiyin_apparent_position {
    int32_t body_id;
    uint32_t body_mask_bit;
    taiyin_status status;
    taiyin_ephemeris_diagnostic diagnostic;
    taiyin_cartesian_state geometric_state;
    taiyin_cartesian_state apparent_state;
    double longitude_rad;
    double latitude_rad;
    double distance_au;
    double light_time_days;
    taiyin_bool cache_hit;
    uint8_t reserved[7];
} taiyin_apparent_position;

typedef struct taiyin_observed_position {
    uint32_t struct_size;
    int32_t body_id;
    taiyin_status status;
    taiyin_ephemeris_diagnostic diagnostic;
    taiyin_apparent_position apparent;
    taiyin_horizontal_coordinates horizontal;
    taiyin_horizontal_rates horizontal_rates;
    taiyin_horizontal_coordinates refracted_horizontal;
    taiyin_horizontal_rates refracted_horizontal_rates;
} taiyin_observed_position;

TAIYIN_C_API void TAIYIN_C_CALL taiyin_observed_position_init(
    taiyin_observed_position* value
);
TAIYIN_C_API taiyin_status TAIYIN_C_CALL taiyin_calc_observed_bodies_ut(
    const taiyin_context* context,
    const taiyin_split_julian_date* jd_ut,
    const int32_t* body_ids,
    size_t body_count,
    uint64_t flags,
    taiyin_observed_position* out_positions,
    taiyin_ephemeris_diagnostic* diagnostics
);
TAIYIN_C_API taiyin_status TAIYIN_C_CALL taiyin_calc_observed_bodies_utc(
    const taiyin_context* context,
    const taiyin_calendar_datetime* datetime_utc,
    const int32_t* body_ids,
    size_t body_count,
    uint64_t flags,
    taiyin_observed_position* out_positions,
    taiyin_ephemeris_diagnostic* diagnostics
);

#ifdef __cplusplus
}
#endif

#endif
