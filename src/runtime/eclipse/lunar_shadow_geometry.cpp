#include "runtime/eclipse/lunar_shadow_geometry.h"

#include "runtime/apparent/fast_apparent.h"

#include "taiyin/apparent_position.h"
#include "taiyin/body_id.h"
#include "taiyin/dispatch.h"
#include "taiyin/geodetic_constants.h"
#include "taiyin/runtime/eclipse_search.h"
#include "taiyin/runtime/lunar_limb.h"

#include <algorithm>
#include <cmath>

namespace taiyin {
namespace runtime {
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

ShadowScales shadow_scales(uint8_t shadow_model) noexcept {
    dispatch::EclipseShadowModelEntry entry;
    if (dispatch::select_eclipse_shadow_model(static_cast<int>(shadow_model), &entry)) {
        return {entry.earth_scale, entry.sun_scale, entry.parallax_scale};
    }
    return {1.01, 1.0, 1.0};
}

double moon_radius_km(uint8_t moon_radius_model) noexcept {
    dispatch::EclipseMoonRadiusModelEntry entry;
    if (dispatch::find_eclipse_moon_radius_model(static_cast<int>(moon_radius_model), &entry)) {
        return entry.radius_km;
    }
    return kMoonAlmanacRadiusRatio * kEarthEquatorialRadiusKm;
}

Status evaluate_directional_moon_radii(
    const NativeCalcContext* context,
    SplitJulianDate jd_tt,
    uint64_t flags,
    const Vector3& moon_au,
    const Vector3& shadow_axis_unit,
    double fallback_radius_km,
    double* out_toward_km,
    double* out_away_km
) noexcept {
    if (!context || !out_toward_km || !out_away_km) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    *out_toward_km = fallback_radius_km;
    *out_away_km = fallback_radius_km;
    if ((flags & TAIYIN_ECLIPSE_LUNAR_LIMB_CORRECTION) == 0u) {
        return TAIYIN_STATUS_OK;
    }

    const double moon_distance = vector3_norm(moon_au);
    if (!(moon_distance > 0.0)) {
        return TAIYIN_EPHEMERIS_ERROR_EVAL_FAILED;
    }
    const Vector3 moon_unit = vector3_scale(moon_au, 1.0 / moon_distance);
    Vector3 toward_shadow = vector3_subtract(
        shadow_axis_unit,
        vector3_scale(moon_unit, vector3_dot(shadow_axis_unit, moon_unit)));
    const double toward_norm = vector3_norm(toward_shadow);
    if (!(toward_norm > 0.0)) {
        return TAIYIN_STATUS_OK;
    }
    toward_shadow = vector3_scale(toward_shadow, 1.0 / toward_norm);

    const Vector3 moon_to_observer = vector3_scale(moon_au, -1.0);
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
        vector3_scale(toward_shadow, -1.0),
        &away_m);
    if (status != TAIYIN_STATUS_OK) return status;

    *out_toward_km = toward_m / 1000.0;
    *out_away_km = away_m / 1000.0;
    return TAIYIN_STATUS_OK;
}

}  // namespace

LunarShadowGeometry::LunarShadowGeometry() noexcept
    : shadow_axis_unit{0.0, 0.0, 0.0},
      transverse_offset_km{0.0, 0.0, 0.0},
      axial_distance_km(0.0),
      axis_distance_km(0.0),
      moon_distance_km(0.0),
      sun_distance_km(0.0),
      moon_radius_km(0.0),
      moon_radius_toward_shadow_km(0.0),
      moon_radius_away_from_shadow_km(0.0),
      umbra_radius_km(0.0),
      penumbra_radius_km(0.0) {}

