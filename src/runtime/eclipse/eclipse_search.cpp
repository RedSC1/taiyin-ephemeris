#include "taiyin/runtime/eclipse_search.h"

#include "runtime/eclipse/eclipse_time.h"
#include "runtime/apparent/fast_apparent.h"
#include "runtime/eclipse/solar_shadow_geometry.h"
#include "runtime/eclipse/solar_route_geometry.h"
#include "runtime/eclipse/solar_apparent_snapshot.h"

#include "taiyin/angle.h"
#include "taiyin/body_id.h"
#include "taiyin/dispatch.h"
#include "taiyin/earth_rotation.h"
#include "taiyin/geodetic_constants.h"
#include "taiyin/observer.h"
#include "taiyin/runtime/lunar_limb.h"
#include "taiyin/runtime/native_context.h"
#include "taiyin/runtime/native_position.h"
#include "taiyin/time.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <new>
#include <vector>

namespace taiyin {
namespace runtime {

bool valid_solar_route_sample_count(size_t route_sample_count) noexcept {
    return route_sample_count >= TAIYIN_SOLAR_ROUTE_MIN_SAMPLE_COUNT
        && route_sample_count <= TAIYIN_SOLAR_ROUTE_MAX_SAMPLE_COUNT;
}

Status compute_local_solar_circumstances_tt_with_options(
    const NativeCalcContext* context,
    SplitJulianDate jd_tt,
    double longitude_deg,
    double latitude_deg,
    double height_m,
    uint64_t flags,
    FastApparentCorrectionSeries* corrections,
    LocalSolarEclipseCircumstances* out,
    EphemerisEvalDiagnostic* diagnostic
) noexcept;

Status refine_local_solar_inner_contact_duration_tt(
    const NativeCalcContext* context,
    SplitJulianDate center_jd_tt,
    double longitude_deg,
    double latitude_deg,
    double height_m,
    uint64_t flags,
    double seed_duration_seconds,
    double* out_duration_seconds,
    EphemerisEvalDiagnostic* diagnostic
) noexcept;

Status compute_solar_besselian_elements_tt_with_corrections(
    const NativeCalcContext* context,
    SplitJulianDate jd_tt,
    double t_hours,
    uint64_t flags,
    FastApparentCorrectionSeries* corrections,
    SolarBesselianElements* out,
    EphemerisEvalDiagnostic* diagnostic
) noexcept;

Status compute_solar_besselian_elements_and_velocity_tt_with_corrections(
    const NativeCalcContext* context,
    SplitJulianDate jd_tt,
    double t_hours,
    uint64_t flags,
    FastApparentCorrectionSeries* corrections,
    SolarBesselianElements* out,
    double* out_x_velocity_per_day,
    double* out_y_velocity_per_day,
    SolarApparentSnapshot* out_snapshot,
    EphemerisEvalDiagnostic* diagnostic
) noexcept;

Status eval_solar_eclipse_equatorial_vectors_km(
    const NativeCalcContext* context,
    SplitJulianDate jd_tt,
    uint64_t flags,
    FastApparentCorrectionSeries* corrections,
    double moon_km[3],
    double sun_km[3],
    EphemerisEvalDiagnostic* diagnostic
) noexcept;

Status compute_solar_besselian_polynomial_tt_with_corrections(
    const NativeCalcContext* context,
    SplitJulianDate center_jd_tt,
    double span_hours,
    double sample_step_hours,
    int degree,
    uint64_t flags,
    FastApparentCorrectionSeries* corrections,
    SolarBesselianPolynomial* out,
    EphemerisEvalDiagnostic* diagnostic
) noexcept;

namespace {

SplitJulianDate invalid_jd() noexcept {
    return SplitJulianDate(0, std::numeric_limits<double>::quiet_NaN());
}

constexpr uint32_t kRouteCurveLayerCenter = 1u << 0;
constexpr uint32_t kRouteCurveLayerPenumbral = 1u << 1;
constexpr uint32_t kRouteCurveLayerCore = 1u << 2;
constexpr uint32_t kRouteCurveLayerHalfMagnitude = 1u << 3;
constexpr uint32_t kRouteCurveLayerAll =
    kRouteCurveLayerCenter
    | kRouteCurveLayerPenumbral
    | kRouteCurveLayerCore
    | kRouteCurveLayerHalfMagnitude;
constexpr double kRouteEndpointSnapRelativeToStep = 1.0e-5;
constexpr double kRouteEndpointSnapMaximumSeconds = 1.0e-3;
// Even the longest partial phase is measured in hours. A larger gap between
// returned rows therefore identifies separate eclipse events, including
// sparse batches whose requested span covers multiple lunations.
constexpr double kRouteBatchEventGapDays = 2.0;

Status fill_route_path_point(
    const NativeCalcContext* context,
    SplitJulianDate jd_tt,
    uint64_t flags,
    double longitude_deg,
    double latitude_deg,
    SolarEclipsePathPoint* out,
    EphemerisEvalDiagnostic* diagnostic
) noexcept;

void init_path_point(SolarEclipsePathPoint* out) noexcept;

double route_moon_radius_ratio(const NativeCalcContext& context) noexcept {
    dispatch::EclipseMoonRadiusModelEntry entry;
    if (dispatch::find_eclipse_moon_radius_model(
            static_cast<int>(context.eclipse_moon_radius_model_id), &entry)
        && std::isfinite(entry.radius_km)
        && entry.radius_km > 0.0) {
        return entry.radius_km / TAIYIN_WGS84_A_KM;
    }
    return 0.2725076;
}

Status route_frame_from_besselian_elements(
    const NativeCalcContext* context,
    SplitJulianDate jd_tt,
    const SolarBesselianElements& e,
    solar_route_geometry::Frame* out,
    EphemerisEvalDiagnostic* diagnostic
) noexcept {
    if (!context || !out || !split_julian_date_is_finite(jd_tt)) return TAIYIN_ERROR_INVALID_ARGUMENT;
    SplitJulianDate jd_ut;
    Status st = eclipse_tt_to_ut(*context, jd_tt, &jd_ut, nullptr, diagnostic);
    if (st != TAIYIN_STATUS_OK) return st;
    double gast = 0.0;
    if (!gast_model_rad(
            context->model_context.precession_model_id,
            context->model_context.nutation_model_id,
            jd_ut,
            jd_tt,
            &gast)) {
        return TAIYIN_ERROR_UNSUPPORTED;
    }
    const double mu = e.mu_deg * TAIYIN_PI / 180.0;
    const double dec = e.d_deg * TAIYIN_PI / 180.0;
    const double ra = gast - mu;
    out->right_ascension_offset_rad = ra - TAIYIN_PI / 2.0;
    out->pole_rotation_rad = TAIYIN_PI / 2.0 + dec;
    out->gast_rad = gast;
    return TAIYIN_STATUS_OK;
}

struct RouteBoundaryPoint {
    bool valid;
    double longitude_rad;
    double latitude_rad;
    double x;
    double y;
};

struct RouteSurfaceVelocity {
    double vx_surface;
    double vy_surface;
    double vx_relative;
    double vy_relative;
    double speed;
};

RouteSurfaceVelocity route_surface_shadow_velocity(
    double x,
    double y,
    double shadow_declination_frame_rad,
    double vx_per_day,
    double vy_per_day
) noexcept {
    RouteSurfaceVelocity out{};
    double z = 1.0 - x * x - y * y;
    z = z < 0.0 ? 0.0 : std::sqrt(z);
    out.vx_surface = 2.0 * TAIYIN_PI
        * (std::sin(shadow_declination_frame_rad) * z - std::cos(shadow_declination_frame_rad) * y);
    out.vy_surface = 2.0 * TAIYIN_PI * x * std::cos(shadow_declination_frame_rad);
    out.vx_relative = vx_per_day - out.vx_surface;
    out.vy_relative = vy_per_day - out.vy_surface;
    out.speed = std::hypot(out.vx_relative, out.vy_relative);
    return out;
}

RouteBoundaryPoint route_shadow_boundary_point(
    double shadow_x,
    double shadow_y,
    double shadow_z,
    double velocity_x_per_day,
    double velocity_y_per_day,
    int side,
    double radius,
    double occluder_radius_ratio,
    const solar_route_geometry::Frame& frame,
    double earth_axis_ratio
) noexcept {
    RouteBoundaryPoint out{};
    if (side == 0 || !std::isfinite(radius) || !(std::fabs(velocity_x_per_day) > 0.0)) {
        return out;
    }

    const double side_sign = static_cast<double>(side);
    double x = shadow_x - velocity_y_per_day / velocity_x_per_day * radius * side_sign;
    double y = shadow_y + side_sign * radius;
    double sin_angle = 0.0;
    double cos_angle = 1.0;
    int clipped = 0;
    for (int i = 0; i < 3; ++i) {
        double z = 1.0 - x * x - y * y;
        if (z < 0.0) {
            if (clipped != 0) break;
            z = 0.0;
            ++clipped;
        } else {
            z = std::sqrt(z);
        }
        if (std::fabs(shadow_z) > 1e-14) {
            x -= (x - shadow_x) * z / shadow_z;
            y -= (y - shadow_y) * z / shadow_z;
        }
        const double relative_x = velocity_x_per_day
            - 2.0 * TAIYIN_PI * (std::sin(frame.pole_rotation_rad) * z
                - std::cos(frame.pole_rotation_rad) * y);
        const double relative_y = velocity_y_per_day
            - 2.0 * TAIYIN_PI * std::cos(frame.pole_rotation_rad) * x;
        const double speed = std::hypot(relative_x, relative_y);
        if (!(speed > 0.0)) return out;
        sin_angle = side_sign * relative_y / speed;
        cos_angle = side_sign * relative_x / speed;
        x = shadow_x - radius * sin_angle;
        y = shadow_y + radius * cos_angle;
    }
    out.x = x;
    out.y = y;
    const double generator_angle = std::atan2(cos_angle, -sin_angle);
    SolarConeEarthPoint earth_point;
    if (!intersect_solar_circular_cone_generator_with_oblate_earth(
            shadow_x,
            shadow_y,
            shadow_z,
            occluder_radius_ratio,
            radius,
            generator_angle,
            frame.pole_rotation_rad,
            frame.right_ascension_offset_rad - frame.gast_rad,
            earth_axis_ratio,
            &earth_point)
        || !earth_point.valid) {
        return out;
    }
    out.valid = true;
    out.longitude_rad = earth_point.longitude_rad;
    out.latitude_rad = earth_point.latitude_rad;
    return out;
}

struct RouteLunarLimbGeometry {
    bool enabled;
    PreparedLunarLimbQuery query;
    Vector3 moon_km;
    Vector3 sun_km;
    double smooth_radius_ratio;
    double profile_radius_scale;

