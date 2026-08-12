#include "runtime/eclipse/solar_eclipse_besselian_solver.h"
#include "runtime/eclipse/solar_eclipse_direct_solver.h"

#include "runtime/eclipse/eclipse_time.h"
#include "runtime/apparent/fast_apparent.h"
#include "runtime/eclipse/solar_shadow_geometry.h"
#include "runtime/eclipse/solar_route_geometry.h"

#include "taiyin/body_id.h"
#include "taiyin/dispatch.h"
#include "taiyin/earth_rotation.h"
#include "taiyin/geodetic_constants.h"
#include "taiyin/observer.h"
#include "taiyin/runtime/lunar_limb.h"
#include "taiyin/runtime/runtime.h"
#include "taiyin/time.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <vector>

namespace taiyin {
namespace runtime {

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

namespace {

SplitJulianDate invalid_jd() noexcept {
    return SplitJulianDate(0, std::numeric_limits<double>::quiet_NaN());
}

constexpr double kAuKm = 149597870.7;
constexpr double kSunRadiusKm = 695700.0;
constexpr double kMoonAlmanacRadiusRatio = 0.2725076;
constexpr double kSolarNodeLimitDeg = 23.0;
constexpr double kEarthAxisRatio = TAIYIN_WGS84_B_KM / TAIYIN_WGS84_A_KM;
constexpr size_t kBesselianElementScratchCapacity = 96;

struct BesselianElementScratchEntry {
    SplitJulianDate jd_tt;
    double t_hours;
    uint64_t flags;
    FastApparentCorrectionSeries* corrections;
    SolarBesselianElements elements;
    bool used;
};

struct BesselianElementScratch {
    BesselianElementScratchEntry entries[kBesselianElementScratchCapacity];
    size_t next;
};

double degnorm(double x) noexcept {
    x = std::fmod(x, 360.0);
    if (x < 0.0) x += 360.0;
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

double clamp_unit(double x) noexcept {
    if (x < -1.0) return -1.0;
    if (x > 1.0) return 1.0;
    return x;
}

double solar_meeus_f(int k) noexcept {
    const double T = static_cast<double>(k) / 1236.85;
    const double T2 = T * T;
    const double T3 = T2 * T;
    const double T4 = T3 * T;
    return degnorm(160.7108 + 390.67050274 * k
                   - 0.0016341 * T2
                   - 0.00000227 * T3
                   + 0.000000011 * T4);
}

double solar_meeus_f_normalized(int k) noexcept {
    double F = solar_meeus_f(k);
    if (F > 180.0) F -= 180.0;
    return F;
}

bool solar_meeus_filter_passes(int k) noexcept {
    const double F = solar_meeus_f_normalized(k);
    return F <= kSolarNodeLimitDeg || F >= (180.0 - kSolarNodeLimitDeg);
}

SplitJulianDate solar_meeus_new_moon_jd(int k) noexcept {
    const double T = static_cast<double>(k) / 1236.85;
    const double T2 = T * T;
    const double T3 = T2 * T;
    const double T4 = T3 * T;
    const double M = degnorm(2.5534 + 29.10535669 * k
                             - 0.0000218 * T2 - 0.00000011 * T3);
    const double M_prime = degnorm(201.5643 + 385.81693528 * k
                                   + 0.1017438 * T2
                                   + 0.00001239 * T3
                                   + 0.000000058 * T4);
    const double E = 1.0 - 0.002516 * T - 0.0000074 * T2;
    SplitJulianDate tjd(
        2451550,
        0.09765
            + 29.530588853 * k
            + 0.0001337 * T2
            - 0.000000150 * T3
            + 0.00000000073 * T4);
    tjd += -0.4075 * std::sin(M_prime * M_PI / 180.0)
           + 0.1721 * E * std::sin(M * M_PI / 180.0);
    return tjd;
}

double moon_radius_km(uint8_t moon_radius_model) noexcept {
    dispatch::EclipseMoonRadiusModelEntry entry;
    if (dispatch::find_eclipse_moon_radius_model(static_cast<int>(moon_radius_model), &entry)) {
        return entry.radius_km;
    }
    return kMoonAlmanacRadiusRatio * TAIYIN_WGS84_A_KM;
}

double dot3(const double a[3], const double b[3]) noexcept {
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

double norm3(const double a[3]) noexcept {
    return std::sqrt(dot3(a, a));
}

void cross3(const double a[3], const double b[3], double out[3]) noexcept {
    out[0] = a[1] * b[2] - a[2] * b[1];
    out[1] = a[2] * b[0] - a[0] * b[2];
    out[2] = a[0] * b[1] - a[1] * b[0];
}

bool normalize3(double v[3]) noexcept {
    const double n = norm3(v);
    if (!(n > 0.0) || !std::isfinite(n)) return false;
    v[0] /= n;
    v[1] /= n;
    v[2] /= n;
    return true;
}

bool plane_basis(const double axis_unit[3], double x_hat[3], double y_hat[3]) noexcept {
    const double z_axis[3] = {0.0, 0.0, 1.0};
    const double x_axis[3] = {1.0, 0.0, 0.0};
    cross3(z_axis, axis_unit, x_hat);
    if (norm3(x_hat) < 1e-12) {
        cross3(x_axis, axis_unit, x_hat);
    }
    if (!normalize3(x_hat)) return false;
    cross3(axis_unit, x_hat, y_hat);
    return normalize3(y_hat);
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

Status eval_solar_equatorial_vectors_km(
    const NativeCalcContext* context,
    SplitJulianDate jd_tt,
    uint64_t flags,
    FastApparentCorrectionSeries* corrections,
    double moon_km[3],
    double sun_km[3],
    EphemerisEvalDiagnostic* diagnostic
) noexcept {
    if (!context || !moon_km || !sun_km) return TAIYIN_ERROR_INVALID_ARGUMENT;
    const double tdb_minus_tt = dispatch::eval_tdb(
        context->model_context.tdb_model_id, jd_tt, 0);
    SplitJulianDate jd_tdb = jd_tt;
    if (!add_seconds_to_split_jd(jd_tdb, tdb_minus_tt, &jd_tdb)) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }

    FastApparentOptions options;
    options.frame = FAST_APPARENT_TRUE_EQUATOR_OF_DATE;
    options.with_velocity = false;
    options.true_position = (flags & TAIYIN_ECLIPSE_TRUEPOS) != 0;
    FastApparentCorrectionEpochSample correction_sample;
    if (corrections) {
        FastApparentCorrectionConfig correction_config;
        const Status correction_status =
            get_fast_correction(
                context,
                TAIYIN_BODY_MOON,
                TAIYIN_BODY_SUN,
                options,
                correction_config,
                jd_tt,
                corrections,
                diagnostic,
                &correction_sample);
        if (correction_status != TAIYIN_STATUS_OK) return correction_status;
        options.correction_sample = &correction_sample;
    }
    FastApparentBody2State pair;
    const Status st = eval_fast_apparent_body_2_tdb(
        context, jd_tdb, jd_tt, TAIYIN_BODY_MOON, TAIYIN_BODY_SUN, options, &pair, diagnostic);
    if (st != TAIYIN_STATUS_OK) return st;

    moon_km[0] = pair.body_0.position_au.x * kAuKm;
    moon_km[1] = pair.body_0.position_au.y * kAuKm;
    moon_km[2] = pair.body_0.position_au.z * kAuKm;
    sun_km[0] = pair.body_1.position_au.x * kAuKm;
    sun_km[1] = pair.body_1.position_au.y * kAuKm;
    sun_km[2] = pair.body_1.position_au.z * kAuKm;
    return TAIYIN_STATUS_OK;
}

// Compute the shadow geometry shared by the polynomial fit and exact contact
// refinement.  This deliberately excludes UT/GAST: penumbral contact against
// the oblate Earth depends on x/y/zeta, the cone radius, and declination, but
// not on the Greenwich hour angle stored in mu_deg.
Status compute_besselian_shadow_geometry_tt(
    const NativeCalcContext* context,
    SplitJulianDate jd_tt,
    double t_hours,
    uint64_t flags,
    FastApparentCorrectionSeries* corrections,
    SolarBesselianElements* out,
    double* out_axis_ra_deg,
    EphemerisEvalDiagnostic* diagnostic
) noexcept {
    if (!context || !out || !split_julian_date_is_finite(jd_tt) || !std::isfinite(t_hours)) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    std::memset(out, 0, sizeof(*out));

    double moon_km[3] = {};
    double sun_km[3] = {};
    const Status st = eval_solar_equatorial_vectors_km(
        context, jd_tt, flags, corrections, moon_km, sun_km, diagnostic);
    if (st != TAIYIN_STATUS_OK) return st;

    double axis_unit[3] = {
        moon_km[0] - sun_km[0],
        moon_km[1] - sun_km[1],
        moon_km[2] - sun_km[2]
    };
    const double sun_moon_km = norm3(axis_unit);
    if (!normalize3(axis_unit) || !(sun_moon_km > 0.0)) {
        return TAIYIN_EPHEMERIS_ERROR_EVAL_FAILED;
    }

    double x_hat[3] = {};
    double y_hat[3] = {};
    if (!plane_basis(axis_unit, x_hat, y_hat)) {
        return TAIYIN_EPHEMERIS_ERROR_EVAL_FAILED;
    }

    const double zeta_km = -dot3(moon_km, axis_unit);
    const double x = dot3(moon_km, x_hat) / TAIYIN_WGS84_A_KM;
    const double y = dot3(moon_km, y_hat) / TAIYIN_WGS84_A_KM;
    const double moon_radius = moon_radius_km(context->eclipse_moon_radius_model_id);
    const double tan_f1 = (kSunRadiusKm + moon_radius) / sun_moon_km;
    const double tan_f2 = (kSunRadiusKm - moon_radius) / sun_moon_km;
    const double l1 = (moon_radius + zeta_km * tan_f1) / TAIYIN_WGS84_A_KM;
    const double l2 = (moon_radius - zeta_km * tan_f2) / TAIYIN_WGS84_A_KM;
    const double dec_deg = std::asin(clamp_unit(axis_unit[2])) * 180.0 / M_PI;

    out->t_hours = t_hours;
    out->x = x;
    out->y = y;
    out->zeta = zeta_km / TAIYIN_WGS84_A_KM;
    out->d_deg = dec_deg;
    out->l1 = l1;
    out->l2 = l2;
    out->f1_deg = std::atan(tan_f1) * 180.0 / M_PI;
    out->f2_deg = std::atan(tan_f2) * 180.0 / M_PI;
    out->tan_f1 = tan_f1;
    out->tan_f2 = tan_f2;
    out->gamma = std::hypot(x, y);
    if (out_axis_ra_deg) {
        *out_axis_ra_deg = normalize_degrees(
            std::atan2(axis_unit[1], axis_unit[0]) * 180.0 / M_PI);
    }
    return TAIYIN_STATUS_OK;
}

Status compute_besselian_elements_tt(
    const NativeCalcContext* context,
    SplitJulianDate jd_tt,
    double t_hours,
    uint64_t flags,
    FastApparentCorrectionSeries* corrections,
    SolarBesselianElements* out,
    EphemerisEvalDiagnostic* diagnostic
) noexcept {
    double axis_ra_deg = 0.0;
    Status st = compute_besselian_shadow_geometry_tt(
        context,
        jd_tt,
        t_hours,
        flags,
        corrections,
        out,
        &axis_ra_deg,
        diagnostic);
    if (st != TAIYIN_STATUS_OK) return st;

    SplitJulianDate jd_ut;
    st = eclipse_tt_to_ut(*context, jd_tt, &jd_ut, nullptr, diagnostic);
    if (st != TAIYIN_STATUS_OK) return st;
    double gast_rad = 0.0;
    if (!context_gast_rad(context, jd_ut, jd_tt, &gast_rad)) {
        return TAIYIN_ERROR_UNSUPPORTED;
    }
    out->mu_deg = normalize_degrees(
        gast_rad * 180.0 / M_PI - axis_ra_deg);
    return TAIYIN_STATUS_OK;
}

Status compute_besselian_elements_cached_tt(
    const NativeCalcContext* context,
    SplitJulianDate jd_tt,
    double t_hours,
    uint64_t flags,
    FastApparentCorrectionSeries* corrections,
    BesselianElementScratch* scratch,
    SolarBesselianElements* out,
    EphemerisEvalDiagnostic* diagnostic
) noexcept {
    if (!scratch) {
        return compute_besselian_elements_tt(
            context, jd_tt, t_hours, flags, corrections, out, diagnostic);
    }
    for (size_t i = 0; i < kBesselianElementScratchCapacity; ++i) {
        const BesselianElementScratchEntry& entry = scratch->entries[i];
        if (entry.used
            && entry.jd_tt == jd_tt
            && entry.t_hours == t_hours
            && entry.flags == flags
            && entry.corrections == corrections) {
            *out = entry.elements;
            return TAIYIN_STATUS_OK;
        }
    }

    SolarBesselianElements computed;
    const Status st = compute_besselian_elements_tt(
        context, jd_tt, t_hours, flags, corrections, &computed, diagnostic);
    if (st != TAIYIN_STATUS_OK) return st;

    BesselianElementScratchEntry& slot =
        scratch->entries[scratch->next % kBesselianElementScratchCapacity];
    slot.jd_tt = jd_tt;
    slot.t_hours = t_hours;
    slot.flags = flags;
    slot.corrections = corrections;
    slot.elements = computed;
    slot.used = true;
    scratch->next = (scratch->next + 1u) % kBesselianElementScratchCapacity;
    *out = computed;
    return TAIYIN_STATUS_OK;
}

double polynomial_value(const double coeffs[TAIYIN_SOLAR_BESSELIAN_COEFF_COUNT], int degree, double t) noexcept {
    double value = 0.0;
    for (int i = degree; i >= 0; --i) {
        value = value * t + coeffs[i];
    }
    return value;
}

double polynomial_derivative(const double coeffs[TAIYIN_SOLAR_BESSELIAN_COEFF_COUNT], int degree, double t) noexcept {
    double value = 0.0;
    for (int i = degree; i >= 1; --i) {
        value = value * t + static_cast<double>(i) * coeffs[i];
    }
    return value;
}

bool solve_linear_system(double* matrix, double* rhs, int n) noexcept {
    for (int column = 0; column < n; ++column) {
        int pivot = column;
        for (int row = column + 1; row < n; ++row) {
            if (std::fabs(matrix[row * n + column]) > std::fabs(matrix[pivot * n + column])) {
                pivot = row;
            }
        }
        if (std::fabs(matrix[pivot * n + column]) < 1e-14) return false;
        if (pivot != column) {
            for (int j = column; j < n; ++j) std::swap(matrix[column * n + j], matrix[pivot * n + j]);
            std::swap(rhs[column], rhs[pivot]);
        }
        const double divisor = matrix[column * n + column];
        for (int j = column; j < n; ++j) matrix[column * n + j] /= divisor;
        rhs[column] /= divisor;
        for (int row = 0; row < n; ++row) {
            if (row == column) continue;
            const double factor = matrix[row * n + column];
            for (int j = column; j < n; ++j) {
                matrix[row * n + j] -= factor * matrix[column * n + j];
            }
            rhs[row] -= factor * rhs[column];
        }
    }
    return true;
}

bool fit_power_polynomial(
    const std::vector<double>& xs,
    const std::vector<double>& ys,
    int degree,
    double out[TAIYIN_SOLAR_BESSELIAN_COEFF_COUNT]
) noexcept {
    if (xs.size() != ys.size() || xs.size() < static_cast<size_t>(degree + 1)
        || degree < 1 || degree > static_cast<int>(TAIYIN_SOLAR_BESSELIAN_MAX_DEGREE)) {
        return false;
    }
    const int n = degree + 1;
    double powers[2 * TAIYIN_SOLAR_BESSELIAN_COEFF_COUNT] = {};
    for (int power = 0; power <= 2 * degree; ++power) {
        double sum = 0.0;
        for (size_t i = 0; i < xs.size(); ++i) sum += std::pow(xs[i], power);
        powers[power] = sum;
    }
    double matrix[TAIYIN_SOLAR_BESSELIAN_COEFF_COUNT * TAIYIN_SOLAR_BESSELIAN_COEFF_COUNT] = {};
    double rhs[TAIYIN_SOLAR_BESSELIAN_COEFF_COUNT] = {};
    for (int row = 0; row < n; ++row) {
        for (int col = 0; col < n; ++col) matrix[row * n + col] = powers[row + col];
        for (size_t i = 0; i < xs.size(); ++i) rhs[row] += ys[i] * std::pow(xs[i], row);
    }
    if (!solve_linear_system(matrix, rhs, n)) return false;
    for (size_t i = 0; i < TAIYIN_SOLAR_BESSELIAN_COEFF_COUNT; ++i) {
        out[i] = i < static_cast<size_t>(n) ? rhs[i] : 0.0;
    }
    return true;
}

Status evaluate_polynomial(
    const SolarBesselianPolynomial* poly,
    double t_hours,
    SolarBesselianElements* out
) noexcept {
    if (!poly || !out || poly->degree < 1
        || poly->degree > static_cast<int>(TAIYIN_SOLAR_BESSELIAN_MAX_DEGREE)) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    std::memset(out, 0, sizeof(*out));
    out->t_hours = t_hours;
    out->x = polynomial_value(poly->x, poly->degree, t_hours);
    out->y = polynomial_value(poly->y, poly->degree, t_hours);
    out->zeta = polynomial_value(poly->zeta, poly->degree, t_hours);
    out->d_deg = polynomial_value(poly->d_deg, poly->degree, t_hours);
    out->mu_deg = normalize_degrees(polynomial_value(poly->mu_deg, poly->degree, t_hours));
    out->l1 = polynomial_value(poly->l1, poly->degree, t_hours);
    out->l2 = polynomial_value(poly->l2, poly->degree, t_hours);
    out->f1_deg = poly->f1_deg;
    out->f2_deg = poly->f2_deg;
    out->tan_f1 = poly->tan_f1;
    out->tan_f2 = poly->tan_f2;
    out->gamma = std::hypot(out->x, out->y);
    return TAIYIN_STATUS_OK;
}

Status build_besselian_polynomial(
    const NativeCalcContext* context,
    SplitJulianDate center_jd_tt,
    double span_hours,
    double sample_step_hours,
    int degree,
    uint64_t flags,
    FastApparentCorrectionSeries* corrections,
    BesselianElementScratch* scratch,
    SolarBesselianPolynomial* out,
    EphemerisEvalDiagnostic* diagnostic
) noexcept {
    if (!context || !out || !(span_hours > 0.0) || !(sample_step_hours > 0.0)
        || degree < 1 || degree > static_cast<int>(TAIYIN_SOLAR_BESSELIAN_MAX_DEGREE)) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    std::memset(out, 0, sizeof(*out));
    out->t0_jd_tt = center_jd_tt;
    out->span_hours = span_hours;
    out->sample_step_hours = sample_step_hours;
    out->degree = degree;

    std::vector<double> offsets;
    const double half_span = span_hours * 0.5;
    for (double t = -half_span; t <= half_span + sample_step_hours * 1e-9; t += sample_step_hours) {
        offsets.push_back(std::fabs(t) < 1e-12 ? 0.0 : t);
    }
    bool has_center = false;
    for (size_t i = 0; i < offsets.size(); ++i) {
        if (std::fabs(offsets[i]) < 1e-12) has_center = true;
    }
    if (!has_center) offsets.push_back(0.0);
    if (offsets.size() < static_cast<size_t>(degree + 1)) return TAIYIN_ERROR_INVALID_ARGUMENT;

    std::vector<SolarBesselianElements> samples(offsets.size());
    for (size_t i = 0; i < offsets.size(); ++i) {
        const Status st = compute_besselian_elements_cached_tt(
            context,
            center_jd_tt + offsets[i] / 24.0,
            offsets[i],
            flags,
            corrections,
            scratch,
            &samples[i],
            diagnostic);
        if (st != TAIYIN_STATUS_OK) return st;
        if (std::fabs(offsets[i]) < 1e-12) out->center = samples[i];
    }

    std::vector<double> xs(offsets.size());
    std::vector<double> values(offsets.size());
    std::vector<double> mu_values(offsets.size());
    for (size_t i = 0; i < offsets.size(); ++i) {
        xs[i] = samples[i].t_hours;
        mu_values[i] = i == 0
            ? samples[i].mu_deg
            : mu_values[i - 1] + signed_degree_delta(samples[i].mu_deg, mu_values[i - 1]);
    }

#define FIT_FIELD(member, target) \
    do { \
        for (size_t i = 0; i < samples.size(); ++i) values[i] = samples[i].member; \
        if (!fit_power_polynomial(xs, values, degree, target)) return TAIYIN_ERROR_INTERNAL; \
    } while (0)

    FIT_FIELD(x, out->x);
    FIT_FIELD(y, out->y);
    FIT_FIELD(zeta, out->zeta);
    FIT_FIELD(d_deg, out->d_deg);
    if (!fit_power_polynomial(xs, mu_values, degree, out->mu_deg)) return TAIYIN_ERROR_INTERNAL;
    FIT_FIELD(l1, out->l1);
    FIT_FIELD(l2, out->l2);

#undef FIT_FIELD

    out->f1_deg = out->center.f1_deg;
    out->f2_deg = out->center.f2_deg;
    out->tan_f1 = out->center.tan_f1;
    out->tan_f2 = out->center.tan_f2;

    std::memset(&out->max_residual, 0, sizeof(out->max_residual));
    for (size_t i = 0; i < samples.size(); ++i) {
        SolarBesselianElements residual;
        const Status st = evaluate_polynomial(out, samples[i].t_hours, &residual);
        if (st != TAIYIN_STATUS_OK) return st;
        out->max_residual.x = std::max(out->max_residual.x, std::fabs(residual.x - samples[i].x));
        out->max_residual.y = std::max(out->max_residual.y, std::fabs(residual.y - samples[i].y));
        out->max_residual.l1 = std::max(out->max_residual.l1, std::fabs(residual.l1 - samples[i].l1));
        out->max_residual.l2 = std::max(out->max_residual.l2, std::fabs(residual.l2 - samples[i].l2));
    }
    return TAIYIN_STATUS_OK;
}

struct BesselianEvent {
    int k = 0;
    SplitJulianDate seed_jd_tt;
    SolarBesselianPolynomial poly = {};
    FastApparentCorrectionSeries corrections;
    BesselianElementScratch scratch = {};
};

double rho2_value(const SolarBesselianPolynomial& poly, double t_hours) noexcept {
    const double x = polynomial_value(poly.x, poly.degree, t_hours);
    const double y = polynomial_value(poly.y, poly.degree, t_hours);
    return x * x + y * y;
}

double rho2_derivative(const SolarBesselianPolynomial& poly, double t_hours) noexcept {
    const double x = polynomial_value(poly.x, poly.degree, t_hours);
    const double y = polynomial_value(poly.y, poly.degree, t_hours);
    const double xd = polynomial_derivative(poly.x, poly.degree, t_hours);
    const double yd = polynomial_derivative(poly.y, poly.degree, t_hours);
    return 2.0 * (x * xd + y * yd);
}

double minimize_rho2(const SolarBesselianPolynomial& poly) noexcept {
    const double half = poly.span_hours * 0.5;
    double best = 0.0;
    double best_value = rho2_value(poly, 0.0);
    const int coarse_steps = 96;
    for (int i = 0; i <= coarse_steps; ++i) {
        const double t = -half + 2.0 * half * static_cast<double>(i) / static_cast<double>(coarse_steps);
        const double value = rho2_value(poly, t);
        if (value < best_value) {
            best_value = value;
            best = t;
        }
    }

    double lo = std::max(-half, best - 0.5);
    double hi = std::min(half, best + 0.5);
    const double gr = (std::sqrt(5.0) - 1.0) * 0.5;
    double c = hi - gr * (hi - lo);
    double d = lo + gr * (hi - lo);
    double fc = rho2_value(poly, c);
    double fd = rho2_value(poly, d);
    for (int i = 0; i < 80; ++i) {
        if (fc < fd) {
            hi = d;
            d = c;
            fd = fc;
            c = hi - gr * (hi - lo);
            fc = rho2_value(poly, c);
        } else {
            lo = c;
            c = d;
            fc = fd;
            d = lo + gr * (hi - lo);
            fd = rho2_value(poly, d);
        }
    }
    return (lo + hi) * 0.5;
}

Status frame_from_elements(
    const NativeCalcContext* context,
    SplitJulianDate jd_tt,
    const SolarBesselianElements& e,
    solar_route_geometry::Frame* out,
    EphemerisEvalDiagnostic* diagnostic
) noexcept {
    SplitJulianDate jd_ut;
    Status st = eclipse_tt_to_ut(*context, jd_tt, &jd_ut, nullptr, diagnostic);
    if (st != TAIYIN_STATUS_OK) return st;
    double gast = 0.0;
    if (!context_gast_rad(context, jd_ut, jd_tt, &gast)) return TAIYIN_ERROR_UNSUPPORTED;
    const double mu = e.mu_deg * M_PI / 180.0;
    const double dec = e.d_deg * M_PI / 180.0;
    const double ra = gast - mu;
    // The polynomial axis is Moon minus Sun, while route geometry follows the
    // shadow direction from Sun through Moon. Convert the frame explicitly.
    out->right_ascension_offset_rad = ra - M_PI / 2.0;
    out->pole_rotation_rad = M_PI / 2.0 + dec;
    out->gast_rad = gast;
    return TAIYIN_STATUS_OK;
}

struct GlobalGeometry {
    double scaled_distance;
    double penumbral_margin;
    double core_margin;
    double central_discriminant;
    bool central;
    double longitude_deg;
    double latitude_deg;
};

Status eval_global_geometry(
    const NativeCalcContext* context,
    SplitJulianDate jd_tt,
    const SolarBesselianElements& e,
    GlobalGeometry* out,
    EphemerisEvalDiagnostic* diagnostic
) noexcept {
    if (!context || !out) return TAIYIN_ERROR_INVALID_ARGUMENT;
    std::memset(out, 0, sizeof(*out));
    out->longitude_deg = std::nan("");
    out->latitude_deg = std::nan("");
    solar_route_geometry::Frame frame;
    Status st = frame_from_elements(context, jd_tt, e, &frame, diagnostic);
    if (st != TAIYIN_STATUS_OK) return st;

    const double scaled = std::sqrt(e.x * e.x + (e.y / kEarthAxisRatio) * (e.y / kEarthAxisRatio));
    out->scaled_distance = scaled;
    out->penumbral_margin = scaled - (1.0 + e.l1);
    out->core_margin = scaled - (1.0 + std::fabs(e.l2));

    SolarConeEarthPoint center;
    if (!intersect_solar_shadow_axis_with_oblate_earth(
            -e.x,
            e.y,
            frame.pole_rotation_rad,
            frame.right_ascension_offset_rad - frame.gast_rad,
            kEarthAxisRatio,
            &center)) {
        return TAIYIN_EPHEMERIS_ERROR_EVAL_FAILED;
    }
    out->central = center.valid;
    out->central_discriminant = center.line_discriminant;
    if (center.valid) {
        out->longitude_deg = center.longitude_rad * 180.0 / M_PI;
        out->latitude_deg = center.latitude_rad * 180.0 / M_PI;
    }
    return TAIYIN_STATUS_OK;
}

uint32_t classify_global(const SolarBesselianElements& e, const GlobalGeometry& g) noexcept {
    if (g.penumbral_margin > 0.0) return TAIYIN_ECLIPSE_NONE;
    const uint32_t centrality = g.central ? TAIYIN_ECLIPSE_CENTRAL : TAIYIN_ECLIPSE_NONCENTRAL;
    if (g.core_margin <= 0.0) {
        return centrality | (e.l2 >= 0.0 ? TAIYIN_ECLIPSE_TOTAL : TAIYIN_ECLIPSE_ANNULAR);
    }
    return centrality | TAIYIN_ECLIPSE_PARTIAL;
}

double partial_scalar_poly(const SolarBesselianPolynomial& poly, double t_hours) noexcept {
    SolarBesselianElements e;
    if (evaluate_polynomial(&poly, t_hours, &e) != TAIYIN_STATUS_OK) return std::nan("");
    const double scaled = std::sqrt(e.x * e.x + (e.y / kEarthAxisRatio) * (e.y / kEarthAxisRatio));
    return scaled - (1.0 + e.l1);
}

double partial_cone_discriminant_poly(
    const NativeCalcContext* context,
    const SolarBesselianPolynomial& poly,
    double t_hours
) noexcept {
    SolarBesselianElements e;
    if (!context || evaluate_polynomial(&poly, t_hours, &e) != TAIYIN_STATUS_OK) {
        return std::nan("");
    }
    SolarConeEarthTangency tangency;
    if (!maximize_solar_circular_cone_earth_discriminant(
            -e.x,
            e.y,
            e.zeta,
            moon_radius_km(context->eclipse_moon_radius_model_id)
                / TAIYIN_WGS84_A_KM,
            e.l1,
            M_PI / 2.0 + e.d_deg * M_PI / 180.0,
            kEarthAxisRatio,
            &tangency)
        || !tangency.valid) {
        return std::nan("");
    }
    return tangency.normalized_discriminant;
}

double central_scalar_poly(
    const NativeCalcContext* context,
    const SolarBesselianPolynomial& poly,
    double t_hours,
    EphemerisEvalDiagnostic* diagnostic
) noexcept {
    SolarBesselianElements e;
    if (evaluate_polynomial(&poly, t_hours, &e) != TAIYIN_STATUS_OK) return std::nan("");
    GlobalGeometry g;
    if (eval_global_geometry(context, poly.t0_jd_tt + t_hours / 24.0, e, &g, diagnostic) != TAIYIN_STATUS_OK) {
        return std::nan("");
    }
    return g.central_discriminant;
}

double central_discriminant_poly(const SolarBesselianPolynomial& poly, double t_hours) noexcept {
    SolarBesselianElements e;
    if (evaluate_polynomial(&poly, t_hours, &e) != TAIYIN_STATUS_OK) return std::nan("");

    const double w = M_PI / 2.0 + e.d_deg * M_PI / 180.0;
    const double p = std::cos(w);
    const double q = std::sin(w);
    const double x1 = -e.x;
    const double y1 = p * e.y - q * 2.0;
    const double z1 = q * e.y + p * 2.0;
    const double x2 = -e.x;
    const double y2 = p * e.y;
    const double z2 = q * e.y;
    const double dx = x2 - x1;
    const double dy = y2 - y1;
    const double dz = z2 - z1;
    const double e2 = kEarthAxisRatio * kEarthAxisRatio;
    const double a = dx * dx + dy * dy + dz * dz / e2;
    const double b = x1 * dx + y1 * dy + z1 * dz / e2;
    const double c = x1 * x1 + y1 * y1 + z1 * z1 / e2 - 1.0;
    return b * b - a * c;
}

bool bracket_root_pair(
    const SolarBesselianPolynomial& poly,
    double max_t,
    bool central,
    const NativeCalcContext* context,
    double* out_before,
    double* out_after,
    EphemerisEvalDiagnostic* diagnostic
) noexcept {
    *out_before = std::nan("");
    *out_after = std::nan("");
    const double half = poly.span_hours * 0.5;
    const double step = central ? 0.025 : 0.05;
    auto scalar = [&](double t) -> double {
        if (central) {
            (void)context;
            (void)diagnostic;
            return central_discriminant_poly(poly, t);
        }
        return -partial_scalar_poly(poly, t);
    };

    double prev_t = -half;
    double prev_v = scalar(prev_t);
    for (double t = -half + step; t <= half + step * 0.5; t += step) {
        const double clamped_t = std::min(t, half);
        const double v = scalar(clamped_t);
        if (std::isfinite(prev_v) && std::isfinite(v) && prev_v * v <= 0.0) {
            double lo = prev_t;
            double hi = clamped_t;
            double flo = prev_v;
            double fhi = v;
            const int max_bisect_iterations = central ? 24 : 60;
            for (int i = 0; i < max_bisect_iterations; ++i) {
                const double mid = (lo + hi) * 0.5;
                const double fm = scalar(mid);
                if (!std::isfinite(fm)) break;
                if (flo * fm <= 0.0) {
                    hi = mid;
                    fhi = fm;
                } else {
                    lo = mid;
                    flo = fm;
                }
            }
            (void)fhi;
            const double root = (lo + hi) * 0.5;
            if (root < max_t && !std::isfinite(*out_before)) *out_before = root;
            if (root > max_t && !std::isfinite(*out_after)) *out_after = root;
        }
        prev_t = clamped_t;
        prev_v = v;
    }
    return std::isfinite(*out_before) && std::isfinite(*out_after);
}

struct ProfiledPenumbralSurfaceContext {
    Vector3 moon_km;
    Vector3 sun_km;
    Vector3 axis_unit;
    double sun_moon_km;
    PreparedLunarLimbQuery limb_query;
};

Vector3 ellipsoid_surface_from_parametric_km(
    double longitude_rad,
    double parametric_latitude_rad
) noexcept {
    const double cos_latitude = std::cos(parametric_latitude_rad);
    return Vector3{
        TAIYIN_WGS84_A_KM * cos_latitude * std::cos(longitude_rad),
        TAIYIN_WGS84_A_KM * cos_latitude * std::sin(longitude_rad),
        TAIYIN_WGS84_B_KM * std::sin(parametric_latitude_rad),
    };
}

double normalize_radians_signed(double value) noexcept {
    value = std::fmod(value + M_PI, 2.0 * M_PI);
    if (value < 0.0) value += 2.0 * M_PI;
    return value - M_PI;
}

Status eval_profiled_penumbral_surface_margin_km(
    const ProfiledPenumbralSurfaceContext& data,
    double longitude_rad,
    double parametric_latitude_rad,
    double* out_margin_km
) noexcept {
    if (!out_margin_km || !std::isfinite(longitude_rad)
        || !std::isfinite(parametric_latitude_rad)) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    const Vector3 surface = ellipsoid_surface_from_parametric_km(
        longitude_rad, parametric_latitude_rad);
    const Vector3 moon_to_surface = vector3_subtract(surface, data.moon_km);
    const double distance_along_axis_km = vector3_dot(moon_to_surface, data.axis_unit);
    if (!(distance_along_axis_km > 0.0)) return TAIYIN_EPHEMERIS_ERROR_EVAL_FAILED;
    const Vector3 axis_point = vector3_add(
        data.moon_km,
        vector3_scale(data.axis_unit, distance_along_axis_km));
    const double distance_from_axis_km = vector3_norm(vector3_subtract(surface, axis_point));

    const Vector3 topo_moon = vector3_subtract(data.moon_km, surface);
    const Vector3 topo_sun = vector3_subtract(data.sun_km, surface);
    const double moon_distance_km = vector3_norm(topo_moon);
    const double sun_distance_km = vector3_norm(topo_sun);
    if (!(moon_distance_km > 0.0) || !(sun_distance_km > 0.0)) {
        return TAIYIN_EPHEMERIS_ERROR_EVAL_FAILED;
    }
    const Vector3 moon_unit = vector3_scale(topo_moon, 1.0 / moon_distance_km);
    const Vector3 sun_unit = vector3_scale(topo_sun, 1.0 / sun_distance_km);
    Vector3 toward_sun = vector3_subtract(
        sun_unit,
        vector3_scale(moon_unit, vector3_dot(sun_unit, moon_unit)));
    const double direction_norm = vector3_norm(toward_sun);
    if (!(direction_norm > 0.0)) return TAIYIN_EPHEMERIS_ERROR_EVAL_FAILED;
    toward_sun = vector3_scale(toward_sun, 1.0 / direction_norm);

    double moon_radius_m = std::nan("");
    const Status limb_status = eval_prepared_lunar_limb_radius_m(
        &data.limb_query,
        moon_to_surface,
        toward_sun,
        &moon_radius_m);
    if (limb_status != TAIYIN_STATUS_OK) return limb_status;
    const double moon_radius_km = moon_radius_m / 1000.0;
    const double penumbra_radius_km = moon_radius_km
        + distance_along_axis_km * (kSunRadiusKm + moon_radius_km)
            / data.sun_moon_km;
    *out_margin_km = distance_from_axis_km - penumbra_radius_km;
    return std::isfinite(*out_margin_km)
        ? TAIYIN_STATUS_OK
        : TAIYIN_EPHEMERIS_ERROR_EVAL_FAILED;
}

Status minimize_profiled_penumbral_surface_margin_km(
    const ProfiledPenumbralSurfaceContext& data,
    const Vector3& initial_direction,
    double* out_margin_km
) noexcept {
    if (!out_margin_km) return TAIYIN_ERROR_INVALID_ARGUMENT;
    *out_margin_km = std::nan("");
    const double horizontal = std::hypot(initial_direction.x, initial_direction.y);
    if (!(vector3_norm(initial_direction) > 0.0)) {
        return TAIYIN_EPHEMERIS_ERROR_EVAL_FAILED;
    }
    double longitude = std::atan2(initial_direction.y, initial_direction.x);
    double latitude = std::atan2(
        initial_direction.z / TAIYIN_WGS84_B_KM,
        horizontal / TAIYIN_WGS84_A_KM);
    double current_margin = std::nan("");
    Status status = eval_profiled_penumbral_surface_margin_km(
        data, longitude, latitude, &current_margin);
    if (status != TAIYIN_STATUS_OK) return status;

    double step_rad = 0.01;
    constexpr double kMinSurfaceStepKm = 0.02;
    constexpr int kMaxIterations = 80;
    for (int iteration = 0; iteration < kMaxIterations
            && step_rad * TAIYIN_WGS84_A_KM > kMinSurfaceStepKm; ++iteration) {
        double best_longitude = longitude;
        double best_latitude = latitude;
        double best_margin = current_margin;
        const double longitude_scale = std::max(0.05, std::fabs(std::cos(latitude)));
        for (int latitude_direction = -1; latitude_direction <= 1; ++latitude_direction) {
            for (int longitude_direction = -1; longitude_direction <= 1; ++longitude_direction) {
                if (latitude_direction == 0 && longitude_direction == 0) continue;
                const double candidate_latitude = std::max(
                    -M_PI / 2.0,
                    std::min(
                        M_PI / 2.0,
                        latitude + static_cast<double>(latitude_direction) * step_rad));
                const double candidate_longitude = normalize_radians_signed(
                    longitude
                    + static_cast<double>(longitude_direction) * step_rad / longitude_scale);
                double candidate_margin = std::nan("");
                status = eval_profiled_penumbral_surface_margin_km(
                    data, candidate_longitude, candidate_latitude, &candidate_margin);
                if (status != TAIYIN_STATUS_OK) return status;
                if (candidate_margin < best_margin) {
                    best_margin = candidate_margin;
                    best_longitude = candidate_longitude;
                    best_latitude = candidate_latitude;
                }
            }
        }
        if (best_margin < current_margin - 1.0e-8) {
            longitude = best_longitude;
            latitude = best_latitude;
            current_margin = best_margin;
        } else {
            step_rad *= 0.5;
        }
    }
    *out_margin_km = current_margin;
    return TAIYIN_STATUS_OK;
}

double exact_scalar(
    const NativeCalcContext* context,
    SplitJulianDate center_jd_tt,
    SplitJulianDate jd_tt,
    uint64_t flags,
    FastApparentCorrectionSeries* corrections,
    BesselianElementScratch* scratch,
    bool central,
    EphemerisEvalDiagnostic* diagnostic
) noexcept {
    if (central) {
        SolarBesselianElements e;
        // Whether the shadow axis intersects an axisymmetric oblate Earth is
        // independent of Greenwich rotation. Avoid the complete Besselian
        // element path (TT->UT, GAST, and mu) while searching central contacts.
        const Status st = compute_besselian_shadow_geometry_tt(
            context,
            jd_tt,
            (jd_tt - center_jd_tt) * 24.0,
            flags,
            corrections,
            &e,
            nullptr,
            diagnostic);
        if (st != TAIYIN_STATUS_OK) return std::nan("");
        (void)scratch;
        SolarConeEarthPoint center;
        if (!intersect_solar_shadow_axis_with_oblate_earth(
                -e.x,
                e.y,
                M_PI / 2.0 + e.d_deg * M_PI / 180.0,
                0.0,
                kEarthAxisRatio,
                &center)) {
            return std::nan("");
        }
        return center.line_discriminant;
    }
    if (!central) {
        if ((flags & TAIYIN_ECLIPSE_LUNAR_LIMB_CORRECTION) != 0u) {
            double moon_km[3] = {};
            double sun_km[3] = {};
            const Status st = eval_solar_equatorial_vectors_km(
                context, jd_tt, flags, corrections, moon_km, sun_km, diagnostic);
            if (st != TAIYIN_STATUS_OK) return std::nan("");
            double axis_unit[3] = {
                moon_km[0] - sun_km[0],
                moon_km[1] - sun_km[1],
                moon_km[2] - sun_km[2]
            };
            const double sun_moon_km = norm3(axis_unit);
            if (!normalize3(axis_unit) || !(sun_moon_km > 0.0)) return std::nan("");
            const double s_km = -dot3(moon_km, axis_unit);
            const double closest_km[3] = {
                moon_km[0] + s_km * axis_unit[0],
                moon_km[1] + s_km * axis_unit[1],
                moon_km[2] + s_km * axis_unit[2]
            };
            ProfiledPenumbralSurfaceContext surface_context;
            surface_context.moon_km = Vector3{moon_km[0], moon_km[1], moon_km[2]};
            surface_context.sun_km = Vector3{sun_km[0], sun_km[1], sun_km[2]};
            surface_context.axis_unit = Vector3{axis_unit[0], axis_unit[1], axis_unit[2]};
            surface_context.sun_moon_km = sun_moon_km;
            const Status prepare_status = prepare_lunar_limb_query(
                context,
                jd_tt,
                (flags & TAIYIN_ECLIPSE_TRUEPOS) != 0u,
                TAIYIN_APPARENT_FRAME_TRUE_EQUATOR_OF_DATE,
                &surface_context.limb_query);
            if (prepare_status != TAIYIN_STATUS_OK) return std::nan("");
            double profiled_margin_km = std::nan("");
            const Status minimize_status = minimize_profiled_penumbral_surface_margin_km(
                surface_context,
                Vector3{closest_km[0], closest_km[1], closest_km[2]},
                &profiled_margin_km);
            if (minimize_status != TAIYIN_STATUS_OK) return std::nan("");
            return -profiled_margin_km;
        }

        SolarBesselianElements e;
        const Status st = compute_besselian_shadow_geometry_tt(
            context,
            jd_tt,
            (jd_tt - center_jd_tt) * 24.0,
            flags,
            corrections,
            &e,
            nullptr,
            diagnostic);
        if (st != TAIYIN_STATUS_OK) return std::nan("");
        SolarConeEarthTangency tangency;
        if (!maximize_solar_circular_cone_earth_discriminant(
                -e.x,
                e.y,
                e.zeta,
                moon_radius_km(context->eclipse_moon_radius_model_id)
                    / TAIYIN_WGS84_A_KM,
                e.l1,
                M_PI / 2.0 + e.d_deg * M_PI / 180.0,
                kEarthAxisRatio,
                &tangency)
            || !tangency.valid) {
            return std::nan("");
        }
        return tangency.normalized_discriminant;
    }
    return std::nan("");
}

Status refine_exact_root(
    const NativeCalcContext* context,
    SplitJulianDate center_jd_tt,
    const SolarBesselianPolynomial& poly,
    FastApparentCorrectionSeries* corrections,
    BesselianElementScratch* scratch,
    SplitJulianDate seed_jd_tt,
    uint64_t flags,
    bool central,
    SplitJulianDate* out_jd_tt,
    EphemerisEvalDiagnostic* diagnostic
) noexcept {
    if (!out_jd_tt) return TAIYIN_ERROR_INVALID_ARGUMENT;
    *out_jd_tt = invalid_jd();

    const bool profiled_partial_contact = !central
        && (flags & TAIYIN_ECLIPSE_LUNAR_LIMB_CORRECTION) != 0u;
    if (!central && !profiled_partial_contact) {
        const double derivative_step_hours = 1.0 / 3600.0;
        const double max_total_correction = 15.0 / 1440.0;
        SplitJulianDate t = seed_jd_tt;
        for (int iter = 0; iter < 3; ++iter) {
            const double f0 = exact_scalar(
                context, center_jd_tt, t, flags, corrections, scratch, false, diagnostic);
            if (!std::isfinite(f0)) break;

            const double th = (t - center_jd_tt) * 24.0;
            const double fm = partial_cone_discriminant_poly(
                context, poly, th - derivative_step_hours);
            const double fp = partial_cone_discriminant_poly(
                context, poly, th + derivative_step_hours);
            if (!std::isfinite(fm) || !std::isfinite(fp)) break;
            const double slope = (fp - fm)
                / (2.0 * derivative_step_hours / 24.0);
            if (!std::isfinite(slope) || std::fabs(slope) < 1.0e-14) break;

            const double raw_correction = -f0 / slope;
            SplitJulianDate next_t = t + raw_correction;
            const SplitJulianDate min_t = seed_jd_tt - max_total_correction;
            const SplitJulianDate max_t = seed_jd_tt + max_total_correction;
            if (next_t < min_t) next_t = min_t;
            if (next_t > max_t) next_t = max_t;
            const double applied_correction = next_t - t;
            if (!split_julian_date_is_finite(next_t) || applied_correction == 0.0) break;
            t = next_t;
            if (std::fabs(applied_correction) < 0.02 / 86400.0) {
                const double left = exact_scalar(
                    context,
                    center_jd_tt,
                    t - 30.0 / 86400.0,
                    flags,
                    corrections,
                    scratch,
                    false,
                    diagnostic);
                const double right = exact_scalar(
                    context,
                    center_jd_tt,
                    t + 30.0 / 86400.0,
                    flags,
                    corrections,
                    scratch,
                    false,
                    diagnostic);
                if (std::isfinite(left) && std::isfinite(right) && left * right <= 0.0) {
                    *out_jd_tt = t;
                    return TAIYIN_STATUS_OK;
                }
                break;
            }
        }
    }

    if (central) {
        const double derivative_step_hours = 1.0 / 60.0;
        const double max_root_correction = 10.0 / 1440.0;
        SplitJulianDate t = seed_jd_tt;
        for (int i = 0; i < 3; ++i) {
            const double f0 = exact_scalar(
                context, center_jd_tt, t, flags, corrections, scratch, true, diagnostic);
            if (!std::isfinite(f0)) break;

            const double th = (t - center_jd_tt) * 24.0;
            const double fm = central_discriminant_poly(poly, th - derivative_step_hours);
            const double fp = central_discriminant_poly(poly, th + derivative_step_hours);
            if (!std::isfinite(fm) || !std::isfinite(fp)) break;
            const double slope = (fp - fm) / (2.0 * derivative_step_hours / 24.0);
            if (!std::isfinite(slope) || std::fabs(slope) < 1.0e-14) break;

            double correction = -f0 / slope;
            if (correction > max_root_correction) correction = max_root_correction;
            if (correction < -max_root_correction) correction = -max_root_correction;
            t += correction;
            if (!split_julian_date_is_finite(t)
                || std::fabs(t - seed_jd_tt) > max_root_correction) break;
            if (std::fabs(correction) < 0.02 / 86400.0) {
                const double left = exact_scalar(
                    context,
                    center_jd_tt,
                    t - 30.0 / 86400.0,
                    flags,
                    corrections,
                    scratch,
                    true,
                    diagnostic);
                const double right = exact_scalar(
                    context,
                    center_jd_tt,
                    t + 30.0 / 86400.0,
                    flags,
                    corrections,
                    scratch,
                    true,
                    diagnostic);
                if (std::isfinite(left) && std::isfinite(right) && left * right <= 0.0) {
                    *out_jd_tt = t;
                    return TAIYIN_STATUS_OK;
                }
                break;
            }
        }
    }

    SplitJulianDate lo = seed_jd_tt - 3.0 / 1440.0;
    SplitJulianDate hi = seed_jd_tt + 3.0 / 1440.0;
    double flo = exact_scalar(context, center_jd_tt, lo, flags, corrections, scratch, central, diagnostic);
    double fhi = exact_scalar(context, center_jd_tt, hi, flags, corrections, scratch, central, diagnostic);
    for (int expand = 0; expand < 5 && std::isfinite(flo) && std::isfinite(fhi) && flo * fhi > 0.0; ++expand) {
        lo -= 3.0 / 1440.0;
        hi += 3.0 / 1440.0;
        flo = exact_scalar(context, center_jd_tt, lo, flags, corrections, scratch, central, diagnostic);
        fhi = exact_scalar(context, center_jd_tt, hi, flags, corrections, scratch, central, diagnostic);
    }
    if (!std::isfinite(flo) || !std::isfinite(fhi) || flo * fhi > 0.0) {
        return TAIYIN_ERROR_UNSUPPORTED;
    }
    for (int i = 0; i < 60 && (hi - lo) > 0.02 / 86400.0; ++i) {
        SplitJulianDate candidate = lo + 0.5 * (hi - lo);
        if (profiled_partial_contact && fhi != flo) {
            const SplitJulianDate secant = hi - fhi * (hi - lo) / (fhi - flo);
            const double guard = (hi - lo) * 0.1;
            if (split_julian_date_is_finite(secant)
                && secant > lo + guard && secant < hi - guard) {
                candidate = secant;
            }
        }
        const double fm = exact_scalar(
            context,
            center_jd_tt,
            candidate,
            flags,
            corrections,
            scratch,
            central,
            diagnostic);
        if (!std::isfinite(fm)) break;
        if (flo * fm <= 0.0) {
            hi = candidate;
            fhi = fm;
        } else {
            lo = candidate;
            flo = fm;
        }
    }
    *out_jd_tt = lo + 0.5 * (hi - lo);
    return TAIYIN_STATUS_OK;
}

Status refine_exact_maximum(
    const NativeCalcContext* context,
    SplitJulianDate center_jd_tt,
    SplitJulianDate seed_jd_tt,
    uint64_t flags,
    FastApparentCorrectionSeries* corrections,
    BesselianElementScratch* scratch,
    SplitJulianDate* out_jd_tt,
    SolarBesselianElements* out_elements,
    EphemerisEvalDiagnostic* diagnostic
) noexcept {
    SplitJulianDate t = seed_jd_tt;
    double step = 1.0 / 1440.0;
    for (int i = 0; i < 8; ++i) {
        SolarBesselianElements em, e0, ep;
        // The vertex iteration only minimizes x^2 + y^2.  It has no
        // Earth-fixed output, so avoid TT->UT/GAST and the Besselian mu angle
        // at all intermediate sample times.  A complete element set is still
        // evaluated once after convergence for the public result.
        Status st = compute_besselian_shadow_geometry_tt(
            context,
            t - step,
            (t - step - center_jd_tt) * 24.0,
            flags,
            corrections,
            &em,
            nullptr,
            diagnostic);
        if (st != TAIYIN_STATUS_OK) return st;
        st = compute_besselian_shadow_geometry_tt(
            context,
            t,
            (t - center_jd_tt) * 24.0,
            flags,
            corrections,
            &e0,
            nullptr,
            diagnostic);
        if (st != TAIYIN_STATUS_OK) return st;
        st = compute_besselian_shadow_geometry_tt(
            context,
            t + step,
            (t + step - center_jd_tt) * 24.0,
            flags,
            corrections,
            &ep,
            nullptr,
            diagnostic);
        if (st != TAIYIN_STATUS_OK) return st;
        const double fm = em.x * em.x + em.y * em.y;
        const double f0 = e0.x * e0.x + e0.y * e0.y;
        const double fp = ep.x * ep.x + ep.y * ep.y;
        const double curvature = fm - 2.0 * f0 + fp;
        if (std::fabs(curvature) > 1e-20) {
            double offset = 0.5 * (fm - fp) / curvature * step;
            if (offset > step) offset = step;
            if (offset < -step) offset = -step;
            t += offset;
        }
        step *= 0.5;
    }
    const Status st = compute_besselian_elements_cached_tt(
        context, t, (t - center_jd_tt) * 24.0, flags, corrections, scratch, out_elements, diagnostic);
    if (st != TAIYIN_STATUS_OK) return st;
    *out_jd_tt = t;
    return TAIYIN_STATUS_OK;
}

void init_result(SolarEclipseResult* out) noexcept {
    out->kind = TAIYIN_ECLIPSE_NONE;
    out->maximum_jd_tt = invalid_jd();
    out->axis_distance_km = std::nan("");
    out->penumbra_radius_km = std::nan("");
    out->core_radius_km = std::nan("");
    out->penumbral_margin_km = std::nan("");
    out->central_margin_km = std::nan("");
    out->maximum_latitude_deg = std::nan("");
    out->maximum_longitude_deg = std::nan("");
    for (size_t i = 0; i < TAIYIN_SOLAR_ECLIPSE_CONTACT_COUNT; ++i) {
        out->contact_jd_tt[i] = invalid_jd();
    }
}

struct DirectSolarEvent {
    SplitJulianDate center_jd_tt;
    FastApparentCorrectionSeries corrections;
};

Status direct_axis_distance2_km(
    const NativeCalcContext* context,
    SplitJulianDate jd_tt,
    uint64_t flags,
    FastApparentCorrectionSeries* corrections,
    double* out_distance2_km,
    EphemerisEvalDiagnostic* diagnostic
) noexcept {
    if (!context || !out_distance2_km) return TAIYIN_ERROR_INVALID_ARGUMENT;
    double moon_km[3] = {};
    double sun_km[3] = {};
    const Status st = eval_solar_equatorial_vectors_km(
        context, jd_tt, flags, corrections, moon_km, sun_km, diagnostic);
    if (st != TAIYIN_STATUS_OK) return st;

    // Shadow line L(s) = Moon + s*u, with u pointing from Sun through Moon.
    // q is the perpendicular vector from the geocenter to that 3-D line.
    double u[3] = {
        moon_km[0] - sun_km[0],
        moon_km[1] - sun_km[1],
        moon_km[2] - sun_km[2],
    };
    if (!normalize3(u)) return TAIYIN_EPHEMERIS_ERROR_EVAL_FAILED;
    const double projection = dot3(moon_km, u);
    const double q[3] = {
        moon_km[0] - projection * u[0],
        moon_km[1] - projection * u[1],
        moon_km[2] - projection * u[2],
    };
    *out_distance2_km = dot3(q, q);
    return std::isfinite(*out_distance2_km)
        ? TAIYIN_STATUS_OK
        : TAIYIN_EPHEMERIS_ERROR_EVAL_FAILED;
}

Status refine_direct_greatest(
    const NativeCalcContext* context,
    SplitJulianDate seed_jd_tt,
    uint64_t flags,
    FastApparentCorrectionSeries* corrections,
    SplitJulianDate* out_jd_tt,
    EphemerisEvalDiagnostic* diagnostic
) noexcept {
    if (!context || !corrections || !out_jd_tt) return TAIYIN_ERROR_INVALID_ARGUMENT;
    SplitJulianDate t = seed_jd_tt;

    // The first three scales move the Meeus conjunction seed onto the shadow
    // axis minimum. The following minute-scale samples refine the same 3-D
    // norm without ever constructing a Besselian polynomial.
    const double steps_days[] = {
        0.5,
        0.125,
        0.03125,
        1.0 / 1440.0,
        0.5 / 1440.0,
        0.25 / 1440.0,
        0.125 / 1440.0,
        0.0625 / 1440.0,
        0.03125 / 1440.0,
    };
    for (double step : steps_days) {
        double fm = 0.0;
        double f0 = 0.0;
        double fp = 0.0;
        Status st = direct_axis_distance2_km(
            context, t - step, flags, corrections, &fm, diagnostic);
        if (st != TAIYIN_STATUS_OK) return st;
        st = direct_axis_distance2_km(
            context, t, flags, corrections, &f0, diagnostic);
        if (st != TAIYIN_STATUS_OK) return st;
        st = direct_axis_distance2_km(
            context, t + step, flags, corrections, &fp, diagnostic);
        if (st != TAIYIN_STATUS_OK) return st;

        const double curvature = fm - 2.0 * f0 + fp;
        if (std::isfinite(curvature) && std::fabs(curvature) > 1.0e-12) {
            double offset = 0.5 * (fm - fp) / curvature * step;
            if (offset > step) offset = step;
            if (offset < -step) offset = -step;
            if (std::isfinite(offset)) t += offset;
        }
    }
    *out_jd_tt = t;
    return TAIYIN_STATUS_OK;
}

double direct_contact_scalar(
    const NativeCalcContext* context,
    DirectSolarEvent* event,
    SplitJulianDate jd_tt,
    uint64_t flags,
    bool central,
    EphemerisEvalDiagnostic* diagnostic
) noexcept {
    if (!event) return std::nan("");
    return exact_scalar(
        context,
        event->center_jd_tt,
        jd_tt,
        flags,
        &event->corrections,
        nullptr,
        central,
        diagnostic);
}

Status find_direct_contact(
    const NativeCalcContext* context,
    DirectSolarEvent* event,
    SplitJulianDate greatest_jd_tt,
    uint64_t flags,
    bool central,
    int direction,
    SplitJulianDate* out_jd_tt,
    EphemerisEvalDiagnostic* diagnostic
) noexcept {
    if (!context || !event || !out_jd_tt || (direction != -1 && direction != 1)) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    *out_jd_tt = invalid_jd();
    const double at_max = direct_contact_scalar(
        context, event, greatest_jd_tt, flags, central, diagnostic);
    if (!std::isfinite(at_max) || at_max < 0.0) return TAIYIN_STATUS_OK;

    SplitJulianDate inner_t = greatest_jd_tt;
    double inner_f = at_max;
    SplitJulianDate outer_t = invalid_jd();
    double outer_f = std::nan("");
    const double offsets_hours[] = {0.5, 1.0, 2.0, 4.0, 8.0, 12.0};
    for (double offset_hours : offsets_hours) {
        const SplitJulianDate candidate = greatest_jd_tt
            + static_cast<double>(direction) * offset_hours / 24.0;
        const double value = direct_contact_scalar(
            context, event, candidate, flags, central, diagnostic);
        if (!std::isfinite(value)) return TAIYIN_EPHEMERIS_ERROR_EVAL_FAILED;
        if (value <= 0.0) {
            outer_t = candidate;
            outer_f = value;
            break;
        }
        inner_t = candidate;
        inner_f = value;
    }
    if (!split_julian_date_is_finite(outer_t)) return TAIYIN_STATUS_OK;

    SplitJulianDate lo = direction < 0 ? outer_t : inner_t;
    SplitJulianDate hi = direction < 0 ? inner_t : outer_t;
    double flo = direction < 0 ? outer_f : inner_f;
    double fhi = direction < 0 ? inner_f : outer_f;
    if (!std::isfinite(flo) || !std::isfinite(fhi) || flo * fhi > 0.0) {
        return TAIYIN_EPHEMERIS_ERROR_EVAL_FAILED;
    }

    int last_replaced = 0;
    for (int iteration = 0; iteration < 40
         && (hi - lo) > 0.02 / 86400.0; ++iteration) {
        const double span = hi - lo;
        SplitJulianDate candidate = lo + 0.5 * span;
        if (fhi != flo) {
            const double fraction = -flo / (fhi - flo);
            if (std::isfinite(fraction) && fraction > 0.05 && fraction < 0.95) {
                candidate = lo + fraction * span;
            }
        }
        const double value = direct_contact_scalar(
            context, event, candidate, flags, central, diagnostic);
        if (!std::isfinite(value)) return TAIYIN_EPHEMERIS_ERROR_EVAL_FAILED;
        if (flo * value <= 0.0) {
            hi = candidate;
            fhi = value;
            if (last_replaced == 1) flo *= 0.5;
            last_replaced = 1;
        } else {
            lo = candidate;
            flo = value;
            if (last_replaced == -1) fhi *= 0.5;
            last_replaced = -1;
        }
    }
    *out_jd_tt = lo + 0.5 * (hi - lo);
    return TAIYIN_STATUS_OK;
}

double lite_scalar(const SolarBesselianElements& e) noexcept {
    const double y_scaled = e.y / kEarthAxisRatio;
    return e.x * e.x + y_scaled * y_scaled;
}

double lite_vertex_offset(double y_minus, double y_center, double y_plus, double step_days) noexcept {
    const double curvature = y_minus - 2.0 * y_center + y_plus;
    if (!std::isfinite(curvature) || std::fabs(curvature) < 1.0e-18) {
        return 0.0;
    }
    double offset = 0.5 * (y_minus - y_plus) / curvature * step_days;
    if (!std::isfinite(offset)) return 0.0;
    if (offset < -step_days) offset = -step_days;
    if (offset > step_days) offset = step_days;
    return offset;
}

Status refine_lite_maximum(
    const NativeCalcContext* context,
    SplitJulianDate center_jd_tt,
    uint64_t flags,
    SplitJulianDate* out_jd_tt,
    SolarBesselianElements* out_elements,
    EphemerisEvalDiagnostic* diagnostic
) noexcept {
    if (!context || !out_jd_tt || !out_elements) return TAIYIN_ERROR_INVALID_ARGUMENT;

    SplitJulianDate t = center_jd_tt;
    double step = 0.5;
    // Lite is only allowed to reject obvious non-eclipses. Keep it cheap and let
    // the full Besselian solver handle boundary or positive candidates.
    for (int i = 0; i < 3; ++i) {
        SolarBesselianElements em, e0, ep;
        Status st = compute_besselian_elements_tt(
            context, t - step, (t - step - center_jd_tt) * 24.0, flags, nullptr, &em, diagnostic);
        if (st != TAIYIN_STATUS_OK) return st;
        st = compute_besselian_elements_tt(
            context, t, (t - center_jd_tt) * 24.0, flags, nullptr, &e0, diagnostic);
        if (st != TAIYIN_STATUS_OK) return st;
        st = compute_besselian_elements_tt(
            context, t + step, (t + step - center_jd_tt) * 24.0, flags, nullptr, &ep, diagnostic);
        if (st != TAIYIN_STATUS_OK) return st;

        t += lite_vertex_offset(lite_scalar(em), lite_scalar(e0), lite_scalar(ep), step);
        step *= 0.25;
    }

    const Status st = compute_besselian_elements_tt(
        context, t, (t - center_jd_tt) * 24.0, flags, nullptr, out_elements, diagnostic);
    if (st != TAIYIN_STATUS_OK) return st;
    *out_jd_tt = t;
    return TAIYIN_STATUS_OK;
}

Status refine_meeus_solar_seed_for_besselian(
    const NativeCalcContext* context,
    SplitJulianDate meeus_seed_jd_tt,
    uint64_t flags,
    SplitJulianDate* out_jd_tt,
    EphemerisEvalDiagnostic* diagnostic
) noexcept {
    if (!context || !out_jd_tt || !split_julian_date_is_finite(meeus_seed_jd_tt)) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }

    SplitJulianDate t = meeus_seed_jd_tt;
    double step_days = (
        meeus_seed_jd_tt < SplitJulianDate(2000000, 0.0)
        || meeus_seed_jd_tt > SplitJulianDate(2500000, 0.0))
        ? 5.0
        : 1.0;
    while (step_days > 0.5) {
        SolarBesselianElements em, e0, ep;
        Status st = compute_besselian_elements_tt(
            context, t - step_days, (t - step_days - meeus_seed_jd_tt) * 24.0,
            flags, nullptr, &em, diagnostic);
        if (st != TAIYIN_STATUS_OK) return st;
        st = compute_besselian_elements_tt(
            context, t, (t - meeus_seed_jd_tt) * 24.0,
            flags, nullptr, &e0, diagnostic);
        if (st != TAIYIN_STATUS_OK) return st;
        st = compute_besselian_elements_tt(
            context, t + step_days, (t + step_days - meeus_seed_jd_tt) * 24.0,
            flags, nullptr, &ep, diagnostic);
        if (st != TAIYIN_STATUS_OK) return st;

        t += lite_vertex_offset(lite_scalar(em), lite_scalar(e0), lite_scalar(ep), step_days);
        step_days *= 0.25;
    }

    *out_jd_tt = t;
    return TAIYIN_STATUS_OK;
}

bool lite_result_uncertain(const SolarBesselianElements& e, const GlobalGeometry& g) noexcept {
    constexpr double kBoundaryGuardEarthRadii = 0.05;
    if (std::fabs(g.penumbral_margin) < kBoundaryGuardEarthRadii) return true;
    if (std::fabs(g.core_margin) < kBoundaryGuardEarthRadii) return true;
    if (std::fabs(g.scaled_distance - 1.0) < kBoundaryGuardEarthRadii) return true;
    if (std::fabs(e.l2) < kBoundaryGuardEarthRadii) return true;
    return false;
}

Status build_event(
    const NativeCalcContext* context,
    int k,
    uint64_t flags,
    BesselianEvent* event,
    EphemerisEvalDiagnostic* diagnostic
) noexcept {
    if (!context || !event) return TAIYIN_ERROR_INVALID_ARGUMENT;
    *event = BesselianEvent();
    event->k = k;
    const SplitJulianDate meeus_seed_jd_tt = solar_meeus_new_moon_jd(k);
    Status st = refine_meeus_solar_seed_for_besselian(
        context, meeus_seed_jd_tt, flags, &event->seed_jd_tt, diagnostic);
    if (st != TAIYIN_STATUS_OK) return st;

    FastApparentOptions options;
    options.frame = FAST_APPARENT_TRUE_EQUATOR_OF_DATE;
    options.with_velocity = false;
    options.true_position = (flags & TAIYIN_ECLIPSE_TRUEPOS) != 0;
    FastApparentCorrectionConfig correction_config;
    correction_config.initial_half_days = 0.4;
    correction_config.sample_step_days = 3.0 / 24.0;
    const Status correction_status = init_fast_correction_series(
        context,
        TAIYIN_BODY_MOON,
        TAIYIN_BODY_SUN,
        options,
        correction_config,
        event->seed_jd_tt,
        &event->corrections,
        diagnostic);
    if (correction_status != TAIYIN_STATUS_OK) return correction_status;

    st = build_besselian_polynomial(
        context, event->seed_jd_tt, 16.0, 1.0, 4, flags, &event->corrections, &event->scratch, &event->poly, diagnostic);
    if (st != TAIYIN_STATUS_OK) return st;
    if (event->poly.max_residual.x > 1e-7 || event->poly.max_residual.y > 1e-7) {
        st = build_besselian_polynomial(
            context, event->seed_jd_tt, 16.0, 0.5, 5, flags, &event->corrections, &event->scratch, &event->poly, diagnostic);
    }
    return st;
}

Status fill_contacts(
    const NativeCalcContext* context,
    BesselianEvent& event,
    double max_t_hours,
    SplitJulianDate max_jd_tt,
    uint64_t flags,
    SolarEclipseResult* out,
    EphemerisEvalDiagnostic* diagnostic
) noexcept {
    out->contact_jd_tt[TAIYIN_SOLAR_ECLIPSE_CONTACT_GREATEST] = max_jd_tt;
    if ((flags & TAIYIN_ECLIPSE_INCLUDE_CONTACTS) == 0) return TAIYIN_STATUS_OK;

    double p1_t = std::nan("");
    double p4_t = std::nan("");
    if (bracket_root_pair(event.poly, max_t_hours, false, context, &p1_t, &p4_t, diagnostic)) {
        Status st = refine_exact_root(
            context, event.seed_jd_tt, event.poly, &event.corrections,
            &event.scratch,
            event.seed_jd_tt + p1_t / 24.0, flags, false,
            &out->contact_jd_tt[TAIYIN_SOLAR_ECLIPSE_CONTACT_P1], diagnostic);
        if (st != TAIYIN_STATUS_OK) return st;
        st = refine_exact_root(
            context, event.seed_jd_tt, event.poly, &event.corrections,
            &event.scratch,
            event.seed_jd_tt + p4_t / 24.0, flags, false,
            &out->contact_jd_tt[TAIYIN_SOLAR_ECLIPSE_CONTACT_P4], diagnostic);
        if (st != TAIYIN_STATUS_OK) return st;
    }

    if ((out->kind & TAIYIN_ECLIPSE_CENTRAL) != 0) {
        double c1_t = std::nan("");
        double c4_t = std::nan("");
        if (bracket_root_pair(event.poly, max_t_hours, true, context, &c1_t, &c4_t, diagnostic)) {
            Status st = refine_exact_root(
                context, event.seed_jd_tt, event.poly, &event.corrections,
                &event.scratch,
                event.seed_jd_tt + c1_t / 24.0, flags, true,
                &out->contact_jd_tt[TAIYIN_SOLAR_ECLIPSE_CONTACT_C1], diagnostic);
            if (st != TAIYIN_STATUS_OK) return st;
            st = refine_exact_root(
                context, event.seed_jd_tt, event.poly, &event.corrections,
                &event.scratch,
                event.seed_jd_tt + c4_t / 24.0, flags, true,
                &out->contact_jd_tt[TAIYIN_SOLAR_ECLIPSE_CONTACT_C4], diagnostic);
            if (st != TAIYIN_STATUS_OK) return st;
        }
    }
    return TAIYIN_STATUS_OK;
}

Status refine_hybrid_kind(
    const NativeCalcContext* context,
    uint64_t flags,
    FastApparentCorrectionSeries* corrections,
    SolarEclipseResult* out,
    EphemerisEvalDiagnostic* diagnostic
) noexcept {
    if ((out->kind & TAIYIN_ECLIPSE_CENTRAL) == 0) return TAIYIN_STATUS_OK;
    if ((out->kind & (TAIYIN_ECLIPSE_TOTAL | TAIYIN_ECLIPSE_ANNULAR)) == 0) return TAIYIN_STATUS_OK;
    const SplitJulianDate c1 = out->contact_jd_tt[TAIYIN_SOLAR_ECLIPSE_CONTACT_C1];
    const SplitJulianDate c4 = out->contact_jd_tt[TAIYIN_SOLAR_ECLIPSE_CONTACT_C4];
    bool has_total = false;
    bool has_annular = false;
    const double eps = 10.0 / 86400.0;
    SplitJulianDate samples[3] = {out->maximum_jd_tt, invalid_jd(), invalid_jd()};
    if (split_julian_date_is_finite(c1)) samples[1] = c1 + eps;
    if (split_julian_date_is_finite(c4)) samples[2] = c4 - eps;
    for (size_t i = 0; i < 3; ++i) {
        if (!split_julian_date_is_finite(samples[i])) continue;
        SolarBesselianElements e;
        const Status st = compute_besselian_elements_tt(
            context, samples[i], 0.0, flags, corrections, &e, diagnostic);
        if (st != TAIYIN_STATUS_OK) return st;
        GlobalGeometry g;
        const Status geo_status = eval_global_geometry(context, samples[i], e, &g, diagnostic);
        if (geo_status != TAIYIN_STATUS_OK) return geo_status;
        if (g.central && std::isfinite(g.longitude_deg) && std::isfinite(g.latitude_deg)) {
            LocalSolarEclipseCircumstances local;
            const Status local_status = compute_local_solar_circumstances_tt_with_options(
                context,
                samples[i],
                g.longitude_deg,
                g.latitude_deg,
                0.0,
                flags,
                corrections,
                &local,
                diagnostic);
            if (local_status != TAIYIN_STATUS_OK) return local_status;
            if (local.moon_angular_radius_deg >= local.sun_angular_radius_deg) has_total = true;
            else has_annular = true;
        } else if (e.l2 >= 0.0) {
            has_total = true;
        } else {
            has_annular = true;
        }
    }
    const uint32_t centrality = out->kind & (TAIYIN_ECLIPSE_CENTRAL | TAIYIN_ECLIPSE_NONCENTRAL);
    out->kind &= ~(TAIYIN_ECLIPSE_TOTAL | TAIYIN_ECLIPSE_ANNULAR | TAIYIN_ECLIPSE_HYBRID);
    if (has_total && has_annular) out->kind = centrality | TAIYIN_ECLIPSE_HYBRID;
    else if (has_total) out->kind = centrality | TAIYIN_ECLIPSE_TOTAL;
    else out->kind = centrality | TAIYIN_ECLIPSE_ANNULAR;
    return TAIYIN_STATUS_OK;
}

// The direct solver classifies the inexpensive maximum geometry before it
// computes locations and contact roots.  Reject filters that cannot possibly
// match at that point so searches do not complete discarded events.  A
// central total/annular classification may still become hybrid during the
// later refinement, so keep any requested central type as a possible match.
bool direct_kind_may_match_filter(
    uint32_t kind,
    uint32_t kind_filter,
    bool refine_central_kind
) noexcept {
    if (kind_filter == 0) return true;

    const uint32_t type_mask = TAIYIN_ECLIPSE_PARTIAL
                             | TAIYIN_ECLIPSE_TOTAL
                             | TAIYIN_ECLIPSE_ANNULAR
                             | TAIYIN_ECLIPSE_HYBRID;
    const uint32_t centrality_mask = TAIYIN_ECLIPSE_CENTRAL
                                   | TAIYIN_ECLIPSE_NONCENTRAL;
    const uint32_t requested_types = kind_filter & type_mask;
    const uint32_t requested_centrality = kind_filter & centrality_mask;
    if (requested_centrality != 0 && (kind & requested_centrality) == 0) {
        return false;
    }
    if (requested_types == 0) {
        return requested_centrality != 0;
    }
    const bool may_refine_to_hybrid = refine_central_kind
        && (kind & TAIYIN_ECLIPSE_CENTRAL) != 0
        && (kind & (TAIYIN_ECLIPSE_TOTAL | TAIYIN_ECLIPSE_ANNULAR)) != 0;
    if (may_refine_to_hybrid) {
        return (requested_types
                & (TAIYIN_ECLIPSE_TOTAL
                   | TAIYIN_ECLIPSE_ANNULAR
                   | TAIYIN_ECLIPSE_HYBRID)) != 0;
    }
    return (kind & requested_types) != 0;
}

}  // namespace

Status solve_solar_eclipse_direct_for_meeus_k(
    const NativeCalcContext* context,
    int k,
    uint64_t flags,
    uint32_t kind_filter,
    bool fill_location,
    bool refine_central_kind,
    SolarEclipseResult* out,
    EphemerisEvalDiagnostic* diagnostic
) noexcept {
    if (!context || !out) return TAIYIN_ERROR_INVALID_ARGUMENT;
    init_result(out);
    if ((flags & TAIYIN_ECLIPSE_LUNAR_LIMB_CORRECTION) != 0u
        && global_lunar_limb_model() == nullptr) {
        return TAIYIN_ERROR_UNSUPPORTED;
    }
    if (!solar_meeus_filter_passes(k)) return TAIYIN_STATUS_OK;

    DirectSolarEvent event;
    event.center_jd_tt = solar_meeus_new_moon_jd(k);
    FastApparentOptions options;
    options.frame = FAST_APPARENT_TRUE_EQUATOR_OF_DATE;
    options.with_velocity = false;
    options.true_position = (flags & TAIYIN_ECLIPSE_TRUEPOS) != 0;
    FastApparentCorrectionConfig correction_config;
    correction_config.initial_half_days = 0.5;
    correction_config.sample_step_days = 3.0 / 24.0;
    Status st = init_fast_correction_series(
        context,
        TAIYIN_BODY_MOON,
        TAIYIN_BODY_SUN,
        options,
        correction_config,
        event.center_jd_tt,
        &event.corrections,
        diagnostic);
    if (st != TAIYIN_STATUS_OK) return st;

    SplitJulianDate greatest_jd_tt = invalid_jd();
    st = refine_direct_greatest(
        context,
        event.center_jd_tt,
        flags,
        &event.corrections,
        &greatest_jd_tt,
        diagnostic);
    if (st != TAIYIN_STATUS_OK) return st;

    SolarBesselianElements greatest;
    st = compute_besselian_elements_tt(
        context,
        greatest_jd_tt,
        (greatest_jd_tt - event.center_jd_tt) * 24.0,
        flags,
        &event.corrections,
        &greatest,
        diagnostic);
    if (st != TAIYIN_STATUS_OK) return st;

    const double penumbral_discriminant = direct_contact_scalar(
        context, &event, greatest_jd_tt, flags, false, diagnostic);
    if (!std::isfinite(penumbral_discriminant)) {
        return TAIYIN_EPHEMERIS_ERROR_EVAL_FAILED;
    }
    if (penumbral_discriminant < 0.0) return TAIYIN_STATUS_OK;

    const double central_discriminant = direct_contact_scalar(
        context, &event, greatest_jd_tt, flags, true, diagnostic);
    if (!std::isfinite(central_discriminant)) {
        return TAIYIN_EPHEMERIS_ERROR_EVAL_FAILED;
    }
    const bool central = central_discriminant >= 0.0;

    SolarConeEarthTangency core_tangency;
    if (!maximize_solar_circular_cone_earth_discriminant(
            -greatest.x,
            greatest.y,
            greatest.zeta,
            moon_radius_km(context->eclipse_moon_radius_model_id)
                / TAIYIN_WGS84_A_KM,
            greatest.l2,
            M_PI / 2.0 + greatest.d_deg * M_PI / 180.0,
            kEarthAxisRatio,
            &core_tangency)) {
        return TAIYIN_EPHEMERIS_ERROR_EVAL_FAILED;
    }
    const bool core_reaches_earth = core_tangency.valid
        && core_tangency.normalized_discriminant >= 0.0;
    const uint32_t centrality = central
        ? TAIYIN_ECLIPSE_CENTRAL
        : TAIYIN_ECLIPSE_NONCENTRAL;
    if (core_reaches_earth) {
        out->kind = centrality
            | (greatest.l2 >= 0.0 ? TAIYIN_ECLIPSE_TOTAL : TAIYIN_ECLIPSE_ANNULAR);
    } else {
        out->kind = centrality | TAIYIN_ECLIPSE_PARTIAL;
    }

    out->maximum_jd_tt = greatest_jd_tt;
    out->axis_distance_km = greatest.gamma * TAIYIN_WGS84_A_KM;
    out->penumbra_radius_km = greatest.l1 * TAIYIN_WGS84_A_KM;
    out->core_radius_km = greatest.l2 * TAIYIN_WGS84_A_KM;
    if ((flags & TAIYIN_ECLIPSE_LUNAR_LIMB_CORRECTION) != 0u) {
        // The profiled contact scalar is the negated physical surface margin.
        out->penumbral_margin_km = -penumbral_discriminant;
    } else {
        // For a generator line, normalized_discriminant = 1 - min(Q), where
        // Q is the oblate-Earth quadratic form. Convert that same exact
        // cone/ellipsoid contact scalar to a signed radial clearance so the
        // public margin cannot contradict the event classification.
        const double closest_ellipsoid_radius = std::sqrt(
            std::max(0.0, 1.0 - penumbral_discriminant));
        out->penumbral_margin_km =
            (closest_ellipsoid_radius - 1.0) * TAIYIN_WGS84_A_KM;
    }
    const double scaled_distance = std::sqrt(
        greatest.x * greatest.x
        + (greatest.y / kEarthAxisRatio) * (greatest.y / kEarthAxisRatio));
    out->central_margin_km =
        (scaled_distance - 1.0) * TAIYIN_WGS84_A_KM;
    out->contact_jd_tt[TAIYIN_SOLAR_ECLIPSE_CONTACT_GREATEST] = greatest_jd_tt;

    if (!direct_kind_may_match_filter(out->kind, kind_filter, refine_central_kind)) {
        return TAIYIN_STATUS_OK;
    }

    if (fill_location && central) {
        GlobalGeometry global;
        st = eval_global_geometry(
            context, greatest_jd_tt, greatest, &global, diagnostic);
        if (st != TAIYIN_STATUS_OK) return st;
        if (global.central) {
            out->maximum_longitude_deg = global.longitude_deg;
            out->maximum_latitude_deg = global.latitude_deg;
        }
    }

    const bool include_global_contacts =
        (flags & TAIYIN_ECLIPSE_INCLUDE_CONTACTS) != 0u;
    if (include_global_contacts) {
        st = find_direct_contact(
            context, &event, greatest_jd_tt, flags, false, -1,
            &out->contact_jd_tt[TAIYIN_SOLAR_ECLIPSE_CONTACT_P1], diagnostic);
        if (st != TAIYIN_STATUS_OK) return st;
        st = find_direct_contact(
            context, &event, greatest_jd_tt, flags, false, 1,
            &out->contact_jd_tt[TAIYIN_SOLAR_ECLIPSE_CONTACT_P4], diagnostic);
        if (st != TAIYIN_STATUS_OK) return st;
    }

    if (central && (include_global_contacts || refine_central_kind)) {
        st = find_direct_contact(
            context, &event, greatest_jd_tt, flags, true, -1,
            &out->contact_jd_tt[TAIYIN_SOLAR_ECLIPSE_CONTACT_C1], diagnostic);
        if (st != TAIYIN_STATUS_OK) return st;
        st = find_direct_contact(
            context, &event, greatest_jd_tt, flags, true, 1,
            &out->contact_jd_tt[TAIYIN_SOLAR_ECLIPSE_CONTACT_C4], diagnostic);
        if (st != TAIYIN_STATUS_OK) return st;
    }

    if (refine_central_kind && central && core_reaches_earth) {
        st = refine_hybrid_kind(
            context, flags, &event.corrections, out, diagnostic);
        if (st != TAIYIN_STATUS_OK) return st;
    }
    if (!include_global_contacts) {
        out->contact_jd_tt[TAIYIN_SOLAR_ECLIPSE_CONTACT_P1] = invalid_jd();
        out->contact_jd_tt[TAIYIN_SOLAR_ECLIPSE_CONTACT_C1] = invalid_jd();
        out->contact_jd_tt[TAIYIN_SOLAR_ECLIPSE_CONTACT_C4] = invalid_jd();
        out->contact_jd_tt[TAIYIN_SOLAR_ECLIPSE_CONTACT_P4] = invalid_jd();
    }
    return TAIYIN_STATUS_OK;
}

Status solve_solar_eclipse_besselian_lite_for_meeus_k(
    const NativeCalcContext* context,
    int k,
    uint64_t flags,
    bool fill_location,
    SolarEclipseResult* out,
    bool* out_uncertain,
    EphemerisEvalDiagnostic* diagnostic
) noexcept {
    if (!context || !out) return TAIYIN_ERROR_INVALID_ARGUMENT;
    init_result(out);
    if (out_uncertain) *out_uncertain = true;
    if (!solar_meeus_filter_passes(k)) {
        if (out_uncertain) *out_uncertain = false;
        return TAIYIN_STATUS_OK;
    }

    const SplitJulianDate seed_jd_tt = solar_meeus_new_moon_jd(k);
    SplitJulianDate max_jd = invalid_jd();
    SolarBesselianElements max_e;
    Status st = refine_lite_maximum(
        context, seed_jd_tt, flags, &max_jd, &max_e, diagnostic);
    if (st != TAIYIN_STATUS_OK) return st;

    GlobalGeometry geo;
    st = eval_global_geometry(context, max_jd, max_e, &geo, diagnostic);
    if (st != TAIYIN_STATUS_OK) return st;

    out->kind = classify_global(max_e, geo);
    if (out_uncertain) {
        *out_uncertain = lite_result_uncertain(max_e, geo);
    }
    if (out->kind == TAIYIN_ECLIPSE_NONE) {
        return TAIYIN_STATUS_OK;
    }

    out->maximum_jd_tt = max_jd;
    out->axis_distance_km = max_e.gamma * TAIYIN_WGS84_A_KM;
    out->penumbra_radius_km = max_e.l1 * TAIYIN_WGS84_A_KM;
    out->core_radius_km = max_e.l2 * TAIYIN_WGS84_A_KM;
    out->penumbral_margin_km = geo.penumbral_margin * TAIYIN_WGS84_A_KM;
    out->central_margin_km = (geo.scaled_distance - 1.0) * TAIYIN_WGS84_A_KM;
    out->contact_jd_tt[TAIYIN_SOLAR_ECLIPSE_CONTACT_GREATEST] = max_jd;
    if (fill_location && geo.central) {
        out->maximum_longitude_deg = geo.longitude_deg;
        out->maximum_latitude_deg = geo.latitude_deg;
    }
    return TAIYIN_STATUS_OK;
}

Status solve_solar_eclipse_besselian_search_for_meeus_k(
    const NativeCalcContext* context,
    int k,
    uint64_t flags,
    bool fill_location,
    bool refine_central_kind,
    SolarEclipseResult* out,
    EphemerisEvalDiagnostic* diagnostic
) noexcept {
    if (!context || !out) return TAIYIN_ERROR_INVALID_ARGUMENT;

    bool uncertain = true;
    const uint64_t lite_flags = flags & ~TAIYIN_ECLIPSE_INCLUDE_CONTACTS;
    Status st = solve_solar_eclipse_besselian_lite_for_meeus_k(
        context, k, lite_flags, fill_location, out, &uncertain, diagnostic);
    if (st != TAIYIN_STATUS_OK) return st;
    if (!uncertain && out->kind == TAIYIN_ECLIPSE_NONE) {
        return TAIYIN_STATUS_OK;
    }

    return solve_solar_eclipse_besselian_for_meeus_k(
        context, k, flags, fill_location, refine_central_kind, out, diagnostic);
}

Status solve_solar_eclipse_besselian_for_meeus_k(
    const NativeCalcContext* context,
    int k,
    uint64_t flags,
    bool fill_location,
    bool refine_central_kind,
    SolarEclipseResult* out,
    EphemerisEvalDiagnostic* diagnostic
) noexcept {
    if (!context || !out) return TAIYIN_ERROR_INVALID_ARGUMENT;
    init_result(out);
    if (!solar_meeus_filter_passes(k)) return TAIYIN_STATUS_OK;

    BesselianEvent event;
    Status st = build_event(context, k, flags, &event, diagnostic);
    if (st != TAIYIN_STATUS_OK) return st;

    const double max_t_poly = minimize_rho2(event.poly);
    const SplitJulianDate max_seed_jd = event.seed_jd_tt + max_t_poly / 24.0;
    SplitJulianDate max_jd = invalid_jd();
    SolarBesselianElements max_e;
    st = refine_exact_maximum(
        context,
        event.seed_jd_tt,
        max_seed_jd,
        flags,
        &event.corrections,
        &event.scratch,
        &max_jd,
        &max_e,
        diagnostic);
    if (st != TAIYIN_STATUS_OK) return st;

    GlobalGeometry geo;
    st = eval_global_geometry(context, max_jd, max_e, &geo, diagnostic);
    if (st != TAIYIN_STATUS_OK) return st;

    out->kind = classify_global(max_e, geo);
    if (out->kind == TAIYIN_ECLIPSE_NONE) return TAIYIN_STATUS_OK;
    out->maximum_jd_tt = max_jd;
    out->axis_distance_km = max_e.gamma * TAIYIN_WGS84_A_KM;
    out->penumbra_radius_km = max_e.l1 * TAIYIN_WGS84_A_KM;
    out->core_radius_km = max_e.l2 * TAIYIN_WGS84_A_KM;
    out->penumbral_margin_km = geo.penumbral_margin * TAIYIN_WGS84_A_KM;
    out->central_margin_km = (geo.scaled_distance - 1.0) * TAIYIN_WGS84_A_KM;
    if (fill_location && geo.central) {
        out->maximum_longitude_deg = geo.longitude_deg;
        out->maximum_latitude_deg = geo.latitude_deg;
    }

    st = fill_contacts(context, event, (max_jd - event.seed_jd_tt) * 24.0, max_jd, flags, out, diagnostic);
    if (st != TAIYIN_STATUS_OK) return st;
    if (refine_central_kind) {
        st = refine_hybrid_kind(
            context, flags, &event.corrections, out, diagnostic);
        if (st != TAIYIN_STATUS_OK) return st;
    }
    return TAIYIN_STATUS_OK;
}


}  // namespace runtime
}  // namespace taiyin
