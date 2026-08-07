#include "taiyin/astrology/houses.h"

#include "runtime/core/native_context_checks.h"

#include "taiyin/angle.h"
#include "taiyin/dispatch.h"
#include "taiyin/earth_rotation.h"
#include "taiyin/time.h"

#include <cmath>

namespace taiyin {
namespace astrology {
namespace {

constexpr double kHouseSpeedStepDays = 1.0 / 86400.0;
constexpr double kHouseSpeedDiscontinuityRad = 1.0e-12;

Status calc_house_positions_impl(
    const runtime::NativeCalcContext* context,
    SplitJulianDate jd_ut,
    SplitJulianDate jd_tt,
    int house_system_id,
    HouseResult* out
) noexcept {
    if (out) *out = HouseResult();
    if (!context || !out
        || !split_julian_date_is_finite(jd_ut)
        || !split_julian_date_is_finite(jd_tt)
        || !runtime::native_context_has_observer_location(*context)) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    if (context->model_context.obliquity_model_id != 0) {
        return TAIYIN_ERROR_UNSUPPORTED;
    }

    Matrix3x3 precession;
    double mean_obliquity = 0.0;
    NutationAngles nutation;
    if (!dispatch::eval_precession(
            context->model_context.precession_model_id,
            jd_tt,
            nullptr,
            &precession,
            &mean_obliquity)
        || !dispatch::eval_nutation(
            context->model_context.nutation_model_id,
            jd_tt,
            nullptr,
            &nutation)) {
        return TAIYIN_ERROR_UNSUPPORTED;
    }
    const double true_obliquity = mean_obliquity + nutation.deps_rad;
    double gast = 0.0;
    if (!std::isfinite(true_obliquity)
        || !gast_model_rad(
            context->model_context.precession_model_id,
            context->model_context.nutation_model_id,
            jd_ut,
            jd_tt,
            &gast)) {
        return TAIYIN_ERROR_UNSUPPORTED;
    }
    return calc_houses_from_armc(
        normalize_radians(gast + context->observer_location.longitude_rad),
        context->observer_location.latitude_rad,
        true_obliquity,
        house_system_id,
        out);
}

double angular_rate(double before, double after, double step_days) noexcept {
    return normalize_signed_radians(after - before) / (2.0 * step_days);
}

void fill_house_speeds(
    const HouseResult& before,
    const HouseResult& after,
    double step_days,
    HouseResult* out
) noexcept {
    if (!out || before.resolved_system_id != out->resolved_system_id
        || after.resolved_system_id != out->resolved_system_id) {
        if (out) out->flags |= TAIYIN_HOUSE_RESULT_SPEED_UNAVAILABLE;
        return;
    }
    if (out->resolved_system_id == TAIYIN_HOUSE_SYSTEM_WHOLE_SIGN) {
        for (int i = 0; i < 12; ++i) {
            if (std::fabs(normalize_signed_radians(
                    after.cusp_longitude_rad[i]
                    - before.cusp_longitude_rad[i])) > kHouseSpeedDiscontinuityRad) {
                out->flags |= TAIYIN_HOUSE_RESULT_SPEED_UNAVAILABLE;
                return;
            }
        }
    }
    out->armc_rate_rad_per_day = angular_rate(before.armc_rad, after.armc_rad, step_days);
    out->ascendant_rate_rad_per_day =
        angular_rate(before.ascendant_rad, after.ascendant_rad, step_days);
    out->midheaven_rate_rad_per_day =
        angular_rate(before.midheaven_rad, after.midheaven_rad, step_days);
    out->vertex_rate_rad_per_day =
        angular_rate(before.vertex_rad, after.vertex_rad, step_days);
    out->east_point_rate_rad_per_day =
        angular_rate(before.east_point_rad, after.east_point_rad, step_days);
    for (int i = 0; i < 12; ++i) {
        out->cusp_longitude_rate_rad_per_day[i] = angular_rate(
            before.cusp_longitude_rad[i],
            after.cusp_longitude_rad[i],
            step_days);
    }
}

Status ut_from_tt(
    const runtime::NativeCalcContext& context,
    SplitJulianDate jd_tt,
    SplitJulianDate* out_jd_ut
) noexcept {
    if (!out_jd_ut || !split_julian_date_is_finite(jd_tt)) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    SplitJulianDate jd_ut;
    double delta_t = dispatch::eval_delta_t_with_ephemeris_correction(
            context.delta_t_model_id,
            context.ephemeris_family_id,
            jd_tt,
            nullptr,
            nullptr);
    if (!tt_to_ut1_split_jd(jd_tt, delta_t, &jd_ut)) {
        return TAIYIN_ERROR_UNSUPPORTED;
    }
    for (int iteration = 0; iteration < 3; ++iteration) {
        delta_t = dispatch::eval_delta_t_with_ephemeris_correction(
            context.delta_t_model_id,
            context.ephemeris_family_id,
            jd_ut,
            nullptr,
            nullptr);
        if (!tt_to_ut1_split_jd(jd_tt, delta_t, &jd_ut)) {
            return TAIYIN_ERROR_UNSUPPORTED;
        }
    }
    if (!split_julian_date_is_finite(jd_ut)) return TAIYIN_ERROR_UNSUPPORTED;
    *out_jd_ut = jd_ut;
    return TAIYIN_STATUS_OK;
}

}  // namespace

Status calc_houses_ut(
    const runtime::NativeCalcContext* context,
    SplitJulianDate jd_ut,
    int house_system_id,
    HouseResult* out
) noexcept {
    if (!context || !out || !split_julian_date_is_finite(jd_ut)) {
        if (out) *out = HouseResult();
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    const double delta_t = dispatch::eval_delta_t_with_ephemeris_correction(
        context->delta_t_model_id,
        context->ephemeris_family_id,
        jd_ut,
        nullptr,
        nullptr);
    SplitJulianDate jd_tt;
    if (!ut1_to_tt_split_jd(jd_ut, delta_t, &jd_tt)) {
        *out = HouseResult();
        return TAIYIN_ERROR_UNSUPPORTED;
    }
    Status status = calc_house_positions_impl(
        context,
        jd_ut,
        jd_tt,
        house_system_id,
        out);
    if (status != TAIYIN_STATUS_OK) return status;

    HouseResult before;
    HouseResult after;
    const SplitJulianDate before_ut = jd_ut - kHouseSpeedStepDays;
    const SplitJulianDate after_ut = jd_ut + kHouseSpeedStepDays;
    const double before_delta_t = dispatch::eval_delta_t_with_ephemeris_correction(
        context->delta_t_model_id, context->ephemeris_family_id, before_ut, nullptr, nullptr);
    const double after_delta_t = dispatch::eval_delta_t_with_ephemeris_correction(
        context->delta_t_model_id, context->ephemeris_family_id, after_ut, nullptr, nullptr);
    SplitJulianDate before_tt;
    SplitJulianDate after_tt;
    const Status before_status = ut1_to_tt_split_jd(before_ut, before_delta_t, &before_tt)
        ? calc_house_positions_impl(context, before_ut, before_tt, house_system_id, &before)
        : TAIYIN_ERROR_UNSUPPORTED;
    const Status after_status = ut1_to_tt_split_jd(after_ut, after_delta_t, &after_tt)
        ? calc_house_positions_impl(context, after_ut, after_tt, house_system_id, &after)
        : TAIYIN_ERROR_UNSUPPORTED;
    if (before_status != TAIYIN_STATUS_OK || after_status != TAIYIN_STATUS_OK) {
        out->flags |= TAIYIN_HOUSE_RESULT_SPEED_UNAVAILABLE;
        return TAIYIN_STATUS_OK;
    }
    fill_house_speeds(before, after, kHouseSpeedStepDays, out);
    return TAIYIN_STATUS_OK;
}

Status calc_houses_tt(
    const runtime::NativeCalcContext* context,
    SplitJulianDate jd_tt,
    int house_system_id,
    HouseResult* out
) noexcept {
    if (!context || !out || !split_julian_date_is_finite(jd_tt)) {
        if (out) *out = HouseResult();
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    SplitJulianDate jd_ut(0, NAN);
    const Status status = ut_from_tt(*context, jd_tt, &jd_ut);
    if (status != TAIYIN_STATUS_OK) {
        if (out) *out = HouseResult();
        return status;
    }
    Status result_status =
        calc_house_positions_impl(context, jd_ut, jd_tt, house_system_id, out);
    if (result_status != TAIYIN_STATUS_OK) return result_status;

    HouseResult before;
    HouseResult after;
    const SplitJulianDate before_tt = jd_tt - kHouseSpeedStepDays;
    const SplitJulianDate after_tt = jd_tt + kHouseSpeedStepDays;
    SplitJulianDate before_ut(0, NAN);
    SplitJulianDate after_ut(0, NAN);
    const Status before_time_status = ut_from_tt(*context, before_tt, &before_ut);
    const Status after_time_status = ut_from_tt(*context, after_tt, &after_ut);
    if (before_time_status != TAIYIN_STATUS_OK || after_time_status != TAIYIN_STATUS_OK
        || calc_house_positions_impl(
            context, before_ut, before_tt, house_system_id, &before) != TAIYIN_STATUS_OK
        || calc_house_positions_impl(
            context, after_ut, after_tt, house_system_id, &after) != TAIYIN_STATUS_OK) {
        out->flags |= TAIYIN_HOUSE_RESULT_SPEED_UNAVAILABLE;
        return TAIYIN_STATUS_OK;
    }
    fill_house_speeds(before, after, kHouseSpeedStepDays, out);
    return TAIYIN_STATUS_OK;
}

}  // namespace astrology
}  // namespace taiyin
