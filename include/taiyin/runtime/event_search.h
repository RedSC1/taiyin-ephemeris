#ifndef TAIYIN_RUNTIME_EVENT_SEARCH_H
#define TAIYIN_RUNTIME_EVENT_SEARCH_H

#include "taiyin/runtime/native_position.h"
#include "taiyin/runtime/phenomena.h"
#include "taiyin/status.h"

#include <stddef.h>
#include <stdint.h>

namespace taiyin {
namespace runtime {

const uint64_t TAIYIN_EVENT_SEARCH_POSITION_FLAGS_MASK = 0x00000000ffffffffull;
const uint64_t TAIYIN_EVENT_SEARCH_OPTION_FLAGS_MASK = 0xffffffff00000000ull;
const uint64_t TAIYIN_EVENT_SEARCH_REVERSE = 1ull << 32;
const uint64_t TAIYIN_EVENT_SEARCH_REFRACTION = 1ull << 33;
const uint64_t TAIYIN_EVENT_SEARCH_NO_REFRACTION = 1ull << 34;

const uint32_t TAIYIN_GREATEST_ELONGATION_EASTERN = 1u << 0;
const uint32_t TAIYIN_GREATEST_ELONGATION_WESTERN = 1u << 1;

const uint32_t TAIYIN_SOLAR_TRANSIT_PARTIAL = 1u << 0;
const uint32_t TAIYIN_SOLAR_TRANSIT_FULL_DISK = 1u << 1;
const uint32_t TAIYIN_SOLAR_TRANSIT_VISIBLE_AT_OBSERVER = 1u << 8;
const uint32_t TAIYIN_SOLAR_TRANSIT_T1_VISIBLE = 1u << 9;
const uint32_t TAIYIN_SOLAR_TRANSIT_T2_VISIBLE = 1u << 10;
const uint32_t TAIYIN_SOLAR_TRANSIT_GREATEST_VISIBLE = 1u << 11;
const uint32_t TAIYIN_SOLAR_TRANSIT_T3_VISIBLE = 1u << 12;
const uint32_t TAIYIN_SOLAR_TRANSIT_T4_VISIBLE = 1u << 13;

const size_t TAIYIN_SOLAR_TRANSIT_CONTACT_T1 = 0;
const size_t TAIYIN_SOLAR_TRANSIT_CONTACT_T2 = 1;
const size_t TAIYIN_SOLAR_TRANSIT_CONTACT_GREATEST = 2;
const size_t TAIYIN_SOLAR_TRANSIT_CONTACT_T3 = 3;
const size_t TAIYIN_SOLAR_TRANSIT_CONTACT_T4 = 4;
const size_t TAIYIN_SOLAR_TRANSIT_CONTACT_COUNT = 5;

struct GreatestElongationSearchResult {
    SplitJulianDate jd_ut;
    double elongation_rad;
    double relative_longitude_rad;
    uint32_t kind;
    int body_id;
    int iteration_count;
    int evaluation_count;
    BodyPhenomena phenomena;

    GreatestElongationSearchResult() noexcept;
};

struct AngularSeparationSearchResult {
    // Time scale follows the entry point: UT for
    // search_minimum_angular_separation_ut(), TT for
    // search_minimum_angular_separation_tt().
    SplitJulianDate jd;
    double separation_rad;
    double separation_rate_rad_per_day;
    int body_a_id;
    int body_b_id;
    int iteration_count;
    int evaluation_count;

    AngularSeparationSearchResult() noexcept;
};

struct BodyStarAngularSeparationSearchResult {
    // Time scale follows the entry point: UT for
    // search_minimum_body_star_angular_separation_ut(), TT for
    // search_minimum_body_star_angular_separation_tt().
    SplitJulianDate jd;
    double separation_rad;
    double separation_rate_rad_per_day;
    int body_id;
    int iteration_count;
    int evaluation_count;

    BodyStarAngularSeparationSearchResult() noexcept;
};

struct SolarTransitSearchResult {
    int body_id;
    uint32_t kind;
    SplitJulianDate greatest_jd_ut;
    double minimum_separation_rad;
    double sun_radius_rad;
    double body_radius_rad;
    SplitJulianDate t1_jd_ut;
    SplitJulianDate t2_jd_ut;
    SplitJulianDate t3_jd_ut;
    SplitJulianDate t4_jd_ut;
    int iteration_count;
    int evaluation_count;

    SolarTransitSearchResult() noexcept;
};

struct LocalSolarTransitSearchResult {
    SolarTransitSearchResult global;
    SolarTransitSearchResult topocentric;
    uint32_t visibility_flags;
    double contact_sun_altitude_deg[TAIYIN_SOLAR_TRANSIT_CONTACT_COUNT];
    double contact_sun_azimuth_deg[TAIYIN_SOLAR_TRANSIT_CONTACT_COUNT];
    SplitJulianDate sunrise_jd_ut;
    SplitJulianDate sunset_jd_ut;

