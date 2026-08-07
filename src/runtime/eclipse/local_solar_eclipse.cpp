#include "taiyin/runtime/eclipse_search.h"

#include "runtime/eclipse/eclipse_time.h"
#include "runtime/eclipse/solar_eclipse_sxwnl.h"
#include "runtime/apparent/fast_apparent.h"

#include "taiyin/body_id.h"
#include "taiyin/dispatch.h"
#include "taiyin/earth_rotation.h"
#include "taiyin/geodetic_constants.h"
#include "taiyin/observer.h"
#include "runtime/core/native_context_checks.h"
#include "taiyin/runtime/native_position.h"
#include "taiyin/runtime/lunar_limb.h"
#include "taiyin/runtime/runtime.h"
#include "taiyin/runtime/solar_visibility.h"
#include "taiyin/time.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>

namespace taiyin {
namespace runtime {

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

constexpr double kAuKm = 149597870.7;

constexpr double kEarthEquatorialRadiusKm = TAIYIN_WGS84_A_KM;
constexpr double kSunRadiusKm = 695700.0;
constexpr double kMoonAlmanacRadiusRatio = 0.2725076;
constexpr double kSxwnlMoonPenumbralRadiusRatio = 0.2725076;
constexpr double kSxwnlMoonUmbralRadiusRatio = 0.2722810;
constexpr double kSxwnlSunRadiusRatio = 109.1222;

SplitJulianDate invalid_jd() noexcept {
    return SplitJulianDate(0, std::numeric_limits<double>::quiet_NaN());
}

double moon_radius_km(uint8_t moon_radius_model) {
    dispatch::EclipseMoonRadiusModelEntry entry;
    if (dispatch::find_eclipse_moon_radius_model(static_cast<int>(moon_radius_model), &entry)) {
        return entry.radius_km;
    }
    // Fallback: almanac radius
    return kMoonAlmanacRadiusRatio * kEarthEquatorialRadiusKm;
}

bool include_contacts(uint64_t flags) {
    return (flags & TAIYIN_ECLIPSE_INCLUDE_CONTACTS) != 0;
}

bool local_eclipse_has_inner_contacts(uint32_t kind) noexcept {
    return (kind & (TAIYIN_ECLIPSE_TOTAL
        | TAIYIN_ECLIPSE_ANNULAR
        | TAIYIN_ECLIPSE_HYBRID)) != 0u;
}

uint32_t eclipse_position_flags(uint32_t base_flags, uint64_t flags) noexcept {
    base_flags |= static_cast<uint32_t>(flags) & TAIYIN_ECLIPSE_SUPPORTED_POSITION_FLAGS;
    return base_flags;
}

double norm3_local(const sxwnl::solar::Vec3& v) noexcept {
    return std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
}

sxwnl::solar::Vec3 xyz_to_llr_km_local(const sxwnl::solar::Vec3& v) noexcept {
    return {std::atan2(v.y, v.x), std::atan2(v.z, std::hypot(v.x, v.y)), norm3_local(v)};
}

Status body_equatorial_llr_km_local(
    const NativeCalcContext* context,
    int body_id,
    SplitJulianDate jd_tt,
    sxwnl::solar::Vec3* out,
    EphemerisEvalDiagnostic* diagnostic
) noexcept {
    const uint32_t position_flags = TAIYIN_NATIVE_POSITION_XYZ
                                  | TAIYIN_NATIVE_POSITION_EQUATORIAL;
    const double tdb_minus_tt = dispatch::eval_tdb(context->model_context.tdb_model_id, jd_tt, nullptr);
    SplitJulianDate jd_tdb = jd_tt;
    if (!add_seconds_to_split_jd(jd_tdb, tdb_minus_tt, &jd_tdb)) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    double pos[6] = {};
    const Status st = calc_position_tdb(context, body_id, jd_tdb, jd_tt, position_flags, pos, diagnostic);
    if (st != TAIYIN_STATUS_OK) return st;
    *out = xyz_to_llr_km_local({pos[0] * kAuKm, pos[1] * kAuKm, pos[2] * kAuKm});
    return TAIYIN_STATUS_OK;
}

Status sxwnl_bse_local(
    const NativeCalcContext* context,
    SplitJulianDate jd_tt,
    sxwnl::solar::BesselianFrame* out,
    EphemerisEvalDiagnostic* diagnostic
) noexcept {
    sxwnl::solar::Vec3 sun;
    sxwnl::solar::Vec3 moon;
    Status st = body_equatorial_llr_km_local(context, TAIYIN_BODY_SUN, jd_tt, &sun, diagnostic);
    if (st != TAIYIN_STATUS_OK) return st;
    st = body_equatorial_llr_km_local(context, TAIYIN_BODY_MOON, jd_tt, &moon, diagnostic);
    if (st != TAIYIN_STATUS_OK) return st;

    const sxwnl::solar::Vec3 s_xyz = sxwnl::solar::llr_to_xyz(sun.x, sun.y, sun.z);
    const sxwnl::solar::Vec3 m_xyz = sxwnl::solar::llr_to_xyz(moon.x, moon.y, moon.z);
    const sxwnl::solar::Vec3 axis = xyz_to_llr_km_local({s_xyz.x - m_xyz.x, s_xyz.y - m_xyz.y, s_xyz.z - m_xyz.z});

    SplitJulianDate jd_ut;
    Status time_status = eclipse_tt_to_ut(*context, jd_tt, &jd_ut, nullptr, diagnostic);
    if (time_status != TAIYIN_STATUS_OK) return time_status;
    double gast = 0.0;
    if (!gast_model_rad(
            context->model_context.precession_model_id,
            context->model_context.nutation_model_id,
            jd_ut,
            jd_tt,
            &gast)) {
        return TAIYIN_ERROR_UNSUPPORTED;
    }

    out->J_rad = M_PI / 2.0 + axis.x;
    out->W_rad = M_PI / 2.0 - axis.y;
    out->gst_rad = gast;
    return TAIYIN_STATUS_OK;
}

Status sxwnl_bseM_local(
    const NativeCalcContext* context,
    SplitJulianDate jd_tt,
    sxwnl::solar::Vec3* out,
    EphemerisEvalDiagnostic* diagnostic
) noexcept {
    sxwnl::solar::Vec3 moon;
    sxwnl::solar::BesselianFrame I;
    Status st = body_equatorial_llr_km_local(context, TAIYIN_BODY_MOON, jd_tt, &moon, diagnostic);
    if (st != TAIYIN_STATUS_OK) return st;
    st = sxwnl_bse_local(context, jd_tt, &I, diagnostic);
    if (st != TAIYIN_STATUS_OK) return st;

    sxwnl::solar::Vec3 r = {moon.x - I.J_rad, moon.y, moon.z};
    r = sxwnl::solar::llrConv(r, -I.W_rad);
    *out = sxwnl::solar::llr_to_xyz(r.x, r.y, r.z);
    out->x /= TAIYIN_WGS84_A_KM;
    out->y /= TAIYIN_WGS84_A_KM;
    out->z /= TAIYIN_WGS84_A_KM;
    return TAIYIN_STATUS_OK;
}

double dot3(const double a[3], const double b[3]) noexcept {
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

double norm3(const double a[3]) noexcept {
    return std::sqrt(dot3(a, a));
}

double clamp_unit(double x) noexcept {
    if (x < -1.0) return -1.0;
    if (x > 1.0) return 1.0;
    return x;
}

double normalize_degrees(double x) noexcept {
    x = std::fmod(x, 360.0);
    if (x < 0.0) x += 360.0;
    return x;
}

double signed_degree_delta(double value, double reference) noexcept {
    double delta = value - reference;
    while (delta > 180.0) delta -= 360.0;
    while (delta < -180.0) delta += 360.0;
    return delta;
}

Status eval_solar_equatorial_vectors_km(
    const NativeCalcContext* context,
    SplitJulianDate jd_tt,
    uint64_t flags,
    FastApparentCorrectionSeries* corrections,
    double moon_km[3],
    double sun_km[3],
    EphemerisEvalDiagnostic* diagnostic
) noexcept {
    if (!context || !moon_km || !sun_km) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    const double tdb_minus_tt = dispatch::eval_tdb(
        context->model_context.tdb_model_id, jd_tt, 0);
    SplitJulianDate jd_tdb = jd_tt;
    if (!add_seconds_to_split_jd(jd_tdb, tdb_minus_tt, &jd_tdb)) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }

    FastApparentOptions pair_options;
    pair_options.frame = FAST_APPARENT_TRUE_EQUATOR_OF_DATE;
    pair_options.with_velocity = false;
    pair_options.true_position = (flags & TAIYIN_ECLIPSE_TRUEPOS) != 0;
    FastApparentCorrectionEpochSample correction_sample;
    if (corrections) {
        FastApparentCorrectionConfig correction_config;
        Status correction_status = get_fast_correction(
            context,
            TAIYIN_BODY_MOON,
            TAIYIN_BODY_SUN,
            pair_options,
            correction_config,
            jd_tt,
            corrections,
            diagnostic,
            &correction_sample);
        if (correction_status != TAIYIN_STATUS_OK) return correction_status;
        pair_options.correction_sample = &correction_sample;
    }
    FastApparentBody2State pair;
    Status st = eval_fast_apparent_body_2_tdb(
        context, jd_tdb, jd_tt, TAIYIN_BODY_MOON, TAIYIN_BODY_SUN, pair_options, &pair, diagnostic);
    if (st != TAIYIN_STATUS_OK) return st;

    moon_km[0] = pair.body_0.position_au.x * kAuKm;
    moon_km[1] = pair.body_0.position_au.y * kAuKm;
    moon_km[2] = pair.body_0.position_au.z * kAuKm;
    sun_km[0] = pair.body_1.position_au.x * kAuKm;
    sun_km[1] = pair.body_1.position_au.y * kAuKm;
    sun_km[2] = pair.body_1.position_au.z * kAuKm;
    return TAIYIN_STATUS_OK;
}

Status eval_directional_moon_radius_km(
    const NativeCalcContext* context,
    SplitJulianDate jd_tt,
    bool true_position,
    const double topo_moon[3],
    const double topo_sun[3],
    bool away_from_sun,
    double* out_radius_km
) noexcept {
    if (!context || !topo_moon || !topo_sun || !out_radius_km) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    if (!global_lunar_limb_model()) {
        return TAIYIN_ERROR_UNSUPPORTED;
    }

    Vector3 direction;
    const Status direction_status = apparent_limb_direction_toward_target(
        Vector3{topo_moon[0], topo_moon[1], topo_moon[2]},
        Vector3{topo_sun[0], topo_sun[1], topo_sun[2]},
        away_from_sun,
        &direction);
    if (direction_status != TAIYIN_STATUS_OK) return direction_status;
    const Vector3 moon_to_observer = {
        -topo_moon[0], -topo_moon[1], -topo_moon[2],
    };
    double radius_m = std::nan("");
    const Status status = eval_lunar_limb_radius_from_apparent_frame_m(
        context,
        jd_tt,
        true_position,
        TAIYIN_APPARENT_FRAME_TRUE_EQUATOR_OF_DATE,
        moon_to_observer,
        direction,
        &radius_m);
    if (status != TAIYIN_STATUS_OK) return status;
    *out_radius_km = radius_m / 1000.0;
    return TAIYIN_STATUS_OK;
}

bool context_gast_rad(
    const NativeCalcContext* context,
    SplitJulianDate jd_ut,
    SplitJulianDate jd_tt,
    double* out
) noexcept {
    return context != nullptr
        && out != nullptr
        && gast_model_rad(
            context->model_context.precession_model_id,
            context->model_context.nutation_model_id,
            jd_ut,
            jd_tt,
            out);
}

bool context_observer_true_equator_km(
    const NativeCalcContext* context,
    SplitJulianDate jd_ut,
    SplitJulianDate jd_tt,
    double longitude_rad,
    double latitude_rad,
    double height_m,
    double out_km[3],
    double* out_sidereal_rad
) noexcept {
    double sidereal = 0.0;
    if (!out_km || !context_gast_rad(context, jd_ut, jd_tt, &sidereal)) {
        return false;
    }
    const Vector3 ecef_m = geodetic_to_ecef_m(longitude_rad, latitude_rad, height_m);
    const Vector3 equatorial_m = rotate_z(ecef_m, sidereal);
    out_km[0] = equatorial_m.x / 1000.0;
    out_km[1] = equatorial_m.y / 1000.0;
    out_km[2] = equatorial_m.z / 1000.0;
    if (out_sidereal_rad) {
        *out_sidereal_rad = sidereal;
    }
    return true;
}

}  // namespace

// ===========================================================================
// Public API
// ===========================================================================
double angular_separation_rad(const double a[3], const double b[3]) noexcept {
    const double cos_sep = clamp_unit(dot3(a, b) / (norm3(a) * norm3(b)));
    return std::acos(cos_sep);
}

Status eval_local_topocentric_geometry(
    const NativeCalcContext* context,
    SplitJulianDate jd_tt,
    uint64_t flags,
    FastApparentCorrectionSeries* corrections,
    double longitude_rad,
    double latitude_rad,
    double height_m,
    double* out_separation_deg,
    double* out_sun_radius_deg,
    double* out_moon_radius_deg,
    double* out_sun_altitude_deg,
    double* out_sun_azimuth_deg,
    EphemerisEvalDiagnostic* diagnostic
) noexcept {
    if (!context || !out_separation_deg || !out_sun_radius_deg || !out_moon_radius_deg
        || !out_sun_altitude_deg || !out_sun_azimuth_deg) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }

    double moon_km[3] = {};
    double sun_km[3] = {};
    Status st = eval_solar_equatorial_vectors_km(
        context, jd_tt, flags, corrections, moon_km, sun_km, diagnostic);
    if (st != TAIYIN_STATUS_OK) return st;

    SplitJulianDate jd_ut;
    Status time_status = eclipse_tt_to_ut(*context, jd_tt, &jd_ut, nullptr, diagnostic);
    if (time_status != TAIYIN_STATUS_OK) return time_status;
    double sidereal_rad = 0.0;
    double observer_km[3] = {};
    if (!context_observer_true_equator_km(
            context,
            jd_ut,
            jd_tt,
            longitude_rad,
            latitude_rad,
            height_m,
            observer_km,
            &sidereal_rad)) {
        return TAIYIN_ERROR_UNSUPPORTED;
    }

    double topo_sun[3] = {
        sun_km[0] - observer_km[0],
        sun_km[1] - observer_km[1],
        sun_km[2] - observer_km[2]
    };
    double topo_moon[3] = {
        moon_km[0] - observer_km[0],
        moon_km[1] - observer_km[1],
        moon_km[2] - observer_km[2]
    };
    const double sun_dist_km = norm3(topo_sun);
    const double moon_dist_km = norm3(topo_moon);
    const double moon_radius = moon_radius_km(context->eclipse_moon_radius_model_id);

    *out_separation_deg = angular_separation_rad(topo_sun, topo_moon) * 180.0 / M_PI;
    *out_sun_radius_deg = std::atan2(kSunRadiusKm, sun_dist_km) * 180.0 / M_PI;
    *out_moon_radius_deg = std::atan2(moon_radius, moon_dist_km) * 180.0 / M_PI;

    Vector3 topo_sun_au = {topo_sun[0] / kAuKm, topo_sun[1] / kAuKm, topo_sun[2] / kAuKm};
    const double lst_rad = sidereal_rad + longitude_rad;
    HorizontalCoordinates horiz = topocentric_position_to_horizontal(
        topo_sun_au, lst_rad, latitude_rad);
    *out_sun_altitude_deg = horiz.altitude_rad * 180.0 / M_PI;
    *out_sun_azimuth_deg = horiz.azimuth_rad * 180.0 / M_PI;
    return TAIYIN_STATUS_OK;
}

Status eval_local_topocentric_separation_radii(
    const NativeCalcContext* context,
    SplitJulianDate jd_tt,
    uint64_t flags,
    FastApparentCorrectionSeries* corrections,
    double longitude_rad,
    double latitude_rad,
    double height_m,
    bool inner_contact,
    double* out_separation_deg,
    double* out_sun_radius_deg,
    double* out_moon_radius_deg,
    EphemerisEvalDiagnostic* diagnostic
) noexcept {
    if (!context || !out_separation_deg || !out_sun_radius_deg || !out_moon_radius_deg) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }

    double moon_km[3] = {};
    double sun_km[3] = {};
    const bool use_lunar_limb = (flags & TAIYIN_ECLIPSE_LUNAR_LIMB_CORRECTION) != 0u;
    Status st = eval_solar_equatorial_vectors_km(
        context,
        jd_tt,
        flags,
        corrections,
        moon_km,
        sun_km,
        diagnostic);
    if (st != TAIYIN_STATUS_OK) return st;

    SplitJulianDate jd_ut;
    Status time_status = eclipse_tt_to_ut(*context, jd_tt, &jd_ut, nullptr, diagnostic);
    if (time_status != TAIYIN_STATUS_OK) return time_status;
    double observer_km[3] = {};
    if (!context_observer_true_equator_km(
            context,
            jd_ut,
            jd_tt,
            longitude_rad,
            latitude_rad,
            height_m,
            observer_km,
            nullptr)) {
        return TAIYIN_ERROR_UNSUPPORTED;
    }

    double topo_sun[3] = {
        sun_km[0] - observer_km[0],
        sun_km[1] - observer_km[1],
        sun_km[2] - observer_km[2]
    };
    double topo_moon[3] = {
        moon_km[0] - observer_km[0],
        moon_km[1] - observer_km[1],
        moon_km[2] - observer_km[2]
    };
    const double sun_dist_km = norm3(topo_sun);
    const double moon_dist_km = norm3(topo_moon);
    double moon_radius = moon_radius_km(context->eclipse_moon_radius_model_id);
    const double smooth_moon_radius_deg = std::atan2(moon_radius, moon_dist_km) * 180.0 / M_PI;
    const double sun_radius_deg = std::atan2(kSunRadiusKm, sun_dist_km) * 180.0 / M_PI;
    if (use_lunar_limb) {
        const bool away_from_sun = inner_contact && smooth_moon_radius_deg < sun_radius_deg;
        st = eval_directional_moon_radius_km(
            context,
            jd_tt,
            (flags & TAIYIN_ECLIPSE_TRUEPOS) != 0u,
            topo_moon,
            topo_sun,
            away_from_sun,
            &moon_radius);
        if (st != TAIYIN_STATUS_OK) return st;
    }

    *out_separation_deg = angular_separation_rad(topo_sun, topo_moon) * 180.0 / M_PI;
    *out_sun_radius_deg = sun_radius_deg;
    *out_moon_radius_deg = std::atan2(moon_radius, moon_dist_km) * 180.0 / M_PI;
    return TAIYIN_STATUS_OK;
}

double solar_magnitude(double separation_deg, double sun_radius_deg, double moon_radius_deg) noexcept {
    if (separation_deg <= std::fabs(moon_radius_deg - sun_radius_deg)) {
        return moon_radius_deg / sun_radius_deg;
    }
    return (sun_radius_deg + moon_radius_deg - separation_deg) / (2.0 * sun_radius_deg);
}

double sxwnl_local_magnitude(double separation_deg, double sun_radius_deg, double moon_radius_deg) noexcept {
    return (moon_radius_deg + sun_radius_deg - separation_deg) / (2.0 * sun_radius_deg);
}

double normalize_degrees_local(double x) noexcept {
    x = std::fmod(x, 360.0);
    if (x < 0.0) x += 360.0;
    return x;
}

double solar_obscuration(double separation_deg, double sun_radius_deg, double moon_radius_deg) noexcept {
    if (separation_deg >= sun_radius_deg + moon_radius_deg) return 0.0;
    if (separation_deg <= std::fabs(moon_radius_deg - sun_radius_deg)) {
        return moon_radius_deg >= sun_radius_deg ? 1.0
            : (moon_radius_deg * moon_radius_deg) / (sun_radius_deg * sun_radius_deg);
    }
    const double d = separation_deg * M_PI / 180.0;
    const double r_sun = sun_radius_deg * M_PI / 180.0;
    const double r_moon = moon_radius_deg * M_PI / 180.0;
    const double sun_part = r_sun * r_sun * std::acos(clamp_unit(
        (d * d + r_sun * r_sun - r_moon * r_moon) / (2.0 * d * r_sun)));
    const double moon_part = r_moon * r_moon * std::acos(clamp_unit(
        (d * d + r_moon * r_moon - r_sun * r_sun) / (2.0 * d * r_moon)));
    const double lens = sun_part + moon_part
        - 0.5 * std::sqrt(std::max(0.0,
            (-d + r_sun + r_moon) * (d + r_sun - r_moon)
            * (d - r_sun + r_moon) * (d + r_sun + r_moon)));
    return lens / (M_PI * r_sun * r_sun);
}

void init_local_solar_contacts(LocalSolarEclipseResult* out) noexcept {
    for (size_t i = 0; i < TAIYIN_LOCAL_SOLAR_CONTACT_COUNT; ++i) {
        out->contact_jd_tt[i] = invalid_jd();
    }
}

void init_local_solar_contacts(LocalSolarEclipseResultUt* out) noexcept {
    for (size_t i = 0; i < TAIYIN_LOCAL_SOLAR_CONTACT_COUNT; ++i) {
        out->contact_jd_ut[i] = invalid_jd();
    }
}

double local_contact_radius(double sun_radius_deg, double moon_radius_deg, bool inner_contact) noexcept {
    return inner_contact
        ? std::fabs(moon_radius_deg - sun_radius_deg)
        : moon_radius_deg + sun_radius_deg;
}

Status eval_local_topocentric_xy(
    const NativeCalcContext* context,
    SplitJulianDate jd_tt,
    uint64_t flags,
    FastApparentCorrectionSeries* corrections,
    double longitude_rad,
    double latitude_rad,
    double height_m,
    double* out_x_deg,
    double* out_y_deg,
    double* out_sun_radius_deg,
    double* out_moon_radius_deg,
    double* out_sun_altitude_deg,
    double* out_sun_azimuth_deg,
    double* out_sun_ra_rad,
    double* out_sun_dec_rad,
    double* out_moon_ra_rad,
    double* out_moon_dec_rad,
    EphemerisEvalDiagnostic* diagnostic
) noexcept {
    double moon_km[3] = {};
    double sun_km[3] = {};
    Status st = eval_solar_equatorial_vectors_km(
        context, jd_tt, flags, corrections, moon_km, sun_km, diagnostic);
    if (st != TAIYIN_STATUS_OK) return st;

    SplitJulianDate jd_ut;
    Status time_status = eclipse_tt_to_ut(*context, jd_tt, &jd_ut, nullptr, diagnostic);
    if (time_status != TAIYIN_STATUS_OK) return time_status;
    double sidereal_rad = 0.0;
    double observer_km[3] = {};
    if (!context_observer_true_equator_km(
            context, jd_ut, jd_tt, longitude_rad, latitude_rad, height_m,
            observer_km, &sidereal_rad)) {
        return TAIYIN_ERROR_UNSUPPORTED;
    }

    double topo_sun[3] = {
        sun_km[0] - observer_km[0],
        sun_km[1] - observer_km[1],
        sun_km[2] - observer_km[2]
    };
    double topo_moon[3] = {
        moon_km[0] - observer_km[0],
        moon_km[1] - observer_km[1],
        moon_km[2] - observer_km[2]
    };
    const double sun_dist_km = norm3(topo_sun);
    const double moon_dist_km = norm3(topo_moon);
    const double moon_radius = moon_radius_km(context->eclipse_moon_radius_model_id);

    const double sr = std::atan2(kSunRadiusKm, sun_dist_km) * 180.0 / M_PI;
    const double mr = std::atan2(moon_radius, moon_dist_km) * 180.0 / M_PI;

    const double sun_ra = std::atan2(topo_sun[1], topo_sun[0]);
    const double sun_dec = std::atan2(topo_sun[2], std::hypot(topo_sun[0], topo_sun[1]));
    const double moon_ra = std::atan2(topo_moon[1], topo_moon[0]);
    const double moon_dec = std::atan2(topo_moon[2], std::hypot(topo_moon[0], topo_moon[1]));

    double dlon = moon_ra - sun_ra;
    if (dlon > M_PI) dlon -= 2.0 * M_PI;
    if (dlon < -M_PI) dlon += 2.0 * M_PI;
    const double avg_dec = (moon_dec + sun_dec) / 2.0;

    *out_x_deg = dlon * std::cos(avg_dec) * 180.0 / M_PI;
    *out_y_deg = (moon_dec - sun_dec) * 180.0 / M_PI;
    *out_sun_radius_deg = sr;
    *out_moon_radius_deg = mr;
    *out_sun_ra_rad = sun_ra;
    *out_sun_dec_rad = sun_dec;
    *out_moon_ra_rad = moon_ra;
    *out_moon_dec_rad = moon_dec;

    Vector3 topo_sun_au = {topo_sun[0] / kAuKm, topo_sun[1] / kAuKm, topo_sun[2] / kAuKm};
    const double lst_rad = sidereal_rad + longitude_rad;
    HorizontalCoordinates horiz = topocentric_position_to_horizontal(
        topo_sun_au, lst_rad, latitude_rad);
    *out_sun_altitude_deg = horiz.altitude_rad * 180.0 / M_PI;
    *out_sun_azimuth_deg = horiz.azimuth_rad * 180.0 / M_PI;
    return TAIYIN_STATUS_OK;
}