    RouteLunarLimbGeometry() noexcept
        : enabled(false),
          query(),
          moon_km{NAN, NAN, NAN},
          sun_km{NAN, NAN, NAN},
          smooth_radius_ratio(NAN),
          profile_radius_scale(NAN) {}
};

Status prepare_route_lunar_limb_geometry(
    const NativeCalcContext* context,
    SplitJulianDate jd_tt,
    uint64_t flags,
    const SolarBesselianElements& elements,
    RouteLunarLimbGeometry* out,
    EphemerisEvalDiagnostic* diagnostic
) noexcept {
    if (out) *out = RouteLunarLimbGeometry();
    if (!context || !out || !split_julian_date_is_finite(jd_tt)) return TAIYIN_ERROR_INVALID_ARGUMENT;
    out->smooth_radius_ratio = route_moon_radius_ratio(*context);
    if ((flags & TAIYIN_ECLIPSE_LUNAR_LIMB_CORRECTION) == 0u) {
        return TAIYIN_STATUS_OK;
    }

    Status status = prepare_lunar_limb_query(
        context,
        jd_tt,
        (flags & TAIYIN_ECLIPSE_TRUEPOS) != 0u,
        TAIYIN_APPARENT_FRAME_TRUE_EQUATOR_OF_DATE,
        &out->query);
    if (status != TAIYIN_STATUS_OK) return status;

    double moon_km[3] = {};
    double sun_km[3] = {};
    status = eval_solar_eclipse_equatorial_vectors_km(
        context, jd_tt, flags, nullptr, moon_km, sun_km, diagnostic);
    if (status != TAIYIN_STATUS_OK) return status;
    out->moon_km = Vector3{moon_km[0], moon_km[1], moon_km[2]};
    out->sun_km = Vector3{sun_km[0], sun_km[1], sun_km[2]};
    const double sun_moon_km = vector3_norm(vector3_subtract(out->sun_km, out->moon_km));
    if (!(sun_moon_km > 0.0)) return TAIYIN_EPHEMERIS_ERROR_EVAL_FAILED;
    out->profile_radius_scale = 1.0
        + elements.zeta * TAIYIN_WGS84_A_KM / sun_moon_km;
    out->enabled = true;
    return std::isfinite(out->profile_radius_scale)
        ? TAIYIN_STATUS_OK
        : TAIYIN_EPHEMERIS_ERROR_EVAL_FAILED;
}

Status route_lunar_limb_radius_ratio(
    const RouteLunarLimbGeometry& geometry,
    const solar_route_geometry::Frame& frame,
    const RouteBoundaryPoint& point,
    bool away_from_sun,
    double* out_radius_ratio
) noexcept {
    if (!out_radius_ratio || !geometry.enabled || !point.valid) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    const Vector3 ecef_m = geodetic_to_ecef_m(
        point.longitude_rad, point.latitude_rad, 0.0);
    const Vector3 surface_km = vector3_scale(
        rotate_z(ecef_m, frame.gast_rad), 1.0 / 1000.0);
    const Vector3 observer_to_moon = vector3_subtract(geometry.moon_km, surface_km);
    const Vector3 observer_to_sun = vector3_subtract(geometry.sun_km, surface_km);
    Vector3 limb_direction;
    Status status = apparent_limb_direction_toward_target(
        observer_to_moon,
        observer_to_sun,
        away_from_sun,
        &limb_direction);
    if (status != TAIYIN_STATUS_OK) return status;

    double radius_m = NAN;
    status = eval_prepared_lunar_limb_radius_m(
        &geometry.query,
        vector3_subtract(surface_km, geometry.moon_km),
        limb_direction,
        &radius_m);
    if (status != TAIYIN_STATUS_OK) return status;
    *out_radius_ratio = radius_m / (1000.0 * TAIYIN_WGS84_A_KM);
    return std::isfinite(*out_radius_ratio) && *out_radius_ratio > 0.0
        ? TAIYIN_STATUS_OK
        : TAIYIN_EPHEMERIS_ERROR_EVAL_FAILED;
}

enum RouteLimbContourKind {
    ROUTE_LIMB_CONTOUR_OUTER = 0,
    ROUTE_LIMB_CONTOUR_CORE = 1,
    ROUTE_LIMB_CONTOUR_HALF_MAGNITUDE = 2,
};

Status polish_profiled_route_boundary(
    const RouteLunarLimbGeometry& geometry,
    const solar_route_geometry::Frame& frame,
    RouteLimbContourKind contour_kind,
    bool away_from_sun,
    RouteBoundaryPoint* point
) noexcept;

Status profiled_route_shadow_boundary_point(
    double shadow_x,
    double shadow_y,
    double shadow_z,
    double velocity_x_per_day,
    double velocity_y_per_day,
    int side,
    double smooth_shadow_radius,
    RouteLimbContourKind contour_kind,
    bool away_from_sun,
    const solar_route_geometry::Frame& frame,
    double earth_axis_ratio,
    const RouteLunarLimbGeometry& geometry,
    RouteBoundaryPoint* out
) noexcept {
    if (!out) return TAIYIN_ERROR_INVALID_ARGUMENT;
    double shadow_radius = smooth_shadow_radius;
    double moon_radius_ratio = geometry.smooth_radius_ratio;
    if (!geometry.enabled) {
        *out = route_shadow_boundary_point(
            shadow_x,
            shadow_y,
            shadow_z,
            velocity_x_per_day,
            velocity_y_per_day,
            side,
            smooth_shadow_radius,
            moon_radius_ratio,
            frame,
            earth_axis_ratio);
        return TAIYIN_STATUS_OK;
    }

    for (int iteration = 0; iteration < 5; ++iteration) {
        *out = route_shadow_boundary_point(
            shadow_x,
            shadow_y,
            shadow_z,
            velocity_x_per_day,
            velocity_y_per_day,
            side,
            shadow_radius,
            moon_radius_ratio,
            frame,
            earth_axis_ratio);
        if (!out->valid) return TAIYIN_STATUS_OK;
        double next_moon_radius_ratio = NAN;
        const Status status = route_lunar_limb_radius_ratio(
            geometry, frame, *out, away_from_sun, &next_moon_radius_ratio);
        if (status != TAIYIN_STATUS_OK) return status;
        const double next_shadow_radius = smooth_shadow_radius
            + (next_moon_radius_ratio - geometry.smooth_radius_ratio)
                * geometry.profile_radius_scale;
        const bool converged = std::fabs(next_shadow_radius - shadow_radius) < 1.0e-10
            && std::fabs(next_moon_radius_ratio - moon_radius_ratio) < 1.0e-10;
        shadow_radius = next_shadow_radius;
        moon_radius_ratio = next_moon_radius_ratio;
        if (converged) break;
    }
    *out = route_shadow_boundary_point(
        shadow_x,
        shadow_y,
        shadow_z,
        velocity_x_per_day,
        velocity_y_per_day,
        side,
        shadow_radius,
        moon_radius_ratio,
        frame,
        earth_axis_ratio);
    if (!out->valid) return TAIYIN_STATUS_OK;
    return polish_profiled_route_boundary(
        geometry, frame, contour_kind, away_from_sun, out);
}

Status apply_profiled_route_curve_point(
    double shadow_x,
    double shadow_y,
    double shadow_z,
    double velocity_x_per_day,
    double velocity_y_per_day,
    int side,
    double smooth_shadow_radius,
    RouteLimbContourKind contour_kind,
    bool away_from_sun,
    const solar_route_geometry::Frame& frame,
    double earth_axis_ratio,
    const RouteLunarLimbGeometry& geometry,
    solar_route_geometry::LimitSample* inout
) noexcept {
    if (!inout) return TAIYIN_ERROR_INVALID_ARGUMENT;
    if (!geometry.enabled || !inout->valid) return TAIYIN_STATUS_OK;

    RouteBoundaryPoint profiled;
    const Status status = profiled_route_shadow_boundary_point(
        shadow_x,
        shadow_y,
        shadow_z,
        velocity_x_per_day,
        velocity_y_per_day,
        side,
        smooth_shadow_radius,
        contour_kind,
        away_from_sun,
        frame,
        earth_axis_ratio,
        geometry,
        &profiled);
    if (status != TAIYIN_STATUS_OK) return status;
    inout->valid = profiled.valid;
    if (profiled.valid) {
        inout->longitude_rad = profiled.longitude_rad;
        inout->latitude_rad = profiled.latitude_rad;
    }
    return TAIYIN_STATUS_OK;
}

double normalize_degrees(double x) noexcept {
    x = std::fmod(x, 360.0);
    if (x < 0.0) x += 360.0;
    return x;
}

double normalize_signed_degrees(double x) noexcept {
    x = std::fmod(x + 180.0, 360.0);
    if (x < 0.0) x += 360.0;
    return x - 180.0;
}

double geodetic_bearing_rad(
    double latitude0_deg,
    double longitude0_deg,
    double latitude1_deg,
    double longitude1_deg
) noexcept {
    const double lat0 = latitude0_deg * TAIYIN_PI / 180.0;
    const double lat1 = latitude1_deg * TAIYIN_PI / 180.0;
    const double dlon = normalize_signed_degrees(longitude1_deg - longitude0_deg) * TAIYIN_PI / 180.0;
    const double y = std::sin(dlon) * std::cos(lat1);
    const double x = std::cos(lat0) * std::sin(lat1)
        - std::sin(lat0) * std::cos(lat1) * std::cos(dlon);
    return std::atan2(y, x);
}

bool local_tangent_coordinates_km(
    const SolarEclipsePathPoint& center,
    const SolarEclipsePathPoint& point,
    double* out_east_km,
    double* out_north_km
) noexcept {
    if (!std::isfinite(center.latitude_deg) || !std::isfinite(center.longitude_deg)
        || !std::isfinite(point.latitude_deg) || !std::isfinite(point.longitude_deg)
        || !out_east_km || !out_north_km) {
        return false;
    }
    const double lat0 = center.latitude_deg * TAIYIN_PI / 180.0;
    const double lat1 = point.latitude_deg * TAIYIN_PI / 180.0;
    const double dlat = lat1 - lat0;
    const double dlon = normalize_signed_degrees(point.longitude_deg - center.longitude_deg) * TAIYIN_PI / 180.0;
    const double s_dlat = std::sin(0.5 * dlat);
    const double s_dlon = std::sin(0.5 * dlon);
    const double a = s_dlat * s_dlat + std::cos(lat0) * std::cos(lat1) * s_dlon * s_dlon;
    const double angular_distance = 2.0 * std::asin(std::min(1.0, std::sqrt(std::max(0.0, a))));
    const double bearing = geodetic_bearing_rad(
        center.latitude_deg, center.longitude_deg, point.latitude_deg, point.longitude_deg);
    const double distance_km = TAIYIN_WGS84_A_KM * angular_distance;
    *out_east_km = distance_km * std::sin(bearing);
    *out_north_km = distance_km * std::cos(bearing);
    return std::isfinite(*out_east_km) && std::isfinite(*out_north_km);
}

bool offset_geodetic_point_km(
    const SolarEclipsePathPoint& center,
    double east_km,
    double north_km,
    double* out_longitude_deg,
    double* out_latitude_deg
) noexcept {
    if (!out_longitude_deg || !out_latitude_deg
        || !std::isfinite(center.latitude_deg)
        || !std::isfinite(center.longitude_deg)
        || !std::isfinite(east_km)
        || !std::isfinite(north_km)) {
        return false;
    }
    const double distance_km = std::hypot(east_km, north_km);
    if (distance_km == 0.0) {
        *out_longitude_deg = center.longitude_deg;
        *out_latitude_deg = center.latitude_deg;
        return true;
    }
    const double angular_distance = distance_km / TAIYIN_WGS84_A_KM;
    const double bearing = std::atan2(east_km, north_km);
    const double lat0 = center.latitude_deg * TAIYIN_PI / 180.0;
    const double lon0 = center.longitude_deg * TAIYIN_PI / 180.0;
    const double sin_lat0 = std::sin(lat0);
    const double cos_lat0 = std::cos(lat0);
    const double sin_d = std::sin(angular_distance);
    const double cos_d = std::cos(angular_distance);
    const double lat = std::asin(std::max(-1.0, std::min(1.0,
        sin_lat0 * cos_d + cos_lat0 * sin_d * std::cos(bearing))));
    const double lon = lon0 + std::atan2(
        std::sin(bearing) * sin_d * cos_lat0,
        cos_d - sin_lat0 * std::sin(lat));
    *out_longitude_deg = normalize_degrees(lon * 180.0 / TAIYIN_PI + 180.0) - 180.0;
    *out_latitude_deg = lat * 180.0 / TAIYIN_PI;
    return std::isfinite(*out_longitude_deg) && std::isfinite(*out_latitude_deg);
}

Status eval_profiled_route_contour_scalar(
    const RouteLunarLimbGeometry& geometry,
    const solar_route_geometry::Frame& frame,
    double longitude_deg,
    double latitude_deg,
    RouteLimbContourKind contour_kind,
    bool away_from_sun,
    double* out_scalar_rad
) noexcept {
    if (!out_scalar_rad || !geometry.enabled
        || !std::isfinite(longitude_deg) || !std::isfinite(latitude_deg)) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    RouteBoundaryPoint point{};
    point.valid = true;
    point.longitude_rad = longitude_deg * TAIYIN_PI / 180.0;
    point.latitude_rad = latitude_deg * TAIYIN_PI / 180.0;
    double moon_radius_ratio = NAN;
    Status status = route_lunar_limb_radius_ratio(
        geometry, frame, point, away_from_sun, &moon_radius_ratio);
    if (status != TAIYIN_STATUS_OK) return status;

    const Vector3 surface_km = vector3_scale(
        rotate_z(
            geodetic_to_ecef_m(point.longitude_rad, point.latitude_rad, 0.0),
            frame.gast_rad),
        1.0 / 1000.0);
    const Vector3 observer_to_moon = vector3_subtract(geometry.moon_km, surface_km);
    const Vector3 observer_to_sun = vector3_subtract(geometry.sun_km, surface_km);
    const double moon_distance_km = vector3_norm(observer_to_moon);
    const double sun_distance_km = vector3_norm(observer_to_sun);
    if (!(moon_distance_km > 0.0) || !(sun_distance_km > 0.0)) {
        return TAIYIN_EPHEMERIS_ERROR_EVAL_FAILED;
    }
    const double cosine = std::max(-1.0, std::min(
        1.0,
        vector3_dot(observer_to_moon, observer_to_sun)
            / (moon_distance_km * sun_distance_km)));
    const double separation = std::acos(cosine);
    constexpr double kSunRadiusKm = 695700.0;
    const double sun_radius = std::atan2(kSunRadiusKm, sun_distance_km);
    const double moon_radius = std::atan2(
        moon_radius_ratio * TAIYIN_WGS84_A_KM, moon_distance_km);
    switch (contour_kind) {
        case ROUTE_LIMB_CONTOUR_OUTER:
            *out_scalar_rad = separation - (sun_radius + moon_radius);
            break;
        case ROUTE_LIMB_CONTOUR_CORE:
            *out_scalar_rad = separation - std::fabs(moon_radius - sun_radius);
            break;
        case ROUTE_LIMB_CONTOUR_HALF_MAGNITUDE:
            *out_scalar_rad = separation - moon_radius;
            break;
        default:
            return TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    return std::isfinite(*out_scalar_rad)
        ? TAIYIN_STATUS_OK
        : TAIYIN_EPHEMERIS_ERROR_EVAL_FAILED;
}

Status polish_profiled_route_boundary(
    const RouteLunarLimbGeometry& geometry,
    const solar_route_geometry::Frame& frame,
    RouteLimbContourKind contour_kind,
    bool away_from_sun,
    RouteBoundaryPoint* point
) noexcept {
    if (!point || !point->valid || !geometry.enabled) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    SolarEclipsePathPoint current{};
    current.longitude_deg = point->longitude_rad * 180.0 / TAIYIN_PI;
    current.latitude_deg = point->latitude_rad * 180.0 / TAIYIN_PI;

    constexpr double kGradientStepKm = 0.25;
    constexpr double kMaxCorrectionKm = 5.0;
    constexpr double kResidualToleranceRad = 1.0e-8;
    for (int iteration = 0; iteration < 6; ++iteration) {
        double scalar = NAN;
        Status status = eval_profiled_route_contour_scalar(
            geometry,
            frame,
            current.longitude_deg,
            current.latitude_deg,
            contour_kind,
            away_from_sun,
            &scalar);
        if (status != TAIYIN_STATUS_OK) return status;
        if (std::fabs(scalar) <= kResidualToleranceRad) {
            point->longitude_rad = current.longitude_deg * TAIYIN_PI / 180.0;
            point->latitude_rad = current.latitude_deg * TAIYIN_PI / 180.0;
            return TAIYIN_STATUS_OK;
        }

        double east_plus_lon = NAN;
        double east_plus_lat = NAN;
        double east_minus_lon = NAN;
        double east_minus_lat = NAN;
        double north_plus_lon = NAN;
        double north_plus_lat = NAN;
        double north_minus_lon = NAN;
        double north_minus_lat = NAN;
        if (!offset_geodetic_point_km(
                current, kGradientStepKm, 0.0, &east_plus_lon, &east_plus_lat)
            || !offset_geodetic_point_km(
                current, -kGradientStepKm, 0.0, &east_minus_lon, &east_minus_lat)
            || !offset_geodetic_point_km(
                current, 0.0, kGradientStepKm, &north_plus_lon, &north_plus_lat)
            || !offset_geodetic_point_km(
                current, 0.0, -kGradientStepKm, &north_minus_lon, &north_minus_lat)) {
            return TAIYIN_EPHEMERIS_ERROR_EVAL_FAILED;
        }
        double east_plus = NAN;
        double east_minus = NAN;
        double north_plus = NAN;
        double north_minus = NAN;
        status = eval_profiled_route_contour_scalar(
            geometry, frame, east_plus_lon, east_plus_lat,
            contour_kind, away_from_sun, &east_plus);
        if (status != TAIYIN_STATUS_OK) return status;
        status = eval_profiled_route_contour_scalar(
            geometry, frame, east_minus_lon, east_minus_lat,
            contour_kind, away_from_sun, &east_minus);
        if (status != TAIYIN_STATUS_OK) return status;
        status = eval_profiled_route_contour_scalar(
            geometry, frame, north_plus_lon, north_plus_lat,
            contour_kind, away_from_sun, &north_plus);
        if (status != TAIYIN_STATUS_OK) return status;
        status = eval_profiled_route_contour_scalar(
            geometry, frame, north_minus_lon, north_minus_lat,
            contour_kind, away_from_sun, &north_minus);
        if (status != TAIYIN_STATUS_OK) return status;

        const double gradient_east = (east_plus - east_minus) / (2.0 * kGradientStepKm);
        const double gradient_north = (north_plus - north_minus) / (2.0 * kGradientStepKm);
        const double gradient2 = gradient_east * gradient_east
            + gradient_north * gradient_north;
        if (!(gradient2 > 1.0e-24)) return TAIYIN_ERROR_UNSUPPORTED;
        double correction_east = -scalar * gradient_east / gradient2;
        double correction_north = -scalar * gradient_north / gradient2;
        const double correction_norm = std::hypot(correction_east, correction_north);
        if (!std::isfinite(correction_norm)) return TAIYIN_EPHEMERIS_ERROR_EVAL_FAILED;
        if (correction_norm > kMaxCorrectionKm) {
            correction_east *= kMaxCorrectionKm / correction_norm;
            correction_north *= kMaxCorrectionKm / correction_norm;
        }
        if (!offset_geodetic_point_km(
                current,
                correction_east,
                correction_north,
                &current.longitude_deg,
                &current.latitude_deg)) {
            return TAIYIN_EPHEMERIS_ERROR_EVAL_FAILED;
        }
        if (correction_norm < 0.002) break;
    }
    point->longitude_rad = current.longitude_deg * TAIYIN_PI / 180.0;
    point->latitude_rad = current.latitude_deg * TAIYIN_PI / 180.0;
    double final_scalar = NAN;
    const Status final_status = eval_profiled_route_contour_scalar(
        geometry,
        frame,
        current.longitude_deg,
        current.latitude_deg,
        contour_kind,
        away_from_sun,
        &final_scalar);
    if (final_status != TAIYIN_STATUS_OK) return final_status;
    return std::fabs(final_scalar) <= kResidualToleranceRad
        ? TAIYIN_STATUS_OK
        : TAIYIN_ERROR_UNSUPPORTED;
}

Status route_center_separation_at_offset_deg(
    const NativeCalcContext* context,
    SplitJulianDate jd_tt,
    uint64_t flags,
    const SolarEclipsePathPoint& center,
    double east_km,
    double north_km,
    double* out_separation_deg,
    EphemerisEvalDiagnostic* diagnostic
) noexcept {
    if (!context || !out_separation_deg || !split_julian_date_is_finite(jd_tt)) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    double lon = 0.0;
    double lat = 0.0;
    if (!offset_geodetic_point_km(center, east_km, north_km, &lon, &lat)) {
        return TAIYIN_EPHEMERIS_ERROR_EVAL_FAILED;
    }
    LocalSolarEclipseCircumstances c;
    const Status st = compute_local_solar_circumstances_tt_with_options(
        context, jd_tt, lon, lat, 0.0, flags, nullptr, &c, diagnostic);
    if (st != TAIYIN_STATUS_OK) return st;
    *out_separation_deg = c.center_separation_deg;
    return std::isfinite(*out_separation_deg) ? TAIYIN_STATUS_OK : TAIYIN_EPHEMERIS_ERROR_EVAL_FAILED;
}

Status refine_route_center_from_guess(
    const NativeCalcContext* context,
    SplitJulianDate jd_tt,
    uint64_t flags,
    const SolarEclipsePathPoint& guess,
    SolarEclipsePathPoint* out,
    EphemerisEvalDiagnostic* diagnostic
) noexcept {
    if (!context || !out || !split_julian_date_is_finite(jd_tt)
        || !std::isfinite(guess.longitude_deg)
        || !std::isfinite(guess.latitude_deg)) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }

    double east = 0.0;
    double north = 0.0;
    double best = 0.0;
    Status st = route_center_separation_at_offset_deg(
        context, jd_tt, flags, guess, east, north, &best, diagnostic);
    if (st != TAIYIN_STATUS_OK) return st;

    double step_km = 2.0;
    for (int iter = 0; iter < 32 && step_km > 0.002; ++iter) {
        double best_east = east;
        double best_north = north;
        double best_value = best;
        for (int de = -1; de <= 1; ++de) {
            for (int dn = -1; dn <= 1; ++dn) {
                if (de == 0 && dn == 0) continue;
                const double trial_east = east + static_cast<double>(de) * step_km;
                const double trial_north = north + static_cast<double>(dn) * step_km;
                double value = 0.0;
                st = route_center_separation_at_offset_deg(
                    context,
                    jd_tt,
                    flags,
                    guess,
                    trial_east,
                    trial_north,
                    &value,
                    diagnostic);
                if (st != TAIYIN_STATUS_OK) return st;
                if (value < best_value) {
                    best_value = value;
                    best_east = trial_east;
                    best_north = trial_north;
                }
            }
        }
        if (best_value < best) {
            east = best_east;
            north = best_north;
            best = best_value;
        } else {
            step_km *= 0.5;
        }
    }

    double lon = 0.0;
    double lat = 0.0;
    if (!offset_geodetic_point_km(guess, east, north, &lon, &lat)) {
        return TAIYIN_EPHEMERIS_ERROR_EVAL_FAILED;
    }
    return fill_route_path_point(context, jd_tt, flags, lon, lat, out, diagnostic);
}

double projected_route_width_km(
    const SolarEclipsePathPoint& center,
    const SolarEclipsePathPoint& north,
    const SolarEclipsePathPoint& south,
    double track_east_unit,
    double track_north_unit
) noexcept {
    double north_east = 0.0;
    double north_north = 0.0;
    double south_east = 0.0;
    double south_north = 0.0;
    if (!local_tangent_coordinates_km(center, north, &north_east, &north_north)
        || !local_tangent_coordinates_km(center, south, &south_east, &south_north)
        || !std::isfinite(track_east_unit) || !std::isfinite(track_north_unit)) {
        return std::nan("");
    }
    const double normal_east_unit = -track_north_unit;
    const double normal_north_unit = track_east_unit;
    const double north_projection = north_east * normal_east_unit + north_north * normal_north_unit;
    const double south_projection = south_east * normal_east_unit + south_north * normal_north_unit;
    return std::fabs(north_projection - south_projection);
}

bool intersect_boundary_curve_with_center_normal(
    const SolarEclipsePathPoint& center,
    const SolarEclipsePathPoint& sample0,
    const SolarEclipsePathPoint& sample1,
    double track_east_unit,
    double track_north_unit,
    double* out_normal_projection_km
) noexcept {
    double e0 = 0.0;
    double n0 = 0.0;
    double e1 = 0.0;
    double n1 = 0.0;
    if (!out_normal_projection_km
        || !local_tangent_coordinates_km(center, sample0, &e0, &n0)
        || !local_tangent_coordinates_km(center, sample1, &e1, &n1)) {
        return false;
    }
    const double d0 = e0 * track_east_unit + n0 * track_north_unit;
    const double d1 = e1 * track_east_unit + n1 * track_north_unit;
    const double denom = d1 - d0;
    if (!std::isfinite(d0) || !std::isfinite(d1) || std::fabs(denom) < 1e-12) {
        return false;
    }
    if ((d0 < 0.0 && d1 < 0.0) || (d0 > 0.0 && d1 > 0.0)) {
        return false;
    }
    const double u = -d0 / denom;
    if (u < -1e-9 || u > 1.0 + 1e-9) {
        return false;
    }
    const double east = e0 + u * (e1 - e0);
    const double north = n0 + u * (n1 - n0);
    const double normal_east_unit = -track_north_unit;
    const double normal_north_unit = track_east_unit;
    *out_normal_projection_km = east * normal_east_unit + north * normal_north_unit;
    return std::isfinite(*out_normal_projection_km);
}

void init_path_point(SolarEclipsePathPoint* out) noexcept {
    out->jd_tt = invalid_jd();
    out->jd_ut = invalid_jd();
    out->latitude_deg = std::nan("");
    out->longitude_deg = std::nan("");
    out->elevation_m = std::nan("");
    out->sun_altitude_deg = std::nan("");
    out->sun_azimuth_deg = std::nan("");
}

Status fill_route_path_point(
    const NativeCalcContext* context,
    SplitJulianDate jd_tt,
    uint64_t flags,
    double longitude_deg,
    double latitude_deg,
    SolarEclipsePathPoint* out,
    EphemerisEvalDiagnostic* diagnostic
) noexcept {
    LocalSolarEclipseCircumstances circumstances;
    const Status st = compute_local_solar_circumstances_tt_with_options(
        context,
        jd_tt,
        longitude_deg,
        latitude_deg,
        0.0,
        flags,
        nullptr,
        &circumstances,
        diagnostic);
    if (st != TAIYIN_STATUS_OK) return st;
    out->jd_tt = jd_tt;
    Status time_status = eclipse_tt_to_ut(*context, jd_tt, &out->jd_ut, nullptr, diagnostic);
    if (time_status != TAIYIN_STATUS_OK) return time_status;
    out->longitude_deg = normalize_degrees(longitude_deg + 180.0) - 180.0;
    out->latitude_deg = latitude_deg;
    out->elevation_m = 0.0;
    out->sun_altitude_deg = circumstances.sun_altitude_deg;
    out->sun_azimuth_deg = circumstances.sun_azimuth_deg;
    return TAIYIN_STATUS_OK;
}

Status fill_route_path_point_rad(
    const NativeCalcContext* context,
    SplitJulianDate jd_tt,
    uint64_t flags,
    double longitude_rad,
    double latitude_rad,
    SolarEclipsePathPoint* out,
    EphemerisEvalDiagnostic* diagnostic
) noexcept {
    return fill_route_path_point(
        context,
        jd_tt,
        flags,
        longitude_rad * 180.0 / TAIYIN_PI,
        latitude_rad * 180.0 / TAIYIN_PI,
        out,
        diagnostic
    );
}

Status compute_route_center_point_tt(
    const NativeCalcContext* context,
    SplitJulianDate jd_tt,
    uint64_t flags,
    SolarEclipsePathPoint* out,
    EphemerisEvalDiagnostic* diagnostic
) noexcept {
    if (!context || !out || !split_julian_date_is_finite(jd_tt)) return TAIYIN_ERROR_INVALID_ARGUMENT;
    init_path_point(out);
    SolarBesselianElements e;
    Status st = compute_solar_besselian_elements_tt_with_corrections(
        context, jd_tt, 0.0, flags, nullptr, &e, diagnostic);
    if (st != TAIYIN_STATUS_OK) return st;
    solar_route_geometry::Frame frame;
    st = route_frame_from_besselian_elements(context, jd_tt, e, &frame, diagnostic);
    if (st != TAIYIN_STATUS_OK) return st;
    SolarConeEarthPoint center;
    if (!intersect_solar_shadow_axis_with_oblate_earth(
            -e.x,
            e.y,
            frame.pole_rotation_rad,
            frame.right_ascension_offset_rad - frame.gast_rad,
            TAIYIN_WGS84_B_KM / TAIYIN_WGS84_A_KM,
            &center)) {
        return TAIYIN_EPHEMERIS_ERROR_EVAL_FAILED;
    }
    if (!center.valid) return TAIYIN_STATUS_OK;
    st = fill_route_path_point_rad(
        context, jd_tt, flags, center.longitude_rad, center.latitude_rad, out, diagnostic);
    if (st != TAIYIN_STATUS_OK) return st;
    SolarEclipsePathPoint refined;
    init_path_point(&refined);
    if (refine_route_center_from_guess(
            context, jd_tt, flags, *out, &refined, diagnostic) == TAIYIN_STATUS_OK) {
        *out = refined;
    }
    return TAIYIN_STATUS_OK;
}

void fill_geodetic_path_point_rad(
    SplitJulianDate jd_tt,
    SplitJulianDate jd_ut,
    double longitude_rad,
    double latitude_rad,
    SolarEclipsePathPoint* out
) noexcept {
    init_path_point(out);
    out->jd_tt = jd_tt;
    out->jd_ut = jd_ut;
    out->longitude_deg = normalize_degrees(longitude_rad * 180.0 / TAIYIN_PI + 180.0) - 180.0;
    out->latitude_deg = latitude_rad * 180.0 / TAIYIN_PI;
    out->elevation_m = 0.0;
}

struct RouteCoreLimitSample {
    bool north_valid;
    bool south_valid;
    SolarEclipsePathPoint north;
    SolarEclipsePathPoint south;
};

Status compute_core_route_limits_at_tt(
    const NativeCalcContext* context,
    SplitJulianDate jd_tt,
    uint64_t flags,
    RouteCoreLimitSample* out,
    EphemerisEvalDiagnostic* diagnostic
) noexcept {
    if (!context || !out || !split_julian_date_is_finite(jd_tt)) return TAIYIN_ERROR_INVALID_ARGUMENT;
    *out = RouteCoreLimitSample{};
    init_path_point(&out->north);
    init_path_point(&out->south);

    SolarBesselianElements e;
    Status st = compute_solar_besselian_elements_tt_with_corrections(
        context, jd_tt, 0.0, flags, nullptr, &e, diagnostic);
    if (st != TAIYIN_STATUS_OK) return st;
    solar_route_geometry::Frame I;
    st = route_frame_from_besselian_elements(context, jd_tt, e, &I, diagnostic);
    if (st != TAIYIN_STATUS_OK) return st;

    const double velocity_step_days = 0.04;
    SolarBesselianElements before;
    SolarBesselianElements after;
    st = compute_solar_besselian_elements_tt_with_corrections(
        context, jd_tt - velocity_step_days, 0.0, flags, nullptr, &before, diagnostic);
    if (st != TAIYIN_STATUS_OK) return st;
    st = compute_solar_besselian_elements_tt_with_corrections(
        context, jd_tt + velocity_step_days, 0.0, flags, nullptr, &after, diagnostic);
    if (st != TAIYIN_STATUS_OK) return st;
    const double vx_per_day = ((-after.x) - (-before.x)) / (2.0 * velocity_step_days);
    const double vy_per_day = (after.y - before.y) / (2.0 * velocity_step_days);
    const double earth_axis_ratio = TAIYIN_WGS84_B_KM / TAIYIN_WGS84_A_KM;
    SolarConeEarthPoint center;
    if (!intersect_solar_shadow_axis_with_oblate_earth(
            -e.x,
            e.y,
            I.pole_rotation_rad,
            I.right_ascension_offset_rad - I.gast_rad,
            earth_axis_ratio,
            &center)) {
        return TAIYIN_EPHEMERIS_ERROR_EVAL_FAILED;
    }
    const double core_radius = center.valid
        ? e.l2 + e.tan_f2 * center.distance_to_parameter_one
        : e.l2;
    RouteLunarLimbGeometry limb_geometry;
    st = prepare_route_lunar_limb_geometry(
        context, jd_tt, flags, e, &limb_geometry, diagnostic);
    if (st != TAIYIN_STATUS_OK) return st;
    RouteBoundaryPoint north;
    st = profiled_route_shadow_boundary_point(
        -e.x, e.y, e.zeta, vx_per_day, vy_per_day, +1, e.l2,
        ROUTE_LIMB_CONTOUR_CORE, core_radius < 0.0,
        I, earth_axis_ratio, limb_geometry, &north);
    if (st != TAIYIN_STATUS_OK) return st;
    RouteBoundaryPoint south;
    st = profiled_route_shadow_boundary_point(
        -e.x, e.y, e.zeta, vx_per_day, vy_per_day, -1, e.l2,
        ROUTE_LIMB_CONTOUR_CORE, core_radius < 0.0,
        I, earth_axis_ratio, limb_geometry, &south);
    if (st != TAIYIN_STATUS_OK) return st;

    if (center.valid) {
        if (core_radius < 0.0) {
            std::swap(north, south);
        }
    }
    if (north.valid) {
        fill_geodetic_path_point_rad(
            jd_tt, invalid_jd(), north.longitude_rad, north.latitude_rad, &out->north);
        out->north_valid = true;
    }
    if (south.valid) {
        fill_geodetic_path_point_rad(
            jd_tt, invalid_jd(), south.longitude_rad, south.latitude_rad, &out->south);
        out->south_valid = true;
    }
    return TAIYIN_STATUS_OK;
}

double intersected_route_width_km(
    const NativeCalcContext* context,
    SplitJulianDate jd_tt,
    uint64_t flags,
    const SolarEclipsePathPoint& center,
    double track_east_unit,
    double track_north_unit,
    EphemerisEvalDiagnostic* diagnostic
) noexcept {
    const double steps_seconds[] = {10.0, 30.0, 60.0, 120.0, 300.0};
    for (double step_seconds : steps_seconds) {
        RouteCoreLimitSample before;
        RouteCoreLimitSample after;
        const double step_days = step_seconds / 86400.0;
        Status st = compute_core_route_limits_at_tt(
            context, jd_tt - step_days, flags, &before, diagnostic);
        if (st != TAIYIN_STATUS_OK) return std::nan("");
        st = compute_core_route_limits_at_tt(
            context, jd_tt + step_days, flags, &after, diagnostic);
        if (st != TAIYIN_STATUS_OK) return std::nan("");
        double north_projection = 0.0;
        double south_projection = 0.0;
        const bool north_ok = before.north_valid && after.north_valid
            && intersect_boundary_curve_with_center_normal(
                center, before.north, after.north, track_east_unit, track_north_unit, &north_projection);
        const bool south_ok = before.south_valid && after.south_valid
            && intersect_boundary_curve_with_center_normal(
                center, before.south, after.south, track_east_unit, track_north_unit, &south_projection);
        if (north_ok && south_ok) {
            const double width = std::fabs(north_projection - south_projection);
            if (std::isfinite(width) && width > 0.0) return width;
        }
    }
    return std::nan("");
}

void init_route_row(SolarEclipseRouteRow* out) noexcept {
    out->jd_tt = invalid_jd();
    out->jd_ut = invalid_jd();
    init_path_point(&out->center_line);
    init_path_point(&out->penumbral_north_limit);
    init_path_point(&out->penumbral_south_limit);
    init_path_point(&out->north_limit);
    init_path_point(&out->south_limit);
    init_path_point(&out->half_magnitude_north_limit);
    init_path_point(&out->half_magnitude_south_limit);
    out->path_width_km = std::nan("");
    out->duration_seconds = std::nan("");
    out->sun_altitude_deg = std::nan("");
    out->sun_azimuth_deg = std::nan("");
}

void init_solar_eclipse_where(SolarEclipseWhere* out) noexcept {
    out->jd_tt = invalid_jd();
    out->jd_ut = invalid_jd();
    init_path_point(&out->center_line);
    init_path_point(&out->penumbral_north_limit);
    init_path_point(&out->penumbral_south_limit);
    init_path_point(&out->north_limit);
    init_path_point(&out->south_limit);
    out->magnitude = std::nan("");
    out->obscuration = std::nan("");
    out->center_separation_deg = std::nan("");
    out->sun_angular_radius_deg = std::nan("");
    out->moon_angular_radius_deg = std::nan("");
}

void init_route_product_summary(SolarEclipseRouteProductSummary* out) noexcept {
    if (!out) return;
    out->flags = 0;
    out->curve_point_count = 0;
    out->center_line_count = 0;
    out->core_north_count = 0;
    out->core_south_count = 0;
    out->core_begin_horizon_count = 0;
    out->core_end_horizon_count = 0;
    out->penumbral_north_count = 0;
    out->penumbral_south_count = 0;
    out->half_magnitude_north_count = 0;
    out->half_magnitude_south_count = 0;
    out->core_polygon_point_count = 0;
    out->penumbral_polygon_point_count = 0;
    out->half_magnitude_polygon_point_count = 0;
    out->polygon_point_count = 0;
    out->min_latitude_deg = std::nan("");
    out->max_latitude_deg = std::nan("");
    out->min_unwrapped_longitude_deg = std::nan("");
    out->max_unwrapped_longitude_deg = std::nan("");
}

bool route_product_point_is_finite(const SolarEclipseRouteCurvePoint& point) noexcept {
    return std::isfinite(point.latitude_deg) && std::isfinite(point.longitude_deg);
}

size_t count_route_curve_kind(
    const SolarEclipseRouteCurvePoint* points,
    size_t count,
    uint32_t kind
) noexcept {
    size_t out = 0;
    for (size_t i = 0; i < count; ++i) {
        if (points[i].curve_kind == kind && route_product_point_is_finite(points[i])) ++out;
    }
    return out;
}

void update_route_product_bounds(
    SolarEclipseRouteProductSummary* summary,
    double latitude_deg,
    double unwrapped_longitude_deg
) noexcept {
    if (!summary || !std::isfinite(latitude_deg) || !std::isfinite(unwrapped_longitude_deg)) return;
    if (!std::isfinite(summary->min_latitude_deg)) {
        summary->min_latitude_deg = latitude_deg;
        summary->max_latitude_deg = latitude_deg;
        summary->min_unwrapped_longitude_deg = unwrapped_longitude_deg;
        summary->max_unwrapped_longitude_deg = unwrapped_longitude_deg;
        return;
    }
    summary->min_latitude_deg = std::min(summary->min_latitude_deg, latitude_deg);
    summary->max_latitude_deg = std::max(summary->max_latitude_deg, latitude_deg);
    summary->min_unwrapped_longitude_deg = std::min(summary->min_unwrapped_longitude_deg, unwrapped_longitude_deg);
    summary->max_unwrapped_longitude_deg = std::max(summary->max_unwrapped_longitude_deg, unwrapped_longitude_deg);
}

size_t route_polygon_required_count(
    size_t north_count,
    size_t south_count,
    size_t begin_horizon_count,
    size_t end_horizon_count
) noexcept {
    return north_count >= 2 && south_count >= 2
        && begin_horizon_count >= 2 && end_horizon_count >= 2
        ? north_count + south_count + begin_horizon_count + end_horizon_count + 1
        : 0;
}

Status append_route_polygon(
    const SolarEclipseRouteCurvePoint* curve_points,
    size_t curve_count,
    uint32_t north_curve_kind,
    uint32_t south_curve_kind,
    uint32_t north_point_kind,
    uint32_t south_point_kind,
    bool include_core_horizon,
    bool include_wide_horizon,
    SolarEclipseRouteProductPoint* out_polygon,
    size_t max_polygon_point_count,
    bool can_write_polygon,
    size_t* inout_count,
    SolarEclipseRouteProductSummary* out_summary
) noexcept {
    if (!inout_count) return TAIYIN_ERROR_INVALID_ARGUMENT;
    const size_t start_count = *inout_count;

    const bool half_magnitude_horizon =
        north_curve_kind == TAIYIN_SOLAR_ROUTE_CURVE_HALF_MAGNITUDE_NORTH;
    const uint32_t sunrise_a_kind = half_magnitude_horizon
        ? TAIYIN_SOLAR_ROUTE_CURVE_HALF_MAGNITUDE_SUNRISE_A
        : TAIYIN_SOLAR_ROUTE_CURVE_SUNRISE_MAX_A;
    const uint32_t sunset_a_kind = half_magnitude_horizon
        ? TAIYIN_SOLAR_ROUTE_CURVE_HALF_MAGNITUDE_SUNSET_A
        : TAIYIN_SOLAR_ROUTE_CURVE_SUNSET_MAX_A;
    const uint32_t sunrise_b_kind = half_magnitude_horizon
        ? TAIYIN_SOLAR_ROUTE_CURVE_HALF_MAGNITUDE_SUNRISE_B
        : TAIYIN_SOLAR_ROUTE_CURVE_SUNRISE_MAX_B;
    const uint32_t sunset_b_kind = half_magnitude_horizon
        ? TAIYIN_SOLAR_ROUTE_CURVE_HALF_MAGNITUDE_SUNSET_B
        : TAIYIN_SOLAR_ROUTE_CURVE_SUNSET_MAX_B;

    bool have_previous = false;
    double previous_unwrapped_lon = 0.0;
    double previous_normalized_lon = 0.0;
    SolarEclipseRouteProductPoint first_point{};
    bool have_first_point = false;

    auto append_polygon_point = [&](const SolarEclipseRouteCurvePoint& source, uint32_t point_kind) -> void {
        const double normalized_lon = normalize_degrees(source.longitude_deg + 180.0) - 180.0;
        double unwrapped_lon = normalized_lon;
        if (have_previous) {
            if (std::fabs(normalized_lon - previous_normalized_lon) > 180.0 && out_summary) {
                out_summary->flags |= TAIYIN_SOLAR_ROUTE_PRODUCT_CROSSES_ANTIMERIDIAN;
            }
            unwrapped_lon = previous_unwrapped_lon + normalize_signed_degrees(normalized_lon - previous_unwrapped_lon);
        }

        SolarEclipseRouteProductPoint point{};
        point.jd_tt = source.jd_tt;
        point.jd_ut = source.jd_ut;
        point.point_kind = point_kind;
        point.source_curve_kind = source.curve_kind;
        point.latitude_deg = source.latitude_deg;
        point.longitude_deg = normalized_lon;
        point.unwrapped_longitude_deg = unwrapped_lon;

        if (can_write_polygon) {
            if (*inout_count >= max_polygon_point_count) return;
            out_polygon[*inout_count] = point;
        }
        ++(*inout_count);
        update_route_product_bounds(out_summary, point.latitude_deg, point.unwrapped_longitude_deg);
        if (!have_first_point) {
            first_point = point;
            have_first_point = true;
        }
        previous_unwrapped_lon = unwrapped_lon;
        previous_normalized_lon = normalized_lon;
        have_previous = true;
    };

    auto append_kind_forward = [&](uint32_t curve_kind, uint32_t point_kind) {
        for (size_t i = 0; i < curve_count; ++i) {
            const SolarEclipseRouteCurvePoint& point = curve_points[i];
            if (point.curve_kind == curve_kind && route_product_point_is_finite(point)) {
                append_polygon_point(point, point_kind);
            }
        }
    };
    auto append_kind_reverse = [&](uint32_t curve_kind, uint32_t point_kind) {
        for (size_t i = curve_count; i > 0; --i) {
            const SolarEclipseRouteCurvePoint& point = curve_points[i - 1];
            if (point.curve_kind == curve_kind && route_product_point_is_finite(point)) {
                append_polygon_point(point, point_kind);
            }
        }
    };

    if (include_wide_horizon) {
        append_kind_forward(sunrise_a_kind, north_point_kind);
        append_kind_forward(north_curve_kind, north_point_kind);
        append_kind_forward(sunset_a_kind, north_point_kind);
        append_kind_reverse(sunset_b_kind, south_point_kind);
        append_kind_reverse(south_curve_kind, south_point_kind);
        append_kind_reverse(sunrise_b_kind, south_point_kind);
    } else {
        append_kind_forward(north_curve_kind, north_point_kind);
        if (include_core_horizon) {
            append_kind_forward(
                TAIYIN_SOLAR_ROUTE_CURVE_CORE_END_HORIZON,
                TAIYIN_SOLAR_ROUTE_PRODUCT_POINT_CORE_END_HORIZON);
        }
        append_kind_reverse(south_curve_kind, south_point_kind);
        if (include_core_horizon) {
            append_kind_forward(
                TAIYIN_SOLAR_ROUTE_CURVE_CORE_BEGIN_HORIZON,
                TAIYIN_SOLAR_ROUTE_PRODUCT_POINT_CORE_BEGIN_HORIZON);
        }
    }
    if (have_first_point) {
        SolarEclipseRouteCurvePoint close_source{};
        close_source.jd_tt = first_point.jd_tt;
        close_source.jd_ut = first_point.jd_ut;
        close_source.curve_kind = first_point.source_curve_kind;
        close_source.latitude_deg = first_point.latitude_deg;
        close_source.longitude_deg = first_point.longitude_deg;
        append_polygon_point(close_source, TAIYIN_SOLAR_ROUTE_PRODUCT_POINT_POLYGON_CLOSE);
    }

    return *inout_count > start_count ? TAIYIN_STATUS_OK : TAIYIN_EPHEMERIS_ERROR_EVAL_FAILED;
}

Status fill_route_polygons(
    const SolarEclipseRouteCurvePoint* curve_points,
    size_t curve_count,
    bool include_core,
    bool include_penumbral,
    bool include_half_magnitude,
    size_t required_polygon_count,
    SolarEclipseRouteProductPoint* out_polygon,
    size_t max_polygon_point_count,
    size_t* out_polygon_point_count,
    SolarEclipseRouteProductSummary* out_summary
) noexcept {
    if (!out_polygon_point_count) return TAIYIN_ERROR_INVALID_ARGUMENT;
    *out_polygon_point_count = required_polygon_count;
    if (required_polygon_count == 0) return TAIYIN_STATUS_OK;
    const bool can_write_polygon = out_polygon && max_polygon_point_count >= required_polygon_count;

    size_t out_count = 0;
    if (include_core) {
        const Status st = append_route_polygon(
            curve_points,
            curve_count,
            TAIYIN_SOLAR_ROUTE_CURVE_CORE_NORTH,
            TAIYIN_SOLAR_ROUTE_CURVE_CORE_SOUTH,
            TAIYIN_SOLAR_ROUTE_PRODUCT_POINT_CORE_NORTH,
            TAIYIN_SOLAR_ROUTE_PRODUCT_POINT_CORE_SOUTH,
            true,
            false,
            out_polygon,
            max_polygon_point_count,
            can_write_polygon,
            &out_count,
            out_summary);
        if (st != TAIYIN_STATUS_OK) return st;
    }
    if (include_penumbral) {
        const Status st = append_route_polygon(
            curve_points,
            curve_count,
            TAIYIN_SOLAR_ROUTE_CURVE_PENUMBRAL_NORTH,
            TAIYIN_SOLAR_ROUTE_CURVE_PENUMBRAL_SOUTH,
            TAIYIN_SOLAR_ROUTE_PRODUCT_POINT_PENUMBRAL_NORTH,
            TAIYIN_SOLAR_ROUTE_PRODUCT_POINT_PENUMBRAL_SOUTH,
            false,
            true,
            out_polygon,
            max_polygon_point_count,
            can_write_polygon,
            &out_count,
            out_summary);
        if (st != TAIYIN_STATUS_OK) return st;
    }
    if (include_half_magnitude) {
        const Status st = append_route_polygon(
            curve_points,
            curve_count,
            TAIYIN_SOLAR_ROUTE_CURVE_HALF_MAGNITUDE_NORTH,
            TAIYIN_SOLAR_ROUTE_CURVE_HALF_MAGNITUDE_SOUTH,
            TAIYIN_SOLAR_ROUTE_PRODUCT_POINT_HALF_MAGNITUDE_NORTH,
            TAIYIN_SOLAR_ROUTE_PRODUCT_POINT_HALF_MAGNITUDE_SOUTH,
            false,
            true,
            out_polygon,
            max_polygon_point_count,
            can_write_polygon,
            &out_count,
            out_summary);
        if (st != TAIYIN_STATUS_OK) return st;
    }

    *out_polygon_point_count = out_count;
    if (out_summary) out_summary->polygon_point_count = out_count;
    return out_polygon && !can_write_polygon ? TAIYIN_ERROR_OUT_OF_MEMORY : TAIYIN_STATUS_OK;
}

}  // namespace

static Status compute_solar_eclipse_route_row_from_elements_tt(
    const NativeCalcContext* context,
    SplitJulianDate jd_tt,
    uint64_t flags,
    const SolarBesselianElements& e,
    double vx_per_day,
    double vy_per_day,
    bool compute_metrics,
    bool include_half_magnitude_limits,
    const SolarApparentSnapshot* apparent_snapshot,
    SolarEclipseRouteRow* out,
    EphemerisEvalDiagnostic* diagnostic
) noexcept {
    if (!context || !out || !split_julian_date_is_finite(jd_tt)) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }

