#ifndef TAIYIN_RUNTIME_SOLAR_ECLIPSE_DIRECT_SOLVER_H
#define TAIYIN_RUNTIME_SOLAR_ECLIPSE_DIRECT_SOLVER_H

#include <cstdint>

#include "taiyin/runtime/eclipse_search.h"

namespace taiyin {
namespace runtime {

// Global event solver based on instantaneous Sun/Moon vectors and direct
// shadow-cone/ellipsoid geometry. It does not construct a Besselian polynomial;
// those remain a route and bulk-local sampling acceleration layer.
Status solve_solar_eclipse_direct_for_meeus_k(
    const NativeCalcContext* context,
    int k,
    uint64_t flags,
    uint32_t kind_filter,
    bool fill_location,
    bool refine_central_kind,
    SolarEclipseResult* out,
    EphemerisEvalDiagnostic* diagnostic
) noexcept;

}  // namespace runtime
}  // namespace taiyin

#endif  // TAIYIN_RUNTIME_SOLAR_ECLIPSE_DIRECT_SOLVER_H