Status eval_local_xy_state(
    const NativeCalcContext* context,
    SplitJulianDate jd_tt,
    uint64_t flags,
    double longitude_rad,
    double latitude_rad,
    double height_m,
    FastApparentCorrectionSeries* corrections,
    double* out_x_deg,
    double* out_y_deg,
    double* out_sun_radius_deg,
    double* out_moon_radius_deg,
    EphemerisEvalDiagnostic* diagnostic
) noexcept {
    double alt, az, sra, sdec, mra, mdec;
    return eval_local_topocentric_xy(
        context, jd_tt, flags, corrections, longitude_rad, latitude_rad, height_m,
        out_x_deg, out_y_deg, out_sun_radius_deg, out_moon_radius_deg,
        &alt, &az, &sra, &sdec, &mra, &mdec, diagnostic);
}

Status eval_local_sxwnl_magnitude_from_xy(
    const NativeCalcContext* context,
    SplitJulianDate jd_tt,
    uint64_t flags,
    double longitude_rad,
    double latitude_rad,
    double height_m,
    FastApparentCorrectionSeries* corrections,
    double* out_magnitude,
    EphemerisEvalDiagnostic* diagnostic
) noexcept {
    double separation, sr, mr;
    const Status st = eval_local_topocentric_separation_radii(
        context, jd_tt, flags, corrections, longitude_rad, latitude_rad, height_m,
        false, &separation, &sr, &mr, diagnostic);
    if (st != TAIYIN_STATUS_OK) return st;
    *out_magnitude = sxwnl_local_magnitude(separation, sr, mr);
    return TAIYIN_STATUS_OK;
}

Status refine_local_sxwnl_magnitude_maximum(
    const NativeCalcContext* context,
    SplitJulianDate* jd_tt,
    uint64_t flags,
    double longitude_rad,
    double latitude_rad,
    double height_m,
    FastApparentCorrectionSeries* corrections,
    double initial_step_days,
    int iterations,
    EphemerisEvalDiagnostic* diagnostic
) noexcept {
    if (!jd_tt || !split_julian_date_is_finite(*jd_tt)) return TAIYIN_ERROR_INVALID_ARGUMENT;
    SplitJulianDate t = *jd_tt;
    double step = initial_step_days;
    if (!(step > 0.0) || !std::isfinite(step)) step = 1.0 / 86400.0;
    if (iterations <= 0) iterations = 8;
    for (int i = 0; i < iterations; ++i) {
        double fm, f0, fp;
        Status st = eval_local_sxwnl_magnitude_from_xy(
            context, t - step, flags, longitude_rad, latitude_rad, height_m, corrections, &fm, diagnostic);
        if (st != TAIYIN_STATUS_OK) return st;
        st = eval_local_sxwnl_magnitude_from_xy(
            context, t, flags, longitude_rad, latitude_rad, height_m, corrections, &f0, diagnostic);
        if (st != TAIYIN_STATUS_OK) return st;
        st = eval_local_sxwnl_magnitude_from_xy(
            context, t + step, flags, longitude_rad, latitude_rad, height_m, corrections, &fp, diagnostic);
        if (st != TAIYIN_STATUS_OK) return st;

        const double curvature = fp + fm - 2.0 * f0;
        if (!(curvature < 0.0) || std::fabs(curvature) < 1e-15) {
            step *= 0.5;
            continue;
        }
        double offset = step * (fm - fp) / (2.0 * curvature);
        if (!std::isfinite(offset)) {
            step *= 0.5;
            continue;
        }
        const double max_offset = 2.0 * step;
        if (offset > max_offset) offset = max_offset;
        if (offset < -max_offset) offset = -max_offset;
        t += offset;
        step *= 0.5;
    }
    *jd_tt = t;
    return TAIYIN_STATUS_OK;
}

struct LocalSolarProbeSample {
    double offset_days;
    double x_deg;
    double y_deg;
    double sun_radius_deg;
    double moon_radius_deg;
};

struct LocalSolarProbeTable {
    SplitJulianDate center_jd_tt;
    int count;
    LocalSolarProbeSample samples[13];
};

double local_probe_sample_magnitude(const LocalSolarProbeSample& s) noexcept {
    return sxwnl_local_magnitude(
        std::hypot(s.x_deg, s.y_deg),
        s.sun_radius_deg,
        s.moon_radius_deg);
}

Status build_local_solar_probe_table(
    const NativeCalcContext* context,
    SplitJulianDate center_jd_tt,
    uint64_t flags,
    double longitude_rad,
    double latitude_rad,
    double height_m,
    LocalSolarProbeTable* out,
    EphemerisEvalDiagnostic* diagnostic
) noexcept {
    if (!context || !out || !split_julian_date_is_finite(center_jd_tt)) return TAIYIN_ERROR_INVALID_ARGUMENT;
    out->center_jd_tt = center_jd_tt;
    out->count = 13;
    constexpr double kStepDays = 0.125;
    for (int i = 0; i < out->count; ++i) {
        const double offset = (static_cast<double>(i) - 6.0) * kStepDays;
        LocalSolarProbeSample& sample = out->samples[i];
        sample.offset_days = offset;
        const Status st = eval_local_xy_state(
            context,
            center_jd_tt + offset,
            flags,
            longitude_rad,
            latitude_rad,
            height_m,
            nullptr,
            &sample.x_deg,
            &sample.y_deg,
            &sample.sun_radius_deg,
            &sample.moon_radius_deg,
            diagnostic);
        if (st != TAIYIN_STATUS_OK) return st;
    }
    return TAIYIN_STATUS_OK;
}

double interpolate_local_probe_value(
    const LocalSolarProbeTable& table,
    double offset_days,
    double LocalSolarProbeSample::*member
) noexcept {
    int start = static_cast<int>(std::floor((offset_days - table.samples[0].offset_days) / 0.125)) - 1;
    if (start < 0) start = 0;
    if (start > table.count - 4) start = table.count - 4;

    double value = 0.0;
    for (int j = 0; j < 4; ++j) {
        const int idx = start + j;
        double basis = 1.0;
        const double xj = table.samples[idx].offset_days;
        for (int m = 0; m < 4; ++m) {
            if (m == j) continue;
            const double xm = table.samples[start + m].offset_days;
            basis *= (offset_days - xm) / (xj - xm);
        }
        value += table.samples[idx].*member * basis;
    }
    return value;
}

LocalSolarProbeSample interpolate_local_probe_sample(
    const LocalSolarProbeTable& table,
    double offset_days
) noexcept {
    if (offset_days <= table.samples[0].offset_days) return table.samples[0];
    if (offset_days >= table.samples[table.count - 1].offset_days) {
        return table.samples[table.count - 1];
    }
    LocalSolarProbeSample out;
    out.offset_days = offset_days;
    out.x_deg = interpolate_local_probe_value(table, offset_days, &LocalSolarProbeSample::x_deg);
    out.y_deg = interpolate_local_probe_value(table, offset_days, &LocalSolarProbeSample::y_deg);
    out.sun_radius_deg = interpolate_local_probe_value(
        table, offset_days, &LocalSolarProbeSample::sun_radius_deg);
    out.moon_radius_deg = interpolate_local_probe_value(
        table, offset_days, &LocalSolarProbeSample::moon_radius_deg);
    return out;
}

Status seed_local_solar_probe_table_maximum(
    const LocalSolarProbeTable& table,
    SplitJulianDate* out_jd_tt
) noexcept {
    if (!out_jd_tt || table.count <= 0) return TAIYIN_ERROR_INVALID_ARGUMENT;
    int best_index = 0;
    double best_mag = -1.0e100;
    for (int i = 0; i < table.count; ++i) {
        const double mag = local_probe_sample_magnitude(table.samples[i]);
        if (mag > best_mag) {
            best_mag = mag;
            best_index = i;
        }
    }

    double best_offset = table.samples[best_index].offset_days;
    if (best_index > 0 && best_index + 1 < table.count) {
        const double ym = local_probe_sample_magnitude(table.samples[best_index - 1]);
        const double y0 = local_probe_sample_magnitude(table.samples[best_index]);
        const double yp = local_probe_sample_magnitude(table.samples[best_index + 1]);
        const double curvature = ym - 2.0 * y0 + yp;
        if (curvature < -1.0e-14) {
            double offset = 0.5 * (ym - yp) / curvature * 0.125;
            if (offset > 0.125) offset = 0.125;
            if (offset < -0.125) offset = -0.125;
            best_offset += offset;
        }
    }

    double step = 20.0 / 1440.0;
    for (int i = 0; i < 6; ++i) {
        const LocalSolarProbeSample sm = interpolate_local_probe_sample(table, best_offset - step);
        const LocalSolarProbeSample s0 = interpolate_local_probe_sample(table, best_offset);
        const LocalSolarProbeSample sp = interpolate_local_probe_sample(table, best_offset + step);
        const double ym = local_probe_sample_magnitude(sm);
        const double y0 = local_probe_sample_magnitude(s0);
        const double yp = local_probe_sample_magnitude(sp);
        const double curvature = ym - 2.0 * y0 + yp;
        if (curvature < -1.0e-14) {
            double correction = 0.5 * (ym - yp) / curvature * step;
            if (correction > step) correction = step;
            if (correction < -step) correction = -step;
            best_offset += correction;
        }
        if (best_offset < table.samples[0].offset_days) {
            best_offset = table.samples[0].offset_days;
        }
        if (best_offset > table.samples[table.count - 1].offset_days) {
            best_offset = table.samples[table.count - 1].offset_days;
        }
        step *= 0.5;
    }

    if (!std::isfinite(best_offset)) return TAIYIN_ERROR_UNSUPPORTED;
    *out_jd_tt = table.center_jd_tt + best_offset;
    return TAIYIN_STATUS_OK;
}

