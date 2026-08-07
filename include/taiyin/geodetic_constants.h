#ifndef TAIYIN_GEODETIC_CONSTANTS_H
#define TAIYIN_GEODETIC_CONSTANTS_H

namespace taiyin {

constexpr double TAIYIN_WGS84_A_M = 6378137.0;
constexpr double TAIYIN_WGS84_A_KM = TAIYIN_WGS84_A_M / 1000.0;
constexpr double TAIYIN_WGS84_F = 1.0 / 298.257223563;
constexpr double TAIYIN_WGS84_E2 = TAIYIN_WGS84_F * (2.0 - TAIYIN_WGS84_F);
constexpr double TAIYIN_WGS84_B_KM = TAIYIN_WGS84_A_KM * (1.0 - TAIYIN_WGS84_F);

}  // namespace taiyin

#endif  // TAIYIN_GEODETIC_CONSTANTS_H
