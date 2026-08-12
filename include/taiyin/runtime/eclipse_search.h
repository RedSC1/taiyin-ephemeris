#ifndef TAIYIN_RUNTIME_ECLIPSE_SEARCH_H
#define TAIYIN_RUNTIME_ECLIPSE_SEARCH_H

#include "taiyin/dispatch.h"
#include "taiyin/runtime/native_position.h"
#include "taiyin/runtime/solar_visibility.h"
#include "taiyin/status.h"

#include <stddef.h>
#include <stdint.h>

namespace taiyin {
namespace runtime {

// ---------------------------------------------------------------------------
// Eclipse kind bitmask
//
// Dual role (mirrors Swiss SE_ECL_* in swephexp.h:307-314):
//   - As kind_filter input: 0 = all kinds, or OR of desired kinds.
//   - As LunarEclipseResult.kind output: the actual classification.
// ---------------------------------------------------------------------------
const uint32_t TAIYIN_ECLIPSE_NONE      = 0;
const uint32_t TAIYIN_ECLIPSE_PENUMBRAL = 1u << 0;
const uint32_t TAIYIN_ECLIPSE_PARTIAL   = 1u << 1;
const uint32_t TAIYIN_ECLIPSE_TOTAL     = 1u << 2;
const uint32_t TAIYIN_ECLIPSE_ANNULAR   = 1u << 3;
const uint32_t TAIYIN_ECLIPSE_HYBRID    = 1u << 4;
const uint32_t TAIYIN_ECLIPSE_CENTRAL   = 1u << 5;
const uint32_t TAIYIN_ECLIPSE_NONCENTRAL = 1u << 6;
const uint32_t TAIYIN_ECLIPSE_VISIBLE_AT_OBSERVER = 1u << 7;
const uint32_t TAIYIN_ECLIPSE_MAXIMUM_VISIBLE = 1u << 8;
const uint32_t TAIYIN_ECLIPSE_PARTIAL_BEGIN_VISIBLE = 1u << 9;
const uint32_t TAIYIN_ECLIPSE_TOTAL_BEGIN_VISIBLE = 1u << 10;
const uint32_t TAIYIN_ECLIPSE_TOTAL_END_VISIBLE = 1u << 11;
const uint32_t TAIYIN_ECLIPSE_PARTIAL_END_VISIBLE = 1u << 12;
const uint32_t TAIYIN_ECLIPSE_PENUMBRAL_BEGIN_VISIBLE = 1u << 13;
const uint32_t TAIYIN_ECLIPSE_PENUMBRAL_END_VISIBLE = 1u << 14;
const uint32_t TAIYIN_ECLIPSE_OCCULTATION_BEGIN_IN_DAYLIGHT = 1u << 15;
const uint32_t TAIYIN_ECLIPSE_OCCULTATION_END_IN_DAYLIGHT = 1u << 16;
const uint32_t TAIYIN_ECLIPSE_ALL_LUNAR =
    TAIYIN_ECLIPSE_PENUMBRAL |
    TAIYIN_ECLIPSE_PARTIAL |
    TAIYIN_ECLIPSE_TOTAL;
const uint32_t TAIYIN_ECLIPSE_ALL_SOLAR =
    TAIYIN_ECLIPSE_PARTIAL |
    TAIYIN_ECLIPSE_TOTAL |
    TAIYIN_ECLIPSE_ANNULAR |
    TAIYIN_ECLIPSE_HYBRID;

// ---------------------------------------------------------------------------
// Eclipse search flags
//
// Only true/false-style options use flags.  Mutually-exclusive model
// selections (shadow model, moon radius model) are read from the
// NativeCalcContext, following the codebase convention (dispatch.h enums +
// NativeCalcContext model_id fields).
//
// Low 32 bits: native position semantic flags, same convention as
// event_search.h and occultation_search.h. Output-shape flags such as XYZ,
// SPEED, EQUATORIAL, RADIANS, and TOPOCENTRIC are not accepted by eclipse
// APIs because eclipse result shapes are fixed.
//
// High 32 bits: eclipse-specific boolean option flags.
// ---------------------------------------------------------------------------

const uint64_t TAIYIN_ECLIPSE_POSITION_FLAGS_MASK = 0xffffffffull;
const uint64_t TAIYIN_ECLIPSE_OPTION_FLAGS_MASK = ~TAIYIN_ECLIPSE_POSITION_FLAGS_MASK;

const uint32_t TAIYIN_ECLIPSE_SUPPORTED_POSITION_FLAGS =
    TAIYIN_NATIVE_POSITION_TRUEPOS;

const size_t TAIYIN_SOLAR_ROUTE_DEFAULT_SAMPLE_COUNT = 400;
const size_t TAIYIN_SOLAR_ROUTE_MIN_SAMPLE_COUNT = 32;
const size_t TAIYIN_SOLAR_ROUTE_MAX_SAMPLE_COUNT = 4096;

// Compatibility alias. True position is a native position semantic, not an
// eclipse-only high-bit option.
const uint64_t TAIYIN_ECLIPSE_TRUEPOS = TAIYIN_NATIVE_POSITION_TRUEPOS;

// Contact output (none set = do not compute contacts, the default).
const uint64_t TAIYIN_ECLIPSE_INCLUDE_CONTACTS = 1ull << 33;

// Penumbral eclipse filtering (none set = include penumbral, the default).
const uint64_t TAIYIN_ECLIPSE_EXCLUDE_PENUMBRAL = 1ull << 34;

// Backward search (none set = forward, the default).
const uint64_t TAIYIN_ECLIPSE_BACKWARD = 1ull << 35;

// Local solar-eclipse visibility-window semantics. LOCAL_REFRACTION selects the
// apparent (refracted) rise/set window; LOCAL_STRICT_METEOROLOGY is only valid
// together with LOCAL_REFRACTION and forbids the standard-atmosphere fallback.
// Neither set (the default) keeps the geometric rise/set window. Refraction
// only shifts the window times; sunrise/sunset magnitude always uses the normal
// eclipse geometry at that instant, so the magnitude definition never changes.
const uint64_t TAIYIN_ECLIPSE_LOCAL_STRICT_METEOROLOGY = 1ull << 32;
const uint64_t TAIYIN_ECLIPSE_LOCAL_REFRACTION = 1ull << 37;

// Use the direction-dependent lunar-limb model attached to NativeCalcContext
// when polishing eclipse contact times. The option is explicit: attaching a
// model alone does not change the default circular-limb calculation.
// Bit 36 was used by a pre-release TRUEPOS layout; leave it rejected rather
// than silently reinterpreting persisted flags from an old binding.
const uint64_t TAIYIN_ECLIPSE_LUNAR_LIMB_CORRECTION = 1ull << 38;

const uint64_t TAIYIN_ECLIPSE_KNOWN_FLAGS =
    static_cast<uint64_t>(TAIYIN_ECLIPSE_SUPPORTED_POSITION_FLAGS)
    | TAIYIN_ECLIPSE_INCLUDE_CONTACTS
    | TAIYIN_ECLIPSE_EXCLUDE_PENUMBRAL
    | TAIYIN_ECLIPSE_BACKWARD
    | TAIYIN_ECLIPSE_LUNAR_LIMB_CORRECTION;

const uint64_t TAIYIN_LOCAL_SOLAR_ECLIPSE_KNOWN_FLAGS =
    TAIYIN_ECLIPSE_KNOWN_FLAGS
    | TAIYIN_ECLIPSE_LOCAL_STRICT_METEOROLOGY
    | TAIYIN_ECLIPSE_LOCAL_REFRACTION;

inline bool valid_eclipse_flags(uint64_t flags) noexcept {
    return (flags & ~TAIYIN_ECLIPSE_KNOWN_FLAGS) == 0u;
}

// Maps the local-eclipse visibility flag bits to the fast solar rise/set
// visibility flag combination. STRICT_METEOROLOGY without LOCAL_REFRACTION is
// invalid; the default (neither set) is the geometric NO_REFRACTION window.
inline bool resolve_local_eclipse_fast_visibility_flags(
    uint64_t eclipse_flags,
    uint64_t* out_fast_visibility_flags
) noexcept {
    if (!out_fast_visibility_flags) return false;
    const bool refraction = (eclipse_flags & TAIYIN_ECLIPSE_LOCAL_REFRACTION) != 0u;
    const bool strict = (eclipse_flags & TAIYIN_ECLIPSE_LOCAL_STRICT_METEOROLOGY) != 0u;
    if (strict && !refraction) return false;
    if (!refraction) {
        *out_fast_visibility_flags = TAIYIN_SOLAR_VISIBILITY_FLAG_NO_REFRACTION;
        return true;
    }
    *out_fast_visibility_flags = TAIYIN_SOLAR_VISIBILITY_FLAG_REFRACTION
        | (strict ? TAIYIN_SOLAR_VISIBILITY_STRICT_METEOROLOGY : 0u);
    return true;
}

inline bool valid_local_solar_eclipse_flags(uint64_t flags) noexcept {
    uint64_t fast_visibility_flags = 0u;
    return (flags & ~TAIYIN_LOCAL_SOLAR_ECLIPSE_KNOWN_FLAGS) == 0u
        && resolve_local_eclipse_fast_visibility_flags(flags, &fast_visibility_flags);
}

const uint64_t TAIYIN_SOLAR_ECLIPSE_ROUTE_KNOWN_FLAGS =
    TAIYIN_ECLIPSE_TRUEPOS | TAIYIN_ECLIPSE_LUNAR_LIMB_CORRECTION;

inline bool valid_solar_eclipse_route_flags(uint64_t flags) noexcept {
    return (flags & ~TAIYIN_SOLAR_ECLIPSE_ROUTE_KNOWN_FLAGS) == 0u;
}

// ---------------------------------------------------------------------------
// Contact kind indices
//
// Contact times are stored in LunarEclipseResult.contact_jd_tt[index].
// NAN means the contact does not apply (e.g. U2/U3 for a partial eclipse).
// Naming follows the NASA / astronomical almanac convention, matching
// taiyin-ephemeris-ts/src/events/types.ts:85.
// ---------------------------------------------------------------------------
const size_t TAIYIN_LUNAR_ECLIPSE_CONTACT_P1       = 0;  // penumbral begin
const size_t TAIYIN_LUNAR_ECLIPSE_CONTACT_U1       = 1;  // umbral (partial) begin
const size_t TAIYIN_LUNAR_ECLIPSE_CONTACT_U2       = 2;  // totality begin
const size_t TAIYIN_LUNAR_ECLIPSE_CONTACT_GREATEST = 3;  // maximum eclipse
const size_t TAIYIN_LUNAR_ECLIPSE_CONTACT_U3       = 4;  // totality end
const size_t TAIYIN_LUNAR_ECLIPSE_CONTACT_U4       = 5;  // umbral (partial) end
const size_t TAIYIN_LUNAR_ECLIPSE_CONTACT_P4       = 6;  // penumbral end
const size_t TAIYIN_LUNAR_ECLIPSE_CONTACT_COUNT    = 7;

const size_t TAIYIN_SOLAR_ECLIPSE_CONTACT_P1       = 0;  // global partial begin
const size_t TAIYIN_SOLAR_ECLIPSE_CONTACT_C1       = 1;  // central line begin
const size_t TAIYIN_SOLAR_ECLIPSE_CONTACT_GREATEST = 2;  // greatest eclipse
const size_t TAIYIN_SOLAR_ECLIPSE_CONTACT_C4       = 3;  // central line end
const size_t TAIYIN_SOLAR_ECLIPSE_CONTACT_P4       = 4;  // global partial end
const size_t TAIYIN_SOLAR_ECLIPSE_CONTACT_COUNT    = 5;

// ---------------------------------------------------------------------------
// Lunar eclipse result
//
// Filled by solve_lunar_eclipse_at and search_next_lunar_eclipse.
// All JD values are in TT (terrestrial time).
// ---------------------------------------------------------------------------
struct LunarEclipseResult {
    // Classification (TAIYIN_ECLIPSE_* bitmask)
    uint32_t kind;