Status seed_local_besselian_polynomial(
    const NativeCalcContext* context,
    SplitJulianDate jd_tt,
    double longitude_rad,
    double latitude_rad,
    double height_m,
    const SolarBesselianPolynomial* precomputed,
    SplitJulianDate* out_jd_tt,
    EphemerisEvalDiagnostic* diagnostic
) noexcept {
    if (!context || !out_jd_tt || !split_julian_date_is_finite(jd_tt)) return TAIYIN_ERROR_INVALID_ARGUMENT;

    SolarBesselianPolynomial poly_storage;
    const SolarBesselianPolynomial* poly = precomputed;
    Status st = TAIYIN_STATUS_OK;
    if (!poly) {
        st = compute_solar_besselian_polynomial_tt(
            context, jd_tt, 12.0, 1.0, 4, &poly_storage, diagnostic);
        if (st != TAIYIN_STATUS_OK) return st;
        poly = &poly_storage;
    }

    const Vector3 fixed_m = geodetic_to_ecef_m(longitude_rad, latitude_rad, height_m);
    const double rho_cos_phi = std::hypot(fixed_m.x, fixed_m.y) / (TAIYIN_WGS84_A_KM * 1000.0);
    const double rho_sin_phi = fixed_m.z / (TAIYIN_WGS84_A_KM * 1000.0);
    const double lon_deg = longitude_rad * 180.0 / M_PI;

    auto metric = [&](double t_hours, double* value) -> Status {
        SolarBesselianElements e;
        const Status s = evaluate_solar_besselian_polynomial(poly, t_hours, &e);
        if (s != TAIYIN_STATUS_OK) return s;
        const double h = normalize_degrees_local(e.mu_deg + lon_deg) * M_PI / 180.0;
        const double d = e.d_deg * M_PI / 180.0;
        const double xi = rho_cos_phi * std::sin(h);
        const double eta = rho_sin_phi * std::cos(d) - rho_cos_phi * std::cos(h) * std::sin(d);
        const double dx = xi - e.x;
        const double dy = eta - e.y;
        *value = dx * dx + dy * dy;
        return TAIYIN_STATUS_OK;
    };

    double best_t = 0.0;
    double best_value = 1.0e100;
    for (int i = -12; i <= 12; ++i) {
        const double t = static_cast<double>(i) * 0.5;
        double v = 0.0;
        st = metric(t, &v);
        if (st != TAIYIN_STATUS_OK) return st;
        if (v < best_value) {
            best_value = v;
            best_t = t;
        }
    }

    double lo = std::max(-6.0, best_t - 1.0);
    double hi = std::min(6.0, best_t + 1.0);
    const double phi = (1.0 + std::sqrt(5.0)) / 2.0;
    double c = hi - (hi - lo) / phi;
    double d = lo + (hi - lo) / phi;
    double fc = 0.0;
    double fd = 0.0;
    st = metric(c, &fc);
    if (st != TAIYIN_STATUS_OK) return st;
    st = metric(d, &fd);
    if (st != TAIYIN_STATUS_OK) return st;
    for (int i = 0; i < 32; ++i) {
        if (fc < fd) {
            hi = d;
            d = c;
            fd = fc;
            c = hi - (hi - lo) / phi;
            st = metric(c, &fc);
            if (st != TAIYIN_STATUS_OK) return st;
        } else {
            lo = c;
            c = d;
            fc = fd;
            d = lo + (hi - lo) / phi;
            st = metric(d, &fd);
            if (st != TAIYIN_STATUS_OK) return st;
        }
    }

    const double t_hours = (lo + hi) * 0.5;
    if (!std::isfinite(t_hours) || std::fabs(t_hours) > 6.0) {
        return TAIYIN_ERROR_UNSUPPORTED;
    }
    *out_jd_tt = jd_tt + t_hours / 24.0;
    return TAIYIN_STATUS_OK;
}

struct LocalBesselianObserver {
    double rho_cos_phi;
    double rho_sin_phi;
    double longitude_deg;
};

LocalBesselianObserver make_local_besselian_observer(
    double longitude_rad,
    double latitude_rad,
    double height_m
) noexcept {
    const Vector3 fixed_m = geodetic_to_ecef_m(longitude_rad, latitude_rad, height_m);
    LocalBesselianObserver out;
    out.rho_cos_phi = std::hypot(fixed_m.x, fixed_m.y) / (TAIYIN_WGS84_A_KM * 1000.0);
    out.rho_sin_phi = fixed_m.z / (TAIYIN_WGS84_A_KM * 1000.0);
    out.longitude_deg = longitude_rad * 180.0 / M_PI;
    return out;
}

Status eval_local_besselian_contact_scalar(
    const SolarBesselianPolynomial* poly,
    const LocalBesselianObserver& observer,
    double t_hours,
    bool inner_contact,
    double* out_value
) noexcept {
    SolarBesselianElements e;
    const Status st = evaluate_solar_besselian_polynomial(poly, t_hours, &e);
    if (st != TAIYIN_STATUS_OK) return st;
    const double h = normalize_degrees_local(e.mu_deg + observer.longitude_deg) * M_PI / 180.0;
    const double d = e.d_deg * M_PI / 180.0;
    const double xi = observer.rho_cos_phi * std::sin(h);
    const double eta = observer.rho_sin_phi * std::cos(d)
        - observer.rho_cos_phi * std::cos(h) * std::sin(d);
    const double zeta = observer.rho_sin_phi * std::sin(d)
        + observer.rho_cos_phi * std::cos(h) * std::cos(d);
    const double dx = e.x - xi;
    const double dy = e.y - eta;
    const double radius = inner_contact
        ? std::fabs(e.l2 - zeta * e.tan_f2)
        : e.l1 + zeta * e.tan_f1;
    *out_value = std::hypot(dx, dy) - radius;
    return TAIYIN_STATUS_OK;
}

Status solve_local_contact_pair_besselian_seed(
    const NativeCalcContext* context,
    SplitJulianDate jd_max_tt,
    uint64_t flags,
    double longitude_rad,
    double latitude_rad,
    double height_m,
    FastApparentCorrectionSeries* corrections,
    const SolarBesselianPolynomial* precomputed,
    bool inner_contact,
    SplitJulianDate* out_before_jd_tt,
    SplitJulianDate* out_after_jd_tt,
    EphemerisEvalDiagnostic* diagnostic
) noexcept {
    if (!context || !out_before_jd_tt || !out_after_jd_tt) return TAIYIN_ERROR_INVALID_ARGUMENT;
    *out_before_jd_tt = invalid_jd();
    *out_after_jd_tt = invalid_jd();

    SolarBesselianPolynomial storage;
    const SolarBesselianPolynomial* poly = precomputed;
    if (!poly || std::fabs((jd_max_tt - poly->t0_jd_tt) * 24.0) > poly->span_hours * 0.5 - 0.25) {
        const Status st = compute_solar_besselian_polynomial_tt_with_corrections(
            context, jd_max_tt, 12.0, 1.0, 4, flags, corrections, &storage, diagnostic);
        if (st != TAIYIN_STATUS_OK) return st;
        poly = &storage;
    }

    const LocalBesselianObserver observer = make_local_besselian_observer(
        longitude_rad, latitude_rad, height_m);
    const double t_max = (jd_max_tt - poly->t0_jd_tt) * 24.0;
    const double half = poly->span_hours * 0.5;
    const double lo = std::max(-half, t_max - 6.0);
    const double hi = std::min(half, t_max + 6.0);
    const double step = inner_contact ? 0.01 : 0.025;

    double prev_t = lo;
    double prev_value = 0.0;
    Status st = eval_local_besselian_contact_scalar(poly, observer, prev_t, inner_contact, &prev_value);
    if (st != TAIYIN_STATUS_OK) return st;
    for (double t = lo + step; t <= hi + step * 0.5; t += step) {
        const double cur_t = std::min(t, hi);
        double cur_value = 0.0;
        st = eval_local_besselian_contact_scalar(poly, observer, cur_t, inner_contact, &cur_value);
        if (st != TAIYIN_STATUS_OK) return st;
        if (std::isfinite(prev_value) && std::isfinite(cur_value) && prev_value * cur_value <= 0.0) {
            double a = prev_t;
            double b = cur_t;
            double fa = prev_value;
            for (int i = 0; i < 50; ++i) {
                const double mid = (a + b) * 0.5;
                double fm = 0.0;
                st = eval_local_besselian_contact_scalar(poly, observer, mid, inner_contact, &fm);
                if (st != TAIYIN_STATUS_OK) return st;
                if (fa * fm <= 0.0) {
                    b = mid;
                } else {
                    a = mid;
                    fa = fm;
                }
            }
            const double root_t = (a + b) * 0.5;
            if (root_t < t_max) *out_before_jd_tt = poly->t0_jd_tt + root_t / 24.0;
            if (root_t > t_max && !split_julian_date_is_finite(*out_after_jd_tt)) {
                *out_after_jd_tt = poly->t0_jd_tt + root_t / 24.0;
            }
        }
        prev_t = cur_t;
        prev_value = cur_value;
    }
    return split_julian_date_is_finite(*out_before_jd_tt) && split_julian_date_is_finite(*out_after_jd_tt)
        ? TAIYIN_STATUS_OK
        : TAIYIN_ERROR_UNSUPPORTED;
}

double local_contact_exact_scalar(
    const NativeCalcContext* context,
    SplitJulianDate jd_tt,
    uint64_t flags,
    FastApparentCorrectionSeries* corrections,
    double longitude_rad,
    double latitude_rad,
    double height_m,
    bool inner_contact,
    EphemerisEvalDiagnostic* diagnostic
) noexcept {
    double separation, sr, mr;
    const Status st = eval_local_topocentric_separation_radii(
        context, jd_tt, flags, corrections, longitude_rad, latitude_rad, height_m,
        inner_contact, &separation, &sr, &mr, diagnostic);
    if (st != TAIYIN_STATUS_OK) return std::nan("");
    return separation - local_contact_radius(sr, mr, inner_contact);
}

Status correct_local_contact_exact(
    const NativeCalcContext* context,
    SplitJulianDate seed_jd_tt,
    uint64_t flags,
    FastApparentCorrectionSeries* corrections,
    double longitude_rad,
    double latitude_rad,
    double height_m,
    bool inner_contact,
    SplitJulianDate* out_jd_tt,
    EphemerisEvalDiagnostic* diagnostic
) noexcept {
    if (!out_jd_tt || !split_julian_date_is_finite(seed_jd_tt)) return TAIYIN_ERROR_INVALID_ARGUMENT;

    SplitJulianDate t = seed_jd_tt;
    constexpr double kDerivativeStepDays = 0.5 / 86400.0;
    constexpr double kMaxCorrectionDays = 30.0 / 86400.0;
    for (int i = 0; i < 2; ++i) {
        const double f0 = local_contact_exact_scalar(
            context, t, flags, corrections, longitude_rad, latitude_rad, height_m, inner_contact, diagnostic);
        const double fm = local_contact_exact_scalar(
            context, t - kDerivativeStepDays, flags, corrections, longitude_rad, latitude_rad, height_m,
            inner_contact, diagnostic);
        const double fp = local_contact_exact_scalar(
            context, t + kDerivativeStepDays, flags, corrections, longitude_rad, latitude_rad, height_m,
            inner_contact, diagnostic);
        if (!std::isfinite(f0) || !std::isfinite(fm) || !std::isfinite(fp)) {
            return TAIYIN_EPHEMERIS_ERROR_EVAL_FAILED;
        }
        const double slope = (fp - fm) / (2.0 * kDerivativeStepDays);
        if (!std::isfinite(slope) || std::fabs(slope) < 1.0e-12) {
            return TAIYIN_ERROR_UNSUPPORTED;
        }
        const double correction = -f0 / slope;
        if (!std::isfinite(correction) || std::fabs(correction) > kMaxCorrectionDays) {
            return TAIYIN_ERROR_UNSUPPORTED;
        }
        t += correction;
        if (std::fabs(correction) < 0.01 / 86400.0) break;
    }
    *out_jd_tt = t;
    return TAIYIN_STATUS_OK;
}

