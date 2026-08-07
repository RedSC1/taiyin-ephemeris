#ifndef TAIYIN_ASTROLOGY_SIDEREAL_INTERNAL_H
#define TAIYIN_ASTROLOGY_SIDEREAL_INTERNAL_H

#include "taiyin/astrology/sidereal.h"

namespace taiyin {
namespace astrology {
namespace internal {

uint32_t ayanamsha_evaluation_flags(uint32_t native_position_flags) noexcept;

uint64_t ayanamsha_context_flags(uint64_t sidereal_flags) noexcept;

Status effective_native_context(
    const runtime::NativeCalcContext* native_context,
    int ayanamsha_id,
    uint64_t sidereal_flags,
    runtime::NativeCalcContext* out
) noexcept;

Status calc_ayanamsha_tt_with_position_flags(
    const runtime::NativeCalcContext* native_context,
    int ayanamsha_id,
    SplitJulianDate jd_tt,
    uint64_t native_position_flags,
    uint64_t sidereal_flags,
    double* out_ayanamsha_rad
) noexcept;

Status calc_longitude_nutation_tt(
    const runtime::NativeCalcContext* native_context,
    SplitJulianDate jd_tt,
    uint64_t native_position_flags,
    double* out_dpsi_rad
) noexcept;

}  // namespace internal
}  // namespace astrology
}  // namespace taiyin

#endif  // TAIYIN_ASTROLOGY_SIDEREAL_INTERNAL_H
