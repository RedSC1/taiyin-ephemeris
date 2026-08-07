#ifndef TAIYIN_RUNTIME_SOLAR_ECLIPSE_BESSELIAN_SOLVER_H
#define TAIYIN_RUNTIME_SOLAR_ECLIPSE_BESSELIAN_SOLVER_H

#include <cstdint>

#include "taiyin/runtime/eclipse_search.h"

namespace taiyin {
namespace runtime {

Status solve_solar_eclipse_besselian_for_meeus_k(
    const NativeCalcContext* context,
    int k,
    uint64_t flags,
    bool fill_location,
    bool refine_central_kind,
    SolarEclipseResult* out,
    EphemerisEvalDiagnostic* diagnostic
) noexcept;

Status solve_solar_eclipse_besselian_lite_for_meeus_k(
    const NativeCalcContext* context,
    int k,
    uint64_t flags,
    bool fill_location,
    SolarEclipseResult* out,
    bool* out_uncertain,
    EphemerisEvalDiagnostic* diagnostic
) noexcept;

Status solve_solar_eclipse_besselian_search_for_meeus_k(
    const NativeCalcContext* context,
    int k,
    uint64_t flags,
    bool fill_location,
    bool refine_central_kind,
    SolarEclipseResult* out,
    EphemerisEvalDiagnostic* diagnostic
) noexcept;

}  // namespace runtime
}  // namespace taiyin

#endif  // TAIYIN_RUNTIME_SOLAR_ECLIPSE_BESSELIAN_SOLVER_H