Status refine_local_contact_exact(
    const NativeCalcContext* context,
    SplitJulianDate seed_jd_tt,
    uint64_t flags,
    FastApparentCorrectionSeries* corrections,
    double longitude_rad,
    double latitude_rad,
    double height_m,
    bool inner_contact,
    SplitJulianDate* out_jd_tt,
    EphemerisEvalDiagnostic* diagnostic
) noexcept {
    if (!out_jd_tt) return TAIYIN_ERROR_INVALID_ARGUMENT;
    *out_jd_tt = invalid_jd();

    const Status direct_correction_status = correct_local_contact_exact(
        context, seed_jd_tt, flags, corrections, longitude_rad, latitude_rad, height_m,
        inner_contact, out_jd_tt, diagnostic);
    if (direct_correction_status == TAIYIN_STATUS_OK) return TAIYIN_STATUS_OK;

    SplitJulianDate lo = invalid_jd();
    SplitJulianDate hi = invalid_jd();
    double flo = std::nan("");
    double fhi = std::nan("");
    const double scan_radius = inner_contact ? (2.0 / 1440.0) : (20.0 / 1440.0);
    const double step = inner_contact ? (2.0 / 86400.0) : (5.0 / 86400.0);
    SplitJulianDate prev_t = seed_jd_tt - scan_radius;
    double prev_v = local_contact_exact_scalar(
        context, prev_t, flags, corrections, longitude_rad, latitude_rad, height_m,
        inner_contact, diagnostic);
    for (double offset = -scan_radius + step;
         offset <= scan_radius + step * 0.5;
         offset += step) {
        const SplitJulianDate t = seed_jd_tt + offset;
        const double v = local_contact_exact_scalar(
            context, t, flags, corrections, longitude_rad, latitude_rad, height_m,
            inner_contact, diagnostic);
        if (std::isfinite(prev_v) && std::isfinite(v) && prev_v * v <= 0.0) {
            lo = prev_t;
            hi = t;
            flo = prev_v;
            fhi = v;
            break;
        }
        prev_t = t;
        prev_v = v;
    }
    if (!std::isfinite(flo) || !std::isfinite(fhi)) {
        return TAIYIN_ERROR_UNSUPPORTED;
    }
    for (int i = 0; i < 24 && (hi - lo) * 86400.0 > 0.02; ++i) {
        const SplitJulianDate mid = lo + 0.5 * (hi - lo);
        const double fm = local_contact_exact_scalar(
            context, mid, flags, corrections, longitude_rad, latitude_rad, height_m,
            inner_contact, diagnostic);
        if (!std::isfinite(fm)) return TAIYIN_EPHEMERIS_ERROR_EVAL_FAILED;
        if (flo * fm <= 0.0) {
            hi = mid;
            fhi = fm;
        } else {
            lo = mid;
            flo = fm;
        }
    }
    (void)fhi;
    const SplitJulianDate root = lo + 0.5 * (hi - lo);
    const Status correction_status = correct_local_contact_exact(
        context, root, flags, corrections, longitude_rad, latitude_rad, height_m,
        inner_contact, out_jd_tt, diagnostic);
    if (correction_status == TAIYIN_STATUS_OK) return TAIYIN_STATUS_OK;
    *out_jd_tt = root;
    return TAIYIN_STATUS_OK;
}

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
) noexcept {
    if (out_duration_seconds) *out_duration_seconds = std::nan("");
    if (!context || !out_duration_seconds
        || !split_julian_date_is_finite(center_jd_tt)
        || !std::isfinite(longitude_deg)
        || !std::isfinite(latitude_deg)
        || !std::isfinite(height_m)
        || !std::isfinite(seed_duration_seconds)
        || !(seed_duration_seconds > 0.0)) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }

    FastApparentOptions window_options;
    window_options.frame = FAST_APPARENT_TRUE_EQUATOR_OF_DATE;
    window_options.with_velocity = false;
    window_options.true_position = (flags & TAIYIN_ECLIPSE_TRUEPOS) != 0;
    FastApparentCorrectionConfig correction_config;
    correction_config.initial_half_days = 3.0 / 24.0;
    correction_config.sample_step_days = 3.0 / 24.0;
    FastApparentCorrectionSeries corrections;
    Status status = init_fast_correction_series(
        context,
        TAIYIN_BODY_MOON,
        TAIYIN_BODY_SUN,
        window_options,
        correction_config,
        center_jd_tt,
        &corrections,
        diagnostic);
    if (status != TAIYIN_STATUS_OK) return status;

    const double longitude_rad = longitude_deg * M_PI / 180.0;
    const double latitude_rad = latitude_deg * M_PI / 180.0;
    const double seed_half_days = 0.5 * seed_duration_seconds / 86400.0;
    SplitJulianDate c2_jd_tt = invalid_jd();
    SplitJulianDate c3_jd_tt = invalid_jd();
    status = refine_local_contact_exact(
        context,
        center_jd_tt - seed_half_days,
        flags,
        &corrections,
        longitude_rad,
        latitude_rad,
        height_m,
        true,
        &c2_jd_tt,
        diagnostic);
    if (status != TAIYIN_STATUS_OK) return status;
    status = refine_local_contact_exact(
        context,
        center_jd_tt + seed_half_days,
        flags,
        &corrections,
        longitude_rad,
        latitude_rad,
        height_m,
        true,
        &c3_jd_tt,
        diagnostic);
    if (status != TAIYIN_STATUS_OK) return status;
    if (!split_julian_date_is_finite(c2_jd_tt) || !split_julian_date_is_finite(c3_jd_tt)
        || !(c2_jd_tt <= center_jd_tt)
        || !(c3_jd_tt >= center_jd_tt)
        || !(c3_jd_tt > c2_jd_tt)) {
        return TAIYIN_ERROR_UNSUPPORTED;
    }
    *out_duration_seconds = (c3_jd_tt - c2_jd_tt) * 86400.0;
    return std::isfinite(*out_duration_seconds) && *out_duration_seconds > 0.0
        ? TAIYIN_STATUS_OK
        : TAIYIN_ERROR_UNSUPPORTED;
}

