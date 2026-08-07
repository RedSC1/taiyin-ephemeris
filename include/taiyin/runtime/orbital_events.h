#ifndef TAIYIN_RUNTIME_ORBITAL_EVENTS_H
#define TAIYIN_RUNTIME_ORBITAL_EVENTS_H

#include "taiyin/runtime/native_position.h"
#include "taiyin/status.h"

#include <stdint.h>

namespace taiyin {
namespace runtime {

constexpr uint64_t TAIYIN_ORBITAL_EVENT_POSITION_FLAGS_MASK = 0x00000000ffffffffull;
constexpr uint64_t TAIYIN_ORBITAL_EVENT_OPTION_FLAGS_MASK = 0xffffffff00000000ull;
constexpr uint64_t TAIYIN_ORBITAL_EVENT_REVERSE = 1ull << 32;

enum BodyApsisKind {
    TAIYIN_BODY_APSIS_PERICENTER = 0,
    TAIYIN_BODY_APSIS_APOCENTER = 1,
};

enum BodyNodeKind {
    TAIYIN_BODY_NODE_ASCENDING = 0,
    TAIYIN_BODY_NODE_DESCENDING = 1,
};

enum BodyOrbitReferencePointModel {
    // Points on the instantaneous two-body orbit fitted to the evaluated
    // geometric position and velocity at the requested epoch.
    TAIYIN_BODY_ORBIT_REFERENCE_POINTS_OSCULATING = 0,
};

struct BodyOrbitReferencePoint {
    Vector3 position_au;
    double longitude_rad;
    double latitude_rad;
    double distance_au;

    BodyOrbitReferencePoint() noexcept;
};

// Geometric reference points of one osculating orbit at a requested epoch.
// These are not searches for the next physical passage through each point.
struct BodyOrbitReferencePoints {
    int body_id;
    int center_id;
    int reference_frame_id;
    BodyOrbitReferencePointModel model;
    BodyOrbitReferencePoint ascending_node;
    BodyOrbitReferencePoint descending_node;
    BodyOrbitReferencePoint periapsis;
    BodyOrbitReferencePoint apoapsis;
    // The second focus relative to the occupied primary focus.
    BodyOrbitReferencePoint second_focus;

    BodyOrbitReferencePoints() noexcept;
};

// Classical osculating elements of a body relative to its fixed physical
// primary, oriented in the requested supported reference frame.
struct BodyOsculatingOrbit {
    int body_id;
    int center_id;
    int reference_frame_id;
    double gravitational_parameter_au3_per_day2;
    double semi_major_axis_au;
    double eccentricity;
    double inclination_rad;
    double longitude_of_ascending_node_rad;
    double argument_of_periapsis_rad;
    double true_anomaly_rad;
    double mean_anomaly_rad;
    double periapsis_distance_au;
    double apoapsis_distance_au;
    double osculating_period_days;
    double current_distance_au;
    double radial_velocity_au_per_day;

    BodyOsculatingOrbit() noexcept;
};

struct BodyApsisSearchResult {
    int body_id;
    int center_id;
    BodyApsisKind kind;
    SplitJulianDate jd;
    double distance_au;
    double radial_velocity_au_per_day;
    int iteration_count;
    int evaluation_count;

    BodyApsisSearchResult() noexcept;
};

struct BodyNodeSearchResult {
    int body_id;
    int center_id;
    int reference_frame_id;
    BodyNodeKind kind;
    SplitJulianDate jd;
    // Longitude in an ecliptic frame; right-ascension direction in an
    // equatorial frame.
    double reference_plane_angle_rad;
    double distance_au;
    int iteration_count;
    int evaluation_count;

    BodyNodeSearchResult() noexcept;
};

// Moon is evaluated relative to Earth. Earth, EMB, major-planet centers, and
// major-planet barycenters are evaluated relative to the Sun. The state is
// always geometric. reference_frame_id must be a supported apparent output
// frame. Only ALLOW_BARYCENTER_APPROX is accepted from the low native-position
// flag word.
Status calc_body_osculating_orbit_tt(
    const NativeCalcContext* context,
    int body_id,
    SplitJulianDate jd_tt,
    int reference_frame_id,
    uint64_t flags,
    BodyOsculatingOrbit* out,
    EphemerisEvalDiagnostic* diagnostic
) noexcept;

Status calc_body_osculating_orbit_ut(
    const NativeCalcContext* context,
    int body_id,
    SplitJulianDate jd_ut,
    int reference_frame_id,
    uint64_t flags,
    BodyOsculatingOrbit* out,
    EphemerisEvalDiagnostic* diagnostic
) noexcept;

Status calc_body_orbit_reference_points_tt(
    const NativeCalcContext* context,
    int body_id,
    SplitJulianDate jd_tt,
    int reference_frame_id,
    uint64_t flags,
    BodyOrbitReferencePoints* out,
    EphemerisEvalDiagnostic* diagnostic
) noexcept;

Status calc_body_orbit_reference_points_ut(
    const NativeCalcContext* context,
    int body_id,
    SplitJulianDate jd_ut,
    int reference_frame_id,
    uint64_t flags,
    BodyOrbitReferencePoints* out,
    EphemerisEvalDiagnostic* diagnostic
) noexcept;

Status search_next_body_apsis_tt(
    const NativeCalcContext* context,
    int body_id,
    BodyApsisKind kind,
    SplitJulianDate jd_start_tt,
    uint64_t flags,
    BodyApsisSearchResult* out,
    EphemerisEvalDiagnostic* diagnostic
) noexcept;

Status search_next_body_apsis_ut(
    const NativeCalcContext* context,
    int body_id,
    BodyApsisKind kind,
    SplitJulianDate jd_start_ut,
    uint64_t flags,
    BodyApsisSearchResult* out,
    EphemerisEvalDiagnostic* diagnostic
) noexcept;

Status search_next_body_plane_node_tt(
    const NativeCalcContext* context,
    int body_id,
    BodyNodeKind kind,
    SplitJulianDate jd_start_tt,
    int reference_frame_id,
    uint64_t flags,
    BodyNodeSearchResult* out,
    EphemerisEvalDiagnostic* diagnostic
) noexcept;

Status search_next_body_plane_node_ut(
    const NativeCalcContext* context,
    int body_id,
    BodyNodeKind kind,
    SplitJulianDate jd_start_ut,
    int reference_frame_id,
    uint64_t flags,
    BodyNodeSearchResult* out,
    EphemerisEvalDiagnostic* diagnostic
) noexcept;

}  // namespace runtime
}  // namespace taiyin

#endif  // TAIYIN_RUNTIME_ORBITAL_EVENTS_H
