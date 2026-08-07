#ifndef TAIYIN_RUNTIME_OCCULTATION_SEARCH_H
#define TAIYIN_RUNTIME_OCCULTATION_SEARCH_H

#include "taiyin/runtime/native_position.h"
#include "taiyin/status.h"

#include <stdint.h>

namespace taiyin {
namespace runtime {

const uint64_t TAIYIN_OCCULTATION_POSITION_FLAGS_MASK = 0x00000000ffffffffull;
const uint64_t TAIYIN_OCCULTATION_OPTION_FLAGS_MASK = 0xffffffff00000000ull;

const uint64_t TAIYIN_OCCULTATION_SEARCH_TRUEPOS =
    static_cast<uint64_t>(TAIYIN_NATIVE_POSITION_TRUEPOS);
const uint64_t TAIYIN_OCCULTATION_SEARCH_BACKWARD = 1ull << 32;
const uint64_t TAIYIN_OCCULTATION_SEARCH_ONE_CANDIDATE = 1ull << 33;
const uint64_t TAIYIN_OCCULTATION_VISIBILITY_REFRACTION = 1ull << 34;
const uint64_t TAIYIN_OCCULTATION_FILTER_PARTIAL = 1ull << 40;
const uint64_t TAIYIN_OCCULTATION_FILTER_TOTAL = 1ull << 41;
const uint64_t TAIYIN_OCCULTATION_FILTER_GRAZING = 1ull << 42;
const uint64_t TAIYIN_OCCULTATION_FILTER_CENTRAL = 1ull << 43;
const uint64_t TAIYIN_OCCULTATION_FILTER_NONCENTRAL = 1ull << 44;
// Refine final lunar radius and contacts with the globally loaded TLL1 model.
const uint64_t TAIYIN_OCCULTATION_LUNAR_LIMB_CORRECTION = 1ull << 45;

const int TAIYIN_OCCULTATION_KIND_NONE = 0;
const int TAIYIN_OCCULTATION_KIND_LUNAR_STAR = 1;
const int TAIYIN_OCCULTATION_KIND_LUNAR_BODY = 2;

const uint32_t TAIYIN_OCCULTATION_TYPE_PARTIAL = 1u << 0;
const uint32_t TAIYIN_OCCULTATION_TYPE_TOTAL = 1u << 1;
const uint32_t TAIYIN_OCCULTATION_TYPE_ANNULAR = 1u << 2;
const uint32_t TAIYIN_OCCULTATION_TYPE_GRAZING = 1u << 3;
const uint32_t TAIYIN_OCCULTATION_TYPE_CENTRAL = 1u << 4;
const uint32_t TAIYIN_OCCULTATION_TYPE_NONCENTRAL = 1u << 5;
const uint32_t TAIYIN_OCCULTATION_TYPE_CENTRALITY_UNAVAILABLE = 1u << 6;

const uint32_t TAIYIN_OCCULTATION_VISIBILITY_SAMPLE_MOON_ABOVE_HORIZON = 1u << 0;
const uint32_t TAIYIN_OCCULTATION_VISIBILITY_SAMPLE_TARGET_ABOVE_HORIZON = 1u << 1;
const uint32_t TAIYIN_OCCULTATION_VISIBILITY_SAMPLE_SUN_BELOW_HORIZON = 1u << 2;
const uint32_t TAIYIN_OCCULTATION_VISIBILITY_SAMPLE_GEOMETRICALLY_VISIBLE =
    TAIYIN_OCCULTATION_VISIBILITY_SAMPLE_MOON_ABOVE_HORIZON
    | TAIYIN_OCCULTATION_VISIBILITY_SAMPLE_TARGET_ABOVE_HORIZON;
const uint32_t TAIYIN_OCCULTATION_VISIBILITY_SAMPLE_DARK_SKY_VISIBLE =
    TAIYIN_OCCULTATION_VISIBILITY_SAMPLE_GEOMETRICALLY_VISIBLE
    | TAIYIN_OCCULTATION_VISIBILITY_SAMPLE_SUN_BELOW_HORIZON;

const uint32_t TAIYIN_OCCULTATION_VISIBILITY_HAS_VISIBLE_SAMPLE = 1u << 0;
const uint32_t TAIYIN_OCCULTATION_VISIBILITY_MAXIMUM_VISIBLE = 1u << 1;
const uint32_t TAIYIN_OCCULTATION_VISIBILITY_HAS_DARK_SAMPLE = 1u << 2;
const uint32_t TAIYIN_OCCULTATION_VISIBILITY_MAXIMUM_DARK = 1u << 3;
const uint32_t TAIYIN_OCCULTATION_VISIBILITY_HAS_VISIBLE_INTERVAL = 1u << 4;
const uint32_t TAIYIN_OCCULTATION_VISIBILITY_HAS_DARK_INTERVAL = 1u << 5;

const int TAIYIN_OCCULTATION_WHERE_MAX_PATH_POINTS = 16;
const int TAIYIN_OCCULTATION_WHERE_MAX_POLYGON_POINTS = 32;
const int TAIYIN_OCCULTATION_MAX_VISIBILITY_INTERVALS = 8;

struct LunarOccultationPhenomena {
    double angular_distance_rad;
    double diameter_ratio;
    double magnitude;
    double obscuration;
    double occulted_fraction;

