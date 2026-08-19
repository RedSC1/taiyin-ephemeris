#include "taiyin/c/base.h"

#include "taiyin/status.h"

#include <cstdio>
#include <cstring>

// The C ABI enum and the C++ runtime constants must stay in lockstep.
static_assert(
    static_cast<uint32_t>(TAIYIN_RESULT_FLAG_FALLBACK_OCCURRED)
        == static_cast<uint32_t>(taiyin::kResultFlagFallbackOccurred),
    "C and C++ FALLBACK_OCCURRED flag values diverged");
static_assert(
    static_cast<uint32_t>(TAIYIN_RESULT_FLAG_NUMERICAL_DERIVATIVE)
        == static_cast<uint32_t>(taiyin::kResultFlagNumericalDerivative),
    "C and C++ NUMERICAL_DERIVATIVE flag values diverged");
static_assert(
    static_cast<uint32_t>(TAIYIN_RESULT_FLAG_BARYCENTER_APPROX)
        == static_cast<uint32_t>(taiyin::kResultFlagBarycenterApprox),
    "C and C++ BARYCENTER_APPROX flag values diverged");
static_assert(
    static_cast<uint32_t>(TAIYIN_RESULT_FLAG_TIME_SCALE_FALLBACK)
        == static_cast<uint32_t>(taiyin::kResultFlagTimeScaleFallback),
    "C and C++ TIME_SCALE_FALLBACK flag values diverged");
static_assert(
    static_cast<uint32_t>(TAIYIN_RESULT_FLAG_HISTORICAL_EVENT_ASSIGNMENT_APPLIED)
        == static_cast<uint32_t>(
            taiyin::kResultFlagHistoricalEventAssignmentApplied),
    "C and C++ HISTORICAL_EVENT_ASSIGNMENT_APPLIED flag values diverged");
static_assert(
    static_cast<uint32_t>(TAIYIN_RESULT_FLAG_HISTORICAL_CALENDAR_RULES_APPLIED)
        == static_cast<uint32_t>(
            taiyin::kResultFlagHistoricalCalendarRulesApplied),
    "C and C++ HISTORICAL_CALENDAR_RULES_APPLIED flag values diverged");
static_assert(
    static_cast<uint32_t>(TAIYIN_RESULT_FLAG_HISTORICAL_PILLAR_TERMS_APPLIED)
        == static_cast<uint32_t>(
            taiyin::kResultFlagHistoricalPillarTermsApplied),
    "C and C++ HISTORICAL_PILLAR_TERMS_APPLIED flag values diverged");

