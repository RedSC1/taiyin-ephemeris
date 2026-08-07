#ifndef TAIYIN_RUNTIME_OBSERVED_POSITION_H
#define TAIYIN_RUNTIME_OBSERVED_POSITION_H

#include "taiyin/observer.h"
#include "taiyin/runtime/major_body_apparent.h"
#include "taiyin/runtime/native_context.h"
#include "taiyin/status.h"

#include <stddef.h>
#include <stdint.h>

namespace taiyin {
namespace runtime {

const uint32_t TAIYIN_OBSERVED_SPEED = 1u << 0;
const uint32_t TAIYIN_OBSERVED_TOPOCENTRIC = 1u << 1;
const uint32_t TAIYIN_OBSERVED_HORIZONTAL = 1u << 2;
const uint32_t TAIYIN_OBSERVED_REFRACTION = 1u << 3;
const uint32_t TAIYIN_OBSERVED_TRUEPOS = 1u << 4;
const uint32_t TAIYIN_OBSERVED_ASTROMETRIC = 1u << 5;
const uint32_t TAIYIN_OBSERVED_NO_ABERR = 1u << 6;
const uint32_t TAIYIN_OBSERVED_NO_GDEFL = 1u << 7;
constexpr uint64_t TAIYIN_OBSERVED_POSITION_FLAGS_MASK = 0x00000000ffffffffull;
constexpr uint64_t TAIYIN_OBSERVED_OPTION_FLAGS_MASK = 0xffffffff00000000ull;
constexpr uint64_t TAIYIN_OBSERVED_STRICT_METEOROLOGY = 1ull << 32;

struct ObservedPosition {
    int body_id;
    Status status;
    EphemerisEvalDiagnostic diagnostic;
    MajorBodyApparentPosition apparent;
    HorizontalCoordinates horizontal;
    HorizontalRates horizontal_rates;
    HorizontalCoordinates refracted_horizontal;
    HorizontalRates refracted_horizontal_rates;

    ObservedPosition() noexcept;
};

Status calc_observed_ut(
    const NativeCalcContext* context,
    const SplitJulianDate& jd_ut,
    const int* body_ids,
    size_t body_count,
    uint64_t flags,
    ObservedPosition* out,
    EphemerisEvalDiagnostic* diagnostics
) noexcept;

Status calc_observed_utc(
    const NativeCalcContext* context,
    const CalendarDateTime& datetime_utc,
    const int* body_ids,
    size_t body_count,
    uint64_t flags,
    ObservedPosition* out,
    EphemerisEvalDiagnostic* diagnostics
) noexcept;

}  // namespace runtime
}  // namespace taiyin

#endif  // TAIYIN_RUNTIME_OBSERVED_POSITION_H
