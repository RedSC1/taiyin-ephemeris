#ifndef TAIYIN_C_OBSERVED_H
#define TAIYIN_C_OBSERVED_H

#include "taiyin/c/base.h"
#include "taiyin/c/context.h"

#ifdef __cplusplus
extern "C" {
#endif

enum taiyin_observed_flags {
    TAIYIN_OBSERVED_SPEED = 1u << 0,
    TAIYIN_OBSERVED_TOPOCENTRIC = 1u << 1,
    TAIYIN_OBSERVED_HORIZONTAL = 1u << 2,
    TAIYIN_OBSERVED_REFRACTION = 1u << 3,
    TAIYIN_OBSERVED_TRUEPOS = 1u << 4,
    TAIYIN_OBSERVED_ASTROMETRIC = 1u << 5,
    TAIYIN_OBSERVED_NO_ABERR = 1u << 6,
    TAIYIN_OBSERVED_NO_GDEFL = 1u << 7
};
#define TAIYIN_OBSERVED_OPTION_STRICT_METEOROLOGY (UINT64_C(1) << 32)

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
