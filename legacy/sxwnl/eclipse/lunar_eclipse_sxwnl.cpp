#include "lunar_eclipse_sxwnl.h"

#include "taiyin/body_id.h"
#include "taiyin/dispatch.h"
#include "taiyin/geodetic_constants.h"
#include "taiyin/runtime/eclipse_search.h"
#include "taiyin/runtime/lunar_limb.h"

#include "runtime/apparent/fast_apparent.h"

#include <algorithm>
#include <cmath>

namespace taiyin {
namespace runtime {
namespace sxwnl {
namespace lunar {

namespace {

constexpr double kAuKm = 149597870.7;
constexpr double kEarthEquatorialRadiusKm = TAIYIN_WGS84_A_KM;
constexpr double kSunRadiusKm = 695700.0;
constexpr double kMoonAlmanacRadiusRatio = 0.2725076;

struct ShadowScales {
    double earth;
    double sun;
    double parallax;
};

ShadowScales shadow_scales(uint8_t shadow_model) {
    dispatch::EclipseShadowModelEntry entry;
    if (dispatch::select_eclipse_shadow_model(static_cast<int>(shadow_model), &entry)) {
        return {entry.earth_scale, entry.sun_scale, entry.parallax_scale};
    }
    return {1.01, 1.0, 1.0};
}

double moon_radius_km(uint8_t moon_radius_model) {
    dispatch::EclipseMoonRadiusModelEntry entry;
    if (dispatch::find_eclipse_moon_radius_model(static_cast<int>(moon_radius_model), &entry)) {
        return entry.radius_km;
    }
    return kMoonAlmanacRadiusRatio * kEarthEquatorialRadiusKm;
}

Status eval_lunar_eclipse_limb_radii_km(
    const NativeCalcContext* context,
    SplitJulianDate jd_tt,
    uint64_t flags,
    const Vector3& moon,
    const Vector3& sun,
    double* out_toward_km,
    double* out_away_km
) noexcept {
    if (!context || !out_toward_km || !out_away_km) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    const double moon_distance = vector3_norm(moon);
    const double sun_distance = vector3_norm(sun);
    if (!(moon_distance > 0.0) || !(sun_distance > 0.0)) {
        return TAIYIN_EPHEMERIS_ERROR_EVAL_FAILED;
    }

    const Vector3 moon_unit = vector3_scale(moon, 1.0 / moon_distance);
    const Vector3 antisolar_unit = vector3_scale(sun, -1.0 / sun_distance);
    Vector3 toward_shadow = vector3_subtract(
        antisolar_unit,
        vector3_scale(moon_unit, vector3_dot(antisolar_unit, moon_unit)));
    const double toward_norm = vector3_norm(toward_shadow);
    if (!(toward_norm > 0.0)) {
        const double radius_km = moon_radius_km(context->eclipse_moon_radius_model_id);
        *out_toward_km = radius_km;
        *out_away_km = radius_km;
        return TAIYIN_STATUS_OK;
    }
    toward_shadow = vector3_scale(toward_shadow, 1.0 / toward_norm);

    const Vector3 moon_to_observer = vector3_scale(moon, -1.0);
    const Vector3 away_from_shadow = vector3_scale(toward_shadow, -1.0);
    double toward_m = std::nan("");
    double away_m = std::nan("");
    Status status = eval_lunar_limb_radius_from_apparent_frame_m(
        context,
        jd_tt,
        (flags & TAIYIN_ECLIPSE_TRUEPOS) != 0u,
        TAIYIN_APPARENT_FRAME_TRUE_ECLIPTIC_OF_DATE,
        moon_to_observer,
        toward_shadow,
        &toward_m);
    if (status != TAIYIN_STATUS_OK) return status;
    status = eval_lunar_limb_radius_from_apparent_frame_m(
        context,
        jd_tt,
        (flags & TAIYIN_ECLIPSE_TRUEPOS) != 0u,
        TAIYIN_APPARENT_FRAME_TRUE_ECLIPTIC_OF_DATE,
        moon_to_observer,
        away_from_shadow,
        &away_m);
    if (status != TAIYIN_STATUS_OK) return status;
    *out_toward_km = toward_m / 1000.0;
    *out_away_km = away_m / 1000.0;
    return TAIYIN_STATUS_OK;
}

double lecMax_dt(const LecMaxResult& p) noexcept {
    const double denom = p.vx_rad_per_day * p.vx_rad_per_day
                       + p.vy_rad_per_day * p.vy_rad_per_day;
    if (denom < 1e-30) return 0.0;
    return -(p.geometry.x_rad * p.vx_rad_per_day + p.geometry.y_rad * p.vy_rad_per_day) / denom;
}

Status lecInput(
    const NativeCalcContext* context,
    SplitJulianDate jd_tt,
    uint64_t flags,
    LecInput* out,
    EphemerisEvalDiagnostic* diagnostic
) noexcept {
    if (!context || !out || !split_julian_date_is_finite(jd_tt)) return TAIYIN_ERROR_INVALID_ARGUMENT;
    const double tdb_minus_tt = dispatch::eval_tdb(
        context->model_context.tdb_model_id, jd_tt, 0);
    SplitJulianDate jd_tdb = jd_tt;
    if (!add_seconds_to_split_jd(jd_tdb, tdb_minus_tt, &jd_tdb)) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }

