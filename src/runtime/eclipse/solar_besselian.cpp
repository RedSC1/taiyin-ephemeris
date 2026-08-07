#include "taiyin/runtime/eclipse_search.h"

#include "runtime/eclipse/eclipse_time.h"
#include "runtime/apparent/fast_apparent.h"

#include "taiyin/body_id.h"
#include "taiyin/dispatch.h"
#include "taiyin/earth_rotation.h"
#include "taiyin/geodetic_constants.h"
#include "taiyin/runtime/native_position.h"
#include "taiyin/time.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

namespace taiyin {
namespace runtime {

namespace {

// ---------------------------------------------------------------------------
// Physical constants
// ---------------------------------------------------------------------------
constexpr double kAuKm = 149597870.7;
constexpr double kEarthEquatorialRadiusKm = TAIYIN_WGS84_A_KM;
constexpr double kSunRadiusKm = 695700.0;
constexpr double kMoonAlmanacRadiusRatio = 0.2725076;

double moon_radius_km(uint8_t moon_radius_model) {
    dispatch::EclipseMoonRadiusModelEntry entry;
    if (dispatch::find_eclipse_moon_radius_model(static_cast<int>(moon_radius_model), &entry)) {
        return entry.radius_km;
    }
    // Fallback: almanac radius
    return kMoonAlmanacRadiusRatio * kEarthEquatorialRadiusKm;
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

bool normalize3(double v[3]) noexcept {
    const double n = norm3(v);
    if (!(n > 0.0) || !std::isfinite(n)) {
        return false;
    }
    v[0] /= n;
    v[1] /= n;
    v[2] /= n;
    return true;
}

void cross3(const double a[3], const double b[3], double out[3]) noexcept {
    out[0] = a[1] * b[2] - a[2] * b[1];
    out[1] = a[2] * b[0] - a[0] * b[2];
    out[2] = a[0] * b[1] - a[1] * b[0];
}

bool plane_basis(const double axis_unit[3], double x_hat[3], double y_hat[3]) noexcept {
    const double z_axis[3] = {0.0, 0.0, 1.0};
    const double x_axis[3] = {1.0, 0.0, 0.0};
    cross3(z_axis, axis_unit, x_hat);
    if (norm3(x_hat) < 1e-12) {
        cross3(x_axis, axis_unit, x_hat);
    }
    if (!normalize3(x_hat)) {
        return false;
    }
    cross3(axis_unit, x_hat, y_hat);
    return normalize3(y_hat);
}

bool context_gast_rad(
    const NativeCalcContext* context,
    SplitJulianDate jd_ut,
    SplitJulianDate jd_tt,
    double* out
) noexcept;

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
        Status correction_status =
            get_fast_correction(
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

Status compute_solar_besselian_elements_tt_impl(
    const NativeCalcContext* context,
    SplitJulianDate jd_tt,
    double t_hours,
    uint64_t flags,
    FastApparentCorrectionSeries* corrections,
    SolarBesselianElements* out,
    EphemerisEvalDiagnostic* diagnostic
) noexcept {
    if (context == nullptr || out == nullptr || !split_julian_date_is_finite(jd_tt) || !std::isfinite(t_hours)) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    std::memset(out, 0, sizeof(*out));

    double moon_km[3] = {};
    double sun_km[3] = {};
    Status st = eval_solar_equatorial_vectors_km(
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
    const double ra_deg = normalize_degrees(std::atan2(axis_unit[1], axis_unit[0]) * 180.0 / M_PI);
    const double dec_deg = std::asin(clamp_unit(axis_unit[2])) * 180.0 / M_PI;
    SplitJulianDate jd_ut;
    const Status time_status = eclipse_tt_to_ut(*context, jd_tt, &jd_ut, nullptr, diagnostic);
    if (time_status != TAIYIN_STATUS_OK) return time_status;
    double gast_rad = 0.0;
    if (!context_gast_rad(context, jd_ut, jd_tt, &gast_rad)) {
        return TAIYIN_ERROR_UNSUPPORTED;
    }
    const double gast_deg = normalize_degrees(gast_rad * 180.0 / M_PI);

    out->t_hours = t_hours;
    out->x = x;
    out->y = y;
    out->zeta = zeta_km / TAIYIN_WGS84_A_KM;
    out->d_deg = dec_deg;
    out->mu_deg = normalize_degrees(gast_deg - ra_deg);
    out->l1 = l1;
    out->l2 = l2;
    out->f1_deg = std::atan(tan_f1) * 180.0 / M_PI;
    out->f2_deg = std::atan(tan_f2) * 180.0 / M_PI;
    out->tan_f1 = tan_f1;
    out->tan_f2 = tan_f2;
    out->gamma = std::hypot(x, y);
    return TAIYIN_STATUS_OK;
}

double polynomial_value(const double coeffs[TAIYIN_SOLAR_BESSELIAN_COEFF_COUNT], int degree, double t) noexcept {
    double value = 0.0;
    for (int i = degree; i >= 0; --i) {
        value = value * t + coeffs[i];
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
        if (std::fabs(matrix[pivot * n + column]) < 1e-14) {
            return false;
        }
        if (pivot != column) {
            for (int j = column; j < n; ++j) {
                const double tmp = matrix[column * n + j];
                matrix[column * n + j] = matrix[pivot * n + j];
                matrix[pivot * n + j] = tmp;
            }
            const double tmp = rhs[column];
            rhs[column] = rhs[pivot];
            rhs[pivot] = tmp;
        }

        const double divisor = matrix[column * n + column];
        for (int j = column; j < n; ++j) {
            matrix[column * n + j] /= divisor;
        }
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
    if (xs.size() != ys.size() || degree < 0 || degree > static_cast<int>(TAIYIN_SOLAR_BESSELIAN_MAX_DEGREE)
        || xs.size() < static_cast<size_t>(degree + 1)) {
        return false;
    }
    const int n = degree + 1;
    double powers[2 * TAIYIN_SOLAR_BESSELIAN_COEFF_COUNT] = {};
    for (int power = 0; power <= 2 * degree; ++power) {
        double sum = 0.0;
        for (size_t i = 0; i < xs.size(); ++i) {
            sum += std::pow(xs[i], power);
        }
        powers[power] = sum;
    }
    double matrix[TAIYIN_SOLAR_BESSELIAN_COEFF_COUNT * TAIYIN_SOLAR_BESSELIAN_COEFF_COUNT] = {};
    double rhs[TAIYIN_SOLAR_BESSELIAN_COEFF_COUNT] = {};
    for (int row = 0; row < n; ++row) {
        for (int col = 0; col < n; ++col) {
            matrix[row * n + col] = powers[row + col];
        }
        for (size_t i = 0; i < xs.size(); ++i) {
            rhs[row] += ys[i] * std::pow(xs[i], row);
        }
    }
    if (!solve_linear_system(matrix, rhs, n)) {
        return false;
    }
    for (size_t i = 0; i < TAIYIN_SOLAR_BESSELIAN_COEFF_COUNT; ++i) {
        out[i] = i < static_cast<size_t>(n) ? rhs[i] : 0.0;
    }
    return true;
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

}  // namespace

// ===========================================================================
// Public API
// ===========================================================================
Status eval_solar_eclipse_equatorial_vectors_km(
    const NativeCalcContext* context,
    SplitJulianDate jd_tt,
    uint64_t flags,
    FastApparentCorrectionSeries* corrections,
    double moon_km[3],
    double sun_km[3],
    EphemerisEvalDiagnostic* diagnostic
) noexcept {
    return eval_solar_equatorial_vectors_km(
        context, jd_tt, flags, corrections, moon_km, sun_km, diagnostic);
}

Status compute_solar_besselian_elements_tt_with_corrections(
    const NativeCalcContext* context,
    SplitJulianDate jd_tt,
    double t_hours,
    uint64_t flags,
    FastApparentCorrectionSeries* corrections,
    SolarBesselianElements* out,
    EphemerisEvalDiagnostic* diagnostic
) noexcept {
    return compute_solar_besselian_elements_tt_impl(
        context, jd_tt, t_hours, flags, corrections, out, diagnostic);
}

Status compute_solar_besselian_elements_tt(
    const NativeCalcContext* context,
    SplitJulianDate jd_tt,
    double t_hours,
    SolarBesselianElements* out,
    EphemerisEvalDiagnostic* diagnostic
) noexcept {
    return compute_solar_besselian_elements_tt_with_corrections(
        context, jd_tt, t_hours, 0, nullptr, out, diagnostic);
}

Status evaluate_solar_besselian_polynomial(
    const SolarBesselianPolynomial* polynomial,
    double t_hours,
    SolarBesselianElements* out
) noexcept {
    if (polynomial == nullptr || out == nullptr || !std::isfinite(t_hours)
        || polynomial->degree < 0
        || polynomial->degree > static_cast<int>(TAIYIN_SOLAR_BESSELIAN_MAX_DEGREE)) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    std::memset(out, 0, sizeof(*out));
    out->t_hours = t_hours;
    out->x = polynomial_value(polynomial->x, polynomial->degree, t_hours);
    out->y = polynomial_value(polynomial->y, polynomial->degree, t_hours);
    out->zeta = polynomial_value(polynomial->zeta, polynomial->degree, t_hours);
    out->d_deg = polynomial_value(polynomial->d_deg, polynomial->degree, t_hours);
    out->mu_deg = normalize_degrees(polynomial_value(polynomial->mu_deg, polynomial->degree, t_hours));
    out->l1 = polynomial_value(polynomial->l1, polynomial->degree, t_hours);
    out->l2 = polynomial_value(polynomial->l2, polynomial->degree, t_hours);
    out->f1_deg = polynomial->f1_deg;
    out->f2_deg = polynomial->f2_deg;
    out->tan_f1 = polynomial->tan_f1;
    out->tan_f2 = polynomial->tan_f2;
    out->gamma = std::hypot(out->x, out->y);
    return TAIYIN_STATUS_OK;
}

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
) noexcept {
    if (context == nullptr || out == nullptr
        || !split_julian_date_is_finite(center_jd_tt)
        || !(span_hours > 0.0)
        || !(sample_step_hours > 0.0)
        || degree < 1
        || degree > static_cast<int>(TAIYIN_SOLAR_BESSELIAN_MAX_DEGREE)) {
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
    if (offsets.size() < static_cast<size_t>(degree + 1)) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }

    std::vector<SolarBesselianElements> samples(offsets.size());
    for (size_t i = 0; i < offsets.size(); ++i) {
        const SplitJulianDate jd = center_jd_tt + offsets[i] / 24.0;
        const Status st = compute_solar_besselian_elements_tt_impl(
            context, jd, offsets[i], flags, corrections, &samples[i], diagnostic);
        if (st != TAIYIN_STATUS_OK) return st;
        if (std::fabs(offsets[i]) < 1e-12) {
            out->center = samples[i];
        }
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

    SolarBesselianElements residual;
    std::memset(&out->max_residual, 0, sizeof(out->max_residual));
    for (size_t i = 0; i < samples.size(); ++i) {
        const Status st = evaluate_solar_besselian_polynomial(out, samples[i].t_hours, &residual);
        if (st != TAIYIN_STATUS_OK) return st;
        out->max_residual.x = std::max(out->max_residual.x, std::fabs(residual.x - samples[i].x));
        out->max_residual.y = std::max(out->max_residual.y, std::fabs(residual.y - samples[i].y));
        out->max_residual.zeta = std::max(out->max_residual.zeta, std::fabs(residual.zeta - samples[i].zeta));
        out->max_residual.d_deg = std::max(out->max_residual.d_deg, std::fabs(residual.d_deg - samples[i].d_deg));
        out->max_residual.mu_deg = std::max(out->max_residual.mu_deg,
            std::fabs(signed_degree_delta(residual.mu_deg, mu_values[i])));
        out->max_residual.l1 = std::max(out->max_residual.l1, std::fabs(residual.l1 - samples[i].l1));
        out->max_residual.l2 = std::max(out->max_residual.l2, std::fabs(residual.l2 - samples[i].l2));
    }
    return TAIYIN_STATUS_OK;
}

Status compute_solar_besselian_polynomial_tt(
    const NativeCalcContext* context,
    SplitJulianDate center_jd_tt,
    double span_hours,
    double sample_step_hours,
    int degree,
    SolarBesselianPolynomial* out,
    EphemerisEvalDiagnostic* diagnostic
) noexcept {
    return compute_solar_besselian_polynomial_tt_with_corrections(
        context, center_jd_tt, span_hours, sample_step_hours, degree, 0, nullptr, out, diagnostic);
}

}  // namespace runtime
}  // namespace taiyin
