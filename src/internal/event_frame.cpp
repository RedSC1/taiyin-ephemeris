#include "taiyin/internal/event_frame.h"

#include "taiyin/angle.h"
#include "taiyin/physical_constants.h"
#include "taiyin/time.h"

#include <cmath>

namespace taiyin {
namespace internal {
namespace {

bool finite_nutation(const NutationAngles& value) noexcept {
    return std::isfinite(value.dpsi_rad)
        && std::isfinite(value.deps_rad)
        && std::isfinite(value.mean_obliquity_rad)
        && std::isfinite(value.true_obliquity_rad);
}

bool finite_matrix(const Matrix3x3& value) noexcept {
    for (int row = 0; row < 3; ++row) {
        for (int col = 0; col < 3; ++col) {
            if (!std::isfinite(value.m[row][col])) {
                return false;
            }
        }
    }
    return true;
}

bool supported_event_frame(int output_frame_id) noexcept {
    return output_frame_id == TAIYIN_APPARENT_FRAME_ICRF
        || output_frame_id == TAIYIN_APPARENT_FRAME_TRUE_EQUATOR_OF_DATE
        || output_frame_id == TAIYIN_APPARENT_FRAME_TRUE_ECLIPTIC_OF_DATE
        || output_frame_id == TAIYIN_APPARENT_FRAME_J2000_MEAN_EQUATOR
        || output_frame_id == TAIYIN_APPARENT_FRAME_J2000_ECLIPTIC
        || output_frame_id == TAIYIN_APPARENT_FRAME_MEAN_EQUATOR_OF_DATE
        || output_frame_id == TAIYIN_APPARENT_FRAME_MEAN_ECLIPTIC_OF_DATE;
}

bool event_frame_needs_nutation(int output_frame_id) noexcept {
    return output_frame_id == TAIYIN_APPARENT_FRAME_TRUE_EQUATOR_OF_DATE
        || output_frame_id == TAIYIN_APPARENT_FRAME_TRUE_ECLIPTIC_OF_DATE;
}

bool event_frame_needs_precession(int output_frame_id) noexcept {
    return output_frame_id != TAIYIN_APPARENT_FRAME_ICRF;
}

}  // namespace

bool resolve_event_frame_models(
    int precession_model_id,
    int nutation_model_id,
    dispatch::PrecessionModelEntry* precession,
    dispatch::NutationModelEntry* nutation
) noexcept {
    if (!precession || !nutation) {
        return false;
    }

    *precession = dispatch::PrecessionModelEntry();
    *nutation = dispatch::NutationModelEntry();

    if (!dispatch::select_precession_model(precession_model_id, precession)
        || !precession->eval
        || !dispatch::select_nutation_model(nutation_model_id, nutation)
        || !nutation->eval) {
        return false;
    }
    return true;
}

bool eval_event_output_frame_matrix(
    const SplitJulianDate& jd_tt,
    int output_frame_id,
    const dispatch::PrecessionModelEntry& precession,
    const dispatch::NutationModelEntry& nutation,
    Matrix3x3* out_precession,
    NutationAngles* out_nutation,
    Matrix3x3* out_output_matrix
) noexcept {
    if (!split_julian_date_is_finite(jd_tt) || !supported_event_frame(output_frame_id) || !out_output_matrix) {
        return false;
    }

    Matrix3x3 precession_matrix = matrix3x3_identity();
    double mean_obliquity = 0.0;
    if (event_frame_needs_precession(output_frame_id)) {
        if (!precession.eval) {
            return false;
        }
        const SplitJulianDate precession_jd = output_frame_id == TAIYIN_APPARENT_FRAME_J2000_MEAN_EQUATOR
                || output_frame_id == TAIYIN_APPARENT_FRAME_J2000_ECLIPTIC
            ? SplitJulianDate(2451545, 0.0)
            : jd_tt;
        if (!precession.eval(precession_jd, 0, &precession_matrix, &mean_obliquity)
            || !std::isfinite(mean_obliquity)
            || !finite_matrix(precession_matrix)) {
            return false;
        }
    }

    NutationAngles nutation_angles;
    nutation_angles.dpsi_rad = 0.0;
    nutation_angles.deps_rad = 0.0;
    nutation_angles.mean_obliquity_rad = mean_obliquity;
    nutation_angles.true_obliquity_rad = mean_obliquity;
    if (event_frame_needs_nutation(output_frame_id)) {
        if (!nutation.eval || !nutation.eval(jd_tt, 0, &nutation_angles)) {
            return false;
        }
        nutation_angles.mean_obliquity_rad = mean_obliquity;
        nutation_angles.true_obliquity_rad = mean_obliquity + nutation_angles.deps_rad;
        if (!finite_nutation(nutation_angles)) {
            return false;
        }
    }

    Matrix3x3 output = matrix3x3_identity();
    switch (output_frame_id) {
    case TAIYIN_APPARENT_FRAME_ICRF:
        output = matrix3x3_identity();
        break;
    case TAIYIN_APPARENT_FRAME_J2000_MEAN_EQUATOR:
    case TAIYIN_APPARENT_FRAME_MEAN_EQUATOR_OF_DATE:
        output = precession_matrix;
        break;
    case TAIYIN_APPARENT_FRAME_J2000_ECLIPTIC:
    case TAIYIN_APPARENT_FRAME_MEAN_ECLIPTIC_OF_DATE:
        output = matrix3x3_multiply(rotation_x_matrix(mean_obliquity), precession_matrix);
        break;
    case TAIYIN_APPARENT_FRAME_TRUE_EQUATOR_OF_DATE:
        output = matrix3x3_multiply(nutation_matrix(nutation_angles), precession_matrix);
        break;
    case TAIYIN_APPARENT_FRAME_TRUE_ECLIPTIC_OF_DATE:
        output = matrix3x3_multiply(
            rotation_x_matrix(nutation_angles.true_obliquity_rad),
            matrix3x3_multiply(nutation_matrix(nutation_angles), precession_matrix));
        break;
    default:
        return false;
    }
    if (!finite_matrix(output)) {
        return false;
    }

    if (out_precession) {
        *out_precession = precession_matrix;
    }
    if (out_nutation) {
        *out_nutation = nutation_angles;
    }
    *out_output_matrix = output;
    return true;
}

}  // namespace internal
}  // namespace taiyin
