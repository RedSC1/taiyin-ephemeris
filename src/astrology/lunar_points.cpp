#include "taiyin/astrology/lunar_points.h"

#include "generated/lunar_apogee_de441_fit.h"

#include "taiyin/angle.h"
#include "taiyin/apparent_position.h"
#include "taiyin/body_id.h"
#include "taiyin/dispatch.h"
#include "taiyin/physical_constants.h"
#include "taiyin/time.h"
#include "taiyin/vector3.h"

#include <cmath>
#include <limits>

namespace taiyin {
namespace astrology {
namespace {

const uint32_t kAllowedPositionFlags = runtime::TAIYIN_NATIVE_POSITION_TRUEPOS
    | runtime::TAIYIN_NATIVE_POSITION_EQUATORIAL
    | runtime::TAIYIN_NATIVE_POSITION_NO_ABERR
    | runtime::TAIYIN_NATIVE_POSITION_NO_GDEFL
    | runtime::TAIYIN_NATIVE_POSITION_ASTROMETRIC
    | runtime::TAIYIN_NATIVE_POSITION_NONUT;

const uint32_t kAllowedMeanFlags = runtime::TAIYIN_NATIVE_POSITION_EQUATORIAL
    | runtime::TAIYIN_NATIVE_POSITION_NONUT;

const int kDelaunayReferenceFrame = TAIYIN_APPARENT_FRAME_MEAN_ECLIPTIC_OF_DATE;
const double kJ2000Jd = 2451545.0;
const double kJulianCenturyDays = 36525.0;

SplitJulianDate model_jd(double jd) noexcept {
    SplitJulianDate result(0, NAN);
    split_julian_date_from_double(jd, &result);
    return result;
}
// Conventional mean lunar inclination to the ecliptic. The Delaunay
// arguments define the phase; this constant defines the mean orbital plane
// used for the explicit Delaunay mean-apogee direction below.
const double kMeanLunarInclinationRad = 5.145396 * TAIYIN_DEG_TO_RAD;

struct AngularArgument {
    double value_rad;
    double rate_rad_per_day;
};

bool valid_node_kind(LunarNodeKind kind) noexcept {
    return kind == TAIYIN_LUNAR_NODE_ASCENDING
        || kind == TAIYIN_LUNAR_NODE_DESCENDING;
}

bool applies_annual_aberration(
    const runtime::NativeCalcContext& context,
    uint32_t flags
) noexcept {
    if ((flags & (runtime::TAIYIN_NATIVE_POSITION_TRUEPOS
            | runtime::TAIYIN_NATIVE_POSITION_ASTROMETRIC
            | runtime::TAIYIN_NATIVE_POSITION_NO_ABERR)) != 0u) {
        return false;
    }
    return (context.apparent_options.flags & TAIYIN_APPARENT_ABERRATION) != 0u;
}

bool is_supported_reference_frame(int frame_id) noexcept {
    return frame_id == TAIYIN_APPARENT_FRAME_ICRF
        || frame_id == TAIYIN_APPARENT_FRAME_J2000_MEAN_EQUATOR
        || frame_id == TAIYIN_APPARENT_FRAME_J2000_ECLIPTIC
        || frame_id == TAIYIN_APPARENT_FRAME_MEAN_EQUATOR_OF_DATE
        || frame_id == TAIYIN_APPARENT_FRAME_TRUE_EQUATOR_OF_DATE
        || frame_id == TAIYIN_APPARENT_FRAME_MEAN_ECLIPTIC_OF_DATE
        || frame_id == TAIYIN_APPARENT_FRAME_TRUE_ECLIPTIC_OF_DATE
        || frame_id == TAIYIN_APPARENT_FRAME_CIRS
        || frame_id == TAIYIN_APPARENT_FRAME_CUSTOM;
}

int without_nutation_reference_frame(int frame_id) noexcept {
    switch (frame_id) {
    case TAIYIN_APPARENT_FRAME_TRUE_EQUATOR_OF_DATE:
        return TAIYIN_APPARENT_FRAME_MEAN_EQUATOR_OF_DATE;
    case TAIYIN_APPARENT_FRAME_TRUE_ECLIPTIC_OF_DATE:
        return TAIYIN_APPARENT_FRAME_MEAN_ECLIPTIC_OF_DATE;
    case TAIYIN_APPARENT_FRAME_CIRS:
        return -1;
    default:
        return frame_id;
    }
}

int effective_reference_frame(
    const runtime::NativeCalcContext& context,
    uint32_t flags
) noexcept {
    if ((flags & runtime::TAIYIN_NATIVE_POSITION_EQUATORIAL) != 0u) {
        return (flags & runtime::TAIYIN_NATIVE_POSITION_NONUT) != 0u
            ? TAIYIN_APPARENT_FRAME_MEAN_EQUATOR_OF_DATE
            : TAIYIN_APPARENT_FRAME_TRUE_EQUATOR_OF_DATE;
    }
    return (flags & runtime::TAIYIN_NATIVE_POSITION_NONUT) != 0u
        ? without_nutation_reference_frame(context.apparent_options.output_frame_id)
        : context.apparent_options.output_frame_id;
}

bool resolve_jd_tt(
    const runtime::NativeCalcContext& context,
    SplitJulianDate jd,
    bool use_ut,
    SplitJulianDate* out_jd_tt
) noexcept {
    if (!out_jd_tt || !split_julian_date_is_finite(jd)) return false;
    SplitJulianDate jd_tt = jd;
    if (use_ut) {
        const double delta_t = dispatch::eval_delta_t_with_ephemeris_correction(
            context.delta_t_model_id, context.ephemeris_family_id, jd, nullptr, nullptr);
        if (!ut1_to_tt_split_jd(jd, delta_t, &jd_tt)) return false;
    }
    if (!split_julian_date_is_finite(jd_tt)) return false;
    *out_jd_tt = jd_tt;
    return true;
}

AngularArgument eval_iers_2003_argument_raw(
    SplitJulianDate jd_tt,
    double c0_arcsec,
    double c1_arcsec_per_century,
    double c2_arcsec_per_century2,
    double c3_arcsec_per_century3,
    double c4_arcsec_per_century4
) noexcept {
    const double t = (jd_tt - model_jd(kJ2000Jd)) / kJulianCenturyDays;
    const double value_arcsec = c0_arcsec + t * (c1_arcsec_per_century
        + t * (c2_arcsec_per_century2 + t * (c3_arcsec_per_century3
            + t * c4_arcsec_per_century4)));
    const double derivative_arcsec_per_century = c1_arcsec_per_century
        + t * (2.0 * c2_arcsec_per_century2 + t * (3.0 * c3_arcsec_per_century3
            + t * 4.0 * c4_arcsec_per_century4));
    return AngularArgument{
        value_arcsec * TAIYIN_ARCSEC_TO_RAD,
        derivative_arcsec_per_century * TAIYIN_ARCSEC_TO_RAD / kJulianCenturyDays,
    };
}

AngularArgument eval_iers_2003_argument(
    SplitJulianDate jd_tt,
    double c0_arcsec,
    double c1_arcsec_per_century,
    double c2_arcsec_per_century2,
    double c3_arcsec_per_century3,
    double c4_arcsec_per_century4
) noexcept {
    AngularArgument result = eval_iers_2003_argument_raw(
        jd_tt,
        c0_arcsec,
        c1_arcsec_per_century,
        c2_arcsec_per_century2,
        c3_arcsec_per_century3,
        c4_arcsec_per_century4);
    result.value_rad = normalize_radians(result.value_rad);
    return result;
}

AngularArgument iers_2003_mean_elongation(SplitJulianDate jd_tt) noexcept {
    return eval_iers_2003_argument(
        jd_tt, 1072260.703692, 1602961601.2090, -6.3706, 0.006593, -0.00003169);
}

AngularArgument iers_2003_mean_solar_anomaly(SplitJulianDate jd_tt) noexcept {
    return eval_iers_2003_argument(
        jd_tt, 1287104.793048, 129596581.0481, -0.5532, 0.000136, -0.00001149);
}

AngularArgument iers_2003_mean_lunar_anomaly(SplitJulianDate jd_tt) noexcept {
    return eval_iers_2003_argument(
        jd_tt, 485868.249036, 1717915923.2178, 31.8792, 0.051635, -0.00024470);
}

AngularArgument iers_2003_unwrapped_mean_lunar_anomaly(SplitJulianDate jd_tt) noexcept {
    return eval_iers_2003_argument_raw(
        jd_tt, 485868.249036, 1717915923.2178, 31.8792, 0.051635, -0.00024470);
}

AngularArgument iers_2003_mean_lunar_argument_of_latitude(SplitJulianDate jd_tt) noexcept {
    return eval_iers_2003_argument(
        jd_tt, 335779.526232, 1739527262.8478, -12.7512, -0.001037, 0.00000417);
}

AngularArgument iers_2003_mean_lunar_node(SplitJulianDate jd_tt) noexcept {
    return eval_iers_2003_argument(
        jd_tt, 450160.398036, -6962890.5431, 7.4722, 0.007702, -0.00005939);
}

bool finite_vector(const Vector3& value) noexcept {
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

Vector3 matrix_multiply_vector(const double matrix[9], const Vector3& vector) noexcept {
    return Vector3{
        matrix[0] * vector.x + matrix[1] * vector.y + matrix[2] * vector.z,
        matrix[3] * vector.x + matrix[4] * vector.y + matrix[5] * vector.z,
        matrix[6] * vector.x + matrix[7] * vector.y + matrix[8] * vector.z,
    };
}

Vector3 matrix_transpose_multiply_vector(
    const double matrix[9],
    const Vector3& vector
) noexcept {
    return Vector3{
        matrix[0] * vector.x + matrix[3] * vector.y + matrix[6] * vector.z,
        matrix[1] * vector.x + matrix[4] * vector.y + matrix[7] * vector.z,
        matrix[2] * vector.x + matrix[5] * vector.y + matrix[8] * vector.z,
    };
}

Status eval_ecliptic_matrices(
    const runtime::NativeCalcContext& context,
    SplitJulianDate jd_tt,
    int frame_id,
    double out_matrix[9],
    double out_matrix_dot[9]
) noexcept {
    if (!calc_apparent_matrices(
            jd_tt,
            TAIYIN_APPARENT_VELOCITY,
            frame_id,
            context.model_context.precession_model_id,
            context.model_context.nutation_model_id,
            context.model_context.obliquity_model_id,
            context.model_context.frame_route_id,
            context.apparent_options.celestial_pole_offset_dx_rad,
            context.apparent_options.celestial_pole_offset_dy_rad,
            context.apparent_options.celestial_pole_offset_dx_rate_rad_per_day,
            context.apparent_options.celestial_pole_offset_dy_rate_rad_per_day,
            context.apparent_options.matrix_derivative_step_days,
            nullptr,
            nullptr,
            out_matrix,
            out_matrix_dot,
            nullptr,
            nullptr,
            nullptr,
            nullptr,
            nullptr,
            context.apparent_options.custom_output_frame_evaluator,
            context.apparent_options.custom_output_frame_data)) {
        return TAIYIN_ERROR_UNSUPPORTED;
    }
    return TAIYIN_STATUS_OK;
}

Status eval_corrected_geocentric_moon_state(
    const runtime::NativeCalcContext& context,
    SplitJulianDate jd,
    bool use_ut,
    uint32_t native_position_flags,
    CartesianState* out,
    runtime::EphemerisEvalDiagnostic* diagnostic
) noexcept {
    if (!out) return TAIYIN_ERROR_INVALID_ARGUMENT;
    runtime::NativeCalcContext geocentric_context = context;
    // An Earth-relative state preserves the existing geocentric light-time
    // derivatives. Annual aberration additionally needs Earth's inertial
    // velocity, which is available when the common center is the SSB.
    const int observer_center_id = applies_annual_aberration(context, native_position_flags)
        ? TAIYIN_BODY_SOLAR_SYSTEM_BARYCENTER
        : TAIYIN_BODY_EARTH;
    const Status observer_status = runtime::native_context_set_geocentric_observer(
        &geocentric_context,
        TAIYIN_BODY_EARTH,
        observer_center_id);
    if (observer_status != TAIYIN_STATUS_OK) return observer_status;
    geocentric_context.apparent_options.output_frame_id = TAIYIN_APPARENT_FRAME_ICRF;
    const uint32_t state_flags = native_position_flags
        & ~runtime::TAIYIN_NATIVE_POSITION_EQUATORIAL;
    const Status status = use_ut
        ? runtime::calc_state_ut(
            &geocentric_context, TAIYIN_BODY_MOON, jd, state_flags, out, diagnostic)
        : runtime::calc_state_tt(
            &geocentric_context, TAIYIN_BODY_MOON, jd, state_flags, out, diagnostic);
    if (status != TAIYIN_STATUS_OK) return status;
    return finite_vector(out->position_au) && finite_vector(out->velocity_au_per_day)
        && finite_vector(out->acceleration_au_per_day2)
        ? TAIYIN_STATUS_OK
        : TAIYIN_EPHEMERIS_ERROR_EVAL_FAILED;
}

Status transform_from_mean_ecliptic_of_date(
    const runtime::NativeCalcContext& context,
    SplitJulianDate jd_tt,
    int destination_frame_id,
    const Vector3& position_in_mean_ecliptic,
    const Vector3& velocity_in_mean_ecliptic_per_day,
    Vector3* out_position,
    Vector3* out_velocity_per_day
) noexcept {
    if (!out_position || !out_velocity_per_day) return TAIYIN_ERROR_INVALID_ARGUMENT;
    double base_matrix[9];
    double base_matrix_dot[9];
    double destination_matrix[9];
    double destination_matrix_dot[9];
    Status status = eval_ecliptic_matrices(
        context, jd_tt, kDelaunayReferenceFrame, base_matrix, base_matrix_dot);
    if (status != TAIYIN_STATUS_OK) return status;
    status = eval_ecliptic_matrices(
        context, jd_tt, destination_frame_id, destination_matrix, destination_matrix_dot);
    if (status != TAIYIN_STATUS_OK) return status;

    const Vector3 position_in_icrf = matrix_transpose_multiply_vector(
        base_matrix, position_in_mean_ecliptic);
    const Vector3 velocity_in_icrf = vector3_add(
        matrix_transpose_multiply_vector(base_matrix, velocity_in_mean_ecliptic_per_day),
        matrix_transpose_multiply_vector(base_matrix_dot, position_in_mean_ecliptic));
    *out_position = matrix_multiply_vector(destination_matrix, position_in_icrf);
    *out_velocity_per_day = vector3_add(
        matrix_multiply_vector(destination_matrix, velocity_in_icrf),
        matrix_multiply_vector(destination_matrix_dot, position_in_icrf));
    return finite_vector(*out_position) && finite_vector(*out_velocity_per_day)
        ? TAIYIN_STATUS_OK
        : TAIYIN_EPHEMERIS_ERROR_EVAL_FAILED;
}

bool spherical_position_and_rates(
    const Vector3& position,
    const Vector3& velocity,
    double* out_longitude_rad,
    double* out_latitude_rad,
    double* out_longitude_rate_rad_per_day,
    double* out_latitude_rate_rad_per_day,
    double* out_distance,
    double* out_distance_rate_per_day
) noexcept {
    if (!out_longitude_rad || !out_latitude_rad || !out_longitude_rate_rad_per_day
        || !out_latitude_rate_rad_per_day || !out_distance || !out_distance_rate_per_day) {
        return false;
    }
    const double xy_squared = position.x * position.x + position.y * position.y;
    const double distance_squared = xy_squared + position.z * position.z;
    if (!(xy_squared > 1.0e-30) || !(distance_squared > 1.0e-30)
        || !std::isfinite(xy_squared) || !std::isfinite(distance_squared)) {
        return false;
    }
    const double xy_distance = std::sqrt(xy_squared);
    const double distance = std::sqrt(distance_squared);
    const double xy_rate = (position.x * velocity.x + position.y * velocity.y) / xy_distance;
    *out_longitude_rad = normalize_radians(std::atan2(position.y, position.x));
    *out_latitude_rad = std::atan2(position.z, xy_distance);
    *out_longitude_rate_rad_per_day =
        (position.x * velocity.y - position.y * velocity.x) / xy_squared;
    *out_latitude_rate_rad_per_day =
        (xy_distance * velocity.z - position.z * xy_rate) / distance_squared;
    *out_distance = distance;
    *out_distance_rate_per_day = vector3_dot(position, velocity) / distance;
    return std::isfinite(*out_longitude_rad) && std::isfinite(*out_latitude_rad)
        && std::isfinite(*out_longitude_rate_rad_per_day)
        && std::isfinite(*out_latitude_rate_rad_per_day)
        && std::isfinite(*out_distance) && std::isfinite(*out_distance_rate_per_day);
}

Status calc_lunar_true_node_impl(
    const runtime::NativeCalcContext* context,
    SplitJulianDate jd,
    bool use_ut,
    LunarNodeKind kind,
    uint32_t native_position_flags,
    LunarTrueNodePosition* out,
    runtime::EphemerisEvalDiagnostic* diagnostic
) noexcept {
    if (!context || !out || !split_julian_date_is_finite(jd) || !valid_node_kind(kind)
        || (native_position_flags & ~kAllowedPositionFlags) != 0u) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    *out = LunarTrueNodePosition();

    const int frame_id = effective_reference_frame(*context, native_position_flags);
    if (!is_supported_reference_frame(frame_id)) return TAIYIN_ERROR_INVALID_ARGUMENT;
    SplitJulianDate jd_tt(0, NAN);
    if (!resolve_jd_tt(*context, jd, use_ut, &jd_tt)) return TAIYIN_ERROR_UNSUPPORTED;

    CartesianState state;
    const Status state_status = eval_corrected_geocentric_moon_state(
        *context, jd, use_ut, native_position_flags, &state, diagnostic);
    if (state_status != TAIYIN_STATUS_OK) return state_status;

    double ecliptic_matrix[9];
    double ecliptic_matrix_dot[9];
    const Status matrix_status = eval_ecliptic_matrices(
        *context, jd_tt, frame_id, ecliptic_matrix, ecliptic_matrix_dot);
    if (matrix_status != TAIYIN_STATUS_OK) return matrix_status;
    const Vector3 angular_momentum = vector3_cross(
        state.position_au, state.velocity_au_per_day);
    const Vector3 angular_momentum_rate = vector3_cross(
        state.position_au, state.acceleration_au_per_day2);
    const Vector3 angular_momentum_in_frame = matrix_multiply_vector(
        ecliptic_matrix, angular_momentum);
    const Vector3 angular_momentum_rate_in_frame = vector3_add(
        matrix_multiply_vector(ecliptic_matrix, angular_momentum_rate),
        matrix_multiply_vector(ecliptic_matrix_dot, angular_momentum));
    Vector3 node = Vector3{
        -angular_momentum_in_frame.y, angular_momentum_in_frame.x, 0.0};
    Vector3 node_rate = Vector3{
        -angular_momentum_rate_in_frame.y,
        angular_momentum_rate_in_frame.x,
        0.0,
    };
    if (kind == TAIYIN_LUNAR_NODE_DESCENDING) {
        node = vector3_negate(node);
        node_rate = vector3_negate(node_rate);
    }
    const double node_squared = vector3_dot(node, node);
    if (!(node_squared > 1.0e-30) || !std::isfinite(node_squared)) {
        return TAIYIN_EPHEMERIS_ERROR_EVAL_FAILED;
    }
    const double longitude_rate = (node.x * node_rate.y - node.y * node_rate.x) / node_squared;
    const double longitude = normalize_radians(std::atan2(node.y, node.x));
    if (!std::isfinite(longitude) || !std::isfinite(longitude_rate)) {
        return TAIYIN_EPHEMERIS_ERROR_EVAL_FAILED;
    }
    out->reference_frame_id = frame_id;
    out->longitude_rad = longitude;
    out->longitude_rate_rad_per_day = longitude_rate;
    return TAIYIN_STATUS_OK;
}

Status calc_lunar_mean_node_impl(
    const runtime::NativeCalcContext* context,
    SplitJulianDate jd,
    bool use_ut,
    LunarNodeKind kind,
    uint32_t native_position_flags,
    LunarNodePosition* out,
    runtime::EphemerisEvalDiagnostic* diagnostic
) noexcept {
    if (!context || !out || !split_julian_date_is_finite(jd) || !valid_node_kind(kind)
        || (native_position_flags & ~kAllowedMeanFlags) != 0u) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    *out = LunarNodePosition();
    const int frame_id = effective_reference_frame(*context, native_position_flags);
    if (!is_supported_reference_frame(frame_id)) return TAIYIN_ERROR_INVALID_ARGUMENT;
    SplitJulianDate jd_tt(0, NAN);
    if (!resolve_jd_tt(*context, jd, use_ut, &jd_tt)) return TAIYIN_ERROR_UNSUPPORTED;

    const AngularArgument node_argument = iers_2003_mean_lunar_node(jd_tt);
    Vector3 node_in_mean_ecliptic = Vector3{
        std::cos(node_argument.value_rad), std::sin(node_argument.value_rad), 0.0};
    Vector3 node_rate_in_mean_ecliptic = Vector3{
        -std::sin(node_argument.value_rad) * node_argument.rate_rad_per_day,
        std::cos(node_argument.value_rad) * node_argument.rate_rad_per_day,
        0.0,
    };
    if (kind == TAIYIN_LUNAR_NODE_DESCENDING) {
        node_in_mean_ecliptic = vector3_negate(node_in_mean_ecliptic);
        node_rate_in_mean_ecliptic = vector3_negate(node_rate_in_mean_ecliptic);
    }

    Vector3 node;
    Vector3 node_rate;
    const Status status = transform_from_mean_ecliptic_of_date(
        *context, jd_tt, frame_id, node_in_mean_ecliptic,
        node_rate_in_mean_ecliptic, &node, &node_rate);
    if (status != TAIYIN_STATUS_OK) return status;
    const double xy_squared = node.x * node.x + node.y * node.y;
    if (!(xy_squared > 1.0e-30) || !std::isfinite(xy_squared)) {
        return TAIYIN_EPHEMERIS_ERROR_EVAL_FAILED;
    }
    const double longitude = normalize_radians(std::atan2(node.y, node.x));
    const double longitude_rate =
        (node.x * node_rate.y - node.y * node_rate.x) / xy_squared;
    if (!std::isfinite(longitude) || !std::isfinite(longitude_rate)) {
        return TAIYIN_EPHEMERIS_ERROR_EVAL_FAILED;
    }
    out->reference_frame_id = frame_id;
    out->longitude_rad = longitude;
    out->longitude_rate_rad_per_day = longitude_rate;
    (void)diagnostic;
    return TAIYIN_STATUS_OK;
}

void eval_delaunay_mean_apogee_in_mean_ecliptic(
    SplitJulianDate jd_tt,
    Vector3* out_position,
    Vector3* out_velocity_per_day
) noexcept {
    const AngularArgument mean_anomaly = iers_2003_mean_lunar_anomaly(jd_tt);
    const AngularArgument argument_of_latitude =
        iers_2003_mean_lunar_argument_of_latitude(jd_tt);
    const AngularArgument node = iers_2003_mean_lunar_node(jd_tt);
    const double argument = normalize_radians(
        argument_of_latitude.value_rad - mean_anomaly.value_rad + TAIYIN_PI);
    const double argument_rate = argument_of_latitude.rate_rad_per_day
        - mean_anomaly.rate_rad_per_day;
    const double sin_node = std::sin(node.value_rad);
    const double cos_node = std::cos(node.value_rad);
    const double sin_argument = std::sin(argument);
    const double cos_argument = std::cos(argument);
    static const double sin_inclination = std::sin(kMeanLunarInclinationRad);
    static const double cos_inclination = std::cos(kMeanLunarInclinationRad);
    *out_position = Vector3{
        cos_node * cos_argument - sin_node * sin_argument * cos_inclination,
        sin_node * cos_argument + cos_node * sin_argument * cos_inclination,
        sin_argument * sin_inclination,
    };
    const Vector3 node_component = Vector3{
        -sin_node * cos_argument - cos_node * sin_argument * cos_inclination,
        cos_node * cos_argument - sin_node * sin_argument * cos_inclination,
        0.0,
    };
    const Vector3 argument_component = Vector3{
        -cos_node * sin_argument - sin_node * cos_argument * cos_inclination,
        -sin_node * sin_argument + cos_node * cos_argument * cos_inclination,
        cos_argument * sin_inclination,
    };
    *out_velocity_per_day = vector3_add(
        vector3_scale(node_component, node.rate_rad_per_day),
        vector3_scale(argument_component, argument_rate));
}

enum LunarApogeeFitSeriesKind {
    kLunarApogeeFitTime,
    kLunarApogeeFitTangentEast,
    kLunarApogeeFitTangentNorth,
    kLunarApogeeFitDistance,
};

const generated::LunarApogeeFitSeries& lunar_apogee_fit_series(
    const generated::LunarApogeeFitSegment& segment,
    LunarApogeeFitSeriesKind kind
) noexcept {
    switch (kind) {
    case kLunarApogeeFitTime: return segment.time_days;
    case kLunarApogeeFitTangentEast: return segment.tangent_east_rad;
    case kLunarApogeeFitTangentNorth: return segment.tangent_north_rad;
    case kLunarApogeeFitDistance: return segment.distance_km;
    }
    return segment.time_days;
}

double eval_lunar_apogee_fit_segment(
    const generated::LunarApogeeFitSegment& segment,
    LunarApogeeFitSeriesKind kind,
    SplitJulianDate jd_tt
) noexcept {
    const generated::LunarApogeeFitSeries& series = lunar_apogee_fit_series(segment, kind);
    const double x = (jd_tt - model_jd(segment.center_jd)) / segment.scale_days;
    double value = series.polynomial[0] + x * (series.polynomial[1]
        + x * (series.polynomial[2] + x * series.polynomial[3]));
    const AngularArgument arguments[] = {
        iers_2003_mean_elongation(jd_tt),
        iers_2003_mean_solar_anomaly(jd_tt),
        iers_2003_mean_lunar_anomaly(jd_tt),
        iers_2003_mean_lunar_argument_of_latitude(jd_tt),
    };
    for (int term_index = 0; term_index < generated::kLunarApogeeFitTermCount; ++term_index) {
        const generated::LunarApogeeFitTerm& term = series.terms[term_index];
        double phase = 0.0;
        for (int argument_index = 0; argument_index < 4; ++argument_index) {
            phase += static_cast<double>(term.multipliers[argument_index])
                * arguments[argument_index].value_rad;
        }
        const double sin_phase = std::sin(phase);
        const double cos_phase = std::cos(phase);
        value += term.coefficients[0] * sin_phase
            + term.coefficients[1] * cos_phase
            + x * (term.coefficients[2] * sin_phase
                + term.coefficients[3] * cos_phase);
    }
    return value;
}

double eval_lunar_apogee_fit(
    LunarApogeeFitSeriesKind kind,
    SplitJulianDate jd_tt
) noexcept {
    const generated::LunarApogeeFitSegment* segments = generated::kLunarApogeeFitSegments;
    const int count = generated::kLunarApogeeFitSegmentCount;
    int segment_index = 0;
    if (jd_tt >= model_jd(segments[count - 1].end_jd)) {
        segment_index = count - 1;
    } else if (jd_tt >= model_jd(segments[0].start_jd)) {
        int low = 0;
        int high = count;
        while (low < high) {
            const int middle = low + (high - low) / 2;
            if (jd_tt < model_jd(segments[middle].end_jd)) {
                high = middle;
            } else {
                low = middle + 1;
            }
        }
        segment_index = low < count ? low : count - 1;
    }

    const double blend = generated::kLunarApogeeFitBlendDays;
    int boundary_index = -1;
    if (blend > 0.0 && segment_index > 0
        && jd_tt <= model_jd(segments[segment_index].start_jd) + blend) {
        boundary_index = segment_index;
    } else if (blend > 0.0 && segment_index + 1 < count
        && jd_tt >= model_jd(segments[segment_index].end_jd) - blend) {
        boundary_index = segment_index + 1;
    }
    if (boundary_index >= 1) {
        const SplitJulianDate boundary = model_jd(segments[boundary_index].start_jd);
        if (jd_tt >= boundary - blend && jd_tt <= boundary + blend) {
            double weight = (jd_tt - (boundary - blend)) / (2.0 * blend);
            weight = weight * weight * (3.0 - 2.0 * weight);
            const double left = eval_lunar_apogee_fit_segment(
                segments[boundary_index - 1], kind, jd_tt);
            const double right = eval_lunar_apogee_fit_segment(
                segments[boundary_index], kind, jd_tt);
            return left + weight * (right - left);
        }
    }
    return eval_lunar_apogee_fit_segment(segments[segment_index], kind, jd_tt);
}

bool solve_fitted_apogee_event_time(
    int64_t event_index,
    SplitJulianDate* out_jd_tt
) noexcept {
    if (!out_jd_tt) return false;
    static const double mean_anomaly_at_j2000 = 485868.249036 * TAIYIN_ARCSEC_TO_RAD;
    static const double mean_rate_at_j2000 =
        1717915923.2178 * TAIYIN_ARCSEC_TO_RAD / kJulianCenturyDays;
    const double target = TAIYIN_PI + TAIYIN_TWO_PI * static_cast<double>(event_index);
    SplitJulianDate seed_jd = model_jd(kJ2000Jd)
        + (target - mean_anomaly_at_j2000) / mean_rate_at_j2000;
    for (int iteration = 0; iteration < 8; ++iteration) {
        const AngularArgument anomaly = iers_2003_unwrapped_mean_lunar_anomaly(seed_jd);
        if (!std::isfinite(anomaly.value_rad) || !std::isfinite(anomaly.rate_rad_per_day)
            || !(std::fabs(anomaly.rate_rad_per_day) > 1.0e-12)) {
            return false;
        }
        seed_jd -= (anomaly.value_rad - target) / anomaly.rate_rad_per_day;
    }
    const SplitJulianDate event_jd = seed_jd
        + eval_lunar_apogee_fit(kLunarApogeeFitTime, seed_jd);
    if (!split_julian_date_is_finite(event_jd)) return false;
    *out_jd_tt = event_jd;
    return true;
}

struct FittedLunarApogeeEvent {
    SplitJulianDate jd_tt;
    Vector3 position_icrf_au;
};

Status eval_fitted_lunar_apogee_event(
    int64_t event_index,
    FittedLunarApogeeEvent* out
) noexcept {
    if (!out || !solve_fitted_apogee_event_time(event_index, &out->jd_tt)) {
        return TAIYIN_EPHEMERIS_ERROR_EVAL_FAILED;
    }
    Vector3 mean_direction;
    Vector3 ignored_rate;
    eval_delaunay_mean_apogee_in_mean_ecliptic(
        out->jd_tt, &mean_direction, &ignored_rate);
    runtime::NativeCalcContext fit_reference_context;
    fit_reference_context.model_context.precession_model_id =
        dispatch::PRECESSION_IAU2006;
    fit_reference_context.model_context.obliquity_model_id = 0;
    fit_reference_context.model_context.frame_route_id =
        dispatch::FRAME_ROUTE_EQUINOX;
    double mean_ecliptic_matrix[9];
    double mean_ecliptic_matrix_dot[9];
    const Status matrix_status = eval_ecliptic_matrices(
        fit_reference_context,
        out->jd_tt,
        kDelaunayReferenceFrame,
        mean_ecliptic_matrix,
        mean_ecliptic_matrix_dot);
    if (matrix_status != TAIYIN_STATUS_OK) return matrix_status;
    const Vector3 mean_direction_icrf = matrix_transpose_multiply_vector(
        mean_ecliptic_matrix, mean_direction);
    const double mean_xy = std::sqrt(
        mean_direction.x * mean_direction.x
        + mean_direction.y * mean_direction.y);
    if (!(mean_xy > 1.0e-12) || !std::isfinite(mean_xy)) {
        return TAIYIN_EPHEMERIS_ERROR_EVAL_FAILED;
    }
    const Vector3 east_mean_ecliptic = Vector3{
        -mean_direction.y / mean_xy,
        mean_direction.x / mean_xy,
        0.0,
    };
    const Vector3 north_mean_ecliptic = vector3_cross(
        mean_direction, east_mean_ecliptic);
    const Vector3 east_icrf = matrix_transpose_multiply_vector(
        mean_ecliptic_matrix, east_mean_ecliptic);
    const Vector3 north_icrf = matrix_transpose_multiply_vector(
        mean_ecliptic_matrix, north_mean_ecliptic);
    const double tangent_east = eval_lunar_apogee_fit(
        kLunarApogeeFitTangentEast, out->jd_tt);
    const double tangent_north = eval_lunar_apogee_fit(
        kLunarApogeeFitTangentNorth, out->jd_tt);
    const double tangent_angle = std::sqrt(
        tangent_east * tangent_east + tangent_north * tangent_north);
    double tangent_scale = 1.0;
    if (tangent_angle > 1.0e-12) {
        tangent_scale = std::sin(tangent_angle) / tangent_angle;
    }
    const Vector3 fitted_direction = vector3_add(
        vector3_scale(mean_direction_icrf, std::cos(tangent_angle)),
        vector3_scale(
            vector3_add(
                vector3_scale(east_icrf, tangent_east),
                vector3_scale(north_icrf, tangent_north)),
            tangent_scale));
    const double direction_norm = vector3_norm(fitted_direction);
    const double distance_au = eval_lunar_apogee_fit(
        kLunarApogeeFitDistance, out->jd_tt) / TAIYIN_AU_KM;
    if (!(direction_norm > 0.0) || !(distance_au > 0.0)
        || !std::isfinite(tangent_angle) || !std::isfinite(direction_norm)
        || !std::isfinite(distance_au) || !finite_vector(fitted_direction)) {
        return TAIYIN_EPHEMERIS_ERROR_EVAL_FAILED;
    }
    out->position_icrf_au = vector3_scale(
        fitted_direction, distance_au / direction_norm);
    return finite_vector(out->position_icrf_au)
        ? TAIYIN_STATUS_OK : TAIYIN_EPHEMERIS_ERROR_EVAL_FAILED;
}

Status find_fitted_lunar_apogee_interval(
    int64_t seed_index,
    SplitJulianDate jd_tt,
    FittedLunarApogeeEvent out_events[4]
) noexcept {
    if (!out_events || !split_julian_date_is_finite(jd_tt)) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }

    int64_t low_index = seed_index;
    int64_t high_index = seed_index;
    FittedLunarApogeeEvent low_event;
    FittedLunarApogeeEvent high_event;
    Status status = eval_fitted_lunar_apogee_event(seed_index, &low_event);
    if (status != TAIYIN_STATUS_OK) return status;
    high_event = low_event;

    int64_t step = 1;
    if (jd_tt < low_event.jd_tt) {
        for (int iteration = 0; iteration < 62; ++iteration) {
            if (seed_index < std::numeric_limits<int64_t>::min() + step) {
                return TAIYIN_ERROR_INVALID_ARGUMENT;
            }
            const int64_t candidate_index = seed_index - step;
            FittedLunarApogeeEvent candidate;
            status = eval_fitted_lunar_apogee_event(candidate_index, &candidate);
            if (status != TAIYIN_STATUS_OK) return status;
            if (!(candidate.jd_tt < high_event.jd_tt)) {
                return TAIYIN_EPHEMERIS_ERROR_EVAL_FAILED;
            }
            low_index = candidate_index;
            low_event = candidate;
            if (candidate.jd_tt <= jd_tt) break;
            high_index = candidate_index;
            high_event = candidate;
            if (step > std::numeric_limits<int64_t>::max() / 2) {
                return TAIYIN_ERROR_INVALID_ARGUMENT;
            }
            step *= 2;
        }
    } else {
        for (int iteration = 0; iteration < 62; ++iteration) {
            if (seed_index > std::numeric_limits<int64_t>::max() - step) {
                return TAIYIN_ERROR_INVALID_ARGUMENT;
            }
            const int64_t candidate_index = seed_index + step;
            FittedLunarApogeeEvent candidate;
            status = eval_fitted_lunar_apogee_event(candidate_index, &candidate);
            if (status != TAIYIN_STATUS_OK) return status;
            if (!(candidate.jd_tt > low_event.jd_tt)) {
                return TAIYIN_EPHEMERIS_ERROR_EVAL_FAILED;
            }
            high_index = candidate_index;
            high_event = candidate;
            if (jd_tt < candidate.jd_tt) break;
            low_index = candidate_index;
            low_event = candidate;
            if (step > std::numeric_limits<int64_t>::max() / 2) {
                return TAIYIN_ERROR_INVALID_ARGUMENT;
            }
            step *= 2;
        }
    }

    if (!(low_event.jd_tt <= jd_tt && jd_tt < high_event.jd_tt)
        || !(low_index < high_index)) {
        return TAIYIN_EPHEMERIS_ERROR_EVAL_FAILED;
    }
    while (high_index - low_index > 1) {
        const int64_t middle_index = low_index + (high_index - low_index) / 2;
        FittedLunarApogeeEvent middle;
        status = eval_fitted_lunar_apogee_event(middle_index, &middle);
        if (status != TAIYIN_STATUS_OK) return status;
        if (!(low_event.jd_tt < middle.jd_tt && middle.jd_tt < high_event.jd_tt)) {
            return TAIYIN_EPHEMERIS_ERROR_EVAL_FAILED;
        }
        if (middle.jd_tt <= jd_tt) {
            low_index = middle_index;
            low_event = middle;
        } else {
            high_index = middle_index;
            high_event = middle;
        }
    }
    if (low_index == std::numeric_limits<int64_t>::min()
        || high_index == std::numeric_limits<int64_t>::max()) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    status = eval_fitted_lunar_apogee_event(low_index - 1, &out_events[0]);
    if (status != TAIYIN_STATUS_OK) return status;
    out_events[1] = low_event;
    out_events[2] = high_event;
    status = eval_fitted_lunar_apogee_event(high_index + 1, &out_events[3]);
    if (status != TAIYIN_STATUS_OK) return status;
    if (!(out_events[0].jd_tt < out_events[1].jd_tt
        && out_events[1].jd_tt <= jd_tt
        && jd_tt < out_events[2].jd_tt
        && out_events[2].jd_tt < out_events[3].jd_tt)) {
        return TAIYIN_EPHEMERIS_ERROR_EVAL_FAILED;
    }
    return TAIYIN_STATUS_OK;
}

Status calc_lunar_fitted_apogee_impl(
    const runtime::NativeCalcContext* context,
    SplitJulianDate jd,
    bool use_ut,
    uint32_t native_position_flags,
    LunarApsisPosition* out,
    runtime::EphemerisEvalDiagnostic* diagnostic
) noexcept {
    if (!context || !out || !split_julian_date_is_finite(jd)
        || (native_position_flags & ~kAllowedMeanFlags) != 0u) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    *out = LunarApsisPosition();
    const int frame_id = effective_reference_frame(*context, native_position_flags);
    if (!is_supported_reference_frame(frame_id)) return TAIYIN_ERROR_INVALID_ARGUMENT;
    SplitJulianDate jd_tt(0, NAN);
    if (!resolve_jd_tt(*context, jd, use_ut, &jd_tt)) return TAIYIN_ERROR_UNSUPPORTED;

    const AngularArgument anomaly = iers_2003_unwrapped_mean_lunar_anomaly(jd_tt);
    const double event_coordinate = (anomaly.value_rad - TAIYIN_PI) / TAIYIN_TWO_PI;
    if (!std::isfinite(event_coordinate)
        || event_coordinate <= static_cast<double>(std::numeric_limits<int64_t>::min() + 4)
        || event_coordinate >= static_cast<double>(std::numeric_limits<int64_t>::max() - 4)) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    const int64_t base_index = static_cast<int64_t>(std::floor(event_coordinate));
    FittedLunarApogeeEvent events[4];
    Status status = find_fitted_lunar_apogee_interval(
        base_index, jd_tt, events);
    if (status != TAIYIN_STATUS_OK) return status;
    const FittedLunarApogeeEvent& previous = events[0];
    const FittedLunarApogeeEvent& left = events[1];
    const FittedLunarApogeeEvent& right = events[2];
    const FittedLunarApogeeEvent& next = events[3];
    const double duration = right.jd_tt - left.jd_tt;
    const double previous_span = right.jd_tt - previous.jd_tt;
    const double next_span = next.jd_tt - left.jd_tt;
    if (!(duration > 0.0) || !(previous_span > 0.0) || !(next_span > 0.0)) {
        return TAIYIN_EPHEMERIS_ERROR_EVAL_FAILED;
    }
    const Vector3 left_tangent = vector3_scale(
        vector3_subtract(right.position_icrf_au, previous.position_icrf_au),
        1.0 / previous_span);
    const Vector3 right_tangent = vector3_scale(
        vector3_subtract(next.position_icrf_au, left.position_icrf_au),
        1.0 / next_span);
    const double u = (jd_tt - left.jd_tt) / duration;
    const double u2 = u * u;
    const double u3 = u2 * u;
    const double h00 = 2.0 * u3 - 3.0 * u2 + 1.0;
    const double h10 = u3 - 2.0 * u2 + u;
    const double h01 = -2.0 * u3 + 3.0 * u2;
    const double h11 = u3 - u2;
    const Vector3 position_icrf = vector3_add(
        vector3_add(
            vector3_scale(left.position_icrf_au, h00),
            vector3_scale(left_tangent, h10 * duration)),
        vector3_add(
            vector3_scale(right.position_icrf_au, h01),
            vector3_scale(right_tangent, h11 * duration)));
    const double dh00 = (6.0 * u2 - 6.0 * u) / duration;
    const double dh10 = 3.0 * u2 - 4.0 * u + 1.0;
    const double dh01 = (-6.0 * u2 + 6.0 * u) / duration;
    const double dh11 = 3.0 * u2 - 2.0 * u;
    const Vector3 velocity_icrf = vector3_add(
        vector3_add(
            vector3_scale(left.position_icrf_au, dh00),
            vector3_scale(left_tangent, dh10)),
        vector3_add(
            vector3_scale(right.position_icrf_au, dh01),
            vector3_scale(right_tangent, dh11)));

    double frame_matrix[9];
    double frame_matrix_dot[9];
    status = eval_ecliptic_matrices(
        *context, jd_tt, frame_id, frame_matrix, frame_matrix_dot);
    if (status != TAIYIN_STATUS_OK) return status;
    const Vector3 position = matrix_multiply_vector(frame_matrix, position_icrf);
    const Vector3 velocity = vector3_add(
        matrix_multiply_vector(frame_matrix, velocity_icrf),
        matrix_multiply_vector(frame_matrix_dot, position_icrf));
    if (!spherical_position_and_rates(
            position, velocity,
            &out->longitude_rad, &out->latitude_rad,
            &out->longitude_rate_rad_per_day, &out->latitude_rate_rad_per_day,
            &out->distance_au, &out->distance_rate_au_per_day)) {
        return TAIYIN_EPHEMERIS_ERROR_EVAL_FAILED;
    }
    out->reference_frame_id = frame_id;
    out->definition = TAIYIN_LUNAR_APSIS_DE441_FITTED_NATURAL;
    out->extrapolated = jd_tt < model_jd(generated::kLunarApogeeFitCoverageStartJd)
        || jd_tt > model_jd(generated::kLunarApogeeFitCoverageEndJd);
    (void)diagnostic;
    return TAIYIN_STATUS_OK;
}

Status calc_lunar_mean_apogee_impl(
    const runtime::NativeCalcContext* context,
    SplitJulianDate jd,
    bool use_ut,
    uint32_t native_position_flags,
    LunarApsisPosition* out,
    runtime::EphemerisEvalDiagnostic* diagnostic
) noexcept {
    if (!context || !out || !split_julian_date_is_finite(jd)
        || (native_position_flags & ~kAllowedMeanFlags) != 0u) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    *out = LunarApsisPosition();
    const int frame_id = effective_reference_frame(*context, native_position_flags);
    if (!is_supported_reference_frame(frame_id)) return TAIYIN_ERROR_INVALID_ARGUMENT;
    SplitJulianDate jd_tt(0, NAN);
    if (!resolve_jd_tt(*context, jd, use_ut, &jd_tt)) return TAIYIN_ERROR_UNSUPPORTED;

    Vector3 apogee_in_mean_ecliptic;
    Vector3 apogee_rate_in_mean_ecliptic;
    eval_delaunay_mean_apogee_in_mean_ecliptic(
        jd_tt, &apogee_in_mean_ecliptic, &apogee_rate_in_mean_ecliptic);

    Vector3 apogee;
    Vector3 apogee_rate;
    const Status status = transform_from_mean_ecliptic_of_date(
        *context, jd_tt, frame_id, apogee_in_mean_ecliptic,
        apogee_rate_in_mean_ecliptic, &apogee, &apogee_rate);
    if (status != TAIYIN_STATUS_OK) return status;
    double ignored_distance = NAN;
    double ignored_distance_rate = NAN;
    if (!spherical_position_and_rates(
            apogee, apogee_rate, &out->longitude_rad, &out->latitude_rad,
            &out->longitude_rate_rad_per_day, &out->latitude_rate_rad_per_day,
            &ignored_distance, &ignored_distance_rate)) {
        return TAIYIN_EPHEMERIS_ERROR_EVAL_FAILED;
    }
    out->reference_frame_id = frame_id;
    out->definition = TAIYIN_LUNAR_APSIS_DELAUNAY_MEAN;
    (void)diagnostic;
    return TAIYIN_STATUS_OK;
}

Status calc_lunar_osculating_apogee_impl(
    const runtime::NativeCalcContext* context,
    SplitJulianDate jd,
    bool use_ut,
    uint32_t native_position_flags,
    LunarApsisPosition* out,
    runtime::EphemerisEvalDiagnostic* diagnostic
) noexcept {
    if (!context || !out || !split_julian_date_is_finite(jd)
        || (native_position_flags & ~kAllowedPositionFlags) != 0u) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    *out = LunarApsisPosition();
    const int frame_id = effective_reference_frame(*context, native_position_flags);
    if (!is_supported_reference_frame(frame_id)) return TAIYIN_ERROR_INVALID_ARGUMENT;
    SplitJulianDate jd_tt(0, NAN);
    if (!resolve_jd_tt(*context, jd, use_ut, &jd_tt)) return TAIYIN_ERROR_UNSUPPORTED;

    CartesianState state;
    Status status = eval_corrected_geocentric_moon_state(
        *context, jd, use_ut, native_position_flags, &state, diagnostic);
    if (status != TAIYIN_STATUS_OK) return status;

    const double radius = vector3_norm(state.position_au);
    const double velocity_squared = vector3_dot(
        state.velocity_au_per_day, state.velocity_au_per_day);
    if (!(radius > 0.0) || !(velocity_squared >= 0.0)
        || !std::isfinite(radius) || !std::isfinite(velocity_squared)) {
        return TAIYIN_EPHEMERIS_ERROR_EVAL_FAILED;
    }
    const double inv_radius = 1.0 / radius;
    const double inv_radius3 = inv_radius * inv_radius * inv_radius;
    const double mu = TAIYIN_EARTH_MOON_MU_AU3_PER_DAY2;
    const Vector3 angular_momentum = vector3_cross(
        state.position_au, state.velocity_au_per_day);
    const Vector3 angular_momentum_rate = vector3_cross(
        state.position_au, state.acceleration_au_per_day2);
    const Vector3 eccentricity_vector = vector3_subtract(
        vector3_scale(vector3_cross(state.velocity_au_per_day, angular_momentum), 1.0 / mu),
        vector3_scale(state.position_au, inv_radius));
    const double eccentricity = vector3_norm(eccentricity_vector);
    if (!(eccentricity > 1.0e-12) || !std::isfinite(eccentricity)) {
        return TAIYIN_EPHEMERIS_ERROR_EVAL_FAILED;
    }
    const Vector3 radial_unit_rate = vector3_subtract(
        vector3_scale(state.velocity_au_per_day, inv_radius),
        vector3_scale(state.position_au,
            vector3_dot(state.position_au, state.velocity_au_per_day)
                * inv_radius3));
    const Vector3 eccentricity_vector_rate = vector3_subtract(
        vector3_scale(vector3_add(
            vector3_cross(state.acceleration_au_per_day2, angular_momentum),
            vector3_cross(state.velocity_au_per_day, angular_momentum_rate)), 1.0 / mu),
        radial_unit_rate);
    const double scalar_eccentricity_rate = vector3_dot(
        eccentricity_vector, eccentricity_vector_rate) / eccentricity;
    const Vector3 apogee_direction_in_icrf = vector3_scale(
        eccentricity_vector, -1.0 / eccentricity);
    const Vector3 apogee_direction_rate_in_icrf = vector3_negate(vector3_subtract(
        vector3_scale(eccentricity_vector_rate, 1.0 / eccentricity),
        vector3_scale(eccentricity_vector,
            scalar_eccentricity_rate / (eccentricity * eccentricity))));
    const double specific_energy = 0.5 * velocity_squared - mu * inv_radius;
    const double specific_energy_rate = vector3_dot(
        state.velocity_au_per_day, state.acceleration_au_per_day2)
        + mu * vector3_dot(state.position_au, state.velocity_au_per_day)
            * inv_radius3;
    if (!(specific_energy < 0.0) || !std::isfinite(specific_energy)
        || !std::isfinite(specific_energy_rate)) {
        return TAIYIN_EPHEMERIS_ERROR_EVAL_FAILED;
    }
    const double semi_major_axis = -mu / (2.0 * specific_energy);
    const double semi_major_axis_rate = mu * specific_energy_rate
        / (2.0 * specific_energy * specific_energy);
    const double apoapsis_distance = semi_major_axis * (1.0 + eccentricity);
    const double apoapsis_distance_rate = semi_major_axis_rate * (1.0 + eccentricity)
        + semi_major_axis * scalar_eccentricity_rate;
    if (!(apoapsis_distance > 0.0) || !std::isfinite(apoapsis_distance)
        || !std::isfinite(apoapsis_distance_rate)) {
        return TAIYIN_EPHEMERIS_ERROR_EVAL_FAILED;
    }

    double frame_matrix[9];
    double frame_matrix_dot[9];
    status = eval_ecliptic_matrices(*context, jd_tt, frame_id, frame_matrix, frame_matrix_dot);
    if (status != TAIYIN_STATUS_OK) return status;
    const Vector3 apogee_direction = matrix_multiply_vector(
        frame_matrix, apogee_direction_in_icrf);
    const Vector3 apogee_direction_rate = vector3_add(
        matrix_multiply_vector(frame_matrix, apogee_direction_rate_in_icrf),
        matrix_multiply_vector(frame_matrix_dot, apogee_direction_in_icrf));
    double ignored_distance = NAN;
    double ignored_distance_rate = NAN;
    if (!spherical_position_and_rates(
            apogee_direction, apogee_direction_rate,
            &out->longitude_rad, &out->latitude_rad,
            &out->longitude_rate_rad_per_day, &out->latitude_rate_rad_per_day,
            &ignored_distance, &ignored_distance_rate)) {
        return TAIYIN_EPHEMERIS_ERROR_EVAL_FAILED;
    }
    out->reference_frame_id = frame_id;
    out->definition = TAIYIN_LUNAR_APSIS_OSCULATING_TWO_BODY;
    out->distance_au = apoapsis_distance;
    out->distance_rate_au_per_day = apoapsis_distance_rate;
    return TAIYIN_STATUS_OK;
}

}  // namespace

LunarNodePosition::LunarNodePosition() noexcept
    : reference_frame_id(-1), longitude_rad(NAN), longitude_rate_rad_per_day(NAN) {}

LunarApsisPosition::LunarApsisPosition() noexcept
    : reference_frame_id(-1),
      definition(TAIYIN_LUNAR_APSIS_DELAUNAY_MEAN),
      longitude_rad(NAN),
      latitude_rad(NAN),
      longitude_rate_rad_per_day(NAN),
      latitude_rate_rad_per_day(NAN),
      distance_au(NAN),
      distance_rate_au_per_day(NAN),
      extrapolated(false) {}

Status calc_lunar_true_node_tt(
    const runtime::NativeCalcContext* context,
    SplitJulianDate jd_tt,
    LunarNodeKind kind,
    uint32_t native_position_flags,
    LunarTrueNodePosition* out,
    runtime::EphemerisEvalDiagnostic* diagnostic
) noexcept {
    return calc_lunar_true_node_impl(
        context, jd_tt, false, kind, native_position_flags, out, diagnostic);
}

Status calc_lunar_true_node_ut(
    const runtime::NativeCalcContext* context,
    SplitJulianDate jd_ut,
    LunarNodeKind kind,
    uint32_t native_position_flags,
    LunarTrueNodePosition* out,
    runtime::EphemerisEvalDiagnostic* diagnostic
) noexcept {
    return calc_lunar_true_node_impl(
        context, jd_ut, true, kind, native_position_flags, out, diagnostic);
}

Status calc_lunar_mean_node_tt(
    const runtime::NativeCalcContext* context,
    SplitJulianDate jd_tt,
    LunarNodeKind kind,
    uint32_t native_position_flags,
    LunarNodePosition* out,
    runtime::EphemerisEvalDiagnostic* diagnostic
) noexcept {
    return calc_lunar_mean_node_impl(
        context, jd_tt, false, kind, native_position_flags, out, diagnostic);
}

Status calc_lunar_mean_node_ut(
    const runtime::NativeCalcContext* context,
    SplitJulianDate jd_ut,
    LunarNodeKind kind,
    uint32_t native_position_flags,
    LunarNodePosition* out,
    runtime::EphemerisEvalDiagnostic* diagnostic
) noexcept {
    return calc_lunar_mean_node_impl(
        context, jd_ut, true, kind, native_position_flags, out, diagnostic);
}

Status calc_lunar_mean_apogee_tt(
    const runtime::NativeCalcContext* context,
    SplitJulianDate jd_tt,
    uint32_t native_position_flags,
    LunarApsisPosition* out,
    runtime::EphemerisEvalDiagnostic* diagnostic
) noexcept {
    return calc_lunar_mean_apogee_impl(
        context, jd_tt, false, native_position_flags, out, diagnostic);
}

Status calc_lunar_mean_apogee_ut(
    const runtime::NativeCalcContext* context,
    SplitJulianDate jd_ut,
    uint32_t native_position_flags,
    LunarApsisPosition* out,
    runtime::EphemerisEvalDiagnostic* diagnostic
) noexcept {
    return calc_lunar_mean_apogee_impl(
        context, jd_ut, true, native_position_flags, out, diagnostic);
}

Status calc_lunar_osculating_apogee_tt(
    const runtime::NativeCalcContext* context,
    SplitJulianDate jd_tt,
    uint32_t native_position_flags,
    LunarApsisPosition* out,
    runtime::EphemerisEvalDiagnostic* diagnostic
) noexcept {
    return calc_lunar_osculating_apogee_impl(
        context, jd_tt, false, native_position_flags, out, diagnostic);
}

Status calc_lunar_osculating_apogee_ut(
    const runtime::NativeCalcContext* context,
    SplitJulianDate jd_ut,
    uint32_t native_position_flags,
    LunarApsisPosition* out,
    runtime::EphemerisEvalDiagnostic* diagnostic
) noexcept {
    return calc_lunar_osculating_apogee_impl(
        context, jd_ut, true, native_position_flags, out, diagnostic);
}

Status calc_lunar_fitted_apogee_tt(
    const runtime::NativeCalcContext* context,
    SplitJulianDate jd_tt,
    uint32_t native_position_flags,
    LunarApsisPosition* out,
    runtime::EphemerisEvalDiagnostic* diagnostic
) noexcept {
    return calc_lunar_fitted_apogee_impl(
        context, jd_tt, false, native_position_flags, out, diagnostic);
}

Status calc_lunar_fitted_apogee_ut(
    const runtime::NativeCalcContext* context,
    SplitJulianDate jd_ut,
    uint32_t native_position_flags,
    LunarApsisPosition* out,
    runtime::EphemerisEvalDiagnostic* diagnostic
) noexcept {
    return calc_lunar_fitted_apogee_impl(
        context, jd_ut, true, native_position_flags, out, diagnostic);
}

}  // namespace astrology
}  // namespace taiyin