Status evaluate_lunar_shadow_geometry(
    const NativeCalcContext* context,
    SplitJulianDate jd_tt,
    uint64_t flags,
    LunarShadowGeometry* out,
    EphemerisEvalDiagnostic* diagnostic
) noexcept {
    if (!context || !out || !split_julian_date_is_finite(jd_tt)) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }

    const double tdb_minus_tt = dispatch::eval_tdb(
        context->model_context.tdb_model_id, jd_tt, 0);
    SplitJulianDate jd_tdb = jd_tt;
    if (!add_seconds_to_split_jd(jd_tdb, tdb_minus_tt, &jd_tdb)) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }

    FastApparentOptions options;
    options.frame = FAST_APPARENT_TRUE_ECLIPTIC_OF_DATE;
    options.with_velocity = false;
    options.true_position = (flags & TAIYIN_ECLIPSE_TRUEPOS) != 0u;
    FastApparentBody2State pair;
    const Status status = eval_fast_apparent_body_2_tdb(
        context,
        jd_tdb,
        jd_tt,
        TAIYIN_BODY_MOON,
        TAIYIN_BODY_SUN,
        options,
        &pair,
        diagnostic);
    if (status != TAIYIN_STATUS_OK) return status;

    const Vector3& moon_au = pair.body_0.position_au;
    const Vector3& sun_au = pair.body_1.position_au;
    const double moon_distance_au = vector3_norm(moon_au);
    const double sun_distance_au = vector3_norm(sun_au);
    if (!(moon_distance_au > 0.0) || !(sun_distance_au > 0.0)
        || !std::isfinite(moon_distance_au) || !std::isfinite(sun_distance_au)) {
        return TAIYIN_EPHEMERIS_ERROR_EVAL_FAILED;
    }

    LunarShadowGeometry geometry;
    geometry.shadow_axis_unit = vector3_scale(sun_au, -1.0 / sun_distance_au);
    const Vector3 moon_km = vector3_scale(moon_au, kAuKm);
    geometry.axial_distance_km = vector3_dot(moon_km, geometry.shadow_axis_unit);
    geometry.transverse_offset_km = vector3_subtract(
        moon_km,
        vector3_scale(geometry.shadow_axis_unit, geometry.axial_distance_km));
    geometry.axis_distance_km = vector3_norm(geometry.transverse_offset_km);
    geometry.moon_distance_km = moon_distance_au * kAuKm;
    geometry.sun_distance_km = sun_distance_au * kAuKm;
    if (!(geometry.axial_distance_km > 0.0) || !std::isfinite(geometry.axis_distance_km)) {
        return TAIYIN_EPHEMERIS_ERROR_EVAL_FAILED;
    }

    geometry.moon_radius_km = moon_radius_km(context->eclipse_moon_radius_model_id);
    Status limb_status = evaluate_directional_moon_radii(
        context,
        jd_tt,
        flags,
        moon_au,
        geometry.shadow_axis_unit,
        geometry.moon_radius_km,
        &geometry.moon_radius_toward_shadow_km,
        &geometry.moon_radius_away_from_shadow_km);
    if (limb_status != TAIYIN_STATUS_OK) return limb_status;

    const ShadowScales scales = shadow_scales(context->eclipse_shadow_model_id);
    const double effective_earth_radius = kEarthEquatorialRadiusKm * scales.earth;
    const double penumbra_slope =
        (kSunRadiusKm * scales.sun + kEarthEquatorialRadiusKm * scales.parallax)
        / geometry.sun_distance_km;
    const double umbra_slope =
        (kSunRadiusKm * scales.sun - kEarthEquatorialRadiusKm * scales.parallax)
        / geometry.sun_distance_km;
    geometry.penumbra_radius_km = effective_earth_radius
        + geometry.axial_distance_km * penumbra_slope;
    geometry.umbra_radius_km = effective_earth_radius
        - geometry.axial_distance_km * umbra_slope;

    if (!std::isfinite(geometry.penumbra_radius_km)
        || !std::isfinite(geometry.umbra_radius_km)) {
        return TAIYIN_EPHEMERIS_ERROR_EVAL_FAILED;
    }
    *out = geometry;
    return TAIYIN_STATUS_OK;
}

}  // namespace runtime
}  // namespace taiyin
