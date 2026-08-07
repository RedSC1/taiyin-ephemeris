#ifndef TAIYIN_RUNTIME_NATIVE_POSITION_H
#define TAIYIN_RUNTIME_NATIVE_POSITION_H

#include "taiyin/runtime/native_context.h"
#include "taiyin/state.h"
#include "taiyin/status.h"

#include <stddef.h>
#include <stdint.h>

namespace taiyin {
namespace runtime {

const uint32_t TAIYIN_NATIVE_POSITION_SPEED = 1u << 0;
const uint32_t TAIYIN_NATIVE_POSITION_XYZ = 1u << 1;
const uint32_t TAIYIN_NATIVE_POSITION_EQUATORIAL = 1u << 2;
const uint32_t TAIYIN_NATIVE_POSITION_RADIANS = 1u << 3;
const uint32_t TAIYIN_NATIVE_POSITION_TRUEPOS = 1u << 4;
const uint32_t TAIYIN_NATIVE_POSITION_NO_ABERR = 1u << 5;
const uint32_t TAIYIN_NATIVE_POSITION_NO_GDEFL = 1u << 6;
const uint32_t TAIYIN_NATIVE_POSITION_ASTROMETRIC = 1u << 7;
const uint32_t TAIYIN_NATIVE_POSITION_NONUT = 1u << 8;
const uint32_t TAIYIN_NATIVE_POSITION_TOPOCENTRIC = 1u << 9;
// Explicitly allow a major-planet barycenter to stand in for the requested
// physical body when the selected route has no body/COB data. The requested
// target remains in diagnostics; component_target_id records the barycenter.
const uint32_t TAIYIN_NATIVE_POSITION_ALLOW_BARYCENTER_APPROX = 1u << 10;

// Setup-time extension hook for a negative target ID whose position is
// computed from other targets or an analytic definition rather than an
// ephemeris block. The callback owns its output semantics, including whether
// a requested distance or Cartesian state is defined; calc_position_tdb()
// forwards its result without imposing the physical-route finite-value check.
// It must not throw.
typedef Status (*NativePositionEvaluatorFn)(
    const NativeCalcContext* context,
    int target_id,
    const SplitJulianDate& jd_tdb,
    const SplitJulianDate& jd_tt,
    uint32_t flags,
    double out[6],
    EphemerisEvalDiagnostic* diagnostic
);

// Optional exact-state counterpart for a registered position evaluator. When
// present, calc_state_tdb() forwards the requested state directly to it. When
// absent, calc_state_tdb() falls back to finite differences of the position
// evaluator's XYZ output. It must not throw.
typedef Status (*NativeStateEvaluatorFn)(
    const NativeCalcContext* context,
    int target_id,
    const SplitJulianDate& jd_tdb,
    const SplitJulianDate& jd_tt,
    uint32_t flags,
    CartesianState* out,
    EphemerisEvalDiagnostic* diagnostic
);

// Register before concurrent calculations begin. Negative evaluator IDs keep
// the physical ephemeris route fast path free of an evaluator-table lookup.
// Evaluators do not participate in ephemeris catalogs or segment caching.
// Repeating the same target and function pointers is harmless; a conflicting
// registration for an existing target is rejected.
bool register_global_native_position_evaluator(
    int target_id,
    NativePositionEvaluatorFn evaluator,
    NativeStateEvaluatorFn state_evaluator = nullptr
) noexcept;

// Remove one setup-time evaluator. Returns false when target_id is not
// currently registered. Must not overlap calculations.
bool unregister_global_native_position_evaluator(int target_id) noexcept;

Status calc_position_tdb(
    const NativeCalcContext* context,
    int target_id,
    const SplitJulianDate& jd_tdb,
    const SplitJulianDate& jd_tt,
    uint32_t flags,
    double out[6],
    EphemerisEvalDiagnostic* diagnostic
) noexcept;

Status calc_position_tt(
    const NativeCalcContext* context,
    int target_id,
    const SplitJulianDate& jd_tt,
    uint32_t flags,
    double out[6],
    EphemerisEvalDiagnostic* diagnostic
) noexcept;

Status calc_position_ut_delta_t(
    const NativeCalcContext* context,
    int target_id,
    const SplitJulianDate& jd_ut1,
    double delta_t_seconds,
    uint32_t flags,
    double out[6],
    EphemerisEvalDiagnostic* diagnostic
) noexcept;

Status calc_position_ut(
    const NativeCalcContext* context,
    int target_id,
    const SplitJulianDate& jd_ut,
    uint32_t flags,
    double out[6],
    EphemerisEvalDiagnostic* diagnostic
) noexcept;

Status calc_position_utc(
    const NativeCalcContext* context,
    int target_id,
    const CalendarDateTime& datetime_utc,
    uint32_t flags,
    double out[6],
    EphemerisEvalDiagnostic* diagnostic
) noexcept;

Status calc_positions_tdb(
    const NativeCalcContext* context,
    const int* target_ids,
    size_t target_count,
    const SplitJulianDate& jd_tdb,
    const SplitJulianDate& jd_tt,
    uint32_t flags,
    double* out,
    EphemerisEvalDiagnostic* diagnostics
) noexcept;

Status calc_positions_tt(
    const NativeCalcContext* context,
    const int* target_ids,
    size_t target_count,
    const SplitJulianDate& jd_tt,
    uint32_t flags,
    double* out,
    EphemerisEvalDiagnostic* diagnostics
) noexcept;

Status calc_positions_ut_delta_t(
    const NativeCalcContext* context,
    const int* target_ids,
    size_t target_count,
    const SplitJulianDate& jd_ut1,
    double delta_t_seconds,
    uint32_t flags,
    double* out,
    EphemerisEvalDiagnostic* diagnostics
) noexcept;

Status calc_positions_ut(
    const NativeCalcContext* context,
    const int* target_ids,
    size_t target_count,
    const SplitJulianDate& jd_ut,
    uint32_t flags,
    double* out,
    EphemerisEvalDiagnostic* diagnostics
) noexcept;

Status calc_positions_utc(
    const NativeCalcContext* context,
    const int* target_ids,
    size_t target_count,
    const CalendarDateTime& datetime_utc,
    uint32_t flags,
    double* out,
    EphemerisEvalDiagnostic* diagnostics
) noexcept;

// Returns Cartesian position, velocity, and acceleration in the same resolved
// output frame selected by the native position flags/context. SPEED and XYZ are
// implied for these entry points; other native flags keep their usual meaning.
Status calc_state_tdb(
    const NativeCalcContext* context,
    int target_id,
    const SplitJulianDate& jd_tdb,
    const SplitJulianDate& jd_tt,
    uint32_t flags,
    CartesianState* out,
    EphemerisEvalDiagnostic* diagnostic
) noexcept;

Status calc_state_tt(
    const NativeCalcContext* context,
    int target_id,
    const SplitJulianDate& jd_tt,
    uint32_t flags,
    CartesianState* out,
    EphemerisEvalDiagnostic* diagnostic
) noexcept;

Status calc_state_ut_delta_t(
    const NativeCalcContext* context,
    int target_id,
    const SplitJulianDate& jd_ut1,
    double delta_t_seconds,
    uint32_t flags,
    CartesianState* out,
    EphemerisEvalDiagnostic* diagnostic
) noexcept;

Status calc_state_ut(
    const NativeCalcContext* context,
    int target_id,
    const SplitJulianDate& jd_ut,
    uint32_t flags,
    CartesianState* out,
    EphemerisEvalDiagnostic* diagnostic
) noexcept;

Status calc_state_utc(
    const NativeCalcContext* context,
    int target_id,
    const CalendarDateTime& datetime_utc,
    uint32_t flags,
    CartesianState* out,
    EphemerisEvalDiagnostic* diagnostic
) noexcept;

Status calc_default_position_tdb(
    int target_id,
    const SplitJulianDate& jd_tdb,
    const SplitJulianDate& jd_tt,
    uint32_t flags,
    double out[6],
    EphemerisEvalDiagnostic* diagnostic
) noexcept;

Status calc_default_position_tt(
    int target_id,
    const SplitJulianDate& jd_tt,
    uint32_t flags,
    double out[6],
    EphemerisEvalDiagnostic* diagnostic
) noexcept;

Status calc_default_position_ut_delta_t(
    int target_id,
    const SplitJulianDate& jd_ut1,
    double delta_t_seconds,
    uint32_t flags,
    double out[6],
    EphemerisEvalDiagnostic* diagnostic
) noexcept;

Status calc_default_position_ut(
    int target_id,
    const SplitJulianDate& jd_ut,
    uint32_t flags,
    double out[6],
    EphemerisEvalDiagnostic* diagnostic
) noexcept;

Status calc_default_position_utc(
    int target_id,
    const CalendarDateTime& datetime_utc,
    uint32_t flags,
    double out[6],
    EphemerisEvalDiagnostic* diagnostic
) noexcept;

Status calc_default_state_tdb(
    int target_id,
    const SplitJulianDate& jd_tdb,
    const SplitJulianDate& jd_tt,
    uint32_t flags,
    CartesianState* out,
    EphemerisEvalDiagnostic* diagnostic
) noexcept;

Status calc_default_state_tt(
    int target_id,
    const SplitJulianDate& jd_tt,
    uint32_t flags,
    CartesianState* out,
    EphemerisEvalDiagnostic* diagnostic
) noexcept;

Status calc_default_state_ut_delta_t(
    int target_id,
    const SplitJulianDate& jd_ut1,
    double delta_t_seconds,
    uint32_t flags,
    CartesianState* out,
    EphemerisEvalDiagnostic* diagnostic
) noexcept;

Status calc_default_state_ut(
    int target_id,
    const SplitJulianDate& jd_ut,
    uint32_t flags,
    CartesianState* out,
    EphemerisEvalDiagnostic* diagnostic
) noexcept;

Status calc_default_state_utc(
    int target_id,
    const CalendarDateTime& datetime_utc,
    uint32_t flags,
    CartesianState* out,
    EphemerisEvalDiagnostic* diagnostic
) noexcept;

}  // namespace runtime
}  // namespace taiyin

#endif  // TAIYIN_RUNTIME_NATIVE_POSITION_H
