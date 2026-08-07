#ifndef TAIYIN_RUNTIME_SOLAR_TIME_H
#define TAIYIN_RUNTIME_SOLAR_TIME_H

#include "taiyin/runtime/native_position.h"
#include "taiyin/status.h"

namespace taiyin {
namespace runtime {

// Apparent solar time minus mean solar time at the same instant.
struct EquationOfTimeResult {
    SplitJulianDate jd_ut;
    SplitJulianDate jd_tt;
    double equation_days;
    double equation_seconds;
    double apparent_sun_right_ascension_rad;
    double gast_rad;

    EquationOfTimeResult() noexcept;
};

Status calc_equation_of_time_ut(
    const NativeCalcContext* context,
    SplitJulianDate jd_ut,
    EquationOfTimeResult* out,
    EphemerisEvalDiagnostic* diagnostic
) noexcept;

Status calc_equation_of_time_tt(
    const NativeCalcContext* context,
    SplitJulianDate jd_tt,
    EquationOfTimeResult* out,
    EphemerisEvalDiagnostic* diagnostic
) noexcept;

// Julian-date local solar-time values use east-positive longitude:
// LMT = UT1 + longitude / 2π and LAT = LMT + equation_of_time.
Status local_mean_to_apparent_solar_time(
    const NativeCalcContext* context,
    SplitJulianDate jd_local_mean,
    double longitude_rad,
    SplitJulianDate* out_jd_local_apparent,
    EphemerisEvalDiagnostic* diagnostic
) noexcept;

Status local_apparent_to_mean_solar_time(
    const NativeCalcContext* context,
    SplitJulianDate jd_local_apparent,
    double longitude_rad,
    SplitJulianDate* out_jd_local_mean,
    EphemerisEvalDiagnostic* diagnostic
) noexcept;

}  // namespace runtime
}  // namespace taiyin

#endif  // TAIYIN_RUNTIME_SOLAR_TIME_H