    LocalSolarTransitSearchResult() noexcept;
};

double recommended_longitude_search_step_days(int body_id) noexcept;
double recommended_aspect_search_step_days(int body_a_id, int body_b_id) noexcept;

Status search_solar_longitude_ut(
    const NativeCalcContext* context,
    double target_longitude_rad,
    SplitJulianDate estimate_jd_ut,
    uint64_t flags,
    SplitJulianDate* out_jd_ut,
    EphemerisEvalDiagnostic* diagnostic
) noexcept;

Status search_solar_longitude_tt(
    const NativeCalcContext* context,
    double target_longitude_rad,
    SplitJulianDate estimate_jd_tt,
    uint64_t flags,
    SplitJulianDate* out_jd_tt,
    EphemerisEvalDiagnostic* diagnostic
) noexcept;

Status search_moon_longitude_ut(
    const NativeCalcContext* context,
    double target_longitude_rad,
    SplitJulianDate estimate_jd_ut,
    uint64_t flags,
    SplitJulianDate* out_jd_ut,
    EphemerisEvalDiagnostic* diagnostic
) noexcept;

Status search_moon_longitude_tt(
    const NativeCalcContext* context,
    double target_longitude_rad,
    SplitJulianDate estimate_jd_tt,
    uint64_t flags,
    SplitJulianDate* out_jd_tt,
    EphemerisEvalDiagnostic* diagnostic
) noexcept;

Status search_body_longitude_crossings_ut(
    const NativeCalcContext* context,
    int body_id,
    double target_longitude_rad,
    SplitJulianDate start_jd_ut,
    SplitJulianDate end_jd_ut,
    double max_step_days,
    uint64_t flags,
    SplitJulianDate* out_jd_ut,
    size_t max_event_count,
    size_t* out_event_count,
    EphemerisEvalDiagnostic* diagnostic
) noexcept;

Status search_body_longitude_crossings_auto_step_ut(
    const NativeCalcContext* context,
    int body_id,
    double target_longitude_rad,
    SplitJulianDate start_jd_ut,
    SplitJulianDate end_jd_ut,
    uint64_t flags,
    SplitJulianDate* out_jd_ut,
    size_t max_event_count,
    size_t* out_event_count,
    EphemerisEvalDiagnostic* diagnostic
) noexcept;

Status search_body_longitude_crossings_tt(
    const NativeCalcContext* context,
    int body_id,
    double target_longitude_rad,
    SplitJulianDate start_jd_tt,
    SplitJulianDate end_jd_tt,
    double max_step_days,
    uint64_t flags,
    SplitJulianDate* out_jd_tt,
    size_t max_event_count,
    size_t* out_event_count,
    EphemerisEvalDiagnostic* diagnostic
) noexcept;

Status search_body_longitude_crossings_auto_step_tt(
    const NativeCalcContext* context,
    int body_id,
    double target_longitude_rad,
    SplitJulianDate start_jd_tt,
    SplitJulianDate end_jd_tt,
    uint64_t flags,
    SplitJulianDate* out_jd_tt,
    size_t max_event_count,
    size_t* out_event_count,
    EphemerisEvalDiagnostic* diagnostic
) noexcept;

Status search_body_longitude_stations_ut(
    const NativeCalcContext* context,
    int body_id,
    SplitJulianDate start_jd_ut,
    SplitJulianDate end_jd_ut,
    double max_step_days,
    uint64_t flags,
    SplitJulianDate* out_jd_ut,
    double* out_longitude_rad,
    size_t max_event_count,
    size_t* out_event_count,
    EphemerisEvalDiagnostic* diagnostic
) noexcept;

Status search_body_longitude_stations_auto_step_ut(
    const NativeCalcContext* context,
    int body_id,
    SplitJulianDate start_jd_ut,
    SplitJulianDate end_jd_ut,
    uint64_t flags,
    SplitJulianDate* out_jd_ut,
    double* out_longitude_rad,
    size_t max_event_count,
    size_t* out_event_count,
    EphemerisEvalDiagnostic* diagnostic
) noexcept;

Status search_body_longitude_stations_tt(
    const NativeCalcContext* context,
    int body_id,
    SplitJulianDate start_jd_tt,
    SplitJulianDate end_jd_tt,
    double max_step_days,
    uint64_t flags,
    SplitJulianDate* out_jd_tt,
    double* out_longitude_rad,
    size_t max_event_count,
    size_t* out_event_count,
    EphemerisEvalDiagnostic* diagnostic
) noexcept;

Status search_body_longitude_stations_auto_step_tt(
    const NativeCalcContext* context,
    int body_id,
    SplitJulianDate start_jd_tt,
    SplitJulianDate end_jd_tt,
    uint64_t flags,
    SplitJulianDate* out_jd_tt,
    double* out_longitude_rad,
    size_t max_event_count,
    size_t* out_event_count,
    EphemerisEvalDiagnostic* diagnostic
) noexcept;

Status search_body_aspect_crossings_ut(
    const NativeCalcContext* context,
    int body_a_id,
    int body_b_id,
    double aspect_rad,
    SplitJulianDate start_jd_ut,
    SplitJulianDate end_jd_ut,
    double max_step_days,
    uint64_t flags,
    SplitJulianDate* out_jd_ut,
    size_t max_event_count,
    size_t* out_event_count,
    EphemerisEvalDiagnostic* diagnostic
) noexcept;

Status search_body_aspect_crossings_auto_step_ut(
    const NativeCalcContext* context,
    int body_a_id,
    int body_b_id,
    double aspect_rad,
    SplitJulianDate start_jd_ut,
    SplitJulianDate end_jd_ut,
    uint64_t flags,
    SplitJulianDate* out_jd_ut,
    size_t max_event_count,
    size_t* out_event_count,
    EphemerisEvalDiagnostic* diagnostic
) noexcept;

Status search_body_aspect_crossings_tt(
    const NativeCalcContext* context,
    int body_a_id,
    int body_b_id,
    double aspect_rad,
    SplitJulianDate start_jd_tt,
    SplitJulianDate end_jd_tt,
    double max_step_days,
    uint64_t flags,
    SplitJulianDate* out_jd_tt,
    size_t max_event_count,
    size_t* out_event_count,
    EphemerisEvalDiagnostic* diagnostic
) noexcept;

Status search_body_aspect_crossings_auto_step_tt(
    const NativeCalcContext* context,
    int body_a_id,
    int body_b_id,
    double aspect_rad,
    SplitJulianDate start_jd_tt,
    SplitJulianDate end_jd_tt,
    uint64_t flags,
    SplitJulianDate* out_jd_tt,
    size_t max_event_count,
    size_t* out_event_count,
    EphemerisEvalDiagnostic* diagnostic
) noexcept;

Status search_body_exact_aspects_ut(
    const NativeCalcContext* context,
    int body_a_id,
    int body_b_id,
    const double* aspect_separations_rad,
    size_t aspect_count,
    SplitJulianDate start_jd_ut,
    SplitJulianDate end_jd_ut,
    double max_step_days,
    uint64_t flags,
    SplitJulianDate* out_jd_ut,
    double* out_target_aspect_rad,
    size_t max_event_count,
    size_t* out_event_count,
    EphemerisEvalDiagnostic* diagnostic
) noexcept;

Status search_body_exact_aspects_auto_step_ut(
    const NativeCalcContext* context,
    int body_a_id,
    int body_b_id,
    const double* aspect_separations_rad,
    size_t aspect_count,
    SplitJulianDate start_jd_ut,
    SplitJulianDate end_jd_ut,
    uint64_t flags,
    SplitJulianDate* out_jd_ut,
    double* out_target_aspect_rad,
    size_t max_event_count,
    size_t* out_event_count,
    EphemerisEvalDiagnostic* diagnostic
) noexcept;

Status search_body_exact_aspects_tt(
    const NativeCalcContext* context,
    int body_a_id,
    int body_b_id,
    const double* aspect_separations_rad,
    size_t aspect_count,
    SplitJulianDate start_jd_tt,
    SplitJulianDate end_jd_tt,
    double max_step_days,
    uint64_t flags,
    SplitJulianDate* out_jd_tt,
    double* out_target_aspect_rad,
    size_t max_event_count,
    size_t* out_event_count,
    EphemerisEvalDiagnostic* diagnostic
) noexcept;

Status search_body_exact_aspects_auto_step_tt(
    const NativeCalcContext* context,
    int body_a_id,
    int body_b_id,
    const double* aspect_separations_rad,
    size_t aspect_count,
    SplitJulianDate start_jd_tt,
    SplitJulianDate end_jd_tt,
    uint64_t flags,
    SplitJulianDate* out_jd_tt,
    double* out_target_aspect_rad,
    size_t max_event_count,
    size_t* out_event_count,
    EphemerisEvalDiagnostic* diagnostic
) noexcept;

Status search_greatest_elongation_ut(
    const NativeCalcContext* context,
    int body_id,
    SplitJulianDate start_jd_ut,
    SplitJulianDate end_jd_ut,
    uint64_t flags,
    GreatestElongationSearchResult* out,
    EphemerisEvalDiagnostic* diagnostic
) noexcept;

Status search_minimum_angular_separation_ut(
    const NativeCalcContext* context,
    int body_a_id,
    int body_b_id,
    SplitJulianDate start_jd_ut,
    SplitJulianDate end_jd_ut,
    double max_step_days,
    uint64_t flags,
    AngularSeparationSearchResult* out,
    EphemerisEvalDiagnostic* diagnostic
) noexcept;

Status search_minimum_angular_separation_tt(
    const NativeCalcContext* context,
    int body_a_id,
    int body_b_id,
    SplitJulianDate start_jd_tt,
    SplitJulianDate end_jd_tt,
    double max_step_days,
    uint64_t flags,
    AngularSeparationSearchResult* out,
    EphemerisEvalDiagnostic* diagnostic
) noexcept;

// Search the minimum apparent angular separation between one solar-system
// body and a named star from the loaded TSC1/TSF1 catalog. Position flags use
// the same policy as the body-body minimum-separation entry points.
Status search_minimum_body_star_angular_separation_ut(
    const NativeCalcContext* context,
    int body_id,
    const char* star_key,
    SplitJulianDate start_jd_ut,
    SplitJulianDate end_jd_ut,
    double max_step_days,
    uint64_t flags,
    BodyStarAngularSeparationSearchResult* out,
    EphemerisEvalDiagnostic* diagnostic
) noexcept;

Status search_minimum_body_star_angular_separation_tt(
    const NativeCalcContext* context,
    int body_id,
    const char* star_key,
    SplitJulianDate start_jd_tt,
    SplitJulianDate end_jd_tt,
    double max_step_days,
    uint64_t flags,
    BodyStarAngularSeparationSearchResult* out,
    EphemerisEvalDiagnostic* diagnostic
) noexcept;

Status search_next_solar_transit_ut(
    const NativeCalcContext* context,
    int body_id,
    SplitJulianDate jd_start_ut,
    uint64_t flags,
    SolarTransitSearchResult* out,
    EphemerisEvalDiagnostic* diagnostic
) noexcept;

Status compute_local_solar_transit_ut(
    const NativeCalcContext* context,
    const SolarTransitSearchResult* global_transit,
    double longitude_deg,
    double latitude_deg,
    double height_m,
    uint64_t flags,
    LocalSolarTransitSearchResult* out,
    EphemerisEvalDiagnostic* diagnostic
) noexcept;

Status search_next_local_solar_transit_ut(
    const NativeCalcContext* context,
    int body_id,
    SplitJulianDate jd_start_ut,
    double longitude_deg,
    double latitude_deg,
    double height_m,
    uint64_t flags,
    LocalSolarTransitSearchResult* out,
    EphemerisEvalDiagnostic* diagnostic
) noexcept;

Status search_lunar_phase_crossings_ut(
    const NativeCalcContext* context,
    double phase_rad,
    SplitJulianDate start_jd_ut,
    SplitJulianDate end_jd_ut,
    double max_step_days,
    uint64_t flags,
    SplitJulianDate* out_jd_ut,
    size_t max_event_count,
    size_t* out_event_count,
    EphemerisEvalDiagnostic* diagnostic
) noexcept;

Status search_lunar_phase_crossings_default_step_ut(
    const NativeCalcContext* context,
    double phase_rad,
    SplitJulianDate start_jd_ut,
    SplitJulianDate end_jd_ut,
    uint64_t flags,
    SplitJulianDate* out_jd_ut,
    size_t max_event_count,
    size_t* out_event_count,
    EphemerisEvalDiagnostic* diagnostic
) noexcept;

Status search_lunar_phase_crossings_tt(
    const NativeCalcContext* context,
    double phase_rad,
    SplitJulianDate start_jd_tt,
    SplitJulianDate end_jd_tt,
    double max_step_days,
    uint64_t flags,
    SplitJulianDate* out_jd_tt,
    size_t max_event_count,
    size_t* out_event_count,
    EphemerisEvalDiagnostic* diagnostic
) noexcept;

Status search_lunar_phase_crossings_default_step_tt(
    const NativeCalcContext* context,
    double phase_rad,
    SplitJulianDate start_jd_tt,
    SplitJulianDate end_jd_tt,
    uint64_t flags,
    SplitJulianDate* out_jd_tt,
    size_t max_event_count,
    size_t* out_event_count,
    EphemerisEvalDiagnostic* diagnostic
) noexcept;

}  // namespace runtime
}  // namespace taiyin

#endif  // TAIYIN_RUNTIME_EVENT_SEARCH_H
