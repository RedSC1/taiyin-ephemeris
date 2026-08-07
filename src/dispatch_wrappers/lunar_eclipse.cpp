#include "taiyin/dispatch.h"

#include <cmath>

namespace taiyin {
namespace dispatch {
namespace wrappers {

// ---------------------------------------------------------------------------
// Lunar eclipse shadow model registration
//
// Each shadow model is a set of three scale factors (earth, sun, parallax)
// applied to the Earth's umbra/penumbra radii.  Built-in models are
// registered at startup; user models can be registered via
// register_eclipse_shadow_model() with IDs >= ECLIPSE_SHADOW_CUSTOM_START.
// ---------------------------------------------------------------------------

void register_builtin_eclipse_shadow_wrappers() {
    // Chauvenet: 2% enlargement + Earth oblateness correction
    // Validated against NASA eclipse bulletins (taiyin-ephemeris-ts test).
    add_eclipse_shadow_model(EclipseShadowModelEntry(
        ECLIPSE_SHADOW_CHAUVENET,
        1.02 * 0.998340,   // earth scale
        1.02,              // sun scale
        1.02));            // parallax scale

    // NASA Danjon: empirical 1% enlargement (earth only)
    add_eclipse_shadow_model(EclipseShadowModelEntry(
        ECLIPSE_SHADOW_NASA_DANJON,
        1.01, 1.0, 1.0));

    // Geometric: pure geometry, no atmosphere, no oblateness
    add_eclipse_shadow_model(EclipseShadowModelEntry(
        ECLIPSE_SHADOW_GEOMETRIC,
        1.0, 1.0, 1.0));

    // Raw Danjon: Danjon's 1/85 atmospheric enlargement
    add_eclipse_shadow_model(EclipseShadowModelEntry(
        ECLIPSE_SHADOW_RAW_DANJON,
        1.0 + 1.0 / 85.0, 1.0, 1.0));

    // Priority order: chauvenet first (most accurate for NASA alignment)
    static bool prioritized = []() -> bool {
        const int order[] = {
            ECLIPSE_SHADOW_CHAUVENET,
            ECLIPSE_SHADOW_NASA_DANJON,
            ECLIPSE_SHADOW_GEOMETRIC,
            ECLIPSE_SHADOW_RAW_DANJON,
        };
        return set_eclipse_shadow_priority_order(
            order, sizeof(order) / sizeof(order[0]));
    }();
    (void)prioritized;
}

// ---------------------------------------------------------------------------
// Lunar eclipse moon radius model registration
//
// Each model stores the Moon's radius in km.  Built-in models are
// registered at startup; user models can be registered via
// register_eclipse_moon_radius_model() with IDs >= ECLIPSE_MOON_CUSTOM_START.
// ---------------------------------------------------------------------------

void register_builtin_eclipse_moon_radius_wrappers() {
    // Almanac: ratio to Earth equatorial radius (conventional)
    add_eclipse_moon_radius_model(EclipseMoonRadiusModelEntry(
        ECLIPSE_MOON_ALMANAC,
        0.2725076 * 6378.1366));  // ≈ 1737.41 km

    // IAU mean radius
    add_eclipse_moon_radius_model(EclipseMoonRadiusModelEntry(
        ECLIPSE_MOON_MEAN,
        1737.4));

    (void)0;
}

}  // namespace wrappers
}  // namespace dispatch
}  // namespace taiyin