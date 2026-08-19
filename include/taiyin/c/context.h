#ifndef TAIYIN_C_CONTEXT_H
#define TAIYIN_C_CONTEXT_H

#include "taiyin/c/base.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct taiyin_context taiyin_context;

enum taiyin_atmosphere_policy_flags {
    TAIYIN_ATMOSPHERE_ALLOW_STANDARD_FALLBACK = 1u << 0
};

enum taiyin_model_selection_id {
    TAIYIN_MODEL_DEFAULT = -1
};

enum taiyin_precession_model_id {
    TAIYIN_PRECESSION_VONDRAK_2011 = 0,
    TAIYIN_PRECESSION_IAU_2006 = 1,
    TAIYIN_PRECESSION_IAU_1976 = 2,
    TAIYIN_PRECESSION_NEWCOMB_1895 = 3
};

enum taiyin_nutation_model_id {
    TAIYIN_NUTATION_IAU_2000B = 0,
    TAIYIN_NUTATION_IAU_2000A = 1
};

enum taiyin_tdb_model_id {
    TAIYIN_TDB_FAST_PERIODIC = 0,
    TAIYIN_TDB_SOFA_FULL = 1
};

enum taiyin_frame_route_id {
    TAIYIN_FRAME_ROUTE_EQUINOX = 0,
    TAIYIN_FRAME_ROUTE_CIRS = 1
};

enum taiyin_delta_t_model_id {
    TAIYIN_DELTA_T_ESTIMATED_DEFAULT = 0
};

enum taiyin_ephemeris_family_id {
    TAIYIN_EPHEMERIS_FAMILY_UNKNOWN = 0,
    TAIYIN_EPHEMERIS_FAMILY_DE431 = 431,
    TAIYIN_EPHEMERIS_FAMILY_DE441 = 441
};

enum taiyin_refraction_model_id {
    TAIYIN_REFRACTION_BENNETT = 0,
    TAIYIN_REFRACTION_SKYFIELD = 1,
    TAIYIN_REFRACTION_HYBRID = 2,
    TAIYIN_REFRACTION_AUER_STANDISH = 3,
    TAIYIN_REFRACTION_SOFA = 4
};

enum taiyin_heliacal_visibility_model_id {
    TAIYIN_HELIACAL_VISIBILITY_BELOKRYLOV_2011 = 0,
    TAIYIN_HELIACAL_VISIBILITY_SCHAEFER_1993 = 1
};

enum taiyin_aberration_model_id {
    TAIYIN_ABERRATION_ANNUAL_RELATIVISTIC = 0
};

enum taiyin_apparent_flags {
    TAIYIN_APPARENT_FLAG_LIGHT_TIME = 1u << 0,
    TAIYIN_APPARENT_FLAG_SPHERICAL = 1u << 2,
    TAIYIN_APPARENT_FLAG_ABERRATION = 1u << 3,
    TAIYIN_APPARENT_FLAG_DEFLECTION = 1u << 4,
    TAIYIN_APPARENT_FLAG_VELOCITY = 1u << 5,
    TAIYIN_APPARENT_FLAG_ACCELERATION = 1u << 6,
    TAIYIN_APPARENT_FLAG_SHAPIRO_DELAY = 1u << 7
};

enum taiyin_deflection_model_id {
    TAIYIN_DEFLECTION_ERFA = 0,
    TAIYIN_DEFLECTION_SOLAR_DISK = 1
};

enum taiyin_eclipse_shadow_model_id {
    TAIYIN_ECLIPSE_SHADOW_NASA_DANJON = 0,
    TAIYIN_ECLIPSE_SHADOW_CHAUVENET = 1,
    TAIYIN_ECLIPSE_SHADOW_GEOMETRIC = 2,
    TAIYIN_ECLIPSE_SHADOW_RAW_DANJON = 3
};

enum taiyin_eclipse_moon_radius_model_id {
    TAIYIN_ECLIPSE_MOON_ALMANAC = 0,
    TAIYIN_ECLIPSE_MOON_MEAN = 1
};

typedef struct taiyin_observer_location {
    uint32_t struct_size;
    double longitude_deg;
    double latitude_deg;
    double height_m;
} taiyin_observer_location;

