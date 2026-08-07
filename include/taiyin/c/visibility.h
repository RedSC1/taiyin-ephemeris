#ifndef TAIYIN_C_VISIBILITY_H
#define TAIYIN_C_VISIBILITY_H

#include "taiyin/c/base.h"
#include "taiyin/c/context.h"

#ifdef __cplusplus
extern "C" {
#endif

enum taiyin_visibility_event_kind {
    TAIYIN_VISIBILITY_EVENT_RISE = 1,
    TAIYIN_VISIBILITY_EVENT_SET = 2,
    TAIYIN_VISIBILITY_EVENT_UPPER_TRANSIT = 3,
    TAIYIN_VISIBILITY_EVENT_LOWER_TRANSIT = 4
};

enum taiyin_visibility_limb_kind {
    TAIYIN_VISIBILITY_LIMB_UPPER = 1,
    TAIYIN_VISIBILITY_LIMB_CENTER = 2,
    TAIYIN_VISIBILITY_LIMB_LOWER = 3
};

enum taiyin_visibility_twilight_kind {
    TAIYIN_TWILIGHT_CIVIL = 1,
    TAIYIN_TWILIGHT_NAUTICAL = 2,
    TAIYIN_TWILIGHT_ASTRONOMICAL = 3
};

enum taiyin_visibility_altitude_state {
    TAIYIN_VISIBILITY_NOT_FOUND = 0,
    TAIYIN_VISIBILITY_CROSSES = 1,
    TAIYIN_VISIBILITY_ALWAYS_ABOVE = 2,
    TAIYIN_VISIBILITY_ALWAYS_BELOW = 3,
    TAIYIN_VISIBILITY_TANGENT = 4
};

enum taiyin_visibility_crossing_direction {
    TAIYIN_VISIBILITY_CROSSING_ANY = 0,
    TAIYIN_VISIBILITY_CROSSING_RISING = 1,
    TAIYIN_VISIBILITY_CROSSING_SETTING = 2
};

enum taiyin_visibility_flags {
    TAIYIN_VISIBILITY_REFRACTION = 1u << 0,
    TAIYIN_VISIBILITY_FIXED_DISC_SIZE = 1u << 1,
    TAIYIN_VISIBILITY_NO_REFRACTION = 1u << 2
};
#define TAIYIN_VISIBILITY_STRICT_METEOROLOGY (UINT64_C(1) << 32)

typedef struct taiyin_visibility_event_result {
    uint32_t struct_size;
    int32_t altitude_state;
    int32_t crossing_direction;
    taiyin_split_julian_date jd_ut;
    double residual_rad;
    double min_residual_rad;
    double max_residual_rad;
    taiyin_split_julian_date min_residual_jd_ut;
    taiyin_split_julian_date max_residual_jd_ut;
    int32_t sample_count;
    int32_t refine_count;
} taiyin_visibility_event_result;

typedef struct taiyin_solar_rise_set_fast_result {
    uint32_t struct_size;
    int32_t altitude_state;
    taiyin_split_julian_date rise_jd_tt;
    taiyin_split_julian_date set_jd_tt;
    int32_t sample_count;
    int32_t refine_count;
} taiyin_solar_rise_set_fast_result;

typedef struct taiyin_solar_transit_fast_result {
    uint32_t struct_size;
    taiyin_split_julian_date transit_jd_tt;
    double altitude_rad;
    double azimuth_rad;
    int32_t sample_count;
    int32_t refine_count;
} taiyin_solar_transit_fast_result;

TAIYIN_C_API void TAIYIN_C_CALL taiyin_visibility_event_result_init(
    taiyin_visibility_event_result* value
);
TAIYIN_C_API void TAIYIN_C_CALL taiyin_solar_rise_set_fast_result_init(
    taiyin_solar_rise_set_fast_result* value
);
TAIYIN_C_API void TAIYIN_C_CALL taiyin_solar_transit_fast_result_init(
    taiyin_solar_transit_fast_result* value
);

TAIYIN_C_API taiyin_status TAIYIN_C_CALL taiyin_search_moon_rise_set_ut(
    const taiyin_context* context,
    const taiyin_split_julian_date* start_jd_ut,
    const taiyin_split_julian_date* end_jd_ut,
    int32_t event_kind,
    int32_t limb_kind,
    uint64_t flags,
    taiyin_visibility_event_result* out,
    taiyin_ephemeris_diagnostic* diagnostic
);
TAIYIN_C_API taiyin_status TAIYIN_C_CALL taiyin_search_moon_rise_set_at_horizon_ut(
    const taiyin_context* context,
    const taiyin_split_julian_date* start_jd_ut,
    const taiyin_split_julian_date* end_jd_ut,
    int32_t event_kind,
    int32_t limb_kind,
    double horizon_altitude_rad,
    uint64_t flags,
    taiyin_visibility_event_result* out,
    taiyin_ephemeris_diagnostic* diagnostic
);
TAIYIN_C_API taiyin_status TAIYIN_C_CALL taiyin_search_moon_transit_ut(
    const taiyin_context* context,
    const taiyin_split_julian_date* start_jd_ut,
    const taiyin_split_julian_date* end_jd_ut,
    int32_t event_kind,
    taiyin_visibility_event_result* out,
    taiyin_ephemeris_diagnostic* diagnostic
);

TAIYIN_C_API taiyin_status TAIYIN_C_CALL taiyin_search_planet_rise_set_ut(
    const taiyin_context* context,
    int32_t body_id,
    const taiyin_split_julian_date* start_jd_ut,
    const taiyin_split_julian_date* end_jd_ut,
    int32_t event_kind,
    int32_t limb_kind,
    uint64_t flags,
    taiyin_visibility_event_result* out,
    taiyin_ephemeris_diagnostic* diagnostic
);
TAIYIN_C_API taiyin_status TAIYIN_C_CALL taiyin_search_planet_rise_set_at_horizon_ut(
    const taiyin_context* context,
    int32_t body_id,
    const taiyin_split_julian_date* start_jd_ut,
    const taiyin_split_julian_date* end_jd_ut,
    int32_t event_kind,
    int32_t limb_kind,
    double horizon_altitude_rad,
    uint64_t flags,
    taiyin_visibility_event_result* out,
    taiyin_ephemeris_diagnostic* diagnostic
);
TAIYIN_C_API taiyin_status TAIYIN_C_CALL taiyin_search_planet_transit_ut(
    const taiyin_context* context,
    int32_t body_id,
    const taiyin_split_julian_date* start_jd_ut,
    const taiyin_split_julian_date* end_jd_ut,
    int32_t event_kind,
    taiyin_visibility_event_result* out,
    taiyin_ephemeris_diagnostic* diagnostic
);

TAIYIN_C_API taiyin_status TAIYIN_C_CALL taiyin_search_solar_rise_set_ut(
    const taiyin_context* context,
    const taiyin_split_julian_date* start_jd_ut,
    const taiyin_split_julian_date* end_jd_ut,
    int32_t event_kind,
    int32_t limb_kind,
    uint64_t flags,
    taiyin_visibility_event_result* out,
    taiyin_ephemeris_diagnostic* diagnostic
);
TAIYIN_C_API taiyin_status TAIYIN_C_CALL taiyin_search_solar_rise_set_at_horizon_ut(
    const taiyin_context* context,
    const taiyin_split_julian_date* start_jd_ut,
    const taiyin_split_julian_date* end_jd_ut,
    int32_t event_kind,
    int32_t limb_kind,
    double horizon_altitude_rad,
    uint64_t flags,
    taiyin_visibility_event_result* out,
    taiyin_ephemeris_diagnostic* diagnostic
);
TAIYIN_C_API taiyin_status TAIYIN_C_CALL taiyin_search_solar_twilight_ut(
    const taiyin_context* context,
    const taiyin_split_julian_date* start_jd_ut,
    const taiyin_split_julian_date* end_jd_ut,
    int32_t event_kind,
    int32_t twilight_kind,
    taiyin_visibility_event_result* out,
    taiyin_ephemeris_diagnostic* diagnostic
);
TAIYIN_C_API taiyin_status TAIYIN_C_CALL taiyin_search_solar_transit_ut(
    const taiyin_context* context,
    const taiyin_split_julian_date* start_jd_ut,
    const taiyin_split_julian_date* end_jd_ut,
    int32_t event_kind,
    taiyin_visibility_event_result* out,
    taiyin_ephemeris_diagnostic* diagnostic
);
TAIYIN_C_API taiyin_status TAIYIN_C_CALL taiyin_compute_solar_rise_set_fast_tt(
    const taiyin_context* context,
    const taiyin_split_julian_date* center_jd_tt,
    double longitude_deg,
    double latitude_deg,
    double height_m,
    int32_t limb_kind,
    double horizon_altitude_rad,
    uint64_t visibility_flags,
    taiyin_solar_rise_set_fast_result* out,
    taiyin_ephemeris_diagnostic* diagnostic
);
TAIYIN_C_API taiyin_status TAIYIN_C_CALL taiyin_compute_solar_transit_fast_tt(
    const taiyin_context* context,
    const taiyin_split_julian_date* center_jd_tt,
    double longitude_deg,
    double latitude_deg,
    double height_m,
    taiyin_solar_transit_fast_result* out,
    taiyin_ephemeris_diagnostic* diagnostic
);

TAIYIN_C_API taiyin_status TAIYIN_C_CALL taiyin_search_star_rise_set_ut(
    const taiyin_context* context,
    const char* star_key,
    const taiyin_split_julian_date* start_jd_ut,
    const taiyin_split_julian_date* end_jd_ut,
    int32_t event_kind,
    uint64_t flags,
    taiyin_visibility_event_result* out,
    taiyin_ephemeris_diagnostic* diagnostic
);
TAIYIN_C_API taiyin_status TAIYIN_C_CALL taiyin_search_star_rise_set_at_horizon_ut(
    const taiyin_context* context,
    const char* star_key,
    const taiyin_split_julian_date* start_jd_ut,
    const taiyin_split_julian_date* end_jd_ut,
    int32_t event_kind,
    double horizon_altitude_rad,
    uint64_t flags,
    taiyin_visibility_event_result* out,
    taiyin_ephemeris_diagnostic* diagnostic
);
TAIYIN_C_API taiyin_status TAIYIN_C_CALL taiyin_search_star_transit_ut(
    const taiyin_context* context,
    const char* star_key,
    const taiyin_split_julian_date* start_jd_ut,
    const taiyin_split_julian_date* end_jd_ut,
    int32_t event_kind,
    taiyin_visibility_event_result* out,
    taiyin_ephemeris_diagnostic* diagnostic
);

#ifdef __cplusplus
}
#endif

#endif