    init_route_row(out);
    out->jd_tt = jd_tt;
    Status st = TAIYIN_STATUS_OK;
    if (apparent_snapshot) {
        out->jd_ut = apparent_snapshot->jd_ut;
    } else {
        st = eclipse_tt_to_ut(*context, jd_tt, &out->jd_ut, nullptr, diagnostic);
        if (st != TAIYIN_STATUS_OK) return st;
    }

    solar_route_geometry::Frame I;
    if (apparent_snapshot) {
        const double mu = e.mu_deg * TAIYIN_PI / 180.0;
        const double dec = e.d_deg * TAIYIN_PI / 180.0;
        const double ra = apparent_snapshot->gast_rad - mu;
        I.right_ascension_offset_rad = ra - TAIYIN_PI / 2.0;
        I.pole_rotation_rad = TAIYIN_PI / 2.0 + dec;
        I.gast_rad = apparent_snapshot->gast_rad;
    } else {
        st = route_frame_from_besselian_elements(context, jd_tt, e, &I, diagnostic);
        if (st != TAIYIN_STATUS_OK) return st;
    }
    const double earth_axis_ratio = TAIYIN_WGS84_B_KM / TAIYIN_WGS84_A_KM;
    RouteLunarLimbGeometry limb_geometry;
    st = prepare_route_lunar_limb_geometry(
        context, jd_tt, flags, e, &limb_geometry, diagnostic);
    if (st != TAIYIN_STATUS_OK) return st;

    SolarConeEarthPoint center;
    if (!intersect_solar_shadow_axis_with_oblate_earth(
            -e.x,
            e.y,
            I.pole_rotation_rad,
            I.right_ascension_offset_rad - I.gast_rad,
            earth_axis_ratio,
            &center)) {
        return TAIYIN_EPHEMERIS_ERROR_EVAL_FAILED;
    }
    if (center.valid) {
        if (compute_metrics) {
            st = fill_route_path_point_rad(
                context,
                jd_tt,
                flags,
                center.longitude_rad,
                center.latitude_rad,
                &out->center_line,
                diagnostic);
            if (st != TAIYIN_STATUS_OK) return st;
            SolarEclipsePathPoint refined_center;
            init_path_point(&refined_center);
            if (refine_route_center_from_guess(
                    context,
                    jd_tt,
                    flags,
                    out->center_line,
                    &refined_center,
                    diagnostic) == TAIYIN_STATUS_OK) {
                out->center_line = refined_center;
            }
            out->sun_altitude_deg = out->center_line.sun_altitude_deg;
            out->sun_azimuth_deg = out->center_line.sun_azimuth_deg;
            if (out->center_line.sun_altitude_deg < 0.0) {
                init_path_point(&out->center_line);
            }
        } else {
            fill_geodetic_path_point_rad(
                jd_tt, out->jd_ut, center.longitude_rad, center.latitude_rad, &out->center_line);
        }
    }

    auto fill_boundary = [&](
        double radius,
        int side,
        RouteLimbContourKind contour_kind,
        bool away_from_sun,
        SolarEclipsePathPoint* point
    ) -> Status {
        RouteBoundaryPoint p;
        const Status boundary_status = profiled_route_shadow_boundary_point(
            -e.x, e.y, e.zeta, vx_per_day, vy_per_day, side, radius,
            contour_kind, away_from_sun, I, earth_axis_ratio, limb_geometry, &p);
        if (boundary_status != TAIYIN_STATUS_OK) return boundary_status;
        if (!p.valid) return TAIYIN_STATUS_OK;
        if (compute_metrics) {
            return fill_route_path_point_rad(
                context, jd_tt, flags, p.longitude_rad, p.latitude_rad, point, diagnostic);
        }
        fill_geodetic_path_point_rad(jd_tt, out->jd_ut, p.longitude_rad, p.latitude_rad, point);
        return TAIYIN_STATUS_OK;
    };

    st = fill_boundary(
        e.l1, +1, ROUTE_LIMB_CONTOUR_OUTER, false, &out->penumbral_north_limit);
    if (st != TAIYIN_STATUS_OK) return st;
    st = fill_boundary(
        e.l1, -1, ROUTE_LIMB_CONTOUR_OUTER, false, &out->penumbral_south_limit);
    if (st != TAIYIN_STATUS_OK) return st;

    if (include_half_magnitude_limits) {
        const double half_mag_radius = 0.5 * (e.l1 + e.l2);
        st = fill_boundary(
            half_mag_radius,
            +1,
            ROUTE_LIMB_CONTOUR_HALF_MAGNITUDE,
            false,
            &out->half_magnitude_north_limit);
        if (st != TAIYIN_STATUS_OK) return st;
        st = fill_boundary(
            half_mag_radius,
            -1,
            ROUTE_LIMB_CONTOUR_HALF_MAGNITUDE,
            false,
            &out->half_magnitude_south_limit);
        if (st != TAIYIN_STATUS_OK) return st;
    }

    double core_radius = NAN;
    RouteBoundaryPoint north{};
    RouteBoundaryPoint south{};
    core_radius = center.valid
        ? e.l2 + e.tan_f2 * center.distance_to_parameter_one
        : e.l2;
    st = profiled_route_shadow_boundary_point(
        -e.x, e.y, e.zeta, vx_per_day, vy_per_day, +1, e.l2,
        ROUTE_LIMB_CONTOUR_CORE, core_radius < 0.0,
        I, earth_axis_ratio, limb_geometry, &north);
    if (st != TAIYIN_STATUS_OK) return st;
    st = profiled_route_shadow_boundary_point(
        -e.x, e.y, e.zeta, vx_per_day, vy_per_day, -1, e.l2,
        ROUTE_LIMB_CONTOUR_CORE, core_radius < 0.0,
        I, earth_axis_ratio, limb_geometry, &south);
    if (st != TAIYIN_STATUS_OK) return st;
    if (core_radius < 0.0) {
        std::swap(north, south);
    }
    if (north.valid) {
        if (compute_metrics) {
            st = fill_route_path_point_rad(
                context,
                jd_tt,
                flags,
                north.longitude_rad,
                north.latitude_rad,
                &out->north_limit,
                diagnostic);
            if (st != TAIYIN_STATUS_OK) return st;
        } else {
            fill_geodetic_path_point_rad(
                jd_tt, out->jd_ut, north.longitude_rad, north.latitude_rad,
                &out->north_limit);
        }
    }
    if (south.valid) {
        if (compute_metrics) {
            st = fill_route_path_point_rad(
                context,
                jd_tt,
                flags,
                south.longitude_rad,
                south.latitude_rad,
                &out->south_limit,
                diagnostic);
            if (st != TAIYIN_STATUS_OK) return st;
        } else {
            fill_geodetic_path_point_rad(
                jd_tt, out->jd_ut, south.longitude_rad, south.latitude_rad,
                &out->south_limit);
        }
    }

