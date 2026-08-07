#ifndef TAIYIN_ASTROLOGY_SIDEREAL_H
#define TAIYIN_ASTROLOGY_SIDEREAL_H

#include "taiyin/runtime/native_position.h"
#include "taiyin/status.h"
#include "taiyin/time.h"

#include <cmath>
#include <stdint.h>

namespace taiyin {
namespace astrology {

// These identifiers intentionally match the well-known convention names, not
// the numeric values of another library's API.
enum SiderealAyanamshaId {
    TAIYIN_SIDEREAL_AYANAMSHA_FAGAN_BRADLEY = 0,
    TAIYIN_SIDEREAL_AYANAMSHA_LAHIRI = 1,
    TAIYIN_SIDEREAL_AYANAMSHA_RAMAN = 3,
    TAIYIN_SIDEREAL_AYANAMSHA_KRISHNAMURTI = 5,
    // Star-anchored definitions use built-in ICRF astrometry, not a loaded
    // user star catalog.
    TAIYIN_SIDEREAL_AYANAMSHA_GALACTIC_CENTER_0_SAGITTARIUS = 17,
    TAIYIN_SIDEREAL_AYANAMSHA_TRUE_CHITRA = 27,
};

constexpr int TAIYIN_SIDEREAL_AYANAMSHA_CUSTOM_START = 10000;

// Sidereal calls share the native-position low word and use the high word for
// sidereal-specific controls. At most one reference-plane bit may be set.
// ECL_T0 and SSY_PLANE require a finite reference_epoch_jd; otherwise it must
// be an invalid split date. REFERENCE_EPOCH_UT1 is valid only with ECL_T0 or
// SSY_PLANE. By
// default historical ayanamshas are compensated to the native context's
// selected precession model; RAW_REFERENCE_OFFSET and
// USE_REFERENCE_PRECESSION override that default and are mutually exclusive.
constexpr uint64_t TAIYIN_SIDEREAL_POSITION_FLAGS_MASK = 0x00000000ffffffffull;
constexpr uint64_t TAIYIN_SIDEREAL_REFERENCE_ECL_T0 = 1ull << 32;
constexpr uint64_t TAIYIN_SIDEREAL_REFERENCE_SSY_PLANE = 1ull << 33;
constexpr uint64_t TAIYIN_SIDEREAL_REFERENCE_J2000_ECLIPTIC = 1ull << 34;
constexpr uint64_t TAIYIN_SIDEREAL_REFERENCE_EPOCH_UT1 = 1ull << 35;
constexpr uint64_t TAIYIN_SIDEREAL_RAW_REFERENCE_OFFSET = 1ull << 36;
constexpr uint64_t TAIYIN_SIDEREAL_USE_REFERENCE_PRECESSION = 1ull << 37;
constexpr uint64_t TAIYIN_SIDEREAL_REFERENCE_PLANE_FLAGS =
    TAIYIN_SIDEREAL_REFERENCE_ECL_T0
    | TAIYIN_SIDEREAL_REFERENCE_SSY_PLANE
    | TAIYIN_SIDEREAL_REFERENCE_J2000_ECLIPTIC;
constexpr uint64_t TAIYIN_SIDEREAL_PRECESSION_POLICY_FLAGS =
    TAIYIN_SIDEREAL_RAW_REFERENCE_OFFSET
    | TAIYIN_SIDEREAL_USE_REFERENCE_PRECESSION;
constexpr uint64_t TAIYIN_SIDEREAL_KNOWN_FLAGS =
    TAIYIN_SIDEREAL_POSITION_FLAGS_MASK
    | TAIYIN_SIDEREAL_REFERENCE_PLANE_FLAGS
    | TAIYIN_SIDEREAL_REFERENCE_EPOCH_UT1
    | TAIYIN_SIDEREAL_PRECESSION_POLICY_FLAGS;

// Owns a configured native calculation context whose output frame is the
// selected sidereal frame. The embedded native context remains the sole source
// of output-frame semantics; ordinary calc_position_* calls can use it
// directly. Copying the wrapper repairs all self-referential callback data.
struct AstrologyContext {
    runtime::NativeCalcContext native_context;
    int ayanamsha_id;
    int32_t coordinate_frame_id;
    uint64_t sidereal_flags;
    SplitJulianDate reference_epoch_jd;

