#include "taiyin/c/position.h"

#include "c_api_internal.h"
#include "position_lifecycle_internal.h"
#include "taiyin/runtime/native_position.h"

#include <limits>
#include <new>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace {

bool valid_diagnostic(taiyin_ephemeris_diagnostic* diagnostic) noexcept {
    return !diagnostic || taiyin_c_internal::valid_struct(diagnostic);
}

taiyin::SplitJulianDate cpp_date(
    const taiyin_split_julian_date* value
) noexcept {
    return value
        ? taiyin_c_internal::to_cpp_split_jd(*value)
        : taiyin::SplitJulianDate(
            0, std::numeric_limits<double>::quiet_NaN());
}

struct CPositionEvaluatorEntry {
    taiyin_native_position_evaluator_fn position;
    taiyin_native_state_evaluator_fn state;
    void* user_data;
};

std::mutex g_c_position_evaluator_mutex;
std::unordered_map<int, CPositionEvaluatorEntry> g_c_position_evaluators;

bool find_c_position_evaluator(
    int target_id,
    CPositionEvaluatorEntry* out
) noexcept {
    try {
        std::lock_guard<std::mutex> lock(g_c_position_evaluator_mutex);
        const std::unordered_map<int, CPositionEvaluatorEntry>::const_iterator it =
            g_c_position_evaluators.find(target_id);
        if (it == g_c_position_evaluators.end()) return false;
        *out = it->second;
        return true;
    } catch (...) {
        return false;
    }
}

