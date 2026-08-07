#ifndef TAIYIN_RUNTIME_HELIACAL_VISIBILITY_H
#define TAIYIN_RUNTIME_HELIACAL_VISIBILITY_H

#include "taiyin/runtime/native_context.h"
#include "taiyin/status.h"

#include <stdint.h>

namespace taiyin {
namespace runtime {

constexpr uint64_t TAIYIN_HELIACAL_VISIBILITY_POSITION_FLAGS_MASK = 0x00000000ffffffffull;
constexpr uint64_t TAIYIN_HELIACAL_VISIBILITY_OPTION_FLAGS_MASK = 0xffffffff00000000ull;
constexpr uint64_t TAIYIN_HELIACAL_VISIBILITY_INCLUDE_MOONLIGHT = 1ull << 32;
constexpr uint64_t TAIYIN_HELIACAL_VISIBILITY_STRICT_METEOROLOGY = 1ull << 33;

const int TAIYIN_HELIACAL_VISIBILITY_EVENT_MORNING_FIRST = 1;
const int TAIYIN_HELIACAL_VISIBILITY_EVENT_MORNING_LAST = 2;
const int TAIYIN_HELIACAL_VISIBILITY_EVENT_EVENING_FIRST = 3;
const int TAIYIN_HELIACAL_VISIBILITY_EVENT_EVENING_LAST = 4;

// Override the profile's calibrated zenith extinction when a local measured
// value is available. A non-finite value selects the profile's derived or
// reference extinction behavior.
struct HeliacalVisibilityConditions {
    double extinction_mag_per_airmass;
    // A measured target-direction background, in nanoLamberts. A non-finite
    // value lets the selected profile calculate its own sky background.
    double sky_brightness_nanolambert;
    // A dark-sky zenith background in nanoLamberts. This is used by profiles
    // with a night-sky component when no direct sky measurement is supplied.
    double night_sky_brightness_nanolambert;
    HeliacalVisibilityConditions() noexcept;
};

// Physical inputs consumed by a registered heliacal-visibility profile.
// Angles are unrefracted topocentric values in radians; magnitudes are
// extra-atmospheric visual magnitudes.
struct HeliacalVisibilityModelInput {
    double target_magnitude;
    double target_altitude_rad;
    double target_azimuth_rad;
    double sun_altitude_rad;
    double sun_azimuth_rad;
    double target_sun_separation_rad;
    double extinction_mag_per_airmass;
    double sky_brightness_nanolambert;
    double night_sky_brightness_nanolambert;
    double moon_altitude_rad;
    double moon_azimuth_rad;
    double moon_phase_angle_rad;
    int include_moonlight;

    HeliacalVisibilityModelInput() noexcept;
};

struct HeliacalVisibilityResult {
    int visible;
    int model_id;
    int extinction_model_id;
    int twilight_model_id;
    int moonlight_model_id;
    int visual_threshold_model_id;
    double target_magnitude;
    double limiting_magnitude;
    double target_altitude_rad;
    double target_azimuth_rad;
    double sun_altitude_rad;
    double sun_azimuth_rad;
    double target_sun_separation_rad;
    double airmass;
    double extinction_mag_per_airmass;
    double extinction_mag;
    double sky_brightness_nanolambert;
    double moonlight_brightness_nanolambert;
    double threshold_illuminance_footcandles;
    double target_illuminance_footcandles;
    // Positive values mean that the target is brighter than the profile's
    // limiting magnitude. This works for all registered profiles.
    double visibility_margin_magnitude;
    // Only profiles with an explicit inverse Sun-altitude criterion populate
    // these two values. Other profiles leave them non-finite.
    double required_sun_altitude_rad;
    double solar_depression_margin_rad;

    HeliacalVisibilityResult() noexcept;
};

// A heliacal event is a date-level transition between daily morning or
// evening visibility windows. jd_ut is the best instantaneous visibility
// time in the returned event's window, not an arbitrary midnight label.
struct HeliacalVisibilitySearchResult {
    int event_kind;
    SplitJulianDate jd_ut;
    SplitJulianDate window_start_jd_ut;
    SplitJulianDate window_end_jd_ut;
    int scanned_day_count;
    int sampled_window_count;
    int visibility_evaluation_count;
    HeliacalVisibilityResult visibility;

    HeliacalVisibilitySearchResult() noexcept;
};

// Both functions evaluate whether the target is visible at one instant. They
// are the primitive used by the morning/evening event searches.
Status calc_body_heliacal_visibility_ut(
    const NativeCalcContext* context,
    int body_id,
    const SplitJulianDate& jd_ut,
    uint64_t flags,
    const HeliacalVisibilityConditions* conditions,
    HeliacalVisibilityResult* out,
    EphemerisEvalDiagnostic* diagnostic = nullptr
) noexcept;

Status calc_star_heliacal_visibility_ut(
    const NativeCalcContext* context,
    const char* star_key,
    const SplitJulianDate& jd_ut,
    uint64_t flags,
    const HeliacalVisibilityConditions* conditions,
    HeliacalVisibilityResult* out,
    EphemerisEvalDiagnostic* diagnostic = nullptr
) noexcept;

// Search the next discrete heliacal visibility transition. max_search_days is
// an explicit positive upper bound; high-latitude dates without a complete
// astronomical-twilight window are ignored rather than treated as invisible.
Status search_next_body_heliacal_visibility_ut(
    const NativeCalcContext* context,
    int body_id,
    const SplitJulianDate& jd_start_ut,
    int event_kind,
    double max_search_days,
    uint64_t flags,
    const HeliacalVisibilityConditions* conditions,
    HeliacalVisibilitySearchResult* out,
    EphemerisEvalDiagnostic* diagnostic = nullptr
) noexcept;

Status search_next_star_heliacal_visibility_ut(
    const NativeCalcContext* context,
    const char* star_key,
    const SplitJulianDate& jd_start_ut,
    int event_kind,
    double max_search_days,
    uint64_t flags,
    const HeliacalVisibilityConditions* conditions,
    HeliacalVisibilitySearchResult* out,
    EphemerisEvalDiagnostic* diagnostic = nullptr
) noexcept;

}  // namespace runtime
}  // namespace taiyin

#endif  // TAIYIN_RUNTIME_HELIACAL_VISIBILITY_H