Status complete_local_solar_eclipse_contacts_from_max(
    const NativeCalcContext* context,
    double longitude_deg,
    double latitude_deg,
    double height_m,
    uint64_t flags,
    bool complete_visibility,
    FastApparentCorrectionSeries* corrections,
    const SolarBesselianPolynomial* besselian_seed,
    LocalSolarEclipseResult* out,
    EphemerisEvalDiagnostic* diagnostic
) noexcept {
    if (!context || !out || !split_julian_date_is_finite(out->maximum_jd_tt)) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    uint64_t fast_visibility_flags = TAIYIN_SOLAR_VISIBILITY_FLAG_NO_REFRACTION;
    if (!resolve_local_eclipse_fast_visibility_flags(flags, &fast_visibility_flags)) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    if ((fast_visibility_flags & TAIYIN_SOLAR_VISIBILITY_FLAG_REFRACTION) != 0u) {
        const bool strict =
            (fast_visibility_flags & TAIYIN_SOLAR_VISIBILITY_STRICT_METEOROLOGY) != 0u;
        const bool allow_standard_fallback =
            !strict
            && (context->atmosphere_policy_flags
                & TAIYIN_NATIVE_ATMOSPHERE_ALLOW_STANDARD_FALLBACK) != 0u;
        NativeAtmosphere atmosphere;
        if (!dispatch::has_refraction_model(context->refraction_model_id)
            || !native_context_resolve_refraction_atmosphere(
                *context, allow_standard_fallback, &atmosphere)) {
            return TAIYIN_ERROR_INVALID_ARGUMENT;
        }
    }

    const double lon_rad = longitude_deg * M_PI / 180.0;
    const double lat_rad = latitude_deg * M_PI / 180.0;
    const SplitJulianDate best_jd = out->maximum_jd_tt;
    const bool write_contacts = include_contacts(flags) || flags == 0;
    constexpr double kContactWindowHalfDays = 6.0 / 24.0;

    FastApparentOptions window_options;
    window_options.frame = FAST_APPARENT_TRUE_EQUATOR_OF_DATE;
    window_options.with_velocity = false;
    window_options.true_position = (flags & TAIYIN_ECLIPSE_TRUEPOS) != 0;
    FastApparentCorrectionConfig correction_config;
    correction_config.initial_half_days = kContactWindowHalfDays;
    correction_config.sample_step_days = 3.0 / 24.0;
    FastApparentCorrectionSeries window_series;
    FastApparentCorrectionSeries* active_corrections = corrections ? corrections : &window_series;
    FastApparentCorrectionEpochSample init_sample;
    Status correction_status = TAIYIN_STATUS_OK;
    if (corrections) {
        correction_status = get_fast_correction(
            context,
            TAIYIN_BODY_MOON,
            TAIYIN_BODY_SUN,
            window_options,
            correction_config,
            best_jd,
            active_corrections,
            diagnostic,
            &init_sample);
    } else {
        correction_status = init_fast_correction_series(
            context,
            TAIYIN_BODY_MOON,
            TAIYIN_BODY_SUN,
            window_options,
            correction_config,
            best_jd,
            active_corrections,
            diagnostic);
    }
    if (correction_status != TAIYIN_STATUS_OK) return correction_status;
    const SolarBesselianPolynomial* contact_besselian_seed = besselian_seed;

    if (write_contacts) {
        SplitJulianDate p1_seed = invalid_jd();
        SplitJulianDate p4_seed = invalid_jd();
        Status contact_status = solve_local_contact_pair_besselian_seed(
            context, best_jd, flags, lon_rad, lat_rad, height_m, active_corrections, contact_besselian_seed, false,
            &p1_seed, &p4_seed, diagnostic);
        if (contact_status != TAIYIN_STATUS_OK) return contact_status;
        out->contact_jd_tt[TAIYIN_LOCAL_SOLAR_CONTACT_GREATEST] = best_jd;
        contact_status = refine_local_contact_exact(
            context, p1_seed, flags, active_corrections, lon_rad, lat_rad, height_m, false,
            &out->contact_jd_tt[TAIYIN_LOCAL_SOLAR_CONTACT_C1], diagnostic);
        if (contact_status != TAIYIN_STATUS_OK) return contact_status;
        contact_status = refine_local_contact_exact(
            context, p4_seed, flags, active_corrections, lon_rad, lat_rad, height_m, false,
            &out->contact_jd_tt[TAIYIN_LOCAL_SOLAR_CONTACT_C4], diagnostic);
        if (contact_status != TAIYIN_STATUS_OK) return contact_status;
        if (local_eclipse_has_inner_contacts(out->kind)) {
            SplitJulianDate c2_seed = invalid_jd();
            SplitJulianDate c3_seed = invalid_jd();
            contact_status = solve_local_contact_pair_besselian_seed(
                context, best_jd, flags, lon_rad, lat_rad, height_m, active_corrections, contact_besselian_seed, true,
                &c2_seed, &c3_seed, diagnostic);
            if (contact_status != TAIYIN_STATUS_OK) return contact_status;
            contact_status = refine_local_contact_exact(
                context, c2_seed, flags, active_corrections, lon_rad, lat_rad, height_m, true,
                &out->contact_jd_tt[TAIYIN_LOCAL_SOLAR_CONTACT_C2], diagnostic);
            if (contact_status != TAIYIN_STATUS_OK) return contact_status;
            contact_status = refine_local_contact_exact(
                context, c3_seed, flags, active_corrections, lon_rad, lat_rad, height_m, true,
                &out->contact_jd_tt[TAIYIN_LOCAL_SOLAR_CONTACT_C3], diagnostic);
            if (contact_status != TAIYIN_STATUS_OK) return contact_status;
        }
    }

    double c1_altitude_deg = std::nan("");
    double c4_altitude_deg = std::nan("");
    auto eval_pa = [&](SplitJulianDate contact_jd, double* pa_deg, double* va_deg, double* altitude_deg) {
        double cx, cy, csr, cmr, calt, caz, csra, csdec, cmra, cmdec;
        Status s2 = eval_local_topocentric_xy(
            context, contact_jd, flags, active_corrections, lon_rad, lat_rad, height_m,
            &cx, &cy, &csr, &cmr, &calt, &caz, &csra, &csdec, &cmra, &cmdec, diagnostic);
        if (s2 != TAIYIN_STATUS_OK) return;
        if (altitude_deg) *altitude_deg = calt;
        const double pa = std::atan2(cx, cy);
        SplitJulianDate jd_ut_c;
        if (eclipse_tt_to_ut(*context, contact_jd, &jd_ut_c, nullptr, diagnostic) != TAIYIN_STATUS_OK) return;
        double gast = 0.0;
        if (gast_model_rad(
                context->model_context.precession_model_id,
                context->model_context.nutation_model_id,
                jd_ut_c, contact_jd, &gast)) {
            const double hour_angle = gast + lon_rad - csra;
            const double q = std::atan2(std::sin(hour_angle), std::tan(csdec) * std::cos(lat_rad) - std::sin(lat_rad) * std::cos(hour_angle));
            *pa_deg = pa * 180.0 / M_PI;
            *va_deg = (pa - q) * 180.0 / M_PI;
        }
    };
    if (split_julian_date_is_finite(out->contact_jd_tt[TAIYIN_LOCAL_SOLAR_CONTACT_C1])) {
        eval_pa(out->contact_jd_tt[TAIYIN_LOCAL_SOLAR_CONTACT_C1],
                &out->position_angle_c1_deg, &out->vertex_angle_c1_deg, &c1_altitude_deg);
    }
    if (split_julian_date_is_finite(out->contact_jd_tt[TAIYIN_LOCAL_SOLAR_CONTACT_C4])) {
        eval_pa(out->contact_jd_tt[TAIYIN_LOCAL_SOLAR_CONTACT_C4],
                &out->position_angle_c4_deg, &out->vertex_angle_c4_deg, &c4_altitude_deg);
    }

    if (split_julian_date_is_finite(out->contact_jd_tt[TAIYIN_LOCAL_SOLAR_CONTACT_C2])
        && split_julian_date_is_finite(out->contact_jd_tt[TAIYIN_LOCAL_SOLAR_CONTACT_C3])) {
        if (out->contact_jd_tt[TAIYIN_LOCAL_SOLAR_CONTACT_C3]
            >= out->contact_jd_tt[TAIYIN_LOCAL_SOLAR_CONTACT_C2]) {
            out->duration_seconds = (out->contact_jd_tt[TAIYIN_LOCAL_SOLAR_CONTACT_C3]
                - out->contact_jd_tt[TAIYIN_LOCAL_SOLAR_CONTACT_C2]) * 86400.0;
        } else {
            out->contact_jd_tt[TAIYIN_LOCAL_SOLAR_CONTACT_C2] = invalid_jd();
            out->contact_jd_tt[TAIYIN_LOCAL_SOLAR_CONTACT_C3] = invalid_jd();
            out->duration_seconds = 0.0;
        }
    }

    if (!complete_visibility) {
        return TAIYIN_STATUS_OK;
    }

    if (std::isfinite(c1_altitude_deg) && std::isfinite(c4_altitude_deg)
        && c1_altitude_deg > 0.0 && c4_altitude_deg > 0.0
        && std::isfinite(out->sun_altitude_deg) && out->sun_altitude_deg > 0.0) {
        return TAIYIN_STATUS_OK;
    }

    SplitJulianDate sun_s_jd = invalid_jd();
    SplitJulianDate sun_j_jd = invalid_jd();
    SolarRiseSetFastResult fast_rise_set;
    const Status fast_status = compute_solar_rise_set_fast_tt(
        context,
        best_jd,
        longitude_deg,
        latitude_deg,
        height_m,
        TAIYIN_SOLAR_VISIBILITY_LIMB_UPPER,
        0.0,
        fast_visibility_flags,
        &fast_rise_set,
        diagnostic);
    if (fast_status != TAIYIN_STATUS_OK
        && (flags & TAIYIN_ECLIPSE_LOCAL_REFRACTION) != 0u) {
        return fast_status;
    }
    if (fast_status == TAIYIN_STATUS_OK) {
        sun_s_jd = fast_rise_set.rise_jd_tt;
        sun_j_jd = fast_rise_set.set_jd_tt;
        out->kind &= ~(TAIYIN_ECLIPSE_VISIBLE_AT_OBSERVER | TAIYIN_ECLIPSE_MAXIMUM_VISIBLE);
        bool maximum_visible = false;
        if (fast_rise_set.altitude_state == TAIYIN_SOLAR_VISIBILITY_ALTITUDE_STATE_ALWAYS_ABOVE) {
            maximum_visible = true;
        } else if (fast_rise_set.altitude_state == TAIYIN_SOLAR_VISIBILITY_ALTITUDE_STATE_CROSSES) {
            maximum_visible =
                (!split_julian_date_is_finite(sun_s_jd) || best_jd >= sun_s_jd)
                && (!split_julian_date_is_finite(sun_j_jd) || best_jd <= sun_j_jd);
        }
        if (maximum_visible) {
            out->kind |= TAIYIN_ECLIPSE_VISIBLE_AT_OBSERVER;
        }
    }

    const bool needs_visibility_geometry = split_julian_date_is_finite(sun_s_jd) || split_julian_date_is_finite(sun_j_jd);
    double visibility_half_days = kContactWindowHalfDays;
    if (split_julian_date_is_finite(sun_s_jd)) {
        visibility_half_days = std::max(visibility_half_days, std::fabs(sun_s_jd - best_jd) + 5.0 / 1440.0);
    }
    if (split_julian_date_is_finite(sun_j_jd)) {
        visibility_half_days = std::max(visibility_half_days, std::fabs(sun_j_jd - best_jd) + 5.0 / 1440.0);
    }
    if (needs_visibility_geometry) {
        FastApparentCorrectionConfig visibility_config = correction_config;
        visibility_config.initial_half_days = visibility_half_days;
        FastApparentCorrectionEpochSample sample;
        correction_status = get_fast_correction(
            context,
            TAIYIN_BODY_MOON,
            TAIYIN_BODY_SUN,
            window_options,
            visibility_config,
            best_jd,
            active_corrections,
            diagnostic,
            &sample);
        if (correction_status != TAIYIN_STATUS_OK) return correction_status;
    }

    if (split_julian_date_is_finite(sun_s_jd)) {
        double s2, sr2, mr2, alt2, az2;
        Status s3 = eval_local_topocentric_geometry(
            context, sun_s_jd, flags, active_corrections, lon_rad, lat_rad, height_m,
            &s2, &sr2, &mr2, &alt2, &az2, diagnostic);
        if (s3 == TAIYIN_STATUS_OK) {
            out->sunrise_magnitude = solar_magnitude(s2, sr2, mr2);
            if (out->sunrise_magnitude < 0.0) out->sunrise_magnitude = 0.0;
        }
    }
    if (split_julian_date_is_finite(sun_j_jd)) {
        double s2, sr2, mr2, alt2, az2;
        Status s3 = eval_local_topocentric_geometry(
            context, sun_j_jd, flags, active_corrections, lon_rad, lat_rad, height_m,
            &s2, &sr2, &mr2, &alt2, &az2, diagnostic);
        if (s3 == TAIYIN_STATUS_OK) {
            out->sunset_magnitude = solar_magnitude(s2, sr2, mr2);
            if (out->sunset_magnitude < 0.0) out->sunset_magnitude = 0.0;
        }
    }

    if (split_julian_date_is_finite(sun_s_jd) || split_julian_date_is_finite(sun_j_jd)) {
        for (size_t ci = 0; ci < TAIYIN_LOCAL_SOLAR_CONTACT_COUNT; ++ci) {
            if (!split_julian_date_is_finite(out->contact_jd_tt[ci])) continue;
            bool below = false;
            if (split_julian_date_is_finite(sun_s_jd) && out->contact_jd_tt[ci] < sun_s_jd) below = true;
            if (split_julian_date_is_finite(sun_j_jd) && out->contact_jd_tt[ci] > sun_j_jd) below = true;
            if (below) out->contact_jd_tt[ci] = invalid_jd();
        }
        if (best_jd < sun_s_jd && out->sunrise_magnitude > 0.0) {
            out->magnitude = out->sunrise_magnitude;
            out->kind |= TAIYIN_ECLIPSE_MAXIMUM_VISIBLE;
        }
        if (best_jd > sun_j_jd && out->sunset_magnitude > 0.0) {
            out->magnitude = out->sunset_magnitude;
            out->kind |= TAIYIN_ECLIPSE_MAXIMUM_VISIBLE;
        }
    }
    if (!split_julian_date_is_finite(sun_s_jd) && !split_julian_date_is_finite(sun_j_jd)
        && (!std::isfinite(out->sun_altitude_deg) || out->sun_altitude_deg <= 0.0)
        && (!std::isfinite(c1_altitude_deg) || c1_altitude_deg <= 0.0)
        && (!std::isfinite(c4_altitude_deg) || c4_altitude_deg <= 0.0)) {
        init_local_solar_contacts(out);
        out->duration_seconds = 0.0;
    }
    return TAIYIN_STATUS_OK;
}

Status complete_local_solar_eclipse_contacts_from_max(
    const NativeCalcContext* context,
    double longitude_deg,
    double latitude_deg,
    double height_m,
    uint64_t flags,
    bool complete_visibility,
    LocalSolarEclipseResult* out,
    EphemerisEvalDiagnostic* diagnostic
) noexcept {
    return complete_local_solar_eclipse_contacts_from_max(
        context,
        longitude_deg,
        latitude_deg,
        height_m,
        flags,
        complete_visibility,
        nullptr,
        nullptr,
        out,
        diagnostic);
}