    // Maximum eclipse (食甚)
    SplitJulianDate maximum_jd_tt;

    // Magnitudes (食分)
    // Formula (taiyin-ephemeris-ts/src/events/eclipse.ts:253-254):
    //   umbral_magnitude    = (umbra_radius + moon_radius - rho) / (2 * moon_radius)
    //   penumbral_magnitude = (penumbra_radius + moon_radius - rho) / (2 * moon_radius)
    // Negative means no eclipse of that type.
    double umbral_magnitude;
    double penumbral_magnitude;

    // Geometry at maximum eclipse (radians)
    double axis_distance_rad;       // Moon center to shadow axis (rho)
    double umbra_radius_rad;        // umbra angular radius at Moon distance
    double penumbra_radius_rad;     // penumbra angular radius at Moon distance
    double moon_radius_rad;         // Moon angular radius

    // Contact times (JD TT). NAN when not applicable.
    // Populated only when TAIYIN_ECLIPSE_INCLUDE_CONTACTS is set.
    SplitJulianDate contact_jd_tt[TAIYIN_LUNAR_ECLIPSE_CONTACT_COUNT];
};

// UT-facing result.  Geometry and magnitudes are identical to
// LunarEclipseResult; only the time fields are converted to UT.
struct LunarEclipseResultUt {
    uint32_t kind;
    SplitJulianDate maximum_jd_ut;
    double delta_t_seconds;
    double umbral_magnitude;
    double penumbral_magnitude;
    double axis_distance_rad;
    double umbra_radius_rad;
    double penumbra_radius_rad;
    double moon_radius_rad;
    SplitJulianDate contact_jd_ut[TAIYIN_LUNAR_ECLIPSE_CONTACT_COUNT];
};

// Local lunar-eclipse visibility samples the Moon center altitude at the
// global lunar-eclipse contacts.  Visibility flags use the existing
// TAIYIN_ECLIPSE_*_VISIBLE bits above.
const uint64_t TAIYIN_LOCAL_LUNAR_ECLIPSE_REFRACTION = 1ull << 37;

struct LocalLunarEclipseResultUt {
    uint32_t eclipse_kind;
    uint32_t visibility_flags;
    SplitJulianDate maximum_jd_ut;
    double delta_t_seconds;
    double umbral_magnitude;
    double penumbral_magnitude;
    SplitJulianDate contact_jd_ut[TAIYIN_LUNAR_ECLIPSE_CONTACT_COUNT];
    double contact_moon_altitude_deg[TAIYIN_LUNAR_ECLIPSE_CONTACT_COUNT];
    double contact_moon_azimuth_deg[TAIYIN_LUNAR_ECLIPSE_CONTACT_COUNT];
    SplitJulianDate moonrise_jd_ut;
    SplitJulianDate moonset_jd_ut;
};

struct LocalLunarEclipseResult {
    uint32_t eclipse_kind;
    uint32_t visibility_flags;
    SplitJulianDate maximum_jd_tt;
    double umbral_magnitude;
    double penumbral_magnitude;
    SplitJulianDate contact_jd_tt[TAIYIN_LUNAR_ECLIPSE_CONTACT_COUNT];
    double contact_moon_altitude_deg[TAIYIN_LUNAR_ECLIPSE_CONTACT_COUNT];
    double contact_moon_azimuth_deg[TAIYIN_LUNAR_ECLIPSE_CONTACT_COUNT];
    SplitJulianDate moonrise_jd_tt;
    SplitJulianDate moonset_jd_tt;
};

// Global solar eclipse result.  This first-stage API describes whether the
// Moon's shadow reaches Earth and the global maximum time.  Local circumstances
// and path/limit curves are intentionally not included here.
struct SolarEclipseResult {
    uint32_t kind;
    SplitJulianDate maximum_jd_tt;
    double axis_distance_km;
    double penumbra_radius_km;
    double core_radius_km;      // positive umbra, negative antumbra
    // Signed penumbral clearance in the WGS84 ellipsoid-normalized radial
    // metric. Negative values mean that the penumbral cone intersects Earth.
    double penumbral_margin_km;
    double central_margin_km;   // axis_distance - Earth radius
    double maximum_latitude_deg;
    double maximum_longitude_deg;
    SplitJulianDate contact_jd_tt[TAIYIN_SOLAR_ECLIPSE_CONTACT_COUNT];
};

struct SolarEclipseResultUt {
    uint32_t kind;
    SplitJulianDate maximum_jd_ut;
    double delta_t_seconds;
    double axis_distance_km;
    double penumbra_radius_km;
    double core_radius_km;
    double penumbral_margin_km;
    double central_margin_km;
    double maximum_latitude_deg;
    double maximum_longitude_deg;
    SplitJulianDate contact_jd_ut[TAIYIN_SOLAR_ECLIPSE_CONTACT_COUNT];
};

const size_t TAIYIN_LOCAL_SOLAR_CONTACT_C1 = 0;
const size_t TAIYIN_LOCAL_SOLAR_CONTACT_C2 = 1;
const size_t TAIYIN_LOCAL_SOLAR_CONTACT_C3 = 2;
const size_t TAIYIN_LOCAL_SOLAR_CONTACT_C4 = 3;
const size_t TAIYIN_LOCAL_SOLAR_CONTACT_GREATEST = 4;
const size_t TAIYIN_LOCAL_SOLAR_CONTACT_COUNT = 5;

struct LocalSolarEclipseCircumstances {
    SplitJulianDate jd_tt;
    double magnitude;
    double obscuration;
    double center_separation_deg;
    double sun_angular_radius_deg;
    double moon_angular_radius_deg;
    double sun_altitude_deg;
    double sun_azimuth_deg;
};

struct LocalSolarEclipseCircumstancesUt {
    SplitJulianDate jd_ut;
    double delta_t_seconds;
    double magnitude;
    double obscuration;
    double center_separation_deg;
    double sun_angular_radius_deg;
    double moon_angular_radius_deg;
    double sun_altitude_deg;
    double sun_azimuth_deg;
};

struct LocalSolarEclipseResult {
    uint32_t kind;
    SplitJulianDate maximum_jd_tt;
    double magnitude;
    double obscuration;
    double sun_altitude_deg;
    double sun_azimuth_deg;
    SplitJulianDate contact_jd_tt[TAIYIN_LOCAL_SOLAR_CONTACT_COUNT];
    double position_angle_c1_deg;
    double position_angle_c4_deg;
    double vertex_angle_c1_deg;
    double vertex_angle_c4_deg;
    double sunrise_magnitude;
    double sunset_magnitude;
    double duration_seconds;
    double moon_sun_radius_ratio;
};

struct LocalSolarEclipseResultUt {
    uint32_t kind;
    SplitJulianDate maximum_jd_ut;
    double delta_t_seconds;
    double magnitude;
    double obscuration;
    double sun_altitude_deg;
    double sun_azimuth_deg;
    SplitJulianDate contact_jd_ut[TAIYIN_LOCAL_SOLAR_CONTACT_COUNT];
    double position_angle_c1_deg;
    double position_angle_c4_deg;
    double vertex_angle_c1_deg;
    double vertex_angle_c4_deg;
    double sunrise_magnitude;
    double sunset_magnitude;
    double duration_seconds;
    double moon_sun_radius_ratio;
};

struct LocalSolarEclipseBoundary {
    double center_longitude_deg;
    double center_latitude_deg;
    uint32_t center_kind;
    double umbra_north_longitude_deg;
    double umbra_north_latitude_deg;
    double umbra_south_longitude_deg;
    double umbra_south_latitude_deg;
    double penumbra_north_longitude_deg;
    double penumbra_north_latitude_deg;
    double penumbra_south_longitude_deg;
    double penumbra_south_latitude_deg;
    double umbra_width_km;
};

struct SolarEclipsePathPoint {
    SplitJulianDate jd_tt;
    SplitJulianDate jd_ut;
    double latitude_deg;
    double longitude_deg;
    double elevation_m;
    double sun_altitude_deg;
    double sun_azimuth_deg;
};

struct SolarEclipseRouteRow {
    SplitJulianDate jd_tt;
    SplitJulianDate jd_ut;
    SolarEclipsePathPoint center_line;
    SolarEclipsePathPoint penumbral_north_limit;
    SolarEclipsePathPoint penumbral_south_limit;
    SolarEclipsePathPoint north_limit;
    SolarEclipsePathPoint south_limit;
    SolarEclipsePathPoint half_magnitude_north_limit;
    SolarEclipsePathPoint half_magnitude_south_limit;
    // Center-line-normal width of the closed core path, including horizon
    // caps near path emergence/disappearance.
    double path_width_km;
    double duration_seconds;
    double sun_altitude_deg;
    double sun_azimuth_deg;
};

enum SolarEclipseRouteCurveKind {
    // Contact, sunrise/sunset maximum, center, and shadow-limit curves used by
    // complete eclipse-map products.
    TAIYIN_SOLAR_ROUTE_CURVE_PARTIAL_BEGIN_A = 0,
    TAIYIN_SOLAR_ROUTE_CURVE_PARTIAL_BEGIN_B = 1,
    TAIYIN_SOLAR_ROUTE_CURVE_PARTIAL_END_A = 2,
    TAIYIN_SOLAR_ROUTE_CURVE_PARTIAL_END_B = 3,
    TAIYIN_SOLAR_ROUTE_CURVE_SUNRISE_MAX_A = 4,
    TAIYIN_SOLAR_ROUTE_CURVE_SUNRISE_MAX_B = 5,
    TAIYIN_SOLAR_ROUTE_CURVE_SUNSET_MAX_A = 6,
    TAIYIN_SOLAR_ROUTE_CURVE_SUNSET_MAX_B = 7,
    TAIYIN_SOLAR_ROUTE_CURVE_CENTER_LINE = 8,
    TAIYIN_SOLAR_ROUTE_CURVE_PENUMBRAL_NORTH = 9,
    TAIYIN_SOLAR_ROUTE_CURVE_PENUMBRAL_SOUTH = 10,
    TAIYIN_SOLAR_ROUTE_CURVE_CORE_NORTH = 11,
    TAIYIN_SOLAR_ROUTE_CURVE_CORE_SOUTH = 12,
    TAIYIN_SOLAR_ROUTE_CURVE_HALF_MAGNITUDE_NORTH = 13,
    TAIYIN_SOLAR_ROUTE_CURVE_HALF_MAGNITUDE_SOUTH = 14,
    TAIYIN_SOLAR_ROUTE_CURVE_UMBRA_OUTLINE = 15,
    TAIYIN_SOLAR_ROUTE_CURVE_PENUMBRA_OUTLINE = 16,
    TAIYIN_SOLAR_ROUTE_CURVE_TERMINATOR = 17,
    TAIYIN_SOLAR_ROUTE_CURVE_CORE_BEGIN_HORIZON = 18,
    TAIYIN_SOLAR_ROUTE_CURVE_CORE_END_HORIZON = 19,
    TAIYIN_SOLAR_ROUTE_CURVE_HALF_MAGNITUDE_SUNRISE_A = 20,
    TAIYIN_SOLAR_ROUTE_CURVE_HALF_MAGNITUDE_SUNRISE_B = 21,
    TAIYIN_SOLAR_ROUTE_CURVE_HALF_MAGNITUDE_SUNSET_A = 22,
    TAIYIN_SOLAR_ROUTE_CURVE_HALF_MAGNITUDE_SUNSET_B = 23,
};

struct SolarEclipseRouteCurvePoint {
    SplitJulianDate jd_tt;
    SplitJulianDate jd_ut;
    uint32_t curve_kind;
    double latitude_deg;
    double longitude_deg;
};

enum SolarEclipseRouteProductFlags {
    TAIYIN_SOLAR_ROUTE_PRODUCT_HAS_CENTER_LINE = 1u << 0,
    TAIYIN_SOLAR_ROUTE_PRODUCT_HAS_CORE_LIMITS = 1u << 1,
    TAIYIN_SOLAR_ROUTE_PRODUCT_HAS_PENUMBRAL_LIMITS = 1u << 2,
    TAIYIN_SOLAR_ROUTE_PRODUCT_HAS_CORE_POLYGON = 1u << 3,
    TAIYIN_SOLAR_ROUTE_PRODUCT_CROSSES_ANTIMERIDIAN = 1u << 4,
    TAIYIN_SOLAR_ROUTE_PRODUCT_HAS_HALF_MAGNITUDE_LIMITS = 1u << 5,
    TAIYIN_SOLAR_ROUTE_PRODUCT_HAS_PENUMBRAL_POLYGON = 1u << 6,
    TAIYIN_SOLAR_ROUTE_PRODUCT_HAS_HALF_MAGNITUDE_POLYGON = 1u << 7,
};

enum SolarEclipseRouteProductPointKind {
    TAIYIN_SOLAR_ROUTE_PRODUCT_POINT_CORE_NORTH = 0,
    TAIYIN_SOLAR_ROUTE_PRODUCT_POINT_CORE_SOUTH = 1,
    TAIYIN_SOLAR_ROUTE_PRODUCT_POINT_POLYGON_CLOSE = 2,
    TAIYIN_SOLAR_ROUTE_PRODUCT_POINT_PENUMBRAL_NORTH = 3,
    TAIYIN_SOLAR_ROUTE_PRODUCT_POINT_PENUMBRAL_SOUTH = 4,
    TAIYIN_SOLAR_ROUTE_PRODUCT_POINT_HALF_MAGNITUDE_NORTH = 5,
    TAIYIN_SOLAR_ROUTE_PRODUCT_POINT_HALF_MAGNITUDE_SOUTH = 6,
    TAIYIN_SOLAR_ROUTE_PRODUCT_POINT_CORE_BEGIN_HORIZON = 7,
    TAIYIN_SOLAR_ROUTE_PRODUCT_POINT_CORE_END_HORIZON = 8,
};

struct SolarEclipseRouteProductPoint {
    SplitJulianDate jd_tt;
    SplitJulianDate jd_ut;
    uint32_t point_kind;
    uint32_t source_curve_kind;
    double latitude_deg;
    double longitude_deg;
    double unwrapped_longitude_deg;
};

struct SolarEclipseRouteProductSummary {
    uint32_t flags;
    size_t curve_point_count;
    size_t center_line_count;
    size_t core_north_count;
    size_t core_south_count;
    size_t core_begin_horizon_count;
    size_t core_end_horizon_count;
    size_t penumbral_north_count;
    size_t penumbral_south_count;
    size_t half_magnitude_north_count;
    size_t half_magnitude_south_count;
    size_t core_polygon_point_count;
    size_t penumbral_polygon_point_count;
    size_t half_magnitude_polygon_point_count;
    size_t polygon_point_count;
    double min_latitude_deg;
    double max_latitude_deg;
    double min_unwrapped_longitude_deg;
    double max_unwrapped_longitude_deg;
};

const size_t TAIYIN_SOLAR_BESSELIAN_MAX_DEGREE = 7;
const size_t TAIYIN_SOLAR_BESSELIAN_COEFF_COUNT = TAIYIN_SOLAR_BESSELIAN_MAX_DEGREE + 1;

struct SolarBesselianElements {
    double t_hours;
    double x;
    double y;
    double zeta;
    double d_deg;
    double mu_deg;
    double l1;
    double l2;
    double f1_deg;
    double f2_deg;
    double tan_f1;
    double tan_f2;
    double gamma;
};

struct SolarBesselianPolynomial {
    SplitJulianDate t0_jd_tt;
    double span_hours;
    double sample_step_hours;
    int degree;
    double x[TAIYIN_SOLAR_BESSELIAN_COEFF_COUNT];
    double y[TAIYIN_SOLAR_BESSELIAN_COEFF_COUNT];
    double zeta[TAIYIN_SOLAR_BESSELIAN_COEFF_COUNT];
    double d_deg[TAIYIN_SOLAR_BESSELIAN_COEFF_COUNT];
    double mu_deg[TAIYIN_SOLAR_BESSELIAN_COEFF_COUNT];
    double l1[TAIYIN_SOLAR_BESSELIAN_COEFF_COUNT];
    double l2[TAIYIN_SOLAR_BESSELIAN_COEFF_COUNT];
    double f1_deg;
    double f2_deg;
    double tan_f1;
    double tan_f2;
    SolarBesselianElements center;
    SolarBesselianElements max_residual;
};

// ---------------------------------------------------------------------------
// solve_lunar_eclipse_at
//
// Evaluate whether a lunar eclipse occurs at or near the given time.
// The time need not be exact; the solver refines the maximum-eclipse time
// from a Meeus ch.52 seed and linear extrapolation.
//
// Shadow and moon-radius models are read from the context
// (eclipse_shadow_model_id, eclipse_moon_radius_model_id), same convention
// as refraction_model_id and other ctx-based model IDs.
//
// Returns TAIYIN_STATUS_OK on success (regardless of whether an eclipse
// was found; check out->kind).  Returns an error status only on
// ephemeris evaluation failure or invalid arguments.
// ---------------------------------------------------------------------------
Status solve_lunar_eclipse_at(
    const NativeCalcContext* context,
    SplitJulianDate jd_tt,
    uint64_t flags,
    LunarEclipseResult* out,
    EphemerisEvalDiagnostic* diagnostic
) noexcept;

Status solve_lunar_eclipse_at_ut(
    const NativeCalcContext* context,
    SplitJulianDate jd_ut,
    uint64_t flags,
    LunarEclipseResultUt* out,
    EphemerisEvalDiagnostic* diagnostic
) noexcept;

// ---------------------------------------------------------------------------
// search_next_lunar_eclipse_tt
//
// Find the next (or previous) lunar eclipse after (before) jd_start_tt.
// Uses the Meeus ch.52 K+F pre-filter to skip ~77% of lunations.
//
// kind_filter: 0 = all kinds, or OR of TAIYIN_ECLIPSE_* constants.
// Use TAIYIN_ECLIPSE_BACKWARD flag for backward search.
//
// Returns TAIYIN_STATUS_OK if an eclipse was found.
// Returns TAIYIN_EVENT_ERROR_NOT_FOUND if no matching eclipse exists
// within the ephemeris data coverage.
// ---------------------------------------------------------------------------
Status search_next_lunar_eclipse_tt(
    const NativeCalcContext* context,
    SplitJulianDate jd_start_tt,
    uint32_t kind_filter,
    uint64_t flags,
    LunarEclipseResult* out,
    EphemerisEvalDiagnostic* diagnostic
) noexcept;

Status search_next_lunar_eclipse_ut(
    const NativeCalcContext* context,
    SplitJulianDate jd_start_ut,
    uint32_t kind_filter,
    uint64_t flags,
    LunarEclipseResultUt* out,
    EphemerisEvalDiagnostic* diagnostic
) noexcept;

// ---------------------------------------------------------------------------
// search_lunar_eclipses_tt
//
// Find all lunar eclipses in [start_jd_tt, end_jd_tt].
// Uses the same K+F pre-filter as search_next_lunar_eclipse_tt.
// ---------------------------------------------------------------------------
Status search_lunar_eclipses_tt(
    const NativeCalcContext* context,
    SplitJulianDate start_jd_tt,
    SplitJulianDate end_jd_tt,
    uint32_t kind_filter,
    uint64_t flags,
    LunarEclipseResult* out_results,
    size_t max_result_count,
    size_t* out_result_count,
    EphemerisEvalDiagnostic* diagnostic
) noexcept;

Status search_lunar_eclipses_ut(
    const NativeCalcContext* context,
    SplitJulianDate start_jd_ut,
    SplitJulianDate end_jd_ut,
    uint32_t kind_filter,
    uint64_t flags,
    LunarEclipseResultUt* out_results,
    size_t max_result_count,
    size_t* out_result_count,
    EphemerisEvalDiagnostic* diagnostic
) noexcept;

// Local lunar-eclipse visibility uses context->observer_location. The context
// must have an observer location field; otherwise these functions return
// TAIYIN_ERROR_INVALID_ARGUMENT.
Status compute_local_lunar_eclipse_visibility_ut(
    const NativeCalcContext* context,
    const LunarEclipseResultUt* eclipse,
    uint64_t flags,
    LocalLunarEclipseResultUt* out,
    EphemerisEvalDiagnostic* diagnostic
) noexcept;

Status compute_local_lunar_eclipse_visibility_tt(
    const NativeCalcContext* context,
    const LunarEclipseResult* eclipse,
    uint64_t flags,
    LocalLunarEclipseResult* out,
    EphemerisEvalDiagnostic* diagnostic
) noexcept;

Status search_next_local_lunar_eclipse_ut(
    const NativeCalcContext* context,
    SplitJulianDate jd_start_ut,
    uint32_t kind_filter,
    uint64_t flags,
    LocalLunarEclipseResultUt* out,
    EphemerisEvalDiagnostic* diagnostic
) noexcept;

Status search_next_local_lunar_eclipse_tt(
    const NativeCalcContext* context,
    SplitJulianDate jd_start_tt,
    uint32_t kind_filter,
    uint64_t flags,
    LocalLunarEclipseResult* out,
    EphemerisEvalDiagnostic* diagnostic
) noexcept;

Status solve_solar_eclipse_at(
    const NativeCalcContext* context,
    SplitJulianDate jd_tt,
    uint64_t flags,
    SolarEclipseResult* out,
    EphemerisEvalDiagnostic* diagnostic
) noexcept;

Status solve_solar_eclipse_at_ut(
    const NativeCalcContext* context,
    SplitJulianDate jd_ut,
    uint64_t flags,
    SolarEclipseResultUt* out,
    EphemerisEvalDiagnostic* diagnostic
) noexcept;

Status search_next_solar_eclipse_tt(
    const NativeCalcContext* context,
    SplitJulianDate jd_start_tt,
    uint32_t kind_filter,
    uint64_t flags,
    SolarEclipseResult* out,
    EphemerisEvalDiagnostic* diagnostic
) noexcept;

Status search_next_solar_eclipse_ut(
    const NativeCalcContext* context,
    SplitJulianDate jd_start_ut,
    uint32_t kind_filter,
    uint64_t flags,
    SolarEclipseResultUt* out,
    EphemerisEvalDiagnostic* diagnostic
) noexcept;

Status search_solar_eclipses_tt(
    const NativeCalcContext* context,
    SplitJulianDate start_jd_tt,
    SplitJulianDate end_jd_tt,
    uint32_t kind_filter,
    uint64_t flags,
    SolarEclipseResult* out_results,
    size_t max_result_count,
    size_t* out_result_count,
    EphemerisEvalDiagnostic* diagnostic
) noexcept;

Status search_solar_eclipses_ut(
    const NativeCalcContext* context,
    SplitJulianDate start_jd_ut,
    SplitJulianDate end_jd_ut,
    uint32_t kind_filter,
    uint64_t flags,
    SolarEclipseResultUt* out_results,
    size_t max_result_count,
    size_t* out_result_count,
    EphemerisEvalDiagnostic* diagnostic
) noexcept;

Status compute_solar_besselian_elements_tt(
    const NativeCalcContext* context,
    SplitJulianDate jd_tt,
    double t_hours,
    SolarBesselianElements* out,
    EphemerisEvalDiagnostic* diagnostic
) noexcept;

Status compute_solar_besselian_polynomial_tt(
    const NativeCalcContext* context,
    SplitJulianDate center_jd_tt,
    double span_hours,
    double sample_step_hours,
    int degree,
    SolarBesselianPolynomial* out,
    EphemerisEvalDiagnostic* diagnostic
) noexcept;

Status evaluate_solar_besselian_polynomial(
    const SolarBesselianPolynomial* polynomial,
    double t_hours,
    SolarBesselianElements* out
) noexcept;

// Local solar-eclipse APIs use context->observer_location. The context must
// have an observer location field; otherwise these functions return
// TAIYIN_ERROR_INVALID_ARGUMENT.
Status solve_local_solar_eclipse_at_tt(
    const NativeCalcContext* context,
    SplitJulianDate jd_tt,
    uint64_t flags,
    LocalSolarEclipseResult* out,
    EphemerisEvalDiagnostic* diagnostic
) noexcept;

Status solve_local_solar_eclipse_at_ut(
    const NativeCalcContext* context,
    SplitJulianDate jd_ut,
    uint64_t flags,
    LocalSolarEclipseResultUt* out,
    EphemerisEvalDiagnostic* diagnostic
) noexcept;

Status search_next_local_solar_eclipse_tt(
    const NativeCalcContext* context,
    SplitJulianDate jd_start_tt,
    uint32_t kind_filter,
    uint64_t flags,
    LocalSolarEclipseResult* out,
    EphemerisEvalDiagnostic* diagnostic
) noexcept;

Status search_next_local_solar_eclipse_ut(
    const NativeCalcContext* context,
    SplitJulianDate jd_start_ut,
    uint32_t kind_filter,
    uint64_t flags,
    LocalSolarEclipseResultUt* out,
    EphemerisEvalDiagnostic* diagnostic
) noexcept;

Status compute_local_solar_circumstances_tt(
    const NativeCalcContext* context,
    SplitJulianDate jd_tt,
    LocalSolarEclipseCircumstances* out,
    EphemerisEvalDiagnostic* diagnostic
) noexcept;

Status compute_local_solar_circumstances_ut(
    const NativeCalcContext* context,
    SplitJulianDate jd_ut,
    LocalSolarEclipseCircumstancesUt* out,
    EphemerisEvalDiagnostic* diagnostic
) noexcept;

// Solar route APIs accept TAIYIN_ECLIPSE_TRUEPOS and
// TAIYIN_ECLIPSE_LUNAR_LIMB_CORRECTION only. The lunar-limb flag refines all
// non-center route limits and the polygons built from them; the center line is
// unchanged because it is defined by the shadow axis.
Status compute_solar_eclipse_route_row_tt(
    const NativeCalcContext* context,
    SplitJulianDate jd_tt,
    uint64_t flags,
    SolarEclipseRouteRow* out,
    EphemerisEvalDiagnostic* diagnostic
) noexcept;

Status compute_solar_eclipse_route_row_ut(
    const NativeCalcContext* context,
    SplitJulianDate jd_ut,
    uint64_t flags,
    SolarEclipseRouteRow* out,
    EphemerisEvalDiagnostic* diagnostic
) noexcept;

// Batch route sampling advances on the time scale named by the function.
// Both endpoints are inclusive; the exact end epoch is emitted once, and the
// final interval may be shorter than step_minutes.
Status compute_solar_eclipse_route_tt(
    const NativeCalcContext* context,
    SplitJulianDate start_jd_tt,
    SplitJulianDate end_jd_tt,
    double step_minutes,
    uint64_t flags,
    SolarEclipseRouteRow* out_rows,
    size_t max_row_count,
    size_t* out_row_count,
    EphemerisEvalDiagnostic* diagnostic
) noexcept;

Status compute_solar_eclipse_route_ut(
    const NativeCalcContext* context,
    SplitJulianDate start_jd_ut,
    SplitJulianDate end_jd_ut,
    double step_minutes,
    uint64_t flags,
    SolarEclipseRouteRow* out_rows,
    size_t max_row_count,
    size_t* out_row_count,
    EphemerisEvalDiagnostic* diagnostic
) noexcept;

Status compute_solar_eclipse_route_curves_tt(
    const NativeCalcContext* context,
    SplitJulianDate jd_near_tt,
    uint64_t flags,
    SolarEclipseRouteCurvePoint* out_points,
    size_t max_point_count,
    size_t* out_point_count,
    EphemerisEvalDiagnostic* diagnostic
) noexcept;

Status compute_solar_eclipse_route_curves_tt_with_options(
    const NativeCalcContext* context,
    SplitJulianDate jd_near_tt,
    uint64_t flags,
    size_t route_sample_count,
    SolarEclipseRouteCurvePoint* out_points,
    size_t max_point_count,
    size_t* out_point_count,
    EphemerisEvalDiagnostic* diagnostic
) noexcept;

Status compute_solar_eclipse_route_curves_ut(
    const NativeCalcContext* context,
    SplitJulianDate jd_near_ut,
    uint64_t flags,
    SolarEclipseRouteCurvePoint* out_points,
    size_t max_point_count,
    size_t* out_point_count,
    EphemerisEvalDiagnostic* diagnostic
) noexcept;

Status compute_solar_eclipse_route_curves_ut_with_options(
    const NativeCalcContext* context,
    SplitJulianDate jd_near_ut,
    uint64_t flags,
    size_t route_sample_count,
    SolarEclipseRouteCurvePoint* out_points,
    size_t max_point_count,
    size_t* out_point_count,
    EphemerisEvalDiagnostic* diagnostic
) noexcept;

Status compute_solar_eclipse_route_product_tt(
    const NativeCalcContext* context,
    SplitJulianDate jd_near_tt,
    uint64_t flags,
    SolarEclipseRouteProductPoint* out_core_polygon,
    size_t max_core_polygon_point_count,
    size_t* out_core_polygon_point_count,
    SolarEclipseRouteProductSummary* out_summary,
    EphemerisEvalDiagnostic* diagnostic
) noexcept;

Status compute_solar_eclipse_route_product_tt_with_options(
    const NativeCalcContext* context,
    SplitJulianDate jd_near_tt,
    uint64_t flags,
    size_t route_sample_count,
    SolarEclipseRouteProductPoint* out_core_polygon,
    size_t max_core_polygon_point_count,
    size_t* out_core_polygon_point_count,
    SolarEclipseRouteProductSummary* out_summary,
    EphemerisEvalDiagnostic* diagnostic
) noexcept;

Status compute_solar_eclipse_route_product_ut(
    const NativeCalcContext* context,
    SplitJulianDate jd_near_ut,
    uint64_t flags,
    SolarEclipseRouteProductPoint* out_core_polygon,
    size_t max_core_polygon_point_count,
    size_t* out_core_polygon_point_count,
    SolarEclipseRouteProductSummary* out_summary,
    EphemerisEvalDiagnostic* diagnostic
) noexcept;

Status compute_solar_eclipse_route_product_ut_with_options(
    const NativeCalcContext* context,
    SplitJulianDate jd_near_ut,
    uint64_t flags,
    size_t route_sample_count,
    SolarEclipseRouteProductPoint* out_core_polygon,
    size_t max_core_polygon_point_count,
    size_t* out_core_polygon_point_count,
    SolarEclipseRouteProductSummary* out_summary,
    EphemerisEvalDiagnostic* diagnostic
) noexcept;

Status compute_solar_eclipse_route_map_product_tt(
    const NativeCalcContext* context,
    SplitJulianDate jd_near_tt,
    uint64_t flags,
    SolarEclipseRouteProductPoint* out_polygon_points,
    size_t max_polygon_point_count,
    size_t* out_polygon_point_count,
    SolarEclipseRouteProductSummary* out_summary,
    EphemerisEvalDiagnostic* diagnostic
) noexcept;

Status compute_solar_eclipse_route_map_product_tt_with_options(
    const NativeCalcContext* context,
    SplitJulianDate jd_near_tt,
    uint64_t flags,
    size_t route_sample_count,
    SolarEclipseRouteProductPoint* out_polygon_points,
    size_t max_polygon_point_count,
    size_t* out_polygon_point_count,
    SolarEclipseRouteProductSummary* out_summary,
    EphemerisEvalDiagnostic* diagnostic
) noexcept;

Status compute_solar_eclipse_route_map_product_ut(
    const NativeCalcContext* context,
    SplitJulianDate jd_near_ut,
    uint64_t flags,
    SolarEclipseRouteProductPoint* out_polygon_points,
    size_t max_polygon_point_count,
    size_t* out_polygon_point_count,
    SolarEclipseRouteProductSummary* out_summary,
    EphemerisEvalDiagnostic* diagnostic
) noexcept;

Status compute_solar_eclipse_route_map_product_ut_with_options(
    const NativeCalcContext* context,
    SplitJulianDate jd_near_ut,
    uint64_t flags,
    size_t route_sample_count,
    SolarEclipseRouteProductPoint* out_polygon_points,
    size_t max_polygon_point_count,
    size_t* out_polygon_point_count,
    SolarEclipseRouteProductSummary* out_summary,
    EphemerisEvalDiagnostic* diagnostic
) noexcept;

// Boundary helpers are map/path geometry probes for an arbitrary geographic
// point, so they intentionally keep explicit longitude/latitude parameters.

Status compute_local_solar_eclipse_boundary_tt(
    const NativeCalcContext* context,
    SplitJulianDate jd_tt,
    double longitude_deg,
    double latitude_deg,
    LocalSolarEclipseBoundary* out,
    EphemerisEvalDiagnostic* diagnostic
) noexcept;

Status compute_local_solar_eclipse_boundary_ut(
    const NativeCalcContext* context,
    SplitJulianDate jd_ut,
    double longitude_deg,
    double latitude_deg,
    LocalSolarEclipseBoundary* out,
    EphemerisEvalDiagnostic* diagnostic
) noexcept;

}  // namespace runtime
}  // namespace taiyin

#endif  // TAIYIN_RUNTIME_ECLIPSE_SEARCH_H