taiyin::Status c_position_evaluator_bridge(
    const taiyin::runtime::NativeCalcContext* context,
    int target_id,
    const taiyin::SplitJulianDate& jd_tdb,
    const taiyin::SplitJulianDate& jd_tt,
    uint32_t flags,
    double out[6],
    taiyin::runtime::EphemerisEvalDiagnostic* diagnostic
) {
    CPositionEvaluatorEntry entry = {};
    if (!context || !out || !find_c_position_evaluator(target_id, &entry)
        || !entry.position) {
        return taiyin::TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    try {
        taiyin_context borrowed;
        borrowed.value = *context;
        if (context->apparent_options.deflectors
            && context->apparent_options.deflector_count > 0) {
            borrowed.deflectors.assign(
                context->apparent_options.deflectors,
                context->apparent_options.deflectors
                    + context->apparent_options.deflector_count);
        }
        taiyin_c_internal::repair_context_pointers(&borrowed);
        taiyin_ephemeris_diagnostic c_diagnostic;
        taiyin_c_internal::initialize_c_diagnostic(&c_diagnostic);
        taiyin_split_julian_date c_jd_tdb;
        taiyin_split_julian_date c_jd_tt;
        taiyin_c_internal::from_cpp_split_jd(jd_tdb, &c_jd_tdb);
        taiyin_c_internal::from_cpp_split_jd(jd_tt, &c_jd_tt);
        const taiyin_status status = entry.position(
            &borrowed, target_id, &c_jd_tdb, &c_jd_tt, flags, out,
            diagnostic ? &c_diagnostic : nullptr, entry.user_data);
        if (diagnostic) {
            taiyin_c_internal::to_cpp_diagnostic(c_diagnostic, diagnostic);
        }
        return static_cast<taiyin::Status>(status);
    } catch (const std::bad_alloc&) {
        return taiyin::TAIYIN_ERROR_OUT_OF_MEMORY;
    } catch (...) {
        return taiyin::TAIYIN_ERROR_INTERNAL;
    }
}

taiyin::Status c_state_evaluator_bridge(
    const taiyin::runtime::NativeCalcContext* context,
    int target_id,
    const taiyin::SplitJulianDate& jd_tdb,
    const taiyin::SplitJulianDate& jd_tt,
    uint32_t flags,
    taiyin::CartesianState* out,
    taiyin::runtime::EphemerisEvalDiagnostic* diagnostic
) {
    CPositionEvaluatorEntry entry = {};
    if (!context || !out || !find_c_position_evaluator(target_id, &entry)
        || !entry.state) {
        return taiyin::TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    try {
        taiyin_context borrowed;
        borrowed.value = *context;
        if (context->apparent_options.deflectors
            && context->apparent_options.deflector_count > 0) {
            borrowed.deflectors.assign(
                context->apparent_options.deflectors,
                context->apparent_options.deflectors
                    + context->apparent_options.deflector_count);
        }
        taiyin_c_internal::repair_context_pointers(&borrowed);
        taiyin_cartesian_state c_state;
        taiyin_c_internal::initialize_c_state(&c_state);
        taiyin_ephemeris_diagnostic c_diagnostic;
        taiyin_c_internal::initialize_c_diagnostic(&c_diagnostic);
        taiyin_split_julian_date c_jd_tdb;
        taiyin_split_julian_date c_jd_tt;
        taiyin_c_internal::from_cpp_split_jd(jd_tdb, &c_jd_tdb);
        taiyin_c_internal::from_cpp_split_jd(jd_tt, &c_jd_tt);
        const taiyin_status status = entry.state(
            &borrowed, target_id, &c_jd_tdb, &c_jd_tt, flags, &c_state,
            diagnostic ? &c_diagnostic : nullptr, entry.user_data);
        if (status == TAIYIN_STATUS_OK) {
            out->position_au = taiyin::Vector3{
                c_state.position_au.x, c_state.position_au.y,
                c_state.position_au.z};
            out->velocity_au_per_day = taiyin::Vector3{
                c_state.velocity_au_per_day.x, c_state.velocity_au_per_day.y,
                c_state.velocity_au_per_day.z};
            out->acceleration_au_per_day2 = taiyin::Vector3{
                c_state.acceleration_au_per_day2.x,
                c_state.acceleration_au_per_day2.y,
                c_state.acceleration_au_per_day2.z};
        }
        if (diagnostic) {
            taiyin_c_internal::to_cpp_diagnostic(c_diagnostic, diagnostic);
        }
        return static_cast<taiyin::Status>(status);
    } catch (const std::bad_alloc&) {
        return taiyin::TAIYIN_ERROR_OUT_OF_MEMORY;
    } catch (...) {
        return taiyin::TAIYIN_ERROR_INTERNAL;
    }
}

void finish_diagnostic(
    const taiyin::runtime::EphemerisEvalDiagnostic& source,
    taiyin_ephemeris_diagnostic* destination
) noexcept {
    taiyin_c_internal::from_cpp_diagnostic(source, destination);
}

template <typename Eval>
taiyin_status calc_positions_batch(
    const taiyin_context* context,
    const int32_t* target_ids,
    size_t target_count,
    double* out_positions,
    taiyin_ephemeris_diagnostic* diagnostics,
    const Eval& eval
) {
    if (!context || (!target_ids && target_count != 0)
        || (!out_positions && target_count != 0)) {
        return taiyin_c_internal::invalid_argument();
    }
    for (size_t i = 0; diagnostics && i < target_count; ++i) {
        if (!taiyin_c_internal::valid_struct(&diagnostics[i])) {
            return taiyin_c_internal::invalid_argument();
        }
    }
    try {
        std::vector<int> cpp_ids(target_count);
        std::vector<taiyin::runtime::EphemerisEvalDiagnostic> cpp_diagnostics(
            diagnostics ? target_count : 0);
        for (size_t i = 0; i < target_count; ++i) cpp_ids[i] = target_ids[i];
        const taiyin::Status status = eval(
            cpp_ids.empty() ? 0 : cpp_ids.data(),
            cpp_diagnostics.empty() ? 0 : cpp_diagnostics.data());
        for (size_t i = 0; diagnostics && i < target_count; ++i) {
            finish_diagnostic(cpp_diagnostics[i], &diagnostics[i]);
        }
        return status;
    } catch (const std::bad_alloc&) {
        return taiyin::TAIYIN_ERROR_OUT_OF_MEMORY;
    } catch (...) {
        return taiyin::TAIYIN_ERROR_INTERNAL;
    }
}

}  // namespace

namespace taiyin_c_internal {

std::mutex& native_position_lifecycle_mutex() noexcept {
    static std::mutex mutex;
    return mutex;
}

void clear_native_position_evaluators_locked() noexcept {
    try {
        std::lock_guard<std::mutex> lock(g_c_position_evaluator_mutex);
        for (std::unordered_map<int, CPositionEvaluatorEntry>::const_iterator
                 it = g_c_position_evaluators.begin();
             it != g_c_position_evaluators.end();
             ++it) {
            taiyin::runtime::unregister_global_native_position_evaluator(
                it->first);
        }
        g_c_position_evaluators.clear();
    } catch (...) {
        // This is a best-effort setup-time cleanup API with no failure channel.
        // Any entry not erased remains registered and retains its callback.
    }
}

}  // namespace taiyin_c_internal

extern "C" {

taiyin_call_result TAIYIN_C_CALL taiyin_register_native_position_evaluator(
    int32_t target_id,
    taiyin_native_position_evaluator_fn position_evaluator,
    taiyin_native_state_evaluator_fn state_evaluator,
    void* user_data
) {
    if (target_id >= 0 || !position_evaluator) {
        return taiyin_c_internal::pack_call_result(taiyin_c_internal::invalid_argument());
    }
    try {
        std::lock_guard<std::mutex> lifecycle_lock(
            taiyin_c_internal::native_position_lifecycle_mutex());
        std::lock_guard<std::mutex> lock(g_c_position_evaluator_mutex);
        if (g_c_position_evaluators.find(target_id)
            != g_c_position_evaluators.end()) {
            return taiyin_c_internal::pack_call_result(TAIYIN_ERROR_INVALID_ARGUMENT);
        }
        CPositionEvaluatorEntry entry = {
            position_evaluator, state_evaluator, user_data};
        const std::pair<
            std::unordered_map<int, CPositionEvaluatorEntry>::iterator,
            bool> inserted =
            g_c_position_evaluators.emplace(target_id, entry);
        if (!inserted.second) return taiyin_c_internal::pack_call_result(TAIYIN_ERROR_INTERNAL);
        const bool registered =
            taiyin::runtime::register_global_native_position_evaluator(
                target_id, &c_position_evaluator_bridge,
                state_evaluator ? &c_state_evaluator_bridge : nullptr);
        if (!registered) {
            g_c_position_evaluators.erase(target_id);
            return taiyin_c_internal::pack_call_result(TAIYIN_ERROR_INVALID_ARGUMENT);
        }
        return taiyin_c_internal::pack_call_result(TAIYIN_STATUS_OK);
    } catch (const std::bad_alloc&) {
        return taiyin_c_internal::pack_call_result(TAIYIN_ERROR_OUT_OF_MEMORY);
    } catch (...) {
        return taiyin_c_internal::pack_call_result(TAIYIN_ERROR_INTERNAL);
    }
}

taiyin_call_result TAIYIN_C_CALL taiyin_unregister_native_position_evaluator(
    int32_t target_id
) {
    if (target_id >= 0) return taiyin_c_internal::pack_call_result(taiyin_c_internal::invalid_argument());
    try {
        std::lock_guard<std::mutex> lifecycle_lock(
            taiyin_c_internal::native_position_lifecycle_mutex());
        std::lock_guard<std::mutex> lock(g_c_position_evaluator_mutex);
        const std::unordered_map<int, CPositionEvaluatorEntry>::iterator it =
            g_c_position_evaluators.find(target_id);
        if (it == g_c_position_evaluators.end()) {
            return taiyin_c_internal::pack_call_result(TAIYIN_ERROR_INVALID_ARGUMENT);
        }
        if (!taiyin::runtime::unregister_global_native_position_evaluator(
                target_id)) {
            return taiyin_c_internal::pack_call_result(TAIYIN_ERROR_INTERNAL);
        }
        g_c_position_evaluators.erase(it);
        return taiyin_c_internal::pack_call_result(TAIYIN_STATUS_OK);
    } catch (...) {
        return taiyin_c_internal::pack_call_result(TAIYIN_ERROR_INTERNAL);
    }
}

void TAIYIN_C_CALL taiyin_clear_native_position_evaluators(void) {
    try {
        std::lock_guard<std::mutex> lifecycle_lock(
            taiyin_c_internal::native_position_lifecycle_mutex());
        taiyin_c_internal::clear_native_position_evaluators_locked();
    } catch (...) {
        // The public clear API has no failure channel.
    }
}

taiyin_call_result TAIYIN_C_CALL taiyin_calc_position_tdb(
    const taiyin_context* context,
    int32_t target_id,
    const taiyin_split_julian_date* jd_tdb,
    const taiyin_split_julian_date* jd_tt,
    uint32_t flags,
    double out_position[6],
    taiyin_ephemeris_diagnostic* diagnostic
) {
    if (!context || !taiyin_c_internal::valid_split_jd(jd_tdb)
        || !taiyin_c_internal::valid_optional_split_jd(jd_tt)
        || !out_position || !valid_diagnostic(diagnostic)) {
        return taiyin_c_internal::pack_call_result(taiyin_c_internal::invalid_argument());
    }
    const taiyin::SplitJulianDate cpp_jd_tdb = cpp_date(jd_tdb);
    const taiyin::SplitJulianDate cpp_jd_tt = jd_tt
        ? cpp_date(jd_tt)
        : cpp_jd_tdb;
    uint32_t result_flags = 0u;
    taiyin::runtime::EphemerisEvalDiagnostic cpp_diagnostic;
    const taiyin::Status status = taiyin::runtime::calc_position_tdb(
        &context->value,
        target_id,
        cpp_jd_tdb,
        cpp_jd_tt,
        flags,
        out_position,
        diagnostic ? &cpp_diagnostic : 0,
        &result_flags);
    finish_diagnostic(cpp_diagnostic, diagnostic);
    return taiyin_c_internal::pack_call_result(status, result_flags);
}

taiyin_call_result TAIYIN_C_CALL taiyin_calc_position_tt(
    const taiyin_context* context,
    int32_t target_id,
    const taiyin_split_julian_date* jd_tt,
    uint32_t flags,
    double out_position[6],
    taiyin_ephemeris_diagnostic* diagnostic
) {
    if (!context || !taiyin_c_internal::valid_split_jd(jd_tt)
        || !out_position || !valid_diagnostic(diagnostic)) {
        return taiyin_c_internal::pack_call_result(taiyin_c_internal::invalid_argument());
    }
    uint32_t result_flags = 0u;
    taiyin::runtime::EphemerisEvalDiagnostic cpp_diagnostic;
    const taiyin::Status status = taiyin::runtime::calc_position_tt(
        &context->value, target_id, cpp_date(jd_tt), flags, out_position,
        diagnostic ? &cpp_diagnostic : 0,
        &result_flags);
    finish_diagnostic(cpp_diagnostic, diagnostic);
    return taiyin_c_internal::pack_call_result(status, result_flags);
}

taiyin_call_result TAIYIN_C_CALL taiyin_calc_position_ut(
    const taiyin_context* context,
    int32_t target_id,
    const taiyin_split_julian_date* jd_ut,
    uint32_t flags,
    double out_position[6],
    taiyin_ephemeris_diagnostic* diagnostic
) {
    if (!context || !taiyin_c_internal::valid_split_jd(jd_ut)
        || !out_position || !valid_diagnostic(diagnostic)) {
        return taiyin_c_internal::pack_call_result(taiyin_c_internal::invalid_argument());
    }
    uint32_t result_flags = 0u;
    taiyin::runtime::EphemerisEvalDiagnostic cpp_diagnostic;
    const taiyin::Status status = taiyin::runtime::calc_position_ut(
        &context->value, target_id, cpp_date(jd_ut), flags, out_position,
        diagnostic ? &cpp_diagnostic : 0,
        &result_flags);
    finish_diagnostic(cpp_diagnostic, diagnostic);
    return taiyin_c_internal::pack_call_result(status, result_flags);
}

taiyin_call_result TAIYIN_C_CALL taiyin_calc_position_ut_delta_t(
    const taiyin_context* context,
    int32_t target_id,
    const taiyin_split_julian_date* jd_ut1,
    double delta_t_seconds,
    uint32_t flags,
    double out_position[6],
    taiyin_ephemeris_diagnostic* diagnostic
) {
    if (!context || !taiyin_c_internal::valid_split_jd(jd_ut1)
        || !out_position || !valid_diagnostic(diagnostic)) {
        return taiyin_c_internal::pack_call_result(taiyin_c_internal::invalid_argument());
    }
    uint32_t result_flags = 0u;
    taiyin::runtime::EphemerisEvalDiagnostic cpp_diagnostic;
    const taiyin::Status status = taiyin::runtime::calc_position_ut_delta_t(
        &context->value, target_id, cpp_date(jd_ut1), delta_t_seconds, flags, out_position,
        diagnostic ? &cpp_diagnostic : 0,
        &result_flags);
    finish_diagnostic(cpp_diagnostic, diagnostic);
    return taiyin_c_internal::pack_call_result(status, result_flags);
}

taiyin_call_result TAIYIN_C_CALL taiyin_calc_position_utc(
    const taiyin_context* context,
    int32_t target_id,
    const taiyin_calendar_datetime* datetime_utc,
    uint32_t flags,
    double out_position[6],
    taiyin_ephemeris_diagnostic* diagnostic
) {
    if (!context || !taiyin_c_internal::valid_struct(datetime_utc)
        || !out_position || !valid_diagnostic(diagnostic)) {
        return taiyin_c_internal::pack_call_result(taiyin_c_internal::invalid_argument());
    }
    uint32_t result_flags = 0u;
    taiyin::runtime::EphemerisEvalDiagnostic cpp_diagnostic;
    const taiyin::CalendarDateTime cpp_datetime =
        taiyin_c_internal::to_cpp_datetime(*datetime_utc);
    const taiyin::Status status = taiyin::runtime::calc_position_utc(
        &context->value, target_id, cpp_datetime, flags, out_position,
        diagnostic ? &cpp_diagnostic : 0,
        &result_flags);
    finish_diagnostic(cpp_diagnostic, diagnostic);
    return taiyin_c_internal::pack_call_result(status, result_flags);
}

taiyin_call_result TAIYIN_C_CALL taiyin_calc_positions_ut(
    const taiyin_context* context,
    const int32_t* target_ids,
    size_t target_count,
    const taiyin_split_julian_date* jd_ut,
    uint32_t flags,
    double* out_positions,
    taiyin_ephemeris_diagnostic* diagnostics
) {
    if (!taiyin_c_internal::valid_split_jd(jd_ut)) {
        return taiyin_c_internal::pack_call_result(taiyin_c_internal::invalid_argument());
    }
    uint32_t result_flags = 0u;
    return taiyin_c_internal::pack_call_result(calc_positions_batch(
        context, target_ids, target_count, out_positions, diagnostics,
        [&](const int* cpp_ids,
            taiyin::runtime::EphemerisEvalDiagnostic* cpp_diagnostics) {
            return taiyin::runtime::calc_positions_ut(
                &context->value, cpp_ids, target_count, cpp_date(jd_ut), flags,
                out_positions, cpp_diagnostics,
                &result_flags);
        }), result_flags);
}

taiyin_call_result TAIYIN_C_CALL taiyin_calc_positions_tdb(
    const taiyin_context* context,
    const int32_t* target_ids,
    size_t target_count,
    const taiyin_split_julian_date* jd_tdb,
    const taiyin_split_julian_date* jd_tt,
    uint32_t flags,
    double* out_positions,
    taiyin_ephemeris_diagnostic* diagnostics
) {
    if (!taiyin_c_internal::valid_split_jd(jd_tdb)
        || !taiyin_c_internal::valid_optional_split_jd(jd_tt)) {
        return taiyin_c_internal::pack_call_result(taiyin_c_internal::invalid_argument());
    }
    const taiyin::SplitJulianDate cpp_jd_tdb = cpp_date(jd_tdb);
    const taiyin::SplitJulianDate cpp_jd_tt = jd_tt
        ? cpp_date(jd_tt)
        : cpp_jd_tdb;
    uint32_t result_flags = 0u;
    return taiyin_c_internal::pack_call_result(calc_positions_batch(
        context, target_ids, target_count, out_positions, diagnostics,
        [&](const int* cpp_ids,
            taiyin::runtime::EphemerisEvalDiagnostic* cpp_diagnostics) {
            return taiyin::runtime::calc_positions_tdb(
                &context->value, cpp_ids, target_count, cpp_jd_tdb, cpp_jd_tt, flags,
                out_positions, cpp_diagnostics,
                &result_flags);
        }), result_flags);
}

taiyin_call_result TAIYIN_C_CALL taiyin_calc_positions_tt(
    const taiyin_context* context,
    const int32_t* target_ids,
    size_t target_count,
    const taiyin_split_julian_date* jd_tt,
    uint32_t flags,
    double* out_positions,
    taiyin_ephemeris_diagnostic* diagnostics
) {
    if (!taiyin_c_internal::valid_split_jd(jd_tt)) {
        return taiyin_c_internal::pack_call_result(taiyin_c_internal::invalid_argument());
    }
    uint32_t result_flags = 0u;
    return taiyin_c_internal::pack_call_result(calc_positions_batch(
        context, target_ids, target_count, out_positions, diagnostics,
        [&](const int* cpp_ids,
            taiyin::runtime::EphemerisEvalDiagnostic* cpp_diagnostics) {
            return taiyin::runtime::calc_positions_tt(
                &context->value, cpp_ids, target_count, cpp_date(jd_tt), flags,
                out_positions, cpp_diagnostics,
                &result_flags);
        }), result_flags);
}

taiyin_call_result TAIYIN_C_CALL taiyin_calc_positions_ut_delta_t(
    const taiyin_context* context,
    const int32_t* target_ids,
    size_t target_count,
    const taiyin_split_julian_date* jd_ut1,
    double delta_t_seconds,
    uint32_t flags,
    double* out_positions,
    taiyin_ephemeris_diagnostic* diagnostics
) {
    if (!taiyin_c_internal::valid_split_jd(jd_ut1)) {
        return taiyin_c_internal::pack_call_result(taiyin_c_internal::invalid_argument());
    }
    uint32_t result_flags = 0u;
    return taiyin_c_internal::pack_call_result(calc_positions_batch(
        context, target_ids, target_count, out_positions, diagnostics,
        [&](const int* cpp_ids,
            taiyin::runtime::EphemerisEvalDiagnostic* cpp_diagnostics) {
            return taiyin::runtime::calc_positions_ut_delta_t(
                &context->value, cpp_ids, target_count, cpp_date(jd_ut1),
                delta_t_seconds, flags, out_positions, cpp_diagnostics,
                &result_flags);
        }), result_flags);
}

taiyin_call_result TAIYIN_C_CALL taiyin_calc_positions_utc(
    const taiyin_context* context,
    const int32_t* target_ids,
    size_t target_count,
    const taiyin_calendar_datetime* datetime_utc,
    uint32_t flags,
    double* out_positions,
    taiyin_ephemeris_diagnostic* diagnostics
) {
    if (!taiyin_c_internal::valid_struct(datetime_utc)) {
        return taiyin_c_internal::pack_call_result(taiyin_c_internal::invalid_argument());
    }
    const taiyin::CalendarDateTime cpp_datetime =
        taiyin_c_internal::to_cpp_datetime(*datetime_utc);
    uint32_t result_flags = 0u;
    return taiyin_c_internal::pack_call_result(calc_positions_batch(
        context, target_ids, target_count, out_positions, diagnostics,
        [&](const int* cpp_ids,
            taiyin::runtime::EphemerisEvalDiagnostic* cpp_diagnostics) {
            return taiyin::runtime::calc_positions_utc(
                &context->value, cpp_ids, target_count, cpp_datetime, flags,
                out_positions, cpp_diagnostics,
                &result_flags);
        }), result_flags);
}

taiyin_call_result TAIYIN_C_CALL taiyin_calc_state_tdb(
    const taiyin_context* context,
    int32_t target_id,
    const taiyin_split_julian_date* jd_tdb,
    const taiyin_split_julian_date* jd_tt,
    uint32_t flags,
    taiyin_cartesian_state* out_state,
    taiyin_ephemeris_diagnostic* diagnostic
) {
    if (!context || !taiyin_c_internal::valid_split_jd(jd_tdb)
        || !taiyin_c_internal::valid_optional_split_jd(jd_tt)
        || !taiyin_c_internal::valid_struct(out_state)
        || !valid_diagnostic(diagnostic)) {
        return taiyin_c_internal::pack_call_result(taiyin_c_internal::invalid_argument());
    }
    const taiyin::SplitJulianDate cpp_jd_tdb = cpp_date(jd_tdb);
    const taiyin::SplitJulianDate cpp_jd_tt = jd_tt
        ? cpp_date(jd_tt)
        : cpp_jd_tdb;
    taiyin::CartesianState cpp_state;
    uint32_t result_flags = 0u;
    taiyin::runtime::EphemerisEvalDiagnostic cpp_diagnostic;
    const taiyin::Status status = taiyin::runtime::calc_state_tdb(
        &context->value,
        target_id,
        cpp_jd_tdb,
        cpp_jd_tt,
        flags,
        &cpp_state,
        diagnostic ? &cpp_diagnostic : 0,
        &result_flags);
    if (status == taiyin::TAIYIN_STATUS_OK) {
        taiyin_c_internal::from_cpp_state(cpp_state, out_state);
    }
    finish_diagnostic(cpp_diagnostic, diagnostic);
    return taiyin_c_internal::pack_call_result(status, result_flags);
}

taiyin_call_result TAIYIN_C_CALL taiyin_calc_state_tt(
    const taiyin_context* context,
    int32_t target_id,
    const taiyin_split_julian_date* jd_tt,
    uint32_t flags,
    taiyin_cartesian_state* out_state,
    taiyin_ephemeris_diagnostic* diagnostic
) {
    if (!context || !taiyin_c_internal::valid_split_jd(jd_tt)
        || !taiyin_c_internal::valid_struct(out_state)
        || !valid_diagnostic(diagnostic)) {
        return taiyin_c_internal::pack_call_result(taiyin_c_internal::invalid_argument());
    }
    taiyin::CartesianState cpp_state;
    uint32_t result_flags = 0u;
    taiyin::runtime::EphemerisEvalDiagnostic cpp_diagnostic;
    const taiyin::Status status = taiyin::runtime::calc_state_tt(
        &context->value, target_id, cpp_date(jd_tt), flags, &cpp_state,
        diagnostic ? &cpp_diagnostic : 0,
        &result_flags);
    if (status == taiyin::TAIYIN_STATUS_OK) {
        taiyin_c_internal::from_cpp_state(cpp_state, out_state);
    }
    finish_diagnostic(cpp_diagnostic, diagnostic);
    return taiyin_c_internal::pack_call_result(status, result_flags);
}

taiyin_call_result TAIYIN_C_CALL taiyin_calc_state_ut(
    const taiyin_context* context,
    int32_t target_id,
    const taiyin_split_julian_date* jd_ut,
    uint32_t flags,
    taiyin_cartesian_state* out_state,
    taiyin_ephemeris_diagnostic* diagnostic
) {
    if (!context || !taiyin_c_internal::valid_split_jd(jd_ut)
        || !taiyin_c_internal::valid_struct(out_state)
        || !valid_diagnostic(diagnostic)) {
        return taiyin_c_internal::pack_call_result(taiyin_c_internal::invalid_argument());
    }
    taiyin::CartesianState cpp_state;
    uint32_t result_flags = 0u;
    taiyin::runtime::EphemerisEvalDiagnostic cpp_diagnostic;
    const taiyin::Status status = taiyin::runtime::calc_state_ut(
        &context->value, target_id, cpp_date(jd_ut), flags, &cpp_state,
        diagnostic ? &cpp_diagnostic : 0,
        &result_flags);
    if (status == taiyin::TAIYIN_STATUS_OK) {
        taiyin_c_internal::from_cpp_state(cpp_state, out_state);
    }
    finish_diagnostic(cpp_diagnostic, diagnostic);
    return taiyin_c_internal::pack_call_result(status, result_flags);
}

taiyin_call_result TAIYIN_C_CALL taiyin_calc_state_ut_delta_t(
    const taiyin_context* context,
    int32_t target_id,
    const taiyin_split_julian_date* jd_ut1,
    double delta_t_seconds,
    uint32_t flags,
    taiyin_cartesian_state* out_state,
    taiyin_ephemeris_diagnostic* diagnostic
) {
    if (!context || !taiyin_c_internal::valid_split_jd(jd_ut1)
        || !taiyin_c_internal::valid_struct(out_state)
        || !valid_diagnostic(diagnostic)) {
        return taiyin_c_internal::pack_call_result(taiyin_c_internal::invalid_argument());
    }
    taiyin::CartesianState cpp_state;
    uint32_t result_flags = 0u;
    taiyin::runtime::EphemerisEvalDiagnostic cpp_diagnostic;
    const taiyin::Status status = taiyin::runtime::calc_state_ut_delta_t(
        &context->value, target_id, cpp_date(jd_ut1), delta_t_seconds, flags, &cpp_state,
        diagnostic ? &cpp_diagnostic : 0,
        &result_flags);
    if (status == taiyin::TAIYIN_STATUS_OK) {
        taiyin_c_internal::from_cpp_state(cpp_state, out_state);
    }
    finish_diagnostic(cpp_diagnostic, diagnostic);
    return taiyin_c_internal::pack_call_result(status, result_flags);
}

taiyin_call_result TAIYIN_C_CALL taiyin_calc_state_utc(
    const taiyin_context* context,
    int32_t target_id,
    const taiyin_calendar_datetime* datetime_utc,
    uint32_t flags,
    taiyin_cartesian_state* out_state,
    taiyin_ephemeris_diagnostic* diagnostic
) {
    if (!context || !taiyin_c_internal::valid_struct(datetime_utc)
        || !taiyin_c_internal::valid_struct(out_state)
        || !valid_diagnostic(diagnostic)) {
        return taiyin_c_internal::pack_call_result(taiyin_c_internal::invalid_argument());
    }
    taiyin::CartesianState cpp_state;
    uint32_t result_flags = 0u;
    taiyin::runtime::EphemerisEvalDiagnostic cpp_diagnostic;
    const taiyin::CalendarDateTime cpp_datetime =
        taiyin_c_internal::to_cpp_datetime(*datetime_utc);
    const taiyin::Status status = taiyin::runtime::calc_state_utc(
        &context->value, target_id, cpp_datetime, flags, &cpp_state,
        diagnostic ? &cpp_diagnostic : 0,
        &result_flags);
    if (status == taiyin::TAIYIN_STATUS_OK) {
        taiyin_c_internal::from_cpp_state(cpp_state, out_state);
    }
    finish_diagnostic(cpp_diagnostic, diagnostic);
    return taiyin_c_internal::pack_call_result(status, result_flags);
}

}  // extern "C"
