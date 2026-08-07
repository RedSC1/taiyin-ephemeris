#ifndef TAIYIN_C_EVENTS_H
#define TAIYIN_C_EVENTS_H

#include "taiyin/c/phenomena.h"

#ifdef __cplusplus
extern "C" {
#endif

#define TAIYIN_EVENT_OPTION_REVERSE (UINT64_C(1) << 32)
#define TAIYIN_EVENT_OPTION_REFRACTION (UINT64_C(1) << 33)
#define TAIYIN_EVENT_OPTION_NO_REFRACTION (UINT64_C(1) << 34)

enum taiyin_greatest_elongation_kind {
    TAIYIN_GREATEST_ELONGATION_EASTERN = 1u << 0,
    TAIYIN_GREATEST_ELONGATION_WESTERN = 1u << 1
};

enum taiyin_solar_transit_kind {
    TAIYIN_SOLAR_TRANSIT_PARTIAL = 1u << 0,
    TAIYIN_SOLAR_TRANSIT_FULL_DISK = 1u << 1
};

enum taiyin_solar_transit_visibility_flags {
    TAIYIN_SOLAR_TRANSIT_VISIBLE_AT_OBSERVER = 1u << 8,
    TAIYIN_SOLAR_TRANSIT_T1_VISIBLE = 1u << 9,
    TAIYIN_SOLAR_TRANSIT_T2_VISIBLE = 1u << 10,
    TAIYIN_SOLAR_TRANSIT_GREATEST_VISIBLE = 1u << 11,
    TAIYIN_SOLAR_TRANSIT_T3_VISIBLE = 1u << 12,
    TAIYIN_SOLAR_TRANSIT_T4_VISIBLE = 1u << 13
};

#define TAIYIN_SOLAR_TRANSIT_CONTACT_SLOT_COUNT 5u

typedef struct taiyin_greatest_elongation_result {
    uint32_t struct_size;
    taiyin_split_julian_date jd_ut;
    double elongation_rad;
    double relative_longitude_rad;
    uint32_t kind;
    int32_t body_id;
    int32_t iteration_count;
    int32_t evaluation_count;
    taiyin_body_phenomena phenomena;
} taiyin_greatest_elongation_result;

typedef struct taiyin_angular_separation_result {
    uint32_t struct_size;
    taiyin_split_julian_date jd;
    double separation_rad;
    double separation_rate_rad_per_day;
    int32_t body_a_id;
    int32_t body_b_id;
    int32_t iteration_count;
    int32_t evaluation_count;
} taiyin_angular_separation_result;

typedef struct taiyin_solar_transit_result {
    uint32_t struct_size;
    int32_t body_id;
    uint32_t kind;
    taiyin_split_julian_date greatest_jd_ut;
    double minimum_separation_rad;
    double sun_radius_rad;
    double body_radius_rad;
    taiyin_split_julian_date t1_jd_ut;
    taiyin_split_julian_date t2_jd_ut;
    taiyin_split_julian_date t3_jd_ut;
    taiyin_split_julian_date t4_jd_ut;
    int32_t iteration_count;
    int32_t evaluation_count;
} taiyin_solar_transit_result;

typedef struct taiyin_local_solar_transit_result {
    uint32_t struct_size;
    taiyin_solar_transit_result global;
    taiyin_solar_transit_result topocentric;
    uint32_t visibility_flags;
    double contact_sun_altitude_deg[TAIYIN_SOLAR_TRANSIT_CONTACT_SLOT_COUNT];
    double contact_sun_azimuth_deg[TAIYIN_SOLAR_TRANSIT_CONTACT_SLOT_COUNT];
    taiyin_split_julian_date sunrise_jd_ut;
    taiyin_split_julian_date sunset_jd_ut;
} taiyin_local_solar_transit_result;

TAIYIN_C_API void TAIYIN_C_CALL taiyin_greatest_elongation_result_init(
    taiyin_greatest_elongation_result* value
);
TAIYIN_C_API void TAIYIN_C_CALL taiyin_angular_separation_result_init(
    taiyin_angular_separation_result* value
);
TAIYIN_C_API void TAIYIN_C_CALL taiyin_solar_transit_result_init(
    taiyin_solar_transit_result* value
);
TAIYIN_C_API void TAIYIN_C_CALL taiyin_local_solar_transit_result_init(
    taiyin_local_solar_transit_result* value
);

TAIYIN_C_API double TAIYIN_C_CALL taiyin_recommended_longitude_search_step_days(
    int32_t body_id
);
TAIYIN_C_API double TAIYIN_C_CALL taiyin_recommended_aspect_search_step_days(
    int32_t body_a_id,
    int32_t body_b_id
);

TAIYIN_C_API taiyin_status TAIYIN_C_CALL taiyin_search_solar_longitude_ut(
    const taiyin_context* context,
    double target_longitude_rad,
    const taiyin_split_julian_date* estimate_jd_ut,
    uint64_t flags,
    taiyin_split_julian_date* out_jd_ut,
    taiyin_ephemeris_diagnostic* diagnostic
);
TAIYIN_C_API taiyin_status TAIYIN_C_CALL taiyin_search_solar_longitude_tt(
    const taiyin_context* context,
    double target_longitude_rad,
    const taiyin_split_julian_date* estimate_jd_tt,
    uint64_t flags,
    taiyin_split_julian_date* out_jd_tt,
    taiyin_ephemeris_diagnostic* diagnostic
);
TAIYIN_C_API taiyin_status TAIYIN_C_CALL taiyin_search_moon_longitude_ut(
    const taiyin_context* context,
    double target_longitude_rad,
    const taiyin_split_julian_date* estimate_jd_ut,
    uint64_t flags,
    taiyin_split_julian_date* out_jd_ut,
    taiyin_ephemeris_diagnostic* diagnostic
);
TAIYIN_C_API taiyin_status TAIYIN_C_CALL taiyin_search_moon_longitude_tt(
    const taiyin_context* context,
    double target_longitude_rad,
    const taiyin_split_julian_date* estimate_jd_tt,
    uint64_t flags,
    taiyin_split_julian_date* out_jd_tt,
    taiyin_ephemeris_diagnostic* diagnostic
);

TAIYIN_C_API taiyin_status TAIYIN_C_CALL taiyin_search_body_longitude_crossings_ut(
    const taiyin_context* context,
    int32_t body_id,
    double target_longitude_rad,
    const taiyin_split_julian_date* start_jd_ut,
    const taiyin_split_julian_date* end_jd_ut,
    double max_step_days,
    uint64_t flags,
    taiyin_split_julian_date* out_jd_ut,
    size_t capacity,
    size_t* out_count,
    taiyin_ephemeris_diagnostic* diagnostic
);
TAIYIN_C_API taiyin_status TAIYIN_C_CALL taiyin_search_body_longitude_crossings_tt(
    const taiyin_context* context,
    int32_t body_id,
    double target_longitude_rad,
    const taiyin_split_julian_date* start_jd_tt,
    const taiyin_split_julian_date* end_jd_tt,
    double max_step_days,
    uint64_t flags,
    taiyin_split_julian_date* out_jd_tt,
    size_t capacity,
    size_t* out_count,
    taiyin_ephemeris_diagnostic* diagnostic
);
TAIYIN_C_API taiyin_status TAIYIN_C_CALL taiyin_search_body_longitude_stations_ut(
    const taiyin_context* context,
    int32_t body_id,
    const taiyin_split_julian_date* start_jd_ut,
    const taiyin_split_julian_date* end_jd_ut,
    double max_step_days,
    uint64_t flags,
    taiyin_split_julian_date* out_jd_ut,
    double* out_longitude_rad,
    size_t capacity,
    size_t* out_count,
    taiyin_ephemeris_diagnostic* diagnostic
);
TAIYIN_C_API taiyin_status TAIYIN_C_CALL taiyin_search_body_longitude_stations_tt(
    const taiyin_context* context,
    int32_t body_id,
    const taiyin_split_julian_date* start_jd_tt,
    const taiyin_split_julian_date* end_jd_tt,
    double max_step_days,
    uint64_t flags,
    taiyin_split_julian_date* out_jd_tt,
    double* out_longitude_rad,
    size_t capacity,
    size_t* out_count,
    taiyin_ephemeris_diagnostic* diagnostic
);
TAIYIN_C_API taiyin_status TAIYIN_C_CALL taiyin_search_body_aspect_crossings_ut(
    const taiyin_context* context,
    int32_t body_a_id,
    int32_t body_b_id,
    double aspect_rad,
    const taiyin_split_julian_date* start_jd_ut,
    const taiyin_split_julian_date* end_jd_ut,
    double max_step_days,
    uint64_t flags,
    taiyin_split_julian_date* out_jd_ut,
    size_t capacity,
    size_t* out_count,
    taiyin_ephemeris_diagnostic* diagnostic
);
TAIYIN_C_API taiyin_status TAIYIN_C_CALL taiyin_search_body_aspect_crossings_tt(
    const taiyin_context* context,
    int32_t body_a_id,
    int32_t body_b_id,
    double aspect_rad,
    const taiyin_split_julian_date* start_jd_tt,
    const taiyin_split_julian_date* end_jd_tt,
    double max_step_days,
    uint64_t flags,
    taiyin_split_julian_date* out_jd_tt,
    size_t capacity,
    size_t* out_count,
    taiyin_ephemeris_diagnostic* diagnostic
);
TAIYIN_C_API taiyin_status TAIYIN_C_CALL taiyin_search_body_exact_aspects_ut(
    const taiyin_context* context,
    int32_t body_a_id,
    int32_t body_b_id,
    const double* aspect_separations_rad,
    size_t aspect_count,
    const taiyin_split_julian_date* start_jd_ut,
    const taiyin_split_julian_date* end_jd_ut,
    double max_step_days,
    uint64_t flags,
    taiyin_split_julian_date* out_jd_ut,
    double* out_target_aspect_rad,
    size_t capacity,
    size_t* out_count,
    taiyin_ephemeris_diagnostic* diagnostic
);
TAIYIN_C_API taiyin_status TAIYIN_C_CALL taiyin_search_body_exact_aspects_tt(
    const taiyin_context* context,
    int32_t body_a_id,
    int32_t body_b_id,
    const double* aspect_separations_rad,
    size_t aspect_count,
    const taiyin_split_julian_date* start_jd_tt,
    const taiyin_split_julian_date* end_jd_tt,
    double max_step_days,
    uint64_t flags,
    taiyin_split_julian_date* out_jd_tt,
    double* out_target_aspect_rad,
    size_t capacity,
    size_t* out_count,
    taiyin_ephemeris_diagnostic* diagnostic
);

TAIYIN_C_API taiyin_status TAIYIN_C_CALL taiyin_search_greatest_elongation_ut(
    const taiyin_context* context,
    int32_t body_id,
    const taiyin_split_julian_date* start_jd_ut,
    const taiyin_split_julian_date* end_jd_ut,
    uint64_t flags,
    taiyin_greatest_elongation_result* out,
    taiyin_ephemeris_diagnostic* diagnostic
);
TAIYIN_C_API taiyin_status TAIYIN_C_CALL taiyin_search_minimum_angular_separation_ut(
    const taiyin_context* context,
    int32_t body_a_id,
    int32_t body_b_id,
    const taiyin_split_julian_date* start_jd_ut,
    const taiyin_split_julian_date* end_jd_ut,
    double max_step_days,
    uint64_t flags,
    taiyin_angular_separation_result* out,
    taiyin_ephemeris_diagnostic* diagnostic
);
TAIYIN_C_API taiyin_status TAIYIN_C_CALL taiyin_search_minimum_angular_separation_tt(
    const taiyin_context* context,
    int32_t body_a_id,
    int32_t body_b_id,
    const taiyin_split_julian_date* start_jd_tt,
    const taiyin_split_julian_date* end_jd_tt,
    double max_step_days,
    uint64_t flags,
    taiyin_angular_separation_result* out,
    taiyin_ephemeris_diagnostic* diagnostic
);
TAIYIN_C_API taiyin_status TAIYIN_C_CALL taiyin_search_next_solar_transit_ut(
    const taiyin_context* context,
    int32_t body_id,
    const taiyin_split_julian_date* jd_start_ut,
    uint64_t flags,
    taiyin_solar_transit_result* out,
    taiyin_ephemeris_diagnostic* diagnostic
);
TAIYIN_C_API taiyin_status TAIYIN_C_CALL taiyin_compute_local_solar_transit_ut(
    const taiyin_context* context,
    const taiyin_solar_transit_result* global_transit,
    double longitude_deg,
    double latitude_deg,
    double height_m,
    uint64_t flags,
    taiyin_local_solar_transit_result* out,
    taiyin_ephemeris_diagnostic* diagnostic
);
TAIYIN_C_API taiyin_status TAIYIN_C_CALL taiyin_search_next_local_solar_transit_ut(
    const taiyin_context* context,
    int32_t body_id,
    const taiyin_split_julian_date* jd_start_ut,
    double longitude_deg,
    double latitude_deg,
    double height_m,
    uint64_t flags,
    taiyin_local_solar_transit_result* out,
    taiyin_ephemeris_diagnostic* diagnostic
);
TAIYIN_C_API taiyin_status TAIYIN_C_CALL taiyin_search_lunar_phase_crossings_ut(
    const taiyin_context* context,
    double phase_rad,
    const taiyin_split_julian_date* start_jd_ut,
    const taiyin_split_julian_date* end_jd_ut,
    double max_step_days,
    uint64_t flags,
    taiyin_split_julian_date* out_jd_ut,
    size_t capacity,
    size_t* out_count,
    taiyin_ephemeris_diagnostic* diagnostic
);
TAIYIN_C_API taiyin_status TAIYIN_C_CALL taiyin_search_lunar_phase_crossings_tt(
    const taiyin_context* context,
    double phase_rad,
    const taiyin_split_julian_date* start_jd_tt,
    const taiyin_split_julian_date* end_jd_tt,
    double max_step_days,
    uint64_t flags,
    taiyin_split_julian_date* out_jd_tt,
    size_t capacity,
    size_t* out_count,
    taiyin_ephemeris_diagnostic* diagnostic
);

#ifdef __cplusplus
}
#endif

#endif
