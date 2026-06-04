#ifndef TAIYIN_INTERNAL_MOSHIER_H
#define TAIYIN_INTERNAL_MOSHIER_H

#include "../state.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace taiyin {
namespace internal {

struct CompiledEphemerisBlock;
struct StorageEphemerisBlock;

const int MOSHIER_NARGS = 18;

enum MoshierPlanetEvaluator {
    GPlan,
    G3Plan,
};

struct MoshierCorrectionSegment {
    double start_year;
    double end_year;
    double center_year;
    double half_width_years;
    double lon_arcsec[4];
    double lat_arcsec[4];
};

struct MoshierPlanetTable {
    int maxargs;
    int max_harmonic[MOSHIER_NARGS];
    int max_power_of_t;
    const int8_t* arg_tbl;
    size_t arg_count;
    const double* lon_tbl;
    size_t lon_count;
    const double* lat_tbl;
    size_t lat_count;
    const double* rad_tbl;
    size_t rad_count;
    double distance_au;
    double timescale_days;
    double trunclvl;

    MoshierPlanetTable()
        : maxargs(0),
          max_harmonic(),
          max_power_of_t(0),
          arg_tbl(0),
          arg_count(0),
          lon_tbl(0),
          lon_count(0),
          lat_tbl(0),
          lat_count(0),
          rad_tbl(0),
          rad_count(0),
          distance_au(0.0),
          timescale_days(0.0),
          trunclvl(0.0) {}
};

struct MoshierPlanetEphemerisData {
    int target_id;
    int center_id;
    double jd_tdb_start;
    double jd_tdb_end;
    MoshierPlanetEvaluator evaluator;
    int objnum;
    bool apply_de441_correction;
    int maxargs;
    int max_harmonic[MOSHIER_NARGS];
    int max_power_of_t;
    double distance_au;
    double timescale_days;
    double trunclvl;
    std::vector<int8_t> arg_tbl;
    std::vector<double> lon_tbl;
    std::vector<double> lat_tbl;
    std::vector<double> rad_tbl;
    std::vector<MoshierCorrectionSegment> corrections;
};

struct MoshierMoonLRTable {
    int maxargs;
    int max_harmonic[MOSHIER_NARGS];
    int max_power_of_t;
    const int8_t* arg_tbl;
    size_t arg_count;
    const double* lon_tbl;
    size_t lon_count;
    const double* rad_tbl;
    size_t rad_count;
    double distance_au;
    double timescale_days;
    double trunclvl;

    MoshierMoonLRTable()
        : maxargs(0),
          max_harmonic(),
          max_power_of_t(0),
          arg_tbl(0),
          arg_count(0),
          lon_tbl(0),
          lon_count(0),
          rad_tbl(0),
          rad_count(0),
          distance_au(0.0),
          timescale_days(0.0),
          trunclvl(0.0) {}
};

struct MoshierMoonLatTable {
    int maxargs;
    int max_harmonic[MOSHIER_NARGS];
    int max_power_of_t;
    const int8_t* arg_tbl;
    size_t arg_count;
    const double* lon_tbl;
    size_t lon_count;
    double distance_au;
    double timescale_days;
    double trunclvl;

