#ifndef TAIYIN_C_API_INTERNAL_H
#define TAIYIN_C_API_INTERNAL_H

#include "taiyin/c/base.h"
#include "taiyin/c/observed.h"
#include "taiyin/runtime/ephemeris_engine.h"
#include "taiyin/runtime/native_context.h"
#include "taiyin/runtime/observed_position.h"
#include "taiyin/state.h"
#include "taiyin/time.h"

#include <cstring>
#include <limits>
#include <vector>

struct taiyin_context {
    taiyin::runtime::NativeCalcContext value;
    std::vector<taiyin::runtime::ApparentDeflector> deflectors;

    taiyin_context() noexcept : value(), deflectors() {}
};

namespace taiyin_c_internal {

template <typename T>
inline bool valid_struct(const T* value) noexcept {
    return value && value->struct_size >= sizeof(T);
}

inline taiyin::SplitJulianDate to_cpp_split_jd(
    const taiyin_split_julian_date& value
) noexcept {
    taiyin::SplitJulianDate out;
    if (!taiyin::normalize_split_julian_date(
            value.day_number, value.day_fraction, &out)) {
        return taiyin::SplitJulianDate(
            0, std::numeric_limits<double>::quiet_NaN());
    }
    return out;
}

inline bool valid_split_jd(
    const taiyin_split_julian_date* value
) noexcept {
    return value
        && taiyin::split_julian_date_is_finite(to_cpp_split_jd(*value));
}

inline bool valid_optional_split_jd(
    const taiyin_split_julian_date* value
) noexcept {
    return !value || valid_split_jd(value);
}

inline void from_cpp_split_jd(
    const taiyin::SplitJulianDate& value,
    taiyin_split_julian_date* out
) noexcept {
    if (!out) return;
    taiyin::SplitJulianDate normalized;
    if (!taiyin::normalize_split_julian_date(
            value.day_number, value.day_fraction, &normalized)) {
        out->day_number = 0;
        out->day_fraction = std::numeric_limits<double>::quiet_NaN();
        return;
    }
    out->day_number = normalized.day_number;
    out->day_fraction = normalized.day_fraction;
}

inline taiyin::CalendarDateTime to_cpp_datetime(
    const taiyin_calendar_datetime& value
) noexcept {
    taiyin::CalendarDateTime out;
    out.year = value.year;
    out.month = value.month;
    out.day = value.day;
    out.hour = value.hour;
    out.minute = value.minute;
    out.second = value.second;
    return out;
}

inline void from_cpp_datetime(
    const taiyin::CalendarDateTime& value,
    taiyin_calendar_datetime* out
) noexcept {
    out->year = value.year;
    out->month = value.month;
    out->day = value.day;
    out->hour = value.hour;
    out->minute = value.minute;
    out->second = value.second;
}

inline void from_cpp_vector(
    const taiyin::Vector3& value,
    taiyin_vector3* out
) noexcept {
    out->x = value.x;
    out->y = value.y;
    out->z = value.z;
}

inline void from_cpp_state(
    const taiyin::CartesianState& value,
    taiyin_cartesian_state* out
) noexcept {
    from_cpp_vector(value.position_au, &out->position_au);
    from_cpp_vector(value.velocity_au_per_day, &out->velocity_au_per_day);
    from_cpp_vector(value.acceleration_au_per_day2, &out->acceleration_au_per_day2);
}

inline taiyin::CartesianState to_cpp_state(
    const taiyin_cartesian_state& value
) noexcept {
    taiyin::CartesianState out;
    out.position_au = taiyin::Vector3{
        value.position_au.x, value.position_au.y, value.position_au.z};
    out.velocity_au_per_day = taiyin::Vector3{
        value.velocity_au_per_day.x,
        value.velocity_au_per_day.y,
        value.velocity_au_per_day.z};
    out.acceleration_au_per_day2 = taiyin::Vector3{
        value.acceleration_au_per_day2.x,
        value.acceleration_au_per_day2.y,
        value.acceleration_au_per_day2.z};
    return out;
}

inline void from_cpp_diagnostic(
    const taiyin::runtime::EphemerisEvalDiagnostic& value,
    taiyin_ephemeris_diagnostic* out
) noexcept {
    if (!out) return;
    out->status = value.status;
    out->target_id = value.target_id;
    out->center_id = value.center_id;
    out->frame = static_cast<int32_t>(value.frame);
    from_cpp_split_jd(value.jd_tdb, &out->jd_tdb);
    out->candidate_count = value.candidate_count;
    out->attempted_method_id = value.attempted_method_id;
    out->nearest_coverage_start = value.nearest_coverage_start;
    out->nearest_coverage_end = value.nearest_coverage_end;
    out->component_target_id = value.component_target_id;
    out->component_center_id = value.component_center_id;
    out->component_method_id = value.component_method_id;
    out->time_scale_route = value.time_scale_route;
    out->time_scale_fallback_reason = value.time_scale_fallback_reason;
    out->time_scale_flags = value.time_scale_flags;
    out->reserved0 = 0u;
    out->tai_minus_utc_seconds = value.tai_minus_utc_seconds;
    out->dut1_seconds = value.dut1_seconds;
    out->delta_t_seconds = value.delta_t_seconds;
}

inline void to_cpp_diagnostic(
    const taiyin_ephemeris_diagnostic& value,
    taiyin::runtime::EphemerisEvalDiagnostic* out
) noexcept {
    if (!out) return;
    out->status = static_cast<taiyin::Status>(value.status);
    out->target_id = value.target_id;
    out->center_id = value.center_id;
    out->frame = static_cast<taiyin::internal::EphemerisFrame>(value.frame);
    out->jd_tdb = to_cpp_split_jd(value.jd_tdb);
    out->candidate_count = value.candidate_count;
    out->attempted_method_id = value.attempted_method_id;
    out->nearest_coverage_start = value.nearest_coverage_start;
    out->nearest_coverage_end = value.nearest_coverage_end;
    out->component_target_id = value.component_target_id;
    out->component_center_id = value.component_center_id;
    out->component_method_id = value.component_method_id;
    out->time_scale_route = value.time_scale_route;
    out->time_scale_fallback_reason = value.time_scale_fallback_reason;
    out->time_scale_flags = value.time_scale_flags;
    out->tai_minus_utc_seconds = value.tai_minus_utc_seconds;
    out->dut1_seconds = value.dut1_seconds;
    out->delta_t_seconds = value.delta_t_seconds;
}

inline void initialize_c_state(taiyin_cartesian_state* out) noexcept {
    std::memset(out, 0, sizeof(*out));
    out->struct_size = sizeof(*out);
}

inline void initialize_c_diagnostic(taiyin_ephemeris_diagnostic* out) noexcept {
    std::memset(out, 0, sizeof(*out));
    out->struct_size = sizeof(*out);
}

inline void from_cpp_horizontal(
    const taiyin::HorizontalCoordinates& value,
    taiyin_horizontal_coordinates* out
) noexcept {
    out->azimuth_rad = value.azimuth_rad;
    out->altitude_rad = value.altitude_rad;
    out->distance_au = value.distance_au;
}

inline void from_cpp_horizontal_rates(
    const taiyin::HorizontalRates& value,
    taiyin_horizontal_rates* out
) noexcept {
    out->azimuth_rate_rad_per_day = value.azimuth_rate_rad_per_day;
    out->altitude_rate_rad_per_day = value.altitude_rate_rad_per_day;
    out->distance_rate_au_per_day = value.distance_rate_au_per_day;
}

inline void from_cpp_apparent(
    const taiyin::runtime::MajorBodyApparentPosition& value,
    taiyin_apparent_position* out
) noexcept {
    out->body_id = value.body_id;
    out->body_mask_bit = value.body_mask_bit;
    out->status = value.status;
    initialize_c_diagnostic(&out->diagnostic);
    from_cpp_diagnostic(value.diagnostic, &out->diagnostic);
    initialize_c_state(&out->geometric_state);
    from_cpp_state(value.geometric_state, &out->geometric_state);
    initialize_c_state(&out->apparent_state);
    from_cpp_state(value.apparent_state, &out->apparent_state);
    out->longitude_rad = value.longitude_rad;
    out->latitude_rad = value.latitude_rad;
    out->distance_au = value.distance_au;
    out->light_time_days = value.light_time_days;
    out->cache_hit = value.cache_hit ? 1u : 0u;
    std::memset(out->reserved, 0, sizeof(out->reserved));
}

inline void from_cpp_observed(
    const taiyin::runtime::ObservedPosition& value,
    taiyin_observed_position* out
) noexcept {
    out->struct_size = sizeof(*out);
    out->body_id = value.body_id;
    out->status = value.status;
    initialize_c_diagnostic(&out->diagnostic);
    from_cpp_diagnostic(value.diagnostic, &out->diagnostic);
    from_cpp_apparent(value.apparent, &out->apparent);
    from_cpp_horizontal(value.horizontal, &out->horizontal);
    from_cpp_horizontal_rates(value.horizontal_rates, &out->horizontal_rates);
    from_cpp_horizontal(value.refracted_horizontal, &out->refracted_horizontal);
    from_cpp_horizontal_rates(
        value.refracted_horizontal_rates, &out->refracted_horizontal_rates);
}

inline void repair_context_pointers(taiyin_context* context) noexcept {
    if (!context) return;
    context->value.apparent_options.model_context = &context->value.model_context;
    if (!context->deflectors.empty()) {
        context->value.apparent_options.deflectors = context->deflectors.data();
        context->value.apparent_options.deflector_count = context->deflectors.size();
    }
}

inline taiyin_status invalid_argument() noexcept {
    return static_cast<taiyin_status>(taiyin::TAIYIN_ERROR_INVALID_ARGUMENT);
}

inline taiyin_status out_of_memory() noexcept {
    return static_cast<taiyin_status>(taiyin::TAIYIN_ERROR_OUT_OF_MEMORY);
}

// Single packing authority for C API implementations: every migrated
// taiyin_call_result return goes through here, never through handwritten
// shifts.  result_flags comes from the runtime's per-call execution facts
// (typically a stack-local uint32_t in the wrapper); it is never derived
// from the rich diagnostic.
inline taiyin_call_result pack_call_result(
    taiyin::Status status,
    uint32_t result_flags = 0u
) noexcept {
    return taiyin_make_call_result(static_cast<taiyin_status>(status), result_flags);
}

// Search-operation helper: owns the operation flags word, the tracker that
// observes per-(target, center) source-id switches, and a context copy that
// points at them.  The model_context self-pointer must be reseated after the
// copy because NativeCalcContext stores it as a raw pointer.
struct TrackedCalcContext {
    uint32_t flags = 0u;
    taiyin::SourceSwitchTracker tracker{&flags};
    taiyin::runtime::NativeCalcContext value;

    explicit TrackedCalcContext(
        const taiyin::runtime::NativeCalcContext& source
    )
        : value(source) {
        value.apparent_options.model_context = &value.model_context;
        value.source_tracker = &tracker;
    }

    TrackedCalcContext(const TrackedCalcContext&) = delete;
    TrackedCalcContext& operator=(const TrackedCalcContext&) = delete;
};

}  // namespace taiyin_c_internal

#endif
