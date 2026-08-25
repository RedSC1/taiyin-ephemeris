#ifndef TAIYIN_C_BASE_H
#define TAIYIN_C_BASE_H

#include <stddef.h>
#include <stdint.h>

#if defined(_WIN32)
#if defined(TAIYIN_C_STATIC)
#define TAIYIN_C_API
#define TAIYIN_C_CORE_API
#define TAIYIN_C_ASTROLOGY_API
#define TAIYIN_C_CHINESE_CALENDAR_API
#define TAIYIN_C_GANZHI_API
#define TAIYIN_C_BAZI_API
#define TAIYIN_C_ZIWEI_API
#elif defined(TAIYIN_C_CORE_BUILD)
#define TAIYIN_C_CORE_API __declspec(dllexport)
#define TAIYIN_C_API TAIYIN_C_CORE_API
#define TAIYIN_C_ASTROLOGY_API __declspec(dllimport)
#define TAIYIN_C_CHINESE_CALENDAR_API __declspec(dllimport)
#define TAIYIN_C_GANZHI_API __declspec(dllimport)
#define TAIYIN_C_BAZI_API __declspec(dllimport)
#define TAIYIN_C_ZIWEI_API __declspec(dllimport)
#elif defined(TAIYIN_C_ASTROLOGY_BUILD)
#define TAIYIN_C_CORE_API __declspec(dllimport)
#define TAIYIN_C_ASTROLOGY_API __declspec(dllexport)
#define TAIYIN_C_API TAIYIN_C_ASTROLOGY_API
#define TAIYIN_C_CHINESE_CALENDAR_API __declspec(dllimport)
#define TAIYIN_C_GANZHI_API __declspec(dllimport)
#define TAIYIN_C_BAZI_API __declspec(dllimport)
#define TAIYIN_C_ZIWEI_API __declspec(dllimport)
#elif defined(TAIYIN_C_CHINESE_CALENDAR_BUILD)
#define TAIYIN_C_CORE_API __declspec(dllimport)
#define TAIYIN_C_ASTROLOGY_API __declspec(dllimport)
#define TAIYIN_C_CHINESE_CALENDAR_API __declspec(dllexport)
#define TAIYIN_C_API TAIYIN_C_CHINESE_CALENDAR_API
#define TAIYIN_C_GANZHI_API __declspec(dllimport)
#define TAIYIN_C_BAZI_API __declspec(dllimport)
#define TAIYIN_C_ZIWEI_API __declspec(dllimport)
#elif defined(TAIYIN_C_GANZHI_BUILD)
#define TAIYIN_C_CORE_API __declspec(dllimport)
#define TAIYIN_C_ASTROLOGY_API __declspec(dllimport)
#define TAIYIN_C_CHINESE_CALENDAR_API __declspec(dllimport)
#define TAIYIN_C_GANZHI_API __declspec(dllexport)
#define TAIYIN_C_API TAIYIN_C_GANZHI_API
#define TAIYIN_C_BAZI_API __declspec(dllimport)
#define TAIYIN_C_ZIWEI_API __declspec(dllimport)
#elif defined(TAIYIN_C_BAZI_BUILD)
#define TAIYIN_C_CORE_API __declspec(dllimport)
#define TAIYIN_C_ASTROLOGY_API __declspec(dllimport)
#define TAIYIN_C_CHINESE_CALENDAR_API __declspec(dllimport)
#define TAIYIN_C_GANZHI_API __declspec(dllimport)
#define TAIYIN_C_BAZI_API __declspec(dllexport)
#define TAIYIN_C_API TAIYIN_C_BAZI_API
#define TAIYIN_C_ZIWEI_API __declspec(dllimport)
#elif defined(TAIYIN_C_ZIWEI_BUILD)
#define TAIYIN_C_CORE_API __declspec(dllimport)
#define TAIYIN_C_ASTROLOGY_API __declspec(dllimport)
#define TAIYIN_C_CHINESE_CALENDAR_API __declspec(dllimport)
#define TAIYIN_C_GANZHI_API __declspec(dllimport)
#define TAIYIN_C_BAZI_API __declspec(dllimport)
#define TAIYIN_C_ZIWEI_API __declspec(dllexport)
#define TAIYIN_C_API TAIYIN_C_ZIWEI_API
#elif defined(TAIYIN_C_BUILD)
#define TAIYIN_C_API __declspec(dllexport)
#define TAIYIN_C_CORE_API __declspec(dllexport)
#define TAIYIN_C_ASTROLOGY_API __declspec(dllexport)
#define TAIYIN_C_CHINESE_CALENDAR_API __declspec(dllexport)
#define TAIYIN_C_GANZHI_API __declspec(dllexport)
#define TAIYIN_C_BAZI_API __declspec(dllexport)
#define TAIYIN_C_ZIWEI_API __declspec(dllexport)
#else
#define TAIYIN_C_API __declspec(dllimport)
#define TAIYIN_C_CORE_API __declspec(dllimport)
#define TAIYIN_C_ASTROLOGY_API __declspec(dllimport)
#define TAIYIN_C_CHINESE_CALENDAR_API __declspec(dllimport)
#define TAIYIN_C_GANZHI_API __declspec(dllimport)
#define TAIYIN_C_BAZI_API __declspec(dllimport)
#define TAIYIN_C_ZIWEI_API __declspec(dllimport)
#endif
#define TAIYIN_C_CALL __cdecl
#else
#define TAIYIN_C_API __attribute__((visibility("default")))
#define TAIYIN_C_CORE_API TAIYIN_C_API
#define TAIYIN_C_ASTROLOGY_API TAIYIN_C_API
#define TAIYIN_C_CHINESE_CALENDAR_API TAIYIN_C_API
#define TAIYIN_C_GANZHI_API TAIYIN_C_API
#define TAIYIN_C_BAZI_API TAIYIN_C_API
#define TAIYIN_C_ZIWEI_API TAIYIN_C_API
#define TAIYIN_C_CALL
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define TAIYIN_C_ABI_VERSION 10u
#define TAIYIN_LIBRARY_VERSION_MAJOR 1u
#define TAIYIN_LIBRARY_VERSION_MINOR 0u
#define TAIYIN_LIBRARY_VERSION_PATCH 0u
#define TAIYIN_LIBRARY_VERSION_PRERELEASE "beta.4"
#define TAIYIN_LIBRARY_VERSION_STRING "1.0.0-beta.4"
#define TAIYIN_LIBRARY_CODENAME "Singularity"

