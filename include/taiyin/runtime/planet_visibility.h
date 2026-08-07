#ifndef TAIYIN_RUNTIME_PLANET_VISIBILITY_H
#define TAIYIN_RUNTIME_PLANET_VISIBILITY_H

#include "taiyin/runtime/native_context.h"
#include "taiyin/status.h"

#include <stdint.h>

namespace taiyin {
namespace runtime {

const int TAIYIN_PLANET_VISIBILITY_EVENT_RISE = 1;
const int TAIYIN_PLANET_VISIBILITY_EVENT_SET = 2;
const int TAIYIN_PLANET_VISIBILITY_EVENT_UPPER_TRANSIT = 3;
const int TAIYIN_PLANET_VISIBILITY_EVENT_LOWER_TRANSIT = 4;

const int TAIYIN_PLANET_VISIBILITY_LIMB_UPPER = 1;
const int TAIYIN_PLANET_VISIBILITY_LIMB_CENTER = 2;
const int TAIYIN_PLANET_VISIBILITY_LIMB_LOWER = 3;

const int TAIYIN_PLANET_VISIBILITY_ALTITUDE_STATE_NOT_FOUND = 0;
const int TAIYIN_PLANET_VISIBILITY_ALTITUDE_STATE_CROSSES = 1;
const int TAIYIN_PLANET_VISIBILITY_ALTITUDE_STATE_ALWAYS_ABOVE = 2;
const int TAIYIN_PLANET_VISIBILITY_ALTITUDE_STATE_ALWAYS_BELOW = 3;
const int TAIYIN_PLANET_VISIBILITY_ALTITUDE_STATE_TANGENT = 4;

const int TAIYIN_PLANET_VISIBILITY_CROSSING_ANY = 0;
const int TAIYIN_PLANET_VISIBILITY_CROSSING_RISING = 1;
const int TAIYIN_PLANET_VISIBILITY_CROSSING_SETTING = 2;

const uint32_t TAIYIN_PLANET_VISIBILITY_FLAG_REFRACTION = 1u << 0;
const uint32_t TAIYIN_PLANET_VISIBILITY_FLAG_NO_REFRACTION = 1u << 1;
constexpr uint64_t TAIYIN_PLANET_VISIBILITY_STRICT_METEOROLOGY = 1ull << 32;

struct PlanetVisibilityEventResult {
    int altitude_state;
    int crossing_direction;
    SplitJulianDate jd_ut;
    double residual_rad;
    double min_residual_rad;
    double max_residual_rad;
    SplitJulianDate min_residual_jd_ut;
    SplitJulianDate max_residual_jd_ut;
    int sample_count;
    int refine_count;

    PlanetVisibilityEventResult() noexcept;
};

Status search_planet_rise_set_ut(
    const NativeCalcContext* context,
    int body_id,
    const SplitJulianDate& start_jd_ut,
    const SplitJulianDate& end_jd_ut,
    int event_kind,
    int limb_kind,
    uint64_t planet_visibility_flags,
    PlanetVisibilityEventResult* out,
    EphemerisEvalDiagnostic* diagnostic = 0
) noexcept;

Status search_planet_rise_set_at_horizon_ut(
    const NativeCalcContext* context,
    int body_id,
    const SplitJulianDate& start_jd_ut,
    const SplitJulianDate& end_jd_ut,
    int event_kind,
    int limb_kind,
    double horizon_altitude_rad,
    uint64_t planet_visibility_flags,
    PlanetVisibilityEventResult* out,
    EphemerisEvalDiagnostic* diagnostic = 0
) noexcept;

Status search_planet_transit_ut(
    const NativeCalcContext* context,
    int body_id,
    const SplitJulianDate& start_jd_ut,
    const SplitJulianDate& end_jd_ut,
    int event_kind,
    PlanetVisibilityEventResult* out,
    EphemerisEvalDiagnostic* diagnostic = 0
) noexcept;

}  // namespace runtime
}  // namespace taiyin

#endif  // TAIYIN_RUNTIME_PLANET_VISIBILITY_H