    FastApparentOptions options;
    options.frame = FAST_APPARENT_TRUE_ECLIPTIC_OF_DATE;
    options.with_velocity = false;
    options.true_position = (flags & TAIYIN_ECLIPSE_TRUEPOS) != 0;
    FastApparentBody2State pair;
    const Status st = eval_fast_apparent_body_2_tdb(
        context,
        jd_tdb,
        jd_tt,
        TAIYIN_BODY_MOON,
        TAIYIN_BODY_SUN,
        options,
        &pair,
        diagnostic);
    if (st != TAIYIN_STATUS_OK) return st;

    const Vector3& moon = pair.body_0.position_au;
    const Vector3& sun = pair.body_1.position_au;
    const double moon_distance = std::sqrt(moon.x * moon.x + moon.y * moon.y + moon.z * moon.z);
    const double sun_distance = std::sqrt(sun.x * sun.x + sun.y * sun.y + sun.z * sun.z);
    if (!std::isfinite(moon_distance) || !std::isfinite(sun_distance)
        || !(moon_distance > 0.0) || !(sun_distance > 0.0)) {
        return TAIYIN_EPHEMERIS_ERROR_EVAL_FAILED;
    }

    const ShadowScales sc = shadow_scales(context->eclipse_shadow_model_id);
    out->moon_lon_rad = std::atan2(moon.y, moon.x);
    out->moon_lat_rad = std::asin(std::max(-1.0, std::min(1.0, moon.z / moon_distance)));
    out->moon_dist_au = moon_distance;
    out->sun_lon_rad = std::atan2(sun.y, sun.x);
    out->sun_lat_rad = std::asin(std::max(-1.0, std::min(1.0, sun.z / sun_distance)));
    out->sun_dist_au = sun_distance;
    out->earth_radius_km = kEarthEquatorialRadiusKm;
    out->sun_radius_km = kSunRadiusKm;
    out->moon_radius_km = moon_radius_km(context->eclipse_moon_radius_model_id);
    out->moon_radius_toward_shadow_km = out->moon_radius_km;
    out->moon_radius_away_from_shadow_km = out->moon_radius_km;
    if ((flags & TAIYIN_ECLIPSE_LUNAR_LIMB_CORRECTION) != 0u) {
        const Status limb_status = eval_lunar_eclipse_limb_radii_km(
            context,
            jd_tt,
            flags,
            moon,
            sun,
            &out->moon_radius_toward_shadow_km,
            &out->moon_radius_away_from_shadow_km);
        if (limb_status != TAIYIN_STATUS_OK) return limb_status;
    }
    out->shadow_earth_scale = sc.earth;
    out->shadow_sun_scale = sc.sun;
    out->shadow_parallax_scale = sc.parallax;
    return TAIYIN_STATUS_OK;
}

}  // namespace

Status lecXY(
    const LecInput& input,
    LecGeometry* out
) noexcept {
    if (!out) return TAIYIN_ERROR_INVALID_ARGUMENT;

    double diff = input.moon_lon_rad + M_PI - input.sun_lon_rad;
    diff = std::fmod(diff + M_PI, 2.0 * M_PI) - M_PI;
    out->x_rad = diff * std::cos((input.moon_lat_rad - input.sun_lat_rad) / 2.0);
    out->y_rad = input.moon_lat_rad + input.sun_lat_rad;
    out->rmin_rad = std::sqrt(out->x_rad * out->x_rad + out->y_rad * out->y_rad);
    out->moon_dist_au = input.moon_dist_au;
    out->sun_dist_au = input.sun_dist_au;

    const double moon_dist_km = input.moon_dist_au * kAuKm;
    const double sun_dist_km = input.sun_dist_au * kAuKm;
    out->moon_radius_rad = std::atan2(input.moon_radius_km, moon_dist_km);
    out->moon_radius_toward_shadow_rad = std::atan2(
        input.moon_radius_toward_shadow_km, moon_dist_km);
    out->moon_radius_away_from_shadow_rad = std::atan2(
        input.moon_radius_away_from_shadow_km, moon_dist_km);

    const double eff_earth_radius = input.earth_radius_km * input.shadow_earth_scale;
    const double penumbra_km = eff_earth_radius
        + moon_dist_km * (input.sun_radius_km * input.shadow_sun_scale
                          + input.earth_radius_km * input.shadow_parallax_scale) / sun_dist_km;
    const double umbra_km = eff_earth_radius
        - moon_dist_km * (input.sun_radius_km * input.shadow_sun_scale
                          - input.earth_radius_km * input.shadow_parallax_scale) / sun_dist_km;

    out->penumbra_radius_rad = std::atan2(penumbra_km, moon_dist_km);
    out->umbra_radius_rad = umbra_km > 0.0
        ? std::atan2(umbra_km, moon_dist_km)
        : -std::atan2(-umbra_km, moon_dist_km);
    return TAIYIN_STATUS_OK;
}

Status lecXY(
    const NativeCalcContext* context,
    SplitJulianDate jd_tt,
    uint64_t flags,
    LecGeometry* out,
    EphemerisEvalDiagnostic* diagnostic
) noexcept {
    LecInput input;
    const Status st = lecInput(context, jd_tt, flags, &input, diagnostic);
    if (st != TAIYIN_STATUS_OK) return st;
    return lecXY(input, out);
}

Status lecMax(
    const LecGeometry& z1,
    const LecGeometry& z2,
    double dt_days,
    LecMaxResult* out
) noexcept {
    if (!out || !(dt_days > 0.0)) return TAIYIN_ERROR_INVALID_ARGUMENT;
    out->geometry = z1;
    out->vx_rad_per_day = (z2.x_rad - z1.x_rad) / dt_days;
    out->vy_rad_per_day = (z2.y_rad - z1.y_rad) / dt_days;
    out->dt_days = lecMax_dt(*out);
    return TAIYIN_STATUS_OK;
}

Status lecMax(
    const NativeCalcContext* context,
    SplitJulianDate jd_tt,
    uint64_t flags,
    LecMaxResult* out,
    EphemerisEvalDiagnostic* diagnostic
) noexcept {
    LecGeometry geo;
    const Status st = lecXY(context, jd_tt, flags, &geo, diagnostic);
    if (st != TAIYIN_STATUS_OK) return st;

    const double dt = 60.0 / 86400.0;
    LecGeometry geo2;
    const Status st2 = lecXY(context, jd_tt + dt, flags, &geo2, diagnostic);
    if (st2 != TAIYIN_STATUS_OK) return st2;

    return lecMax(geo, geo2, dt, out);
}

double lineT(double x, double y, double vx, double vy, double r, int n) noexcept {
    const double A = vx * vx + vy * vy;
    const double B = x * vx + y * vy;
    const double C = x * x + y * y - r * r;
    const double D = B * B - A * C;
    if (D < 0.0 || A < 1e-30) {
        return std::nan("");
    }
    const double sqrtD = std::sqrt(D);
    return n == 0 ? (-B - sqrtD) / A : (-B + sqrtD) / A;
}

}  // namespace lunar
}  // namespace sxwnl
}  // namespace runtime
}  // namespace taiyin
