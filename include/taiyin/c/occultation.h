#ifndef TAIYIN_C_OCCULTATION_H
#define TAIYIN_C_OCCULTATION_H

#include "taiyin/c/context.h"

#ifdef __cplusplus
extern "C" {
#endif

#define TAIYIN_OCCULTATION_OPTION_BACKWARD (UINT64_C(1) << 32)
#define TAIYIN_OCCULTATION_OPTION_ONE_CANDIDATE (UINT64_C(1) << 33)
#define TAIYIN_OCCULTATION_OPTION_REFRACTION (UINT64_C(1) << 34)
#define TAIYIN_OCCULTATION_OPTION_FILTER_PARTIAL (UINT64_C(1) << 40)
#define TAIYIN_OCCULTATION_OPTION_FILTER_TOTAL (UINT64_C(1) << 41)
#define TAIYIN_OCCULTATION_OPTION_FILTER_GRAZING (UINT64_C(1) << 42)
#define TAIYIN_OCCULTATION_OPTION_FILTER_CENTRAL (UINT64_C(1) << 43)
#define TAIYIN_OCCULTATION_OPTION_FILTER_NONCENTRAL (UINT64_C(1) << 44)
#define TAIYIN_OCCULTATION_OPTION_LUNAR_LIMB_CORRECTION (UINT64_C(1) << 45)

enum taiyin_occultation_kind {
    TAIYIN_C_OCCULTATION_KIND_NONE = 0,
    TAIYIN_C_OCCULTATION_KIND_LUNAR_STAR = 1,
    TAIYIN_C_OCCULTATION_KIND_LUNAR_BODY = 2
};

enum taiyin_occultation_type_flags {
    TAIYIN_C_OCCULTATION_TYPE_PARTIAL = 1u << 0,
    TAIYIN_C_OCCULTATION_TYPE_TOTAL = 1u << 1,
    TAIYIN_C_OCCULTATION_TYPE_ANNULAR = 1u << 2,
    TAIYIN_C_OCCULTATION_TYPE_GRAZING = 1u << 3,
    TAIYIN_C_OCCULTATION_TYPE_CENTRAL = 1u << 4,
    TAIYIN_C_OCCULTATION_TYPE_NONCENTRAL = 1u << 5,
    TAIYIN_C_OCCULTATION_TYPE_CENTRALITY_UNAVAILABLE = 1u << 6
};

enum taiyin_occultation_visibility_sample_flags {
    TAIYIN_C_OCCULTATION_SAMPLE_MOON_ABOVE_HORIZON = 1u << 0,
    TAIYIN_C_OCCULTATION_SAMPLE_TARGET_ABOVE_HORIZON = 1u << 1,
    TAIYIN_C_OCCULTATION_SAMPLE_SUN_BELOW_HORIZON = 1u << 2
};

enum taiyin_occultation_visibility_flags {
    TAIYIN_C_OCCULTATION_VISIBILITY_HAS_VISIBLE_SAMPLE = 1u << 0,
    TAIYIN_C_OCCULTATION_VISIBILITY_MAXIMUM_VISIBLE = 1u << 1,
    TAIYIN_C_OCCULTATION_VISIBILITY_HAS_DARK_SAMPLE = 1u << 2,
    TAIYIN_C_OCCULTATION_VISIBILITY_MAXIMUM_DARK = 1u << 3,
    TAIYIN_C_OCCULTATION_VISIBILITY_HAS_VISIBLE_INTERVAL = 1u << 4,
    TAIYIN_C_OCCULTATION_VISIBILITY_HAS_DARK_INTERVAL = 1u << 5
};

enum {
    TAIYIN_C_OCCULTATION_WHERE_MAX_PATH_POINTS = 16,
    TAIYIN_C_OCCULTATION_WHERE_MAX_POLYGON_POINTS = 32,
    TAIYIN_C_OCCULTATION_MAX_VISIBILITY_INTERVALS = 8
};

typedef struct taiyin_lunar_occultation_phenomena {
    uint32_t struct_size;
    double angular_distance_rad;
    double diameter_ratio;
    double magnitude;
    double obscuration;
    double occulted_fraction;
} taiyin_lunar_occultation_phenomena;

typedef struct taiyin_lunar_occultation_result {
    uint32_t struct_size;
    int32_t kind;
    uint32_t type_flags;
    taiyin_split_julian_date jd_ut;
    taiyin_split_julian_date begin_jd_ut;
    taiyin_split_julian_date end_jd_ut;
    taiyin_split_julian_date first_contact_jd_ut;
    taiyin_split_julian_date second_contact_jd_ut;
    taiyin_split_julian_date third_contact_jd_ut;
    taiyin_split_julian_date fourth_contact_jd_ut;
    double separation_rad;
    double moon_radius_rad;
    double target_radius_rad;
    double margin_rad;
    taiyin_lunar_occultation_phenomena phenomena;
    taiyin_split_julian_date candidate_jd_ut;
    taiyin_split_julian_date next_search_jd_ut;
    int32_t candidate_count;
    int32_t iteration_count;
    int32_t evaluation_count;
} taiyin_lunar_occultation_result;

typedef struct taiyin_lunar_occultation_visibility_interval {
    uint32_t struct_size;
    uint8_t valid;
    uint8_t reserved[3];
    taiyin_split_julian_date begin_jd_ut;
    taiyin_split_julian_date end_jd_ut;
} taiyin_lunar_occultation_visibility_interval;

typedef struct taiyin_lunar_occultation_visibility_sample {
    uint32_t struct_size;
    uint8_t valid;
    uint8_t reserved[3];
    taiyin_split_julian_date jd_ut;
    double moon_altitude_rad;
    double moon_azimuth_rad;
    double target_altitude_rad;
    double target_azimuth_rad;
    double sun_altitude_rad;
    double sun_azimuth_rad;
    uint32_t visibility_flags;
} taiyin_lunar_occultation_visibility_sample;

typedef struct taiyin_lunar_occultation_local_visibility {
    uint32_t struct_size;
    taiyin_lunar_occultation_visibility_sample first_contact;
    taiyin_lunar_occultation_visibility_sample second_contact;
    taiyin_lunar_occultation_visibility_sample maximum;
    taiyin_lunar_occultation_visibility_sample third_contact;
    taiyin_lunar_occultation_visibility_sample fourth_contact;
    taiyin_split_julian_date target_rise_jd_ut;
    taiyin_split_julian_date target_set_jd_ut;
    taiyin_split_julian_date visible_begin_jd_ut;
    taiyin_split_julian_date visible_end_jd_ut;
    taiyin_split_julian_date dark_visible_begin_jd_ut;
    taiyin_split_julian_date dark_visible_end_jd_ut;
    int32_t visible_interval_count;
    taiyin_lunar_occultation_visibility_interval
        visible_intervals[TAIYIN_C_OCCULTATION_MAX_VISIBILITY_INTERVALS];
    int32_t dark_visible_interval_count;
    taiyin_lunar_occultation_visibility_interval
        dark_visible_intervals[TAIYIN_C_OCCULTATION_MAX_VISIBILITY_INTERVALS];
    uint32_t visibility_flags;
} taiyin_lunar_occultation_local_visibility;

typedef struct taiyin_lunar_occultation_path_point {
    uint32_t struct_size;
    uint8_t valid;
    uint8_t reserved[3];
    taiyin_split_julian_date jd_ut;
    double longitude_deg;
    double latitude_deg;
    double height_m;
} taiyin_lunar_occultation_path_point;

typedef struct taiyin_lunar_occultation_where_result {
    uint32_t struct_size;
    uint8_t center_line_hits_earth;
    uint8_t reserved[3];
    uint32_t type_flags;
    taiyin_split_julian_date jd_ut;
    taiyin_split_julian_date center_line_begin_jd_ut;
    taiyin_split_julian_date center_line_end_jd_ut;
    int32_t center_line_path_count;
    taiyin_lunar_occultation_path_point
        center_line_path[TAIYIN_C_OCCULTATION_WHERE_MAX_PATH_POINTS];
    double center_line_min_longitude_deg;
    double center_line_max_longitude_deg;
    double center_line_min_latitude_deg;
    double center_line_max_latitude_deg;
    double center_line_path_distance_km;
    int32_t outer_limit_path_count;
    taiyin_lunar_occultation_path_point
        outer_north_path[TAIYIN_C_OCCULTATION_WHERE_MAX_PATH_POINTS];
    taiyin_lunar_occultation_path_point
        outer_south_path[TAIYIN_C_OCCULTATION_WHERE_MAX_PATH_POINTS];
    double outer_limit_mean_width_km;
    double outer_limit_max_width_km;
    int32_t visible_region_polygon_count;
    taiyin_lunar_occultation_path_point
        visible_region_polygon[TAIYIN_C_OCCULTATION_WHERE_MAX_POLYGON_POINTS];
    double visible_region_min_longitude_deg;
    double visible_region_max_longitude_deg;
    double visible_region_min_latitude_deg;
    double visible_region_max_latitude_deg;
    double longitude_deg;
    double latitude_deg;
    double height_m;
    double separation_rad;
    double moon_radius_rad;
    double target_radius_rad;
    double margin_rad;
    taiyin_lunar_occultation_phenomena phenomena;
    taiyin_lunar_occultation_visibility_sample local_sample;
    uint32_t visibility_flags;
} taiyin_lunar_occultation_where_result;

TAIYIN_C_API void TAIYIN_C_CALL taiyin_lunar_occultation_result_init(
    taiyin_lunar_occultation_result* value
);
TAIYIN_C_API void TAIYIN_C_CALL taiyin_lunar_occultation_local_visibility_init(
    taiyin_lunar_occultation_local_visibility* value
);
TAIYIN_C_API void TAIYIN_C_CALL taiyin_lunar_occultation_where_result_init(
    taiyin_lunar_occultation_where_result* value
);

TAIYIN_C_API taiyin_call_result TAIYIN_C_CALL
taiyin_search_next_geocentric_lunar_star_occultation_ut(
    const taiyin_context* context,
    const char* star_key,
    const taiyin_split_julian_date* jd_start_ut,
    uint64_t flags,
    taiyin_lunar_occultation_result* out,
    taiyin_ephemeris_diagnostic* diagnostic
);
TAIYIN_C_API taiyin_call_result TAIYIN_C_CALL
taiyin_search_next_local_lunar_star_occultation_ut(
    const taiyin_context* context,
    const char* star_key,
    const taiyin_split_julian_date* jd_start_ut,
    uint64_t flags,
    taiyin_lunar_occultation_result* out,
    taiyin_ephemeris_diagnostic* diagnostic
);
TAIYIN_C_API taiyin_call_result TAIYIN_C_CALL
taiyin_search_next_geocentric_lunar_body_occultation_ut(
    const taiyin_context* context,
    int32_t body_id,
    const taiyin_split_julian_date* jd_start_ut,
    uint64_t flags,
    taiyin_lunar_occultation_result* out,
    taiyin_ephemeris_diagnostic* diagnostic
);
TAIYIN_C_API taiyin_call_result TAIYIN_C_CALL
taiyin_search_next_geocentric_lunar_body_occultation_with_radius_ut(
    const taiyin_context* context,
    int32_t body_id,
    double target_radius_km,
    const taiyin_split_julian_date* jd_start_ut,
    uint64_t flags,
    taiyin_lunar_occultation_result* out,
    taiyin_ephemeris_diagnostic* diagnostic
);
TAIYIN_C_API taiyin_call_result TAIYIN_C_CALL
taiyin_search_next_local_lunar_body_occultation_ut(
    const taiyin_context* context,
    int32_t body_id,
    const taiyin_split_julian_date* jd_start_ut,
    uint64_t flags,
    taiyin_lunar_occultation_result* out,
    taiyin_ephemeris_diagnostic* diagnostic
);
TAIYIN_C_API taiyin_call_result TAIYIN_C_CALL
taiyin_search_next_local_lunar_body_occultation_with_radius_ut(
    const taiyin_context* context,
    int32_t body_id,
    double target_radius_km,
    const taiyin_split_julian_date* jd_start_ut,
    uint64_t flags,
    taiyin_lunar_occultation_result* out,
    taiyin_ephemeris_diagnostic* diagnostic
);
TAIYIN_C_API taiyin_call_result TAIYIN_C_CALL
taiyin_compute_lunar_star_occultation_local_visibility_ut(
    const taiyin_context* context,
    const char* star_key,
    const taiyin_lunar_occultation_result* occultation,
    uint64_t visibility_flags,
    taiyin_lunar_occultation_local_visibility* out,
    taiyin_ephemeris_diagnostic* diagnostic
);
TAIYIN_C_API taiyin_call_result TAIYIN_C_CALL
taiyin_compute_lunar_body_occultation_local_visibility_ut(
    const taiyin_context* context,
    int32_t body_id,
    const taiyin_lunar_occultation_result* occultation,
    uint64_t visibility_flags,
    taiyin_lunar_occultation_local_visibility* out,
    taiyin_ephemeris_diagnostic* diagnostic
);
TAIYIN_C_API taiyin_call_result TAIYIN_C_CALL
taiyin_compute_lunar_star_occultation_where_ut(
    const taiyin_context* context,
    const char* star_key,
    const taiyin_lunar_occultation_result* occultation,
    uint64_t flags,
    taiyin_lunar_occultation_where_result* out,
    taiyin_ephemeris_diagnostic* diagnostic
);
TAIYIN_C_API taiyin_call_result TAIYIN_C_CALL
taiyin_compute_lunar_body_occultation_where_ut(
    const taiyin_context* context,
    int32_t body_id,
    const taiyin_lunar_occultation_result* occultation,
    uint64_t flags,
    taiyin_lunar_occultation_where_result* out,
    taiyin_ephemeris_diagnostic* diagnostic
);
TAIYIN_C_API taiyin_call_result TAIYIN_C_CALL
taiyin_compute_lunar_body_occultation_where_with_radius_ut(
    const taiyin_context* context,
    int32_t body_id,
    double target_radius_km,
    const taiyin_lunar_occultation_result* occultation,
    uint64_t flags,
    taiyin_lunar_occultation_where_result* out,
    taiyin_ephemeris_diagnostic* diagnostic
);

#ifdef __cplusplus
}
#endif

#endif
