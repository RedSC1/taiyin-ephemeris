#ifndef TAIYIN_INTERNAL_MOSHIER_BUILTIN_H
#define TAIYIN_INTERNAL_MOSHIER_BUILTIN_H

#include "moshier.h"

namespace taiyin {
namespace internal {

bool get_builtin_moshier_planet_table(int target_id, MoshierPlanetTable* out) noexcept;

bool get_builtin_moshier_planet_corrections(
    int target_id,
    const MoshierCorrectionSegment** out_segments,
    size_t* out_count
) noexcept;

bool get_builtin_moshier_planet_coverage(
    int target_id,
    double* out_jd_tdb_start,
    double* out_jd_tdb_end
) noexcept;

bool get_builtin_moshier_moon_tables(
    MoshierMoonLRTable* out_lr,
    MoshierMoonLatTable* out_lat
) noexcept;

bool get_builtin_moshier_moon_coverage(
    double* out_jd_tdb_start,
    double* out_jd_tdb_end
) noexcept;

bool compile_builtin_moshier_planet_ephemeris_block(
    int target_id,
    int center_id,
    double jd_tdb_start,
    double jd_tdb_end,
    bool apply_de441_correction,
    StorageEphemerisBlock* out
) noexcept;

}  // namespace internal
}  // namespace taiyin

#endif  // TAIYIN_INTERNAL_MOSHIER_BUILTIN_H
