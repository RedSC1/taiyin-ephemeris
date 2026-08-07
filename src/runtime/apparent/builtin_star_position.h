#ifndef TAIYIN_RUNTIME_BUILTIN_STAR_POSITION_H
#define TAIYIN_RUNTIME_BUILTIN_STAR_POSITION_H

#include "taiyin/runtime/native_context.h"
#include "taiyin/status.h"
#include "taiyin/time.h"

#include <stdint.h>

namespace taiyin {
namespace runtime {

// ICRF/J2000 astrometry for a small number of runtime-owned reference stars.
// Proper motion in right ascension is mu_alpha*cos(delta), in mas/year.
struct BuiltinStarAstrometry {
    double ra_j2000_rad;
    double dec_j2000_rad;
    double pm_ra_mas_per_year;
    double pm_dec_mas_per_year;
    double parallax_mas;
    double radial_velocity_km_per_second;
    double reference_jd_tdb;
};

Status calc_builtin_star_position_tt(
    const NativeCalcContext* context,
    const BuiltinStarAstrometry& astrometry,
    const SplitJulianDate& jd_tt,
    uint64_t flags,
    double out[6],
    EphemerisEvalDiagnostic* diagnostic
) noexcept;

}  // namespace runtime
}  // namespace taiyin

#endif  // TAIYIN_RUNTIME_BUILTIN_STAR_POSITION_H