    LunarOccultationPhenomena() noexcept;
};

struct LunarOccultationVisibilityInterval {
    int valid;
    SplitJulianDate begin_jd_ut;
    SplitJulianDate end_jd_ut;

    LunarOccultationVisibilityInterval() noexcept;
};

struct LunarStarOccultationSearchResult {
    int kind;
    uint32_t type_flags;
    SplitJulianDate jd_ut;
    SplitJulianDate begin_jd_ut;
    SplitJulianDate end_jd_ut;
    SplitJulianDate first_contact_jd_ut;
    SplitJulianDate second_contact_jd_ut;
    SplitJulianDate third_contact_jd_ut;
    SplitJulianDate fourth_contact_jd_ut;
    double separation_rad;
    double moon_radius_rad;
    double target_radius_rad;
    double margin_rad;
    LunarOccultationPhenomena phenomena;
    SplitJulianDate candidate_jd_ut;
    SplitJulianDate next_search_jd_ut;
    int candidate_count;
    int iteration_count;
    int evaluation_count;

    LunarStarOccultationSearchResult() noexcept;
};

using LunarBodyOccultationSearchResult = LunarStarOccultationSearchResult;

struct LunarOccultationLocalVisibilitySample {
    int valid;
    SplitJulianDate jd_ut;
    double moon_altitude_rad;
    double moon_azimuth_rad;
    double target_altitude_rad;
    double target_azimuth_rad;
    double sun_altitude_rad;
    double sun_azimuth_rad;
    uint32_t visibility_flags;

    LunarOccultationLocalVisibilitySample() noexcept;
};

struct LunarOccultationLocalVisibility {
    LunarOccultationLocalVisibilitySample first_contact;
    LunarOccultationLocalVisibilitySample second_contact;
    LunarOccultationLocalVisibilitySample maximum;
    LunarOccultationLocalVisibilitySample third_contact;
    LunarOccultationLocalVisibilitySample fourth_contact;
    SplitJulianDate target_rise_jd_ut;
    SplitJulianDate target_set_jd_ut;
    SplitJulianDate visible_begin_jd_ut;
    SplitJulianDate visible_end_jd_ut;
    SplitJulianDate dark_visible_begin_jd_ut;
    SplitJulianDate dark_visible_end_jd_ut;
    int visible_interval_count;
    LunarOccultationVisibilityInterval visible_intervals[TAIYIN_OCCULTATION_MAX_VISIBILITY_INTERVALS];
    int dark_visible_interval_count;
    LunarOccultationVisibilityInterval dark_visible_intervals[TAIYIN_OCCULTATION_MAX_VISIBILITY_INTERVALS];
    uint32_t visibility_flags;

    LunarOccultationLocalVisibility() noexcept;
};

struct LunarOccultationWherePathPoint {
    int valid;
    SplitJulianDate jd_ut;
    double longitude_deg;
    double latitude_deg;
    double height_m;

    LunarOccultationWherePathPoint() noexcept;
};

struct LunarOccultationWhereResult {
    int center_line_hits_earth;
    uint32_t type_flags;
    SplitJulianDate jd_ut;
    SplitJulianDate center_line_begin_jd_ut;
    SplitJulianDate center_line_end_jd_ut;
    int center_line_path_count;
    LunarOccultationWherePathPoint center_line_path[TAIYIN_OCCULTATION_WHERE_MAX_PATH_POINTS];
    double center_line_min_longitude_deg;
    double center_line_max_longitude_deg;
    double center_line_min_latitude_deg;
    double center_line_max_latitude_deg;
    double center_line_path_distance_km;
    int outer_limit_path_count;
    LunarOccultationWherePathPoint outer_north_path[TAIYIN_OCCULTATION_WHERE_MAX_PATH_POINTS];
    LunarOccultationWherePathPoint outer_south_path[TAIYIN_OCCULTATION_WHERE_MAX_PATH_POINTS];
    double outer_limit_mean_width_km;
    double outer_limit_max_width_km;
    int visible_region_polygon_count;
    LunarOccultationWherePathPoint visible_region_polygon[TAIYIN_OCCULTATION_WHERE_MAX_POLYGON_POINTS];
    double visible_region_min_longitude_deg;
    double visible_region_max_longitude_deg;
    double visible_region_min_latitude_deg;
    double visible_region_max_latitude_deg;
    double longitude_deg;
    double latitude_deg;
    double height_m;
    double separation_rad;
    double moon_radius_rad;
    double target_radius_rad;
    double margin_rad;
    LunarOccultationPhenomena phenomena;
    LunarOccultationLocalVisibilitySample local_sample;
    uint32_t visibility_flags;

