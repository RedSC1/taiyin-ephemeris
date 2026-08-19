#ifndef TAIYIN_C_ASTROLOGY_H
#define TAIYIN_C_ASTROLOGY_H

#include "taiyin/c/context.h"

#ifdef __cplusplus
extern "C" {
#endif

enum taiyin_ayanamsha_id {
    TAIYIN_C_AYANAMSHA_FAGAN_BRADLEY = 0,
    TAIYIN_C_AYANAMSHA_LAHIRI = 1,
    TAIYIN_C_AYANAMSHA_RAMAN = 3,
    TAIYIN_C_AYANAMSHA_KRISHNAMURTI = 5,
    TAIYIN_C_AYANAMSHA_GALACTIC_CENTER_0_SAGITTARIUS = 17,
    TAIYIN_C_AYANAMSHA_TRUE_CHITRA = 27
};

#define TAIYIN_C_SIDEREAL_REFERENCE_ECL_T0 (UINT64_C(1) << 32)
#define TAIYIN_C_SIDEREAL_REFERENCE_SSY_PLANE (UINT64_C(1) << 33)
#define TAIYIN_C_SIDEREAL_REFERENCE_J2000_ECLIPTIC (UINT64_C(1) << 34)
#define TAIYIN_C_SIDEREAL_REFERENCE_EPOCH_UT1 (UINT64_C(1) << 35)
#define TAIYIN_C_SIDEREAL_RAW_REFERENCE_OFFSET (UINT64_C(1) << 36)
#define TAIYIN_C_SIDEREAL_USE_REFERENCE_PRECESSION (UINT64_C(1) << 37)

enum taiyin_sidereal_coordinate_frame {
    TAIYIN_C_SIDEREAL_FRAME_MEAN_ECLIPTIC_OF_DATE = 0,
    TAIYIN_C_SIDEREAL_FRAME_MEAN_EQUATOR_OF_DATE = 1,
    TAIYIN_C_SIDEREAL_FRAME_TRUE_EQUATOR_OF_DATE = 2,
    TAIYIN_C_SIDEREAL_FRAME_FIXED_MEAN_ECLIPTIC_AT_EPOCH = 3,
    TAIYIN_C_SIDEREAL_FRAME_SOLAR_SYSTEM_INVARIABLE = 4,
    TAIYIN_C_SIDEREAL_FRAME_J2000_ECLIPTIC = 5
};

enum taiyin_house_system_id {
    TAIYIN_C_HOUSE_SYSTEM_WHOLE_SIGN = 0,
    TAIYIN_C_HOUSE_SYSTEM_EQUAL = 1,
    TAIYIN_C_HOUSE_SYSTEM_PORPHYRY = 2,
    TAIYIN_C_HOUSE_SYSTEM_PLACIDUS = 3,
    TAIYIN_C_HOUSE_SYSTEM_KOCH = 4,
    TAIYIN_C_HOUSE_SYSTEM_REGIOMONTANUS = 5,
    TAIYIN_C_HOUSE_SYSTEM_CAMPANUS = 6,
    TAIYIN_C_HOUSE_SYSTEM_ALCABITIUS = 7,
    TAIYIN_C_HOUSE_SYSTEM_POLICH_PAGE = 8,
    TAIYIN_C_HOUSE_SYSTEM_MORINUS = 9
};

enum taiyin_house_result_flags {
    TAIYIN_C_HOUSE_RESULT_USED_FALLBACK = 1u << 0,
    TAIYIN_C_HOUSE_RESULT_FALLBACK_PORPHYRY = 1u << 1,
    TAIYIN_C_HOUSE_RESULT_SPEED_UNAVAILABLE = 1u << 2
};

enum taiyin_lunar_node_kind {
    TAIYIN_C_LUNAR_NODE_ASCENDING = 0,
    TAIYIN_C_LUNAR_NODE_DESCENDING = 1
};

enum taiyin_lunar_apsis_definition {
    TAIYIN_C_LUNAR_APSIS_DELAUNAY_MEAN = 0,
    TAIYIN_C_LUNAR_APSIS_OSCULATING_TWO_BODY = 1,
    TAIYIN_C_LUNAR_APSIS_DE441_FITTED_NATURAL = 2
};

enum taiyin_astrology_target_id {
    TAIYIN_C_ASTROLOGY_TARGET_TRUE_NODE = -100001,
    TAIYIN_C_ASTROLOGY_TARGET_TRUE_DESCENDING_NODE = -100002,
    TAIYIN_C_ASTROLOGY_TARGET_MEAN_NODE = -100003,
    TAIYIN_C_ASTROLOGY_TARGET_MEAN_DESCENDING_NODE = -100004,
    TAIYIN_C_ASTROLOGY_TARGET_MEAN_LILITH = -100005,
    TAIYIN_C_ASTROLOGY_TARGET_OSCULATING_LILITH = -100006,
    TAIYIN_C_ASTROLOGY_TARGET_FITTED_LILITH = -100007
};

typedef struct taiyin_sidereal_position {
    uint32_t struct_size;
    int32_t coordinate_frame_id;
    /* For fixed/invariable planes, this is unshifted longitude on that plane. */
    double tropical_longitude_rad;
    double sidereal_longitude_rad;
    double latitude_rad;
    double distance_au;
    double tropical_longitude_rate_rad_per_day;
    double sidereal_longitude_rate_rad_per_day;
} taiyin_sidereal_position;

/*
 * Generic sidereal position output. `values` follow taiyin_position_flags:
 * longitude/latitude/distance (and rates), or x/y/z (and velocity) with XYZ.
 * Without EQUATORIAL, values use the selected sidereal reference plane. Every
 * ecliptic reference plane is mean, so NONUT does not change the result. With
 * EQUATORIAL, values follow the conventional Swiss Ephemeris-compatible
 * behavior: they are tropical mean equator of date with NONUT, or tropical
 * true equator of date without it, and are independent of the selected
 * ayanamsha, precession policy, and sidereal reference plane.
 * Without SPEED, values[3..5] are zero. position_flags echoes the caller's
 * requested flags rather than internal normalization used by the sidereal
 * ecliptic calculation.
 */
typedef struct taiyin_sidereal_coordinates {
    uint32_t struct_size;
    int32_t coordinate_frame_id;
    uint32_t position_flags;
    double values[6];
} taiyin_sidereal_coordinates;

typedef struct taiyin_house_result {
    uint32_t struct_size;
    int32_t requested_system_id;
    int32_t resolved_system_id;
    uint32_t flags;
    double armc_rad;
    double ascendant_rad;
    double midheaven_rad;
    double vertex_rad;
    double east_point_rad;
    double armc_rate_rad_per_day;
    double ascendant_rate_rad_per_day;
    double midheaven_rate_rad_per_day;
    double vertex_rate_rad_per_day;
    double east_point_rate_rad_per_day;
    double cusp_longitude_rad[12];
    double cusp_longitude_rate_rad_per_day[12];
} taiyin_house_result;

typedef struct taiyin_house_position_result {
    uint32_t struct_size;
    int32_t house_number;
    double fraction;
    double continuous_house_position;
} taiyin_house_position_result;

typedef struct taiyin_lunar_node_position {
    uint32_t struct_size;
    int32_t reference_frame_id;
    double longitude_rad;
    double longitude_rate_rad_per_day;
} taiyin_lunar_node_position;

typedef struct taiyin_lunar_apsis_position {
    uint32_t struct_size;
    int32_t reference_frame_id;
    int32_t definition;
    double longitude_rad;
    double latitude_rad;
    double longitude_rate_rad_per_day;
    double latitude_rate_rad_per_day;
    double distance_au;
    double distance_rate_au_per_day;
    uint8_t extrapolated;
    uint8_t reserved[7];
} taiyin_lunar_apsis_position;

typedef taiyin_status (TAIYIN_C_CALL *taiyin_ayanamsha_evaluator_fn)(
    const taiyin_context* context,
    const taiyin_split_julian_date* jd_tt,
    uint64_t native_position_flags,
    double* out_ayanamsha_rad,
    void* user_data
);

typedef struct taiyin_house_system_dispatch_data {
    uint32_t struct_size;
    double armc_rad;
    double observer_latitude_rad;
    double true_obliquity_rad;
    double ascendant_rad;
    double midheaven_rad;
} taiyin_house_system_dispatch_data;

typedef taiyin_bool (TAIYIN_C_CALL *taiyin_house_system_evaluator_fn)(
    const taiyin_house_system_dispatch_data* data,
    double out_cusp_longitude_rad[12],
    void* user_data
);

TAIYIN_C_ASTROLOGY_API void TAIYIN_C_CALL taiyin_sidereal_position_init(taiyin_sidereal_position*);
TAIYIN_C_ASTROLOGY_API void TAIYIN_C_CALL taiyin_sidereal_coordinates_init(taiyin_sidereal_coordinates*);
TAIYIN_C_ASTROLOGY_API void TAIYIN_C_CALL taiyin_house_result_init(taiyin_house_result*);
TAIYIN_C_ASTROLOGY_API void TAIYIN_C_CALL taiyin_house_position_result_init(taiyin_house_position_result*);
TAIYIN_C_ASTROLOGY_API void TAIYIN_C_CALL taiyin_lunar_node_position_init(taiyin_lunar_node_position*);
TAIYIN_C_ASTROLOGY_API void TAIYIN_C_CALL taiyin_lunar_apsis_position_init(taiyin_lunar_apsis_position*);

TAIYIN_C_ASTROLOGY_API taiyin_call_result TAIYIN_C_CALL taiyin_calc_ayanamsha_tt(
    const taiyin_context* context,
    int32_t ayanamsha_id,
    const taiyin_split_julian_date* jd_tt,
    uint64_t flags,
    double* out_ayanamsha_rad
);
TAIYIN_C_ASTROLOGY_API taiyin_call_result TAIYIN_C_CALL taiyin_calc_sidereal_position_tt(
    const taiyin_context* context,
    int32_t ayanamsha_id,
    int32_t body_id,
    const taiyin_split_julian_date* jd_tt,
    uint64_t flags,
    const taiyin_split_julian_date* reference_epoch_jd,
    taiyin_sidereal_position* out,
    taiyin_ephemeris_diagnostic* diagnostic
);
TAIYIN_C_ASTROLOGY_API taiyin_call_result TAIYIN_C_CALL taiyin_calc_sidereal_position_ut(
    const taiyin_context* context,
    int32_t ayanamsha_id,
    int32_t body_id,
    const taiyin_split_julian_date* jd_ut,
    uint64_t flags,
    const taiyin_split_julian_date* reference_epoch_jd,
    taiyin_sidereal_position* out,
    taiyin_ephemeris_diagnostic* diagnostic
);
TAIYIN_C_ASTROLOGY_API taiyin_call_result TAIYIN_C_CALL taiyin_calc_sidereal_coordinates_tt(
    const taiyin_context* context,
    int32_t ayanamsha_id,
    int32_t body_id,
    const taiyin_split_julian_date* jd_tt,
    uint64_t flags,
    const taiyin_split_julian_date* reference_epoch_jd,
    taiyin_sidereal_coordinates* out,
    taiyin_ephemeris_diagnostic* diagnostic
);
TAIYIN_C_ASTROLOGY_API taiyin_call_result TAIYIN_C_CALL taiyin_calc_sidereal_coordinates_ut(
    const taiyin_context* context,
    int32_t ayanamsha_id,
    int32_t body_id,
    const taiyin_split_julian_date* jd_ut,
    uint64_t flags,
    const taiyin_split_julian_date* reference_epoch_jd,
    taiyin_sidereal_coordinates* out,
    taiyin_ephemeris_diagnostic* diagnostic
);

TAIYIN_C_ASTROLOGY_API taiyin_call_result TAIYIN_C_CALL taiyin_calc_houses_from_armc(
    double armc_rad,
    double observer_latitude_rad,
    double true_obliquity_rad,
    int32_t house_system_id,
    taiyin_house_result* out
);
TAIYIN_C_ASTROLOGY_API taiyin_call_result TAIYIN_C_CALL taiyin_calc_houses_ut(
    const taiyin_context* context,
    const taiyin_split_julian_date* jd_ut,
    int32_t house_system_id,
    taiyin_house_result* out
);
TAIYIN_C_ASTROLOGY_API taiyin_call_result TAIYIN_C_CALL taiyin_calc_houses_tt(
    const taiyin_context* context,
    const taiyin_split_julian_date* jd_tt,
    int32_t house_system_id,
    taiyin_house_result* out
);
TAIYIN_C_ASTROLOGY_API taiyin_call_result TAIYIN_C_CALL
taiyin_calc_house_position_from_longitude(
    const taiyin_house_result* houses,
    double ecliptic_longitude_rad,
    taiyin_house_position_result* out
);
TAIYIN_C_ASTROLOGY_API taiyin_bool TAIYIN_C_CALL taiyin_has_house_system_model(
    int32_t model_id
);
TAIYIN_C_ASTROLOGY_API taiyin_bool TAIYIN_C_CALL taiyin_has_ayanamsha_model(
    int32_t model_id
);

/*
 * Astrology is part of the base taiyin library in both aggregate and modular
 * layouts, so it is not independently unloadable and this returns
 * TAIYIN_ERROR_UNSUPPORTED.
 */
TAIYIN_C_ASTROLOGY_API taiyin_call_result TAIYIN_C_CALL
    taiyin_astrology_module_shutdown(void);

/*
 * Custom callbacks may be invoked concurrently. model_id must be >= 10000.
 * The callback and user_data must remain valid until the model is unregistered
 * or the corresponding C-API model set is cleared, including when
 * taiyin_runtime_initialize() resets an already initialized runtime.
 * Registration changes and runtime initialization must be serialized and must
 * not overlap evaluation.
 */
TAIYIN_C_ASTROLOGY_API taiyin_call_result TAIYIN_C_CALL taiyin_register_ayanamsha_model(
    int32_t model_id,
    taiyin_ayanamsha_evaluator_fn evaluator,
    int32_t reference_precession_model_id,
    void* user_data
);
TAIYIN_C_ASTROLOGY_API taiyin_call_result TAIYIN_C_CALL taiyin_register_house_system_model(
    int32_t model_id,
    taiyin_house_system_evaluator_fn evaluator,
    int32_t fallback_model_id,
    void* user_data
);

/*
 * Token-returning variants for language bindings and other clients that keep
 * registration handles. On success, out_registration_token receives an opaque
 * non-zero token identifying this exact registration. Pass it to the matching
 * unregister_with_token function so an old handle cannot remove a later
 * same-ID registration. out_registration_token must not be NULL.
 */
TAIYIN_C_ASTROLOGY_API taiyin_call_result TAIYIN_C_CALL
taiyin_register_ayanamsha_model_with_token(
    int32_t model_id,
    taiyin_ayanamsha_evaluator_fn evaluator,
    int32_t reference_precession_model_id,
    void* user_data,
    uint64_t* out_registration_token
);
TAIYIN_C_ASTROLOGY_API taiyin_call_result TAIYIN_C_CALL
taiyin_register_house_system_model_with_token(
    int32_t model_id,
    taiyin_house_system_evaluator_fn evaluator,
    int32_t fallback_model_id,
    void* user_data,
    uint64_t* out_registration_token
);

/*
 * Unregister a C callback registered for model_id. Returns invalid argument
 * when model_id is not currently registered through this C API. Built-in C++
 * models and custom C++ registrations are not affected. Registration changes
 * and runtime initialization must be serialized and must not overlap
 * evaluation. A house-system unregister also returns TAIYIN_ERROR_UNSUPPORTED
 * while another registered house system selects it as a fallback.
 */
TAIYIN_C_ASTROLOGY_API taiyin_call_result TAIYIN_C_CALL
taiyin_unregister_ayanamsha_model(int32_t model_id);
TAIYIN_C_ASTROLOGY_API taiyin_call_result TAIYIN_C_CALL
taiyin_unregister_house_system_model(int32_t model_id);

/*
 * Ownership-aware unregister variants. A mismatched or stale token returns
 * TAIYIN_ERROR_INVALID_ARGUMENT without removing a current same-ID model.
 * A house-system token remains valid while its model is used as a fallback,
 * but unregister returns TAIYIN_ERROR_UNSUPPORTED until dependents are
 * removed first.
 */
TAIYIN_C_ASTROLOGY_API taiyin_call_result TAIYIN_C_CALL
taiyin_unregister_ayanamsha_model_with_token(
    int32_t model_id,
    uint64_t registration_token
);
TAIYIN_C_ASTROLOGY_API taiyin_call_result TAIYIN_C_CALL
taiyin_unregister_house_system_model_with_token(
    int32_t model_id,
    uint64_t registration_token
);

/*
 * Unregister every callback registered through the matching C API. Built-in
 * C++ models and custom C++ registrations are not affected. Registration
 * changes and runtime initialization must be serialized and must not overlap
 * evaluation. A model that is still the fallback of a non-C-API house model
 * is retained until its dependent is removed; this avoids leaving that C++
 * model with a dangling fallback target.
 */
TAIYIN_C_ASTROLOGY_API void TAIYIN_C_CALL taiyin_clear_ayanamsha_models(void);
TAIYIN_C_ASTROLOGY_API void TAIYIN_C_CALL taiyin_clear_house_system_models(void);

TAIYIN_C_ASTROLOGY_API taiyin_call_result TAIYIN_C_CALL taiyin_calc_lunar_true_node_tt(
    const taiyin_context*, const taiyin_split_julian_date*, int32_t, uint32_t,
    taiyin_lunar_node_position*, taiyin_ephemeris_diagnostic*
);
TAIYIN_C_ASTROLOGY_API taiyin_call_result TAIYIN_C_CALL taiyin_calc_lunar_true_node_ut(
    const taiyin_context*, const taiyin_split_julian_date*, int32_t, uint32_t,
    taiyin_lunar_node_position*, taiyin_ephemeris_diagnostic*
);
TAIYIN_C_ASTROLOGY_API taiyin_call_result TAIYIN_C_CALL taiyin_calc_lunar_mean_node_tt(
    const taiyin_context*, const taiyin_split_julian_date*, int32_t, uint32_t,
    taiyin_lunar_node_position*, taiyin_ephemeris_diagnostic*
);
TAIYIN_C_ASTROLOGY_API taiyin_call_result TAIYIN_C_CALL taiyin_calc_lunar_mean_node_ut(
    const taiyin_context*, const taiyin_split_julian_date*, int32_t, uint32_t,
    taiyin_lunar_node_position*, taiyin_ephemeris_diagnostic*
);
TAIYIN_C_ASTROLOGY_API taiyin_call_result TAIYIN_C_CALL taiyin_calc_lunar_mean_apogee_tt(
    const taiyin_context*, const taiyin_split_julian_date*, uint32_t,
    taiyin_lunar_apsis_position*, taiyin_ephemeris_diagnostic*
);
TAIYIN_C_ASTROLOGY_API taiyin_call_result TAIYIN_C_CALL taiyin_calc_lunar_mean_apogee_ut(
    const taiyin_context*, const taiyin_split_julian_date*, uint32_t,
    taiyin_lunar_apsis_position*, taiyin_ephemeris_diagnostic*
);
TAIYIN_C_ASTROLOGY_API taiyin_call_result TAIYIN_C_CALL taiyin_calc_lunar_osculating_apogee_tt(
    const taiyin_context*, const taiyin_split_julian_date*, uint32_t,
    taiyin_lunar_apsis_position*, taiyin_ephemeris_diagnostic*
);
TAIYIN_C_ASTROLOGY_API taiyin_call_result TAIYIN_C_CALL taiyin_calc_lunar_osculating_apogee_ut(
    const taiyin_context*, const taiyin_split_julian_date*, uint32_t,
    taiyin_lunar_apsis_position*, taiyin_ephemeris_diagnostic*
);
TAIYIN_C_ASTROLOGY_API taiyin_call_result TAIYIN_C_CALL taiyin_calc_lunar_fitted_apogee_tt(
    const taiyin_context*, const taiyin_split_julian_date*, uint32_t,
    taiyin_lunar_apsis_position*, taiyin_ephemeris_diagnostic*
);
TAIYIN_C_ASTROLOGY_API taiyin_call_result TAIYIN_C_CALL taiyin_calc_lunar_fitted_apogee_ut(
    const taiyin_context*, const taiyin_split_julian_date*, uint32_t,
    taiyin_lunar_apsis_position*, taiyin_ephemeris_diagnostic*
);
TAIYIN_C_ASTROLOGY_API taiyin_call_result TAIYIN_C_CALL
taiyin_register_builtin_astrology_targets(void);

#ifdef __cplusplus
}
#endif

#endif
