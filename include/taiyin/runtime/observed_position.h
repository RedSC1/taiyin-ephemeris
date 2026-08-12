#ifndef TAIYIN_RUNTIME_OBSERVED_POSITION_H
#define TAIYIN_RUNTIME_OBSERVED_POSITION_H

#include "taiyin/observer.h"
#include "taiyin/runtime/major_body_apparent.h"
#include "taiyin/runtime/native_context.h"
#include "taiyin/runtime/native_position.h"
#include "taiyin/status.h"

#include <stddef.h>
#include <stdint.h>

namespace taiyin {
namespace runtime {

// Observed calculation flags reuse the native-position bits whose semantics
// survive an apparent/horizontal result. Representation selectors such as XYZ,
// EQUATORIAL, RADIANS, and NONUT are not accepted: ObservedPosition always
// contains both Cartesian and spherical data in its documented frame.
const uint32_t TAIYIN_OBSERVED_SPEED = TAIYIN_NATIVE_POSITION_SPEED;
const uint32_t TAIYIN_OBSERVED_TRUEPOS = TAIYIN_NATIVE_POSITION_TRUEPOS;
const uint32_t TAIYIN_OBSERVED_NO_ABERR = TAIYIN_NATIVE_POSITION_NO_ABERR;
const uint32_t TAIYIN_OBSERVED_NO_GDEFL = TAIYIN_NATIVE_POSITION_NO_GDEFL;
const uint32_t TAIYIN_OBSERVED_ASTROMETRIC = TAIYIN_NATIVE_POSITION_ASTROMETRIC;
const uint32_t TAIYIN_OBSERVED_TOPOCENTRIC = TAIYIN_NATIVE_POSITION_TOPOCENTRIC;
const uint32_t TAIYIN_OBSERVED_ALLOW_BARYCENTER_APPROX =
    TAIYIN_NATIVE_POSITION_ALLOW_BARYCENTER_APPROX;
constexpr uint64_t TAIYIN_OBSERVED_POSITION_FLAGS_MASK = 0x00000000ffffffffull;
constexpr uint64_t TAIYIN_OBSERVED_CALCULATION_FLAGS_MASK =
    TAIYIN_OBSERVED_SPEED
    | TAIYIN_OBSERVED_TRUEPOS
    | TAIYIN_OBSERVED_NO_ABERR
    | TAIYIN_OBSERVED_NO_GDEFL
    | TAIYIN_OBSERVED_ASTROMETRIC
    | TAIYIN_OBSERVED_TOPOCENTRIC
    | TAIYIN_OBSERVED_ALLOW_BARYCENTER_APPROX;
constexpr uint64_t TAIYIN_OBSERVED_OPTION_FLAGS_MASK = 0xffffffff00000000ull;
constexpr uint64_t TAIYIN_OBSERVED_HORIZONTAL = 1ull << 32;
constexpr uint64_t TAIYIN_OBSERVED_REFRACTION = 1ull << 33;
constexpr uint64_t TAIYIN_OBSERVED_STRICT_METEOROLOGY = 1ull << 34;

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
