#include "taiyin/runtime/lunar_limb.h"

#include "taiyin/apparent_position.h"
#include "taiyin/dispatch.h"
#include "taiyin/lunar_limb_tll1.h"
#include "taiyin/runtime/runtime.h"

#include <cmath>

namespace taiyin {
namespace runtime {

namespace {

Matrix3x3 matrix_from_array(const double values[9]) noexcept {
    Matrix3x3 matrix = {};
    for (int row = 0; row < 3; ++row) {
        for (int column = 0; column < 3; ++column) {
            matrix.m[row][column] = values[row * 3 + column];
        }
    }
    return matrix;
}

}  // namespace

PreparedLunarLimbQuery::PreparedLunarLimbQuery() noexcept
    : model(nullptr),
      apparent_to_lunar_body(matrix3x3_identity()),
      valid(false) {}

Status apparent_limb_direction_toward_target(
    const Vector3& observer_to_moon,
    const Vector3& observer_to_target,
    bool away_from_target,
    Vector3* out
) noexcept {
    if (out) *out = Vector3{NAN, NAN, NAN};
    if (!out) return TAIYIN_ERROR_INVALID_ARGUMENT;
    const double moon_distance = vector3_norm(observer_to_moon);
    const double target_distance = vector3_norm(observer_to_target);
    if (!(moon_distance > 0.0) || !(target_distance > 0.0)) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    const Vector3 moon_unit = vector3_scale(observer_to_moon, 1.0 / moon_distance);
    const Vector3 target_unit = vector3_scale(observer_to_target, 1.0 / target_distance);
    Vector3 direction = vector3_subtract(
        target_unit,
        vector3_scale(moon_unit, vector3_dot(target_unit, moon_unit)));
    const double direction_norm = vector3_norm(direction);
    if (!(direction_norm > 0.0)) return TAIYIN_EPHEMERIS_ERROR_EVAL_FAILED;
    direction = vector3_scale(
        direction,
        (away_from_target ? -1.0 : 1.0) / direction_norm);
    *out = direction;
    return TAIYIN_STATUS_OK;
}

Status eval_lunar_limb_radius_m(
    const NativeCalcContext* context,
    SplitJulianDate jd_tdb,
    const Vector3& moon_to_observer_j2000,
    const Vector3& apparent_limb_direction_j2000,
    double* out_radius_m,
    LunarLimbViewCoordinates* out_view
) noexcept {
    if (out_radius_m) *out_radius_m = std::nan("");
    if (out_view) *out_view = LunarLimbViewCoordinates();
    if (!context || !out_radius_m || !split_julian_date_is_finite(jd_tdb)) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    const Tll1LunarLimbModel* model = global_lunar_limb_model();
    if (!model) {
        return TAIYIN_ERROR_UNSUPPORTED;
    }

    LunarLimbViewCoordinates view;
    const Status view_status = lunar_limb_view_coordinates_j2000(
        jd_tdb,
        moon_to_observer_j2000,
        apparent_limb_direction_j2000,
        &view);
    if (view_status != TAIYIN_STATUS_OK) return view_status;

    const Status radius_status = tll1_lunar_limb_radius_m(
        model,
        view.libration_longitude_deg,
        view.libration_latitude_deg,
        view.position_angle_deg,
        out_radius_m);
    if (radius_status != TAIYIN_STATUS_OK) return radius_status;
    if (out_view) *out_view = view;
    return TAIYIN_STATUS_OK;
}

Status prepare_lunar_limb_query(
    const NativeCalcContext* context,
    SplitJulianDate jd_tt,
    bool true_position,
    int output_frame_id,
    PreparedLunarLimbQuery* out
) noexcept {
    if (out) *out = PreparedLunarLimbQuery();
    if (!context || !out || !split_julian_date_is_finite(jd_tt)) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    const Tll1LunarLimbModel* model = global_lunar_limb_model();
    if (!model) {
        return TAIYIN_ERROR_UNSUPPORTED;
    }

    uint32_t matrix_flags = context->apparent_options.flags | TAIYIN_APPARENT_SPHERICAL;
    matrix_flags &= ~(TAIYIN_APPARENT_VELOCITY | TAIYIN_APPARENT_ACCELERATION);
    if (true_position) {
        matrix_flags &= ~(TAIYIN_APPARENT_LIGHT_TIME
            | TAIYIN_APPARENT_ABERRATION
            | TAIYIN_APPARENT_DEFLECTION
            | TAIYIN_APPARENT_SHAPIRO_DELAY
            | TAIYIN_APPARENT_TOPOCENTRIC);
    }
    double precession_matrix[9] = {};
    double nutation_matrix[9] = {};
    double output_matrix[9] = {};
    double output_matrix_dot[9] = {};
    double output_matrix_ddot[9] = {};
    if (!calc_apparent_matrices(
            jd_tt,
            matrix_flags,
            output_frame_id,
            context->model_context.precession_model_id,
            context->model_context.nutation_model_id,
            context->model_context.obliquity_model_id,
            context->model_context.frame_route_id,
            context->apparent_options.celestial_pole_offset_dx_rad,
            context->apparent_options.celestial_pole_offset_dy_rad,
            context->apparent_options.celestial_pole_offset_dx_rate_rad_per_day,
            context->apparent_options.celestial_pole_offset_dy_rate_rad_per_day,
            context->apparent_options.matrix_derivative_step_days,
            precession_matrix,
            nutation_matrix,
            output_matrix,
            output_matrix_dot,
            output_matrix_ddot,
            nullptr,
            nullptr,
            nullptr,
            nullptr)) {
        return TAIYIN_EPHEMERIS_ERROR_EVAL_FAILED;
    }
    const double tdb_minus_tt = dispatch::eval_tdb(
        context->model_context.tdb_model_id, jd_tt, nullptr);
    Matrix3x3 j2000_to_lunar_body;
    if (!iau2009_moon_j2000_to_mean_earth_matrix(
            jd_tt + tdb_minus_tt / 86400.0, &j2000_to_lunar_body)) {
        return TAIYIN_EPHEMERIS_ERROR_EVAL_FAILED;
    }
    const Matrix3x3 apparent_to_j2000 = matrix3x3_transpose(
        matrix_from_array(output_matrix));
    out->model = model;
    out->apparent_to_lunar_body = matrix3x3_multiply(
        j2000_to_lunar_body, apparent_to_j2000);
    out->valid = true;
    return TAIYIN_STATUS_OK;
}

Status eval_prepared_lunar_limb_radius_m(
    const PreparedLunarLimbQuery* query,
    const Vector3& moon_to_observer,
    const Vector3& apparent_limb_direction,
    double* out_radius_m,
    LunarLimbViewCoordinates* out_view
) noexcept {
    if (out_radius_m) *out_radius_m = std::nan("");
    if (out_view) *out_view = LunarLimbViewCoordinates();
    if (!query || !query->valid || !query->model || !out_radius_m) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    LunarLimbViewCoordinates view;
    const Status view_status = lunar_limb_view_coordinates_body_fixed(
        matrix3x3_multiply_vector(query->apparent_to_lunar_body, moon_to_observer),
        matrix3x3_multiply_vector(query->apparent_to_lunar_body, apparent_limb_direction),
        &view);
    if (view_status != TAIYIN_STATUS_OK) return view_status;
    const Status radius_status = tll1_lunar_limb_radius_m(
        query->model,
        view.libration_longitude_deg,
        view.libration_latitude_deg,
        view.position_angle_deg,
        out_radius_m);
    if (radius_status != TAIYIN_STATUS_OK) return radius_status;
    if (out_view) *out_view = view;
    return TAIYIN_STATUS_OK;
}

Status eval_lunar_limb_radius_from_apparent_frame_m(
    const NativeCalcContext* context,
    SplitJulianDate jd_tt,
    bool true_position,
    int output_frame_id,
    const Vector3& moon_to_observer,
    const Vector3& apparent_limb_direction,
    double* out_radius_m,
    LunarLimbViewCoordinates* out_view
) noexcept {
    PreparedLunarLimbQuery query;
    const Status prepare_status = prepare_lunar_limb_query(
        context, jd_tt, true_position, output_frame_id, &query);
    if (prepare_status != TAIYIN_STATUS_OK) return prepare_status;
    return eval_prepared_lunar_limb_radius_m(
        &query,
        moon_to_observer,
        apparent_limb_direction,
        out_radius_m,
        out_view);
}

}  // namespace runtime
}  // namespace taiyin