Status solve_local_solar_eclipse_at_tt_with_besselian_seed(
    const NativeCalcContext* context,
    SplitJulianDate jd_tt,
    double longitude_deg,
    double latitude_deg,
    double height_m,
    uint64_t flags,
    SplitJulianDate local_max_seed_jd_tt,
    const SolarBesselianPolynomial* besselian_seed,
    FastApparentCorrectionSeries* besselian_seed_corrections,
    bool complete_visibility,
    LocalSolarEclipseResult* out,
    EphemerisEvalDiagnostic* diagnostic
) noexcept {
    if (!context || !out || !split_julian_date_is_finite(jd_tt)
        || !valid_local_solar_eclipse_flags(flags)) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }

    std::memset(out, 0, sizeof(*out));
    init_local_solar_contacts(out);
    out->kind = TAIYIN_ECLIPSE_NONE;
    out->maximum_jd_tt = jd_tt;
    out->magnitude = std::nan("");
    out->obscuration = std::nan("");
    out->sun_altitude_deg = std::nan("");
    out->sun_azimuth_deg = std::nan("");

    const double lon_rad = longitude_deg * M_PI / 180.0;
    const double lat_rad = latitude_deg * M_PI / 180.0;
    SplitJulianDate best_jd = jd_tt;

    if (split_julian_date_is_finite(local_max_seed_jd_tt) && std::fabs(local_max_seed_jd_tt - jd_tt) <= 1.0) {
        best_jd = local_max_seed_jd_tt;
    } else {
        Status seed_status = seed_local_besselian_polynomial(
            context, jd_tt, lon_rad, lat_rad, height_m, besselian_seed, &best_jd, diagnostic);
        if (seed_status != TAIYIN_STATUS_OK) {
            return seed_status;
        }
    }

    FastApparentOptions window_options;
    window_options.frame = FAST_APPARENT_TRUE_EQUATOR_OF_DATE;
    window_options.with_velocity = false;
    window_options.true_position = (flags & TAIYIN_ECLIPSE_TRUEPOS) != 0;
    FastApparentCorrectionConfig correction_config;
    correction_config.initial_half_days = 3.0 / 24.0;
    correction_config.sample_step_days = 3.0 / 24.0;
    FastApparentCorrectionSeries window_series;
    FastApparentCorrectionSeries* active_corrections =
        besselian_seed_corrections ? besselian_seed_corrections : &window_series;
    FastApparentCorrectionEpochSample correction_sample;
    Status correction_status = TAIYIN_STATUS_OK;
    if (besselian_seed_corrections) {
        correction_status = get_fast_correction(
            context,
            TAIYIN_BODY_MOON,
            TAIYIN_BODY_SUN,
            window_options,
            correction_config,
            best_jd,
            active_corrections,
            diagnostic,
            &correction_sample);
    } else {
        correction_status = init_fast_correction_series(
            context,
            TAIYIN_BODY_MOON,
            TAIYIN_BODY_SUN,
            window_options,
            correction_config,
            best_jd,
            active_corrections,
            diagnostic);
    }
    if (correction_status != TAIYIN_STATUS_OK) return correction_status;

    Status refine_status = refine_local_sxwnl_magnitude_maximum(
        context, &best_jd, flags, lon_rad, lat_rad, height_m,
        active_corrections, 1.0 / 96.0, 10, diagnostic);
    if (refine_status != TAIYIN_STATUS_OK) return refine_status;
    correction_status = get_fast_correction(
        context,
        TAIYIN_BODY_MOON,
        TAIYIN_BODY_SUN,
        window_options,
        correction_config,
        best_jd,
        active_corrections,
        diagnostic,
        &correction_sample);
    if (correction_status != TAIYIN_STATUS_OK) return correction_status;

    double separation, sun_radius, moon_radius, sun_alt, sun_az;
    const Status st = eval_local_topocentric_geometry(
        context, best_jd, flags, active_corrections, lon_rad, lat_rad, height_m,
        &separation, &sun_radius, &moon_radius, &sun_alt, &sun_az, diagnostic);
    if (st != TAIYIN_STATUS_OK) return st;

    out->maximum_jd_tt = best_jd;
    out->magnitude = solar_magnitude(separation, sun_radius, moon_radius);
    out->obscuration = solar_obscuration(separation, sun_radius, moon_radius);
    out->sun_altitude_deg = sun_alt;
    out->sun_azimuth_deg = sun_az;
    out->moon_sun_radius_ratio = sun_radius > 0.0 ? moon_radius / sun_radius : 1.0;
    out->position_angle_c1_deg = std::nan("");
    out->position_angle_c4_deg = std::nan("");
    out->vertex_angle_c1_deg = std::nan("");
    out->vertex_angle_c4_deg = std::nan("");
    out->sunrise_magnitude = 0.0;
    out->sunset_magnitude = 0.0;
    out->duration_seconds = 0.0;
    if (out->magnitude > 0.0) {
        out->kind = TAIYIN_ECLIPSE_PARTIAL;
        if (separation <= std::fabs(moon_radius - sun_radius)) {
            bool saw_total = moon_radius >= sun_radius;
            bool saw_annular = moon_radius < sun_radius;
            const SplitJulianDate type_samples[] = {
                best_jd - 1.0 / 24.0,
                best_jd + 1.0 / 24.0
            };
            for (size_t i = 0; i < sizeof(type_samples) / sizeof(type_samples[0]); ++i) {
                double sample_separation, sample_sr, sample_mr, sample_alt, sample_az;
                const Status sample_status = eval_local_topocentric_geometry(
                    context, type_samples[i], flags, active_corrections, lon_rad, lat_rad, height_m,
                    &sample_separation, &sample_sr, &sample_mr, &sample_alt, &sample_az, diagnostic);
                if (sample_status != TAIYIN_STATUS_OK) return sample_status;
                if (sample_separation <= std::fabs(sample_mr - sample_sr)) {
                    if (sample_mr >= sample_sr) saw_total = true;
                    else saw_annular = true;
                }
            }
            if (saw_total && saw_annular) {
                out->kind = TAIYIN_ECLIPSE_HYBRID;
            } else if (moon_radius >= sun_radius) {
                out->kind = TAIYIN_ECLIPSE_TOTAL;
            } else {
                out->kind = TAIYIN_ECLIPSE_ANNULAR;
            }
        }
        if (sun_alt > 0.0) out->kind |= TAIYIN_ECLIPSE_VISIBLE_AT_OBSERVER;
    }
    return complete_local_solar_eclipse_contacts_from_max(
        context,
        longitude_deg,
        latitude_deg,
        height_m,
        flags,
        complete_visibility,
        active_corrections,
        besselian_seed,
        out,
        diagnostic);
}

Status solve_local_solar_eclipse_at_tt(
    const NativeCalcContext* context,
    SplitJulianDate jd_tt,
    uint64_t flags,
    LocalSolarEclipseResult* out,
    EphemerisEvalDiagnostic* diagnostic
) noexcept {
    double longitude_deg = 0.0;
    double latitude_deg = 0.0;
    double height_m = 0.0;
    if (!context
        || !out
        || !valid_local_solar_eclipse_flags(flags)
        || !native_context_observer_degrees(*context, &longitude_deg, &latitude_deg, &height_m)) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    NativeCalcContext local;
    Status status = native_context_copy_geocentric_with_observer(*context, &local);
    if (status != TAIYIN_STATUS_OK) return status;
    return solve_local_solar_eclipse_at_tt_with_besselian_seed(
        &local, jd_tt, longitude_deg, latitude_deg, height_m,
        flags, invalid_jd(), nullptr, nullptr, true, out, diagnostic);
}

Status solve_local_solar_eclipse_at_ut(
    const NativeCalcContext* context,
    SplitJulianDate jd_ut,
    uint64_t flags,
    LocalSolarEclipseResultUt* out,
    EphemerisEvalDiagnostic* diagnostic
) noexcept {
    if (!context
        || !out
        || !split_julian_date_is_finite(jd_ut)
        || !valid_local_solar_eclipse_flags(flags)
        || !native_context_has_observer_location(*context)) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }

    std::memset(out, 0, sizeof(*out));
    init_local_solar_contacts(out);
    LocalSolarEclipseResult tt_out;
    SplitJulianDate jd_tt;
    Status st = eclipse_ut_to_tt(*context, jd_ut, &jd_tt, nullptr, diagnostic);
    if (st != TAIYIN_STATUS_OK) return st;
    st = solve_local_solar_eclipse_at_tt(
        context, jd_tt, flags, &tt_out, diagnostic);
    if (st != TAIYIN_STATUS_OK) return st;

    out->kind = tt_out.kind;
    st = eclipse_tt_to_ut(*context, tt_out.maximum_jd_tt, &out->maximum_jd_ut, &out->delta_t_seconds, diagnostic);
    if (st != TAIYIN_STATUS_OK) return st;
    out->magnitude = tt_out.magnitude;
    out->obscuration = tt_out.obscuration;
    out->sun_altitude_deg = tt_out.sun_altitude_deg;
    out->sun_azimuth_deg = tt_out.sun_azimuth_deg;
    out->position_angle_c1_deg = tt_out.position_angle_c1_deg;
    out->position_angle_c4_deg = tt_out.position_angle_c4_deg;
    out->vertex_angle_c1_deg = tt_out.vertex_angle_c1_deg;
    out->vertex_angle_c4_deg = tt_out.vertex_angle_c4_deg;
    out->sunrise_magnitude = tt_out.sunrise_magnitude;
    out->sunset_magnitude = tt_out.sunset_magnitude;
    out->duration_seconds = tt_out.duration_seconds;
    out->moon_sun_radius_ratio = tt_out.moon_sun_radius_ratio;
    for (size_t i = 0; i < TAIYIN_LOCAL_SOLAR_CONTACT_COUNT; ++i) {
        if (split_julian_date_is_finite(tt_out.contact_jd_tt[i])) {
            st = eclipse_tt_to_ut(*context, tt_out.contact_jd_tt[i], &out->contact_jd_ut[i], nullptr, diagnostic);
            if (st != TAIYIN_STATUS_OK) return st;
        }
    }
    return TAIYIN_STATUS_OK;
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
) noexcept {
    if (!context || !out || !split_julian_date_is_finite(jd_tt)) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    std::memset(out, 0, sizeof(*out));
    out->jd_tt = jd_tt;

    double separation, sun_radius, moon_radius, sun_alt, sun_az;
    Status st = eval_local_topocentric_geometry(
        context, jd_tt,
        flags,
        corrections,
        longitude_deg * M_PI / 180.0,
        latitude_deg * M_PI / 180.0,
        height_m,
        &separation, &sun_radius, &moon_radius,
        &sun_alt, &sun_az, diagnostic);
    if (st != TAIYIN_STATUS_OK) return st;

    out->center_separation_deg = separation;
    out->sun_angular_radius_deg = sun_radius;
    out->moon_angular_radius_deg = moon_radius;
    out->magnitude = solar_magnitude(separation, sun_radius, moon_radius);
    out->obscuration = solar_obscuration(separation, sun_radius, moon_radius);
    out->sun_altitude_deg = sun_alt;
    out->sun_azimuth_deg = sun_az;
    return TAIYIN_STATUS_OK;
}

Status compute_local_solar_circumstances_tt(
    const NativeCalcContext* context,
    SplitJulianDate jd_tt,
    LocalSolarEclipseCircumstances* out,
    EphemerisEvalDiagnostic* diagnostic
) noexcept {
    double longitude_deg = 0.0;
    double latitude_deg = 0.0;
    double height_m = 0.0;
    if (!context || !out
        || !native_context_observer_degrees(*context, &longitude_deg, &latitude_deg, &height_m)) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    NativeCalcContext local;
    Status status = native_context_copy_geocentric_with_observer(*context, &local);
    if (status != TAIYIN_STATUS_OK) return status;
    return compute_local_solar_circumstances_tt_with_options(
        &local, jd_tt, longitude_deg, latitude_deg, height_m, 0, nullptr, out, diagnostic);
}

Status compute_local_solar_circumstances_ut(
    const NativeCalcContext* context,
    SplitJulianDate jd_ut,
    LocalSolarEclipseCircumstancesUt* out,
    EphemerisEvalDiagnostic* diagnostic
) noexcept {
    if (!context || !out || !split_julian_date_is_finite(jd_ut) || !native_context_has_observer_location(*context)) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    std::memset(out, 0, sizeof(*out));

    SplitJulianDate jd_tt;
    double delta_t_seconds = 0.0;
    Status time_status = eclipse_ut_to_tt(*context, jd_ut, &jd_tt, &delta_t_seconds, diagnostic);
    if (time_status != TAIYIN_STATUS_OK) return time_status;
    LocalSolarEclipseCircumstances tt_out;
    Status st = compute_local_solar_circumstances_tt(
        context, jd_tt, &tt_out, diagnostic);
    if (st != TAIYIN_STATUS_OK) return st;

    out->jd_ut = jd_ut;
    out->delta_t_seconds = delta_t_seconds;
    out->magnitude = tt_out.magnitude;
    out->obscuration = tt_out.obscuration;
    out->center_separation_deg = tt_out.center_separation_deg;
    out->sun_angular_radius_deg = tt_out.sun_angular_radius_deg;
    out->moon_angular_radius_deg = tt_out.moon_angular_radius_deg;
    out->sun_altitude_deg = tt_out.sun_altitude_deg;
    out->sun_azimuth_deg = tt_out.sun_azimuth_deg;
    return TAIYIN_STATUS_OK;
}

