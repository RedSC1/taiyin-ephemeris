#ifndef TAIYIN_INTERNAL_EOP_H
#define TAIYIN_INTERNAL_EOP_H

#include <cstddef>

namespace taiyin {
namespace internal {

struct EarthOrientationSample {
    double jd_utc;
    double dut1_seconds;
    double xp_rad;
    double yp_rad;
    double sp_rad;
    double lod_seconds;
    double dx_rad;
    double dy_rad;
};

struct EarthOrientationTable {
    const EarthOrientationSample* samples;
    size_t count;
};

struct EarthOrientationRates {
    double xp_rate_rad_per_day;
    double yp_rate_rad_per_day;
    double sp_rate_rad_per_day;
    double dx_rate_rad_per_day;
    double dy_rate_rad_per_day;
};

struct EarthRotationDerivatives {
    double dut1_rate_seconds_per_day;
    double lod_seconds;
    double lod_rate_seconds_per_day;
};

double sp_rad_for_jd(double jd) noexcept;

bool interpolate_earth_orientation(
    const EarthOrientationTable* table,
    double jd_utc,
    EarthOrientationSample* out
) noexcept;

bool derive_earth_orientation_rates(
    const EarthOrientationTable* table,
    double jd_utc,
    EarthOrientationRates* out_rates,
    EarthRotationDerivatives* out_derivatives
) noexcept;

bool parse_finals2000a_table(
    const char* data,
    int size,
    EarthOrientationSample* out_samples,
    size_t max_count,
    size_t* out_count
) noexcept;

bool load_finals2000a_file(
    const char* path,
    EarthOrientationTable* out
) noexcept;

void destroy_earth_orientation_table(EarthOrientationTable* table) noexcept;

}  // namespace internal
}  // namespace taiyin

#endif  // TAIYIN_INTERNAL_EOP_H