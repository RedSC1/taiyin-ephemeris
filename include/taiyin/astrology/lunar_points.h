#ifndef TAIYIN_ASTROLOGY_LUNAR_POINTS_H
#define TAIYIN_ASTROLOGY_LUNAR_POINTS_H

#include "taiyin/runtime/native_position.h"
#include "taiyin/status.h"
#include "taiyin/time.h"

#include <stdint.h>

namespace taiyin {
namespace astrology {

// The osculating lunar orbit intersects the selected ecliptic plane at these
// two directions. They are commonly called the true lunar nodes.
enum LunarNodeKind {
    TAIYIN_LUNAR_NODE_ASCENDING = 0,
    TAIYIN_LUNAR_NODE_DESCENDING = 1,
};

struct LunarNodePosition {
    // Effective reference frame used for longitude. An osculating node is the
    // orbit plane's intersection with this frame's XY plane. An IERS mean
    // node is a conventional mean-ecliptic direction transformed into this
    // frame.
    int reference_frame_id;
    double longitude_rad;
    double longitude_rate_rad_per_day;

    LunarNodePosition() noexcept;
};

// Retained as a source-compatible name for the initially published
// osculating-node API.
using LunarTrueNodePosition = LunarNodePosition;

enum LunarApsisDefinition {
    // A conventional direction derived from the IERS 2003 Delaunay arguments.
    // It is a mean-model direction, not a physical Moon position.
    TAIYIN_LUNAR_APSIS_DELAUNAY_MEAN = 0,
    // The apoapsis of the two-body ellipse osculating the corrected,
    // geocentric Moon state at the requested epoch.
    TAIYIN_LUNAR_APSIS_OSCULATING_TWO_BODY = 1,
    // A continuous natural-apogee convention fitted to successive physical
    // lunar apoapses in NASA/JPL DE441.
    TAIYIN_LUNAR_APSIS_DE441_FITTED_NATURAL = 2,
};

struct LunarApsisPosition {
    int reference_frame_id;
    LunarApsisDefinition definition;
    double longitude_rad;
    double latitude_rad;
    double longitude_rate_rad_per_day;
    double latitude_rate_rad_per_day;
    // Physical distance and rate for definitions that provide them. A
    // Delaunay mean apogee is only a conventional direction, so these fields
    // are NAN for that definition.
    double distance_au;
    double distance_rate_au_per_day;
    // For DE441_FITTED_NATURAL, true when the date lies outside the source
    // interval and the nearest boundary segment is extrapolated. False for
    // the other definitions.
    bool extrapolated;

    LunarApsisPosition() noexcept;
};

// Evaluates the geocentric osculating ("true") lunar node. The supplied
// native flags retain their normal apparent-correction meaning: TRUEPOS,
// ASTROMETRIC, NO_ABERR, NO_GDEFL, and NONUT are accepted. The node itself is
// always geocentric, so TOPOCENTRIC and Cartesian/output-shape flags are
// rejected. The native context's selected output frame determines the
// reference plane; EQUATORIAL explicitly selects an equatorial frame.
Status calc_lunar_true_node_tt(
    const runtime::NativeCalcContext* context,
    SplitJulianDate jd_tt,
    LunarNodeKind kind,
    uint32_t native_position_flags,
    LunarTrueNodePosition* out,
    runtime::EphemerisEvalDiagnostic* diagnostic
) noexcept;

Status calc_lunar_true_node_ut(
    const runtime::NativeCalcContext* context,
    SplitJulianDate jd_ut,
    LunarNodeKind kind,
    uint32_t native_position_flags,
    LunarTrueNodePosition* out,
    runtime::EphemerisEvalDiagnostic* diagnostic
) noexcept;

// Evaluates the IERS 2003 conventional mean lunar node. Only EQUATORIAL and
// NONUT are accepted: apparent/light-time corrections do not apply to a
// Delaunay mean argument. The result is transformed from mean ecliptic of
// date into the selected output frame.
Status calc_lunar_mean_node_tt(
    const runtime::NativeCalcContext* context,
    SplitJulianDate jd_tt,
    LunarNodeKind kind,
    uint32_t native_position_flags,
    LunarNodePosition* out,
    runtime::EphemerisEvalDiagnostic* diagnostic
) noexcept;

Status calc_lunar_mean_node_ut(
    const runtime::NativeCalcContext* context,
    SplitJulianDate jd_ut,
    LunarNodeKind kind,
    uint32_t native_position_flags,
    LunarNodePosition* out,
    runtime::EphemerisEvalDiagnostic* diagnostic
) noexcept;

// Evaluates the Delaunay mean lunar apogee direction. It is commonly called
// the mean Black Moon or mean Lilith in astrology, but does not claim to be a
// unique physical definition of that term. Only EQUATORIAL and NONUT are
// accepted from native_position_flags.
Status calc_lunar_mean_apogee_tt(
    const runtime::NativeCalcContext* context,
    SplitJulianDate jd_tt,
    uint32_t native_position_flags,
    LunarApsisPosition* out,
    runtime::EphemerisEvalDiagnostic* diagnostic
) noexcept;

Status calc_lunar_mean_apogee_ut(
    const runtime::NativeCalcContext* context,
    SplitJulianDate jd_ut,
    uint32_t native_position_flags,
    LunarApsisPosition* out,
    runtime::EphemerisEvalDiagnostic* diagnostic
) noexcept;

// Evaluates the geocentric osculating lunar apoapsis. This is a well-defined
// instantaneous two-body construction and is commonly labelled "true Lilith"
// by astrology software. The supplied flags have the same contract as the
// true-node API; TOPOCENTRIC and output-shape flags are rejected.
Status calc_lunar_osculating_apogee_tt(
    const runtime::NativeCalcContext* context,
    SplitJulianDate jd_tt,
    uint32_t native_position_flags,
    LunarApsisPosition* out,
    runtime::EphemerisEvalDiagnostic* diagnostic
) noexcept;

Status calc_lunar_osculating_apogee_ut(
    const runtime::NativeCalcContext* context,
    SplitJulianDate jd_ut,
    uint32_t native_position_flags,
    LunarApsisPosition* out,
    runtime::EphemerisEvalDiagnostic* diagnostic
) noexcept;

// Evaluates a continuous natural-apogee convention fitted to physical DE441
// lunar apoapsis events. Event directions are reconstructed in fixed ICRF
// from a Delaunay mean direction and a fitted spherical tangent correction;
// successive event vectors are joined with a nonuniform cubic Hermite
// interpolant. The model remains evaluable outside the DE441 fit interval by
// extrapolating its nearest boundary segment and reports that condition
// through LunarApsisPosition::extrapolated. Only EQUATORIAL and NONUT are
// accepted from native_position_flags.
Status calc_lunar_fitted_apogee_tt(
    const runtime::NativeCalcContext* context,
    SplitJulianDate jd_tt,
    uint32_t native_position_flags,
    LunarApsisPosition* out,
    runtime::EphemerisEvalDiagnostic* diagnostic
) noexcept;

Status calc_lunar_fitted_apogee_ut(
    const runtime::NativeCalcContext* context,
    SplitJulianDate jd_ut,
    uint32_t native_position_flags,
    LunarApsisPosition* out,
    runtime::EphemerisEvalDiagnostic* diagnostic
) noexcept;

}  // namespace astrology
}  // namespace taiyin

#endif  // TAIYIN_ASTROLOGY_LUNAR_POINTS_H
