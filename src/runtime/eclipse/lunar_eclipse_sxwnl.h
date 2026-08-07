#ifndef TAIYIN_RUNTIME_LUNAR_ECLIPSE_SXWNL_H
#define TAIYIN_RUNTIME_LUNAR_ECLIPSE_SXWNL_H

#include "taiyin/status.h"
#include "taiyin/time.h"

#include <stdint.h>

namespace taiyin {
namespace runtime {

struct EphemerisEvalDiagnostic;
struct NativeCalcContext;

namespace sxwnl {
namespace lunar {

struct LecGeometry {
    double x_rad;
    double y_rad;
    double rmin_rad;
    double moon_radius_rad;
    double moon_radius_toward_shadow_rad;
    double moon_radius_away_from_shadow_rad;
    double umbra_radius_rad;
    double penumbra_radius_rad;
    double moon_dist_au;
    double sun_dist_au;
};

struct LecInput {
    double moon_lon_rad;
    double moon_lat_rad;
    double moon_dist_au;
    double sun_lon_rad;
    double sun_lat_rad;
    double sun_dist_au;
    double earth_radius_km;
    double sun_radius_km;
    double moon_radius_km;
    double moon_radius_toward_shadow_km;
    double moon_radius_away_from_shadow_km;
    double shadow_earth_scale;
    double shadow_sun_scale;
    double shadow_parallax_scale;
};

struct LecMaxResult {
    LecGeometry geometry;
    double vx_rad_per_day;
    double vy_rad_per_day;
    double dt_days;
};

Status lecXY(const LecInput& input, LecGeometry* out) noexcept;

Status lecXY(
    const NativeCalcContext* context,
    SplitJulianDate jd_tt,
    uint64_t flags,
    LecGeometry* out,
    EphemerisEvalDiagnostic* diagnostic
) noexcept;

Status lecMax(
    const LecGeometry& z1,
    const LecGeometry& z2,
    double dt_days,
    LecMaxResult* out
) noexcept;

Status lecMax(
    const NativeCalcContext* context,
    SplitJulianDate jd_tt,
    uint64_t flags,
    LecMaxResult* out,
    EphemerisEvalDiagnostic* diagnostic
) noexcept;

double lineT(double x, double y, double vx, double vy, double r, int n) noexcept;

}  // namespace lunar
}  // namespace sxwnl
}  // namespace runtime
}  // namespace taiyin

#endif  // TAIYIN_RUNTIME_LUNAR_ECLIPSE_SXWNL_H
