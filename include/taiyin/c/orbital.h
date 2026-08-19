#ifndef TAIYIN_C_ORBITAL_H
#define TAIYIN_C_ORBITAL_H

#include "taiyin/c/position.h"

#ifdef __cplusplus
extern "C" {
#endif

#define TAIYIN_ORBITAL_OPTION_REVERSE (UINT64_C(1) << 32)

enum taiyin_body_apsis_kind {
    TAIYIN_APSIS_PERICENTER = 0,
    TAIYIN_APSIS_APOCENTER = 1
};

enum taiyin_body_node_kind {
    TAIYIN_NODE_ASCENDING = 0,
    TAIYIN_NODE_DESCENDING = 1
};

typedef struct taiyin_body_orbit_reference_point {
    taiyin_vector3 position_au;
    double longitude_rad;
    double latitude_rad;
    double distance_au;
} taiyin_body_orbit_reference_point;

typedef struct taiyin_body_orbit_reference_points {
    uint32_t struct_size;
    int32_t body_id;
    int32_t center_id;
    int32_t reference_frame_id;
    int32_t model_id;
    taiyin_body_orbit_reference_point ascending_node;
    taiyin_body_orbit_reference_point descending_node;
    taiyin_body_orbit_reference_point periapsis;
    taiyin_body_orbit_reference_point apoapsis;
    taiyin_body_orbit_reference_point second_focus;
} taiyin_body_orbit_reference_points;

typedef struct taiyin_body_osculating_orbit {
    uint32_t struct_size;
    int32_t body_id;
    int32_t center_id;
    int32_t reference_frame_id;
    double gravitational_parameter_au3_per_day2;
    double semi_major_axis_au;
    double eccentricity;
    double inclination_rad;
    double longitude_of_ascending_node_rad;
    double argument_of_periapsis_rad;
    double true_anomaly_rad;
    double mean_anomaly_rad;
    double periapsis_distance_au;
    double apoapsis_distance_au;
    double osculating_period_days;
    double current_distance_au;
    double radial_velocity_au_per_day;
} taiyin_body_osculating_orbit;

typedef struct taiyin_body_apsis_search_result {
    uint32_t struct_size;
    int32_t body_id;
    int32_t center_id;
    int32_t kind;
    taiyin_split_julian_date jd;
    double distance_au;
    double radial_velocity_au_per_day;
    int32_t iteration_count;
    int32_t evaluation_count;
} taiyin_body_apsis_search_result;

typedef struct taiyin_body_node_search_result {
    uint32_t struct_size;
    int32_t body_id;
    int32_t center_id;
    int32_t reference_frame_id;
    int32_t kind;
    taiyin_split_julian_date jd;
    double reference_plane_angle_rad;
    double distance_au;
    int32_t iteration_count;
    int32_t evaluation_count;
} taiyin_body_node_search_result;

TAIYIN_C_API void TAIYIN_C_CALL taiyin_body_osculating_orbit_init(
    taiyin_body_osculating_orbit* value
);
TAIYIN_C_API void TAIYIN_C_CALL taiyin_body_orbit_reference_points_init(
    taiyin_body_orbit_reference_points* value
);
TAIYIN_C_API void TAIYIN_C_CALL taiyin_body_apsis_search_result_init(
    taiyin_body_apsis_search_result* value
);
TAIYIN_C_API void TAIYIN_C_CALL taiyin_body_node_search_result_init(
    taiyin_body_node_search_result* value
);

TAIYIN_C_API taiyin_call_result TAIYIN_C_CALL taiyin_calc_body_osculating_orbit_tt(
    const taiyin_context* context,
    int32_t body_id,
    const taiyin_split_julian_date* jd_tt,
    int32_t reference_frame_id,
    uint64_t flags,
    taiyin_body_osculating_orbit* out,
    taiyin_ephemeris_diagnostic* diagnostic
);
TAIYIN_C_API taiyin_call_result TAIYIN_C_CALL taiyin_calc_body_osculating_orbit_ut(
    const taiyin_context* context,
    int32_t body_id,
    const taiyin_split_julian_date* jd_ut,
    int32_t reference_frame_id,
    uint64_t flags,
    taiyin_body_osculating_orbit* out,
    taiyin_ephemeris_diagnostic* diagnostic
);
TAIYIN_C_API taiyin_call_result TAIYIN_C_CALL taiyin_calc_body_orbit_reference_points_tt(
    const taiyin_context* context,
    int32_t body_id,
    const taiyin_split_julian_date* jd_tt,
    int32_t reference_frame_id,
    uint64_t flags,
    taiyin_body_orbit_reference_points* out,
    taiyin_ephemeris_diagnostic* diagnostic
);
TAIYIN_C_API taiyin_call_result TAIYIN_C_CALL taiyin_calc_body_orbit_reference_points_ut(
    const taiyin_context* context,
    int32_t body_id,
    const taiyin_split_julian_date* jd_ut,
    int32_t reference_frame_id,
    uint64_t flags,
    taiyin_body_orbit_reference_points* out,
    taiyin_ephemeris_diagnostic* diagnostic
);
TAIYIN_C_API taiyin_call_result TAIYIN_C_CALL taiyin_search_next_body_apsis_tt(
    const taiyin_context* context,
    int32_t body_id,
    int32_t kind,
    const taiyin_split_julian_date* jd_start_tt,
    uint64_t flags,
    taiyin_body_apsis_search_result* out,
    taiyin_ephemeris_diagnostic* diagnostic
);
TAIYIN_C_API taiyin_call_result TAIYIN_C_CALL taiyin_search_next_body_apsis_ut(
    const taiyin_context* context,
    int32_t body_id,
    int32_t kind,
    const taiyin_split_julian_date* jd_start_ut,
    uint64_t flags,
    taiyin_body_apsis_search_result* out,
    taiyin_ephemeris_diagnostic* diagnostic
);
TAIYIN_C_API taiyin_call_result TAIYIN_C_CALL taiyin_search_next_body_plane_node_tt(
    const taiyin_context* context,
    int32_t body_id,
    int32_t kind,
    const taiyin_split_julian_date* jd_start_tt,
    int32_t reference_frame_id,
    uint64_t flags,
    taiyin_body_node_search_result* out,
    taiyin_ephemeris_diagnostic* diagnostic
);
TAIYIN_C_API taiyin_call_result TAIYIN_C_CALL taiyin_search_next_body_plane_node_ut(
    const taiyin_context* context,
    int32_t body_id,
    int32_t kind,
    const taiyin_split_julian_date* jd_start_ut,
    int32_t reference_frame_id,
    uint64_t flags,
    taiyin_body_node_search_result* out,
    taiyin_ephemeris_diagnostic* diagnostic
);

#ifdef __cplusplus
}
#endif

#endif