    if (compute_metrics && center.valid && std::isfinite(out->center_line.latitude_deg)) {
        // Route-table width is transverse to the center-line track, not the same-time
        // north/south limit arc and not the raw solar-projection feature width.
        const double track_step_days = 10.0 / 86400.0;
        SolarEclipsePathPoint before_point;
        SolarEclipsePathPoint after_point;
        st = compute_route_center_point_tt(
            context, jd_tt - track_step_days, flags, &before_point, diagnostic);
        if (st != TAIYIN_STATUS_OK) return st;
        st = compute_route_center_point_tt(
            context, jd_tt + track_step_days, flags, &after_point, diagnostic);
        if (st != TAIYIN_STATUS_OK) return st;
        if (std::isfinite(before_point.latitude_deg)
            && std::isfinite(after_point.latitude_deg)
            && north.valid && south.valid) {
            double before_east = 0.0;
            double before_north = 0.0;
            double after_east = 0.0;
            double after_north = 0.0;
            if (local_tangent_coordinates_km(out->center_line, before_point, &before_east, &before_north)
                && local_tangent_coordinates_km(out->center_line, after_point, &after_east, &after_north)) {
                double track_east = after_east - before_east;
                double track_north = after_north - before_north;
                const double track_norm = std::hypot(track_east, track_north);
                if (track_norm > 1e-9) {
                    track_east /= track_norm;
                    track_north /= track_norm;
                    double route_width = intersected_route_width_km(
                        context, jd_tt, flags, out->center_line, track_east, track_north, diagnostic);
                    if (!std::isfinite(route_width) || !(route_width > 0.0)) {
                        route_width = projected_route_width_km(
                            out->center_line, out->north_limit, out->south_limit, track_east, track_north);
                    }
                    if (std::isfinite(route_width) && route_width > 0.0) {
                        out->path_width_km = route_width;
                    }
                }
            }
        }

        const RouteSurfaceVelocity sv = route_surface_shadow_velocity(-e.x, e.y, I.pole_rotation_rad, vx_per_day, vy_per_day);
        if (sv.speed > 1e-14 && (north.valid || south.valid)) {
            out->duration_seconds = 2.0 * std::fabs(core_radius) / sv.speed * 86400.0;
            if (limb_geometry.enabled) {
                double corrected_duration_seconds = NAN;
                st = refine_local_solar_inner_contact_duration_tt(
                    context,
                    jd_tt,
                    center.longitude_rad * 180.0 / TAIYIN_PI,
                    center.latitude_rad * 180.0 / TAIYIN_PI,
                    0.0,
                    flags,
                    out->duration_seconds,
                    &corrected_duration_seconds,
                    diagnostic);
                if (st != TAIYIN_STATUS_OK) return st;
                out->duration_seconds = corrected_duration_seconds;
            }
        }
    }

    return TAIYIN_STATUS_OK;
}

Status build_core_route_polygon_tt(
    const NativeCalcContext* context,
    SplitJulianDate jd_near_tt,
    uint64_t flags,
    size_t route_sample_count,
    std::vector<SolarEclipseRouteProductPoint>* out_polygon,
    EphemerisEvalDiagnostic* diagnostic
) noexcept;

double core_polygon_width_at_track(
    const SolarEclipseRouteProductPoint* polygon,
    size_t polygon_count,
    const SolarEclipsePathPoint& center,
    double track_east,
    double track_north
) noexcept;

static Status compute_solar_eclipse_route_row_tt_impl(
    const NativeCalcContext* context,
    SplitJulianDate jd_tt,
    uint64_t flags,
    bool refine_closed_path_width,
    SolarEclipseRouteRow* out,
    EphemerisEvalDiagnostic* diagnostic
) noexcept {
    if (!context || !out || !split_julian_date_is_finite(jd_tt)
        || !valid_solar_eclipse_route_flags(flags)) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }

    SolarBesselianElements e;
    double x_velocity_per_day = 0.0;
    double y_velocity_per_day = 0.0;
    Status st = compute_solar_besselian_elements_and_velocity_tt_with_corrections(
        context, jd_tt, 0.0, flags, nullptr, &e,
        &x_velocity_per_day, &y_velocity_per_day, nullptr, diagnostic);
    if (st != TAIYIN_STATUS_OK) return st;
    st = compute_solar_eclipse_route_row_from_elements_tt(
        context, jd_tt, flags, e, -x_velocity_per_day, y_velocity_per_day,
        true, true, nullptr, out, diagnostic);
    if (st != TAIYIN_STATUS_OK || !refine_closed_path_width
        || !std::isfinite(out->center_line.latitude_deg)
        || !(out->sun_altitude_deg < 12.0)) {
        return st;
    }

    const double track_step_days = 10.0 / 86400.0;
    SolarEclipsePathPoint before_center;
    SolarEclipsePathPoint after_center;
    st = compute_route_center_point_tt(
        context, jd_tt - track_step_days, flags, &before_center, diagnostic);
    if (st != TAIYIN_STATUS_OK) return st;
    st = compute_route_center_point_tt(
        context, jd_tt + track_step_days, flags, &after_center, diagnostic);
    if (st != TAIYIN_STATUS_OK) return st;
    double before_east = NAN;
    double before_north = NAN;
    double after_east = NAN;
    double after_north = NAN;
    if (!local_tangent_coordinates_km(
            out->center_line, before_center, &before_east, &before_north)
        || !local_tangent_coordinates_km(
            out->center_line, after_center, &after_east, &after_north)) {
        return TAIYIN_STATUS_OK;
    }
    double track_east = after_east - before_east;
    double track_north = after_north - before_north;
    const double track_norm = std::hypot(track_east, track_north);
    if (!(track_norm > 1.0e-9)) return TAIYIN_STATUS_OK;
    track_east /= track_norm;
    track_north /= track_norm;

    std::vector<SolarEclipseRouteProductPoint> polygon;
    st = build_core_route_polygon_tt(
        context, jd_tt, flags, 256, &polygon, diagnostic);
    if (st != TAIYIN_STATUS_OK) return st;
    const double closed_width = core_polygon_width_at_track(
        polygon.data(), polygon.size(), out->center_line, track_east, track_north);
    if (std::isfinite(closed_width)) out->path_width_km = closed_width;
    return TAIYIN_STATUS_OK;
}

Status compute_solar_eclipse_route_row_tt(
    const NativeCalcContext* context,
    SplitJulianDate jd_tt,
    uint64_t flags,
    SolarEclipseRouteRow* out,
    EphemerisEvalDiagnostic* diagnostic
) noexcept {
    return compute_solar_eclipse_route_row_tt_impl(
        context, jd_tt, flags, true, out, diagnostic);
}

Status compute_solar_eclipse_where_tt(
    const NativeCalcContext* context,
    SplitJulianDate jd_tt,
    uint64_t flags,
    SolarEclipseWhere* out,
    EphemerisEvalDiagnostic* diagnostic
) noexcept {
    if (!context || !out || !split_julian_date_is_finite(jd_tt)
        || !valid_solar_eclipse_route_flags(flags)) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    init_solar_eclipse_where(out);

    SolarBesselianElements elements;
    SolarApparentSnapshot apparent_snapshot;
    double x_velocity_per_day = 0.0;
    double y_velocity_per_day = 0.0;
    Status st = compute_solar_besselian_elements_and_velocity_tt_with_corrections(
        context, jd_tt, 0.0, flags, nullptr, &elements,
        &x_velocity_per_day, &y_velocity_per_day, &apparent_snapshot, diagnostic);
    if (st != TAIYIN_STATUS_OK) return st;
    SolarEclipseRouteRow geometry;
    st = compute_solar_eclipse_route_row_from_elements_tt(
        context, jd_tt, flags, elements, -x_velocity_per_day, y_velocity_per_day,
        false, false, &apparent_snapshot, &geometry, diagnostic);
    if (st != TAIYIN_STATUS_OK) return st;

    out->jd_tt = geometry.jd_tt;
    out->jd_ut = geometry.jd_ut;
    out->center_line = geometry.center_line;
    out->penumbral_north_limit = geometry.penumbral_north_limit;
    out->penumbral_south_limit = geometry.penumbral_south_limit;
    out->north_limit = geometry.north_limit;
    out->south_limit = geometry.south_limit;

    if (std::isfinite(out->center_line.longitude_deg)
        && std::isfinite(out->center_line.latitude_deg)) {
        LocalSolarEclipseCircumstances circumstances;
        st = compute_local_solar_circumstances_from_apparent_snapshot_tt(
            context, apparent_snapshot,
            out->center_line.longitude_deg, out->center_line.latitude_deg,
            0.0, &circumstances, diagnostic);
        if (st != TAIYIN_STATUS_OK) return st;
        out->center_line.sun_altitude_deg = circumstances.sun_altitude_deg;
        out->center_line.sun_azimuth_deg = circumstances.sun_azimuth_deg;
        out->magnitude = circumstances.magnitude;
        out->obscuration = circumstances.obscuration;
        out->center_separation_deg = circumstances.center_separation_deg;
        out->sun_angular_radius_deg = circumstances.sun_angular_radius_deg;
        out->moon_angular_radius_deg = circumstances.moon_angular_radius_deg;
    }
    return TAIYIN_STATUS_OK;
}

Status compute_solar_eclipse_where_ut(
    const NativeCalcContext* context,
    SplitJulianDate jd_ut,
    uint64_t flags,
    SolarEclipseWhere* out,
    EphemerisEvalDiagnostic* diagnostic
) noexcept {
    if (!context || !out || !split_julian_date_is_finite(jd_ut)
        || !valid_solar_eclipse_route_flags(flags)) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    SplitJulianDate jd_tt;
    const Status st = eclipse_ut_to_tt(*context, jd_ut, &jd_tt, nullptr, diagnostic);
    if (st != TAIYIN_STATUS_OK) return st;
    return compute_solar_eclipse_where_tt(context, jd_tt, flags, out, diagnostic);
}

Status compute_solar_eclipse_route_row_ut(
    const NativeCalcContext* context,
    SplitJulianDate jd_ut,
    uint64_t flags,
    SolarEclipseRouteRow* out,
    EphemerisEvalDiagnostic* diagnostic
) noexcept {
    if (!context || !out || !split_julian_date_is_finite(jd_ut)
        || !valid_solar_eclipse_route_flags(flags)) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    SplitJulianDate jd_tt;
    const Status st = eclipse_ut_to_tt(*context, jd_ut, &jd_tt, nullptr, diagnostic);
    if (st != TAIYIN_STATUS_OK) return st;
    return compute_solar_eclipse_route_row_tt(context, jd_tt, flags, out, diagnostic);
}

bool route_row_track_unit(
    const SolarEclipseRouteRow* rows,
    size_t count,
    size_t index,
    double* out_east,
    double* out_north
) noexcept {
    if (!rows || index >= count || !out_east || !out_north
        || !std::isfinite(rows[index].center_line.latitude_deg)) {
        return false;
    }
    size_t before = index;
    while (before > 0) {
        --before;
        if (std::isfinite(rows[before].center_line.latitude_deg)) break;
    }
    size_t after = index;
    while (after + 1 < count) {
        ++after;
        if (std::isfinite(rows[after].center_line.latitude_deg)) break;
    }
    const bool have_before = before < index
        && std::isfinite(rows[before].center_line.latitude_deg);
    const bool have_after = after > index
        && std::isfinite(rows[after].center_line.latitude_deg);
    if (!have_before && !have_after) return false;

    double before_east = 0.0;
    double before_north = 0.0;
    double after_east = 0.0;
    double after_north = 0.0;
    if (have_before && !local_tangent_coordinates_km(
            rows[index].center_line,
            rows[before].center_line,
            &before_east,
            &before_north)) {
        return false;
    }
    if (have_after && !local_tangent_coordinates_km(
            rows[index].center_line,
            rows[after].center_line,
            &after_east,
            &after_north)) {
        return false;
    }
    double east = have_after ? after_east : -before_east;
    double north = have_after ? after_north : -before_north;
    if (have_before && have_after) {
        east = after_east - before_east;
        north = after_north - before_north;
    }
    const double norm = std::hypot(east, north);
    if (!(norm > 1.0e-9)) return false;
    *out_east = east / norm;
    *out_north = north / norm;
    return true;
}

double core_polygon_width_at_track(
    const SolarEclipseRouteProductPoint* polygon,
    size_t polygon_count,
    const SolarEclipsePathPoint& center,
    double track_east,
    double track_north
) noexcept {
    if (!polygon || polygon_count < 3) return std::nan("");
    double minimum_projection = std::numeric_limits<double>::infinity();
    double maximum_projection = -std::numeric_limits<double>::infinity();
    for (size_t point_index = 0; point_index + 1 < polygon_count; ++point_index) {
        SolarEclipsePathPoint a;
        SolarEclipsePathPoint b;
        init_path_point(&a);
        init_path_point(&b);
        a.latitude_deg = polygon[point_index].latitude_deg;
        a.longitude_deg = polygon[point_index].longitude_deg;
        b.latitude_deg = polygon[point_index + 1].latitude_deg;
        b.longitude_deg = polygon[point_index + 1].longitude_deg;
        double projection = NAN;
        if (intersect_boundary_curve_with_center_normal(
                center, a, b, track_east, track_north, &projection)) {
            minimum_projection = std::min(minimum_projection, projection);
            maximum_projection = std::max(maximum_projection, projection);
        }
    }
    if (!(minimum_projection < 0.0 && maximum_projection > 0.0)) {
        return std::nan("");
    }
    const double width = maximum_projection - minimum_projection;
    return std::isfinite(width) && width > 0.0 ? width : std::nan("");
}

void apply_core_polygon_widths(
    const SolarEclipseRouteProductPoint* polygon,
    size_t polygon_count,
    SolarEclipseRouteRow* rows,
    size_t row_count
) noexcept {
    if (!polygon || polygon_count < 3 || !rows) return;
    for (size_t row_index = 0; row_index < row_count; ++row_index) {
        SolarEclipseRouteRow& row = rows[row_index];
        double track_east = NAN;
        double track_north = NAN;
        if (!route_row_track_unit(
                rows, row_count, row_index, &track_east, &track_north)) {
            continue;
        }
        const double width = core_polygon_width_at_track(
            polygon, polygon_count, row.center_line, track_east, track_north);
        if (std::isfinite(width)) row.path_width_km = width;
    }
}

Status compute_solar_eclipse_route_product_curve_points_tt(
    const NativeCalcContext* context,
    SplitJulianDate jd_near_tt,
    uint64_t flags,
    size_t route_sample_count,
    std::vector<SolarEclipseRouteCurvePoint>* out_points,
    EphemerisEvalDiagnostic* diagnostic
) noexcept;

Status build_core_route_polygon_tt(
    const NativeCalcContext* context,
    SplitJulianDate jd_near_tt,
    uint64_t flags,
    size_t route_sample_count,
    std::vector<SolarEclipseRouteProductPoint>* out_polygon,
    EphemerisEvalDiagnostic* diagnostic
) noexcept {
    if (!context || !out_polygon) return TAIYIN_ERROR_INVALID_ARGUMENT;
    out_polygon->clear();
    std::vector<SolarEclipseRouteCurvePoint> curve_points;
    Status status = compute_solar_eclipse_route_product_curve_points_tt(
        context,
        jd_near_tt,
        flags,
        route_sample_count,
        &curve_points,
        diagnostic);
    if (status != TAIYIN_STATUS_OK) return status;
    const size_t core_north_count = count_route_curve_kind(
        curve_points.data(), curve_points.size(), TAIYIN_SOLAR_ROUTE_CURVE_CORE_NORTH);
    const size_t core_south_count = count_route_curve_kind(
        curve_points.data(), curve_points.size(), TAIYIN_SOLAR_ROUTE_CURVE_CORE_SOUTH);
    const size_t core_begin_horizon_count = count_route_curve_kind(
        curve_points.data(), curve_points.size(), TAIYIN_SOLAR_ROUTE_CURVE_CORE_BEGIN_HORIZON);
    const size_t core_end_horizon_count = count_route_curve_kind(
        curve_points.data(), curve_points.size(), TAIYIN_SOLAR_ROUTE_CURVE_CORE_END_HORIZON);
    size_t polygon_count = route_polygon_required_count(
        core_north_count,
        core_south_count,
        core_begin_horizon_count,
        core_end_horizon_count);
    if (polygon_count < 3) return TAIYIN_STATUS_OK;
    try {
        out_polygon->resize(polygon_count);
    } catch (const std::bad_alloc&) {
        return TAIYIN_ERROR_OUT_OF_MEMORY;
    }
    status = fill_route_polygons(
        curve_points.data(),
        curve_points.size(),
        true,
        false,
        false,
        polygon_count,
        out_polygon->data(),
        out_polygon->size(),
        &polygon_count,
        nullptr);
    if (status != TAIYIN_STATUS_OK) {
        out_polygon->clear();
        return status;
    }
    out_polygon->resize(polygon_count);
    return TAIYIN_STATUS_OK;
}

static Status apply_batch_core_polygon_widths(
    const NativeCalcContext* context,
    uint64_t flags,
    SolarEclipseRouteRow* rows,
    size_t row_count,
    EphemerisEvalDiagnostic* diagnostic
) noexcept {
    if (!rows || row_count == 0) return TAIYIN_STATUS_OK;

    size_t group_begin = 0;
    while (group_begin < row_count) {
        size_t group_end = group_begin + 1;
        while (group_end < row_count
            && rows[group_end].jd_tt - rows[group_end - 1].jd_tt
                <= kRouteBatchEventGapDays) {
            ++group_end;
        }
        const size_t group_count = group_end - group_begin;
        SolarEclipseRouteRow* const group_rows = rows + group_begin;
        if (group_count == 1) {
            SolarEclipseRouteRow refined;
            const Status status = compute_solar_eclipse_route_row_tt_impl(
                context,
                group_rows[0].jd_tt,
                flags,
                true,
                &refined,
                diagnostic);
            if (status != TAIYIN_STATUS_OK) return status;
            // Preserve the public batch grid exactly; the refined row
            // represents the same TT epoch but may have obtained UT through
            // a numerical round trip.
            refined.jd_ut = group_rows[0].jd_ut;
            group_rows[0] = refined;
        } else {
            const SplitJulianDate near_jd_tt =
                group_rows[group_count / 2].jd_tt;
            std::vector<SolarEclipseRouteProductPoint> polygon;
            const Status status = build_core_route_polygon_tt(
                context, near_jd_tt, flags, 256, &polygon, diagnostic);
            if (status != TAIYIN_STATUS_OK) return status;
            if (polygon.size() >= 3) {
                apply_core_polygon_widths(
                    polygon.data(), polygon.size(), group_rows, group_count);
            }
        }
        group_begin = group_end;
    }
    return TAIYIN_STATUS_OK;
}

Status compute_solar_eclipse_route_tt(
    const NativeCalcContext* context,
    SplitJulianDate start_jd_tt,
    SplitJulianDate end_jd_tt,
    double step_minutes,
    uint64_t flags,
    SolarEclipseRouteRow* out_rows,
    size_t max_row_count,
    size_t* out_row_count,
    EphemerisEvalDiagnostic* diagnostic
) noexcept {
    if (!context || !out_row_count
        || !split_julian_date_is_finite(start_jd_tt)
        || !split_julian_date_is_finite(end_jd_tt)
        || !std::isfinite(step_minutes)
        || !valid_solar_eclipse_route_flags(flags)
        || !(step_minutes > 0.0)
        || end_jd_tt < start_jd_tt) {
        if (out_row_count) *out_row_count = 0;
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }

    *out_row_count = 0;
    const double step_days = step_minutes / 1440.0;
    size_t count = 0;
    SplitJulianDate jd = start_jd_tt;
    for (;;) {
        const SplitJulianDate sample_jd_tt = std::min(jd, end_jd_tt);
        SolarEclipseRouteRow row;
        const Status st = compute_solar_eclipse_route_row_tt_impl(
            context, sample_jd_tt, flags, false, &row, diagnostic);
        if (st != TAIYIN_STATUS_OK) {
            *out_row_count = count;
            return st;
        }
        const bool has_data = std::isfinite(row.center_line.latitude_deg)
            || std::isfinite(row.penumbral_north_limit.latitude_deg)
            || std::isfinite(row.penumbral_south_limit.latitude_deg)
            || std::isfinite(row.north_limit.latitude_deg)
            || std::isfinite(row.south_limit.latitude_deg);
        if (has_data) {
            if (out_rows) {
                if (count >= max_row_count) {
                    *out_row_count = count;
                    return TAIYIN_ERROR_OUT_OF_MEMORY;
                }
                out_rows[count] = row;
            }
            ++count;
        }
        if (!(sample_jd_tt < end_jd_tt)) break;
        const SplitJulianDate next_jd = jd + step_days;
        jd = next_jd > jd ? next_jd : end_jd_tt;
    }
    const Status polygon_status = apply_batch_core_polygon_widths(
        context, flags, out_rows, count, diagnostic);
    if (polygon_status != TAIYIN_STATUS_OK) {
        *out_row_count = count;
        return polygon_status;
    }
    *out_row_count = count;
    return TAIYIN_STATUS_OK;
}

Status compute_solar_eclipse_route_ut(
    const NativeCalcContext* context,
    SplitJulianDate start_jd_ut,
    SplitJulianDate end_jd_ut,
    double step_minutes,
    uint64_t flags,
    SolarEclipseRouteRow* out_rows,
    size_t max_row_count,
    size_t* out_row_count,
    EphemerisEvalDiagnostic* diagnostic
) noexcept {
    if (!context || !out_row_count
        || !split_julian_date_is_finite(start_jd_ut)
        || !split_julian_date_is_finite(end_jd_ut)
        || !std::isfinite(step_minutes)
        || !valid_solar_eclipse_route_flags(flags)
        || !(step_minutes > 0.0)
        || end_jd_ut < start_jd_ut) {
        if (out_row_count) *out_row_count = 0;
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }

    *out_row_count = 0;
    const double step_days = step_minutes / 1440.0;
    size_t count = 0;
    SplitJulianDate jd_ut = start_jd_ut;
    for (;;) {
        const SplitJulianDate sample_jd_ut = std::min(jd_ut, end_jd_ut);
        SplitJulianDate sample_jd_tt;
        Status status = eclipse_ut_to_tt(
            *context, sample_jd_ut, &sample_jd_tt, nullptr, diagnostic);
        if (status != TAIYIN_STATUS_OK) {
            *out_row_count = count;
            return status;
        }

        SolarEclipseRouteRow row;
        status = compute_solar_eclipse_route_row_tt_impl(
            context, sample_jd_tt, flags, false, &row, diagnostic);
        if (status != TAIYIN_STATUS_OK) {
            *out_row_count = count;
            return status;
        }
        // Preserve the exact public input-grid epoch. The internal TT row
        // conversion is only a numerical round trip back to the same UT.
        row.jd_ut = sample_jd_ut;
        const bool has_data = std::isfinite(row.center_line.latitude_deg)
            || std::isfinite(row.penumbral_north_limit.latitude_deg)
            || std::isfinite(row.penumbral_south_limit.latitude_deg)
            || std::isfinite(row.north_limit.latitude_deg)
            || std::isfinite(row.south_limit.latitude_deg);
        if (has_data) {
            if (out_rows) {
                if (count >= max_row_count) {
                    *out_row_count = count;
                    return TAIYIN_ERROR_OUT_OF_MEMORY;
                }
                out_rows[count] = row;
            }
            ++count;
        }
        if (!(sample_jd_ut < end_jd_ut)) break;
        const double remaining_days = end_jd_ut - sample_jd_ut;
        const double endpoint_snap_days = std::min(
            step_days * kRouteEndpointSnapRelativeToStep,
            kRouteEndpointSnapMaximumSeconds / 86400.0);
        if (remaining_days <= step_days + endpoint_snap_days) {
            jd_ut = end_jd_ut;
        } else {
            const SplitJulianDate next_jd = jd_ut + step_days;
            jd_ut = next_jd > jd_ut ? next_jd : end_jd_ut;
        }
    }

    const Status polygon_status = apply_batch_core_polygon_widths(
        context, flags, out_rows, count, diagnostic);
    if (polygon_status != TAIYIN_STATUS_OK) {
        *out_row_count = count;
        return polygon_status;
    }
    *out_row_count = count;
    return TAIYIN_STATUS_OK;
}