namespace {

struct ConeVertex {
    double longitude_rad;
    double latitude_rad;
    double radius;
};

Status compute_cone_vertices(
    const NativeCalcContext* context,
    SplitJulianDate jd_tt,
    ConeVertex* umbra_vertex,
    ConeVertex* penumbra_vertex,
    EphemerisEvalDiagnostic* diagnostic
) noexcept {
    double moon_km[3] = {};
    double sun_km[3] = {};
    Status st = eval_solar_equatorial_vectors_km(
        context, jd_tt, 0, nullptr, moon_km, sun_km, diagnostic);
    if (st != TAIYIN_STATUS_OK) return st;

    const double k_ratio = kSunRadiusKm / (kMoonAlmanacRadiusRatio * kEarthEquatorialRadiusKm);

    double sun_xyz[3] = {
        sun_km[0], sun_km[1], sun_km[2]
    };
    double moon_xyz[3] = {
        moon_km[0], moon_km[1], moon_km[2]
    };

    double dx = sun_xyz[0] - moon_xyz[0];
    double dy = sun_xyz[1] - moon_xyz[1];
    double dz = sun_xyz[2] - moon_xyz[2];

    {
        const double fx = dx / (1.0 - k_ratio) + moon_xyz[0];
        const double fy = dy / (1.0 - k_ratio) + moon_xyz[1];
        const double fz = dz / (1.0 - k_ratio) + moon_xyz[2];
        umbra_vertex->longitude_rad = std::atan2(fy, fx);
        umbra_vertex->latitude_rad = std::atan2(fz, std::hypot(fx, fy));
        umbra_vertex->radius = std::sqrt(fx * fx + fy * fy + fz * fz);
    }
    {
        const double fx = dx / (1.0 + k_ratio) + moon_xyz[0];
        const double fy = dy / (1.0 + k_ratio) + moon_xyz[1];
        const double fz = dz / (1.0 + k_ratio) + moon_xyz[2];
        penumbra_vertex->longitude_rad = std::atan2(fy, fx);
        penumbra_vertex->latitude_rad = std::atan2(fz, std::hypot(fx, fy));
        penumbra_vertex->radius = std::sqrt(fx * fx + fy * fy + fz * fz);
    }
    return TAIYIN_STATUS_OK;
}

struct LocalXYState {
    double x_deg;
    double y_deg;
    double sun_ra_rad;
    double sun_dec_rad;
    double sun_radius_deg;
    double moon_radius_deg;
    double sun_dist;
};

Status eval_local_rspl_xy(
    const NativeCalcContext* context,
    SplitJulianDate jd_tt,
    double longitude_rad,
    double latitude_rad,
    LocalXYState* out,
    EphemerisEvalDiagnostic* diagnostic
) noexcept {
    double x, y, sr, mr, alt, az, sra, sdec, mra, mdec;
    Status st = eval_local_topocentric_xy(
        context, jd_tt, 0, nullptr, longitude_rad, latitude_rad, 0.0,
        &x, &y, &sr, &mr, &alt, &az, &sra, &sdec, &mra, &mdec, diagnostic);
    if (st != TAIYIN_STATUS_OK) return st;
    out->x_deg = x;
    out->y_deg = y;
    out->sun_ra_rad = sra;
    out->sun_dec_rad = sdec;
    out->sun_radius_deg = sr;
    out->moon_radius_deg = mr;
    out->sun_dist = 0.0;
    return TAIYIN_STATUS_OK;
}

}  // namespace

Status probe_local_solar_eclipse_for_search(
    const NativeCalcContext* context,
    SplitJulianDate jd_seed_tt,
    double longitude_deg,
    double latitude_deg,
    double height_m,
    uint64_t flags,
    bool central_only,
    bool* out_possible,
    SplitJulianDate* out_best_jd_tt,
    EphemerisEvalDiagnostic* diagnostic
) noexcept {
    if (!context || !out_possible || !out_best_jd_tt || !split_julian_date_is_finite(jd_seed_tt)) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }

    *out_possible = true;
    *out_best_jd_tt = jd_seed_tt;

    const double lon_rad = longitude_deg * M_PI / 180.0;
    const double lat_rad = latitude_deg * M_PI / 180.0;
    LocalSolarProbeTable table;
    Status seed_status = build_local_solar_probe_table(
        context,
        jd_seed_tt,
        flags,
        lon_rad,
        lat_rad,
        height_m,
        &table,
        diagnostic);
    if (seed_status == TAIYIN_ERROR_UNSUPPORTED) {
        return TAIYIN_STATUS_OK;
    }
    if (seed_status != TAIYIN_STATUS_OK) {
        return seed_status;
    }

    SplitJulianDate best_jd = jd_seed_tt;
    seed_status = seed_local_solar_probe_table_maximum(
        table,
        &best_jd);
    if (seed_status == TAIYIN_ERROR_UNSUPPORTED) {
        return TAIYIN_STATUS_OK;
    }
    if (seed_status != TAIYIN_STATUS_OK) {
        return seed_status;
    }

    double separation = 0.0;
    double sun_radius = 0.0;
    double moon_radius = 0.0;
    const Status geometry_status = eval_local_topocentric_separation_radii(
        context,
        best_jd,
        flags,
        nullptr,
        lon_rad,
        lat_rad,
        height_m,
        false,
        &separation,
        &sun_radius,
        &moon_radius,
        diagnostic);
    if (geometry_status != TAIYIN_STATUS_OK) {
        return geometry_status;
    }

    *out_best_jd_tt = best_jd;
    constexpr double kGuardDeg = 0.03;
    const double contact_radius = central_only
        ? std::fabs(moon_radius - sun_radius)
        : moon_radius + sun_radius;
    *out_possible = separation <= contact_radius + kGuardDeg;
    return TAIYIN_STATUS_OK;
}

Status compute_local_solar_eclipse_boundary_tt(
    const NativeCalcContext* context,
    SplitJulianDate jd_tt,
    double longitude_deg,
    double latitude_deg,
    LocalSolarEclipseBoundary* out,
    EphemerisEvalDiagnostic* diagnostic
) noexcept {
    if (!context || !out || !split_julian_date_is_finite(jd_tt)) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    std::memset(out, 0, sizeof(*out));
    out->center_latitude_deg = std::nan("");
    out->center_longitude_deg = std::nan("");
    out->center_kind = TAIYIN_ECLIPSE_NONE;
    out->umbra_north_latitude_deg = std::nan("");
    out->umbra_north_longitude_deg = std::nan("");
    out->umbra_south_latitude_deg = std::nan("");
    out->umbra_south_longitude_deg = std::nan("");
    out->penumbra_north_latitude_deg = std::nan("");
    out->penumbra_north_longitude_deg = std::nan("");
    out->penumbra_south_latitude_deg = std::nan("");
    out->penumbra_south_longitude_deg = std::nan("");
    out->umbra_width_km = std::nan("");

    const double lon_rad = longitude_deg * M_PI / 180.0;
    const double lat_rad = latitude_deg * M_PI / 180.0;

    ConeVertex umbra_vertex, penumbra_vertex;
    Status st = compute_cone_vertices(context, jd_tt, &umbra_vertex, &penumbra_vertex, diagnostic);
    if (st != TAIYIN_STATUS_OK) return st;

    sxwnl::solar::BesselianFrame I;
    sxwnl::solar::Vec3 M;
    st = sxwnl_bse_local(context, jd_tt, &I, diagnostic);
    if (st != TAIYIN_STATUS_OK) return st;
    st = sxwnl_bseM_local(context, jd_tt, &M, diagnostic);
    if (st != TAIYIN_STATUS_OK) return st;

    {
        sxwnl::solar::GeoPoint center = sxwnl::solar::bseXY2db(M.x, M.y, I, true);
        if (center.valid) {
            out->center_longitude_deg = center.longitude_rad * 180.0 / M_PI;
            out->center_latitude_deg = center.latitude_rad * 180.0 / M_PI;
            LocalXYState ls;
            Status s2 = eval_local_rspl_xy(context, jd_tt, lon_rad, lat_rad, &ls, diagnostic);
            if (s2 == TAIYIN_STATUS_OK) {
                if (ls.moon_radius_deg >= ls.sun_radius_deg) {
                    out->center_kind = TAIYIN_ECLIPSE_TOTAL;
                } else {
                    out->center_kind = TAIYIN_ECLIPSE_ANNULAR;
                }
            }
        }
    }

    const double earth_axis_ratio = TAIYIN_WGS84_B_KM / TAIYIN_WGS84_A_KM;
    const double dt_step = 0.04;
    sxwnl::solar::Vec3 before, after;
    st = sxwnl_bseM_local(context, jd_tt - dt_step, &before, diagnostic);
    if (st != TAIYIN_STATUS_OK) return st;
    st = sxwnl_bseM_local(context, jd_tt + dt_step, &after, diagnostic);
    if (st != TAIYIN_STATUS_OK) return st;
    const double vx = (after.x - before.x) / (2.0 * dt_step);
    const double vy = (after.y - before.y) / (2.0 * dt_step);

    sxwnl::solar::Vec3 sun_llr, moon_llr;
    st = body_equatorial_llr_km_local(context, TAIYIN_BODY_SUN, jd_tt, &sun_llr, diagnostic);
    if (st != TAIYIN_STATUS_OK) return st;
    st = body_equatorial_llr_km_local(context, TAIYIN_BODY_MOON, jd_tt, &moon_llr, diagnostic);
    if (st != TAIYIN_STATUS_OK) return st;
    const double dyj = (sun_llr.z - moon_llr.z) / (TAIYIN_WGS84_A_KM);
    const sxwnl::solar::ShadowRadii shadow = sxwnl::solar::rSM(
        M.z,
        kSxwnlMoonPenumbralRadiusRatio,
        kSxwnlMoonUmbralRadiusRatio,
        kSxwnlSunRadiusRatio,
        (kSxwnlSunRadiusRatio + kSxwnlMoonPenumbralRadiusRatio) / dyj,
        (kSxwnlSunRadiusRatio - kSxwnlMoonUmbralRadiusRatio) / dyj,
        dyj
    );

    auto fill_boundary = [&](double radius, int side, double* out_lon_deg, double* out_lat_deg) -> Status {
        const sxwnl::solar::BoundaryPoint p = sxwnl::solar::nanbei(
            M.x, M.y, M.z, vx, vy, side, radius, I,
            kSxwnlMoonPenumbralRadiusRatio, earth_axis_ratio);
        if (!p.valid) return TAIYIN_STATUS_OK;
        *out_lon_deg = p.longitude_rad * 180.0 / M_PI;
        *out_lat_deg = p.latitude_rad * 180.0 / M_PI;
        return TAIYIN_STATUS_OK;
    };

    st = fill_boundary(shadow.r2, +1, &out->umbra_north_longitude_deg, &out->umbra_north_latitude_deg);
    if (st != TAIYIN_STATUS_OK) return st;
    st = fill_boundary(shadow.r2, -1, &out->umbra_south_longitude_deg, &out->umbra_south_latitude_deg);
    if (st != TAIYIN_STATUS_OK) return st;
    st = fill_boundary(shadow.r1, +1, &out->penumbra_north_longitude_deg, &out->penumbra_north_latitude_deg);
    if (st != TAIYIN_STATUS_OK) return st;
    st = fill_boundary(shadow.r1, -1, &out->penumbra_south_longitude_deg, &out->penumbra_south_latitude_deg);
    if (st != TAIYIN_STATUS_OK) return st;

    if (std::isfinite(out->umbra_north_latitude_deg) && std::isfinite(out->umbra_south_latitude_deg)) {
        const double dlon = (out->umbra_north_longitude_deg - out->umbra_south_longitude_deg) * M_PI / 180.0;
        const double dlat = (out->umbra_north_latitude_deg - out->umbra_south_latitude_deg) * M_PI / 180.0;
        const double avg_lat = (out->umbra_north_latitude_deg + out->umbra_south_latitude_deg) / 2.0 * M_PI / 180.0;
        out->umbra_width_km = TAIYIN_WGS84_A_KM * std::sqrt(
            dlon * dlon * std::cos(avg_lat) * std::cos(avg_lat) + dlat * dlat);
    }

    return TAIYIN_STATUS_OK;
}

Status compute_local_solar_eclipse_boundary_ut(
    const NativeCalcContext* context,
    SplitJulianDate jd_ut,
    double longitude_deg,
    double latitude_deg,
    LocalSolarEclipseBoundary* out,
    EphemerisEvalDiagnostic* diagnostic
) noexcept {
    if (!context || !out || !split_julian_date_is_finite(jd_ut)) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    SplitJulianDate jd_tt;
    const Status st = eclipse_ut_to_tt(*context, jd_ut, &jd_tt, nullptr, diagnostic);
    if (st != TAIYIN_STATUS_OK) return st;
    return compute_local_solar_eclipse_boundary_tt(context, jd_tt, longitude_deg, latitude_deg, out, diagnostic);
}

}  // namespace runtime
}  // namespace taiyin
