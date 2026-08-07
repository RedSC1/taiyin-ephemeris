#include "taiyin/runtime/orbital_events.h"

#include "taiyin/angle.h"
#include "taiyin/body_id.h"
#include "taiyin/coordinates.h"
#include "taiyin/dispatch.h"
#include "taiyin/physical_constants.h"
#include "taiyin/time.h"
#include "taiyin/vector3.h"

#include <algorithm>
#include <cmath>

namespace taiyin {
namespace runtime {
namespace {

constexpr double kTimeToleranceDays = 1.0e-9;
constexpr int kMaxRefineIterations = 48;
constexpr int kFallbackScanSegments = 32;
constexpr double kSmall = 1.0e-14;

SplitJulianDate invalid_jd() noexcept {
    return SplitJulianDate(0, NAN);
}

struct BodyOrbitModel {
    int center_id;
    double gravitational_parameter_au3_per_day2;
};

struct OrbitalSample {
    CartesianState state;
    double distance_au;
    double radial_numerator_au2_per_day;
    double radial_numerator_rate_au2_per_day2;
    Vector3 reference_position_au;
    Vector3 reference_inertial_velocity_au_per_day;
    Vector3 reference_coordinate_velocity_au_per_day;

    OrbitalSample() noexcept
        : state(),
          distance_au(NAN),
          radial_numerator_au2_per_day(NAN),
          radial_numerator_rate_au2_per_day2(NAN),
          reference_position_au(),
          reference_inertial_velocity_au_per_day(),
          reference_coordinate_velocity_au_per_day() {}
};

bool supported_orbital_reference_frame(int frame_id) noexcept {
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

Matrix3x3 matrix_from_array(const double values[9]) noexcept {
    Matrix3x3 result = matrix3x3_identity();
    for (int row = 0; row < 3; ++row) {
        for (int column = 0; column < 3; ++column) {
            result.m[row][column] = values[row * 3 + column];
        }
    }
    return result;
}

Status eval_orbital_reference_matrices(
    const NativeCalcContext& context,
    SplitJulianDate jd,
    bool use_ut,
    int reference_frame_id,
    Matrix3x3* out_matrix,
    Matrix3x3* out_matrix_dot
) noexcept {
    if (!out_matrix || !out_matrix_dot || !split_julian_date_is_finite(jd)
        || !supported_orbital_reference_frame(reference_frame_id)) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    SplitJulianDate jd_tt = jd;
    if (use_ut) {
        const double delta_t = dispatch::eval_delta_t_with_ephemeris_correction(
            context.delta_t_model_id, context.ephemeris_family_id, jd, 0, 0);
        if (!ut1_to_tt_split_jd(jd, delta_t, &jd_tt)) return TAIYIN_ERROR_UNSUPPORTED;
    }

    double output_matrix[9];
    double output_matrix_dot[9];
    if (!calc_apparent_matrices(
            jd_tt,
            TAIYIN_APPARENT_VELOCITY,
            reference_frame_id,
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
            output_matrix,
            output_matrix_dot,
            nullptr,
            nullptr,
            nullptr,
            nullptr,
            nullptr,
            context.apparent_options.custom_output_frame_evaluator,
            context.apparent_options.custom_output_frame_data)) {
        return TAIYIN_ERROR_UNSUPPORTED;
    }
    *out_matrix = matrix_from_array(output_matrix);
    *out_matrix_dot = matrix_from_array(output_matrix_dot);
    return TAIYIN_STATUS_OK;
}

bool finite_vector(const Vector3& value) noexcept {
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

bool finite_state(const CartesianState& state) noexcept {
    return finite_vector(state.position_au)
        && finite_vector(state.velocity_au_per_day)
        && finite_vector(state.acceleration_au_per_day2);
}

bool is_supported_orbital_body(int body_id) noexcept {
    switch (body_id) {
    case TAIYIN_BODY_MOON:
    case TAIYIN_BODY_EARTH:
    case TAIYIN_BODY_EARTH_MOON_BARYCENTER:
    case TAIYIN_BODY_MERCURY:
    case TAIYIN_BODY_MERCURY_BARYCENTER:
    case TAIYIN_BODY_VENUS:
    case TAIYIN_BODY_VENUS_BARYCENTER:
    case TAIYIN_BODY_MARS:
    case TAIYIN_BODY_MARS_BARYCENTER:
    case TAIYIN_BODY_JUPITER:
    case TAIYIN_BODY_JUPITER_BARYCENTER:
    case TAIYIN_BODY_SATURN:
    case TAIYIN_BODY_SATURN_BARYCENTER:
    case TAIYIN_BODY_URANUS:
    case TAIYIN_BODY_URANUS_BARYCENTER:
    case TAIYIN_BODY_NEPTUNE:
    case TAIYIN_BODY_NEPTUNE_BARYCENTER:
    case TAIYIN_BODY_PLUTO:
    case TAIYIN_BODY_PLUTO_BARYCENTER:
        return true;
    default:
        return false;
    }
}

double km3_per_second2_to_au3_per_day2(double value) noexcept {
    return value * SECONDS_PER_DAY * SECONDS_PER_DAY
        / (TAIYIN_AU_KM * TAIYIN_AU_KM * TAIYIN_AU_KM);
}

double body_gravitational_parameter_km3_per_second2(int body_id) noexcept {
    switch (body_id) {
    case TAIYIN_BODY_MERCURY:
        return 2.2031868551400003e4;
    case TAIYIN_BODY_MERCURY_BARYCENTER:
        return 2.2031868551400003e4;
    case TAIYIN_BODY_VENUS:
        return 3.2485859200000000e5;
    case TAIYIN_BODY_VENUS_BARYCENTER:
        return 3.2485859200000000e5;
    case TAIYIN_BODY_EARTH:
        return TAIYIN_EARTH_MU_KM3_PER_S2;
    case TAIYIN_BODY_EARTH_MOON_BARYCENTER:
        return 4.0350323562548019e5;
    case TAIYIN_BODY_MARS:
        return 4.282837362069909e4;
    case TAIYIN_BODY_MARS_BARYCENTER:
        return 4.2828375815756102e4;
    case TAIYIN_BODY_JUPITER:
        return 1.266865319003704e8;
    case TAIYIN_BODY_JUPITER_BARYCENTER:
        return 1.2671276409999998e8;
    case TAIYIN_BODY_SATURN:
        return 3.793120623436167e7;
    case TAIYIN_BODY_SATURN_BARYCENTER:
        return 3.7940584841799997e7;
    case TAIYIN_BODY_URANUS:
        return 5.793951256527211e6;
    case TAIYIN_BODY_URANUS_BARYCENTER:
        return 5.7945563999999985e6;
    case TAIYIN_BODY_NEPTUNE:
        return 6.835103145462294e6;
    case TAIYIN_BODY_NEPTUNE_BARYCENTER:
        return 6.8365271005803989e6;
    case TAIYIN_BODY_PLUTO:
        return 8.696138177608748e2;
    case TAIYIN_BODY_PLUTO_BARYCENTER:
        return 9.7550000000000000e2;
    default:
        return NAN;
    }
}

bool body_orbit_model(int body_id, BodyOrbitModel* out) noexcept {
    if (!out || !is_supported_orbital_body(body_id)) return false;
    if (body_id == TAIYIN_BODY_MOON) {
        out->center_id = TAIYIN_BODY_EARTH;
        out->gravitational_parameter_au3_per_day2 = TAIYIN_EARTH_MOON_MU_AU3_PER_DAY2;
        return true;
    }

    const double body_gm = body_gravitational_parameter_km3_per_second2(body_id);
    if (!(body_gm > 0.0) || !std::isfinite(body_gm)) return false;
    out->center_id = TAIYIN_BODY_SUN;
    // NAIF DE440 GM for the Sun (10), plus the target body/system GM.
    out->gravitational_parameter_au3_per_day2 = km3_per_second2_to_au3_per_day2(
        1.3271244004127942e11 + body_gm);
    return true;
}

bool valid_orbital_position_flags(uint64_t flags) noexcept {
    const uint32_t position_flags = static_cast<uint32_t>(
        flags & TAIYIN_ORBITAL_EVENT_POSITION_FLAGS_MASK);
    return (position_flags & ~TAIYIN_NATIVE_POSITION_ALLOW_BARYCENTER_APPROX) == 0u;
}

bool valid_orbital_calculation_flags(uint64_t flags) noexcept {
    return valid_orbital_position_flags(flags)
        && (flags & TAIYIN_ORBITAL_EVENT_OPTION_FLAGS_MASK) == 0u;
}

bool valid_orbital_search_flags(uint64_t flags) noexcept {
    return valid_orbital_position_flags(flags)
        && (flags & TAIYIN_ORBITAL_EVENT_OPTION_FLAGS_MASK & ~TAIYIN_ORBITAL_EVENT_REVERSE) == 0u;
}

uint32_t orbital_position_flags(uint64_t flags) noexcept {
    return static_cast<uint32_t>(flags & TAIYIN_ORBITAL_EVENT_POSITION_FLAGS_MASK)
        | TAIYIN_NATIVE_POSITION_TRUEPOS;
}

void set_diagnostic(
    EphemerisEvalDiagnostic* diagnostic,
    Status status,
    int body_id,
    int center_id,
    SplitJulianDate jd
) noexcept {
    if (!diagnostic) return;
    *diagnostic = EphemerisEvalDiagnostic();
    diagnostic->status = status;
    diagnostic->target_id = body_id;
    diagnostic->center_id = center_id;
    diagnostic->jd_tdb = jd;
}

Status eval_relative_orbital_state(
    const NativeCalcContext* context,
    int body_id,
    const BodyOrbitModel& model,
    SplitJulianDate jd,
    bool use_ut,
    uint64_t flags,
    int reference_frame_id,
    OrbitalSample* out,
    EphemerisEvalDiagnostic* diagnostic
) noexcept {
    if (!context || !out || !split_julian_date_is_finite(jd)) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }

    NativeCalcContext orbit_context = *context;
    orbit_context.observer_id = model.center_id;
    orbit_context.apparent_options.output_frame_id = TAIYIN_APPARENT_FRAME_ICRF;

    CartesianState state;
    const Status status = use_ut
        ? calc_state_ut(&orbit_context, body_id, jd, orbital_position_flags(flags), &state, diagnostic)
        : calc_state_tt(&orbit_context, body_id, jd, orbital_position_flags(flags), &state, diagnostic);
    if (status != TAIYIN_STATUS_OK) return status;
    if (!finite_state(state)) {
        set_diagnostic(diagnostic, TAIYIN_EPHEMERIS_ERROR_EVAL_FAILED, body_id, model.center_id, jd);
        return TAIYIN_EPHEMERIS_ERROR_EVAL_FAILED;
    }

    const double distance = vector3_norm(state.position_au);
    if (!(distance > 0.0) || !std::isfinite(distance)) {
        set_diagnostic(diagnostic, TAIYIN_EPHEMERIS_ERROR_EVAL_FAILED, body_id, model.center_id, jd);
        return TAIYIN_EPHEMERIS_ERROR_EVAL_FAILED;
    }

    out->state = state;
    out->distance_au = distance;
    out->radial_numerator_au2_per_day = vector3_dot(
        state.position_au, state.velocity_au_per_day);
    out->radial_numerator_rate_au2_per_day2 = vector3_dot(
        state.velocity_au_per_day, state.velocity_au_per_day)
        + vector3_dot(state.position_au, state.acceleration_au_per_day2);
    Matrix3x3 reference_matrix = matrix3x3_identity();
    Matrix3x3 reference_matrix_dot = matrix3x3_identity();
    const Status frame_status = eval_orbital_reference_matrices(
        orbit_context, jd, use_ut, reference_frame_id, &reference_matrix, &reference_matrix_dot);
    if (frame_status != TAIYIN_STATUS_OK) return frame_status;
    out->reference_position_au = matrix3x3_multiply_vector(
        reference_matrix, state.position_au);
    out->reference_inertial_velocity_au_per_day = matrix3x3_multiply_vector(
        reference_matrix, state.velocity_au_per_day);
    out->reference_coordinate_velocity_au_per_day = vector3_add(
        out->reference_inertial_velocity_au_per_day,
        matrix3x3_multiply_vector(reference_matrix_dot, state.position_au));
    if (!std::isfinite(out->radial_numerator_au2_per_day)
        || !std::isfinite(out->radial_numerator_rate_au2_per_day2)
        || !finite_vector(out->reference_position_au)
        || !finite_vector(out->reference_inertial_velocity_au_per_day)
        || !finite_vector(out->reference_coordinate_velocity_au_per_day)) {
        set_diagnostic(diagnostic, TAIYIN_EPHEMERIS_ERROR_EVAL_FAILED, body_id, model.center_id, jd);
        return TAIYIN_EPHEMERIS_ERROR_EVAL_FAILED;
    }
    return TAIYIN_STATUS_OK;
}

bool compute_orbit_elements(
    int body_id,
    const BodyOrbitModel& model,
    const OrbitalSample& sample,
    int reference_frame_id,
    BodyOsculatingOrbit* out
) noexcept {
    if (!out) return false;
    const Vector3& r = sample.reference_position_au;
    const Vector3& v = sample.reference_inertial_velocity_au_per_day;
    const double radius = sample.distance_au;
    const double mu = model.gravitational_parameter_au3_per_day2;
    const Vector3 h = vector3_cross(r, v);
    const double h_norm = vector3_norm(h);
    const double speed_squared = vector3_dot(v, v);
    if (!(radius > 0.0) || !(mu > 0.0) || !(h_norm > kSmall)
        || !std::isfinite(speed_squared)) {
        return false;
    }

    const Vector3 eccentricity_vector = vector3_subtract(
        vector3_scale(vector3_cross(v, h), 1.0 / mu),
        vector3_scale(r, 1.0 / radius));
    const double eccentricity = vector3_norm(eccentricity_vector);
    const double specific_energy = 0.5 * speed_squared - mu / radius;
    if (!std::isfinite(eccentricity) || !std::isfinite(specific_energy)
        || std::fabs(specific_energy) <= kSmall) {
        return false;
    }

    const double semi_major_axis = -mu / (2.0 * specific_energy);
    const double horizontal_angular_momentum = std::sqrt(h.x * h.x + h.y * h.y);
    const double inclination = std::atan2(horizontal_angular_momentum, h.z);
    const Vector3 node = Vector3{-h.y, h.x, 0.0};
    const double node_norm = horizontal_angular_momentum;
    const double longitude_of_node = node_norm > kSmall
        ? normalize_radians(std::atan2(node.y, node.x))
        : 0.0;

    double argument_of_periapsis = 0.0;
    double true_anomaly = 0.0;
    double mean_anomaly = 0.0;
    if (eccentricity > kSmall) {
        if (node_norm > kSmall) {
            const double sin_argp = vector3_dot(vector3_cross(node, eccentricity_vector), h)
                / (node_norm * eccentricity * h_norm);
            const double cos_argp = vector3_dot(node, eccentricity_vector)
                / (node_norm * eccentricity);
            argument_of_periapsis = normalize_radians(std::atan2(sin_argp, cos_argp));
        } else {
            // In an equatorial orbit Ω is conventionally zero, so ω carries
            // the inertial longitude of periapsis. Retrograde equatorial
            // elements reverse the in-plane Y orientation.
            argument_of_periapsis = normalize_radians(std::atan2(
                h.z < 0.0 ? -eccentricity_vector.y : eccentricity_vector.y,
                eccentricity_vector.x));
        }
        const double sin_true = vector3_dot(vector3_cross(eccentricity_vector, r), h)
            / (eccentricity * radius * h_norm);
        const double cos_true = vector3_dot(eccentricity_vector, r)
            / (eccentricity * radius);
        true_anomaly = normalize_radians(std::atan2(sin_true, cos_true));
        if (semi_major_axis > 0.0 && eccentricity < 1.0) {
            const double eccentric_anomaly = std::atan2(
                std::sqrt(1.0 - eccentricity * eccentricity) * std::sin(true_anomaly),
                eccentricity + std::cos(true_anomaly));
            mean_anomaly = normalize_radians(
                eccentric_anomaly - eccentricity * std::sin(eccentric_anomaly));
        }
    } else {
        const double argument_of_latitude = node_norm > kSmall
            ? std::atan2(
                vector3_dot(vector3_cross(node, r), h) / (node_norm * radius * h_norm),
                vector3_dot(node, r) / (node_norm * radius))
            : std::atan2(h.z < 0.0 ? -r.y : r.y, r.x);
        true_anomaly = normalize_radians(argument_of_latitude);
        mean_anomaly = true_anomaly;
    }

    out->body_id = body_id;
    out->center_id = model.center_id;
    out->reference_frame_id = reference_frame_id;
    out->gravitational_parameter_au3_per_day2 = mu;
    out->semi_major_axis_au = semi_major_axis;
    out->eccentricity = eccentricity;
    out->inclination_rad = inclination;
    out->longitude_of_ascending_node_rad = longitude_of_node;
    out->argument_of_periapsis_rad = argument_of_periapsis;
    out->true_anomaly_rad = true_anomaly;
    out->mean_anomaly_rad = mean_anomaly;
    out->periapsis_distance_au = semi_major_axis * (1.0 - eccentricity);
    out->apoapsis_distance_au = semi_major_axis > 0.0 && eccentricity < 1.0
        ? semi_major_axis * (1.0 + eccentricity)
        : NAN;
    out->osculating_period_days = semi_major_axis > 0.0 && eccentricity < 1.0
        ? TAIYIN_TWO_PI * std::sqrt(
            semi_major_axis * semi_major_axis * semi_major_axis / mu)
        : NAN;
    out->current_distance_au = radius;
    out->radial_velocity_au_per_day = sample.radial_numerator_au2_per_day / radius;
    return std::isfinite(out->semi_major_axis_au)
        && std::isfinite(out->eccentricity)
        && std::isfinite(out->inclination_rad)
        && std::isfinite(out->current_distance_au)
        && std::isfinite(out->radial_velocity_au_per_day);
}

Vector3 orbit_direction(
    const BodyOsculatingOrbit& orbit,
    double true_anomaly_rad
) noexcept {
    const double node = orbit.longitude_of_ascending_node_rad;
    const double argument = orbit.argument_of_periapsis_rad + true_anomaly_rad;
    const double cos_node = std::cos(node);
    const double sin_node = std::sin(node);
    const double cos_argument = std::cos(argument);
    const double sin_argument = std::sin(argument);
    const double cos_inclination = std::cos(orbit.inclination_rad);
    const double sin_inclination = std::sin(orbit.inclination_rad);
    return Vector3{
        cos_node * cos_argument - sin_node * sin_argument * cos_inclination,
        sin_node * cos_argument + cos_node * sin_argument * cos_inclination,
        sin_argument * sin_inclination,
    };
}

bool fill_orbit_reference_point(
    const Vector3& position,
    bool allow_zero_distance,
    BodyOrbitReferencePoint* out
) noexcept {
    if (!out || !finite_vector(position)) return false;
    const double distance = vector3_norm(position);
    if (!std::isfinite(distance) || (!allow_zero_distance && !(distance > 0.0))) {
        return false;
    }
    out->position_au = position;
    out->distance_au = distance;
    if (distance == 0.0) {
        out->longitude_rad = NAN;
        out->latitude_rad = NAN;
    } else {
        out->longitude_rad = normalize_radians(std::atan2(position.y, position.x));
        out->latitude_rad = std::asin(
            std::max(-1.0, std::min(1.0, position.z / distance)));
    }
    return true;
}

bool compute_orbit_reference_points(
    const BodyOsculatingOrbit& orbit,
    BodyOrbitReferencePoints* out
) noexcept {
    if (!out || !(orbit.semi_major_axis_au > 0.0)
        || !(orbit.eccentricity >= 0.0) || !(orbit.eccentricity < 1.0)
        || !std::isfinite(orbit.argument_of_periapsis_rad)) {
        return false;
    }
    const double semi_latus_rectum = orbit.semi_major_axis_au
        * (1.0 - orbit.eccentricity * orbit.eccentricity);
    const double cos_argument_of_periapsis =
        std::cos(orbit.argument_of_periapsis_rad);
    const double ascending_radius = semi_latus_rectum
        / (1.0 + orbit.eccentricity * cos_argument_of_periapsis);
    const double descending_radius = semi_latus_rectum
        / (1.0 - orbit.eccentricity * cos_argument_of_periapsis);
    const double cos_node = std::cos(orbit.longitude_of_ascending_node_rad);
    const double sin_node = std::sin(orbit.longitude_of_ascending_node_rad);
    const Vector3 ascending_direction = Vector3{cos_node, sin_node, 0.0};
    const Vector3 descending_direction = Vector3{-cos_node, -sin_node, 0.0};
    const Vector3 periapsis_direction = orbit_direction(orbit, 0.0);

    out->body_id = orbit.body_id;
    out->center_id = orbit.center_id;
    out->reference_frame_id = orbit.reference_frame_id;
    out->model = TAIYIN_BODY_ORBIT_REFERENCE_POINTS_OSCULATING;
    return fill_orbit_reference_point(
               vector3_scale(ascending_direction, ascending_radius),
               false,
               &out->ascending_node)
        && fill_orbit_reference_point(
               vector3_scale(descending_direction, descending_radius),
               false,
               &out->descending_node)
        && fill_orbit_reference_point(
               vector3_scale(periapsis_direction, orbit.periapsis_distance_au),
               false,
               &out->periapsis)
        && fill_orbit_reference_point(
               vector3_scale(periapsis_direction, -orbit.apoapsis_distance_au),
               false,
               &out->apoapsis)
        && fill_orbit_reference_point(
               vector3_scale(
                   periapsis_direction,
                   -2.0 * orbit.semi_major_axis_au * orbit.eccentricity),
               true,
               &out->second_focus);
}

Status calc_body_osculating_orbit_impl(
    const NativeCalcContext* context,
    int body_id,
    SplitJulianDate jd,
    bool use_ut,
    int reference_frame_id,
    uint64_t flags,
    BodyOsculatingOrbit* out,
    EphemerisEvalDiagnostic* diagnostic
) noexcept;

Status calc_body_orbit_reference_points_impl(
    const NativeCalcContext* context,
    int body_id,
    SplitJulianDate jd,
    bool use_ut,
    int reference_frame_id,
    uint64_t flags,
    BodyOrbitReferencePoints* out,
    EphemerisEvalDiagnostic* diagnostic
) noexcept {
    if (out) *out = BodyOrbitReferencePoints();
    if (diagnostic) *diagnostic = EphemerisEvalDiagnostic();
    if (!out) {
        set_diagnostic(diagnostic, TAIYIN_ERROR_INVALID_ARGUMENT, body_id, 0, jd);
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    BodyOsculatingOrbit orbit;
    const Status status = calc_body_osculating_orbit_impl(
        context, body_id, jd, use_ut, reference_frame_id, flags, &orbit, diagnostic);
    if (status != TAIYIN_STATUS_OK) return status;
    if (!compute_orbit_reference_points(orbit, out)) {
        *out = BodyOrbitReferencePoints();
        set_diagnostic(
            diagnostic, TAIYIN_ERROR_UNSUPPORTED, body_id, orbit.center_id, jd);
        return TAIYIN_ERROR_UNSUPPORTED;
    }
    return TAIYIN_STATUS_OK;
}

bool apsis_sign_matches(BodyApsisKind kind, double lower, double upper) noexcept {
    return kind == TAIYIN_BODY_APSIS_PERICENTER
        ? lower < 0.0 && upper > 0.0
        : lower > 0.0 && upper < 0.0;
}

bool node_sign_matches(BodyNodeKind kind, double lower, double upper) noexcept {
    return kind == TAIYIN_BODY_NODE_ASCENDING
        ? lower < 0.0 && upper > 0.0
        : lower > 0.0 && upper < 0.0;
}

double mean_anomaly_from_true_anomaly(double true_anomaly, double eccentricity) noexcept {
    const double eccentric_anomaly = std::atan2(
        std::sqrt(std::max(0.0, 1.0 - eccentricity * eccentricity)) * std::sin(true_anomaly),
        eccentricity + std::cos(true_anomaly));
    return normalize_radians(eccentric_anomaly - eccentricity * std::sin(eccentric_anomaly));
}

double directed_angle_delta(double from, double to, bool reverse) noexcept {
    const double forward = normalize_radians(to - from);
    return reverse ? (forward == 0.0 ? 0.0 : forward - TAIYIN_TWO_PI) : forward;
}

template <typename ValueFn, typename DerivativeFn>
Status refine_bracketed_root(
    const NativeCalcContext* context,
    int body_id,
    const BodyOrbitModel& model,
    bool use_ut,
    uint64_t flags,
    int reference_frame_id,
    SplitJulianDate lower_jd,
    OrbitalSample lower_sample,
    SplitJulianDate upper_jd,
    OrbitalSample upper_sample,
    ValueFn value,
    DerivativeFn derivative,
    SplitJulianDate* out_jd,
    OrbitalSample* out_sample,
    int* out_iterations,
    int* in_out_evaluations,
    EphemerisEvalDiagnostic* diagnostic
) noexcept {
    if (!out_jd || !out_sample || !out_iterations || !in_out_evaluations
        || !(upper_jd > lower_jd)) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    SplitJulianDate lo = lower_jd;
    SplitJulianDate hi = upper_jd;
    OrbitalSample lo_sample = lower_sample;
    OrbitalSample hi_sample = upper_sample;
    double lo_value = value(lo_sample);
    double hi_value = value(hi_sample);
    if (!std::isfinite(lo_value) || !std::isfinite(hi_value) || !(lo_value * hi_value <= 0.0)) {
        return TAIYIN_EVENT_ERROR_NOT_FOUND;
    }

    for (int iteration = 0; iteration < kMaxRefineIterations; ++iteration) {
        const SplitJulianDate midpoint = lo + 0.5 * (hi - lo);
        SplitJulianDate candidate = midpoint;
        const bool use_lower_reference = std::fabs(lo_value) < std::fabs(hi_value);
        const OrbitalSample& reference = use_lower_reference ? lo_sample : hi_sample;
        const SplitJulianDate reference_jd = use_lower_reference ? lo : hi;
        const double reference_value = use_lower_reference ? lo_value : hi_value;
        const double derivative_value = derivative(reference);
        if (std::isfinite(derivative_value) && std::fabs(derivative_value) > kSmall) {
            const SplitJulianDate newton = reference_jd - reference_value / derivative_value;
            if (split_julian_date_is_finite(newton) && newton > lo && newton < hi) {
                candidate = newton;
            }
        }
        if (candidate == midpoint && std::fabs(hi_value - lo_value) > kSmall) {
            const SplitJulianDate secant = lo - lo_value * (hi - lo) / (hi_value - lo_value);
            if (split_julian_date_is_finite(secant) && secant > lo && secant < hi) {
                candidate = secant;
            }
        }

        OrbitalSample candidate_sample;
        const Status status = eval_relative_orbital_state(
            context, body_id, model, candidate, use_ut, flags, reference_frame_id,
            &candidate_sample, diagnostic);
        ++(*in_out_evaluations);
        if (status != TAIYIN_STATUS_OK) return status;
        const double candidate_value = value(candidate_sample);
        if (!std::isfinite(candidate_value)) return TAIYIN_EPHEMERIS_ERROR_EVAL_FAILED;
        if (candidate_value == 0.0 || (hi - lo) <= kTimeToleranceDays) {
            *out_jd = candidate;
            *out_sample = candidate_sample;
            *out_iterations = iteration + 1;
            return TAIYIN_STATUS_OK;
        }
        if ((lo_value < 0.0 && candidate_value > 0.0)
            || (lo_value > 0.0 && candidate_value < 0.0)) {
            hi = candidate;
            hi_sample = candidate_sample;
            hi_value = candidate_value;
        } else {
            lo = candidate;
            lo_sample = candidate_sample;
            lo_value = candidate_value;
        }
    }

    const SplitJulianDate jd = lo + 0.5 * (hi - lo);
    OrbitalSample sample;
    const Status status = eval_relative_orbital_state(
        context, body_id, model, jd, use_ut, flags, reference_frame_id, &sample, diagnostic);
    ++(*in_out_evaluations);
    if (status != TAIYIN_STATUS_OK) return status;
    *out_jd = jd;
    *out_sample = sample;
    *out_iterations = kMaxRefineIterations;
    return TAIYIN_STATUS_OK;
}

template <typename ValueFn, typename SignMatchFn, typename DerivativeFn>
Status search_next_root(
    const NativeCalcContext* context,
    int body_id,
    const BodyOrbitModel& model,
    const BodyOsculatingOrbit& orbit,
    SplitJulianDate jd_start,
    bool use_ut,
    uint64_t flags,
    int reference_frame_id,
    double phase_target_rad,
    ValueFn value,
    SignMatchFn sign_matches,
    DerivativeFn derivative,
    SplitJulianDate* out_jd,
    OrbitalSample* out_sample,
    int* out_iterations,
    int* out_evaluations,
    EphemerisEvalDiagnostic* diagnostic
) noexcept {
    if (!context || !out_jd || !out_sample || !out_iterations || !out_evaluations
        || !std::isfinite(orbit.osculating_period_days)
        || !(orbit.osculating_period_days > 0.0)
        || !std::isfinite(orbit.mean_anomaly_rad)) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    const bool reverse = (flags & TAIYIN_ORBITAL_EVENT_REVERSE) != 0u;
    const double direction = reverse ? -1.0 : 1.0;
    const double period = orbit.osculating_period_days;
    const double mean_motion = TAIYIN_TWO_PI / period;
    // A search started exactly at an event asks for the following/previous
    // occurrence, rather than echoing that same root.
    const SplitJulianDate origin_jd = jd_start + direction * std::max(
        4.0 * kTimeToleranceDays, period * 1.0e-8);
    const double phase_delta = directed_angle_delta(orbit.mean_anomaly_rad, phase_target_rad, reverse);
    const SplitJulianDate seed_jd = origin_jd + phase_delta / mean_motion;
    const double half_window = period / 16.0;
    const SplitJulianDate initial_jd = seed_jd - half_window;
    const SplitJulianDate final_jd = seed_jd + half_window;

    OrbitalSample previous;
    Status status = eval_relative_orbital_state(
        context, body_id, model, initial_jd, use_ut, flags, reference_frame_id, &previous, diagnostic);
    ++(*out_evaluations);
    if (status != TAIYIN_STATUS_OK) return status;
    SplitJulianDate previous_jd = initial_jd;

    for (int i = 1; i <= 8; ++i) {
        const SplitJulianDate jd = i == 8 ? final_jd
            : initial_jd + (final_jd - initial_jd) * static_cast<double>(i) / 8.0;
        OrbitalSample sample;
        status = eval_relative_orbital_state(
            context, body_id, model, jd, use_ut, flags, reference_frame_id, &sample, diagnostic);
        ++(*out_evaluations);
        if (status != TAIYIN_STATUS_OK) return status;
        if (sign_matches(value(previous), value(sample))) {
            status = refine_bracketed_root(
                context, body_id, model, use_ut, flags, reference_frame_id,
                previous_jd, previous, jd, sample,
                value, derivative, out_jd, out_sample, out_iterations,
                out_evaluations, diagnostic);
            if (status != TAIYIN_STATUS_OK) return status;
            if ((!reverse && *out_jd > jd_start + kTimeToleranceDays)
                || (reverse && *out_jd < jd_start - kTimeToleranceDays)) {
                return TAIYIN_STATUS_OK;
            }
        }
        previous = sample;
        previous_jd = jd;
    }

    // The osculating seed is intentionally only an optimization. A full local
    // period scan proves that an event was not missed when perturbations move
    // the real event outside its two-body prediction window.
    const double scan_span = 1.25 * period;
    const SplitJulianDate scan_start = reverse ? origin_jd - scan_span : origin_jd;
    const SplitJulianDate scan_end = reverse ? origin_jd : origin_jd + scan_span;
    if (reverse) {
        status = eval_relative_orbital_state(
            context, body_id, model, scan_end, use_ut, flags, reference_frame_id, &previous, diagnostic);
        ++(*out_evaluations);
        if (status != TAIYIN_STATUS_OK) return status;
        previous_jd = scan_end;
        for (int i = 1; i <= kFallbackScanSegments; ++i) {
            const SplitJulianDate jd = i == kFallbackScanSegments ? scan_start
                : scan_end - (scan_end - scan_start) * static_cast<double>(i) / kFallbackScanSegments;
            OrbitalSample sample;
            status = eval_relative_orbital_state(
                context, body_id, model, jd, use_ut, flags, reference_frame_id, &sample, diagnostic);
            ++(*out_evaluations);
            if (status != TAIYIN_STATUS_OK) return status;
            if (sign_matches(value(sample), value(previous))) {
                SplitJulianDate candidate_jd = invalid_jd();
                OrbitalSample candidate;
                int candidate_iterations = 0;
                status = refine_bracketed_root(
                    context, body_id, model, use_ut, flags, reference_frame_id,
                    jd, sample, previous_jd, previous,
                    value, derivative, &candidate_jd, &candidate, &candidate_iterations,
                    out_evaluations, diagnostic);
                if (status != TAIYIN_STATUS_OK) return status;
                if (candidate_jd < jd_start - kTimeToleranceDays) {
                    *out_jd = candidate_jd;
                    *out_sample = candidate;
                    *out_iterations = candidate_iterations;
                    return TAIYIN_STATUS_OK;
                }
            }
            previous = sample;
            previous_jd = jd;
        }
        return TAIYIN_EVENT_ERROR_NOT_FOUND;
    }

    status = eval_relative_orbital_state(
        context, body_id, model, scan_start, use_ut, flags, reference_frame_id, &previous, diagnostic);
    ++(*out_evaluations);
    if (status != TAIYIN_STATUS_OK) return status;
    previous_jd = scan_start;
    for (int i = 1; i <= kFallbackScanSegments; ++i) {
        const SplitJulianDate jd = i == kFallbackScanSegments ? scan_end
            : scan_start + (scan_end - scan_start) * static_cast<double>(i) / kFallbackScanSegments;
        OrbitalSample sample;
        status = eval_relative_orbital_state(
            context, body_id, model, jd, use_ut, flags, reference_frame_id, &sample, diagnostic);
        ++(*out_evaluations);
        if (status != TAIYIN_STATUS_OK) return status;
        if (sign_matches(value(previous), value(sample))) {
            SplitJulianDate candidate_jd = invalid_jd();
            OrbitalSample candidate;
            int candidate_iterations = 0;
            status = refine_bracketed_root(
                context, body_id, model, use_ut, flags, reference_frame_id,
                previous_jd, previous, jd, sample,
                value, derivative, &candidate_jd, &candidate, &candidate_iterations,
                out_evaluations, diagnostic);
            if (status != TAIYIN_STATUS_OK) return status;
            if ((!reverse && candidate_jd > jd_start + kTimeToleranceDays)
                || (reverse && candidate_jd < jd_start - kTimeToleranceDays)) {
                *out_jd = candidate_jd;
                *out_sample = candidate;
                *out_iterations = candidate_iterations;
                return TAIYIN_STATUS_OK;
            }
        }
        previous = sample;
        previous_jd = jd;
    }
    return TAIYIN_EVENT_ERROR_NOT_FOUND;
}

Status calc_body_osculating_orbit_impl(
    const NativeCalcContext* context,
    int body_id,
    SplitJulianDate jd,
    bool use_ut,
    int reference_frame_id,
    uint64_t flags,
    BodyOsculatingOrbit* out,
    EphemerisEvalDiagnostic* diagnostic
) noexcept {
    if (out) *out = BodyOsculatingOrbit();
    if (diagnostic) *diagnostic = EphemerisEvalDiagnostic();
    BodyOrbitModel model;
    if (!context || !out || !split_julian_date_is_finite(jd) || !supported_orbital_reference_frame(reference_frame_id)
        || !valid_orbital_calculation_flags(flags)
        || !body_orbit_model(body_id, &model)) {
        set_diagnostic(diagnostic, TAIYIN_ERROR_INVALID_ARGUMENT, body_id, 0, jd);
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    OrbitalSample sample;
    const Status status = eval_relative_orbital_state(
        context, body_id, model, jd, use_ut, flags, reference_frame_id, &sample, diagnostic);
    if (status != TAIYIN_STATUS_OK) return status;
    if (!compute_orbit_elements(body_id, model, sample, reference_frame_id, out)) {
        set_diagnostic(diagnostic, TAIYIN_ERROR_UNSUPPORTED, body_id, model.center_id, jd);
        return TAIYIN_ERROR_UNSUPPORTED;
    }
    return TAIYIN_STATUS_OK;
}

Status search_next_body_apsis_impl(
    const NativeCalcContext* context,
    int body_id,
    BodyApsisKind kind,
    SplitJulianDate jd_start,
    bool use_ut,
    uint64_t flags,
    BodyApsisSearchResult* out,
    EphemerisEvalDiagnostic* diagnostic
) noexcept {
    if (out) *out = BodyApsisSearchResult();
    if (diagnostic) *diagnostic = EphemerisEvalDiagnostic();
    BodyOrbitModel model;
    if (!context || !out || !split_julian_date_is_finite(jd_start) || !valid_orbital_search_flags(flags)
        || (kind != TAIYIN_BODY_APSIS_PERICENTER && kind != TAIYIN_BODY_APSIS_APOCENTER)
        || !body_orbit_model(body_id, &model)) {
        set_diagnostic(diagnostic, TAIYIN_ERROR_INVALID_ARGUMENT, body_id, 0, jd_start);
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    BodyOsculatingOrbit orbit;
    Status status = calc_body_osculating_orbit_impl(
        context,
        body_id,
        jd_start,
        use_ut,
        TAIYIN_APPARENT_FRAME_J2000_ECLIPTIC,
        flags & TAIYIN_ORBITAL_EVENT_POSITION_FLAGS_MASK,
        &orbit,
        diagnostic);
    if (status != TAIYIN_STATUS_OK) return status;

    SplitJulianDate root_jd = invalid_jd();
    OrbitalSample root_sample;
    int iterations = 0;
    int evaluations = 1;
    const double phase_target = kind == TAIYIN_BODY_APSIS_PERICENTER ? 0.0 : TAIYIN_PI;
    status = search_next_root(
        context, body_id, model, orbit, jd_start, use_ut, flags,
        TAIYIN_APPARENT_FRAME_J2000_ECLIPTIC, phase_target,
        [](const OrbitalSample& sample) { return sample.radial_numerator_au2_per_day; },
        [kind](double lower, double upper) { return apsis_sign_matches(kind, lower, upper); },
        [](const OrbitalSample& sample) { return sample.radial_numerator_rate_au2_per_day2; },
        &root_jd, &root_sample, &iterations, &evaluations, diagnostic);
    if (status != TAIYIN_STATUS_OK) return status;

    out->body_id = body_id;
    out->center_id = model.center_id;
    out->kind = kind;
    out->jd = root_jd;
    out->distance_au = root_sample.distance_au;
    out->radial_velocity_au_per_day = root_sample.radial_numerator_au2_per_day / root_sample.distance_au;
    out->iteration_count = iterations;
    out->evaluation_count = evaluations;
    return TAIYIN_STATUS_OK;
}

Status search_next_body_node_impl(
    const NativeCalcContext* context,
    int body_id,
    BodyNodeKind kind,
    SplitJulianDate jd_start,
    bool use_ut,
    int reference_frame_id,
    uint64_t flags,
    BodyNodeSearchResult* out,
    EphemerisEvalDiagnostic* diagnostic
) noexcept {
    if (out) *out = BodyNodeSearchResult();
    if (diagnostic) *diagnostic = EphemerisEvalDiagnostic();
    BodyOrbitModel model;
    if (!context || !out || !split_julian_date_is_finite(jd_start) || !supported_orbital_reference_frame(reference_frame_id)
        || !valid_orbital_search_flags(flags)
        || (kind != TAIYIN_BODY_NODE_ASCENDING && kind != TAIYIN_BODY_NODE_DESCENDING)
        || !body_orbit_model(body_id, &model)) {
        set_diagnostic(diagnostic, TAIYIN_ERROR_INVALID_ARGUMENT, body_id, 0, jd_start);
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    BodyOsculatingOrbit orbit;
    Status status = calc_body_osculating_orbit_impl(
        context,
        body_id,
        jd_start,
        use_ut,
        reference_frame_id,
        flags & TAIYIN_ORBITAL_EVENT_POSITION_FLAGS_MASK,
        &orbit,
        diagnostic);
    if (status != TAIYIN_STATUS_OK) return status;
    if (orbit.inclination_rad <= 1.0e-10 || orbit.eccentricity >= 1.0) {
        set_diagnostic(diagnostic, TAIYIN_ERROR_UNSUPPORTED, body_id, model.center_id, jd_start);
        return TAIYIN_ERROR_UNSUPPORTED;
    }

    const double true_target = kind == TAIYIN_BODY_NODE_ASCENDING
        ? normalize_radians(-orbit.argument_of_periapsis_rad)
        : normalize_radians(TAIYIN_PI - orbit.argument_of_periapsis_rad);
    const double phase_target = mean_anomaly_from_true_anomaly(true_target, orbit.eccentricity);
    SplitJulianDate root_jd = invalid_jd();
    OrbitalSample root_sample;
    int iterations = 0;
    int evaluations = 1;
    status = search_next_root(
        context, body_id, model, orbit, jd_start, use_ut, flags, reference_frame_id, phase_target,
        [](const OrbitalSample& sample) { return sample.reference_position_au.z; },
        [kind](double lower, double upper) { return node_sign_matches(kind, lower, upper); },
        [](const OrbitalSample& sample) { return sample.reference_coordinate_velocity_au_per_day.z; },
        &root_jd, &root_sample, &iterations, &evaluations, diagnostic);
    if (status != TAIYIN_STATUS_OK) return status;

    out->body_id = body_id;
    out->center_id = model.center_id;
    out->reference_frame_id = reference_frame_id;
    out->kind = kind;
    out->jd = root_jd;
    out->reference_plane_angle_rad = normalize_radians(std::atan2(
        root_sample.reference_position_au.y, root_sample.reference_position_au.x));
    out->distance_au = root_sample.distance_au;
    out->iteration_count = iterations;
    out->evaluation_count = evaluations;
    return TAIYIN_STATUS_OK;
}

}  // namespace

BodyOrbitReferencePoint::BodyOrbitReferencePoint() noexcept
    : position_au(),
      longitude_rad(NAN),
      latitude_rad(NAN),
      distance_au(NAN) {}

BodyOrbitReferencePoints::BodyOrbitReferencePoints() noexcept
    : body_id(0),
      center_id(0),
      reference_frame_id(TAIYIN_APPARENT_FRAME_J2000_ECLIPTIC),
      model(TAIYIN_BODY_ORBIT_REFERENCE_POINTS_OSCULATING),
      ascending_node(),
      descending_node(),
      periapsis(),
      apoapsis(),
      second_focus() {}

BodyOsculatingOrbit::BodyOsculatingOrbit() noexcept
    : body_id(0),
      center_id(0),
      reference_frame_id(TAIYIN_APPARENT_FRAME_J2000_ECLIPTIC),
      gravitational_parameter_au3_per_day2(NAN),
      semi_major_axis_au(NAN),
      eccentricity(NAN),
      inclination_rad(NAN),
      longitude_of_ascending_node_rad(NAN),
      argument_of_periapsis_rad(NAN),
      true_anomaly_rad(NAN),
      mean_anomaly_rad(NAN),
      periapsis_distance_au(NAN),
      apoapsis_distance_au(NAN),
      osculating_period_days(NAN),
      current_distance_au(NAN),
      radial_velocity_au_per_day(NAN) {}

BodyApsisSearchResult::BodyApsisSearchResult() noexcept
    : body_id(0),
      center_id(0),
      kind(TAIYIN_BODY_APSIS_PERICENTER),
      jd(invalid_jd()),
      distance_au(NAN),
      radial_velocity_au_per_day(NAN),
      iteration_count(0),
      evaluation_count(0) {}

BodyNodeSearchResult::BodyNodeSearchResult() noexcept
    : body_id(0),
      center_id(0),
      reference_frame_id(TAIYIN_APPARENT_FRAME_J2000_ECLIPTIC),
      kind(TAIYIN_BODY_NODE_ASCENDING),
      jd(invalid_jd()),
      reference_plane_angle_rad(NAN),
      distance_au(NAN),
      iteration_count(0),
      evaluation_count(0) {}

Status calc_body_osculating_orbit_tt(
    const NativeCalcContext* context,
    int body_id,
    SplitJulianDate jd_tt,
    int reference_frame_id,
    uint64_t flags,
    BodyOsculatingOrbit* out,
    EphemerisEvalDiagnostic* diagnostic
) noexcept {
    return calc_body_osculating_orbit_impl(
        context, body_id, jd_tt, false, reference_frame_id, flags, out, diagnostic);
}

Status calc_body_osculating_orbit_ut(
    const NativeCalcContext* context,
    int body_id,
    SplitJulianDate jd_ut,
    int reference_frame_id,
    uint64_t flags,
    BodyOsculatingOrbit* out,
    EphemerisEvalDiagnostic* diagnostic
) noexcept {
    return calc_body_osculating_orbit_impl(
        context, body_id, jd_ut, true, reference_frame_id, flags, out, diagnostic);
}

Status calc_body_orbit_reference_points_tt(
    const NativeCalcContext* context,
    int body_id,
    SplitJulianDate jd_tt,
    int reference_frame_id,
    uint64_t flags,
    BodyOrbitReferencePoints* out,
    EphemerisEvalDiagnostic* diagnostic
) noexcept {
    return calc_body_orbit_reference_points_impl(
        context, body_id, jd_tt, false, reference_frame_id, flags, out, diagnostic);
}

Status calc_body_orbit_reference_points_ut(
    const NativeCalcContext* context,
    int body_id,
    SplitJulianDate jd_ut,
    int reference_frame_id,
    uint64_t flags,
    BodyOrbitReferencePoints* out,
    EphemerisEvalDiagnostic* diagnostic
) noexcept {
    return calc_body_orbit_reference_points_impl(
        context, body_id, jd_ut, true, reference_frame_id, flags, out, diagnostic);
}

Status search_next_body_apsis_tt(
    const NativeCalcContext* context,
    int body_id,
    BodyApsisKind kind,
    SplitJulianDate jd_start_tt,
    uint64_t flags,
    BodyApsisSearchResult* out,
    EphemerisEvalDiagnostic* diagnostic
) noexcept {
    return search_next_body_apsis_impl(
        context, body_id, kind, jd_start_tt, false, flags, out, diagnostic);
}

Status search_next_body_apsis_ut(
    const NativeCalcContext* context,
    int body_id,
    BodyApsisKind kind,
    SplitJulianDate jd_start_ut,
    uint64_t flags,
    BodyApsisSearchResult* out,
    EphemerisEvalDiagnostic* diagnostic
) noexcept {
    return search_next_body_apsis_impl(
        context, body_id, kind, jd_start_ut, true, flags, out, diagnostic);
}

Status search_next_body_plane_node_tt(
    const NativeCalcContext* context,
    int body_id,
    BodyNodeKind kind,
    SplitJulianDate jd_start_tt,
    int reference_frame_id,
    uint64_t flags,
    BodyNodeSearchResult* out,
    EphemerisEvalDiagnostic* diagnostic
) noexcept {
    return search_next_body_node_impl(
        context, body_id, kind, jd_start_tt, false, reference_frame_id, flags, out, diagnostic);
}

Status search_next_body_plane_node_ut(
    const NativeCalcContext* context,
    int body_id,
    BodyNodeKind kind,
    SplitJulianDate jd_start_ut,
    int reference_frame_id,
    uint64_t flags,
    BodyNodeSearchResult* out,
    EphemerisEvalDiagnostic* diagnostic
) noexcept {
    return search_next_body_node_impl(
        context, body_id, kind, jd_start_ut, true, reference_frame_id, flags, out, diagnostic);
}

}  // namespace runtime
}  // namespace taiyin
