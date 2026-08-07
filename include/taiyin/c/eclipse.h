#ifndef TAIYIN_C_ECLIPSE_H
#define TAIYIN_C_ECLIPSE_H

#include "taiyin/c/context.h"

#ifdef __cplusplus
extern "C" {
#endif

#define TAIYIN_ECLIPSE_OPTION_INCLUDE_CONTACTS (UINT64_C(1) << 33)
#define TAIYIN_ECLIPSE_OPTION_EXCLUDE_PENUMBRAL (UINT64_C(1) << 34)
#define TAIYIN_ECLIPSE_OPTION_BACKWARD (UINT64_C(1) << 35)
#define TAIYIN_ECLIPSE_OPTION_LOCAL_REFRACTION (UINT64_C(1) << 37)
#define TAIYIN_ECLIPSE_OPTION_LUNAR_LIMB_CORRECTION (UINT64_C(1) << 38)

// Local solar-eclipse visibility-window semantics. LOCAL_REFRACTION selects
// the apparent (refracted) rise/set window; LOCAL_STRICT_METEOROLOGY is only
// valid together with LOCAL_REFRACTION and forbids the standard-atmosphere
// fallback. Neither set (the default) keeps the geometric rise/set window.
#define TAIYIN_ECLIPSE_OPTION_LOCAL_STRICT_METEOROLOGY (UINT64_C(1) << 32)

enum taiyin_eclipse_kind_flags {
    TAIYIN_C_ECLIPSE_NONE = 0,
    TAIYIN_C_ECLIPSE_PENUMBRAL = 1u << 0,
    TAIYIN_C_ECLIPSE_PARTIAL = 1u << 1,
    TAIYIN_C_ECLIPSE_TOTAL = 1u << 2,
    TAIYIN_C_ECLIPSE_ANNULAR = 1u << 3,
    TAIYIN_C_ECLIPSE_HYBRID = 1u << 4,
    TAIYIN_C_ECLIPSE_CENTRAL = 1u << 5,
    TAIYIN_C_ECLIPSE_NONCENTRAL = 1u << 6,
    TAIYIN_C_ECLIPSE_VISIBLE_AT_OBSERVER = 1u << 7,
    TAIYIN_C_ECLIPSE_MAXIMUM_VISIBLE = 1u << 8,
    TAIYIN_C_ECLIPSE_PARTIAL_BEGIN_VISIBLE = 1u << 9,
    TAIYIN_C_ECLIPSE_TOTAL_BEGIN_VISIBLE = 1u << 10,
    TAIYIN_C_ECLIPSE_TOTAL_END_VISIBLE = 1u << 11,
    TAIYIN_C_ECLIPSE_PARTIAL_END_VISIBLE = 1u << 12,
    TAIYIN_C_ECLIPSE_PENUMBRAL_BEGIN_VISIBLE = 1u << 13,
    TAIYIN_C_ECLIPSE_PENUMBRAL_END_VISIBLE = 1u << 14,
    TAIYIN_C_ECLIPSE_OCCULTATION_BEGIN_IN_DAYLIGHT = 1u << 15,
    TAIYIN_C_ECLIPSE_OCCULTATION_END_IN_DAYLIGHT = 1u << 16
};

enum {
    TAIYIN_C_LUNAR_ECLIPSE_CONTACT_COUNT = 7,
    TAIYIN_C_SOLAR_ECLIPSE_CONTACT_COUNT = 5,
    TAIYIN_C_LOCAL_SOLAR_CONTACT_COUNT = 5,
    TAIYIN_C_SOLAR_BESSELIAN_COEFF_COUNT = 8
};

enum taiyin_solar_route_curve_kind {
    TAIYIN_C_SOLAR_ROUTE_CURVE_PARTIAL_BEGIN_A = 0,
    TAIYIN_C_SOLAR_ROUTE_CURVE_PARTIAL_BEGIN_B = 1,
    TAIYIN_C_SOLAR_ROUTE_CURVE_PARTIAL_END_A = 2,
    TAIYIN_C_SOLAR_ROUTE_CURVE_PARTIAL_END_B = 3,
    TAIYIN_C_SOLAR_ROUTE_CURVE_SUNRISE_MAX_A = 4,
    TAIYIN_C_SOLAR_ROUTE_CURVE_SUNRISE_MAX_B = 5,
    TAIYIN_C_SOLAR_ROUTE_CURVE_SUNSET_MAX_A = 6,
    TAIYIN_C_SOLAR_ROUTE_CURVE_SUNSET_MAX_B = 7,
    TAIYIN_C_SOLAR_ROUTE_CURVE_CENTER_LINE = 8,
    TAIYIN_C_SOLAR_ROUTE_CURVE_PENUMBRAL_NORTH = 9,
    TAIYIN_C_SOLAR_ROUTE_CURVE_PENUMBRAL_SOUTH = 10,
    TAIYIN_C_SOLAR_ROUTE_CURVE_CORE_NORTH = 11,
    TAIYIN_C_SOLAR_ROUTE_CURVE_CORE_SOUTH = 12,
    TAIYIN_C_SOLAR_ROUTE_CURVE_HALF_MAGNITUDE_NORTH = 13,
    TAIYIN_C_SOLAR_ROUTE_CURVE_HALF_MAGNITUDE_SOUTH = 14,
    TAIYIN_C_SOLAR_ROUTE_CURVE_UMBRA_OUTLINE = 15,
    TAIYIN_C_SOLAR_ROUTE_CURVE_PENUMBRA_OUTLINE = 16,
    TAIYIN_C_SOLAR_ROUTE_CURVE_TERMINATOR = 17,
    TAIYIN_C_SOLAR_ROUTE_CURVE_CORE_BEGIN_HORIZON = 18,
    TAIYIN_C_SOLAR_ROUTE_CURVE_CORE_END_HORIZON = 19,
    TAIYIN_C_SOLAR_ROUTE_CURVE_HALF_MAGNITUDE_SUNRISE_A = 20,
    TAIYIN_C_SOLAR_ROUTE_CURVE_HALF_MAGNITUDE_SUNRISE_B = 21,
    TAIYIN_C_SOLAR_ROUTE_CURVE_HALF_MAGNITUDE_SUNSET_A = 22,
    TAIYIN_C_SOLAR_ROUTE_CURVE_HALF_MAGNITUDE_SUNSET_B = 23
};

enum taiyin_solar_route_product_flags {
    TAIYIN_C_SOLAR_ROUTE_PRODUCT_HAS_CENTER_LINE = 1u << 0,
    TAIYIN_C_SOLAR_ROUTE_PRODUCT_HAS_CORE_LIMITS = 1u << 1,
    TAIYIN_C_SOLAR_ROUTE_PRODUCT_HAS_PENUMBRAL_LIMITS = 1u << 2,
    TAIYIN_C_SOLAR_ROUTE_PRODUCT_HAS_CORE_POLYGON = 1u << 3,
    TAIYIN_C_SOLAR_ROUTE_PRODUCT_CROSSES_ANTIMERIDIAN = 1u << 4,
    TAIYIN_C_SOLAR_ROUTE_PRODUCT_HAS_HALF_MAGNITUDE_LIMITS = 1u << 5,
    TAIYIN_C_SOLAR_ROUTE_PRODUCT_HAS_PENUMBRAL_POLYGON = 1u << 6,
    TAIYIN_C_SOLAR_ROUTE_PRODUCT_HAS_HALF_MAGNITUDE_POLYGON = 1u << 7
};

typedef struct taiyin_lunar_eclipse_result_tt {
    uint32_t struct_size;
    uint32_t kind;
    taiyin_split_julian_date maximum_jd_tt;
    double umbral_magnitude;
    double penumbral_magnitude;
    double axis_distance_rad;
    double umbra_radius_rad;
    double penumbra_radius_rad;
    double moon_radius_rad;
    taiyin_split_julian_date contact_jd_tt[TAIYIN_C_LUNAR_ECLIPSE_CONTACT_COUNT];
} taiyin_lunar_eclipse_result_tt;

typedef struct taiyin_lunar_eclipse_result_ut {
    uint32_t struct_size;
    uint32_t kind;
    taiyin_split_julian_date maximum_jd_ut;
    double delta_t_seconds;
    double umbral_magnitude;
    double penumbral_magnitude;
    double axis_distance_rad;
    double umbra_radius_rad;
    double penumbra_radius_rad;
    double moon_radius_rad;
    taiyin_split_julian_date contact_jd_ut[TAIYIN_C_LUNAR_ECLIPSE_CONTACT_COUNT];
} taiyin_lunar_eclipse_result_ut;

typedef struct taiyin_local_lunar_eclipse_result_tt {
    uint32_t struct_size;
    uint32_t eclipse_kind;
    uint32_t visibility_flags;
    taiyin_split_julian_date maximum_jd_tt;
    double umbral_magnitude;
    double penumbral_magnitude;
    taiyin_split_julian_date contact_jd_tt[TAIYIN_C_LUNAR_ECLIPSE_CONTACT_COUNT];
    double contact_moon_altitude_deg[TAIYIN_C_LUNAR_ECLIPSE_CONTACT_COUNT];
    double contact_moon_azimuth_deg[TAIYIN_C_LUNAR_ECLIPSE_CONTACT_COUNT];
    taiyin_split_julian_date moonrise_jd_tt;
    taiyin_split_julian_date moonset_jd_tt;
} taiyin_local_lunar_eclipse_result_tt;

typedef struct taiyin_local_lunar_eclipse_result_ut {
    uint32_t struct_size;
    uint32_t eclipse_kind;
    uint32_t visibility_flags;
    taiyin_split_julian_date maximum_jd_ut;
    double delta_t_seconds;
    double umbral_magnitude;
    double penumbral_magnitude;
    taiyin_split_julian_date contact_jd_ut[TAIYIN_C_LUNAR_ECLIPSE_CONTACT_COUNT];
    double contact_moon_altitude_deg[TAIYIN_C_LUNAR_ECLIPSE_CONTACT_COUNT];
    double contact_moon_azimuth_deg[TAIYIN_C_LUNAR_ECLIPSE_CONTACT_COUNT];
    taiyin_split_julian_date moonrise_jd_ut;
    taiyin_split_julian_date moonset_jd_ut;
} taiyin_local_lunar_eclipse_result_ut;

typedef struct taiyin_solar_eclipse_result_tt {
    uint32_t struct_size;
    uint32_t kind;
    taiyin_split_julian_date maximum_jd_tt;
    double axis_distance_km;
    double penumbra_radius_km;
    double core_radius_km;
    double penumbral_margin_km;
    double central_margin_km;
    double maximum_latitude_deg;
    double maximum_longitude_deg;
    taiyin_split_julian_date contact_jd_tt[TAIYIN_C_SOLAR_ECLIPSE_CONTACT_COUNT];
} taiyin_solar_eclipse_result_tt;

typedef struct taiyin_solar_eclipse_result_ut {
    uint32_t struct_size;
    uint32_t kind;
    taiyin_split_julian_date maximum_jd_ut;
    double delta_t_seconds;
    double axis_distance_km;
    double penumbra_radius_km;
    double core_radius_km;
    double penumbral_margin_km;
    double central_margin_km;
    double maximum_latitude_deg;
    double maximum_longitude_deg;
    taiyin_split_julian_date contact_jd_ut[TAIYIN_C_SOLAR_ECLIPSE_CONTACT_COUNT];
} taiyin_solar_eclipse_result_ut;

typedef struct taiyin_local_solar_eclipse_circumstances_tt {
    uint32_t struct_size;
    taiyin_split_julian_date jd_tt;
    double magnitude;
    double obscuration;
    double center_separation_deg;
    double sun_angular_radius_deg;
    double moon_angular_radius_deg;
    double sun_altitude_deg;
    double sun_azimuth_deg;
} taiyin_local_solar_eclipse_circumstances_tt;

typedef struct taiyin_local_solar_eclipse_circumstances_ut {
    uint32_t struct_size;
    taiyin_split_julian_date jd_ut;
    double delta_t_seconds;
    double magnitude;
    double obscuration;
    double center_separation_deg;
    double sun_angular_radius_deg;
    double moon_angular_radius_deg;
    double sun_altitude_deg;
    double sun_azimuth_deg;
} taiyin_local_solar_eclipse_circumstances_ut;

typedef struct taiyin_local_solar_eclipse_result_tt {
    uint32_t struct_size;
    uint32_t kind;
    taiyin_split_julian_date maximum_jd_tt;
    double magnitude;
    double obscuration;
    double sun_altitude_deg;
    double sun_azimuth_deg;
    taiyin_split_julian_date contact_jd_tt[TAIYIN_C_LOCAL_SOLAR_CONTACT_COUNT];
    double position_angle_c1_deg;
    double position_angle_c4_deg;
    double vertex_angle_c1_deg;
    double vertex_angle_c4_deg;
    double sunrise_magnitude;
    double sunset_magnitude;
    double duration_seconds;
    double moon_sun_radius_ratio;
} taiyin_local_solar_eclipse_result_tt;

typedef struct taiyin_local_solar_eclipse_result_ut {
    uint32_t struct_size;
    uint32_t kind;
    taiyin_split_julian_date maximum_jd_ut;
    double delta_t_seconds;
    double magnitude;
    double obscuration;
    double sun_altitude_deg;
    double sun_azimuth_deg;
    taiyin_split_julian_date contact_jd_ut[TAIYIN_C_LOCAL_SOLAR_CONTACT_COUNT];
    double position_angle_c1_deg;
    double position_angle_c4_deg;
    double vertex_angle_c1_deg;
    double vertex_angle_c4_deg;
    double sunrise_magnitude;
    double sunset_magnitude;
    double duration_seconds;
    double moon_sun_radius_ratio;
} taiyin_local_solar_eclipse_result_ut;

typedef struct taiyin_local_solar_eclipse_boundary {
    uint32_t struct_size;
    double center_longitude_deg;
    double center_latitude_deg;
    uint32_t center_kind;
    double umbra_north_longitude_deg;
    double umbra_north_latitude_deg;
    double umbra_south_longitude_deg;
    double umbra_south_latitude_deg;
    double penumbra_north_longitude_deg;
    double penumbra_north_latitude_deg;
    double penumbra_south_longitude_deg;
    double penumbra_south_latitude_deg;
    double umbra_width_km;
} taiyin_local_solar_eclipse_boundary;

typedef struct taiyin_solar_eclipse_path_point {
    uint32_t struct_size;
    taiyin_split_julian_date jd_tt;
    taiyin_split_julian_date jd_ut;
    double latitude_deg;
    double longitude_deg;
    double elevation_m;
    double sun_altitude_deg;
    double sun_azimuth_deg;
} taiyin_solar_eclipse_path_point;

typedef struct taiyin_solar_eclipse_route_row {
    uint32_t struct_size;
    taiyin_split_julian_date jd_tt;
    taiyin_split_julian_date jd_ut;
    taiyin_solar_eclipse_path_point center_line;
    taiyin_solar_eclipse_path_point penumbral_north_limit;
    taiyin_solar_eclipse_path_point penumbral_south_limit;
    taiyin_solar_eclipse_path_point north_limit;
    taiyin_solar_eclipse_path_point south_limit;
    taiyin_solar_eclipse_path_point half_magnitude_north_limit;
    taiyin_solar_eclipse_path_point half_magnitude_south_limit;
    double path_width_km;
    double duration_seconds;
    double sun_altitude_deg;
    double sun_azimuth_deg;
} taiyin_solar_eclipse_route_row;

typedef struct taiyin_solar_eclipse_route_curve_point {
    uint32_t struct_size;
    taiyin_split_julian_date jd_tt;
    taiyin_split_julian_date jd_ut;
    uint32_t curve_kind;
    double latitude_deg;
    double longitude_deg;
} taiyin_solar_eclipse_route_curve_point;

typedef struct taiyin_solar_eclipse_route_product_point {
    uint32_t struct_size;
    taiyin_split_julian_date jd_tt;
    taiyin_split_julian_date jd_ut;
    uint32_t point_kind;
    uint32_t source_curve_kind;
    double latitude_deg;
    double longitude_deg;
    double unwrapped_longitude_deg;
} taiyin_solar_eclipse_route_product_point;

typedef struct taiyin_solar_eclipse_route_product_summary {
    uint32_t struct_size;
    uint32_t flags;
    size_t curve_point_count;
    size_t center_line_count;
    size_t core_north_count;
    size_t core_south_count;
    size_t core_begin_horizon_count;
    size_t core_end_horizon_count;
    size_t penumbral_north_count;
    size_t penumbral_south_count;
    size_t half_magnitude_north_count;
    size_t half_magnitude_south_count;
    size_t core_polygon_point_count;
    size_t penumbral_polygon_point_count;
    size_t half_magnitude_polygon_point_count;
    size_t polygon_point_count;
    double min_latitude_deg;
    double max_latitude_deg;
    double min_unwrapped_longitude_deg;
    double max_unwrapped_longitude_deg;
} taiyin_solar_eclipse_route_product_summary;

typedef struct taiyin_solar_besselian_elements {
    uint32_t struct_size;
    double t_hours;
    double x;
    double y;
    double zeta;
    double d_deg;
    double mu_deg;
    double l1;
    double l2;
    double f1_deg;
    double f2_deg;
    double tan_f1;
    double tan_f2;
    double gamma;
} taiyin_solar_besselian_elements;

typedef struct taiyin_solar_besselian_polynomial {
    uint32_t struct_size;
    taiyin_split_julian_date t0_jd_tt;
    double span_hours;
    double sample_step_hours;
    int32_t degree;
    double x[TAIYIN_C_SOLAR_BESSELIAN_COEFF_COUNT];
    double y[TAIYIN_C_SOLAR_BESSELIAN_COEFF_COUNT];
    double zeta[TAIYIN_C_SOLAR_BESSELIAN_COEFF_COUNT];
    double d_deg[TAIYIN_C_SOLAR_BESSELIAN_COEFF_COUNT];
    double mu_deg[TAIYIN_C_SOLAR_BESSELIAN_COEFF_COUNT];
    double l1[TAIYIN_C_SOLAR_BESSELIAN_COEFF_COUNT];
    double l2[TAIYIN_C_SOLAR_BESSELIAN_COEFF_COUNT];
    double f1_deg;
    double f2_deg;
    double tan_f1;
    double tan_f2;
    taiyin_solar_besselian_elements center;
    taiyin_solar_besselian_elements max_residual;
} taiyin_solar_besselian_polynomial;

TAIYIN_C_API void TAIYIN_C_CALL taiyin_lunar_eclipse_result_tt_init(taiyin_lunar_eclipse_result_tt*);
TAIYIN_C_API void TAIYIN_C_CALL taiyin_lunar_eclipse_result_ut_init(taiyin_lunar_eclipse_result_ut*);
TAIYIN_C_API void TAIYIN_C_CALL taiyin_local_lunar_eclipse_result_tt_init(taiyin_local_lunar_eclipse_result_tt*);
TAIYIN_C_API void TAIYIN_C_CALL taiyin_local_lunar_eclipse_result_ut_init(taiyin_local_lunar_eclipse_result_ut*);
TAIYIN_C_API void TAIYIN_C_CALL taiyin_solar_eclipse_result_tt_init(taiyin_solar_eclipse_result_tt*);
TAIYIN_C_API void TAIYIN_C_CALL taiyin_solar_eclipse_result_ut_init(taiyin_solar_eclipse_result_ut*);
TAIYIN_C_API void TAIYIN_C_CALL taiyin_local_solar_eclipse_result_tt_init(taiyin_local_solar_eclipse_result_tt*);
TAIYIN_C_API void TAIYIN_C_CALL taiyin_local_solar_eclipse_result_ut_init(taiyin_local_solar_eclipse_result_ut*);
TAIYIN_C_API void TAIYIN_C_CALL taiyin_local_solar_eclipse_circumstances_tt_init(taiyin_local_solar_eclipse_circumstances_tt*);
TAIYIN_C_API void TAIYIN_C_CALL taiyin_local_solar_eclipse_circumstances_ut_init(taiyin_local_solar_eclipse_circumstances_ut*);
TAIYIN_C_API void TAIYIN_C_CALL taiyin_local_solar_eclipse_boundary_init(taiyin_local_solar_eclipse_boundary*);
TAIYIN_C_API void TAIYIN_C_CALL taiyin_solar_eclipse_route_row_init(taiyin_solar_eclipse_route_row*);
TAIYIN_C_API void TAIYIN_C_CALL taiyin_solar_eclipse_route_product_summary_init(taiyin_solar_eclipse_route_product_summary*);
TAIYIN_C_API void TAIYIN_C_CALL taiyin_solar_besselian_elements_init(taiyin_solar_besselian_elements*);
TAIYIN_C_API void TAIYIN_C_CALL taiyin_solar_besselian_polynomial_init(taiyin_solar_besselian_polynomial*);

TAIYIN_C_API taiyin_status TAIYIN_C_CALL taiyin_solve_lunar_eclipse_at_tt(const taiyin_context*, const taiyin_split_julian_date*, uint64_t, taiyin_lunar_eclipse_result_tt*, taiyin_ephemeris_diagnostic*);
TAIYIN_C_API taiyin_status TAIYIN_C_CALL taiyin_solve_lunar_eclipse_at_ut(const taiyin_context*, const taiyin_split_julian_date*, uint64_t, taiyin_lunar_eclipse_result_ut*, taiyin_ephemeris_diagnostic*);
TAIYIN_C_API taiyin_status TAIYIN_C_CALL taiyin_search_next_lunar_eclipse_tt(const taiyin_context*, const taiyin_split_julian_date*, uint32_t, uint64_t, taiyin_lunar_eclipse_result_tt*, taiyin_ephemeris_diagnostic*);
TAIYIN_C_API taiyin_status TAIYIN_C_CALL taiyin_search_next_lunar_eclipse_ut(const taiyin_context*, const taiyin_split_julian_date*, uint32_t, uint64_t, taiyin_lunar_eclipse_result_ut*, taiyin_ephemeris_diagnostic*);
TAIYIN_C_API taiyin_status TAIYIN_C_CALL taiyin_search_lunar_eclipses_tt(const taiyin_context*, const taiyin_split_julian_date*, const taiyin_split_julian_date*, uint32_t, uint64_t, taiyin_lunar_eclipse_result_tt*, size_t, size_t*, taiyin_ephemeris_diagnostic*);
TAIYIN_C_API taiyin_status TAIYIN_C_CALL taiyin_search_lunar_eclipses_ut(const taiyin_context*, const taiyin_split_julian_date*, const taiyin_split_julian_date*, uint32_t, uint64_t, taiyin_lunar_eclipse_result_ut*, size_t, size_t*, taiyin_ephemeris_diagnostic*);
TAIYIN_C_API taiyin_status TAIYIN_C_CALL taiyin_compute_local_lunar_eclipse_visibility_tt(const taiyin_context*, const taiyin_lunar_eclipse_result_tt*, uint64_t, taiyin_local_lunar_eclipse_result_tt*, taiyin_ephemeris_diagnostic*);
TAIYIN_C_API taiyin_status TAIYIN_C_CALL taiyin_compute_local_lunar_eclipse_visibility_ut(const taiyin_context*, const taiyin_lunar_eclipse_result_ut*, uint64_t, taiyin_local_lunar_eclipse_result_ut*, taiyin_ephemeris_diagnostic*);
TAIYIN_C_API taiyin_status TAIYIN_C_CALL taiyin_search_next_local_lunar_eclipse_tt(const taiyin_context*, const taiyin_split_julian_date*, uint32_t, uint64_t, taiyin_local_lunar_eclipse_result_tt*, taiyin_ephemeris_diagnostic*);
TAIYIN_C_API taiyin_status TAIYIN_C_CALL taiyin_search_next_local_lunar_eclipse_ut(const taiyin_context*, const taiyin_split_julian_date*, uint32_t, uint64_t, taiyin_local_lunar_eclipse_result_ut*, taiyin_ephemeris_diagnostic*);

TAIYIN_C_API taiyin_status TAIYIN_C_CALL taiyin_solve_solar_eclipse_at_tt(const taiyin_context*, const taiyin_split_julian_date*, uint64_t, taiyin_solar_eclipse_result_tt*, taiyin_ephemeris_diagnostic*);
TAIYIN_C_API taiyin_status TAIYIN_C_CALL taiyin_solve_solar_eclipse_at_ut(const taiyin_context*, const taiyin_split_julian_date*, uint64_t, taiyin_solar_eclipse_result_ut*, taiyin_ephemeris_diagnostic*);
TAIYIN_C_API taiyin_status TAIYIN_C_CALL taiyin_search_next_solar_eclipse_tt(const taiyin_context*, const taiyin_split_julian_date*, uint32_t, uint64_t, taiyin_solar_eclipse_result_tt*, taiyin_ephemeris_diagnostic*);
TAIYIN_C_API taiyin_status TAIYIN_C_CALL taiyin_search_next_solar_eclipse_ut(const taiyin_context*, const taiyin_split_julian_date*, uint32_t, uint64_t, taiyin_solar_eclipse_result_ut*, taiyin_ephemeris_diagnostic*);
TAIYIN_C_API taiyin_status TAIYIN_C_CALL taiyin_search_solar_eclipses_tt(const taiyin_context*, const taiyin_split_julian_date*, const taiyin_split_julian_date*, uint32_t, uint64_t, taiyin_solar_eclipse_result_tt*, size_t, size_t*, taiyin_ephemeris_diagnostic*);
TAIYIN_C_API taiyin_status TAIYIN_C_CALL taiyin_search_solar_eclipses_ut(const taiyin_context*, const taiyin_split_julian_date*, const taiyin_split_julian_date*, uint32_t, uint64_t, taiyin_solar_eclipse_result_ut*, size_t, size_t*, taiyin_ephemeris_diagnostic*);
TAIYIN_C_API taiyin_status TAIYIN_C_CALL taiyin_solve_local_solar_eclipse_at_tt(const taiyin_context*, const taiyin_split_julian_date*, uint64_t, taiyin_local_solar_eclipse_result_tt*, taiyin_ephemeris_diagnostic*);
TAIYIN_C_API taiyin_status TAIYIN_C_CALL taiyin_solve_local_solar_eclipse_at_ut(const taiyin_context*, const taiyin_split_julian_date*, uint64_t, taiyin_local_solar_eclipse_result_ut*, taiyin_ephemeris_diagnostic*);
TAIYIN_C_API taiyin_status TAIYIN_C_CALL taiyin_search_next_local_solar_eclipse_tt(const taiyin_context*, const taiyin_split_julian_date*, uint32_t, uint64_t, taiyin_local_solar_eclipse_result_tt*, taiyin_ephemeris_diagnostic*);
TAIYIN_C_API taiyin_status TAIYIN_C_CALL taiyin_search_next_local_solar_eclipse_ut(const taiyin_context*, const taiyin_split_julian_date*, uint32_t, uint64_t, taiyin_local_solar_eclipse_result_ut*, taiyin_ephemeris_diagnostic*);
TAIYIN_C_API taiyin_status TAIYIN_C_CALL taiyin_compute_local_solar_circumstances_tt(const taiyin_context*, const taiyin_split_julian_date*, taiyin_local_solar_eclipse_circumstances_tt*, taiyin_ephemeris_diagnostic*);
TAIYIN_C_API taiyin_status TAIYIN_C_CALL taiyin_compute_local_solar_circumstances_ut(const taiyin_context*, const taiyin_split_julian_date*, taiyin_local_solar_eclipse_circumstances_ut*, taiyin_ephemeris_diagnostic*);

TAIYIN_C_API taiyin_status TAIYIN_C_CALL taiyin_compute_solar_besselian_elements_tt(const taiyin_context*, const taiyin_split_julian_date*, double, taiyin_solar_besselian_elements*, taiyin_ephemeris_diagnostic*);
TAIYIN_C_API taiyin_status TAIYIN_C_CALL taiyin_compute_solar_besselian_polynomial_tt(const taiyin_context*, const taiyin_split_julian_date*, double, double, int32_t, taiyin_solar_besselian_polynomial*, taiyin_ephemeris_diagnostic*);
TAIYIN_C_API taiyin_status TAIYIN_C_CALL taiyin_evaluate_solar_besselian_polynomial(const taiyin_solar_besselian_polynomial*, double, taiyin_solar_besselian_elements*);

TAIYIN_C_API taiyin_status TAIYIN_C_CALL taiyin_compute_solar_eclipse_route_row_tt(const taiyin_context*, const taiyin_split_julian_date*, uint64_t, taiyin_solar_eclipse_route_row*, taiyin_ephemeris_diagnostic*);
TAIYIN_C_API taiyin_status TAIYIN_C_CALL taiyin_compute_solar_eclipse_route_row_ut(const taiyin_context*, const taiyin_split_julian_date*, uint64_t, taiyin_solar_eclipse_route_row*, taiyin_ephemeris_diagnostic*);
TAIYIN_C_API taiyin_status TAIYIN_C_CALL taiyin_compute_solar_eclipse_route_tt(const taiyin_context*, const taiyin_split_julian_date*, const taiyin_split_julian_date*, double, uint64_t, taiyin_solar_eclipse_route_row*, size_t, size_t*, taiyin_ephemeris_diagnostic*);
TAIYIN_C_API taiyin_status TAIYIN_C_CALL taiyin_compute_solar_eclipse_route_ut(const taiyin_context*, const taiyin_split_julian_date*, const taiyin_split_julian_date*, double, uint64_t, taiyin_solar_eclipse_route_row*, size_t, size_t*, taiyin_ephemeris_diagnostic*);
TAIYIN_C_API taiyin_status TAIYIN_C_CALL taiyin_compute_solar_eclipse_route_curves_tt(const taiyin_context*, const taiyin_split_julian_date*, uint64_t, size_t, taiyin_solar_eclipse_route_curve_point*, size_t, size_t*, taiyin_ephemeris_diagnostic*);
TAIYIN_C_API taiyin_status TAIYIN_C_CALL taiyin_compute_solar_eclipse_route_curves_ut(const taiyin_context*, const taiyin_split_julian_date*, uint64_t, size_t, taiyin_solar_eclipse_route_curve_point*, size_t, size_t*, taiyin_ephemeris_diagnostic*);
TAIYIN_C_API taiyin_status TAIYIN_C_CALL taiyin_compute_solar_eclipse_route_product_tt(const taiyin_context*, const taiyin_split_julian_date*, uint64_t, size_t, taiyin_solar_eclipse_route_product_point*, size_t, size_t*, taiyin_solar_eclipse_route_product_summary*, taiyin_ephemeris_diagnostic*);
TAIYIN_C_API taiyin_status TAIYIN_C_CALL taiyin_compute_solar_eclipse_route_product_ut(const taiyin_context*, const taiyin_split_julian_date*, uint64_t, size_t, taiyin_solar_eclipse_route_product_point*, size_t, size_t*, taiyin_solar_eclipse_route_product_summary*, taiyin_ephemeris_diagnostic*);
TAIYIN_C_API taiyin_status TAIYIN_C_CALL taiyin_compute_solar_eclipse_route_map_product_tt(const taiyin_context*, const taiyin_split_julian_date*, uint64_t, size_t, taiyin_solar_eclipse_route_product_point*, size_t, size_t*, taiyin_solar_eclipse_route_product_summary*, taiyin_ephemeris_diagnostic*);
TAIYIN_C_API taiyin_status TAIYIN_C_CALL taiyin_compute_solar_eclipse_route_map_product_ut(const taiyin_context*, const taiyin_split_julian_date*, uint64_t, size_t, taiyin_solar_eclipse_route_product_point*, size_t, size_t*, taiyin_solar_eclipse_route_product_summary*, taiyin_ephemeris_diagnostic*);
TAIYIN_C_API taiyin_status TAIYIN_C_CALL taiyin_compute_local_solar_eclipse_boundary_tt(const taiyin_context*, const taiyin_split_julian_date*, double, double, taiyin_local_solar_eclipse_boundary*, taiyin_ephemeris_diagnostic*);
TAIYIN_C_API taiyin_status TAIYIN_C_CALL taiyin_compute_local_solar_eclipse_boundary_ut(const taiyin_context*, const taiyin_split_julian_date*, double, double, taiyin_local_solar_eclipse_boundary*, taiyin_ephemeris_diagnostic*);

#ifdef __cplusplus
}
#endif

#endif
