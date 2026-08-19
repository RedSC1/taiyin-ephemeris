#ifndef TAIYIN_C_POSITION_H
#define TAIYIN_C_POSITION_H

#include "taiyin/c/base.h"
#include "taiyin/c/context.h"

#ifdef __cplusplus
extern "C" {
#endif

enum taiyin_body_id {
    TAIYIN_BODY_SSB = 0,
    TAIYIN_BODY_MERCURY_BARYCENTER = 1,
    TAIYIN_BODY_VENUS_BARYCENTER = 2,
    TAIYIN_BODY_EMB = 3,
    TAIYIN_BODY_MARS_BARYCENTER = 4,
    TAIYIN_BODY_JUPITER_BARYCENTER = 5,
    TAIYIN_BODY_SATURN_BARYCENTER = 6,
    TAIYIN_BODY_URANUS_BARYCENTER = 7,
    TAIYIN_BODY_NEPTUNE_BARYCENTER = 8,
    TAIYIN_BODY_PLUTO_BARYCENTER = 9,
    TAIYIN_BODY_SUN = 10,
    TAIYIN_BODY_MERCURY = 199,
    TAIYIN_BODY_VENUS = 299,
    TAIYIN_BODY_MOON = 301,
    TAIYIN_BODY_EARTH = 399,
    TAIYIN_BODY_MARS = 499,
    TAIYIN_BODY_JUPITER = 599,
    TAIYIN_BODY_SATURN = 699,
    TAIYIN_BODY_URANUS = 799,
    TAIYIN_BODY_NEPTUNE = 899,
    TAIYIN_BODY_PLUTO = 999,
    TAIYIN_BODY_PHOBOS = 401,
    TAIYIN_BODY_DEIMOS = 402,
    TAIYIN_BODY_IO = 501,
    TAIYIN_BODY_EUROPA = 502,
    TAIYIN_BODY_GANYMEDE = 503,
    TAIYIN_BODY_CALLISTO = 504,
    TAIYIN_BODY_MIMAS = 601,
    TAIYIN_BODY_ENCELADUS = 602,
    TAIYIN_BODY_TETHYS = 603,
    TAIYIN_BODY_DIONE = 604,
    TAIYIN_BODY_RHEA = 605,
    TAIYIN_BODY_TITAN = 606,
    TAIYIN_BODY_HYPERION = 607,
    TAIYIN_BODY_IAPETUS = 608,
    TAIYIN_BODY_ARIEL = 701,
    TAIYIN_BODY_UMBRIEL = 702,
    TAIYIN_BODY_TITANIA = 703,
    TAIYIN_BODY_OBERON = 704,
    TAIYIN_BODY_MIRANDA = 705,
    TAIYIN_BODY_TRITON = 801,
    TAIYIN_BODY_CHARON = 901,
    TAIYIN_BODY_NIX = 902,
    TAIYIN_BODY_HYDRA = 903,
    TAIYIN_BODY_KERBEROS = 904,
    TAIYIN_BODY_STYX = 905
};

enum taiyin_position_flags {
    TAIYIN_POSITION_SPEED = 1u << 0,
    TAIYIN_POSITION_XYZ = 1u << 1,
    TAIYIN_POSITION_EQUATORIAL = 1u << 2,
    TAIYIN_POSITION_RADIANS = 1u << 3,
    TAIYIN_POSITION_TRUEPOS = 1u << 4,
    TAIYIN_POSITION_NO_ABERR = 1u << 5,
    TAIYIN_POSITION_NO_GDEFL = 1u << 6,
    TAIYIN_POSITION_ASTROMETRIC = 1u << 7,
    TAIYIN_POSITION_NONUT = 1u << 8,
    TAIYIN_POSITION_TOPOCENTRIC = 1u << 9,
    TAIYIN_POSITION_ALLOW_BARYCENTER_APPROX = 1u << 10
};

enum taiyin_apparent_frame_id {
    TAIYIN_FRAME_ICRF = 0,
    TAIYIN_FRAME_TRUE_EQUATOR_OF_DATE = 1,
    TAIYIN_FRAME_TRUE_ECLIPTIC_OF_DATE = 2,
    TAIYIN_FRAME_J2000_MEAN_EQUATOR = 3,
    TAIYIN_FRAME_J2000_ECLIPTIC = 4,
    TAIYIN_FRAME_MEAN_EQUATOR_OF_DATE = 5,
    TAIYIN_FRAME_MEAN_ECLIPTIC_OF_DATE = 6,
    TAIYIN_FRAME_CIRS = 7
};

typedef taiyin_status (TAIYIN_C_CALL *taiyin_native_position_evaluator_fn)(
    const taiyin_context* context,
    int32_t target_id,
    const taiyin_split_julian_date* jd_tdb,
    const taiyin_split_julian_date* jd_tt,
    uint32_t flags,
    double out_position[6],
    taiyin_ephemeris_diagnostic* diagnostic,
    void* user_data
);

typedef taiyin_status (TAIYIN_C_CALL *taiyin_native_state_evaluator_fn)(
    const taiyin_context* context,
    int32_t target_id,
    const taiyin_split_julian_date* jd_tdb,
    const taiyin_split_julian_date* jd_tt,
    uint32_t flags,
    taiyin_cartesian_state* out_state,
    taiyin_ephemeris_diagnostic* diagnostic,
    void* user_data
);

/*
 * Register before concurrent calculations begin. target_id must be negative.
 * Registration changes and runtime initialization must be serialized.
 * Callbacks and user_data must remain valid until the evaluator is
 * unregistered or all native position evaluators are cleared.
 * A null state evaluator selects the runtime finite-difference fallback.
 */
TAIYIN_C_API taiyin_call_result TAIYIN_C_CALL
taiyin_register_native_position_evaluator(
    int32_t target_id,
    taiyin_native_position_evaluator_fn position_evaluator,
    taiyin_native_state_evaluator_fn state_evaluator,
    void* user_data
);

/*
 * Unregister a C callback registered for target_id. Returns invalid argument
 * when target_id is not currently registered through this C API.
 *
 * Registration changes and runtime initialization must be serialized and must
 * not overlap calculations.
 */
TAIYIN_C_API taiyin_call_result TAIYIN_C_CALL
taiyin_unregister_native_position_evaluator(int32_t target_id);

/*
 * Unregister every native position evaluator registered through this C API.
 * Built-in C++ evaluators are not affected.
 *
 * Registration changes and runtime initialization must be serialized and must
 * not overlap calculations.
 */
TAIYIN_C_API void TAIYIN_C_CALL
taiyin_clear_native_position_evaluators(void);

TAIYIN_C_API taiyin_call_result TAIYIN_C_CALL taiyin_calc_position_tdb(
    const taiyin_context* context,
    int32_t target_id,
    const taiyin_split_julian_date* jd_tdb,
    const taiyin_split_julian_date* jd_tt,
    uint32_t flags,
    double out_position[6],
    taiyin_ephemeris_diagnostic* diagnostic
);
TAIYIN_C_API taiyin_call_result TAIYIN_C_CALL taiyin_calc_position_tt(
    const taiyin_context* context,
    int32_t target_id,
    const taiyin_split_julian_date* jd_tt,
    uint32_t flags,
    double out_position[6],
    taiyin_ephemeris_diagnostic* diagnostic
);
TAIYIN_C_API taiyin_call_result TAIYIN_C_CALL taiyin_calc_position_ut(
    const taiyin_context* context,
    int32_t target_id,
    const taiyin_split_julian_date* jd_ut,
    uint32_t flags,
    double out_position[6],
    taiyin_ephemeris_diagnostic* diagnostic
);
TAIYIN_C_API taiyin_call_result TAIYIN_C_CALL taiyin_calc_position_ut_delta_t(
    const taiyin_context* context,
    int32_t target_id,
    const taiyin_split_julian_date* jd_ut1,
    double delta_t_seconds,
    uint32_t flags,
    double out_position[6],
    taiyin_ephemeris_diagnostic* diagnostic
);
TAIYIN_C_API taiyin_call_result TAIYIN_C_CALL taiyin_calc_position_utc(
    const taiyin_context* context,
    int32_t target_id,
    const taiyin_calendar_datetime* datetime_utc,
    uint32_t flags,
    double out_position[6],
    taiyin_ephemeris_diagnostic* diagnostic
);

TAIYIN_C_API taiyin_call_result TAIYIN_C_CALL taiyin_calc_positions_ut(
    const taiyin_context* context,
    const int32_t* target_ids,
    size_t target_count,
    const taiyin_split_julian_date* jd_ut,
    uint32_t flags,
    double* out_positions,
    taiyin_ephemeris_diagnostic* diagnostics
);
TAIYIN_C_API taiyin_call_result TAIYIN_C_CALL taiyin_calc_positions_tdb(
    const taiyin_context* context,
    const int32_t* target_ids,
    size_t target_count,
    const taiyin_split_julian_date* jd_tdb,
    const taiyin_split_julian_date* jd_tt,
    uint32_t flags,
    double* out_positions,
    taiyin_ephemeris_diagnostic* diagnostics
);
TAIYIN_C_API taiyin_call_result TAIYIN_C_CALL taiyin_calc_positions_tt(
    const taiyin_context* context,
    const int32_t* target_ids,
    size_t target_count,
    const taiyin_split_julian_date* jd_tt,
    uint32_t flags,
    double* out_positions,
    taiyin_ephemeris_diagnostic* diagnostics
);
TAIYIN_C_API taiyin_call_result TAIYIN_C_CALL taiyin_calc_positions_ut_delta_t(
    const taiyin_context* context,
    const int32_t* target_ids,
    size_t target_count,
    const taiyin_split_julian_date* jd_ut1,
    double delta_t_seconds,
    uint32_t flags,
    double* out_positions,
    taiyin_ephemeris_diagnostic* diagnostics
);
TAIYIN_C_API taiyin_call_result TAIYIN_C_CALL taiyin_calc_positions_utc(
    const taiyin_context* context,
    const int32_t* target_ids,
    size_t target_count,
    const taiyin_calendar_datetime* datetime_utc,
    uint32_t flags,
    double* out_positions,
    taiyin_ephemeris_diagnostic* diagnostics
);

TAIYIN_C_API taiyin_call_result TAIYIN_C_CALL taiyin_calc_state_tdb(
    const taiyin_context* context,
    int32_t target_id,
    const taiyin_split_julian_date* jd_tdb,
    const taiyin_split_julian_date* jd_tt,
    uint32_t flags,
    taiyin_cartesian_state* out_state,
    taiyin_ephemeris_diagnostic* diagnostic
);
TAIYIN_C_API taiyin_call_result TAIYIN_C_CALL taiyin_calc_state_tt(
    const taiyin_context* context,
    int32_t target_id,
    const taiyin_split_julian_date* jd_tt,
    uint32_t flags,
    taiyin_cartesian_state* out_state,
    taiyin_ephemeris_diagnostic* diagnostic
);
TAIYIN_C_API taiyin_call_result TAIYIN_C_CALL taiyin_calc_state_ut(
    const taiyin_context* context,
    int32_t target_id,
    const taiyin_split_julian_date* jd_ut,
    uint32_t flags,
    taiyin_cartesian_state* out_state,
    taiyin_ephemeris_diagnostic* diagnostic
);
TAIYIN_C_API taiyin_call_result TAIYIN_C_CALL taiyin_calc_state_ut_delta_t(
    const taiyin_context* context,
    int32_t target_id,
    const taiyin_split_julian_date* jd_ut1,
    double delta_t_seconds,
    uint32_t flags,
    taiyin_cartesian_state* out_state,
    taiyin_ephemeris_diagnostic* diagnostic
);
TAIYIN_C_API taiyin_call_result TAIYIN_C_CALL taiyin_calc_state_utc(
    const taiyin_context* context,
    int32_t target_id,
    const taiyin_calendar_datetime* datetime_utc,
    uint32_t flags,
    taiyin_cartesian_state* out_state,
    taiyin_ephemeris_diagnostic* diagnostic
);

#ifdef __cplusplus
}
#endif

#endif