    LunarOccultationWhereResult() noexcept;
};

Status search_next_geocentric_lunar_star_occultation_ut(
    const NativeCalcContext* context,
    const char* star_key,
    SplitJulianDate jd_start_ut,
    uint64_t flags,
    LunarStarOccultationSearchResult* out,
    EphemerisEvalDiagnostic* diagnostic = nullptr
) noexcept;

Status search_next_local_lunar_star_occultation_ut(
    const NativeCalcContext* context,
    const char* star_key,
    SplitJulianDate jd_start_ut,
    uint64_t flags,
    LunarStarOccultationSearchResult* out,
    EphemerisEvalDiagnostic* diagnostic = nullptr
) noexcept;

Status search_next_geocentric_lunar_body_occultation_ut(
    const NativeCalcContext* context,
    int body_id,
    SplitJulianDate jd_start_ut,
    uint64_t flags,
    LunarBodyOccultationSearchResult* out,
    EphemerisEvalDiagnostic* diagnostic = nullptr
) noexcept;

Status search_next_geocentric_lunar_body_occultation_ut(
    const NativeCalcContext* context,
    int body_id,
    double target_radius_km,
    SplitJulianDate jd_start_ut,
    uint64_t flags,
    LunarBodyOccultationSearchResult* out,
    EphemerisEvalDiagnostic* diagnostic = nullptr
) noexcept;

Status search_next_local_lunar_body_occultation_ut(
    const NativeCalcContext* context,
    int body_id,
    SplitJulianDate jd_start_ut,
    uint64_t flags,
    LunarBodyOccultationSearchResult* out,
    EphemerisEvalDiagnostic* diagnostic = nullptr
) noexcept;

Status search_next_local_lunar_body_occultation_ut(
    const NativeCalcContext* context,
    int body_id,
    double target_radius_km,
    SplitJulianDate jd_start_ut,
    uint64_t flags,
    LunarBodyOccultationSearchResult* out,
    EphemerisEvalDiagnostic* diagnostic = nullptr
) noexcept;

Status compute_lunar_star_occultation_local_visibility_ut(
    const NativeCalcContext* context,
    const char* star_key,
    const LunarStarOccultationSearchResult* occultation,
    uint64_t visibility_flags,
    LunarOccultationLocalVisibility* out,
    EphemerisEvalDiagnostic* diagnostic = nullptr
) noexcept;

Status compute_lunar_body_occultation_local_visibility_ut(
    const NativeCalcContext* context,
    int body_id,
    const LunarBodyOccultationSearchResult* occultation,
    uint64_t visibility_flags,
    LunarOccultationLocalVisibility* out,
    EphemerisEvalDiagnostic* diagnostic = nullptr
) noexcept;

Status compute_lunar_star_occultation_where_ut(
    const NativeCalcContext* context,
    const char* star_key,
    const LunarStarOccultationSearchResult* occultation,
    uint64_t flags,
    LunarOccultationWhereResult* out,
    EphemerisEvalDiagnostic* diagnostic = nullptr
) noexcept;

Status compute_lunar_body_occultation_where_ut(
    const NativeCalcContext* context,
    int body_id,
    const LunarBodyOccultationSearchResult* occultation,
    uint64_t flags,
    LunarOccultationWhereResult* out,
    EphemerisEvalDiagnostic* diagnostic = nullptr
) noexcept;

Status compute_lunar_body_occultation_where_ut(
    const NativeCalcContext* context,
    int body_id,
    double target_radius_km,
    const LunarBodyOccultationSearchResult* occultation,
    uint64_t flags,
    LunarOccultationWhereResult* out,
    EphemerisEvalDiagnostic* diagnostic = nullptr
) noexcept;

}  // namespace runtime
}  // namespace taiyin

#endif  // TAIYIN_RUNTIME_OCCULTATION_SEARCH_H
