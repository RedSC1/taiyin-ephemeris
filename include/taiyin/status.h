#ifndef TAIYIN_STATUS_H
#define TAIYIN_STATUS_H

#include <stdint.h>

namespace taiyin {

typedef int32_t Status;

enum StatusCode {
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
    TAIYIN_RUNTIME_ERROR_REGISTRY_FAILED = -6003,
};

enum StatusCategory {
    TAIYIN_STATUS_CATEGORY_OK = 0,
    TAIYIN_STATUS_CATEGORY_GENERIC = 1,
    TAIYIN_STATUS_CATEGORY_EPHEMERIS = 10,
    TAIYIN_STATUS_CATEGORY_FILE = 20,
    TAIYIN_STATUS_CATEGORY_TIME = 30,
    TAIYIN_STATUS_CATEGORY_OBSERVER = 40,
    TAIYIN_STATUS_CATEGORY_EVENT = 50,
    TAIYIN_STATUS_CATEGORY_RUNTIME = 60,
    TAIYIN_STATUS_CATEGORY_UNKNOWN = 999,
};

inline bool status_ok(Status status) noexcept {
    return status == TAIYIN_STATUS_OK;
}

// Execution facts mirrored by the C ABI's taiyin_result_flag enum.  These
// are warning lights for abnormal-but-successful execution: they tell the
// caller something non-default happened that the input flags cannot reveal,
// and that the diagnostic record explains in detail.  They never alter the
// Status itself.
enum ResultFlag {
    // Within one operation, two evaluations of the same (target, center)
    // were served by different ephemeris source ids — a continuity hazard
    // for iterative searches regardless of the direction of the switch.
    kResultFlagFallbackOccurred = 1u << 0,
    // A requested state's velocity/acceleration came from finite
    // differences of a position-only evaluator.
    kResultFlagNumericalDerivative = 1u << 1,
    // A requested physical body had no COB/center-of-body correction in
    // the selected route, so its system barycenter stood in for the body.
    kResultFlagBarycenterApprox = 1u << 2,
    // The preferred precise time-scale route (UTC/EOP) was unavailable and
    // an estimated delta-T model produced the conversion.
    kResultFlagTimeScaleFallback = 1u << 3,
    // Historical civil-day assignment for new moons/solar terms (the
    // historical profile's day table, not an ancient algorithm's instant)
    // participated in constructing a Chinese lunar calendar result.  Only
    // possible when the calendar context's historical mode is enabled.
    kResultFlagHistoricalEventAssignmentApplied = 1u << 4,
    // Era-specific historical calendar rules (year-start shifts, special
    // month names, early-historical arrangements) actually modified a
    // Chinese lunar calendar result.  Only possible in historical mode.
    kResultFlagHistoricalCalendarRulesApplied = 1u << 5,
    // A four-pillars (ganzhi) calculation assigned its solar-term boundary
    // days using the historical profile.  Independent of lunar-calendar
    // arrangement and gated by the ganzhi module's own historical switch.
    kResultFlagHistoricalPillarTermsApplied = 1u << 6
};

inline void set_result_flag(uint32_t* result_flags, ResultFlag flag) noexcept {
    if (result_flags) {
        *result_flags |= flag;
    }
}

inline const char* status_name(Status status) noexcept {
    switch (status) {
    case TAIYIN_STATUS_OK: return "TAIYIN_STATUS_OK";
    case TAIYIN_ERROR_INVALID_ARGUMENT: return "TAIYIN_ERROR_INVALID_ARGUMENT";
    case TAIYIN_ERROR_OUT_OF_MEMORY: return "TAIYIN_ERROR_OUT_OF_MEMORY";
    case TAIYIN_ERROR_INTERNAL: return "TAIYIN_ERROR_INTERNAL";
    case TAIYIN_ERROR_UNSUPPORTED: return "TAIYIN_ERROR_UNSUPPORTED";
    case TAIYIN_EPHEMERIS_ERROR_NO_ROUTE: return "TAIYIN_EPHEMERIS_ERROR_NO_ROUTE";
    case TAIYIN_EPHEMERIS_ERROR_COVERAGE_GAP: return "TAIYIN_EPHEMERIS_ERROR_COVERAGE_GAP";
    case TAIYIN_EPHEMERIS_ERROR_LOAD_FAILED: return "TAIYIN_EPHEMERIS_ERROR_LOAD_FAILED";
    case TAIYIN_EPHEMERIS_ERROR_EVAL_FAILED: return "TAIYIN_EPHEMERIS_ERROR_EVAL_FAILED";
    case TAIYIN_EPHEMERIS_ERROR_COMPOSITE_MISSING_COMPONENT: return "TAIYIN_EPHEMERIS_ERROR_COMPOSITE_MISSING_COMPONENT";
    case TAIYIN_EPHEMERIS_ERROR_COMPOSITE_COVERAGE_GAP: return "TAIYIN_EPHEMERIS_ERROR_COMPOSITE_COVERAGE_GAP";
    case TAIYIN_EPHEMERIS_ERROR_COMPOSITE_METHOD_MISMATCH: return "TAIYIN_EPHEMERIS_ERROR_COMPOSITE_METHOD_MISMATCH";
    case TAIYIN_FILE_ERROR_NOT_FOUND: return "TAIYIN_FILE_ERROR_NOT_FOUND";
    case TAIYIN_FILE_ERROR_BAD_FORMAT: return "TAIYIN_FILE_ERROR_BAD_FORMAT";
    case TAIYIN_FILE_ERROR_UNSUPPORTED_FORMAT: return "TAIYIN_FILE_ERROR_UNSUPPORTED_FORMAT";
    case TAIYIN_FILE_ERROR_DISCOVERY_FAILED: return "TAIYIN_FILE_ERROR_DISCOVERY_FAILED";
    case TAIYIN_TIME_ERROR_EOP_OUT_OF_RANGE: return "TAIYIN_TIME_ERROR_EOP_OUT_OF_RANGE";
    case TAIYIN_TIME_ERROR_LEAP_SECOND_UNAVAILABLE: return "TAIYIN_TIME_ERROR_LEAP_SECOND_UNAVAILABLE";
    case TAIYIN_EVENT_ERROR_NOT_FOUND: return "TAIYIN_EVENT_ERROR_NOT_FOUND";
    case TAIYIN_RUNTIME_ERROR_NOT_INITIALIZED: return "TAIYIN_RUNTIME_ERROR_NOT_INITIALIZED";
    case TAIYIN_RUNTIME_ERROR_CACHE_INSERT_FAILED: return "TAIYIN_RUNTIME_ERROR_CACHE_INSERT_FAILED";
    case TAIYIN_RUNTIME_ERROR_REGISTRY_FAILED: return "TAIYIN_RUNTIME_ERROR_REGISTRY_FAILED";
    default: return "TAIYIN_STATUS_UNKNOWN";
    }
}

inline const char* status_message(Status status) noexcept {
    switch (status) {
    case TAIYIN_STATUS_OK: return "ok";
    case TAIYIN_ERROR_INVALID_ARGUMENT: return "invalid argument";
    case TAIYIN_ERROR_OUT_OF_MEMORY: return "out of memory";
    case TAIYIN_ERROR_INTERNAL: return "internal error";
    case TAIYIN_ERROR_UNSUPPORTED: return "unsupported operation";
    case TAIYIN_EPHEMERIS_ERROR_NO_ROUTE: return "no ephemeris route matches the request";
    case TAIYIN_EPHEMERIS_ERROR_COVERAGE_GAP: return "ephemeris route exists but does not cover the requested time";
    case TAIYIN_EPHEMERIS_ERROR_LOAD_FAILED: return "ephemeris block load failed";
    case TAIYIN_EPHEMERIS_ERROR_EVAL_FAILED: return "ephemeris block evaluation failed";
    case TAIYIN_EPHEMERIS_ERROR_COMPOSITE_MISSING_COMPONENT: return "composite ephemeris component is missing";
    case TAIYIN_EPHEMERIS_ERROR_COMPOSITE_COVERAGE_GAP: return "composite ephemeris component does not cover the requested time";
    case TAIYIN_EPHEMERIS_ERROR_COMPOSITE_METHOD_MISMATCH: return "composite ephemeris components use incompatible methods";
    case TAIYIN_FILE_ERROR_NOT_FOUND: return "file not found";
    case TAIYIN_FILE_ERROR_BAD_FORMAT: return "bad file format";
    case TAIYIN_FILE_ERROR_UNSUPPORTED_FORMAT: return "unsupported file format";
    case TAIYIN_FILE_ERROR_DISCOVERY_FAILED: return "file discovery failed";
    case TAIYIN_TIME_ERROR_EOP_OUT_OF_RANGE: return "earth orientation data does not cover the requested time";
    case TAIYIN_TIME_ERROR_LEAP_SECOND_UNAVAILABLE: return "leap second data is unavailable for the requested time";
    case TAIYIN_EVENT_ERROR_NOT_FOUND: return "event was not found within the search bounds";
    case TAIYIN_RUNTIME_ERROR_NOT_INITIALIZED: return "runtime service is not initialized";
    case TAIYIN_RUNTIME_ERROR_CACHE_INSERT_FAILED: return "runtime cache insert failed";
    case TAIYIN_RUNTIME_ERROR_REGISTRY_FAILED: return "runtime registry operation failed";
    default: return "unknown Taiyin status";
    }
}

inline StatusCategory status_category(Status status) noexcept {
    if (status == TAIYIN_STATUS_OK) {
        return TAIYIN_STATUS_CATEGORY_OK;
    }

    const int32_t code = status < 0 ? -status : status;
    if (code < 1000) return TAIYIN_STATUS_CATEGORY_GENERIC;
    if (code < 2000) return TAIYIN_STATUS_CATEGORY_EPHEMERIS;
    if (code < 3000) return TAIYIN_STATUS_CATEGORY_FILE;
    if (code < 4000) return TAIYIN_STATUS_CATEGORY_TIME;
    if (code < 5000) return TAIYIN_STATUS_CATEGORY_OBSERVER;
    if (code < 6000) return TAIYIN_STATUS_CATEGORY_EVENT;
    if (code < 7000) return TAIYIN_STATUS_CATEGORY_RUNTIME;
    return TAIYIN_STATUS_CATEGORY_UNKNOWN;
}

}  // namespace taiyin

#endif  // TAIYIN_STATUS_H
