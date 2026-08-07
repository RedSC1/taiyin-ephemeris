#ifndef TAIYIN_ASTROLOGY_EXTENSION_HOUSES_INTERNAL_H
#define TAIYIN_ASTROLOGY_EXTENSION_HOUSES_INTERNAL_H

namespace taiyin {
namespace astrology {
namespace internal {

// Pure Placidus geometry. Inputs are the local ARMC, geographic latitude, and
// true obliquity of date. It returns false for polar/no-convergence cases;
// public API fallback policy belongs to calc_houses_*().
bool calc_placidus_cusps_from_armc(
    double armc_rad,
    double latitude_rad,
    double true_obliquity_rad,
    double out_cusp_longitude_rad[12]
) noexcept;

}  // namespace internal
}  // namespace astrology
}  // namespace taiyin

#endif  // TAIYIN_ASTROLOGY_EXTENSION_HOUSES_INTERNAL_H