    MoshierMoonLatTable()
        : maxargs(0),
          max_harmonic(),
          max_power_of_t(0),
          arg_tbl(0),
          arg_count(0),
          lon_tbl(0),
          lon_count(0),
          distance_au(0.0),
          timescale_days(0.0),
          trunclvl(0.0) {}
};

struct MoshierMoonEphemerisData {
    int target_id;
    int center_id;
    double jd_tdb_start;
    double jd_tdb_end;
    int lr_maxargs;
    int lr_max_harmonic[MOSHIER_NARGS];
    int lr_max_power_of_t;
    double lr_distance_au;
    double lr_timescale_days;
    double lr_trunclvl;
    int lat_maxargs;
    int lat_max_harmonic[MOSHIER_NARGS];
    int lat_max_power_of_t;
    double lat_distance_au;
    double lat_timescale_days;
    double lat_trunclvl;
    std::vector<int8_t> lr_arg_tbl;
    std::vector<double> lr_lon_tbl;
    std::vector<double> lr_rad_tbl;
    std::vector<int8_t> lat_arg_tbl;
    std::vector<double> lat_lon_tbl;
};

struct MoshierEarthBodyEphemerisData {
    int target_id;
    int center_id;
    double jd_tdb_start;
    double jd_tdb_end;
    MoshierPlanetEphemerisData* emb;
    MoshierMoonEphemerisData* moon;
};

bool make_moshier_planet_ephemeris_data(
    int target_id,
    int center_id,
    double jd_tdb_start,
    double jd_tdb_end,
    MoshierPlanetEvaluator evaluator,
    int objnum,
    const MoshierPlanetTable& table,
    const MoshierCorrectionSegment* corrections,
    size_t correction_count,
    MoshierPlanetEphemerisData** out
) noexcept;

bool make_moshier_moon_ephemeris_data(
    int target_id,
    int center_id,
    double jd_tdb_start,
    double jd_tdb_end,
    const MoshierMoonLRTable& moon_lr,
    const MoshierMoonLatTable& moon_lat,
    MoshierMoonEphemerisData** out
) noexcept;

bool compile_moshier_planet_ephemeris_block(
    int target_id,
    int center_id,
    double jd_tdb_start,
    double jd_tdb_end,
    MoshierPlanetEvaluator evaluator,
    int objnum,
    const MoshierPlanetTable& table,
    const MoshierCorrectionSegment* corrections,
    size_t correction_count,
    StorageEphemerisBlock* out
) noexcept;

bool compile_moshier_moon_ephemeris_block(
    int target_id,
    int center_id,
    double jd_tdb_start,
    double jd_tdb_end,
    const MoshierMoonLRTable& moon_lr,
    const MoshierMoonLatTable& moon_lat,
    StorageEphemerisBlock* out
) noexcept;

bool compile_moshier_earth_body_ephemeris_block(
    int target_id,
    int center_id,
    double jd_tdb_start,
    double jd_tdb_end,
    const MoshierPlanetTable& emb_table,
    const MoshierCorrectionSegment* emb_corrections,
    size_t emb_correction_count,
    const MoshierMoonLRTable& moon_lr,
    const MoshierMoonLatTable& moon_lat,
    StorageEphemerisBlock* out
) noexcept;

bool calc_moshier_planet_state(
    double jd_tdb,
    const MoshierPlanetEphemerisData* data,
    CartesianState* out
) noexcept;

bool calc_moshier_moon_state(
    double jd_tdb,
    const MoshierMoonEphemerisData* data,
    CartesianState* out
) noexcept;

bool calc_moshier_earth_body_state(
    double jd_tdb,
    const MoshierEarthBodyEphemerisData* data,
    CartesianState* out
) noexcept;

bool calc_moshier_planet_state_void(
    double jd_tdb,
    const void* data,
    CartesianState* out
) noexcept;

bool calc_moshier_moon_state_void(
    double jd_tdb,
    const void* data,
    CartesianState* out
) noexcept;

bool calc_moshier_earth_body_state_void(
    double jd_tdb,
    const void* data,
    CartesianState* out
) noexcept;

void moshier_planet_ephemeris_data_destroy(MoshierPlanetEphemerisData* data) noexcept;
void moshier_planet_ephemeris_data_destroy_void(void* data) noexcept;
void moshier_moon_ephemeris_data_destroy(MoshierMoonEphemerisData* data) noexcept;
void moshier_moon_ephemeris_data_destroy_void(void* data) noexcept;
void moshier_earth_body_ephemeris_data_destroy(MoshierEarthBodyEphemerisData* data) noexcept;
void moshier_earth_body_ephemeris_data_destroy_void(void* data) noexcept;

}  // namespace internal
}  // namespace taiyin

#endif  // TAIYIN_INTERNAL_MOSHIER_H
