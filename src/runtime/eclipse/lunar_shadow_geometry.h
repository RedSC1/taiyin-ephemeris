#ifndef TAIYIN_RUNTIME_LUNAR_SHADOW_GEOMETRY_H
#define TAIYIN_RUNTIME_LUNAR_SHADOW_GEOMETRY_H

#include "taiyin/status.h"
#include "taiyin/time.h"
#include "taiyin/vector3.h"

#include <stdint.h>

namespace taiyin {
namespace runtime {

struct EphemerisEvalDiagnostic;
struct NativeCalcContext;

// Geocentric lunar-eclipse geometry evaluated in an orthonormal apparent
// frame. Distances and radii are physical kilometres at the Moon's shadow
// plane; no ecliptic longitude/latitude projection is used.
struct LunarShadowGeometry {
    Vector3 shadow_axis_unit;
    Vector3 transverse_offset_km;
    double axial_distance_km;
    double axis_distance_km;
    double moon_distance_km;
    double sun_distance_km;
    double moon_radius_km;
    double moon_radius_toward_shadow_km;
    double moon_radius_away_from_shadow_km;
    double umbra_radius_km;
    double penumbra_radius_km;

    LunarShadowGeometry() noexcept;
};

Status evaluate_lunar_shadow_geometry(
    const NativeCalcContext* context,
    SplitJulianDate jd_tt,
    uint64_t flags,
    LunarShadowGeometry* out,
    EphemerisEvalDiagnostic* diagnostic
) noexcept;

}  // namespace runtime
}  // namespace taiyin

#endif  // TAIYIN_RUNTIME_LUNAR_SHADOW_GEOMETRY_H