enum taiyin_capability {
    TAIYIN_CAPABILITY_RUNTIME = 1ull << 0,
    TAIYIN_CAPABILITY_TIME = 1ull << 1,
    TAIYIN_CAPABILITY_POSITION = 1ull << 2,
    TAIYIN_CAPABILITY_STAR = 1ull << 3,
    TAIYIN_CAPABILITY_VISIBILITY = 1ull << 4,
    TAIYIN_CAPABILITY_PHENOMENA = 1ull << 5,
    TAIYIN_CAPABILITY_EVENTS = 1ull << 6,
    TAIYIN_CAPABILITY_ECLIPSE = 1ull << 7,
    TAIYIN_CAPABILITY_OCCULTATION = 1ull << 8,
    TAIYIN_CAPABILITY_HELIACAL = 1ull << 9,
    TAIYIN_CAPABILITY_ASTROLOGY = 1ull << 10,
    TAIYIN_CAPABILITY_CUSTOM_TARGETS = 1ull << 11,
    TAIYIN_CAPABILITY_CUSTOM_AYANAMSHA = 1ull << 12,
    TAIYIN_CAPABILITY_CUSTOM_HOUSES = 1ull << 13,
    TAIYIN_CAPABILITY_SPLIT_TIME = 1ull << 14,
    TAIYIN_CAPABILITY_CHINESE_CALENDAR = 1ull << 15,
    TAIYIN_CAPABILITY_BAZI = 1ull << 16,
    TAIYIN_CAPABILITY_GANZHI_CALENDAR = 1ull << 17,
    TAIYIN_CAPABILITY_ZIWEI = 1ull << 18
};

typedef int32_t taiyin_status;
typedef uint8_t taiyin_bool;

/*
 * Packed call result for operations that report execution facts alongside the
 * final status.  The high 32 bits carry the int32 taiyin_status; the low 32
 * bits carry taiyin_result_flag execution facts.  Because a status of OK is 0
 * and every error is negative, a non-negative taiyin_call_result means the
 * call succeeded regardless of its result flags.  Result flags are
 * independent of the rich taiyin_ephemeris_diagnostic: a flag is a stable
 * summary fact about how the call executed, not route/candidate history.
 */
typedef int64_t taiyin_call_result;

