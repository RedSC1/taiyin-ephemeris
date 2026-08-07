#ifndef TAIYIN_RUNTIME_FAST_APPARENT_H
#define TAIYIN_RUNTIME_FAST_APPARENT_H

#include "taiyin/runtime/native_context.h"
#include "taiyin/state.h"
#include "taiyin/status.h"
#include "taiyin/time.h"

#include <stdint.h>
#include <vector>

namespace taiyin {
namespace runtime {

enum FastApparentFrame {
    FAST_APPARENT_TRUE_EQUATOR_OF_DATE = 1,
    FAST_APPARENT_TRUE_ECLIPTIC_OF_DATE = 2,
};

struct FastApparentCorrectionEpochSample;

struct FastApparentOptions {
    FastApparentFrame frame;
    bool with_velocity;
    bool true_position;
    const FastApparentCorrectionEpochSample* correction_sample;

    FastApparentOptions() noexcept
        : frame(FAST_APPARENT_TRUE_EQUATOR_OF_DATE),
          with_velocity(false),
          true_position(false),
          correction_sample(nullptr) {}
};

struct FastApparentBody2State {
    CartesianState body_0;
    CartesianState body_1;
};

struct FastApparentCorrectionBodySample {
    CartesianState geometric;
    CartesianState light_time_delta;
    CartesianState deflection_delta;
    CartesianState aberration_delta;
};

struct FastApparentCorrectionEpochSample {
    SplitJulianDate jd_tt;
    double matrix[9];
    double matrix_dot[9];
    FastApparentCorrectionBodySample body_0;
    FastApparentCorrectionBodySample body_1;
};

enum FastApparentCorrectionInterpolationKind {
    FAST_APPARENT_CORRECTION_INTERPOLATION_LINEAR = 1,
    FAST_APPARENT_CORRECTION_INTERPOLATION_QUADRATIC = 2,
    FAST_APPARENT_CORRECTION_INTERPOLATION_CUBIC = 3,
    FAST_APPARENT_CORRECTION_INTERPOLATION_CATMULL_ROM = 4,
};

struct FastApparentCorrectionConfig {
    double initial_half_days;
    double sample_step_days;
    FastApparentCorrectionInterpolationKind interpolation_kind;

    FastApparentCorrectionConfig() noexcept
        : initial_half_days(3.0 / 24.0),
          sample_step_days(3.0 / 24.0),
          interpolation_kind(FAST_APPARENT_CORRECTION_INTERPOLATION_LINEAR) {}
};

struct FastApparentCorrectionSeries {
    SplitJulianDate start_jd_tt;
    SplitJulianDate end_jd_tt;
    double sample_step_days;
    FastApparentCorrectionInterpolationKind interpolation_kind;
    uint64_t identity_hash;
    std::vector<FastApparentCorrectionEpochSample> samples;

    FastApparentCorrectionSeries() noexcept
        : start_jd_tt(),
          end_jd_tt(),
          sample_step_days(0.0),
          interpolation_kind(FAST_APPARENT_CORRECTION_INTERPOLATION_LINEAR),
          identity_hash(0u),
          samples() {}
};

Status init_fast_correction_series(
    const NativeCalcContext* context,
    int body_0_id,
    int body_1_id,
    const FastApparentOptions& options,
    const FastApparentCorrectionConfig& config,
    const SplitJulianDate& center_jd_tt,
    FastApparentCorrectionSeries* series,
    EphemerisEvalDiagnostic* diagnostic
);

Status get_fast_correction(
    const NativeCalcContext* context,
    int body_0_id,
    int body_1_id,
    const FastApparentOptions& options,
    const FastApparentCorrectionConfig& config,
    const SplitJulianDate& jd_tt,
    FastApparentCorrectionSeries* series,
    EphemerisEvalDiagnostic* diagnostic,
    FastApparentCorrectionEpochSample* out
);

Status eval_fast_apparent_body_tdb(
    const NativeCalcContext* context,
    const SplitJulianDate& jd_tdb,
    const SplitJulianDate& jd_tt,
    int body_id,
    const FastApparentOptions& options,
    CartesianState* out,
    EphemerisEvalDiagnostic* diagnostic
) noexcept;

Status eval_fast_apparent_body_2_tdb(
    const NativeCalcContext* context,
    const SplitJulianDate& jd_tdb,
    const SplitJulianDate& jd_tt,
    int body_0_id,
    int body_1_id,
    const FastApparentOptions& options,
    FastApparentBody2State* out,
    EphemerisEvalDiagnostic* diagnostic
) noexcept;

}  // namespace runtime
}  // namespace taiyin

#endif  // TAIYIN_RUNTIME_FAST_APPARENT_H
