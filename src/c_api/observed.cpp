#include "taiyin/c/observed.h"

#include "c_api_internal.h"

#include <cstring>
#include <new>
#include <vector>

namespace {

bool valid_outputs(
    taiyin_observed_position* out,
    taiyin_ephemeris_diagnostic* diagnostics,
    size_t count
) noexcept {
    for (size_t i = 0; i < count; ++i) {
        if (!taiyin_c_internal::valid_struct(&out[i])
            || (diagnostics
                && !taiyin_c_internal::valid_struct(&diagnostics[i]))) {
            return false;
        }
    }
    return true;
}

template <typename Eval>
taiyin_call_result calc_observed(
    const taiyin_context* context,
    const int32_t* body_ids,
    size_t body_count,
    taiyin_observed_position* out_positions,
    taiyin_ephemeris_diagnostic* diagnostics,
    const Eval& eval
) {
    if (!context || (!body_ids && body_count != 0)
        || (!out_positions && body_count != 0)
        || !valid_outputs(out_positions, diagnostics, body_count)) {
        return taiyin_c_internal::pack_call_result(
            taiyin_c_internal::invalid_argument());
    }
    taiyin_c_internal::TrackedCalcContext tracked(context->value);
    try {
        std::vector<int> cpp_ids(body_count);
        std::vector<taiyin::runtime::ObservedPosition> cpp_out(body_count);
        std::vector<taiyin::runtime::EphemerisEvalDiagnostic> cpp_diagnostics(
            diagnostics ? body_count : 0);
        for (size_t i = 0; i < body_count; ++i) cpp_ids[i] = body_ids[i];
        const taiyin::Status status = eval(
            &tracked.value,
            cpp_ids.empty() ? 0 : cpp_ids.data(),
            cpp_out.empty() ? 0 : cpp_out.data(),
            cpp_diagnostics.empty() ? 0 : cpp_diagnostics.data());
        for (size_t i = 0; i < body_count; ++i) {
            if (status == taiyin::TAIYIN_STATUS_OK) {
                taiyin_c_internal::from_cpp_observed(
                    cpp_out[i], &out_positions[i]);
            }
            if (diagnostics) {
                taiyin_c_internal::from_cpp_diagnostic(
                    cpp_diagnostics[i], &diagnostics[i]);
            }
        }
        return taiyin_c_internal::pack_call_result(status, tracked.flags);
    } catch (const std::bad_alloc&) {
        return taiyin_c_internal::pack_call_result(
            taiyin::TAIYIN_ERROR_OUT_OF_MEMORY, tracked.flags);
    } catch (...) {
        return taiyin_c_internal::pack_call_result(
            taiyin::TAIYIN_ERROR_INTERNAL, tracked.flags);
    }
}

}  // namespace

extern "C" {

void TAIYIN_C_CALL taiyin_observed_position_init(
    taiyin_observed_position* value
) {
    if (!value) return;
    std::memset(value, 0, sizeof(*value));
    value->struct_size = sizeof(*value);
    taiyin_c_internal::initialize_c_diagnostic(&value->diagnostic);
    taiyin_c_internal::initialize_c_diagnostic(&value->apparent.diagnostic);
    taiyin_c_internal::initialize_c_state(&value->apparent.geometric_state);
    taiyin_c_internal::initialize_c_state(&value->apparent.apparent_state);
}

taiyin_call_result TAIYIN_C_CALL taiyin_calc_observed_bodies_ut(
    const taiyin_context* context,
    const taiyin_split_julian_date* jd_ut,
    const int32_t* body_ids,
    size_t body_count,
    uint64_t flags,
    taiyin_observed_position* out_positions,
    taiyin_ephemeris_diagnostic* diagnostics
) {
    if (!taiyin_c_internal::valid_split_jd(jd_ut)) {
        return taiyin_c_internal::pack_call_result(taiyin_c_internal::invalid_argument());
    }
    return calc_observed(
        context, body_ids, body_count, out_positions, diagnostics,
        [&](const taiyin::runtime::NativeCalcContext* calc,
            const int* cpp_ids,
            taiyin::runtime::ObservedPosition* cpp_out,
            taiyin::runtime::EphemerisEvalDiagnostic* cpp_diagnostics) {
            return taiyin::runtime::calc_observed_ut(
                calc, taiyin_c_internal::to_cpp_split_jd(*jd_ut),
                cpp_ids, body_count, flags,
                cpp_out, cpp_diagnostics);
        });
}

taiyin_call_result TAIYIN_C_CALL taiyin_calc_observed_bodies_utc(
    const taiyin_context* context,
    const taiyin_calendar_datetime* datetime_utc,
    const int32_t* body_ids,
    size_t body_count,
    uint64_t flags,
    taiyin_observed_position* out_positions,
    taiyin_ephemeris_diagnostic* diagnostics
) {
    if (!taiyin_c_internal::valid_struct(datetime_utc)) {
        return taiyin_c_internal::pack_call_result(taiyin_c_internal::invalid_argument());
    }
    const taiyin::CalendarDateTime cpp_datetime =
        taiyin_c_internal::to_cpp_datetime(*datetime_utc);
    return calc_observed(
        context, body_ids, body_count, out_positions, diagnostics,
        [&](const taiyin::runtime::NativeCalcContext* calc,
            const int* cpp_ids,
            taiyin::runtime::ObservedPosition* cpp_out,
            taiyin::runtime::EphemerisEvalDiagnostic* cpp_diagnostics) {
            return taiyin::runtime::calc_observed_utc(
                calc, cpp_datetime, cpp_ids, body_count, flags,
                cpp_out, cpp_diagnostics);
        });
}

}  // extern "C"