/*
 * Execution facts for the low 32 bits of taiyin_call_result.  These are
 * warning lights for abnormal-but-successful execution: each bit answers
 * "did something happen that the caller cannot see from the input flags but
 * may change how they judge this result?"  The rich diagnostic explains the
 * details.  Only add flags with a clear, stable meaning; route names,
 * source IDs, and model names belong in the diagnostic, not here.
 */
enum taiyin_result_flag {
    /* Within one operation, two evaluations of the same (target, center)
     * were served by different ephemeris source ids.  A continuity hazard
     * for iterative searches (eclipse/solar-term/new-moon solving),
     * regardless of which source is more precise. */
    TAIYIN_RESULT_FLAG_FALLBACK_OCCURRED = 1u << 0,
    /* The requested state's velocity/acceleration came from finite
     * differences of a position-only evaluator instead of an exact state
     * evaluation. */
    TAIYIN_RESULT_FLAG_NUMERICAL_DERIVATIVE = 1u << 1,
    /* A requested physical body had no COB/center-of-body correction in
     * the selected route, so its system barycenter stood in for the body.
     * Routine for ancient dates; reported separately so it does not
     * drown out FALLBACK_OCCURRED. */
    TAIYIN_RESULT_FLAG_BARYCENTER_APPROX = 1u << 2,
    /* The preferred precise time-scale route (UTC/EOP) was unavailable and
     * an estimated delta-T model produced the conversion. */
    TAIYIN_RESULT_FLAG_TIME_SCALE_FALLBACK = 1u << 3,
    /* Historical civil-day assignment for new moons/solar terms (the
     * historical profile's day table, not an ancient algorithm's instant)
     * participated in constructing a Chinese lunar calendar result.  Only
     * possible when the calendar context's historical mode is enabled. */
    TAIYIN_RESULT_FLAG_HISTORICAL_EVENT_ASSIGNMENT_APPLIED = 1u << 4,
    /* Era-specific historical calendar rules (year-start shifts, special
     * month names, early-historical arrangements) actually modified a
     * Chinese lunar calendar result.  Only possible in historical mode. */
    TAIYIN_RESULT_FLAG_HISTORICAL_CALENDAR_RULES_APPLIED = 1u << 5,
    /* A four-pillars (ganzhi) calculation assigned its solar-term boundary
     * days using the historical profile.  Independent of lunar-calendar
     * arrangement and gated by the ganzhi module's own historical switch. */
    TAIYIN_RESULT_FLAG_HISTORICAL_PILLAR_TERMS_APPLIED = 1u << 6
};

typedef struct taiyin_split_julian_date {
    int64_t day_number;
    double day_fraction;
} taiyin_split_julian_date;

enum taiyin_status_code {
    TAIYIN_STATUS_OK = 0,

    TAIYIN_ERROR_INVALID_ARGUMENT = -1,
    TAIYIN_ERROR_OUT_OF_MEMORY = -2,
    TAIYIN_ERROR_INTERNAL = -3,
    TAIYIN_ERROR_UNSUPPORTED = -4,

    TAIYIN_EPHEMERIS_ERROR_NO_ROUTE = -1001,
    TAIYIN_EPHEMERIS_ERROR_COVERAGE_GAP = -1002,
    TAIYIN_EPHEMERIS_ERROR_LOAD_FAILED = -1003,
    TAIYIN_EPHEMERIS_ERROR_EVAL_FAILED = -1004,
    TAIYIN_EPHEMERIS_ERROR_COMPOSITE_MISSING_COMPONENT = -1010,
    TAIYIN_EPHEMERIS_ERROR_COMPOSITE_COVERAGE_GAP = -1011,
    TAIYIN_EPHEMERIS_ERROR_COMPOSITE_METHOD_MISMATCH = -1012,

    TAIYIN_FILE_ERROR_NOT_FOUND = -2001,
    TAIYIN_FILE_ERROR_BAD_FORMAT = -2002,
    TAIYIN_FILE_ERROR_UNSUPPORTED_FORMAT = -2003,
    TAIYIN_FILE_ERROR_DISCOVERY_FAILED = -2004,

    TAIYIN_TIME_ERROR_EOP_OUT_OF_RANGE = -3001,
    TAIYIN_TIME_ERROR_LEAP_SECOND_UNAVAILABLE = -3002,
    TAIYIN_EVENT_ERROR_NOT_FOUND = -5001,
    TAIYIN_RUNTIME_ERROR_NOT_INITIALIZED = -6001,
    TAIYIN_RUNTIME_ERROR_CACHE_INSERT_FAILED = -6002,
    TAIYIN_RUNTIME_ERROR_REGISTRY_FAILED = -6003
};

