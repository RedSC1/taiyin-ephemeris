#include "taiyin/runtime/phenomena.h"

#include "taiyin/angle.h"
#include "taiyin/apparent_position.h"
#include "taiyin/body_id.h"
#include "taiyin/internal/body_disc_radius.h"
#include "taiyin/geodetic_constants.h"
#include "taiyin/physical_constants.h"
#include "taiyin/runtime/native_position.h"
#include "taiyin/time.h"

#include "runtime/events/phenomena_internal.h"

#include <algorithm>
#include <cmath>

namespace taiyin {
namespace runtime {
namespace {

constexpr uint64_t kNativePositionFlagsMask = 0xffffffffull;
constexpr double kMoonAbsoluteMagnitudeH = 0.21;
constexpr double kPlutoAbsoluteMagnitudeH = -0.55;
constexpr double kDefaultMinorPlanetSlopeG = 0.15;

typedef Status (*PositionFunction)(
    const NativeCalcContext*,
    int,
    const SplitJulianDate&,
    uint32_t,
    double[6],
    EphemerisEvalDiagnostic*);

void clear_phenomena(BodyPhenomena* out) noexcept {
    if (!out) return;
    out->phase_angle_rad = NAN;
    out->illuminated_fraction = NAN;
    out->solar_elongation_rad = NAN;
    out->apparent_diameter_rad = NAN;
    out->apparent_magnitude = NAN;
    out->horizontal_parallax_rad = NAN;
}

double norm3(const double v[3]) noexcept {
    return std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
}

double dot3(const double a[3], const double b[3]) noexcept {
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

bool negate3(const double in[3], double out[3]) noexcept {
    if (!in || !out) return false;
    out[0] = -in[0];
    out[1] = -in[1];
    out[2] = -in[2];
    return std::isfinite(out[0]) && std::isfinite(out[1]) && std::isfinite(out[2]);
}

double clamp_unit(double value) noexcept {
    return std::max(-1.0, std::min(1.0, value));
}

bool sub3(const double a[3], const double b[3], double out[3]) noexcept {
    if (!a || !b || !out) return false;
    out[0] = a[0] - b[0];
    out[1] = a[1] - b[1];
    out[2] = a[2] - b[2];
    return std::isfinite(out[0]) && std::isfinite(out[1]) && std::isfinite(out[2]);
}

double angle_between_rad(const double a[3], const double b[3]) noexcept {
    const double na = norm3(a);
    const double nb = norm3(b);
    if (!(na > 0.0) || !(nb > 0.0) || !std::isfinite(na) || !std::isfinite(nb)) {
        return NAN;
    }
    const double c = clamp_unit(dot3(a, b) / (na * nb));
    return std::acos(c);
}

Status return_internal_lookup_failure(
    Status status,
    EphemerisEvalDiagnostic* diagnostic,
    const EphemerisEvalDiagnostic& internal_diagnostic
) noexcept {
    if (diagnostic) {
        *diagnostic = internal_diagnostic;
    }
    return status;
}

double apparent_diameter_rad(double radius_km, double distance_au) noexcept {
    if (!(radius_km > 0.0) || !(distance_au > 0.0)
        || !std::isfinite(radius_km) || !std::isfinite(distance_au)) {
        return NAN;
    }
    const double distance_km = distance_au * TAIYIN_AU_KM;
    if (!(distance_km > radius_km)) {
        return TAIYIN_PI;
    }
    return 2.0 * std::asin(radius_km / distance_km);
}

double moon_horizontal_parallax_rad(double distance_au) noexcept {
    if (!(distance_au > 0.0) || !std::isfinite(distance_au)) {
        return NAN;
    }
    const double sin_hp = TAIYIN_WGS84_A_KM / (distance_au * TAIYIN_AU_KM);
    if (!(sin_hp >= -1.0 && sin_hp <= 1.0)) {
        return NAN;
    }
    return std::asin(sin_hp);
}

Status eval_geocentric_moon_distance_for_parallax(
    const NativeCalcContext* context,
    const SplitJulianDate& jd,
    uint32_t position_flags,
    PositionFunction position_fn,
    EphemerisEvalDiagnostic* diagnostic,
    double* out_distance_au
) noexcept {
    if (!context || !position_fn || !out_distance_au || !split_julian_date_is_finite(jd)) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    NativeCalcContext geocentric_context = *context;
    const Status observer_status = native_context_set_geocentric_observer(
        &geocentric_context,
        TAIYIN_BODY_EARTH,
        TAIYIN_BODY_EARTH);
    if (observer_status != TAIYIN_STATUS_OK) return observer_status;

    const uint32_t geocentric_flags =
        (position_flags | TAIYIN_NATIVE_POSITION_XYZ) & ~TAIYIN_NATIVE_POSITION_TOPOCENTRIC;
    double moon[6] = {};
    const Status status = position_fn(
        &geocentric_context,
        TAIYIN_BODY_MOON,
        jd,
        geocentric_flags,
        moon,
        diagnostic);
    if (status != TAIYIN_STATUS_OK) return status;
    const double distance = norm3(moon);
    if (!(distance > 0.0) || !std::isfinite(distance)) {
        return TAIYIN_EPHEMERIS_ERROR_EVAL_FAILED;
    }
    *out_distance_au = distance;
    return TAIYIN_STATUS_OK;
}

bool lon_lat_from_vector_rad(const double v[3], double* out_lon, double* out_lat) noexcept {
    if (!v || !out_lon || !out_lat) return false;
    const double r = norm3(v);
    if (!(r > 0.0) || !std::isfinite(r)) return false;
    *out_lon = std::atan2(v[1], v[0]);
    *out_lat = std::asin(clamp_unit(v[2] / r));
    return std::isfinite(*out_lon) && std::isfinite(*out_lat);
}

double julian_year_from_jd(const SplitJulianDate& jd) noexcept {
    return 2000.0 + julian_centuries_from_j2000(jd) * 100.0;
}

struct BodyFixedAxes {
    double x[3];
    double y[3];
    double z[3];
};

bool body_fixed_axes_j2000(
    double pole_ra_deg,
    double pole_dec_deg,
    double prime_meridian_deg,
    BodyFixedAxes* out
) noexcept {
    if (!out || !std::isfinite(pole_ra_deg) || !std::isfinite(pole_dec_deg)
        || !std::isfinite(prime_meridian_deg)) {
        return false;
    }
    const double a = pole_ra_deg * TAIYIN_DEG_TO_RAD;
    const double d = pole_dec_deg * TAIYIN_DEG_TO_RAD;
    const double w = prime_meridian_deg * TAIYIN_DEG_TO_RAD;
    const double sin_a = std::sin(a);
    const double cos_a = std::cos(a);
    const double sin_d = std::sin(d);
    const double cos_d = std::cos(d);
    const double sin_w = std::sin(w);
    const double cos_w = std::cos(w);

    out->x[0] = -sin_w * sin_a - cos_w * sin_d * cos_a;
    out->x[1] =  sin_w * cos_a - cos_w * sin_d * sin_a;
    out->x[2] =  cos_w * cos_d;
    out->y[0] =  cos_w * sin_a - sin_w * sin_d * cos_a;
    out->y[1] = -cos_w * cos_a - sin_w * sin_d * sin_a;
    out->y[2] =  sin_w * cos_d;
    out->z[0] =  cos_d * cos_a;
    out->z[1] =  cos_d * sin_a;
    out->z[2] =  sin_d;
    return true;
}

bool body_fixed_lon_lat_deg(
    const double vector_from_body[3],
    const BodyFixedAxes& axes,
    double* out_lon_deg,
    double* out_lat_deg
) noexcept {
    if (!vector_from_body || !out_lon_deg || !out_lat_deg) return false;
    const double r = norm3(vector_from_body);
    if (!(r > 0.0) || !std::isfinite(r)) return false;
    const double x = dot3(vector_from_body, axes.x);
    const double y = dot3(vector_from_body, axes.y);
    const double z = dot3(vector_from_body, axes.z);
    *out_lon_deg = normalize_degrees(std::atan2(y, x) * TAIYIN_RAD_TO_DEG);
    *out_lat_deg = std::asin(clamp_unit(z / r)) * TAIYIN_RAD_TO_DEG;
    return std::isfinite(*out_lon_deg) && std::isfinite(*out_lat_deg);
}

double planetographic_from_planetocentric_deg(double planetocentric_deg, double one_minus_e2) noexcept {
    if (!std::isfinite(planetocentric_deg) || !(one_minus_e2 > 0.0)) return NAN;
    return std::atan(std::tan(planetocentric_deg * TAIYIN_DEG_TO_RAD) / one_minus_e2)
        * TAIYIN_RAD_TO_DEG;
}

double distance_magnitude_term(double sun_body_distance_au, double observer_body_distance_au) noexcept {
    if (!(sun_body_distance_au > 0.0) || !(observer_body_distance_au > 0.0)
        || !std::isfinite(sun_body_distance_au) || !std::isfinite(observer_body_distance_au)) {
        return NAN;
    }
    return 5.0 * std::log10(sun_body_distance_au * observer_body_distance_au);
}

double sun_apparent_magnitude(double observer_body_distance_au) noexcept {
    if (!(observer_body_distance_au > 0.0) || !std::isfinite(observer_body_distance_au)) {
        return NAN;
    }
    return -26.74 + 5.0 * std::log10(observer_body_distance_au);
}

double moon_apparent_magnitude(
    double phase_angle_deg,
    double observer_body_distance_au,
    double sun_body_distance_au,
    bool before_full
) noexcept {
    if (!std::isfinite(phase_angle_deg)) return NAN;
    const double a = std::fabs(phase_angle_deg);
    if (!(a <= 150.0)) {
        const double crescent = 180.0 - a;
        if (!(crescent > 0.0)) return NAN;
        const double distance_factor =
            observer_body_distance_au * sun_body_distance_au * TAIYIN_AU_KM / TAIYIN_WGS84_A_KM;
        if (!(distance_factor > 0.0) || !std::isfinite(distance_factor)) return NAN;
        return -4.5444 - 7.5 * std::log10(crescent) + 5.0 * std::log10(distance_factor);
    }
    const double a2 = a * a;
    const double a3 = a2 * a;
    const double a4 = a2 * a2;
    const double a5 = a4 * a;
    const double phase_term = before_full
        ? a * (2.9994e-2 + a * (-1.6057e-4 + a * (3.1543e-6
            + a * (-2.0667e-8 + 6.2553e-11 * a))))
        : 3.3234e-2 * a - 3.0725e-4 * a2 + 6.1575e-6 * a3
            - 4.7723e-8 * a4 + 1.4681e-10 * a5;
    const double distance_term = distance_magnitude_term(sun_body_distance_au, observer_body_distance_au);
    return std::isfinite(distance_term) ? kMoonAbsoluteMagnitudeH + distance_term + phase_term : NAN;
}

double stirling_fourth_order_10deg(const double table[36], double angle_deg) noexcept {
    if (!table || !std::isfinite(angle_deg)) return NAN;
    const double wrapped = normalize_degrees(angle_deg);
    const int zero_point = static_cast<int>(wrapped / 10.0);
    const double p1 = wrapped / 10.0 - static_cast<double>(zero_point);
    const double p2 = p1 * p1;
    const double p3 = p2 * p1;
    const double p4 = p3 * p1;

    const int bin_count = 36;
    // The Mars correction tables are 10-degree circular grids; use the requested
    // grid point as the central value and wrap the two neighbors on each side.
    const double ym2 = table[(zero_point + bin_count - 2) % bin_count];
    const double ym1 = table[(zero_point + bin_count - 1) % bin_count];
    const double y0 = table[zero_point % bin_count];
    const double yp1 = table[(zero_point + 1) % bin_count];
    const double yp2 = table[(zero_point + 2) % bin_count];

    const double d0 = ym1 - ym2;
    const double d1 = y0 - ym1;
    const double d2 = yp1 - y0;
    const double d3 = yp2 - yp1;
    const double e0 = d1 - d0;
    const double e1 = d2 - d1;
    const double e2 = d3 - d2;
    const double f0 = e1 - e0;
    const double f1 = e2 - e1;
    const double g0 = f1 - f0;

    const double a4 = g0 / 24.0;
    const double a3 = (f0 + f1) / 12.0;
    const double a2 = e1 / 2.0 - a4;
    const double a1 = (d1 + d2) / 2.0 - a3;
    return y0 + a1 * p1 + a2 * p2 + a3 * p3 + a4 * p4;
}

double mars_magnitude_correction(char kind, double angle_deg) noexcept {
    static const double rotation[36] = {
         0.024,  0.034,  0.036,  0.045,  0.038,  0.023,  0.015,  0.011,
         0.000, -0.012, -0.018, -0.036, -0.044, -0.059, -0.060, -0.055,
        -0.043, -0.041, -0.041, -0.036, -0.036, -0.018, -0.038, -0.011,
         0.002,  0.004,  0.018,  0.019,  0.035,  0.050,  0.035,  0.027,
         0.037,  0.048,  0.025,  0.022
    };
    static const double orbit[36] = {
        -0.030, -0.017, -0.029, -0.017, -0.014, -0.006, -0.018, -0.020,
        -0.014, -0.030, -0.008, -0.040, -0.024, -0.037, -0.036, -0.032,
         0.010,  0.010, -0.001,  0.044,  0.025, -0.004, -0.016, -0.008,
         0.029, -0.054, -0.033,  0.055,  0.017,  0.052,  0.006,  0.087,
         0.006,  0.064,  0.030,  0.019
    };
    return kind == 'R'
        ? stirling_fourth_order_10deg(rotation, angle_deg)
        : stirling_fourth_order_10deg(orbit, angle_deg);
}

bool mars_physical_angles_deg(
    const SplitJulianDate& jd,
    const double observer_to_body_j2000[3],
    const double sun_to_body_j2000[3],
    const double sun_to_body_ecliptic[3],
    double* out_effective_cm_deg,
    double* out_ls_deg
) noexcept {
    if (!observer_to_body_j2000 || !sun_to_body_j2000 || !sun_to_body_ecliptic
        || !out_effective_cm_deg || !out_ls_deg || !split_julian_date_is_finite(jd)) {
        return false;
    }
    const double t = julian_centuries_from_j2000(jd);
    const double d = jd - SPLIT_JD_J2000;
    BodyFixedAxes axes;
    if (!body_fixed_axes_j2000(
            317.68143 - 0.1061 * t,
            52.88650 - 0.0609 * t,
            normalize_degrees(176.630 + 350.89198226 * d),
            &axes)) {
        return false;
    }

    double body_to_observer[3] = {};
    double body_to_sun[3] = {};
    if (!negate3(observer_to_body_j2000, body_to_observer)
        || !negate3(sun_to_body_j2000, body_to_sun)) {
        return false;
    }
    double observer_lon = 0.0;
    double observer_lat = 0.0;
    double sun_lon = 0.0;
    double sun_lat = 0.0;
    if (!body_fixed_lon_lat_deg(body_to_observer, axes, &observer_lon, &observer_lat)
        || !body_fixed_lon_lat_deg(body_to_sun, axes, &sun_lon, &sun_lat)) {
        return false;
    }

    double effective_cm = 0.5 * (observer_lon + sun_lon);
    if (std::fabs(observer_lon - sun_lon) > 180.0) effective_cm += 180.0;
    *out_effective_cm_deg = normalize_degrees(effective_cm);

    double heliocentric_lon = 0.0;
    double heliocentric_lat = 0.0;
    if (!lon_lat_from_vector_rad(sun_to_body_ecliptic, &heliocentric_lon, &heliocentric_lat)) {
        return false;
    }
    *out_ls_deg = normalize_degrees(heliocentric_lon * TAIYIN_RAD_TO_DEG - 85.0);
    return true;
}

bool uranus_effective_planetographic_latitude_deg(
    const double observer_to_body_j2000[3],
    const double sun_to_body_j2000[3],
    double* out_latitude_deg
) noexcept {
    if (!observer_to_body_j2000 || !sun_to_body_j2000 || !out_latitude_deg) return false;
    BodyFixedAxes axes;
    if (!body_fixed_axes_j2000(257.311, -15.175, 0.0, &axes)) return false;

    double body_to_observer[3] = {};
    double body_to_sun[3] = {};
    if (!negate3(observer_to_body_j2000, body_to_observer)
        || !negate3(sun_to_body_j2000, body_to_sun)) {
        return false;
    }
    double observer_lon = 0.0;
    double observer_lat_centric = 0.0;
    double sun_lon = 0.0;
    double sun_lat_centric = 0.0;
    if (!body_fixed_lon_lat_deg(body_to_observer, axes, &observer_lon, &observer_lat_centric)
        || !body_fixed_lon_lat_deg(body_to_sun, axes, &sun_lon, &sun_lat_centric)) {
        return false;
    }
    const double one_minus_e2 = (24973.0 * 24973.0) / (25559.0 * 25559.0);
    const double observer_lat = planetographic_from_planetocentric_deg(observer_lat_centric, one_minus_e2);
    const double sun_lat = planetographic_from_planetocentric_deg(sun_lat_centric, one_minus_e2);
    if (!std::isfinite(observer_lat) || !std::isfinite(sun_lat)) return false;
    *out_latitude_deg = 0.5 * (std::fabs(observer_lat) + std::fabs(sun_lat));
    return std::isfinite(*out_latitude_deg);
}

double saturn_ring_brightness_term(
    const SplitJulianDate& jd,
    double phase_angle_deg,
    const double observer_body[3],
    const double sun_to_body[3]
) noexcept {
    double geocentric_lon = 0.0;
    double geocentric_lat = 0.0;
    double heliocentric_lon = 0.0;
    double heliocentric_lat = 0.0;
    if (!lon_lat_from_vector_rad(observer_body, &geocentric_lon, &geocentric_lat)
        || !lon_lat_from_vector_rad(sun_to_body, &heliocentric_lon, &heliocentric_lat)) {
        return NAN;
    }
    const double t = julian_centuries_from_j2000(jd);
    const double ring_inclination =
        (28.075216 - 0.012998 * t + 0.000004 * t * t) * TAIYIN_DEG_TO_RAD;
    const double ascending_node =
        (169.508470 + 1.394681 * t + 0.000412 * t * t) * TAIYIN_DEG_TO_RAD;

    const double sin_b_earth =
        std::sin(ring_inclination) * std::cos(geocentric_lat) * std::sin(geocentric_lon - ascending_node)
        - std::cos(ring_inclination) * std::sin(geocentric_lat);
    const double sin_b_sun =
        std::sin(ring_inclination) * std::cos(heliocentric_lat) * std::sin(heliocentric_lon - ascending_node)
        - std::cos(ring_inclination) * std::sin(heliocentric_lat);
    if (!std::isfinite(sin_b_earth) || !std::isfinite(sin_b_sun)) return NAN;
    const double earth_ring_lat = std::asin(clamp_unit(sin_b_earth));
    const double sun_ring_lat = std::asin(clamp_unit(sin_b_sun));
    const double ring_tilt = earth_ring_lat * sun_ring_lat > 0.0
        ? std::fabs(std::sin(std::sqrt(std::fabs(earth_ring_lat * sun_ring_lat))))
        : 0.0;
    return -8.914
        - 1.825 * ring_tilt
        + 0.026 * phase_angle_deg
        - 0.378 * ring_tilt * std::exp(-2.25 * phase_angle_deg);
}

double jupiter_phase_magnitude_term(double phase_angle_deg) noexcept {
    if (phase_angle_deg <= 12.0) {
        return -9.395 + phase_angle_deg * (-3.7e-4 + 6.16e-4 * phase_angle_deg);
    }
    const double x = phase_angle_deg / 180.0;
    const double albedo = 1.0 + x * (-1.507 + x * (-0.363 + x * (-0.062 + x * (2.809 - 1.876 * x))));
    if (!(albedo > 0.0) || !std::isfinite(albedo)) return NAN;
    return -9.428 - 2.5 * std::log10(albedo);
}

double saturn_phase_magnitude_term(double phase_angle_deg, double ring_term) noexcept {
    if (phase_angle_deg <= 6.5) {
        return ring_term;
    }
    return NAN;
}

double neptune_phase_magnitude_term(const SplitJulianDate& jd, double phase_angle_deg) noexcept {
    const double year = julian_year_from_jd(jd);
    double unit_magnitude = NAN;
    if (year > 2000.0) {
        unit_magnitude = -7.00;
    } else if (year < 1980.0) {
        unit_magnitude = -6.89;
    } else {
        unit_magnitude = -6.89 - 0.0054 * (year - 1980.0);
    }
    if (phase_angle_deg > 1.9) {
        if (!(year > 2000.0)) return NAN;
        unit_magnitude += phase_angle_deg * (7.944e-3 + 9.617e-5 * phase_angle_deg);
    }
    return unit_magnitude;
}

double hg_phase_function(double phase_angle_deg, double h, double g) noexcept {
    if (!std::isfinite(phase_angle_deg) || !std::isfinite(h) || !std::isfinite(g)
        || phase_angle_deg < 0.0 || phase_angle_deg >= 120.0) {
        return NAN;
    }
    const double tan_half = std::tan(0.5 * phase_angle_deg * TAIYIN_DEG_TO_RAD);
    if (!(tan_half >= 0.0) || !std::isfinite(tan_half)) return NAN;
    const double phi1 = std::exp(-3.332 * std::pow(tan_half, 0.631));
    const double phi2 = std::exp(-1.862 * std::pow(tan_half, 1.218));
    const double q = (1.0 - g) * phi1 + g * phi2;
    if (!(q > 0.0) || !std::isfinite(q)) return NAN;
    return h - 2.5 * std::log10(q);
}

double empirical_apparent_magnitude(
    int body_id,
    const SplitJulianDate& jd,
    double phase_angle_rad,
    double observer_body_distance_au,
    double sun_body_distance_au,
    double apparent_diameter_rad,
    const double observer_body[3],
    const double sun_to_body[3],
    const double observer_body_j2000[3],
    const double sun_to_body_j2000[3],
    const double sun_to_body_ecliptic[3],
    bool moon_waxing
) noexcept {
    const double phase_angle_deg = phase_angle_rad * TAIYIN_RAD_TO_DEG;
    if (body_id == TAIYIN_BODY_SUN) {
        return sun_apparent_magnitude(observer_body_distance_au);
    }
    if (!std::isfinite(phase_angle_deg)) return NAN;
    if (body_id == TAIYIN_BODY_MOON) {
        return moon_apparent_magnitude(
            phase_angle_deg, observer_body_distance_au, sun_body_distance_au, moon_waxing);
    }

    const double distance_term = distance_magnitude_term(sun_body_distance_au, observer_body_distance_au);
    if (!std::isfinite(distance_term)) return NAN;
    const double a = phase_angle_deg;
    switch (body_id) {
    case TAIYIN_BODY_MERCURY:
        return -0.613 + a * (6.3280e-2 + a * (-1.6336e-3
            + a * (3.3644e-5 + a * (-3.4265e-7
            + a * (1.6893e-9 - 3.0334e-12 * a))))) + distance_term;
    case TAIYIN_BODY_VENUS:
        if (a <= 163.7) {
            return -4.384 + a * (-1.044e-3 + a * (3.687e-4
                + a * (-2.814e-6 + 8.938e-9 * a))) + distance_term;
        }
        return 236.05828 + a * (-2.81914 + 8.39034e-3 * a) + distance_term;
    case TAIYIN_BODY_MARS:
    {
        double effective_cm = 0.0;
        double ls = 0.0;
        if (!mars_physical_angles_deg(
                jd, observer_body_j2000, sun_to_body_j2000, sun_to_body_ecliptic, &effective_cm, &ls)) {
            return NAN;
        }
        const double rotation_correction = mars_magnitude_correction('R', effective_cm);
        const double orbit_correction = mars_magnitude_correction('O', ls);
        if (!std::isfinite(rotation_correction) || !std::isfinite(orbit_correction)) return NAN;
        return (a <= 50.0
            ? -1.601 + a * (0.02267 - 0.0001302 * a)
            : -0.367 + a * (-0.02573 + 0.0003445 * a))
            + rotation_correction + orbit_correction + distance_term;
    }
    case TAIYIN_BODY_JUPITER:
    {
        const double phase_term = jupiter_phase_magnitude_term(a);
        return std::isfinite(phase_term) ? phase_term + distance_term : NAN;
    }
    case TAIYIN_BODY_SATURN: {
        const double saturn_term = saturn_ring_brightness_term(jd, a, observer_body, sun_to_body);
        const double phase_term = saturn_phase_magnitude_term(a, saturn_term);
        return std::isfinite(phase_term) ? phase_term + distance_term : NAN;
    }
    case TAIYIN_BODY_URANUS: {
        double sub_lat = 0.0;
        if (!uranus_effective_planetographic_latitude_deg(observer_body_j2000, sun_to_body_j2000, &sub_lat)) {
            return NAN;
        }
        const double phase_term = a <= 3.1
            ? 0.0
            : a * (6.587e-3 + 1.045e-4 * a);
        return -7.110 - 0.00084 * sub_lat + phase_term + distance_term;
    }
    case TAIYIN_BODY_NEPTUNE:
    {
        const double phase_term = neptune_phase_magnitude_term(jd, a);
        return std::isfinite(phase_term) ? phase_term + distance_term : NAN;
    }
    case TAIYIN_BODY_PLUTO:
    {
        const double phase_term = hg_phase_function(
            a, kPlutoAbsoluteMagnitudeH, kDefaultMinorPlanetSlopeG);
        return std::isfinite(phase_term) ? phase_term + distance_term : NAN;
    }
    default:
        return NAN;
    }
}

Status moon_phase_angle_at(
    const NativeCalcContext* context,
    const SplitJulianDate& jd,
    uint32_t position_flags,
    PositionFunction position_fn,
    EphemerisEvalDiagnostic* diagnostic,
    double* out_phase
) noexcept {
    if (!context || !position_fn || !out_phase || !split_julian_date_is_finite(jd)) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    double moon[6] = {};
    Status status = position_fn(context, TAIYIN_BODY_MOON, jd, position_flags, moon, diagnostic);
    if (status != TAIYIN_STATUS_OK) return status;
    double sun[6] = {};
    status = position_fn(context, TAIYIN_BODY_SUN, jd, position_flags, sun, diagnostic);
    if (status != TAIYIN_STATUS_OK) return status;
    double sun_to_moon[3] = {};
    if (!sub3(moon, sun, sun_to_moon)) {
        return TAIYIN_EPHEMERIS_ERROR_EVAL_FAILED;
    }
    *out_phase = angle_between_rad(moon, sun_to_moon);
    return std::isfinite(*out_phase) ? TAIYIN_STATUS_OK : TAIYIN_EPHEMERIS_ERROR_EVAL_FAILED;
}

Status estimate_moon_before_full(
    const NativeCalcContext* context,
    const SplitJulianDate& jd,
    uint32_t position_flags,
    PositionFunction position_fn,
    EphemerisEvalDiagnostic* diagnostic,
    bool* out_before_full
) noexcept {
    if (!out_before_full) return TAIYIN_ERROR_INVALID_ARGUMENT;
    const double step_days = 1.0 / 24.0;
    double before = NAN;
    double after = NAN;
    Status status = moon_phase_angle_at(
        context, jd - step_days, position_flags, position_fn, diagnostic, &before);
    if (status != TAIYIN_STATUS_OK) return status;
    status = moon_phase_angle_at(
        context, jd + step_days, position_flags, position_fn, diagnostic, &after);
    if (status != TAIYIN_STATUS_OK) return status;
    *out_before_full = after < before;
    return TAIYIN_STATUS_OK;
}

Status calc_body_phenomena_with_position_fn(
    const NativeCalcContext* context,
    int body_id,
    const SplitJulianDate& jd,
    uint64_t flags,
    BodyPhenomena* out,
    EphemerisEvalDiagnostic* diagnostic,
    PositionFunction position_fn
) noexcept {
    clear_phenomena(out);
    if (!context || !out || !split_julian_date_is_finite(jd) || !position_fn) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    if ((flags & ~kNativePositionFlagsMask) != 0ull) {
        return TAIYIN_ERROR_UNSUPPORTED;
    }
    const double radius_km = ::taiyin::internal::body_disc_radius_km(
        body_id, ::taiyin::internal::BodyDiscRadiusConvention::ApparentDisc);
    if (!std::isfinite(radius_km)) {
        return TAIYIN_ERROR_UNSUPPORTED;
    }

    const uint32_t position_flags = static_cast<uint32_t>(flags) | TAIYIN_NATIVE_POSITION_XYZ;
    double body[6] = {};
    Status status = position_fn(context, body_id, jd, position_flags, body, diagnostic);
    if (status != TAIYIN_STATUS_OK) return status;
    const EphemerisEvalDiagnostic primary_diagnostic =
        diagnostic ? *diagnostic : EphemerisEvalDiagnostic();
    EphemerisEvalDiagnostic internal_diagnostic;

    const double body_distance_au = norm3(body);
    if (!(body_distance_au > 0.0) || !std::isfinite(body_distance_au)) {
        return TAIYIN_EPHEMERIS_ERROR_EVAL_FAILED;
    }

    out->apparent_diameter_rad = apparent_diameter_rad(radius_km, body_distance_au);

    if (body_id == TAIYIN_BODY_SUN) {
        out->phase_angle_rad = 0.0;
        out->illuminated_fraction = 1.0;
        out->solar_elongation_rad = 0.0;
        out->apparent_magnitude = empirical_apparent_magnitude(
            body_id,
            jd,
            out->phase_angle_rad,
            body_distance_au,
            NAN,
            out->apparent_diameter_rad,
            body,
            nullptr,
            nullptr,
            nullptr,
            nullptr,
            false);
        if (!std::isfinite(out->apparent_diameter_rad)
            || !std::isfinite(out->apparent_magnitude)) {
            return TAIYIN_EPHEMERIS_ERROR_EVAL_FAILED;
        }
        if (diagnostic) *diagnostic = primary_diagnostic;
        return TAIYIN_STATUS_OK;
    }

    double sun[6] = {};
    status = position_fn(context, TAIYIN_BODY_SUN, jd, position_flags, sun, &internal_diagnostic);
    if (status != TAIYIN_STATUS_OK) {
        return return_internal_lookup_failure(status, diagnostic, internal_diagnostic);
    }

    double sun_to_body[3] = {};
    if (!sub3(body, sun, sun_to_body)) {
        return TAIYIN_EPHEMERIS_ERROR_EVAL_FAILED;
    }

    const double phase = angle_between_rad(body, sun_to_body);
    const double elongation = angle_between_rad(body, sun);
    const double sun_body_distance_au = norm3(sun_to_body);
    if (!std::isfinite(phase) || !std::isfinite(elongation)
        || !std::isfinite(out->apparent_diameter_rad)
        || !(sun_body_distance_au > 0.0) || !std::isfinite(sun_body_distance_au)) {
        return TAIYIN_EPHEMERIS_ERROR_EVAL_FAILED;
    }

    out->phase_angle_rad = phase;
    out->illuminated_fraction = 0.5 * (1.0 + std::cos(phase));
    out->solar_elongation_rad = elongation;
    const double* magnitude_body = body;
    const double* magnitude_sun_to_body = sun_to_body;
    const double* magnitude_body_j2000 = nullptr;
    const double* magnitude_sun_to_body_j2000 = nullptr;
    const double* magnitude_sun_to_body_ecliptic = nullptr;
    bool moon_before_full = false;
    if (body_id == TAIYIN_BODY_MOON) {
        status = estimate_moon_before_full(
            context, jd, position_flags, position_fn, &internal_diagnostic, &moon_before_full);
        if (status != TAIYIN_STATUS_OK) {
            return return_internal_lookup_failure(status, diagnostic, internal_diagnostic);
        }
    }
    double saturn_ecliptic_body[6] = {};
    double saturn_ecliptic_sun[6] = {};
    double saturn_ecliptic_sun_to_body[3] = {};
    double physical_j2000_body[6] = {};
    double physical_j2000_sun[6] = {};
    double physical_j2000_sun_to_body[3] = {};
    double physical_ecliptic_body[6] = {};
    double physical_ecliptic_sun[6] = {};
    double physical_ecliptic_sun_to_body[3] = {};
    if (body_id == TAIYIN_BODY_MARS || body_id == TAIYIN_BODY_URANUS) {
        NativeCalcContext j2000_context = *context;
        j2000_context.apparent_options.output_frame_id = TAIYIN_APPARENT_FRAME_J2000_MEAN_EQUATOR;
        const uint32_t j2000_position_flags =
            position_flags & ~(TAIYIN_NATIVE_POSITION_EQUATORIAL | TAIYIN_NATIVE_POSITION_NONUT);
        status = position_fn(
            &j2000_context,
            body_id,
            jd,
            j2000_position_flags,
            physical_j2000_body,
            &internal_diagnostic);
        if (status != TAIYIN_STATUS_OK) {
            return return_internal_lookup_failure(status, diagnostic, internal_diagnostic);
        }
        status = position_fn(
            &j2000_context,
            TAIYIN_BODY_SUN,
            jd,
            j2000_position_flags,
            physical_j2000_sun,
            &internal_diagnostic);
        if (status != TAIYIN_STATUS_OK) {
            return return_internal_lookup_failure(status, diagnostic, internal_diagnostic);
        }
        if (!sub3(physical_j2000_body, physical_j2000_sun, physical_j2000_sun_to_body)) {
            return TAIYIN_EPHEMERIS_ERROR_EVAL_FAILED;
        }
        magnitude_body_j2000 = physical_j2000_body;
        magnitude_sun_to_body_j2000 = physical_j2000_sun_to_body;
    }
    if (body_id == TAIYIN_BODY_MARS || body_id == TAIYIN_BODY_SATURN) {
        NativeCalcContext ecliptic_context = *context;
        ecliptic_context.apparent_options.output_frame_id = TAIYIN_APPARENT_FRAME_TRUE_ECLIPTIC_OF_DATE;
        const uint32_t ecliptic_position_flags =
            position_flags & ~TAIYIN_NATIVE_POSITION_EQUATORIAL;
        status = position_fn(
            &ecliptic_context,
            body_id,
            jd,
            ecliptic_position_flags,
            body_id == TAIYIN_BODY_SATURN ? saturn_ecliptic_body : physical_ecliptic_body,
            &internal_diagnostic);
        if (status != TAIYIN_STATUS_OK) {
            return return_internal_lookup_failure(status, diagnostic, internal_diagnostic);
        }
        status = position_fn(
            &ecliptic_context,
            TAIYIN_BODY_SUN,
            jd,
            ecliptic_position_flags,
            body_id == TAIYIN_BODY_SATURN ? saturn_ecliptic_sun : physical_ecliptic_sun,
            &internal_diagnostic);
        if (status != TAIYIN_STATUS_OK) {
            return return_internal_lookup_failure(status, diagnostic, internal_diagnostic);
        }
        if (body_id == TAIYIN_BODY_SATURN
            && !sub3(saturn_ecliptic_body, saturn_ecliptic_sun, saturn_ecliptic_sun_to_body)) {
            return TAIYIN_EPHEMERIS_ERROR_EVAL_FAILED;
        }
        if (body_id == TAIYIN_BODY_MARS
            && !sub3(physical_ecliptic_body, physical_ecliptic_sun, physical_ecliptic_sun_to_body)) {
            return TAIYIN_EPHEMERIS_ERROR_EVAL_FAILED;
        }
        if (body_id == TAIYIN_BODY_SATURN) {
            magnitude_body = saturn_ecliptic_body;
            magnitude_sun_to_body = saturn_ecliptic_sun_to_body;
        } else {
            magnitude_sun_to_body_ecliptic = physical_ecliptic_sun_to_body;
        }
    }
    out->apparent_magnitude = empirical_apparent_magnitude(
        body_id,
        jd,
        phase,
        body_distance_au,
        sun_body_distance_au,
        out->apparent_diameter_rad,
        magnitude_body,
        magnitude_sun_to_body,
        magnitude_body_j2000,
        magnitude_sun_to_body_j2000,
        magnitude_sun_to_body_ecliptic,
        moon_before_full);
    if (!std::isfinite(out->apparent_magnitude)) {
        return TAIYIN_EPHEMERIS_ERROR_EVAL_FAILED;
    }
    if (body_id == TAIYIN_BODY_MOON) {
        double geocentric_moon_distance_au = NAN;
        status = eval_geocentric_moon_distance_for_parallax(
            context,
            jd,
            position_flags,
            position_fn,
            &internal_diagnostic,
            &geocentric_moon_distance_au);
        if (status != TAIYIN_STATUS_OK) {
            return return_internal_lookup_failure(status, diagnostic, internal_diagnostic);
        }
        out->horizontal_parallax_rad = moon_horizontal_parallax_rad(geocentric_moon_distance_au);
        if (!std::isfinite(out->horizontal_parallax_rad)) {
            return TAIYIN_EPHEMERIS_ERROR_EVAL_FAILED;
        }
    }
    if (diagnostic) *diagnostic = primary_diagnostic;
    return TAIYIN_STATUS_OK;
}

}  // namespace

namespace internal {

double phenomena_sun_apparent_magnitude(double observer_body_distance_au) noexcept {
    return sun_apparent_magnitude(observer_body_distance_au);
}

double phenomena_moon_apparent_magnitude(
    double phase_angle_deg,
    double observer_body_distance_au,
    double sun_body_distance_au,
    bool before_full
) noexcept {
    return moon_apparent_magnitude(
        phase_angle_deg, observer_body_distance_au, sun_body_distance_au, before_full);
}

double phenomena_mars_magnitude_correction(char kind, double angle_deg) noexcept {
    return mars_magnitude_correction(kind, angle_deg);
}

double phenomena_neptune_phase_magnitude_term(SplitJulianDate jd, double phase_angle_deg) noexcept {
    return neptune_phase_magnitude_term(jd, phase_angle_deg);
}

double phenomena_hg_phase_function(double phase_angle_deg, double h, double g) noexcept {
    return hg_phase_function(phase_angle_deg, h, g);
}

}  // namespace internal

BodyPhenomena::BodyPhenomena() noexcept
    : phase_angle_rad(NAN),
      illuminated_fraction(NAN),
      solar_elongation_rad(NAN),
      apparent_diameter_rad(NAN),
      apparent_magnitude(NAN),
      horizontal_parallax_rad(NAN) {}

Status calc_body_phenomena_tt(
    const NativeCalcContext* context,
    int body_id,
    const SplitJulianDate& jd_tt,
    uint64_t flags,
    BodyPhenomena* out,
    EphemerisEvalDiagnostic* diagnostic
) noexcept {
    return calc_body_phenomena_with_position_fn(
        context,
        body_id,
        jd_tt,
        flags,
        out,
        diagnostic,
        &calc_position_tt);
}

Status calc_body_phenomena_ut(
    const NativeCalcContext* context,
    int body_id,
    const SplitJulianDate& jd_ut,
    uint64_t flags,
    BodyPhenomena* out,
    EphemerisEvalDiagnostic* diagnostic
) noexcept {
    return calc_body_phenomena_with_position_fn(
        context,
        body_id,
        jd_ut,
        flags,
        out,
        diagnostic,
        &calc_position_ut);
}

}  // namespace runtime
}  // namespace taiyin