Status compute_solar_eclipse_route_product_curve_points_tt(
    const NativeCalcContext* context,
    SplitJulianDate jd_near_tt,
    uint64_t flags,
    size_t route_sample_count,
    std::vector<SolarEclipseRouteCurvePoint>* out_points,
    EphemerisEvalDiagnostic* diagnostic
) noexcept;

Status compute_solar_eclipse_route_map_curve_points_tt(
    const NativeCalcContext* context,
    SplitJulianDate jd_near_tt,
    uint64_t flags,
    size_t route_sample_count,
    std::vector<SolarEclipseRouteCurvePoint>* out_points,
    EphemerisEvalDiagnostic* diagnostic
) noexcept;

Status compute_solar_eclipse_route_curves_tt(
    const NativeCalcContext* context,
    SplitJulianDate jd_near_tt,
    uint64_t flags,
    SolarEclipseRouteCurvePoint* out_points,
    size_t max_point_count,
    size_t* out_point_count,
    EphemerisEvalDiagnostic* diagnostic
) noexcept {
    return compute_solar_eclipse_route_curves_tt_with_options(
        context,
        jd_near_tt,
        flags,
        TAIYIN_SOLAR_ROUTE_DEFAULT_SAMPLE_COUNT,
        out_points,
        max_point_count,
        out_point_count,
        diagnostic);
}

