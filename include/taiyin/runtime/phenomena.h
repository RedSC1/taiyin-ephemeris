#ifndef TAIYIN_RUNTIME_PHENOMENA_H
#define TAIYIN_RUNTIME_PHENOMENA_H

#include "taiyin/runtime/native_context.h"
#include "taiyin/status.h"

#include <stdint.h>

namespace taiyin {
namespace runtime {

struct BodyPhenomena {
    double phase_angle_rad;
    double illuminated_fraction;
    double solar_elongation_rad;
    double apparent_diameter_rad;
    double apparent_magnitude;
    double horizontal_parallax_rad;

    BodyPhenomena() noexcept;
};

Status calc_body_phenomena_tt(
    const NativeCalcContext* context,
    int body_id,
    const SplitJulianDate& jd_tt,
    uint64_t flags,
    BodyPhenomena* out,
    EphemerisEvalDiagnostic* diagnostic = nullptr
) noexcept;

Status calc_body_phenomena_ut(
    const NativeCalcContext* context,
    int body_id,
    const SplitJulianDate& jd_ut,
    uint64_t flags,
    BodyPhenomena* out,
    EphemerisEvalDiagnostic* diagnostic = nullptr
) noexcept;

}  // namespace runtime
}  // namespace taiyin

#endif  // TAIYIN_RUNTIME_PHENOMENA_H
