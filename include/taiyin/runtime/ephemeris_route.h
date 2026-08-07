#ifndef TAIYIN_RUNTIME_EPHEMERIS_ROUTE_H
#define TAIYIN_RUNTIME_EPHEMERIS_ROUTE_H

#include <cstdint>

namespace taiyin {
namespace runtime {

const uint64_t TAIYIN_EPHEMERIS_ROUTE_AUTO = 0;
const uint64_t TAIYIN_EPHEMERIS_ROUTE_OPM2 = 1;
const uint64_t TAIYIN_EPHEMERIS_ROUTE_SPK = 2;
const uint64_t TAIYIN_EPHEMERIS_ROUTE_SEMI_ANALYTIC = 3;

}  // namespace runtime
}  // namespace taiyin

#endif  // TAIYIN_RUNTIME_EPHEMERIS_ROUTE_H