Status compute_solar_eclipse_route_curves_tt_with_options(
    const NativeCalcContext* context,
    SplitJulianDate jd_near_tt,
    uint64_t flags,
    size_t route_sample_count,
    SolarEclipseRouteCurvePoint* out_points,
    size_t max_point_count,
    size_t* out_point_count,
    EphemerisEvalDiagnostic* diagnostic
) noexcept {
    if (!context || !out_point_count || !split_julian_date_is_finite(jd_near_tt)
        || !valid_solar_eclipse_route_flags(flags)
        || !valid_solar_route_sample_count(route_sample_count)) {
        if (out_point_count) *out_point_count = 0;
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    *out_point_count = 0;

    std::vector<SolarEclipseRouteCurvePoint> points;
    const Status st = compute_solar_eclipse_route_map_curve_points_tt(
        context, jd_near_tt, flags, route_sample_count, &points, diagnostic);
    if (st != TAIYIN_STATUS_OK) return st;
    *out_point_count = points.size();
    if (out_points) {
        if (max_point_count < points.size()) return TAIYIN_ERROR_OUT_OF_MEMORY;
        std::copy(points.begin(), points.end(), out_points);
    }
    return TAIYIN_STATUS_OK;
}

Status compute_solar_eclipse_route_curves_ut(
    const NativeCalcContext* context,
    SplitJulianDate jd_near_ut,
    uint64_t flags,
    SolarEclipseRouteCurvePoint* out_points,
    size_t max_point_count,
    size_t* out_point_count,
    EphemerisEvalDiagnostic* diagnostic
) noexcept {
    return compute_solar_eclipse_route_curves_ut_with_options(
        context,
        jd_near_ut,
        flags,
        TAIYIN_SOLAR_ROUTE_DEFAULT_SAMPLE_COUNT,
        out_points,
        max_point_count,
        out_point_count,
        diagnostic);
}

Status compute_solar_eclipse_route_curves_ut_with_options(
    const NativeCalcContext* context,
    SplitJulianDate jd_near_ut,
    uint64_t flags,
    size_t route_sample_count,
    SolarEclipseRouteCurvePoint* out_points,
    size_t max_point_count,
    size_t* out_point_count,
    EphemerisEvalDiagnostic* diagnostic
) noexcept {
    if (!context || !out_point_count || !split_julian_date_is_finite(jd_near_ut)
        || !valid_solar_eclipse_route_flags(flags)
        || !valid_solar_route_sample_count(route_sample_count)) {
        if (out_point_count) *out_point_count = 0;
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    SplitJulianDate jd_near_tt;
    const Status st = eclipse_ut_to_tt(*context, jd_near_ut, &jd_near_tt, nullptr, diagnostic);
    if (st != TAIYIN_STATUS_OK) return st;
    return compute_solar_eclipse_route_curves_tt_with_options(
        context,
        jd_near_tt,
        flags,
        route_sample_count,
        out_points,
        max_point_count,
        out_point_count,
        diagnostic);
}

Status compute_solar_eclipse_route_curve_vector_tt(
    const NativeCalcContext* context,
    SplitJulianDate jd_near_tt,
    uint64_t flags,
    std::vector<SolarEclipseRouteCurvePoint>* out_points,
    EphemerisEvalDiagnostic* diagnostic
) noexcept {
    if (!context || !out_points || !split_julian_date_is_finite(jd_near_tt)
        || !valid_solar_eclipse_route_flags(flags)) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    out_points->clear();
    return compute_solar_eclipse_route_product_curve_points_tt(
        context,
        jd_near_tt,
        flags,
        TAIYIN_SOLAR_ROUTE_DEFAULT_SAMPLE_COUNT,
        out_points,
        diagnostic);
}

struct RouteCurveBuckets {
    std::vector<SolarEclipseRouteCurvePoint> partial_begin_a;
    std::vector<SolarEclipseRouteCurvePoint> partial_begin_b;
    std::vector<SolarEclipseRouteCurvePoint> partial_end_a;
    std::vector<SolarEclipseRouteCurvePoint> partial_end_b;
    std::vector<SolarEclipseRouteCurvePoint> sunrise_max_a;
    std::vector<SolarEclipseRouteCurvePoint> sunrise_max_b;
    std::vector<SolarEclipseRouteCurvePoint> sunset_max_a;
    std::vector<SolarEclipseRouteCurvePoint> sunset_max_b;
    std::vector<SolarEclipseRouteCurvePoint> center_line;
    std::vector<SolarEclipseRouteCurvePoint> penumbral_north;
    std::vector<SolarEclipseRouteCurvePoint> penumbral_south;
    std::vector<SolarEclipseRouteCurvePoint> core_north;
    std::vector<SolarEclipseRouteCurvePoint> core_south;
    std::vector<SolarEclipseRouteCurvePoint> core_begin_horizon;
    std::vector<SolarEclipseRouteCurvePoint> core_end_horizon;
    std::vector<SolarEclipseRouteCurvePoint> half_magnitude_north;
    std::vector<SolarEclipseRouteCurvePoint> half_magnitude_south;
    std::vector<SolarEclipseRouteCurvePoint> half_magnitude_sunrise_a;
    std::vector<SolarEclipseRouteCurvePoint> half_magnitude_sunrise_b;
    std::vector<SolarEclipseRouteCurvePoint> half_magnitude_sunset_a;
    std::vector<SolarEclipseRouteCurvePoint> half_magnitude_sunset_b;

    size_t size() const noexcept {
        return partial_begin_a.size()
            + partial_begin_b.size()
            + partial_end_a.size()
            + partial_end_b.size()
            + sunrise_max_a.size()
            + sunrise_max_b.size()
            + sunset_max_a.size()
            + sunset_max_b.size()
            + center_line.size()
            + penumbral_north.size()
            + penumbral_south.size()
            + core_north.size()
            + core_south.size()
            + core_begin_horizon.size()
            + core_end_horizon.size()
            + half_magnitude_north.size()
            + half_magnitude_south.size()
            + half_magnitude_sunrise_a.size()
            + half_magnitude_sunrise_b.size()
            + half_magnitude_sunset_a.size()
            + half_magnitude_sunset_b.size();
    }
};

Status append_route_curve_bucket(
    std::vector<SolarEclipseRouteCurvePoint>* out_points,
    const std::vector<SolarEclipseRouteCurvePoint>& bucket
) noexcept {
    if (!out_points) return TAIYIN_ERROR_INVALID_ARGUMENT;
    try {
        out_points->insert(out_points->end(), bucket.begin(), bucket.end());
    } catch (const std::bad_alloc&) {
        return TAIYIN_ERROR_OUT_OF_MEMORY;
    }
    return TAIYIN_STATUS_OK;
}

Status flatten_route_curve_buckets(
    const RouteCurveBuckets& buckets,
    std::vector<SolarEclipseRouteCurvePoint>* out_points
) noexcept {
    if (!out_points) return TAIYIN_ERROR_INVALID_ARGUMENT;
    out_points->clear();
    try {
        out_points->reserve(buckets.size());
    } catch (const std::bad_alloc&) {
        return TAIYIN_ERROR_OUT_OF_MEMORY;
    }
    Status st = append_route_curve_bucket(out_points, buckets.partial_begin_a);
    if (st != TAIYIN_STATUS_OK) return st;
    st = append_route_curve_bucket(out_points, buckets.partial_begin_b);
    if (st != TAIYIN_STATUS_OK) return st;
    st = append_route_curve_bucket(out_points, buckets.partial_end_a);
    if (st != TAIYIN_STATUS_OK) return st;
    st = append_route_curve_bucket(out_points, buckets.partial_end_b);
    if (st != TAIYIN_STATUS_OK) return st;
    st = append_route_curve_bucket(out_points, buckets.sunrise_max_a);
    if (st != TAIYIN_STATUS_OK) return st;
    st = append_route_curve_bucket(out_points, buckets.sunrise_max_b);
    if (st != TAIYIN_STATUS_OK) return st;
    st = append_route_curve_bucket(out_points, buckets.sunset_max_a);
    if (st != TAIYIN_STATUS_OK) return st;
    st = append_route_curve_bucket(out_points, buckets.sunset_max_b);
    if (st != TAIYIN_STATUS_OK) return st;
    st = append_route_curve_bucket(out_points, buckets.center_line);
    if (st != TAIYIN_STATUS_OK) return st;
    st = append_route_curve_bucket(out_points, buckets.penumbral_north);
    if (st != TAIYIN_STATUS_OK) return st;
    st = append_route_curve_bucket(out_points, buckets.penumbral_south);
    if (st != TAIYIN_STATUS_OK) return st;
    st = append_route_curve_bucket(out_points, buckets.core_north);
    if (st != TAIYIN_STATUS_OK) return st;
    st = append_route_curve_bucket(out_points, buckets.core_south);
    if (st != TAIYIN_STATUS_OK) return st;
    st = append_route_curve_bucket(out_points, buckets.core_begin_horizon);
    if (st != TAIYIN_STATUS_OK) return st;
    st = append_route_curve_bucket(out_points, buckets.core_end_horizon);
    if (st != TAIYIN_STATUS_OK) return st;
    st = append_route_curve_bucket(out_points, buckets.half_magnitude_north);
    if (st != TAIYIN_STATUS_OK) return st;
    st = append_route_curve_bucket(out_points, buckets.half_magnitude_south);
    if (st != TAIYIN_STATUS_OK) return st;
    st = append_route_curve_bucket(out_points, buckets.half_magnitude_sunrise_a);
    if (st != TAIYIN_STATUS_OK) return st;
    st = append_route_curve_bucket(out_points, buckets.half_magnitude_sunrise_b);
    if (st != TAIYIN_STATUS_OK) return st;
    st = append_route_curve_bucket(out_points, buckets.half_magnitude_sunset_a);
    if (st != TAIYIN_STATUS_OK) return st;
    return append_route_curve_bucket(out_points, buckets.half_magnitude_sunset_b);
}

Status append_route_limit_endpoint(
    const NativeCalcContext* context,
    SplitJulianDate sample_jd_tt,
    SplitJulianDate endpoint_jd_tt,
    const solar_route_geometry::LimitSample& endpoint,
    uint32_t curve_kind,
    std::vector<SolarEclipseRouteCurvePoint>* out_points,
    EphemerisEvalDiagnostic* diagnostic
) noexcept {
    if (!context || !out_points) return TAIYIN_ERROR_INVALID_ARGUMENT;
    if (!endpoint.endpoint_valid) return TAIYIN_STATUS_OK;

    SolarEclipseRouteCurvePoint point{};
    point.jd_tt = std::isfinite(endpoint.endpoint_time_offset_days)
        ? sample_jd_tt + endpoint.endpoint_time_offset_days
        : endpoint_jd_tt;
    Status st = eclipse_tt_to_ut(*context, point.jd_tt, &point.jd_ut, nullptr, diagnostic);
    if (st != TAIYIN_STATUS_OK) return st;
    point.curve_kind = curve_kind;
    point.latitude_deg = endpoint.endpoint_latitude_rad * 180.0 / TAIYIN_PI;
    point.longitude_deg = normalize_degrees(
        endpoint.endpoint_longitude_rad * 180.0 / TAIYIN_PI + 180.0) - 180.0;
    try {
        out_points->push_back(point);
    } catch (const std::bad_alloc&) {
        return TAIYIN_ERROR_OUT_OF_MEMORY;
    }
    return TAIYIN_STATUS_OK;
}

Status append_route_limit_point(
    const NativeCalcContext* context,
    SplitJulianDate jd_tt,
    const solar_route_geometry::LimitSample& boundary,
    uint32_t curve_kind,
    std::vector<SolarEclipseRouteCurvePoint>* out_points,
    EphemerisEvalDiagnostic* diagnostic
) noexcept {
    if (!context || !out_points) return TAIYIN_ERROR_INVALID_ARGUMENT;
    if (!boundary.valid) return TAIYIN_STATUS_OK;

    SolarEclipseRouteCurvePoint point{};
    point.jd_tt = jd_tt;
    Status st = eclipse_tt_to_ut(*context, point.jd_tt, &point.jd_ut, nullptr, diagnostic);
    if (st != TAIYIN_STATUS_OK) return st;
    point.curve_kind = curve_kind;
    point.latitude_deg = boundary.latitude_rad * 180.0 / TAIYIN_PI;
    point.longitude_deg = normalize_degrees(
        boundary.longitude_rad * 180.0 / TAIYIN_PI + 180.0) - 180.0;
    try {
        out_points->push_back(point);
    } catch (const std::bad_alloc&) {
        return TAIYIN_ERROR_OUT_OF_MEMORY;
    }
    return TAIYIN_STATUS_OK;
}

Status append_route_center_endpoint(
    const NativeCalcContext* context,
    SplitJulianDate endpoint_jd_tt,
    bool entering,
    double center_x,
    double center_y,
    double vx_per_day,
    double vy_per_day,
    const solar_route_geometry::Frame& frame,
    double projected_axis_ratio,
    std::vector<SolarEclipseRouteCurvePoint>* out_points,
    EphemerisEvalDiagnostic* diagnostic
) noexcept {
    if (!context || !out_points || !split_julian_date_is_finite(endpoint_jd_tt)) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    const solar_route_geometry::ProjectedIntersection crossing = solar_route_geometry::intersect_projected_ellipse_line(
        center_x, center_y, vx_per_day, vy_per_day, 1.0, projected_axis_ratio);
    const double speed = std::hypot(vx_per_day, vy_per_day);
    if (crossing.count == 0 || !(speed > 1.0e-14)) return TAIYIN_STATUS_OK;

    const double distance = entering
        ? crossing.distance_b
        : crossing.distance_a;
    const double x = entering ? crossing.bx : crossing.ax;
    const double y = entering ? crossing.by : crossing.ay;
    solar_route_geometry::Frame endpoint_frame = frame;
    endpoint_frame.gast_rad -= distance / speed * 2.0 * TAIYIN_PI;
    const solar_route_geometry::SurfacePoint endpoint = solar_route_geometry::fundamental_to_geodetic(
        x, y, 0.0, endpoint_frame, true);
    if (!endpoint.valid) return TAIYIN_STATUS_OK;

    SolarEclipseRouteCurvePoint point{};
    point.jd_tt = endpoint_jd_tt;
    Status st = eclipse_tt_to_ut(*context, point.jd_tt, &point.jd_ut, nullptr, diagnostic);
    if (st != TAIYIN_STATUS_OK) return st;
    point.curve_kind = TAIYIN_SOLAR_ROUTE_CURVE_CENTER_LINE;
    point.latitude_deg = endpoint.latitude_rad * 180.0 / TAIYIN_PI;
    point.longitude_deg = normalize_degrees(
        endpoint.longitude_rad * 180.0 / TAIYIN_PI + 180.0) - 180.0;
    try {
        out_points->push_back(point);
    } catch (const std::bad_alloc&) {
        return TAIYIN_ERROR_OUT_OF_MEMORY;
    }
    return TAIYIN_STATUS_OK;
}

Status append_route_horizon_point(
    const NativeCalcContext* context,
    SplitJulianDate jd_tt,
    const solar_route_geometry::HorizonPoint& horizon,
    uint32_t curve_kind,
    std::vector<SolarEclipseRouteCurvePoint>* out_points,
    EphemerisEvalDiagnostic* diagnostic
) noexcept {
    if (!context || !out_points) return TAIYIN_ERROR_INVALID_ARGUMENT;
    if (!horizon.found) return TAIYIN_STATUS_OK;
    SolarEclipseRouteCurvePoint point{};
    point.jd_tt = jd_tt;
    Status st = eclipse_tt_to_ut(*context, jd_tt, &point.jd_ut, nullptr, diagnostic);
    if (st != TAIYIN_STATUS_OK) return st;
    point.curve_kind = curve_kind;
    point.latitude_deg = horizon.latitude_rad * 180.0 / TAIYIN_PI;
    point.longitude_deg = normalize_degrees(
        horizon.longitude_rad * 180.0 / TAIYIN_PI + 180.0) - 180.0;
    try {
        out_points->push_back(point);
    } catch (const std::bad_alloc&) {
        return TAIYIN_ERROR_OUT_OF_MEMORY;
    }
    return TAIYIN_STATUS_OK;
}

Status append_route_geo_point(
    const NativeCalcContext* context,
    SplitJulianDate jd_tt,
    const solar_route_geometry::SurfacePoint& geo,
    uint32_t curve_kind,
    std::vector<SolarEclipseRouteCurvePoint>* out_points,
    EphemerisEvalDiagnostic* diagnostic
) noexcept {
    if (!context || !out_points) return TAIYIN_ERROR_INVALID_ARGUMENT;
    if (!geo.valid) return TAIYIN_STATUS_OK;
    SolarEclipseRouteCurvePoint point{};
    point.jd_tt = jd_tt;
    Status st = eclipse_tt_to_ut(*context, jd_tt, &point.jd_ut, nullptr, diagnostic);
    if (st != TAIYIN_STATUS_OK) return st;
    point.curve_kind = curve_kind;
    point.latitude_deg = geo.latitude_rad * 180.0 / TAIYIN_PI;
    point.longitude_deg = normalize_degrees(
        geo.longitude_rad * 180.0 / TAIYIN_PI + 180.0) - 180.0;
    try {
        out_points->push_back(point);
    } catch (const std::bad_alloc&) {
        return TAIYIN_ERROR_OUT_OF_MEMORY;
    }
    return TAIYIN_STATUS_OK;
}

double route_curve_point_distance_squared(
    const SolarEclipseRouteCurvePoint& a,
    const SolarEclipseRouteCurvePoint& b
) noexcept;

Status append_core_horizon_curve_tt(
    const NativeCalcContext* context,
    const SolarBesselianPolynomial& polynomial,
    SplitJulianDate contact_jd_tt,
    uint32_t curve_kind,
    size_t route_sample_count,
    const SolarEclipseRouteCurvePoint& desired_start,
    const SolarEclipseRouteCurvePoint& desired_end,
    std::vector<SolarEclipseRouteCurvePoint>* out_points,
    EphemerisEvalDiagnostic* diagnostic
) noexcept {
    if (!context || !out_points || !split_julian_date_is_finite(contact_jd_tt)
        || !valid_solar_route_sample_count(route_sample_count)) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    const double probe_step_days = 0.04;
    const double t_hours = (contact_jd_tt - polynomial.t0_jd_tt) * 24.0;
    SolarBesselianElements elements;
    SolarBesselianElements before;
    SolarBesselianElements after;
    Status st = evaluate_solar_besselian_polynomial(&polynomial, t_hours, &elements);
    if (st != TAIYIN_STATUS_OK) return st;
    st = evaluate_solar_besselian_polynomial(
        &polynomial, t_hours - probe_step_days * 24.0, &before);
    if (st != TAIYIN_STATUS_OK) return st;
    st = evaluate_solar_besselian_polynomial(
        &polynomial, t_hours + probe_step_days * 24.0, &after);
    if (st != TAIYIN_STATUS_OK) return st;
    const double vx_per_day = ((-after.x) - (-before.x)) / (2.0 * probe_step_days);
    const double vy_per_day = (after.y - before.y) / (2.0 * probe_step_days);
    const double speed_per_day = std::hypot(vx_per_day, vy_per_day);
    if (!(speed_per_day > 1.0e-14)) return TAIYIN_EPHEMERIS_ERROR_EVAL_FAILED;

    const double core_radius = std::fabs(elements.l2);
    const double half_span_days = std::max(
        10.0 / 1440.0,
        std::min(60.0 / 1440.0, 4.0 * core_radius / speed_per_day));
    const double coarse_step_days = 2.0 / 86400.0;
    const SplitJulianDate start_jd_tt = contact_jd_tt - half_span_days;
    const SplitJulianDate end_jd_tt = contact_jd_tt + half_span_days;

    auto sample_horizon = [&](
        SplitJulianDate sample_jd_tt,
        std::vector<SolarEclipseRouteCurvePoint>* points,
        int root_filter = -1
    ) -> Status {
        const double sample_t_hours = (sample_jd_tt - polynomial.t0_jd_tt) * 24.0;
        Status sample_status = evaluate_solar_besselian_polynomial(
            &polynomial, sample_t_hours, &elements);
        if (sample_status != TAIYIN_STATUS_OK) return sample_status;
        sample_status = evaluate_solar_besselian_polynomial(
            &polynomial, sample_t_hours - probe_step_days * 24.0, &before);
        if (sample_status != TAIYIN_STATUS_OK) return sample_status;
        sample_status = evaluate_solar_besselian_polynomial(
            &polynomial, sample_t_hours + probe_step_days * 24.0, &after);
        if (sample_status != TAIYIN_STATUS_OK) return sample_status;
        const double sample_vx_per_day = ((-after.x) - (-before.x)) / (2.0 * probe_step_days);
        const double sample_vy_per_day = (after.y - before.y) / (2.0 * probe_step_days);
        solar_route_geometry::Frame frame;
        sample_status = route_frame_from_besselian_elements(
            context, sample_jd_tt, elements, &frame, diagnostic);
        if (sample_status != TAIYIN_STATUS_OK) return sample_status;
        const double earth_axis_ratio = TAIYIN_WGS84_B_KM / TAIYIN_WGS84_A_KM;
        const double shadow_declination = frame.pole_rotation_rad - TAIYIN_PI / 2.0;
        const double projected_axis_ratio = earth_axis_ratio * (
            1.0
            + (1.0 - earth_axis_ratio * earth_axis_ratio)
                * std::sin(shadow_declination) * std::sin(shadow_declination) / 2.0);
        const double sample_core_radius = std::fabs(elements.l2);
        for (int root = 0; root < 2; ++root) {
            if (root_filter >= 0 && root != root_filter) continue;
            const solar_route_geometry::HorizonPoint horizon = solar_route_geometry::find_horizon_curve_point(
                -elements.x,
                elements.y,
                sample_vx_per_day,
                sample_vy_per_day,
                root != 0,
                sample_core_radius,
                frame,
                projected_axis_ratio);
            sample_status = append_route_horizon_point(
                context, sample_jd_tt, horizon, curve_kind, points, diagnostic);
            if (sample_status != TAIYIN_STATUS_OK) return sample_status;
        }
        return TAIYIN_STATUS_OK;
    };

    std::vector<SolarEclipseRouteCurvePoint> coarse_points;
    for (SplitJulianDate jd_tt = start_jd_tt; ; jd_tt += coarse_step_days) {
        const SplitJulianDate sample_jd_tt = std::min(jd_tt, end_jd_tt);
        st = sample_horizon(sample_jd_tt, &coarse_points);
        if (st != TAIYIN_STATUS_OK) return st;
        if (sample_jd_tt == end_jd_tt) break;
    }
    if (coarse_points.empty()) return TAIYIN_STATUS_OK;

    std::vector<SolarEclipseRouteCurvePoint> probe_points;
    SplitJulianDate invalid_before_jd_tt = std::max(
        start_jd_tt, coarse_points.front().jd_tt - coarse_step_days);
    SplitJulianDate valid_begin_jd_tt = coarse_points.front().jd_tt;
    for (int iteration = 0; iteration < 32; ++iteration) {
        const SplitJulianDate mid_jd_tt = invalid_before_jd_tt
            + 0.5 * (valid_begin_jd_tt - invalid_before_jd_tt);
        probe_points.clear();
        st = sample_horizon(mid_jd_tt, &probe_points);
        if (st != TAIYIN_STATUS_OK) return st;
        if (probe_points.empty()) {
            invalid_before_jd_tt = mid_jd_tt;
        } else {
            valid_begin_jd_tt = mid_jd_tt;
        }
    }

    SplitJulianDate valid_end_jd_tt = coarse_points.back().jd_tt;
    SplitJulianDate invalid_after_jd_tt = std::min(
        end_jd_tt, coarse_points.back().jd_tt + coarse_step_days);
    for (int iteration = 0; iteration < 32; ++iteration) {
        const SplitJulianDate mid_jd_tt = valid_end_jd_tt
            + 0.5 * (invalid_after_jd_tt - valid_end_jd_tt);
        probe_points.clear();
        st = sample_horizon(mid_jd_tt, &probe_points);
        if (st != TAIYIN_STATUS_OK) return st;
        if (probe_points.empty()) {
            invalid_after_jd_tt = mid_jd_tt;
        } else {
            valid_end_jd_tt = mid_jd_tt;
        }
    }

    std::vector<SolarEclipseRouteCurvePoint> branches[2];
    for (SplitJulianDate jd_tt = valid_begin_jd_tt; ; jd_tt += coarse_step_days) {
        const SplitJulianDate sample_jd_tt = std::min(jd_tt, valid_end_jd_tt);
        for (int root = 0; root < 2; ++root) {
            st = sample_horizon(sample_jd_tt, &branches[root], root);
            if (st != TAIYIN_STATUS_OK) return st;
        }
        if (sample_jd_tt == valid_end_jd_tt) break;
    }

    std::vector<std::vector<SolarEclipseRouteCurvePoint> > candidates;
    auto add_candidate = [&] (
        const std::vector<SolarEclipseRouteCurvePoint>& first,
        bool reverse_first,
        const std::vector<SolarEclipseRouteCurvePoint>* second,
        bool reverse_second
    ) -> Status {
        if (first.empty()) return TAIYIN_STATUS_OK;
        std::vector<SolarEclipseRouteCurvePoint> candidate;
        try {
            candidate.reserve(first.size() + (second ? second->size() : 0));
            if (reverse_first) {
                candidate.insert(candidate.end(), first.rbegin(), first.rend());
            } else {
                candidate.insert(candidate.end(), first.begin(), first.end());
            }
            if (second && !second->empty()) {
                if (reverse_second) {
                    candidate.insert(candidate.end(), second->rbegin(), second->rend());
                } else {
                    candidate.insert(candidate.end(), second->begin(), second->end());
                }
            }
            candidates.push_back(std::move(candidate));
        } catch (const std::bad_alloc&) {
            return TAIYIN_ERROR_OUT_OF_MEMORY;
        }
        return TAIYIN_STATUS_OK;
    };
    for (int root = 0; root < 2; ++root) {
        st = add_candidate(branches[root], false, nullptr, false);
        if (st != TAIYIN_STATUS_OK) return st;
        st = add_candidate(branches[root], true, nullptr, false);
        if (st != TAIYIN_STATUS_OK) return st;
    }
    st = add_candidate(branches[0], false, &branches[1], true);
    if (st != TAIYIN_STATUS_OK) return st;
    st = add_candidate(branches[1], false, &branches[0], true);
    if (st != TAIYIN_STATUS_OK) return st;
    st = add_candidate(branches[0], true, &branches[1], false);
    if (st != TAIYIN_STATUS_OK) return st;
    st = add_candidate(branches[1], true, &branches[0], false);
    if (st != TAIYIN_STATUS_OK) return st;

    double best_cost = INFINITY;
    size_t best_index = 0;
    for (size_t candidate_index = 0; candidate_index < candidates.size(); ++candidate_index) {
        const std::vector<SolarEclipseRouteCurvePoint>& candidate = candidates[candidate_index];
        if (candidate.empty()) continue;
        double length = 0.0;
        double largest_segment = 0.0;
        for (size_t i = 1; i < candidate.size(); ++i) {
            const double segment = std::sqrt(
                route_curve_point_distance_squared(candidate[i - 1], candidate[i]));
            length += segment;
            largest_segment = std::max(largest_segment, segment);
        }
        const double endpoint_cost = std::sqrt(
            route_curve_point_distance_squared(candidate.front(), desired_start))
            + std::sqrt(route_curve_point_distance_squared(candidate.back(), desired_end));
        const double cost = 100.0 * endpoint_cost + length + 10.0 * largest_segment;
        if (cost < best_cost) {
            best_cost = cost;
            best_index = candidate_index;
        }
    }
    if (candidates.empty() || !std::isfinite(best_cost)) return TAIYIN_STATUS_OK;
    out_points->swap(candidates[best_index]);


    // A central-path horizon cap can exist for only a few seconds. The coarse
    // two-second scan then leaves just two or three vertices, even when the
    // rest of the route is densely sampled. Subdivide in map space so this
    // short, high-curvature part follows the same caller-selected resolution.
    const double max_chord_deg = std::max(
        0.01, 20.0 / static_cast<double>(route_sample_count));
    const double max_midpoint_error_deg = std::max(
        0.00025, 0.5 / static_cast<double>(route_sample_count));
    const size_t max_horizon_points = std::max<size_t>(
        32, std::min<size_t>(1024, route_sample_count));

    auto sample_nearest_root = [&] (
        SplitJulianDate sample_jd_tt,
        const SolarEclipseRouteCurvePoint& a,
        const SolarEclipseRouteCurvePoint& b,
        SolarEclipseRouteCurvePoint* out
    ) -> Status {
        if (!out) return TAIYIN_ERROR_INVALID_ARGUMENT;
        std::vector<SolarEclipseRouteCurvePoint> roots;
        const Status sample_status = sample_horizon(sample_jd_tt, &roots);
        if (sample_status != TAIYIN_STATUS_OK) return sample_status;
        if (roots.empty()) return TAIYIN_EVENT_ERROR_NOT_FOUND;
        size_t best_index = 0;
        double best_cost = INFINITY;
        for (size_t i = 0; i < roots.size(); ++i) {
            const double cost = route_curve_point_distance_squared(a, roots[i])
                + route_curve_point_distance_squared(roots[i], b);
            if (cost < best_cost) {
                best_cost = cost;
                best_index = i;
            }
        }
        *out = roots[best_index];
        return TAIYIN_STATUS_OK;
    };

    for (size_t pass = 0; pass < 16 && out_points->size() < max_horizon_points; ++pass) {
        bool inserted = false;
        for (size_t i = 0; i + 1 < out_points->size()
                && out_points->size() < max_horizon_points; ++i) {
            const SolarEclipseRouteCurvePoint a = (*out_points)[i];
            const SolarEclipseRouteCurvePoint b = (*out_points)[i + 1];
            const SplitJulianDate mid_jd_tt = a.jd_tt + 0.5 * (b.jd_tt - a.jd_tt);
            if (mid_jd_tt == a.jd_tt || mid_jd_tt == b.jd_tt) continue;

            SolarEclipseRouteCurvePoint mid;
            const Status sample_status = sample_nearest_root(mid_jd_tt, a, b, &mid);
            if (sample_status == TAIYIN_EVENT_ERROR_NOT_FOUND) continue;
            if (sample_status != TAIYIN_STATUS_OK) return sample_status;

            const double chord_deg = std::sqrt(route_curve_point_distance_squared(a, b));
            const double longitude_midpoint_deg = a.longitude_deg
                + 0.5 * normalize_signed_degrees(b.longitude_deg - a.longitude_deg);
            SolarEclipseRouteCurvePoint chord_midpoint = a;
            chord_midpoint.latitude_deg = 0.5 * (a.latitude_deg + b.latitude_deg);
            chord_midpoint.longitude_deg = normalize_degrees(longitude_midpoint_deg + 180.0) - 180.0;
            const double midpoint_error_deg = std::sqrt(
                route_curve_point_distance_squared(mid, chord_midpoint));
            if (chord_deg <= max_chord_deg
                && midpoint_error_deg <= max_midpoint_error_deg) {
                continue;
            }
            try {
                out_points->insert(
                    out_points->begin() + static_cast<std::ptrdiff_t>(i + 1), mid);
            } catch (const std::bad_alloc&) {
                return TAIYIN_ERROR_OUT_OF_MEMORY;
            }
            inserted = true;
            ++i;
        }
        if (!inserted) break;
    }

    // The horizon and limit solvers describe the same geometric junction but use
    // independent numerical paths. Snap the exported cap endpoints so the
    // polygon is continuous without adding an artificial connector segment.
    out_points->front().latitude_deg = desired_start.latitude_deg;
    out_points->front().longitude_deg = desired_start.longitude_deg;
    out_points->back().latitude_deg = desired_end.latitude_deg;
    out_points->back().longitude_deg = desired_end.longitude_deg;
    return TAIYIN_STATUS_OK;
}

Status append_core_horizon_caps_tt(
    const NativeCalcContext* context,
    const SolarBesselianPolynomial& polynomial,
    SplitJulianDate core_begin_jd_tt,
    SplitJulianDate core_end_jd_tt,
    size_t route_sample_count,
    RouteCurveBuckets* buckets,
    EphemerisEvalDiagnostic* diagnostic
) noexcept {
    if (!context || !buckets || !split_julian_date_is_finite(core_begin_jd_tt)
        || !split_julian_date_is_finite(core_end_jd_tt) || !(core_end_jd_tt > core_begin_jd_tt)
        || !valid_solar_route_sample_count(route_sample_count)) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    if (buckets->core_north.size() < 2 || buckets->core_south.size() < 2) {
        return TAIYIN_STATUS_OK;
    }

    Status status = append_core_horizon_curve_tt(
        context,
        polynomial,
        core_begin_jd_tt,
        TAIYIN_SOLAR_ROUTE_CURVE_CORE_BEGIN_HORIZON,
        route_sample_count,
        buckets->core_south.front(),
        buckets->core_north.front(),
        &buckets->core_begin_horizon,
        diagnostic);
    if (status != TAIYIN_STATUS_OK) return status;
    return append_core_horizon_curve_tt(
        context,
        polynomial,
        core_end_jd_tt,
        TAIYIN_SOLAR_ROUTE_CURVE_CORE_END_HORIZON,
        route_sample_count,
        buckets->core_north.back(),
        buckets->core_south.back(),
        &buckets->core_end_horizon,
        diagnostic);
}

double route_curve_point_distance_squared(
    const SolarEclipseRouteCurvePoint& a,
    const SolarEclipseRouteCurvePoint& b
) noexcept {
    const double mean_latitude_rad = 0.5 * (a.latitude_deg + b.latitude_deg) * TAIYIN_PI / 180.0;
    const double dx = normalize_signed_degrees(b.longitude_deg - a.longitude_deg)
        * std::cos(mean_latitude_rad);
    const double dy = b.latitude_deg - a.latitude_deg;
    return dx * dx + dy * dy;
}

Status add_route_connector_point(
    std::vector<SolarEclipseRouteCurvePoint>* curve,
    const SolarEclipseRouteCurvePoint& source,
    uint32_t curve_kind,
    bool prepend
) noexcept {
    if (!curve) return TAIYIN_ERROR_INVALID_ARGUMENT;
    SolarEclipseRouteCurvePoint point = source;
    point.curve_kind = curve_kind;
    try {
        if (prepend) {
            curve->insert(curve->begin(), point);
        } else {
            curve->push_back(point);
        }
    } catch (const std::bad_alloc&) {
        return TAIYIN_ERROR_OUT_OF_MEMORY;
    }
    return TAIYIN_STATUS_OK;
}

Status stitch_wide_route_boundary(
    const std::vector<SolarEclipseRouteCurvePoint>& north_limit,
    const std::vector<SolarEclipseRouteCurvePoint>& south_limit,
    uint32_t sunrise_a_kind,
    uint32_t sunrise_b_kind,
    uint32_t sunset_a_kind,
    uint32_t sunset_b_kind,
    std::vector<SolarEclipseRouteCurvePoint>* sunrise_a,
    std::vector<SolarEclipseRouteCurvePoint>* sunrise_b,
    std::vector<SolarEclipseRouteCurvePoint>* sunset_a,
    std::vector<SolarEclipseRouteCurvePoint>* sunset_b
) noexcept {
    if (!sunrise_a || !sunrise_b || !sunset_a || !sunset_b) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    Status st = TAIYIN_STATUS_OK;
    if (!north_limit.empty()) {
        st = add_route_connector_point(
            sunrise_a, north_limit.front(), sunrise_a_kind, false);
        if (st != TAIYIN_STATUS_OK) return st;
        st = add_route_connector_point(
            sunset_a, north_limit.back(), sunset_a_kind, true);
        if (st != TAIYIN_STATUS_OK) return st;
    } else if (sunset_a->empty() && !sunrise_a->empty()) {
        st = add_route_connector_point(
            sunset_a, sunrise_a->back(), sunset_a_kind, false);
        if (st != TAIYIN_STATUS_OK) return st;
    }
    if (!south_limit.empty()) {
        st = add_route_connector_point(
            sunrise_b, south_limit.front(), sunrise_b_kind, false);
        if (st != TAIYIN_STATUS_OK) return st;
        st = add_route_connector_point(
            sunset_b, south_limit.back(), sunset_b_kind, true);
        if (st != TAIYIN_STATUS_OK) return st;
    } else if (sunset_b->empty() && !sunrise_b->empty()) {
        st = add_route_connector_point(
            sunset_b, sunrise_b->back(), sunset_b_kind, false);
        if (st != TAIYIN_STATUS_OK) return st;
    }

    if (sunrise_a->empty() && !sunrise_b->empty()) {
        st = add_route_connector_point(
            sunrise_a, sunrise_b->front(), sunrise_a_kind, false);
        if (st != TAIYIN_STATUS_OK) return st;
    } else if (sunrise_b->empty() && !sunrise_a->empty()) {
        st = add_route_connector_point(
            sunrise_b, sunrise_a->front(), sunrise_b_kind, false);
        if (st != TAIYIN_STATUS_OK) return st;
    } else if (!sunrise_a->empty() && !sunrise_b->empty()) {
        SolarEclipseRouteCurvePoint common = sunrise_b->front();
        common.curve_kind = sunrise_a_kind;
        if (sunrise_a->size() == 1) {
            st = add_route_connector_point(
                sunrise_a, common, sunrise_a_kind, true);
            if (st != TAIYIN_STATUS_OK) return st;
        } else {
            sunrise_a->front() = common;
        }
    }

    if (sunset_a->empty() && !sunset_b->empty()) {
        st = add_route_connector_point(
            sunset_a, sunset_b->back(), sunset_a_kind, false);
        if (st != TAIYIN_STATUS_OK) return st;
    } else if (sunset_b->empty() && !sunset_a->empty()) {
        st = add_route_connector_point(
            sunset_b, sunset_a->back(), sunset_b_kind, false);
        if (st != TAIYIN_STATUS_OK) return st;
    } else if (!sunset_a->empty() && !sunset_b->empty()) {
        st = add_route_connector_point(
            sunset_b, sunset_a->back(), sunset_b_kind, false);
        if (st != TAIYIN_STATUS_OK) return st;
    }
    return TAIYIN_STATUS_OK;
}

Status append_solar_eclipse_route_curve_span_tt(
    const NativeCalcContext* context,
    const SolarBesselianPolynomial& polynomial,
    SplitJulianDate start_jd_tt,
    SplitJulianDate end_jd_tt,
    uint64_t flags,
    uint32_t layer_mask,
    size_t route_sample_count,
    SplitJulianDate core_contact_start_jd_tt,
    SplitJulianDate core_contact_end_jd_tt,
    RouteCurveBuckets* buckets,
    EphemerisEvalDiagnostic* diagnostic
) noexcept {
    if (!context || !buckets || !split_julian_date_is_finite(start_jd_tt) || !split_julian_date_is_finite(end_jd_tt)
        || !(end_jd_tt > start_jd_tt)
        || !valid_solar_route_sample_count(route_sample_count)) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }

    Status st = TAIYIN_STATUS_OK;
    solar_route_geometry::LimitState raw_north_state{};
    solar_route_geometry::LimitState raw_south_state{};
    solar_route_geometry::LimitState penumbral_north_state{};
    solar_route_geometry::LimitState penumbral_south_state{};
    solar_route_geometry::LimitState half_magnitude_north_state{};
    solar_route_geometry::LimitState half_magnitude_south_state{};
    bool center_was_valid = false;
    int partial_overlap_phase = 0;
    std::vector<SolarEclipseRouteCurvePoint>* max_a = &buckets->sunrise_max_a;
    std::vector<SolarEclipseRouteCurvePoint>* max_b = &buckets->sunrise_max_b;
    std::vector<SolarEclipseRouteCurvePoint>* half_max_a =
        &buckets->half_magnitude_sunrise_a;
    std::vector<SolarEclipseRouteCurvePoint>* half_max_b =
        &buckets->half_magnitude_sunrise_b;
    const double base_span_days = end_jd_tt - start_jd_tt;
    const double step_days = base_span_days / static_cast<double>(route_sample_count);
    const bool core_only_sampling =
        (layer_mask & (kRouteCurveLayerCenter | kRouteCurveLayerCore)) != 0u
        && (layer_mask & (kRouteCurveLayerPenumbral | kRouteCurveLayerHalfMagnitude)) == 0u;
    const double visibility_padding_days = (layer_mask & kRouteCurveLayerPenumbral) != 0u
        ? 10.0 * step_days
        : (core_only_sampling ? 10.0 * step_days : 0.0);
    const SplitJulianDate scan_start_jd_tt = start_jd_tt - visibility_padding_days;
    const SplitJulianDate scan_end_jd_tt = end_jd_tt + visibility_padding_days;
    const bool endpoint_dense_sampling = core_only_sampling;
    const int scan_steps = std::max(
        1,
        static_cast<int>(std::ceil((scan_end_jd_tt - scan_start_jd_tt) / step_days)));
    for (int sample_index = 0; sample_index <= scan_steps; ++sample_index) {
        SplitJulianDate sample_jd = scan_end_jd_tt;
        if (sample_index != scan_steps) {
            if (endpoint_dense_sampling) {
                const double phase = TAIYIN_PI * static_cast<double>(sample_index)
                    / static_cast<double>(scan_steps);
                const double fraction = 0.5 - 0.5 * std::cos(phase);
                sample_jd = scan_start_jd_tt
                    + (scan_end_jd_tt - scan_start_jd_tt) * fraction;
            } else {
                sample_jd = scan_start_jd_tt + static_cast<double>(sample_index) * step_days;
            }
        }
        const bool in_requested_span = sample_jd >= start_jd_tt && sample_jd <= end_jd_tt;
        const double t_hours = (sample_jd - polynomial.t0_jd_tt) * 24.0;
        SolarBesselianElements elements;
        st = evaluate_solar_besselian_polynomial(&polynomial, t_hours, &elements);
        if (st != TAIYIN_STATUS_OK) return st;

        const double velocity_step_hours = 0.04 * 24.0;
        SolarBesselianElements before;
        SolarBesselianElements after;
        st = evaluate_solar_besselian_polynomial(&polynomial, t_hours - velocity_step_hours, &before);
        if (st != TAIYIN_STATUS_OK) return st;
        st = evaluate_solar_besselian_polynomial(&polynomial, t_hours + velocity_step_hours, &after);
        if (st != TAIYIN_STATUS_OK) return st;

        const double vx_per_day = ((-after.x) - (-before.x)) / (2.0 * 0.04);
        const double vy_per_day = (after.y - before.y) / (2.0 * 0.04);
        if ((layer_mask & (
                kRouteCurveLayerCenter | kRouteCurveLayerCore
                | kRouteCurveLayerPenumbral | kRouteCurveLayerHalfMagnitude)) != 0u) {
            solar_route_geometry::Frame frame;
            st = route_frame_from_besselian_elements(
                context, sample_jd, elements, &frame, diagnostic);
            if (st != TAIYIN_STATUS_OK) return st;
            const double earth_axis_ratio = TAIYIN_WGS84_B_KM / TAIYIN_WGS84_A_KM;
            const double shadow_declination = frame.pole_rotation_rad - TAIYIN_PI / 2.0;
            const double projected_axis_ratio = earth_axis_ratio * (
                1.0
                + (1.0 - earth_axis_ratio * earth_axis_ratio)
                    * std::sin(shadow_declination) * std::sin(shadow_declination) / 2.0);
            RouteLunarLimbGeometry limb_geometry;
            if ((flags & TAIYIN_ECLIPSE_LUNAR_LIMB_CORRECTION) != 0u) {
                st = prepare_route_lunar_limb_geometry(
                    context, sample_jd, flags, elements, &limb_geometry, diagnostic);
                if (st != TAIYIN_STATUS_OK) return st;
            }
            if ((layer_mask & kRouteCurveLayerPenumbral) != 0u) {
                const solar_route_geometry::ProjectedIntersection overlap = solar_route_geometry::intersect_projected_ellipse_circle(
                    1.0,
                    projected_axis_ratio,
                    elements.l1,
                    -elements.x,
                    elements.y);
                if ((partial_overlap_phase & 1) != 0) {
                    if (overlap.count == 0) ++partial_overlap_phase;
                } else if (overlap.count != 0) {
                    ++partial_overlap_phase;
                }
                if (overlap.count != 0) {
                    const solar_route_geometry::SurfacePoint point_a = solar_route_geometry::fundamental_to_geodetic(
                        overlap.ax, overlap.ay, 0.0, frame, true);
                    const solar_route_geometry::SurfacePoint point_b = solar_route_geometry::fundamental_to_geodetic(
                        overlap.bx, overlap.by, 0.0, frame, true);
                    if (partial_overlap_phase == 1) {
                        st = append_route_geo_point(
                            context,
                            sample_jd,
                            point_a,
                            TAIYIN_SOLAR_ROUTE_CURVE_PARTIAL_BEGIN_A,
                            &buckets->partial_begin_a,
                            diagnostic);
                        if (st != TAIYIN_STATUS_OK) return st;
                        st = append_route_geo_point(
                            context,
                            sample_jd,
                            point_b,
                            TAIYIN_SOLAR_ROUTE_CURVE_PARTIAL_BEGIN_B,
                            &buckets->partial_begin_b,
                            diagnostic);
                        if (st != TAIYIN_STATUS_OK) return st;
                    } else if (partial_overlap_phase == 3) {
                        st = append_route_geo_point(
                            context,
                            sample_jd,
                            point_a,
                            TAIYIN_SOLAR_ROUTE_CURVE_PARTIAL_END_A,
                            &buckets->partial_end_a,
                            diagnostic);
                        if (st != TAIYIN_STATUS_OK) return st;
                        st = append_route_geo_point(
                            context,
                            sample_jd,
                            point_b,
                            TAIYIN_SOLAR_ROUTE_CURVE_PARTIAL_END_B,
                            &buckets->partial_end_b,
                            diagnostic);
                        if (st != TAIYIN_STATUS_OK) return st;
                    }
                }

                solar_route_geometry::HorizonPoint horizon = solar_route_geometry::find_horizon_curve_point(
                    -elements.x,
                    elements.y,
                    vx_per_day,
                    vy_per_day,
                    false,
                    elements.l1,
                    frame,
                    projected_axis_ratio);
                if (!horizon.found) {
                    if (!max_a->empty()) max_a = &buckets->sunset_max_a;
                } else {
                    const uint32_t curve_kind = max_a == &buckets->sunrise_max_a
                        ? TAIYIN_SOLAR_ROUTE_CURVE_SUNRISE_MAX_A
                        : TAIYIN_SOLAR_ROUTE_CURVE_SUNSET_MAX_A;
                    st = append_route_horizon_point(
                        context, sample_jd, horizon, curve_kind, max_a, diagnostic);
                    if (st != TAIYIN_STATUS_OK) return st;
                }

                horizon = solar_route_geometry::find_horizon_curve_point(
                    -elements.x,
                    elements.y,
                    vx_per_day,
                    vy_per_day,
                    true,
                    elements.l1,
                    frame,
                    projected_axis_ratio);
                if (!horizon.found) {
                    if (!max_b->empty()) max_b = &buckets->sunset_max_b;
                } else {
                    const uint32_t curve_kind = max_b == &buckets->sunrise_max_b
                        ? TAIYIN_SOLAR_ROUTE_CURVE_SUNRISE_MAX_B
                        : TAIYIN_SOLAR_ROUTE_CURVE_SUNSET_MAX_B;
                    st = append_route_horizon_point(
                        context, sample_jd, horizon, curve_kind, max_b, diagnostic);
                    if (st != TAIYIN_STATUS_OK) return st;
                }
                if (sample_jd > polynomial.t0_jd_tt) {
                    if (max_a == &buckets->sunrise_max_a && max_a->empty()) {
                        max_a = &buckets->sunset_max_a;
                    }
                    if (max_b == &buckets->sunrise_max_b && max_b->empty()) {
                        max_b = &buckets->sunset_max_b;
                    }
                }

                solar_route_geometry::LimitSample north_endpoint = solar_route_geometry::sample_shadow_limit(
                    -elements.x,
                    elements.y,
                    elements.zeta,
                    vx_per_day,
                    vy_per_day,
                    +1,
                    elements.l1,
                    frame,
                    route_moon_radius_ratio(*context),
                    earth_axis_ratio,
                    projected_axis_ratio,
                    &penumbral_north_state);
                solar_route_geometry::LimitSample south_endpoint = solar_route_geometry::sample_shadow_limit(
                    -elements.x,
                    elements.y,
                    elements.zeta,
                    vx_per_day,
                    vy_per_day,
                    -1,
                    elements.l1,
                    frame,
                    route_moon_radius_ratio(*context),
                    earth_axis_ratio,
                    projected_axis_ratio,
                    &penumbral_south_state);
                st = apply_profiled_route_curve_point(
                    -elements.x,
                    elements.y,
                    elements.zeta,
                    vx_per_day,
                    vy_per_day,
                    +1,
                    elements.l1,
                    ROUTE_LIMB_CONTOUR_OUTER,
                    false,
                    frame,
                    earth_axis_ratio,
                    limb_geometry,
                    &north_endpoint);
                if (st != TAIYIN_STATUS_OK) return st;
                st = apply_profiled_route_curve_point(
                    -elements.x,
                    elements.y,
                    elements.zeta,
                    vx_per_day,
                    vy_per_day,
                    -1,
                    elements.l1,
                    ROUTE_LIMB_CONTOUR_OUTER,
                    false,
                    frame,
                    earth_axis_ratio,
                    limb_geometry,
                    &south_endpoint);
                if (st != TAIYIN_STATUS_OK) return st;
                st = append_route_limit_endpoint(
                    context,
                    sample_jd,
                    sample_jd,
                    north_endpoint,
                    TAIYIN_SOLAR_ROUTE_CURVE_PENUMBRAL_NORTH,
                    &buckets->penumbral_north,
                    diagnostic);
                if (st != TAIYIN_STATUS_OK) return st;
                st = append_route_limit_point(
                    context,
                    sample_jd,
                    north_endpoint,
                    TAIYIN_SOLAR_ROUTE_CURVE_PENUMBRAL_NORTH,
                    &buckets->penumbral_north,
                    diagnostic);
                if (st != TAIYIN_STATUS_OK) return st;
                st = append_route_limit_endpoint(
                    context,
                    sample_jd,
                    sample_jd,
                    south_endpoint,
                    TAIYIN_SOLAR_ROUTE_CURVE_PENUMBRAL_SOUTH,
                    &buckets->penumbral_south,
                    diagnostic);
                if (st != TAIYIN_STATUS_OK) return st;
                st = append_route_limit_point(
                    context,
                    sample_jd,
                    south_endpoint,
                    TAIYIN_SOLAR_ROUTE_CURVE_PENUMBRAL_SOUTH,
                    &buckets->penumbral_south,
                    diagnostic);
                if (st != TAIYIN_STATUS_OK) return st;
            }
            if ((layer_mask & kRouteCurveLayerHalfMagnitude) != 0u) {
                const double half_magnitude_radius = 0.5 * (elements.l1 + elements.l2);
                solar_route_geometry::HorizonPoint horizon = solar_route_geometry::find_horizon_curve_point(
                    -elements.x,
                    elements.y,
                    vx_per_day,
                    vy_per_day,
                    false,
                    half_magnitude_radius,
                    frame,
                    projected_axis_ratio);
                if (!horizon.found) {
                    if (!half_max_a->empty()) {
                        half_max_a = &buckets->half_magnitude_sunset_a;
                    }
                } else {
                    const uint32_t curve_kind =
                        half_max_a == &buckets->half_magnitude_sunrise_a
                            ? TAIYIN_SOLAR_ROUTE_CURVE_HALF_MAGNITUDE_SUNRISE_A
                            : TAIYIN_SOLAR_ROUTE_CURVE_HALF_MAGNITUDE_SUNSET_A;
                    st = append_route_horizon_point(
                        context, sample_jd, horizon, curve_kind, half_max_a, diagnostic);
                    if (st != TAIYIN_STATUS_OK) return st;
                }
                horizon = solar_route_geometry::find_horizon_curve_point(
                    -elements.x,
                    elements.y,
                    vx_per_day,
                    vy_per_day,
                    true,
                    half_magnitude_radius,
                    frame,
                    projected_axis_ratio);
                if (!horizon.found) {
                    if (!half_max_b->empty()) {
                        half_max_b = &buckets->half_magnitude_sunset_b;
                    }
                } else {
                    const uint32_t curve_kind =
                        half_max_b == &buckets->half_magnitude_sunrise_b
                            ? TAIYIN_SOLAR_ROUTE_CURVE_HALF_MAGNITUDE_SUNRISE_B
                            : TAIYIN_SOLAR_ROUTE_CURVE_HALF_MAGNITUDE_SUNSET_B;
                    st = append_route_horizon_point(
                        context, sample_jd, horizon, curve_kind, half_max_b, diagnostic);
                    if (st != TAIYIN_STATUS_OK) return st;
                }
                if (sample_jd > polynomial.t0_jd_tt) {
                    if (half_max_a == &buckets->half_magnitude_sunrise_a
                        && half_max_a->empty()) {
                        half_max_a = &buckets->half_magnitude_sunset_a;
                    }
                    if (half_max_b == &buckets->half_magnitude_sunrise_b
                        && half_max_b->empty()) {
                        half_max_b = &buckets->half_magnitude_sunset_b;
                    }
                }
                solar_route_geometry::LimitSample north_endpoint = solar_route_geometry::sample_shadow_limit(
                    -elements.x,
                    elements.y,
                    elements.zeta,
                    vx_per_day,
                    vy_per_day,
                    +1,
                    half_magnitude_radius,
                    frame,
                    route_moon_radius_ratio(*context),
                    earth_axis_ratio,
                    projected_axis_ratio,
                    &half_magnitude_north_state);
                solar_route_geometry::LimitSample south_endpoint = solar_route_geometry::sample_shadow_limit(
                    -elements.x,
                    elements.y,
                    elements.zeta,
                    vx_per_day,
                    vy_per_day,
                    -1,
                    half_magnitude_radius,
                    frame,
                    route_moon_radius_ratio(*context),
                    earth_axis_ratio,
                    projected_axis_ratio,
                    &half_magnitude_south_state);
                st = apply_profiled_route_curve_point(
                    -elements.x,
                    elements.y,
                    elements.zeta,
                    vx_per_day,
                    vy_per_day,
                    +1,
                    half_magnitude_radius,
                    ROUTE_LIMB_CONTOUR_HALF_MAGNITUDE,
                    false,
                    frame,
                    earth_axis_ratio,
                    limb_geometry,
                    &north_endpoint);
                if (st != TAIYIN_STATUS_OK) return st;
                st = apply_profiled_route_curve_point(
                    -elements.x,
                    elements.y,
                    elements.zeta,
                    vx_per_day,
                    vy_per_day,
                    -1,
                    half_magnitude_radius,
                    ROUTE_LIMB_CONTOUR_HALF_MAGNITUDE,
                    false,
                    frame,
                    earth_axis_ratio,
                    limb_geometry,
                    &south_endpoint);
                if (st != TAIYIN_STATUS_OK) return st;
                st = append_route_limit_endpoint(
                    context,
                    sample_jd,
                    sample_jd,
                    north_endpoint,
                    TAIYIN_SOLAR_ROUTE_CURVE_HALF_MAGNITUDE_NORTH,
                    &buckets->half_magnitude_north,
                    diagnostic);
                if (st != TAIYIN_STATUS_OK) return st;
                st = append_route_limit_point(
                    context,
                    sample_jd,
                    north_endpoint,
                    TAIYIN_SOLAR_ROUTE_CURVE_HALF_MAGNITUDE_NORTH,
                    &buckets->half_magnitude_north,
                    diagnostic);
                if (st != TAIYIN_STATUS_OK) return st;
                st = append_route_limit_endpoint(
                    context,
                    sample_jd,
                    sample_jd,
                    south_endpoint,
                    TAIYIN_SOLAR_ROUTE_CURVE_HALF_MAGNITUDE_SOUTH,
                    &buckets->half_magnitude_south,
                    diagnostic);
                if (st != TAIYIN_STATUS_OK) return st;
                st = append_route_limit_point(
                    context,
                    sample_jd,
                    south_endpoint,
                    TAIYIN_SOLAR_ROUTE_CURVE_HALF_MAGNITUDE_SOUTH,
                    &buckets->half_magnitude_south,
                    diagnostic);
                if (st != TAIYIN_STATUS_OK) return st;
            }
            if ((layer_mask & kRouteCurveLayerCenter) != 0u) {
                const solar_route_geometry::SurfacePoint center = solar_route_geometry::shadow_axis_to_geodetic(
                    -elements.x, elements.y, frame, true);
                if (center.valid != center_was_valid) {
                    st = append_route_center_endpoint(
                        context,
                        center.valid ? core_contact_start_jd_tt : core_contact_end_jd_tt,
                        center.valid,
                        -elements.x,
                        elements.y,
                        vx_per_day,
                        vy_per_day,
                        frame,
                        projected_axis_ratio,
                        &buckets->center_line,
                        diagnostic);
                    if (st != TAIYIN_STATUS_OK) return st;
                    center_was_valid = center.valid;
                }
                if (in_requested_span && center.valid) {
                    st = append_route_geo_point(
                        context,
                        sample_jd,
                        center,
                        TAIYIN_SOLAR_ROUTE_CURVE_CENTER_LINE,
                        &buckets->center_line,
                        diagnostic);
                    if (st != TAIYIN_STATUS_OK) return st;
                }
            }
            if ((layer_mask & kRouteCurveLayerCore) != 0u) {
                solar_route_geometry::LimitSample north_endpoint = solar_route_geometry::sample_shadow_limit(
                    -elements.x,
                    elements.y,
                    elements.zeta,
                    vx_per_day,
                    vy_per_day,
                    +1,
                    elements.l2,
                    frame,
                    route_moon_radius_ratio(*context),
                    earth_axis_ratio,
                    projected_axis_ratio,
                    &raw_north_state);
                solar_route_geometry::LimitSample south_endpoint = solar_route_geometry::sample_shadow_limit(
                    -elements.x,
                    elements.y,
                    elements.zeta,
                    vx_per_day,
                    vy_per_day,
                    -1,
                    elements.l2,
                    frame,
                    route_moon_radius_ratio(*context),
                    earth_axis_ratio,
                    projected_axis_ratio,
                    &raw_south_state);
                const solar_route_geometry::SurfacePoint core_center = solar_route_geometry::shadow_axis_to_geodetic(
                    -elements.x, elements.y, frame, true);
                const double core_radius = core_center.valid
                    ? elements.l2
                        + elements.tan_f2
                            * core_center.distance_to_fundamental_plane
                    : elements.l2;
                st = apply_profiled_route_curve_point(
                    -elements.x,
                    elements.y,
                    elements.zeta,
                    vx_per_day,
                    vy_per_day,
                    +1,
                    elements.l2,
                    ROUTE_LIMB_CONTOUR_CORE,
                    core_radius < 0.0,
                    frame,
                    earth_axis_ratio,
                    limb_geometry,
                    &north_endpoint);
                if (st != TAIYIN_STATUS_OK) return st;
                st = apply_profiled_route_curve_point(
                    -elements.x,
                    elements.y,
                    elements.zeta,
                    vx_per_day,
                    vy_per_day,
                    -1,
                    elements.l2,
                    ROUTE_LIMB_CONTOUR_CORE,
                    core_radius < 0.0,
                    frame,
                    earth_axis_ratio,
                    limb_geometry,
                    &south_endpoint);
                if (st != TAIYIN_STATUS_OK) return st;
                if (core_radius < 0.0) {
                    std::swap(north_endpoint, south_endpoint);
                }
                st = append_route_limit_endpoint(
                    context,
                    sample_jd,
                    sample_jd <= polynomial.t0_jd_tt
                        ? core_contact_start_jd_tt
                        : core_contact_end_jd_tt,
                    north_endpoint,
                    TAIYIN_SOLAR_ROUTE_CURVE_CORE_NORTH,
                    &buckets->core_north,
                    diagnostic);
                if (st != TAIYIN_STATUS_OK) return st;
                st = append_route_limit_point(
                    context,
                    sample_jd,
                    north_endpoint,
                    TAIYIN_SOLAR_ROUTE_CURVE_CORE_NORTH,
                    &buckets->core_north,
                    diagnostic);
                if (st != TAIYIN_STATUS_OK) return st;
                st = append_route_limit_endpoint(
                    context,
                    sample_jd,
                    sample_jd <= polynomial.t0_jd_tt
                        ? core_contact_start_jd_tt
                        : core_contact_end_jd_tt,
                    south_endpoint,
                    TAIYIN_SOLAR_ROUTE_CURVE_CORE_SOUTH,
                    &buckets->core_south,
                    diagnostic);
                if (st != TAIYIN_STATUS_OK) return st;
                st = append_route_limit_point(
                    context,
                    sample_jd,
                    south_endpoint,
                    TAIYIN_SOLAR_ROUTE_CURVE_CORE_SOUTH,
                    &buckets->core_south,
                    diagnostic);
                if (st != TAIYIN_STATUS_OK) return st;

            }
        }
    }
    if ((layer_mask & (kRouteCurveLayerCenter | kRouteCurveLayerCore)) != 0u) {
        auto by_epoch = [](const SolarEclipseRouteCurvePoint& a, const SolarEclipseRouteCurvePoint& b) {
            return a.jd_tt < b.jd_tt;
        };
        std::stable_sort(buckets->center_line.begin(), buckets->center_line.end(), by_epoch);
        std::stable_sort(buckets->core_north.begin(), buckets->core_north.end(), by_epoch);
        std::stable_sort(buckets->core_south.begin(), buckets->core_south.end(), by_epoch);
    }
    if ((layer_mask & (kRouteCurveLayerPenumbral | kRouteCurveLayerHalfMagnitude)) != 0u) {
        st = stitch_wide_route_boundary(
            buckets->penumbral_north,
            buckets->penumbral_south,
            TAIYIN_SOLAR_ROUTE_CURVE_SUNRISE_MAX_A,
            TAIYIN_SOLAR_ROUTE_CURVE_SUNRISE_MAX_B,
            TAIYIN_SOLAR_ROUTE_CURVE_SUNSET_MAX_A,
            TAIYIN_SOLAR_ROUTE_CURVE_SUNSET_MAX_B,
            &buckets->sunrise_max_a,
            &buckets->sunrise_max_b,
            &buckets->sunset_max_a,
            &buckets->sunset_max_b);
        if (st != TAIYIN_STATUS_OK) return st;
        st = stitch_wide_route_boundary(
            buckets->half_magnitude_north,
            buckets->half_magnitude_south,
            TAIYIN_SOLAR_ROUTE_CURVE_HALF_MAGNITUDE_SUNRISE_A,
            TAIYIN_SOLAR_ROUTE_CURVE_HALF_MAGNITUDE_SUNRISE_B,
            TAIYIN_SOLAR_ROUTE_CURVE_HALF_MAGNITUDE_SUNSET_A,
            TAIYIN_SOLAR_ROUTE_CURVE_HALF_MAGNITUDE_SUNSET_B,
            &buckets->half_magnitude_sunrise_a,
            &buckets->half_magnitude_sunrise_b,
            &buckets->half_magnitude_sunset_a,
            &buckets->half_magnitude_sunset_b);
        if (st != TAIYIN_STATUS_OK) return st;
    }
    if ((layer_mask & kRouteCurveLayerCore) != 0u
        && !buckets->core_north.empty() && !buckets->core_south.empty()) {
        st = append_core_horizon_caps_tt(
            context,
            polynomial,
            core_contact_start_jd_tt,
            core_contact_end_jd_tt,
            route_sample_count,
            buckets,
            diagnostic);
        if (st != TAIYIN_STATUS_OK) return st;
    }
    return TAIYIN_STATUS_OK;
}

Status compute_solar_eclipse_route_product_curve_points_tt(
    const NativeCalcContext* context,
    SplitJulianDate jd_near_tt,
    uint64_t flags,
    size_t route_sample_count,
    std::vector<SolarEclipseRouteCurvePoint>* out_points,
    EphemerisEvalDiagnostic* diagnostic
) noexcept {
    if (!context || !out_points || !split_julian_date_is_finite(jd_near_tt)
        || !valid_solar_eclipse_route_flags(flags)
        || !valid_solar_route_sample_count(route_sample_count)) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    out_points->clear();

    SolarEclipseResult result;
    Status st = solve_solar_eclipse_at(
        context,
        jd_near_tt,
        flags | TAIYIN_ECLIPSE_INCLUDE_CONTACTS,
        &result,
        diagnostic);
    if (st != TAIYIN_STATUS_OK) return st;
    if (result.kind == TAIYIN_ECLIPSE_NONE) {
        return TAIYIN_STATUS_OK;
    }

    const bool central = (result.kind
        & (TAIYIN_ECLIPSE_TOTAL | TAIYIN_ECLIPSE_ANNULAR | TAIYIN_ECLIPSE_HYBRID)) != 0u;
    SplitJulianDate start = result.contact_jd_tt[TAIYIN_SOLAR_ECLIPSE_CONTACT_P1];
    SplitJulianDate end = result.contact_jd_tt[TAIYIN_SOLAR_ECLIPSE_CONTACT_P4];
    if (!split_julian_date_is_finite(start) || !split_julian_date_is_finite(end)
        || !(end > start)) {
        start = result.maximum_jd_tt - 3.0 / 24.0;
        end = result.maximum_jd_tt + 3.0 / 24.0;
    }
    SplitJulianDate core_start = result.contact_jd_tt[TAIYIN_SOLAR_ECLIPSE_CONTACT_C1];
    SplitJulianDate core_end = result.contact_jd_tt[TAIYIN_SOLAR_ECLIPSE_CONTACT_C4];
    if (!split_julian_date_is_finite(core_start) || !split_julian_date_is_finite(core_end)
        || !(core_end > core_start)) {
        core_start = result.maximum_jd_tt - 3.0 / 24.0;
        core_end = result.maximum_jd_tt + 3.0 / 24.0;
    }
    const double max_offset_hours =
        std::max(std::fabs(start - result.maximum_jd_tt), std::fabs(end - result.maximum_jd_tt)) * 24.0;
    const double polynomial_span_hours = std::max(6.0, 2.0 * max_offset_hours + 2.0);
    SolarBesselianPolynomial polynomial;
    st = compute_solar_besselian_polynomial_tt_with_corrections(
        context,
        result.maximum_jd_tt,
        polynomial_span_hours,
        1.0,
        4,
        flags,
        nullptr,
        &polynomial,
        diagnostic);
    if (st != TAIYIN_STATUS_OK) return st;

    RouteCurveBuckets buckets;
    st = append_solar_eclipse_route_curve_span_tt(
        context,
        polynomial,
        start,
        end,
        flags,
        kRouteCurveLayerPenumbral | kRouteCurveLayerHalfMagnitude,
        route_sample_count,
        invalid_jd(),
        invalid_jd(),
        &buckets,
        diagnostic);
    if (st == TAIYIN_STATUS_OK && central) {
        st = append_solar_eclipse_route_curve_span_tt(
            context,
            polynomial,
            core_start,
            core_end,
            flags,
            kRouteCurveLayerCenter | kRouteCurveLayerCore,
            route_sample_count,
            core_start,
            core_end,
            &buckets,
            diagnostic);
    }
    if (st != TAIYIN_STATUS_OK) {
        out_points->clear();
        return st;
    }
    st = flatten_route_curve_buckets(buckets, out_points);
    if (st != TAIYIN_STATUS_OK) {
        out_points->clear();
        return st;
    }
    return TAIYIN_STATUS_OK;
}

Status compute_solar_eclipse_route_map_curve_points_tt(
    const NativeCalcContext* context,
    SplitJulianDate jd_near_tt,
    uint64_t flags,
    size_t route_sample_count,
    std::vector<SolarEclipseRouteCurvePoint>* out_points,
    EphemerisEvalDiagnostic* diagnostic
) noexcept {
    if (!context || !out_points || !split_julian_date_is_finite(jd_near_tt)
        || !valid_solar_eclipse_route_flags(flags)
        || !valid_solar_route_sample_count(route_sample_count)) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    out_points->clear();

    SolarEclipseResult result;
    Status st = solve_solar_eclipse_at(
        context,
        jd_near_tt,
        flags | TAIYIN_ECLIPSE_INCLUDE_CONTACTS,
        &result,
        diagnostic);
    if (st != TAIYIN_STATUS_OK) return st;
    if (result.kind == TAIYIN_ECLIPSE_NONE) {
        return TAIYIN_STATUS_OK;
    }

    const bool central = (result.kind
        & (TAIYIN_ECLIPSE_TOTAL | TAIYIN_ECLIPSE_ANNULAR | TAIYIN_ECLIPSE_HYBRID)) != 0u;

    SplitJulianDate core_start = result.maximum_jd_tt - 3.0 / 24.0;
    SplitJulianDate core_end = result.maximum_jd_tt + 3.0 / 24.0;
    if (central) {
        const SplitJulianDate c1 = result.contact_jd_tt[TAIYIN_SOLAR_ECLIPSE_CONTACT_C1];
        const SplitJulianDate c4 = result.contact_jd_tt[TAIYIN_SOLAR_ECLIPSE_CONTACT_C4];
        if (split_julian_date_is_finite(c1) && split_julian_date_is_finite(c4) && c4 > c1) {
            core_start = c1;
            core_end = c4;
        }
    }

    SplitJulianDate wide_start = result.contact_jd_tt[TAIYIN_SOLAR_ECLIPSE_CONTACT_P1];
    SplitJulianDate wide_end = result.contact_jd_tt[TAIYIN_SOLAR_ECLIPSE_CONTACT_P4];
    if (!split_julian_date_is_finite(wide_start) || !split_julian_date_is_finite(wide_end)
        || !(wide_end > wide_start)) {
        wide_start = core_start;
        wide_end = core_end;
    }

    const double max_offset_hours =
        std::max(std::fabs(wide_start - result.maximum_jd_tt), std::fabs(wide_end - result.maximum_jd_tt)) * 24.0;
    const double polynomial_span_hours = std::max(6.0, 2.0 * max_offset_hours + 2.0);
    SolarBesselianPolynomial polynomial;
    st = compute_solar_besselian_polynomial_tt_with_corrections(
        context,
        result.maximum_jd_tt,
        polynomial_span_hours,
        1.0,
        4,
        flags,
        nullptr,
        &polynomial,
        diagnostic);
    if (st != TAIYIN_STATUS_OK) return st;

    RouteCurveBuckets buckets;
    st = append_solar_eclipse_route_curve_span_tt(
        context,
        polynomial,
        wide_start,
        wide_end,
        flags,
        kRouteCurveLayerPenumbral | kRouteCurveLayerHalfMagnitude,
        route_sample_count,
        invalid_jd(),
        invalid_jd(),
        &buckets,
        diagnostic);
    if (st == TAIYIN_STATUS_OK && central) {
        st = append_solar_eclipse_route_curve_span_tt(
            context,
            polynomial,
            core_start,
            core_end,
            flags,
            kRouteCurveLayerCenter | kRouteCurveLayerCore,
            route_sample_count,
            core_start,
            core_end,
            &buckets,
            diagnostic);
    }
    if (st != TAIYIN_STATUS_OK) {
        out_points->clear();
        return st;
    }
    st = flatten_route_curve_buckets(buckets, out_points);
    if (st != TAIYIN_STATUS_OK) {
        out_points->clear();
        return st;
    }
    return TAIYIN_STATUS_OK;
}

Status compute_solar_eclipse_route_product_tt(
    const NativeCalcContext* context,
    SplitJulianDate jd_near_tt,
    uint64_t flags,
    SolarEclipseRouteProductPoint* out_core_polygon,
    size_t max_core_polygon_point_count,
    size_t* out_core_polygon_point_count,
    SolarEclipseRouteProductSummary* out_summary,
    EphemerisEvalDiagnostic* diagnostic
) noexcept {
    return compute_solar_eclipse_route_product_tt_with_options(
        context,
        jd_near_tt,
        flags,
        TAIYIN_SOLAR_ROUTE_DEFAULT_SAMPLE_COUNT,
        out_core_polygon,
        max_core_polygon_point_count,
        out_core_polygon_point_count,
        out_summary,
        diagnostic);
}

Status compute_solar_eclipse_route_product_tt_with_options(
    const NativeCalcContext* context,
    SplitJulianDate jd_near_tt,
    uint64_t flags,
    size_t route_sample_count,
    SolarEclipseRouteProductPoint* out_core_polygon,
    size_t max_core_polygon_point_count,
    size_t* out_core_polygon_point_count,
    SolarEclipseRouteProductSummary* out_summary,
    EphemerisEvalDiagnostic* diagnostic
) noexcept {
    if (!context || !out_core_polygon_point_count || !split_julian_date_is_finite(jd_near_tt)
        || !valid_solar_eclipse_route_flags(flags)
        || !valid_solar_route_sample_count(route_sample_count)) {
        if (out_core_polygon_point_count) *out_core_polygon_point_count = 0;
        init_route_product_summary(out_summary);
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }

    *out_core_polygon_point_count = 0;
    init_route_product_summary(out_summary);

    std::vector<SolarEclipseRouteCurvePoint> curve_points;
    Status st = compute_solar_eclipse_route_product_curve_points_tt(
        context,
        jd_near_tt,
        flags,
        route_sample_count,
        &curve_points,
        diagnostic);
    if (st != TAIYIN_STATUS_OK) return st;
    const size_t curve_count = curve_points.size();

    const size_t center_count = count_route_curve_kind(
        curve_points.data(), curve_count, TAIYIN_SOLAR_ROUTE_CURVE_CENTER_LINE);
    const size_t core_north_count = count_route_curve_kind(
        curve_points.data(), curve_count, TAIYIN_SOLAR_ROUTE_CURVE_CORE_NORTH);
    const size_t core_south_count = count_route_curve_kind(
        curve_points.data(), curve_count, TAIYIN_SOLAR_ROUTE_CURVE_CORE_SOUTH);
    const size_t core_begin_horizon_count = count_route_curve_kind(
        curve_points.data(), curve_count, TAIYIN_SOLAR_ROUTE_CURVE_CORE_BEGIN_HORIZON);
    const size_t core_end_horizon_count = count_route_curve_kind(
        curve_points.data(), curve_count, TAIYIN_SOLAR_ROUTE_CURVE_CORE_END_HORIZON);
    const size_t penumbral_north_count = count_route_curve_kind(
        curve_points.data(), curve_count, TAIYIN_SOLAR_ROUTE_CURVE_PENUMBRAL_NORTH);
    const size_t penumbral_south_count = count_route_curve_kind(
        curve_points.data(), curve_count, TAIYIN_SOLAR_ROUTE_CURVE_PENUMBRAL_SOUTH);
    const size_t half_magnitude_north_count = count_route_curve_kind(
        curve_points.data(), curve_count, TAIYIN_SOLAR_ROUTE_CURVE_HALF_MAGNITUDE_NORTH);
    const size_t half_magnitude_south_count = count_route_curve_kind(
        curve_points.data(), curve_count, TAIYIN_SOLAR_ROUTE_CURVE_HALF_MAGNITUDE_SOUTH);
    const size_t required_polygon_count = route_polygon_required_count(
        core_north_count,
        core_south_count,
        core_begin_horizon_count,
        core_end_horizon_count);

    if (out_summary) {
        out_summary->curve_point_count = curve_count;
        out_summary->center_line_count = center_count;
        out_summary->core_north_count = core_north_count;
        out_summary->core_south_count = core_south_count;
        out_summary->core_begin_horizon_count = core_begin_horizon_count;
        out_summary->core_end_horizon_count = core_end_horizon_count;
        out_summary->penumbral_north_count = penumbral_north_count;
        out_summary->penumbral_south_count = penumbral_south_count;
        out_summary->half_magnitude_north_count = half_magnitude_north_count;
        out_summary->half_magnitude_south_count = half_magnitude_south_count;
        out_summary->core_polygon_point_count = required_polygon_count;
        out_summary->polygon_point_count = required_polygon_count;
        if (center_count > 0) out_summary->flags |= TAIYIN_SOLAR_ROUTE_PRODUCT_HAS_CENTER_LINE;
        if (core_north_count > 0 && core_south_count > 0) {
            out_summary->flags |= TAIYIN_SOLAR_ROUTE_PRODUCT_HAS_CORE_LIMITS;
        }
        if (penumbral_north_count > 0 && penumbral_south_count > 0) {
            out_summary->flags |= TAIYIN_SOLAR_ROUTE_PRODUCT_HAS_PENUMBRAL_LIMITS;
        }
        if (half_magnitude_north_count > 0 && half_magnitude_south_count > 0) {
            out_summary->flags |= TAIYIN_SOLAR_ROUTE_PRODUCT_HAS_HALF_MAGNITUDE_LIMITS;
        }
        if (required_polygon_count > 0) out_summary->flags |= TAIYIN_SOLAR_ROUTE_PRODUCT_HAS_CORE_POLYGON;
    }

    return fill_route_polygons(
        curve_points.data(),
        curve_count,
        required_polygon_count > 0,
        false,
        false,
        required_polygon_count,
        out_core_polygon,
        max_core_polygon_point_count,
        out_core_polygon_point_count,
        out_summary);
}

Status compute_solar_eclipse_route_product_ut(
    const NativeCalcContext* context,
    SplitJulianDate jd_near_ut,
    uint64_t flags,
    SolarEclipseRouteProductPoint* out_core_polygon,
    size_t max_core_polygon_point_count,
    size_t* out_core_polygon_point_count,
    SolarEclipseRouteProductSummary* out_summary,
    EphemerisEvalDiagnostic* diagnostic
) noexcept {
    return compute_solar_eclipse_route_product_ut_with_options(
        context,
        jd_near_ut,
        flags,
        TAIYIN_SOLAR_ROUTE_DEFAULT_SAMPLE_COUNT,
        out_core_polygon,
        max_core_polygon_point_count,
        out_core_polygon_point_count,
        out_summary,
        diagnostic);
}

Status compute_solar_eclipse_route_product_ut_with_options(
    const NativeCalcContext* context,
    SplitJulianDate jd_near_ut,
    uint64_t flags,
    size_t route_sample_count,
    SolarEclipseRouteProductPoint* out_core_polygon,
    size_t max_core_polygon_point_count,
    size_t* out_core_polygon_point_count,
    SolarEclipseRouteProductSummary* out_summary,
    EphemerisEvalDiagnostic* diagnostic
) noexcept {
    if (!context || !out_core_polygon_point_count || !split_julian_date_is_finite(jd_near_ut)
        || !valid_solar_eclipse_route_flags(flags)
        || !valid_solar_route_sample_count(route_sample_count)) {
        if (out_core_polygon_point_count) *out_core_polygon_point_count = 0;
        init_route_product_summary(out_summary);
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    SplitJulianDate jd_near_tt;
    const Status st = eclipse_ut_to_tt(*context, jd_near_ut, &jd_near_tt, nullptr, diagnostic);
    if (st != TAIYIN_STATUS_OK) {
        *out_core_polygon_point_count = 0;
        init_route_product_summary(out_summary);
        return st;
    }
    return compute_solar_eclipse_route_product_tt_with_options(
        context,
        jd_near_tt,
        flags,
        route_sample_count,
        out_core_polygon,
        max_core_polygon_point_count,
        out_core_polygon_point_count,
        out_summary,
        diagnostic);
}

Status compute_solar_eclipse_route_map_product_tt(
    const NativeCalcContext* context,
    SplitJulianDate jd_near_tt,
    uint64_t flags,
    SolarEclipseRouteProductPoint* out_polygon_points,
    size_t max_polygon_point_count,
    size_t* out_polygon_point_count,
    SolarEclipseRouteProductSummary* out_summary,
    EphemerisEvalDiagnostic* diagnostic
) noexcept {
    return compute_solar_eclipse_route_map_product_tt_with_options(
        context,
        jd_near_tt,
        flags,
        TAIYIN_SOLAR_ROUTE_DEFAULT_SAMPLE_COUNT,
        out_polygon_points,
        max_polygon_point_count,
        out_polygon_point_count,
        out_summary,
        diagnostic);
}

Status compute_solar_eclipse_route_map_product_tt_with_options(
    const NativeCalcContext* context,
    SplitJulianDate jd_near_tt,
    uint64_t flags,
    size_t route_sample_count,
    SolarEclipseRouteProductPoint* out_polygon_points,
    size_t max_polygon_point_count,
    size_t* out_polygon_point_count,
    SolarEclipseRouteProductSummary* out_summary,
    EphemerisEvalDiagnostic* diagnostic
) noexcept {
    if (!context || !out_polygon_point_count || !split_julian_date_is_finite(jd_near_tt)
        || !valid_solar_eclipse_route_flags(flags)
        || !valid_solar_route_sample_count(route_sample_count)) {
        if (out_polygon_point_count) *out_polygon_point_count = 0;
        init_route_product_summary(out_summary);
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }

    *out_polygon_point_count = 0;
    init_route_product_summary(out_summary);

    std::vector<SolarEclipseRouteCurvePoint> curve_points;
    Status st = compute_solar_eclipse_route_map_curve_points_tt(
        context,
        jd_near_tt,
        flags,
        route_sample_count,
        &curve_points,
        diagnostic);
    if (st != TAIYIN_STATUS_OK) return st;
    const size_t curve_count = curve_points.size();

    const size_t center_count = count_route_curve_kind(
        curve_points.data(), curve_count, TAIYIN_SOLAR_ROUTE_CURVE_CENTER_LINE);
    const size_t core_north_count = count_route_curve_kind(
        curve_points.data(), curve_count, TAIYIN_SOLAR_ROUTE_CURVE_CORE_NORTH);
    const size_t core_south_count = count_route_curve_kind(
        curve_points.data(), curve_count, TAIYIN_SOLAR_ROUTE_CURVE_CORE_SOUTH);
    const size_t core_begin_horizon_count = count_route_curve_kind(
        curve_points.data(), curve_count, TAIYIN_SOLAR_ROUTE_CURVE_CORE_BEGIN_HORIZON);
    const size_t core_end_horizon_count = count_route_curve_kind(
        curve_points.data(), curve_count, TAIYIN_SOLAR_ROUTE_CURVE_CORE_END_HORIZON);
    const size_t penumbral_north_count = count_route_curve_kind(
        curve_points.data(), curve_count, TAIYIN_SOLAR_ROUTE_CURVE_PENUMBRAL_NORTH);
    const size_t penumbral_south_count = count_route_curve_kind(
        curve_points.data(), curve_count, TAIYIN_SOLAR_ROUTE_CURVE_PENUMBRAL_SOUTH);
    const size_t half_magnitude_north_count = count_route_curve_kind(
        curve_points.data(), curve_count, TAIYIN_SOLAR_ROUTE_CURVE_HALF_MAGNITUDE_NORTH);
    const size_t half_magnitude_south_count = count_route_curve_kind(
        curve_points.data(), curve_count, TAIYIN_SOLAR_ROUTE_CURVE_HALF_MAGNITUDE_SOUTH);

    const size_t core_polygon_count = route_polygon_required_count(
        core_north_count,
        core_south_count,
        core_begin_horizon_count,
        core_end_horizon_count);
    auto wide_polygon_count = [&](
        size_t north_count,
        size_t south_count,
        uint32_t sunrise_a_kind,
        uint32_t sunrise_b_kind,
        uint32_t sunset_a_kind,
        uint32_t sunset_b_kind
    ) noexcept {
        const size_t boundary_count = north_count + south_count
            + count_route_curve_kind(curve_points.data(), curve_count, sunrise_a_kind)
            + count_route_curve_kind(curve_points.data(), curve_count, sunrise_b_kind)
            + count_route_curve_kind(curve_points.data(), curve_count, sunset_a_kind)
            + count_route_curve_kind(curve_points.data(), curve_count, sunset_b_kind);
        return boundary_count >= 3 ? boundary_count + 1 : 0;
    };
    const size_t penumbral_polygon_count = wide_polygon_count(
        penumbral_north_count,
        penumbral_south_count,
        TAIYIN_SOLAR_ROUTE_CURVE_SUNRISE_MAX_A,
        TAIYIN_SOLAR_ROUTE_CURVE_SUNRISE_MAX_B,
        TAIYIN_SOLAR_ROUTE_CURVE_SUNSET_MAX_A,
        TAIYIN_SOLAR_ROUTE_CURVE_SUNSET_MAX_B);
    const size_t half_magnitude_polygon_count = wide_polygon_count(
        half_magnitude_north_count,
        half_magnitude_south_count,
        TAIYIN_SOLAR_ROUTE_CURVE_HALF_MAGNITUDE_SUNRISE_A,
        TAIYIN_SOLAR_ROUTE_CURVE_HALF_MAGNITUDE_SUNRISE_B,
        TAIYIN_SOLAR_ROUTE_CURVE_HALF_MAGNITUDE_SUNSET_A,
        TAIYIN_SOLAR_ROUTE_CURVE_HALF_MAGNITUDE_SUNSET_B);
    const size_t required_polygon_count =
        core_polygon_count + penumbral_polygon_count + half_magnitude_polygon_count;

    if (out_summary) {
        out_summary->curve_point_count = curve_count;
        out_summary->center_line_count = center_count;
        out_summary->core_north_count = core_north_count;
        out_summary->core_south_count = core_south_count;
        out_summary->core_begin_horizon_count = core_begin_horizon_count;
        out_summary->core_end_horizon_count = core_end_horizon_count;
        out_summary->penumbral_north_count = penumbral_north_count;
        out_summary->penumbral_south_count = penumbral_south_count;
        out_summary->half_magnitude_north_count = half_magnitude_north_count;
        out_summary->half_magnitude_south_count = half_magnitude_south_count;
        out_summary->core_polygon_point_count = core_polygon_count;
        out_summary->penumbral_polygon_point_count = penumbral_polygon_count;
        out_summary->half_magnitude_polygon_point_count = half_magnitude_polygon_count;
        out_summary->polygon_point_count = required_polygon_count;
        if (center_count > 0) out_summary->flags |= TAIYIN_SOLAR_ROUTE_PRODUCT_HAS_CENTER_LINE;
        if (core_north_count > 0 && core_south_count > 0) {
            out_summary->flags |= TAIYIN_SOLAR_ROUTE_PRODUCT_HAS_CORE_LIMITS;
        }
        if (penumbral_north_count > 0 && penumbral_south_count > 0) {
            out_summary->flags |= TAIYIN_SOLAR_ROUTE_PRODUCT_HAS_PENUMBRAL_LIMITS;
        }
        if (half_magnitude_north_count > 0 && half_magnitude_south_count > 0) {
            out_summary->flags |= TAIYIN_SOLAR_ROUTE_PRODUCT_HAS_HALF_MAGNITUDE_LIMITS;
        }
        if (core_polygon_count > 0) out_summary->flags |= TAIYIN_SOLAR_ROUTE_PRODUCT_HAS_CORE_POLYGON;
        if (penumbral_polygon_count > 0) out_summary->flags |= TAIYIN_SOLAR_ROUTE_PRODUCT_HAS_PENUMBRAL_POLYGON;
        if (half_magnitude_polygon_count > 0) {
            out_summary->flags |= TAIYIN_SOLAR_ROUTE_PRODUCT_HAS_HALF_MAGNITUDE_POLYGON;
        }
    }

    return fill_route_polygons(
        curve_points.data(),
        curve_count,
        core_polygon_count > 0,
        penumbral_polygon_count > 0,
        half_magnitude_polygon_count > 0,
        required_polygon_count,
        out_polygon_points,
        max_polygon_point_count,
        out_polygon_point_count,
        out_summary);
}

Status compute_solar_eclipse_route_map_product_ut(
    const NativeCalcContext* context,
    SplitJulianDate jd_near_ut,
    uint64_t flags,
    SolarEclipseRouteProductPoint* out_polygon_points,
    size_t max_polygon_point_count,
    size_t* out_polygon_point_count,
    SolarEclipseRouteProductSummary* out_summary,
    EphemerisEvalDiagnostic* diagnostic
) noexcept {
    return compute_solar_eclipse_route_map_product_ut_with_options(
        context,
        jd_near_ut,
        flags,
        TAIYIN_SOLAR_ROUTE_DEFAULT_SAMPLE_COUNT,
        out_polygon_points,
        max_polygon_point_count,
        out_polygon_point_count,
        out_summary,
        diagnostic);
}

Status compute_solar_eclipse_route_map_product_ut_with_options(
    const NativeCalcContext* context,
    SplitJulianDate jd_near_ut,
    uint64_t flags,
    size_t route_sample_count,
    SolarEclipseRouteProductPoint* out_polygon_points,
    size_t max_polygon_point_count,
    size_t* out_polygon_point_count,
    SolarEclipseRouteProductSummary* out_summary,
    EphemerisEvalDiagnostic* diagnostic
) noexcept {
    if (!context || !out_polygon_point_count || !split_julian_date_is_finite(jd_near_ut)
        || !valid_solar_eclipse_route_flags(flags)
        || !valid_solar_route_sample_count(route_sample_count)) {
        if (out_polygon_point_count) *out_polygon_point_count = 0;
        init_route_product_summary(out_summary);
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    SplitJulianDate jd_tt;
    const Status st = eclipse_ut_to_tt(*context, jd_near_ut, &jd_tt, nullptr, diagnostic);
    if (st != TAIYIN_STATUS_OK) {
        *out_polygon_point_count = 0;
        init_route_product_summary(out_summary);
        return st;
    }
    return compute_solar_eclipse_route_map_product_tt_with_options(
        context,
        jd_tt,
        flags,
        route_sample_count,
        out_polygon_points,
        max_polygon_point_count,
        out_polygon_point_count,
        out_summary,
        diagnostic);
}

}  // namespace runtime
}  // namespace taiyin