typedef struct taiyin_atmosphere {
    uint32_t struct_size;
    double pressure_mbar;
    double temperature_celsius;
    double relative_humidity_percent;
    double wavelength_micrometer;
} taiyin_atmosphere;

typedef struct taiyin_astro_model_config {
    uint32_t struct_size;
    int32_t tdb_model_id;
    int32_t precession_model_id;
    int32_t nutation_model_id;
    int32_t obliquity_model_id;
    int32_t frame_route_id;
} taiyin_astro_model_config;

/*
 * Topocentric state and deflectors are configured through their dedicated
 * context functions and are intentionally not embedded in this value object.
 */
typedef struct taiyin_apparent_config {
    uint32_t struct_size;
    uint32_t flags;
    int32_t output_frame_id;
    int32_t light_time_method_id;
    int32_t shapiro_delay_model_id;
    int32_t aberration_model_id;
    int32_t deflection_model_id;
    int32_t max_light_time_iterations;
    double light_time_tolerance_days;
    double matrix_derivative_step_days;
} taiyin_apparent_config;

typedef struct taiyin_apparent_deflector {
    uint32_t struct_size;
    int32_t body_id;
    double schwarzschild_radius_au;
    double limit;
} taiyin_apparent_deflector;

TAIYIN_C_API void TAIYIN_C_CALL taiyin_observer_location_init(
    taiyin_observer_location* location
);
TAIYIN_C_API void TAIYIN_C_CALL taiyin_atmosphere_init(
    taiyin_atmosphere* atmosphere
);
TAIYIN_C_API void TAIYIN_C_CALL taiyin_astro_model_config_init(
    taiyin_astro_model_config* config
);
TAIYIN_C_API void TAIYIN_C_CALL taiyin_apparent_config_init(
    taiyin_apparent_config* config
);
TAIYIN_C_API void TAIYIN_C_CALL taiyin_apparent_deflector_init(
    taiyin_apparent_deflector* deflector
);
TAIYIN_C_API taiyin_call_result TAIYIN_C_CALL taiyin_context_create(
    taiyin_context** out_context
);
TAIYIN_C_API taiyin_call_result TAIYIN_C_CALL taiyin_context_clone(
    const taiyin_context* source,
    taiyin_context** out_context
);
TAIYIN_C_API void TAIYIN_C_CALL taiyin_context_destroy(taiyin_context* context);
TAIYIN_C_API taiyin_call_result TAIYIN_C_CALL taiyin_context_reset(
    taiyin_context* context
);

TAIYIN_C_API taiyin_call_result TAIYIN_C_CALL taiyin_context_set_observer_location(
    taiyin_context* context,
    const taiyin_observer_location* location
);
TAIYIN_C_API taiyin_call_result TAIYIN_C_CALL taiyin_context_clear_observer_location(
    taiyin_context* context
);
TAIYIN_C_API taiyin_call_result TAIYIN_C_CALL taiyin_context_set_atmosphere(
    taiyin_context* context,
    const taiyin_atmosphere* atmosphere
);
TAIYIN_C_API taiyin_call_result TAIYIN_C_CALL
taiyin_context_set_atmosphere_pressure_temperature(
    taiyin_context* context,
    double pressure_mbar,
    double temperature_celsius
);
TAIYIN_C_API taiyin_call_result TAIYIN_C_CALL taiyin_context_set_standard_atmosphere(
    taiyin_context* context
);
TAIYIN_C_API taiyin_call_result TAIYIN_C_CALL taiyin_context_set_atmosphere_policy(
    taiyin_context* context,
    uint32_t flags
);
TAIYIN_C_API taiyin_call_result TAIYIN_C_CALL taiyin_context_set_meteorological_range_km(
    taiyin_context* context,
    double range_km
);
TAIYIN_C_API taiyin_call_result TAIYIN_C_CALL taiyin_context_set_geocentric_observer(
    taiyin_context* context,
    int32_t observer_id,
    int32_t center_id
);
/* Topocentric observer setters are Earth-only in the 1.0 API and return
 * TAIYIN_ERROR_UNSUPPORTED when the context observer is not Earth. */
