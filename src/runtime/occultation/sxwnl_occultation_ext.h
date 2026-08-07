#ifndef TAIYIN_RUNTIME_SXWNL_OCCULTATION_EXT_H
#define TAIYIN_RUNTIME_SXWNL_OCCULTATION_EXT_H

#include "runtime/eclipse/solar_eclipse_sxwnl.h"

namespace taiyin {
namespace runtime {
namespace sxwnl_ext {
namespace occultation {

// 寿星万年历 rsPL-style geometry generalized for lunar occultations.
// Keep this namespace internal: public Taiyin APIs should expose named result
// structs, not sxwnl-shaped helper names.

struct Boundary {
    bool valid;
    double longitude_rad;
    double latitude_rad;

    Boundary() noexcept;
};

struct ZbState {
    sxwnl::solar::Vec3 M;
    sxwnl::solar::Vec3 T;
    double gast_rad;

    ZbState() noexcept;
};

struct NbjResult {
    Boundary pp0;
    Boundary pp1;
    int center_line_hits_earth;

    NbjResult() noexcept;
};

void zb0(
    const sxwnl::solar::Vec3& moon_llr,
    const sxwnl::solar::Vec3& target_llr,
    double gast_rad,
    ZbState* out
) noexcept;

Boundary lineEar_llr(
    const sxwnl::solar::Vec3& moon_llr,
    const sxwnl::solar::Vec3& target_llr,
    double gast_rad
) noexcept;

Boundary pp0(const ZbState& state) noexcept;

Boundary pp1(const ZbState& state) noexcept;

NbjResult nbj(const ZbState& state) noexcept;

double surface_distance_km(
    double longitude_a_rad,
    double latitude_a_rad,
    double longitude_b_rad,
    double latitude_b_rad
) noexcept;

}  // namespace occultation
}  // namespace sxwnl_ext
}  // namespace runtime
}  // namespace taiyin

#endif  // TAIYIN_RUNTIME_SXWNL_OCCULTATION_EXT_H
