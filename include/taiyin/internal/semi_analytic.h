#ifndef TAIYIN_INTERNAL_SEMI_ANALYTIC_H
#define TAIYIN_INTERNAL_SEMI_ANALYTIC_H

#include "ephemeris_block.h"

#include <cstdint>

namespace taiyin {
namespace internal {

constexpr int SEMI_ANALYTIC_METHOD_ID = 4002;
constexpr uint64_t SEMI_ANALYTIC_SOURCE_ID = 8;
constexpr uint32_t SEMI_ANALYTIC_SOURCE_GENERATION = 1;
constexpr uint32_t SEMI_ANALYTIC_SOURCE_PURPOSE = 0;

const char* builtin_semi_analytic_source_revision() noexcept;
const char* builtin_semi_analytic_coefficients_sha256() noexcept;

bool get_builtin_semi_analytic_coverage(
    int target_id,
    int center_id,
    double* out_jd_tdb_start,
    double* out_jd_tdb_end
) noexcept;

bool compile_builtin_semi_analytic_ephemeris_block(
    int target_id,
    int center_id,
    double jd_tdb_start,
    double jd_tdb_end,
    StorageEphemerisBlock* out
) noexcept;

}  // namespace internal
}  // namespace taiyin

#endif  // TAIYIN_INTERNAL_SEMI_ANALYTIC_H
