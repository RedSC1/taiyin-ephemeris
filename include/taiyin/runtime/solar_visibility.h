#ifndef TAIYIN_RUNTIME_SOLAR_VISIBILITY_H
#define TAIYIN_RUNTIME_SOLAR_VISIBILITY_H

#include "taiyin/runtime/native_context.h"
#include "taiyin/status.h"

#include <stdint.h>

namespace taiyin {
namespace runtime {

const int TAIYIN_SOLAR_VISIBILITY_EVENT_RISE = 1;
const int TAIYIN_SOLAR_VISIBILITY_EVENT_SET = 2;
const int TAIYIN_SOLAR_VISIBILITY_EVENT_UPPER_TRANSIT = 3;
const int TAIYIN_SOLAR_VISIBILITY_EVENT_LOWER_TRANSIT = 4;

const int TAIYIN_SOLAR_VISIBILITY_LIMB_UPPER = 1;
const int TAIYIN_SOLAR_VISIBILITY_LIMB_CENTER = 2;
const int TAIYIN_SOLAR_VISIBILITY_LIMB_LOWER = 3;

const int TAIYIN_SOLAR_VISIBILITY_TWILIGHT_CIVIL = 1;
const int TAIYIN_SOLAR_VISIBILITY_TWILIGHT_NAUTICAL = 2;
const int TAIYIN_SOLAR_VISIBILITY_TWILIGHT_ASTRONOMICAL = 3;

const int TAIYIN_SOLAR_VISIBILITY_ALTITUDE_STATE_NOT_FOUND = 0;
const int TAIYIN_SOLAR_VISIBILITY_ALTITUDE_STATE_CROSSES = 1;
const int TAIYIN_SOLAR_VISIBILITY_ALTITUDE_STATE_ALWAYS_ABOVE = 2;
const int TAIYIN_SOLAR_VISIBILITY_ALTITUDE_STATE_ALWAYS_BELOW = 3;
const int TAIYIN_SOLAR_VISIBILITY_ALTITUDE_STATE_TANGENT = 4;

const int TAIYIN_SOLAR_VISIBILITY_CROSSING_ANY = 0;
const int TAIYIN_SOLAR_VISIBILITY_CROSSING_RISING = 1;
const int TAIYIN_SOLAR_VISIBILITY_CROSSING_SETTING = 2;

const uint32_t TAIYIN_SOLAR_VISIBILITY_FLAG_REFRACTION = 1u << 0;
const uint32_t TAIYIN_SOLAR_VISIBILITY_FLAG_FIXED_DISC_SIZE = 1u << 1;
const uint32_t TAIYIN_SOLAR_VISIBILITY_FLAG_NO_REFRACTION = 1u << 2;
constexpr uint64_t TAIYIN_SOLAR_VISIBILITY_STRICT_METEOROLOGY = 1ull << 32;

struct SolarVisibilityEventResult {
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

    SolarVisibilityEventResult() noexcept;
};

struct SolarRiseSetFastResult {
    int altitude_state;
    SplitJulianDate rise_jd_tt;
    SplitJulianDate set_jd_tt;
    int sample_count;
    int refine_count;

    SolarRiseSetFastResult() noexcept;
};

struct SolarTransitFastResult {
    SplitJulianDate transit_jd_tt;
    double altitude_rad;
    double azimuth_rad;
    int sample_count;
    int refine_count;

    SolarTransitFastResult() noexcept;
};

Status compute_solar_rise_set_fast_tt(
    const NativeCalcContext* context,
    const SplitJulianDate& center_jd_tt,
    double longitude_deg,
    double latitude_deg,
    double height_m,
    int limb_kind,
    double horizon_altitude_rad,
    uint64_t solar_visibility_flags,
    SolarRiseSetFastResult* out,
    EphemerisEvalDiagnostic* diagnostic = 0
) noexcept;

Status compute_solar_transit_fast_tt(
    const NativeCalcContext* context,
    const SplitJulianDate& center_jd_tt,
    double longitude_deg,
    double latitude_deg,
    double height_m,
    SolarTransitFastResult* out,
    EphemerisEvalDiagnostic* diagnostic = 0
) noexcept;

Status search_solar_rise_set_ut(
    const NativeCalcContext* context,
    const SplitJulianDate& start_jd_ut,
    const SplitJulianDate& end_jd_ut,
    int event_kind,
    int limb_kind,
    uint64_t solar_visibility_flags,
    SolarVisibilityEventResult* out,
    EphemerisEvalDiagnostic* diagnostic = 0
) noexcept;

Status search_solar_rise_set_at_horizon_ut(
    const NativeCalcContext* context,
    const SplitJulianDate& start_jd_ut,
    const SplitJulianDate& end_jd_ut,
    int event_kind,
    int limb_kind,
    double horizon_altitude_rad,
    uint64_t solar_visibility_flags,
    SolarVisibilityEventResult* out,
    EphemerisEvalDiagnostic* diagnostic = 0
) noexcept;

Status search_solar_twilight_ut(
    const NativeCalcContext* context,
    const SplitJulianDate& start_jd_ut,
    const SplitJulianDate& end_jd_ut,
    int event_kind,
    int twilight_kind,
    SolarVisibilityEventResult* out,
    EphemerisEvalDiagnostic* diagnostic = 0
) noexcept;

Status search_solar_transit_ut(
    const NativeCalcContext* context,
    const SplitJulianDate& start_jd_ut,
    const SplitJulianDate& end_jd_ut,
    int event_kind,
    SolarVisibilityEventResult* out,
    EphemerisEvalDiagnostic* diagnostic = 0
) noexcept;

}  // namespace runtime
}  // namespace taiyin

#endif  // TAIYIN_RUNTIME_SOLAR_VISIBILITY_H
