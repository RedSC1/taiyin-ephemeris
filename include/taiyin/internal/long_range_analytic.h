#ifndef TAIYIN_INTERNAL_LONG_RANGE_ANALYTIC_H
#define TAIYIN_INTERNAL_LONG_RANGE_ANALYTIC_H

#include "ephemeris_block.h"

#include <cstdint>

namespace taiyin {
namespace internal {

const char* builtin_long_range_analytic_source_revision() noexcept;
const char* builtin_long_range_analytic_coefficients_sha256() noexcept;

bool get_builtin_long_range_analytic_coverage(
    int target_id,
    int center_id,
    double* out_jd_tdb_start,
    double* out_jd_tdb_end
) noexcept;

bool compile_builtin_long_range_analytic_ephemeris_block(
    int target_id,
    int center_id,
    double jd_tdb_start,
    double jd_tdb_end,
    StorageEphemerisBlock* out
) noexcept;

bool eval_builtin_long_range_analytic_state(
    int target_id,
    int center_id,
    const SplitJulianDate& jd_tdb,
    CartesianState* out
) noexcept;

bool eval_builtin_long_range_pluto_near_state(
    const SplitJulianDate& jd_tdb,
    CartesianState* out
) noexcept;

bool eval_builtin_long_range_pluto_fallback_state(
    const SplitJulianDate& jd_tdb,
    CartesianState* out
) noexcept;

}  // namespace internal
}  // namespace taiyin

#endif  // TAIYIN_INTERNAL_LONG_RANGE_ANALYTIC_H
