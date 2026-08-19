#include "taiyin/c/star.h"

#include "c_api_internal.h"
#include "taiyin/runtime/star_position.h"

#include <new>
#include <vector>

namespace {

bool valid_diagnostic(taiyin_ephemeris_diagnostic* diagnostic) noexcept {
    return !diagnostic || taiyin_c_internal::valid_struct(diagnostic);
}

void finish_diagnostic(
    const taiyin::runtime::EphemerisEvalDiagnostic& source,
    taiyin_ephemeris_diagnostic* destination
) noexcept {
    taiyin_c_internal::from_cpp_diagnostic(source, destination);
}

template <typename Eval>
taiyin_call_result calc_star_positions(
    const taiyin_context* context,
    const char* const* star_keys,
    size_t star_count,
    double* out_positions,
    taiyin_ephemeris_diagnostic* diagnostics,
    const Eval& eval
) {
    if (!context || (!star_keys && star_count != 0)
        || (!out_positions && star_count != 0)) {
        return taiyin_c_internal::pack_call_result(
            taiyin_c_internal::invalid_argument());
    }
    for (size_t i = 0; i < star_count; ++i) {
        if (!star_keys[i] || star_keys[i][0] == '\0') {
            return taiyin_c_internal::pack_call_result(
                taiyin_c_internal::invalid_argument());
        }
    }
    for (size_t i = 0; diagnostics && i < star_count; ++i) {
        if (!taiyin_c_internal::valid_struct(&diagnostics[i])) {
            return taiyin_c_internal::pack_call_result(
                taiyin_c_internal::invalid_argument());
        }
    }
    taiyin_c_internal::TrackedCalcContext tracked(context->value);
    try {
        std::vector<taiyin::runtime::EphemerisEvalDiagnostic> cpp_diagnostics(
            diagnostics ? star_count : 0);
        const taiyin::Status status = eval(
            &tracked.value,
            cpp_diagnostics.empty() ? nullptr : cpp_diagnostics.data());
        for (size_t i = 0; diagnostics && i < star_count; ++i) {
            finish_diagnostic(cpp_diagnostics[i], &diagnostics[i]);
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

taiyin_call_result TAIYIN_C_CALL taiyin_star_catalog_add_tsc1(const char* path) {
    if (!path || path[0] == '\0') return taiyin_c_internal::pack_call_result(taiyin_c_internal::invalid_argument());
    return taiyin_c_internal::pack_call_result(taiyin::runtime::add_global_tsc1_star_catalog(path));
}

taiyin_call_result TAIYIN_C_CALL taiyin_star_catalog_add_tsc1_memory(
    const uint8_t* data,
    size_t size
) {
    if (!data || size == 0) return taiyin_c_internal::pack_call_result(taiyin_c_internal::invalid_argument());
    return taiyin_c_internal::pack_call_result(taiyin::runtime::add_global_tsc1_star_catalog_from_memory(data, size));
}

taiyin_call_result TAIYIN_C_CALL taiyin_star_catalog_add_tsf1(const char* path) {
    if (!path || path[0] == '\0') return taiyin_c_internal::pack_call_result(taiyin_c_internal::invalid_argument());
    return taiyin_c_internal::pack_call_result(taiyin::runtime::add_global_tsf1_star_catalog(path));
}

void TAIYIN_C_CALL taiyin_star_catalog_clear(void) {
    taiyin::runtime::clear_global_star_catalogs();
}

size_t TAIYIN_C_CALL taiyin_star_catalog_count(void) {
    return taiyin::runtime::global_star_catalog_count();
}

taiyin_call_result TAIYIN_C_CALL taiyin_star_find_magnitude(
    const char* star_key,
    double* out_magnitude
) {
    if (!star_key || star_key[0] == '\0' || !out_magnitude) {
        return taiyin_c_internal::pack_call_result(taiyin_c_internal::invalid_argument());
    }
    return taiyin_c_internal::pack_call_result(taiyin::runtime::find_global_star_magnitude(star_key, out_magnitude));
}

taiyin_call_result TAIYIN_C_CALL taiyin_calc_star_position_tdb(
    const taiyin_context* context,
    const char* star_key,
    const taiyin_split_julian_date* jd_tdb,
    const taiyin_split_julian_date* jd_tt,
    uint64_t flags,
    double out_position[6],
    taiyin_ephemeris_diagnostic* diagnostic
) {
    if (!context || !star_key || star_key[0] == '\0'
        || !taiyin_c_internal::valid_split_jd(jd_tdb)
        || !taiyin_c_internal::valid_optional_split_jd(jd_tt)
        || !out_position
        || !valid_diagnostic(diagnostic)) {
        return taiyin_c_internal::pack_call_result(taiyin_c_internal::invalid_argument());
    }
    const taiyin::SplitJulianDate cpp_jd_tdb =
        taiyin_c_internal::to_cpp_split_jd(*jd_tdb);
    const taiyin::SplitJulianDate cpp_jd_tt = jd_tt
        ? taiyin_c_internal::to_cpp_split_jd(*jd_tt)
        : cpp_jd_tdb;
    taiyin::runtime::EphemerisEvalDiagnostic cpp_diagnostic;
    taiyin_c_internal::TrackedCalcContext tracked(context->value);
    const taiyin::Status status = taiyin::runtime::calc_star_position_tdb(
        &tracked.value, star_key, cpp_jd_tdb, cpp_jd_tt, flags, out_position,
        diagnostic ? &cpp_diagnostic : 0);
    finish_diagnostic(cpp_diagnostic, diagnostic);
    return taiyin_c_internal::pack_call_result(status, tracked.flags);
}

taiyin_call_result TAIYIN_C_CALL taiyin_calc_star_position_tt(
    const taiyin_context* context,
    const char* star_key,
    const taiyin_split_julian_date* jd_tt,
    uint64_t flags,
    double out_position[6],
    taiyin_ephemeris_diagnostic* diagnostic
) {
    if (!context || !star_key || star_key[0] == '\0'
        || !taiyin_c_internal::valid_split_jd(jd_tt)
        || !out_position
        || !valid_diagnostic(diagnostic)) {
        return taiyin_c_internal::pack_call_result(taiyin_c_internal::invalid_argument());
    }
    taiyin::runtime::EphemerisEvalDiagnostic cpp_diagnostic;
    taiyin_c_internal::TrackedCalcContext tracked(context->value);
    const taiyin::Status status = taiyin::runtime::calc_star_position_tt(
        &tracked.value, star_key,
        taiyin_c_internal::to_cpp_split_jd(*jd_tt), flags, out_position,
        diagnostic ? &cpp_diagnostic : 0);
    finish_diagnostic(cpp_diagnostic, diagnostic);
    return taiyin_c_internal::pack_call_result(status, tracked.flags);
}

taiyin_call_result TAIYIN_C_CALL taiyin_calc_star_position_ut(
    const taiyin_context* context,
    const char* star_key,
    const taiyin_split_julian_date* jd_ut,
    uint64_t flags,
    double out_position[6],
    taiyin_ephemeris_diagnostic* diagnostic
) {
    if (!context || !star_key || star_key[0] == '\0'
        || !taiyin_c_internal::valid_split_jd(jd_ut)
        || !out_position
        || !valid_diagnostic(diagnostic)) {
        return taiyin_c_internal::pack_call_result(taiyin_c_internal::invalid_argument());
    }
    taiyin::runtime::EphemerisEvalDiagnostic cpp_diagnostic;
    taiyin_c_internal::TrackedCalcContext tracked(context->value);
    const taiyin::Status status = taiyin::runtime::calc_star_position_ut(
        &tracked.value, star_key,
        taiyin_c_internal::to_cpp_split_jd(*jd_ut), flags, out_position,
        diagnostic ? &cpp_diagnostic : 0);
    finish_diagnostic(cpp_diagnostic, diagnostic);
    return taiyin_c_internal::pack_call_result(status, tracked.flags);
}

taiyin_call_result TAIYIN_C_CALL taiyin_calc_star_position_ut_delta_t(
    const taiyin_context* context,
    const char* star_key,
    const taiyin_split_julian_date* jd_ut1,
    double delta_t_seconds,
    uint64_t flags,
    double out_position[6],
    taiyin_ephemeris_diagnostic* diagnostic
) {
    if (!context || !star_key || star_key[0] == '\0'
        || !taiyin_c_internal::valid_split_jd(jd_ut1)
        || !out_position
        || !valid_diagnostic(diagnostic)) {
        return taiyin_c_internal::pack_call_result(taiyin_c_internal::invalid_argument());
    }
    taiyin::runtime::EphemerisEvalDiagnostic cpp_diagnostic;
    taiyin_c_internal::TrackedCalcContext tracked(context->value);
    const taiyin::Status status = taiyin::runtime::calc_star_position_ut_delta_t(
        &tracked.value, star_key,
        taiyin_c_internal::to_cpp_split_jd(*jd_ut1), delta_t_seconds,
        flags, out_position,
        diagnostic ? &cpp_diagnostic : 0);
    finish_diagnostic(cpp_diagnostic, diagnostic);
    return taiyin_c_internal::pack_call_result(status, tracked.flags);
}

taiyin_call_result TAIYIN_C_CALL taiyin_calc_star_positions_tdb(
    const taiyin_context* context,
    const char* const* star_keys,
    size_t star_count,
    const taiyin_split_julian_date* jd_tdb,
    const taiyin_split_julian_date* jd_tt,
    uint64_t flags,
    double* out_positions,
    taiyin_ephemeris_diagnostic* diagnostics
) {
    if (!taiyin_c_internal::valid_split_jd(jd_tdb)
        || !taiyin_c_internal::valid_optional_split_jd(jd_tt)) {
        return taiyin_c_internal::pack_call_result(taiyin_c_internal::invalid_argument());
    }
    const taiyin::SplitJulianDate cpp_jd_tdb =
        taiyin_c_internal::to_cpp_split_jd(*jd_tdb);
    const taiyin::SplitJulianDate cpp_jd_tt = jd_tt
        ? taiyin_c_internal::to_cpp_split_jd(*jd_tt)
        : cpp_jd_tdb;
    return calc_star_positions(
        context, star_keys, star_count, out_positions, diagnostics,
        [&](const taiyin::runtime::NativeCalcContext* calc,
            taiyin::runtime::EphemerisEvalDiagnostic* cpp_diagnostics) {
            return taiyin::runtime::calc_star_positions_tdb(
                calc, star_keys, star_count,
                cpp_jd_tdb, cpp_jd_tt, flags,
                out_positions, cpp_diagnostics);
        });
}

taiyin_call_result TAIYIN_C_CALL taiyin_calc_star_positions_tt(
    const taiyin_context* context,
    const char* const* star_keys,
    size_t star_count,
    const taiyin_split_julian_date* jd_tt,
    uint64_t flags,
    double* out_positions,
    taiyin_ephemeris_diagnostic* diagnostics
) {
    if (!taiyin_c_internal::valid_split_jd(jd_tt)) {
        return taiyin_c_internal::pack_call_result(taiyin_c_internal::invalid_argument());
    }
    return calc_star_positions(
        context, star_keys, star_count, out_positions, diagnostics,
        [&](const taiyin::runtime::NativeCalcContext* calc,
            taiyin::runtime::EphemerisEvalDiagnostic* cpp_diagnostics) {
            return taiyin::runtime::calc_star_positions_tt(
                calc, star_keys, star_count,
                taiyin_c_internal::to_cpp_split_jd(*jd_tt), flags,
                out_positions, cpp_diagnostics);
        });
}

taiyin_call_result TAIYIN_C_CALL taiyin_calc_star_positions_ut(
    const taiyin_context* context,
    const char* const* star_keys,
    size_t star_count,
    const taiyin_split_julian_date* jd_ut,
    uint64_t flags,
    double* out_positions,
    taiyin_ephemeris_diagnostic* diagnostics
) {
    if (!taiyin_c_internal::valid_split_jd(jd_ut)) {
        return taiyin_c_internal::pack_call_result(taiyin_c_internal::invalid_argument());
    }
    return calc_star_positions(
        context, star_keys, star_count, out_positions, diagnostics,
        [&](const taiyin::runtime::NativeCalcContext* calc,
            taiyin::runtime::EphemerisEvalDiagnostic* cpp_diagnostics) {
            return taiyin::runtime::calc_star_positions_ut(
                calc, star_keys, star_count,
                taiyin_c_internal::to_cpp_split_jd(*jd_ut), flags,
                out_positions, cpp_diagnostics);
        });
}

taiyin_call_result TAIYIN_C_CALL taiyin_calc_star_positions_ut_delta_t(
    const taiyin_context* context,
    const char* const* star_keys,
    size_t star_count,
    const taiyin_split_julian_date* jd_ut1,
    double delta_t_seconds,
    uint64_t flags,
    double* out_positions,
    taiyin_ephemeris_diagnostic* diagnostics
) {
    if (!taiyin_c_internal::valid_split_jd(jd_ut1)) {
        return taiyin_c_internal::pack_call_result(taiyin_c_internal::invalid_argument());
    }
    return calc_star_positions(
        context, star_keys, star_count, out_positions, diagnostics,
        [&](const taiyin::runtime::NativeCalcContext* calc,
            taiyin::runtime::EphemerisEvalDiagnostic* cpp_diagnostics) {
            return taiyin::runtime::calc_star_positions_ut_delta_t(
                calc, star_keys, star_count,
                taiyin_c_internal::to_cpp_split_jd(*jd_ut1),
                delta_t_seconds, flags, out_positions, cpp_diagnostics);
        });
}

taiyin_call_result TAIYIN_C_CALL taiyin_calc_observed_star_ut(
    const taiyin_context* context,
    const char* star_key,
    const taiyin_split_julian_date* jd_ut,
    uint64_t flags,
    taiyin_observed_position* out_position,
    taiyin_ephemeris_diagnostic* diagnostic
) {
    if (!context || !star_key || star_key[0] == '\0'
        || !taiyin_c_internal::valid_split_jd(jd_ut)
        || !taiyin_c_internal::valid_struct(out_position)
        || !valid_diagnostic(diagnostic)) {
        return taiyin_c_internal::pack_call_result(taiyin_c_internal::invalid_argument());
    }
    taiyin::runtime::ObservedPosition cpp_out;
    taiyin::runtime::EphemerisEvalDiagnostic cpp_diagnostic;
    taiyin_c_internal::TrackedCalcContext tracked(context->value);
    const taiyin::Status status = taiyin::runtime::calc_observed_star_ut(
        &tracked.value, star_key,
        taiyin_c_internal::to_cpp_split_jd(*jd_ut), flags, &cpp_out,
        diagnostic ? &cpp_diagnostic : 0);
    if (status == taiyin::TAIYIN_STATUS_OK) {
        taiyin_c_internal::from_cpp_observed(cpp_out, out_position);
    }
    finish_diagnostic(cpp_diagnostic, diagnostic);
    return taiyin_c_internal::pack_call_result(status, tracked.flags);
}

taiyin_call_result TAIYIN_C_CALL taiyin_calc_observed_stars_ut(
    const taiyin_context* context,
    const char* const* star_keys,
    size_t star_count,
    const taiyin_split_julian_date* jd_ut,
    uint64_t flags,
    taiyin_observed_position* out_positions,
    taiyin_ephemeris_diagnostic* diagnostics
) {
    if (!context || !taiyin_c_internal::valid_split_jd(jd_ut)
        || (!star_keys && star_count != 0)
        || (!out_positions && star_count != 0)) {
        return taiyin_c_internal::pack_call_result(taiyin_c_internal::invalid_argument());
    }
    for (size_t i = 0; i < star_count; ++i) {
        if (!star_keys[i] || star_keys[i][0] == '\0'
            || !taiyin_c_internal::valid_struct(&out_positions[i])
            || (diagnostics
                && !taiyin_c_internal::valid_struct(&diagnostics[i]))) {
            return taiyin_c_internal::pack_call_result(taiyin_c_internal::invalid_argument());
        }
    }
    taiyin_c_internal::TrackedCalcContext tracked(context->value);
    try {
        std::vector<taiyin::runtime::ObservedPosition> cpp_out(star_count);
        std::vector<taiyin::runtime::EphemerisEvalDiagnostic> cpp_diagnostics(
            diagnostics ? star_count : 0);
        const taiyin::Status status = taiyin::runtime::calc_observed_stars_ut(
            &tracked.value, star_keys, star_count,
            taiyin_c_internal::to_cpp_split_jd(*jd_ut), flags,
            cpp_out.empty() ? 0 : cpp_out.data(),
            cpp_diagnostics.empty() ? 0 : cpp_diagnostics.data());
        for (size_t i = 0; i < star_count; ++i) {
            if (status == taiyin::TAIYIN_STATUS_OK) {
                taiyin_c_internal::from_cpp_observed(
                    cpp_out[i], &out_positions[i]);
            }
            if (diagnostics) {
                finish_diagnostic(cpp_diagnostics[i], &diagnostics[i]);
            }
        }
        return taiyin_c_internal::pack_call_result(status, tracked.flags);
    } catch (const std::bad_alloc&) {
        return taiyin_c_internal::pack_call_result(taiyin::TAIYIN_ERROR_OUT_OF_MEMORY, tracked.flags);
    } catch (...) {
        return taiyin_c_internal::pack_call_result(taiyin::TAIYIN_ERROR_INTERNAL, tracked.flags);
    }
}

}  // extern "C"