    AstrologyContext() noexcept;
    AstrologyContext(const AstrologyContext& other) noexcept;
    AstrologyContext& operator=(const AstrologyContext& other) noexcept;
};

struct AyanamshaDispatchData {
    const runtime::NativeCalcContext* native_context;
    int ayanamsha_id;
    SplitJulianDate jd_tt;
    uint64_t native_position_flags;
    uint64_t sidereal_flags;
    // Filled by eval_ayanamsha_model from the registered model entry.
    const void* model_data;

    AyanamshaDispatchData() noexcept;
};

Status configure_astrology_context(
    AstrologyContext* out,
    const runtime::NativeCalcContext* native_context,
    int ayanamsha_id,
    // High bits select the sidereal policy. Low native-position bits fix the
    // correction semantics used by star-anchored ayanamsha models; output
    // shape bits are ignored for that evaluation.
    uint64_t sidereal_flags,
    SplitJulianDate reference_epoch_jd = SplitJulianDate(0, NAN)
) noexcept;

// Evaluators may be called concurrently. Callback code and all state reachable
// through model_data must remain loaded, valid, and safe for concurrent access
// until the model is removed. Registration and removal are setup-time changes
// and must not overlap evaluation.
typedef Status (*AyanamshaFn)(
    const AyanamshaDispatchData* data,
    double* out_ayanamsha_rad
);

struct AyanamshaModelEntry {
    int model_id;
    AyanamshaFn eval;
    // Used only by TAIYIN_SIDEREAL_USE_REFERENCE_PRECESSION. A negative value
    // keeps the precession model selected by NativeCalcContext.
    int reference_precession_model_id;
    // The registry does not own this pointer. It may be read concurrently and
    // must remain valid until this model is removed.
    const void* model_data;

    AyanamshaModelEntry() noexcept;
    AyanamshaModelEntry(
        int model_id_value,
        AyanamshaFn eval_value,
        int reference_precession_model_id_value = -1,
        const void* model_data_value = nullptr
    ) noexcept;
};

struct SiderealPosition {
    int32_t coordinate_frame_id;
    // On the default ecliptic-of-date plane, this follows calc_ayanamsha_tt():
    // apparent/true longitude unless NONUT requests the mean longitude. On a
    // fixed or invariable plane, it is the unshifted longitude on that plane.
    double tropical_longitude_rad;
    double sidereal_longitude_rad;
    double latitude_rad;
    double distance_au;
    double tropical_longitude_rate_rad_per_day;
    double sidereal_longitude_rate_rad_per_day;

    SiderealPosition() noexcept;
};

// The coordinate frame used by SiderealCoordinates. EQUATORIAL follows the
// conventional Swiss Ephemeris-compatible behavior: it is tropical
// equatorial output, with the requested mean/true-of-date frame.
enum SiderealCoordinateFrame {
    TAIYIN_SIDEREAL_FRAME_MEAN_ECLIPTIC_OF_DATE = 0,
    TAIYIN_SIDEREAL_FRAME_MEAN_EQUATOR_OF_DATE = 1,
    TAIYIN_SIDEREAL_FRAME_TRUE_EQUATOR_OF_DATE = 2,
    TAIYIN_SIDEREAL_FRAME_FIXED_MEAN_ECLIPTIC_AT_EPOCH = 3,
    TAIYIN_SIDEREAL_FRAME_SOLAR_SYSTEM_INVARIABLE = 4,
    TAIYIN_SIDEREAL_FRAME_J2000_ECLIPTIC = 5,
};

// Generic six-value sidereal output. Values follow the native-position flag
// convention: spherical longitude/latitude/distance (and their rates), or
// Cartesian x/y/z and velocity when XYZ is requested. Spherical angular
// values are radians only when the RADIANS flag is requested. Without SPEED,
// values[3..5] are zero, following the generic native-position convention;
// this differs from SiderealPosition, whose unavailable rate fields are NaN.
// position_flags echoes the caller's requested flags rather than internal
// normalization used to evaluate the sidereal ecliptic path.
struct SiderealCoordinates {
    int32_t coordinate_frame_id;
    uint32_t position_flags;
    double values[6];

