#ifndef TAIYIN_RUNTIME_STAR_POSITION_H
#define TAIYIN_RUNTIME_STAR_POSITION_H

#include "taiyin/runtime/native_position.h"
#include "taiyin/runtime/observed_position.h"
#include "taiyin/status.h"

#include <stddef.h>
#include <stdint.h>

namespace taiyin {
namespace runtime {

Status add_global_tsc1_star_catalog(const char* path) noexcept;
Status add_global_tsc1_star_catalog_from_memory(const uint8_t* data, size_t size) noexcept;
Status add_global_tsf1_star_catalog(const char* path) noexcept;
void clear_global_star_catalogs() noexcept;
size_t global_star_catalog_count() noexcept;
Status find_global_star_magnitude(const char* star_key, double* out_magnitude) noexcept;

// Fixed-star position and observation are Earth-observer-only in the 1.0 API.
// Calls return TAIYIN_ERROR_UNSUPPORTED for a non-Earth context observer.
Status calc_star_position_tdb(
    const NativeCalcContext* context,
    const char* star_key,
    const SplitJulianDate& jd_tdb,
    const SplitJulianDate& jd_tt,
    uint64_t flags,
    double out[6],
    EphemerisEvalDiagnostic* diagnostic
) noexcept;

Status calc_star_position_tt(
    const NativeCalcContext* context,
    const char* star_key,
    const SplitJulianDate& jd_tt,
    uint64_t flags,
    double out[6],
    EphemerisEvalDiagnostic* diagnostic
) noexcept;

Status calc_star_position_ut_delta_t(
    const NativeCalcContext* context,
    const char* star_key,
    const SplitJulianDate& jd_ut1,
    double delta_t_seconds,
    uint64_t flags,
    double out[6],
    EphemerisEvalDiagnostic* diagnostic
) noexcept;

Status calc_star_position_ut(
    const NativeCalcContext* context,
    const char* star_key,
    const SplitJulianDate& jd_ut,
    uint64_t flags,
    double out[6],
    EphemerisEvalDiagnostic* diagnostic
) noexcept;

Status calc_star_positions_tdb(
    const NativeCalcContext* context,
    const char* const* star_keys,
    size_t star_count,
    const SplitJulianDate& jd_tdb,
    const SplitJulianDate& jd_tt,
    uint64_t flags,
    double* out,
    EphemerisEvalDiagnostic* diagnostics
) noexcept;

Status calc_star_positions_tt(
    const NativeCalcContext* context,
    const char* const* star_keys,
    size_t star_count,
    const SplitJulianDate& jd_tt,
    uint64_t flags,
    double* out,
    EphemerisEvalDiagnostic* diagnostics
) noexcept;

Status calc_star_positions_ut_delta_t(
    const NativeCalcContext* context,
    const char* const* star_keys,
    size_t star_count,
    const SplitJulianDate& jd_ut1,
    double delta_t_seconds,
    uint64_t flags,
    double* out,
    EphemerisEvalDiagnostic* diagnostics
) noexcept;

Status calc_star_positions_ut(
    const NativeCalcContext* context,
    const char* const* star_keys,
    size_t star_count,
    const SplitJulianDate& jd_ut,
    uint64_t flags,
    double* out,
    EphemerisEvalDiagnostic* diagnostics
) noexcept;

Status calc_observed_star_ut(
    const NativeCalcContext* context,
    const char* star_key,
    const SplitJulianDate& jd_ut,
    uint64_t flags,
    ObservedPosition* out,
    EphemerisEvalDiagnostic* diagnostic
) noexcept;

Status calc_observed_stars_ut(
    const NativeCalcContext* context,
    const char* const* star_keys,
    size_t star_count,
    const SplitJulianDate& jd_ut,
    uint64_t flags,
    ObservedPosition* out,
    EphemerisEvalDiagnostic* diagnostics
) noexcept;

}  // namespace runtime
}  // namespace taiyin

#endif  // TAIYIN_RUNTIME_STAR_POSITION_H