TAIYIN_C_API taiyin_call_result TAIYIN_C_CALL
taiyin_context_set_topocentric_observer_offset(
    taiyin_context* context,
    const taiyin_cartesian_state* observer_offset
);
TAIYIN_C_API taiyin_call_result TAIYIN_C_CALL
taiyin_context_set_simple_topocentric_observer(
    taiyin_context* context,
    const taiyin_observer_location* location,
    const taiyin_split_julian_date* jd_ut1,
    const taiyin_split_julian_date* jd_tt
);
TAIYIN_C_API taiyin_call_result TAIYIN_C_CALL
taiyin_context_set_precise_topocentric_observer(
    taiyin_context* context,
    const taiyin_observer_location* location,
    const taiyin_split_julian_date* jd_utc,
    const taiyin_split_julian_date* jd_tt
);
TAIYIN_C_API taiyin_call_result TAIYIN_C_CALL taiyin_context_set_route_rule(
    taiyin_context* context,
    uint64_t route_rule_id
);
/*
 * UTC entry points are strict by default. When enabled, missing or
 * out-of-range UTC/EOP data may fall back to approximate UT1 plus Delta-T.
 * This setting never changes the semantics of a *_ut entry point.
 */
TAIYIN_C_API taiyin_call_result TAIYIN_C_CALL
taiyin_context_set_allow_utc_out_of_range_estimate(
    taiyin_context* context,
    taiyin_bool allow
);
TAIYIN_C_API taiyin_call_result TAIYIN_C_CALL taiyin_context_set_delta_t_model(
    taiyin_context* context,
    int32_t delta_t_model_id,
    int32_t ephemeris_family_id
);
TAIYIN_C_API taiyin_call_result TAIYIN_C_CALL taiyin_context_set_tdb_model(
    taiyin_context* context,
    int32_t tdb_model_id
);
TAIYIN_C_API taiyin_call_result TAIYIN_C_CALL taiyin_context_set_astro_models(
    taiyin_context* context,
    const taiyin_astro_model_config* config
);
TAIYIN_C_API taiyin_call_result TAIYIN_C_CALL taiyin_context_set_apparent_config(
    taiyin_context* context,
    const taiyin_apparent_config* config
);
TAIYIN_C_API taiyin_call_result TAIYIN_C_CALL
taiyin_context_set_celestial_pole_offset(
    taiyin_context* context,
    double dx_rad,
    double dy_rad,
    double dx_rate_rad_per_day,
    double dy_rate_rad_per_day
);
TAIYIN_C_API taiyin_call_result TAIYIN_C_CALL taiyin_context_set_refraction_model(
    taiyin_context* context,
    int32_t refraction_model_id
);
TAIYIN_C_API taiyin_call_result TAIYIN_C_CALL taiyin_context_set_heliacal_visibility_model(
    taiyin_context* context,
    int32_t model_id
);
TAIYIN_C_API taiyin_call_result TAIYIN_C_CALL taiyin_context_use_solar_deflector(
    taiyin_context* context
);
TAIYIN_C_API taiyin_call_result TAIYIN_C_CALL taiyin_context_clear_deflectors(
    taiyin_context* context
);
TAIYIN_C_API taiyin_call_result TAIYIN_C_CALL taiyin_context_set_deflectors(
    taiyin_context* context,
    const taiyin_apparent_deflector* deflectors,
    size_t deflector_count,
    int32_t solar_deflector_index
);
TAIYIN_C_API taiyin_call_result TAIYIN_C_CALL
taiyin_context_set_light_time_iteration(
    taiyin_context* context,
    int32_t max_iterations,
    double tolerance_days
);
TAIYIN_C_API taiyin_call_result TAIYIN_C_CALL taiyin_context_enable_shapiro_delay(
    taiyin_context* context,
    int32_t shapiro_delay_model_id
);
TAIYIN_C_API taiyin_call_result TAIYIN_C_CALL taiyin_context_disable_shapiro_delay(
    taiyin_context* context
);
TAIYIN_C_API taiyin_call_result TAIYIN_C_CALL taiyin_context_set_eclipse_models(
    taiyin_context* context,
    int32_t shadow_model_id,
    int32_t moon_radius_model_id
);

#ifdef __cplusplus
}
#endif

#endif