    SiderealCoordinates() noexcept;
};

// Custom model IDs must be at least TAIYIN_SIDEREAL_AYANAMSHA_CUSTOM_START.
// Built-in IDs and duplicate custom IDs cannot be replaced.
bool add_ayanamsha_model(const AyanamshaModelEntry& entry) noexcept;
// Removes one custom model. Built-in IDs cannot be removed. Registration and
// removal are setup-time changes and must not overlap evaluation.
bool remove_ayanamsha_model(int model_id) noexcept;
// Removes a custom model only when its callback identity still matches. This
// is intended for ownership-aware FFI cleanup: a stale owner must not remove a
// different model that was later registered with the same ID.
bool remove_ayanamsha_model_if_matches(
    int model_id,
    AyanamshaFn expected_eval,
    const void* expected_model_data
) noexcept;
bool find_ayanamsha_model(int model_id, AyanamshaModelEntry* out) noexcept;
bool has_ayanamsha_model(int model_id) noexcept;
Status eval_ayanamsha_model(
    int model_id,
    const AyanamshaDispatchData* data,
    double* out_ayanamsha_rad
) noexcept;

// Evaluates the selected ayanamsha with Swiss Ephemeris _ex semantics. Unless
// NONUT is set, longitude nutation is added to the mean ayanamsha. The result
// is normalized to [0, 2*pi). Star-anchored modes evaluate their built-in ICRF
// reference star with normal apparent corrections. This does not modify
// native_context or global state.
Status calc_ayanamsha_tt(
    const runtime::NativeCalcContext* native_context,
    int ayanamsha_id,
    SplitJulianDate jd_tt,
    uint64_t flags,
    double* out_ayanamsha_rad
) noexcept;

// Native flags retain their normal correction semantics. XYZ and EQUATORIAL
// output are rejected because sidereal longitude is an ecliptic quantity.
Status calc_sidereal_position_tt(
    const runtime::NativeCalcContext* native_context,
    int ayanamsha_id,
    int body_id,
    SplitJulianDate jd_tt,
    uint64_t flags,
    SiderealPosition* out,
    runtime::EphemerisEvalDiagnostic* diagnostic,
    SplitJulianDate reference_epoch_jd = SplitJulianDate(0, NAN)
) noexcept;

Status calc_sidereal_position_ut(
    const runtime::NativeCalcContext* native_context,
    int ayanamsha_id,
    int body_id,
    SplitJulianDate jd_ut,
    uint64_t flags,
    SiderealPosition* out,
    runtime::EphemerisEvalDiagnostic* diagnostic,
    SplitJulianDate reference_epoch_jd = SplitJulianDate(0, NAN)
) noexcept;

// Calculates generic sidereal coordinates. Without EQUATORIAL, output is on
// the selected sidereal reference plane. All ecliptic reference planes here
// are mean planes, so NONUT has no additional effect. With EQUATORIAL, output
// follows the conventional Swiss Ephemeris-compatible behavior: it is tropical
// mean/true equator of date according to NONUT, and is consequently independent
// of the selected ayanamsha, precession policy, and sidereal reference plane.
// XYZ and EQUATORIAL may be combined. Physical-correction flags retain their
// native-position meaning.
Status calc_sidereal_coordinates_tt(
    const runtime::NativeCalcContext* native_context,
    int ayanamsha_id,
    int body_id,
    SplitJulianDate jd_tt,
    uint64_t flags,
    SiderealCoordinates* out,
    runtime::EphemerisEvalDiagnostic* diagnostic,
    SplitJulianDate reference_epoch_jd = SplitJulianDate(0, NAN)
) noexcept;

Status calc_sidereal_coordinates_ut(
    const runtime::NativeCalcContext* native_context,
    int ayanamsha_id,
    int body_id,
    SplitJulianDate jd_ut,
    uint64_t flags,
    SiderealCoordinates* out,
    runtime::EphemerisEvalDiagnostic* diagnostic,
    SplitJulianDate reference_epoch_jd = SplitJulianDate(0, NAN)
) noexcept;

}  // namespace astrology
}  // namespace taiyin

#endif  // TAIYIN_ASTROLOGY_SIDEREAL_H
