#ifndef TAIYIN_STATUS_H
#define TAIYIN_STATUS_H

#include <stdint.h>

namespace taiyin {

typedef int32_t TaiyinStatus;

enum TaiyinStatusCode {
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

    TAIYIN_RUNTIME_ERROR_NOT_INITIALIZED = -6001,
    TAIYIN_RUNTIME_ERROR_CACHE_INSERT_FAILED = -6002,
    TAIYIN_RUNTIME_ERROR_REGISTRY_FAILED = -6003,
};

enum TaiyinStatusCategory {
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

inline bool taiyin_status_ok(TaiyinStatus status) noexcept {
    return status == TAIYIN_STATUS_OK;
}

inline const char* taiyin_status_name(TaiyinStatus status) noexcept {
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
    case TAIYIN_RUNTIME_ERROR_NOT_INITIALIZED: return "TAIYIN_RUNTIME_ERROR_NOT_INITIALIZED";
    case TAIYIN_RUNTIME_ERROR_CACHE_INSERT_FAILED: return "TAIYIN_RUNTIME_ERROR_CACHE_INSERT_FAILED";
    case TAIYIN_RUNTIME_ERROR_REGISTRY_FAILED: return "TAIYIN_RUNTIME_ERROR_REGISTRY_FAILED";
    default: return "TAIYIN_STATUS_UNKNOWN";
    }
}

inline const char* taiyin_status_message(TaiyinStatus status) noexcept {
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
    case TAIYIN_RUNTIME_ERROR_NOT_INITIALIZED: return "runtime service is not initialized";
    case TAIYIN_RUNTIME_ERROR_CACHE_INSERT_FAILED: return "runtime cache insert failed";
    case TAIYIN_RUNTIME_ERROR_REGISTRY_FAILED: return "runtime registry operation failed";
    default: return "unknown Taiyin status";
    }
}

inline TaiyinStatusCategory taiyin_status_category(TaiyinStatus status) noexcept {
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