extern "C" {

void TAIYIN_C_CALL taiyin_cartesian_state_init(taiyin_cartesian_state* value) {
    if (!value) return;
    std::memset(value, 0, sizeof(*value));
    value->struct_size = sizeof(*value);
}

void TAIYIN_C_CALL taiyin_calendar_datetime_init(taiyin_calendar_datetime* value) {
    if (!value) return;
    std::memset(value, 0, sizeof(*value));
    value->struct_size = sizeof(*value);
}

void TAIYIN_C_CALL taiyin_ephemeris_diagnostic_init(
    taiyin_ephemeris_diagnostic* value
) {
    if (!value) return;
    std::memset(value, 0, sizeof(*value));
    value->struct_size = sizeof(*value);
}

uint32_t TAIYIN_C_CALL taiyin_get_c_abi_version(void) {
    return TAIYIN_C_ABI_VERSION;
}

const char* TAIYIN_C_CALL taiyin_get_library_version(void) {
    return TAIYIN_LIBRARY_VERSION_STRING;
}

const char* TAIYIN_C_CALL taiyin_get_library_codename(void) {
    return TAIYIN_LIBRARY_CODENAME;
}

uint64_t TAIYIN_C_CALL taiyin_get_capabilities(void) {
    return TAIYIN_CAPABILITY_RUNTIME
        | TAIYIN_CAPABILITY_TIME
        | TAIYIN_CAPABILITY_POSITION
        | TAIYIN_CAPABILITY_STAR
        | TAIYIN_CAPABILITY_VISIBILITY
        | TAIYIN_CAPABILITY_PHENOMENA
        | TAIYIN_CAPABILITY_EVENTS
        | TAIYIN_CAPABILITY_ECLIPSE
        | TAIYIN_CAPABILITY_OCCULTATION
        | TAIYIN_CAPABILITY_HELIACAL
        | TAIYIN_CAPABILITY_CUSTOM_TARGETS
        | TAIYIN_CAPABILITY_CUSTOM_AYANAMSHA
        | TAIYIN_CAPABILITY_CUSTOM_HOUSES
        | TAIYIN_CAPABILITY_SPLIT_TIME
        | TAIYIN_CAPABILITY_ASTROLOGY
        | TAIYIN_CAPABILITY_CHINESE_CALENDAR
        | TAIYIN_CAPABILITY_GANZHI_CALENDAR
#ifdef TAIYIN_C_HAS_BAZI_EXTENSION
        | TAIYIN_CAPABILITY_BAZI
#endif
#ifdef TAIYIN_C_HAS_ZIWEI_EXTENSION
        | TAIYIN_CAPABILITY_ZIWEI
#endif
        ;
}

taiyin_status TAIYIN_C_CALL taiyin_format_ephemeris_diagnostic(
    const taiyin_ephemeris_diagnostic* diagnostic,
    char* buffer,
    size_t capacity,
    size_t* out_required_size
) {
    if (!diagnostic
        || diagnostic->struct_size < sizeof(*diagnostic)
        || (!buffer && capacity != 0u)
        || !out_required_size) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }

    const int written = std::snprintf(
        buffer,
        capacity,
        "status=%s(%d) target=%d center=%d frame=%d jd_tdb=%lld+%.17g "
        "candidates=%d method=%d coverage=[%.17g,%.17g] "
        "component=(%d,%d,%d) time_route=%u fallback=%u time_flags=%u "
        "tai_minus_utc=%.17g dut1=%.17g delta_t=%.17g",
        taiyin::status_name(diagnostic->status),
        static_cast<int>(diagnostic->status),
        static_cast<int>(diagnostic->target_id),
        static_cast<int>(diagnostic->center_id),
        static_cast<int>(diagnostic->frame),
        static_cast<long long>(diagnostic->jd_tdb.day_number),
        diagnostic->jd_tdb.day_fraction,
        static_cast<int>(diagnostic->candidate_count),
        static_cast<int>(diagnostic->attempted_method_id),
        diagnostic->nearest_coverage_start,
        diagnostic->nearest_coverage_end,
        static_cast<int>(diagnostic->component_target_id),
        static_cast<int>(diagnostic->component_center_id),
        static_cast<int>(diagnostic->component_method_id),
        static_cast<unsigned>(diagnostic->time_scale_route),
        static_cast<unsigned>(diagnostic->time_scale_fallback_reason),
        static_cast<unsigned>(diagnostic->time_scale_flags),
        diagnostic->tai_minus_utc_seconds,
        diagnostic->dut1_seconds,
        diagnostic->delta_t_seconds);
    if (written < 0) {
        *out_required_size = 0u;
        return TAIYIN_ERROR_INTERNAL;
    }

    *out_required_size = static_cast<size_t>(written) + 1u;
    return !buffer || capacity >= *out_required_size
        ? TAIYIN_STATUS_OK
        : TAIYIN_ERROR_OUT_OF_MEMORY;
}

const char* TAIYIN_C_CALL taiyin_status_name(taiyin_status status) {
    return taiyin::status_name(status);
}

taiyin_call_result TAIYIN_C_CALL taiyin_make_call_result(
    taiyin_status status,
    uint32_t result_flags
) {
    // The status sign bit lands on the int64 sign bit, so a non-negative
    // taiyin_call_result always means success, with or without flags.
    return static_cast<taiyin_call_result>(
        (static_cast<uint64_t>(static_cast<uint32_t>(status)) << 32)
        | static_cast<uint64_t>(result_flags));
}

taiyin_status TAIYIN_C_CALL taiyin_call_result_status(
    taiyin_call_result result
) {
    return static_cast<taiyin_status>(
        static_cast<uint32_t>(static_cast<uint64_t>(result) >> 32));
}

uint32_t TAIYIN_C_CALL taiyin_call_result_flags(taiyin_call_result result) {
    return static_cast<uint32_t>(static_cast<uint64_t>(result) & 0xFFFFFFFFu);
}

taiyin_bool TAIYIN_C_CALL taiyin_call_result_ok(taiyin_call_result result) {
    return result >= 0 ? 1u : 0u;
}

const char* TAIYIN_C_CALL taiyin_status_message(taiyin_status status) {
    return taiyin::status_message(status);
}

int32_t TAIYIN_C_CALL taiyin_status_category(taiyin_status status) {
    return static_cast<int32_t>(taiyin::status_category(status));
}

}  // extern "C"
