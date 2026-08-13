#ifndef TAIYIN_RUNTIME_ECLIPSE_SOLAR_APPARENT_SNAPSHOT_H
#define TAIYIN_RUNTIME_ECLIPSE_SOLAR_APPARENT_SNAPSHOT_H

#include "taiyin/runtime/eclipse_search.h"
#include "taiyin/runtime/native_context.h"
#include "taiyin/status.h"
#include "taiyin/time.h"

#include <stdint.h>

namespace taiyin {
namespace runtime {

// One canonical apparent Sun/Moon evaluation for instantaneous solar-eclipse
// geometry.  The vectors are expressed in the caller-selected true equator of
// date and already include the apparent-position corrections selected by the
// context and eclipse flags.
struct SolarApparentSnapshot {
    SplitJulianDate jd_tt;
    SplitJulianDate jd_ut;
    uint64_t eclipse_flags;
    double gast_rad;
    double moon_km[3];
    double sun_km[3];
    double moon_velocity_km_per_day[3];
    double sun_velocity_km_per_day[3];

    SolarApparentSnapshot() noexcept
        : jd_tt(),
          jd_ut(),
          eclipse_flags(0),
          gast_rad(0.0),
          moon_km(),
          sun_km(),
          moon_velocity_km_per_day(),
          sun_velocity_km_per_day() {}
};

Status compute_local_solar_circumstances_from_apparent_snapshot_tt(
    const NativeCalcContext* context,
    const SolarApparentSnapshot& snapshot,
    double longitude_deg,
    double latitude_deg,
    double height_m,
    LocalSolarEclipseCircumstances* out,
    EphemerisEvalDiagnostic* diagnostic
) noexcept;

}  // namespace runtime
}  // namespace taiyin

#endif  // TAIYIN_RUNTIME_ECLIPSE_SOLAR_APPARENT_SNAPSHOT_H