enum taiyin_status_category {
    TAIYIN_STATUS_CATEGORY_OK = 0,
    TAIYIN_STATUS_CATEGORY_GENERIC = 1,
    TAIYIN_STATUS_CATEGORY_EPHEMERIS = 10,
    TAIYIN_STATUS_CATEGORY_FILE = 20,
    TAIYIN_STATUS_CATEGORY_TIME = 30,
    TAIYIN_STATUS_CATEGORY_OBSERVER = 40,
    TAIYIN_STATUS_CATEGORY_EVENT = 50,
    TAIYIN_STATUS_CATEGORY_RUNTIME = 60,
    TAIYIN_STATUS_CATEGORY_UNKNOWN = 999
};

typedef struct taiyin_vector3 {
    double x;
    double y;
    double z;
} taiyin_vector3;

typedef struct taiyin_cartesian_state {
    uint32_t struct_size;
    taiyin_vector3 position_au;
    taiyin_vector3 velocity_au_per_day;
    taiyin_vector3 acceleration_au_per_day2;
} taiyin_cartesian_state;

typedef struct taiyin_calendar_datetime {
    uint32_t struct_size;
    int32_t year;
    int32_t month;
    int32_t day;
    int32_t hour;
    int32_t minute;
    double second;
} taiyin_calendar_datetime;

typedef struct taiyin_ephemeris_diagnostic {
    uint32_t struct_size;
    taiyin_status status;
    int32_t target_id;
    int32_t center_id;
    int32_t frame;
    taiyin_split_julian_date jd_tdb;
    int32_t candidate_count;
    int32_t attempted_method_id;
    double nearest_coverage_start;
    double nearest_coverage_end;
    int32_t component_target_id;
    int32_t component_center_id;
    int32_t component_method_id;
    uint8_t time_scale_route;
    uint8_t time_scale_fallback_reason;
    uint8_t time_scale_flags;
    uint8_t reserved0;
    double tai_minus_utc_seconds;
    double dut1_seconds;
    double delta_t_seconds;
} taiyin_ephemeris_diagnostic;

TAIYIN_C_API void TAIYIN_C_CALL taiyin_cartesian_state_init(
    taiyin_cartesian_state* value
);
TAIYIN_C_API void TAIYIN_C_CALL taiyin_calendar_datetime_init(
    taiyin_calendar_datetime* value
);
TAIYIN_C_API void TAIYIN_C_CALL taiyin_ephemeris_diagnostic_init(
    taiyin_ephemeris_diagnostic* value
);
TAIYIN_C_API uint32_t TAIYIN_C_CALL taiyin_get_c_abi_version(void);
TAIYIN_C_API const char* TAIYIN_C_CALL taiyin_get_library_version(void);
TAIYIN_C_API const char* TAIYIN_C_CALL taiyin_get_library_codename(void);
TAIYIN_C_API uint64_t TAIYIN_C_CALL taiyin_get_capabilities(void);
TAIYIN_C_API taiyin_status TAIYIN_C_CALL taiyin_format_ephemeris_diagnostic(
    const taiyin_ephemeris_diagnostic* diagnostic,
    char* buffer,
    size_t capacity,
    size_t* out_required_size
);
TAIYIN_C_API taiyin_call_result TAIYIN_C_CALL taiyin_make_call_result(
    taiyin_status status,
    uint32_t result_flags
);
TAIYIN_C_API taiyin_status TAIYIN_C_CALL taiyin_call_result_status(
    taiyin_call_result result
);
TAIYIN_C_API uint32_t TAIYIN_C_CALL taiyin_call_result_flags(
    taiyin_call_result result
);
TAIYIN_C_API taiyin_bool TAIYIN_C_CALL taiyin_call_result_ok(
    taiyin_call_result result
);
TAIYIN_C_API const char* TAIYIN_C_CALL taiyin_status_name(taiyin_status status);
TAIYIN_C_API const char* TAIYIN_C_CALL taiyin_status_message(taiyin_status status);
TAIYIN_C_API int32_t TAIYIN_C_CALL taiyin_status_category(taiyin_status status);

#ifdef __cplusplus
}
#endif

#endif
