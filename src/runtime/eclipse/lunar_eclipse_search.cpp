#include "taiyin/runtime/eclipse_search.h"

#include "runtime/eclipse/eclipse_time.h"
#include "runtime/eclipse/lunar_shadow_geometry.h"

#include "taiyin/angle.h"
#include "taiyin/time.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <vector>

namespace taiyin {
namespace runtime {

namespace {

SplitJulianDate invalid_jd() noexcept {
    return SplitJulianDate(0, std::numeric_limits<double>::quiet_NaN());
}

// Meeus ch.52 node-distance threshold for eclipse possibility.
constexpr double kNodeLimitDeg = 23.0;

// ---------------------------------------------------------------------------
// Meeus ch.52 formulas (from Astronomical Algorithms, 2nd ed., ch.52)
//
// The coefficients and structure follow the book directly.  Variable names
// match the book: M (solar mean anomaly), M' (lunar mean anomaly),
// F (argument of latitude), Omega (ascending node longitude).
// ---------------------------------------------------------------------------

double degnorm(double x) {
    x = std::fmod(x, 360.0);
    if (x < 0.0) x += 360.0;
    return x;
}

// F: argument of latitude for lunation k (Meeus ch.47, used in ch.52).
double meeus_f(double k) {
    const double k_half = k + 0.5;
    const double T  = k_half / 1236.85;
    const double T2 = T * T;
    const double T3 = T2 * T;
    const double T4 = T3 * T;
    return degnorm(160.7108 + 390.67050274 * k_half
                   - 0.0016341 * T2
                   - 0.00000227 * T3
                   + 0.000000011 * T4);
}

// F normalized to [0, 180) for node-distance check.
double meeus_f_normalized(double k) {
    double F = meeus_f(k);
    if (F > 180.0) F -= 180.0;
    return F;
}

// Approximate JD of maximum eclipse for lunation k (Meeus ch.52).
// Uses lunar constants per Meeus note on formula (52.1):
// -0.4065 for sin M' and +0.1727 for E sin M.
SplitJulianDate meeus_max_jd(double k) {
    const double k_half = k + 0.5;
    const double T  = k_half / 1236.85;
    const double T2 = T * T;
    const double T3 = T2 * T;
    const double T4 = T3 * T;

    const double M = degnorm(2.5534 + 29.10535669 * k_half
                             - 0.0000218 * T2 - 0.00000011 * T3);
    const double M_prime = degnorm(201.5643 + 385.81693528 * k_half
                                   + 0.1017438 * T2
                                   + 0.00001239 * T3
                                   + 0.000000058 * T4);
    const double Omega = degnorm(124.7746 - 1.56375580 * k_half
                                 + 0.0020691 * T2 + 0.00000215 * T3);
    const double E  = 1.0 - 0.002516 * T - 0.0000074 * T2;
    const double A1 = degnorm(299.77 + 0.107408 * k_half - 0.009173 * T2);

    // Mean syzygy time (Meeus 47.1)
    SplitJulianDate tjd(
        2451550,
        0.09765
            + 29.530588853 * k_half
            + 0.0001337 * T2
            - 0.000000150 * T3
            + 0.00000000073 * T4);

    // F1 = F - 0.02665 * sin(Omega), in degrees (Meeus ch.52).
    const double F1_deg = meeus_f_normalized(k)
                          - 0.02665 * std::sin(Omega * TAIYIN_PI / 180.0);

    // Convert to radians for trig calls.
    const double M_rad       = M * TAIYIN_PI / 180.0;
    const double M_prime_rad = M_prime * TAIYIN_PI / 180.0;
    const double F1_rad      = F1_deg * TAIYIN_PI / 180.0;
    const double A1_rad      = A1 * TAIYIN_PI / 180.0;
    const double Omega_rad   = Omega * TAIYIN_PI / 180.0;

    // Max-eclipse time correction (Meeus 52.1), lunar constants.
    tjd += (-0.4065 * std::sin(M_prime_rad)
            + 0.1727 * E * std::sin(M_rad)
            + 0.0161 * std::sin(2.0 * M_prime_rad)
            - 0.0097 * std::sin(2.0 * F1_rad)
            + 0.0073 * E * std::sin(M_prime_rad - M_rad)
            - 0.0050 * E * std::sin(M_prime_rad + M_rad)
            - 0.0023 * std::sin(M_prime_rad - 2.0 * F1_rad)
            + 0.0021 * E * std::sin(2.0 * M_rad)
            + 0.0012 * std::sin(M_prime_rad + 2.0 * F1_rad)
            + 0.0006 * E * std::sin(2.0 * M_prime_rad + M_rad)
            - 0.0004 * std::sin(3.0 * M_prime_rad)
            - 0.0003 * E * std::sin(M_rad + 2.0 * F1_rad)
            + 0.0003 * std::sin(A1_rad)
            - 0.0002 * E * std::sin(M_rad - 2.0 * F1_rad)
            - 0.0002 * E * std::sin(2.0 * M_prime_rad - M_rad)
            - 0.0002 * std::sin(Omega_rad));
    return tjd;
}

// Lunar-eclipse lunation index for a given JD.  The Meeus phase formulas use
// k + 0.5 for full moons, so the integer k is the floor of the continuous
// lunation count, not the nearest integer lunation.
int meeus_k_for_jd(SplitJulianDate jd_tt) {
    const double k_continuous = (jd_tt - SplitJulianDate(2451545, 0.0))
        / 365.2425 * 12.3685;
    return static_cast<int>(std::floor(k_continuous));
}

bool meeus_filter_passes(double k) {
    const double F = meeus_f_normalized(k);
    return F <= kNodeLimitDeg || F >= (180.0 - kNodeLimitDeg);
}

bool include_penumbral(uint64_t flags) {
    return (flags & TAIYIN_ECLIPSE_EXCLUDE_PENUMBRAL) == 0;
}

bool include_contacts(uint64_t flags) {
    return (flags & TAIYIN_ECLIPSE_INCLUDE_CONTACTS) != 0;
}

// ---------------------------------------------------------------------------
// Classify eclipse from geometry
// ---------------------------------------------------------------------------
uint32_t classify_eclipse(const LunarShadowGeometry& geo, bool include_penumbral) {
    const double rmin = geo.axis_distance_km;
    const double mr_toward = geo.moon_radius_toward_shadow_km;
    const double mr_away = geo.moon_radius_away_from_shadow_km;
    const double er = geo.umbra_radius_km;
    const double Er = geo.penumbra_radius_km;

    if (er > 0.0 && rmin <= er - mr_away) {
        return TAIYIN_ECLIPSE_TOTAL;
    }
    if (er > 0.0 && rmin <= er + mr_toward) {
        return TAIYIN_ECLIPSE_PARTIAL;
    }
    if (include_penumbral && rmin <= Er + mr_toward) {
        return TAIYIN_ECLIPSE_PENUMBRAL;
    }
    return TAIYIN_ECLIPSE_NONE;
}

// ---------------------------------------------------------------------------
// Fill LunarEclipseResult from geometry at maximum eclipse
// ---------------------------------------------------------------------------
void fill_result(
    const LunarShadowGeometry& geo,
    SplitJulianDate jd_max_tt,
    LunarEclipseResult* out
) noexcept {
    out->maximum_jd_tt = jd_max_tt;
    out->axis_distance_rad = std::atan2(geo.axis_distance_km, geo.axial_distance_km);
    out->umbra_radius_rad = std::atan2(geo.umbra_radius_km, geo.moon_distance_km);
    out->penumbra_radius_rad = std::atan2(geo.penumbra_radius_km, geo.moon_distance_km);
    out->moon_radius_rad = std::atan2(geo.moon_radius_km, geo.moon_distance_km);

    const double mr = geo.moon_radius_km;
    const double rho = geo.axis_distance_km;
    out->umbral_magnitude =
        (geo.umbra_radius_km + mr - rho) / (2.0 * mr);
    out->penumbral_magnitude =
        (geo.penumbra_radius_km + mr - rho) / (2.0 * mr);
}

// Initialize contact times to NAN.
void init_contacts(LunarEclipseResult* out) noexcept {
    for (size_t i = 0; i < TAIYIN_LUNAR_ECLIPSE_CONTACT_COUNT; ++i) {
        out->contact_jd_tt[i] = invalid_jd();
    }
}

void init_contacts(LunarEclipseResultUt* out) noexcept {
    for (size_t i = 0; i < TAIYIN_LUNAR_ECLIPSE_CONTACT_COUNT; ++i) {
        out->contact_jd_ut[i] = invalid_jd();
    }
}

Status fill_ut_result(
    const NativeCalcContext& context,
    const LunarEclipseResult& src,
    LunarEclipseResultUt* out,
    EphemerisEvalDiagnostic* diagnostic
) noexcept {
    out->kind = src.kind;
    if (split_julian_date_is_finite(src.maximum_jd_tt)) {
        const Status st = eclipse_tt_to_ut(context, src.maximum_jd_tt, &out->maximum_jd_ut, &out->delta_t_seconds, diagnostic);
        if (st != TAIYIN_STATUS_OK) return st;
    } else {
        out->maximum_jd_ut = invalid_jd();
        out->delta_t_seconds = std::nan("");
    }
    out->umbral_magnitude = src.umbral_magnitude;
    out->penumbral_magnitude = src.penumbral_magnitude;
    out->axis_distance_rad = src.axis_distance_rad;
    out->umbra_radius_rad = src.umbra_radius_rad;
    out->penumbra_radius_rad = src.penumbra_radius_rad;
    out->moon_radius_rad = src.moon_radius_rad;
    for (size_t i = 0; i < TAIYIN_LUNAR_ECLIPSE_CONTACT_COUNT; ++i) {
        if (split_julian_date_is_finite(src.contact_jd_tt[i])) {
            const Status st = eclipse_tt_to_ut(context, src.contact_jd_tt[i], &out->contact_jd_ut[i], nullptr, diagnostic);
            if (st != TAIYIN_STATUS_OK) return st;
        } else {
            out->contact_jd_ut[i] = invalid_jd();
        }
    }
    return TAIYIN_STATUS_OK;
}

enum LunarContactBoundary {
    LUNAR_CONTACT_PENUMBRAL_OUTER,
    LUNAR_CONTACT_UMBRAL_OUTER,
    LUNAR_CONTACT_UMBRAL_INNER,
};

double contact_boundary_radius_km(
    const LunarShadowGeometry& geometry,
    LunarContactBoundary boundary
) noexcept {
    switch (boundary) {
    case LUNAR_CONTACT_PENUMBRAL_OUTER:
        return geometry.penumbra_radius_km
            + geometry.moon_radius_toward_shadow_km;
    case LUNAR_CONTACT_UMBRAL_OUTER:
        return geometry.umbra_radius_km
            + geometry.moon_radius_toward_shadow_km;
    case LUNAR_CONTACT_UMBRAL_INNER:
        return geometry.umbra_radius_km
            - geometry.moon_radius_away_from_shadow_km;
    }
    return std::nan("");
}

double transverse_distance_squared(const LunarShadowGeometry& geometry) noexcept {
    return vector3_dot(geometry.transverse_offset_km, geometry.transverse_offset_km);
}

Status refine_lunar_greatest(
    const NativeCalcContext* context,
    SplitJulianDate seed_jd_tt,
    uint64_t flags,
    SplitJulianDate* out_jd_tt,
    LunarShadowGeometry* out_geometry,
    EphemerisEvalDiagnostic* diagnostic
) noexcept {
    if (!context || !out_jd_tt || !out_geometry) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }

    constexpr double kStepDays = 60.0 / 86400.0;
    constexpr double kMaxStepDays = 0.25;
    SplitJulianDate center = seed_jd_tt;
    for (int iteration = 0; iteration < 4; ++iteration) {
        LunarShadowGeometry minus;
        LunarShadowGeometry current;
        LunarShadowGeometry plus;
        Status status = evaluate_lunar_shadow_geometry(
            context, center - kStepDays, flags, &minus, diagnostic);
        if (status != TAIYIN_STATUS_OK) return status;
        status = evaluate_lunar_shadow_geometry(
            context, center, flags, &current, diagnostic);
        if (status != TAIYIN_STATUS_OK) return status;
        status = evaluate_lunar_shadow_geometry(
            context, center + kStepDays, flags, &plus, diagnostic);
        if (status != TAIYIN_STATUS_OK) return status;

        const double fm = transverse_distance_squared(minus);
        const double f0 = transverse_distance_squared(current);
        const double fp = transverse_distance_squared(plus);
        const double curvature = fm - 2.0 * f0 + fp;
        double correction = std::nan("");
        if (std::isfinite(curvature) && curvature > 0.0) {
            correction = 0.5 * kStepDays * (fm - fp) / curvature;
        }
        if (!std::isfinite(correction)) {
            const Vector3 velocity = vector3_scale(
                vector3_subtract(plus.transverse_offset_km, minus.transverse_offset_km),
                1.0 / (2.0 * kStepDays));
            const double speed2 = vector3_dot(velocity, velocity);
            if (!(speed2 > 0.0)) return TAIYIN_ERROR_UNSUPPORTED;
            correction = -vector3_dot(current.transverse_offset_km, velocity) / speed2;
        }
        correction = std::max(-kMaxStepDays, std::min(kMaxStepDays, correction));
        center += correction;
        if (std::fabs(correction) < 0.001 / 86400.0) {
            break;
        }
    }

    const Status status = evaluate_lunar_shadow_geometry(
        context, center, flags, out_geometry, diagnostic);
    if (status != TAIYIN_STATUS_OK) return status;
    *out_jd_tt = center;
    return TAIYIN_STATUS_OK;
}

struct LunarShadowLocalMotion {
    LunarShadowGeometry center;
    Vector3 transverse_velocity_km_per_day;
    double boundary_radius_rate_km_per_day[3];
};

Status evaluate_lunar_shadow_local_motion(
    const NativeCalcContext* context,
    SplitJulianDate jd_max,
    uint64_t flags,
    LunarShadowLocalMotion* out,
    EphemerisEvalDiagnostic* diagnostic
) noexcept {
    if (!context || !out) return TAIYIN_ERROR_INVALID_ARGUMENT;
    constexpr double kStepDays = 60.0 / 86400.0;
    LunarShadowGeometry minus;
    LunarShadowGeometry plus;
    Status status = evaluate_lunar_shadow_geometry(
        context, jd_max - kStepDays, flags, &minus, diagnostic);
    if (status != TAIYIN_STATUS_OK) return status;
    status = evaluate_lunar_shadow_geometry(
        context, jd_max, flags, &out->center, diagnostic);
    if (status != TAIYIN_STATUS_OK) return status;
    status = evaluate_lunar_shadow_geometry(
        context, jd_max + kStepDays, flags, &plus, diagnostic);
    if (status != TAIYIN_STATUS_OK) return status;

    out->transverse_velocity_km_per_day = vector3_scale(
        vector3_subtract(plus.transverse_offset_km, minus.transverse_offset_km),
        1.0 / (2.0 * kStepDays));
    for (int boundary = LUNAR_CONTACT_PENUMBRAL_OUTER;
         boundary <= LUNAR_CONTACT_UMBRAL_INNER;
         ++boundary) {
        const LunarContactBoundary kind = static_cast<LunarContactBoundary>(boundary);
        out->boundary_radius_rate_km_per_day[boundary] =
            (contact_boundary_radius_km(plus, kind)
             - contact_boundary_radius_km(minus, kind))
            / (2.0 * kStepDays);
    }
    return TAIYIN_STATUS_OK;
}

bool solve_moving_shadow_boundary(
    const LunarShadowLocalMotion& motion,
    LunarContactBoundary boundary,
    bool later_contact,
    double* out_dt_days
) noexcept {
    if (!out_dt_days) return false;
    const Vector3& q = motion.center.transverse_offset_km;
    const Vector3& velocity = motion.transverse_velocity_km_per_day;
    const double radius = contact_boundary_radius_km(motion.center, boundary);
    const double radius_rate = motion.boundary_radius_rate_km_per_day[boundary];
    if (!(radius > 0.0) || !std::isfinite(radius_rate)) return false;

    const double a = vector3_dot(velocity, velocity) - radius_rate * radius_rate;
    const double b = vector3_dot(q, velocity) - radius * radius_rate;
    const double c = vector3_dot(q, q) - radius * radius;
    if (std::fabs(a) < 1.0e-20) {
        if (std::fabs(b) < 1.0e-20) return false;
        *out_dt_days = -c / (2.0 * b);
        return std::isfinite(*out_dt_days);
    }
    const double discriminant = b * b - a * c;
    if (!(discriminant >= 0.0)) return false;
    const double sqrt_discriminant = std::sqrt(discriminant);
    const double first = (-b - sqrt_discriminant) / a;
    const double second = (-b + sqrt_discriminant) / a;
    *out_dt_days = later_contact ? std::max(first, second) : std::min(first, second);
    return std::isfinite(*out_dt_days);
}

constexpr size_t kLunarElementSampleCount = 5;
constexpr double kLunarElementHalfSpanDays = 0.25;

// Five full Sun/Moon evaluations fit cubic local elements for the transverse
// vector q and every contact-boundary radius R. Circular-limb contacts need no
// further ephemeris work; TLL1 contacts receive one direct Newton correction
// because the topographic limb radius is not globally smooth.
struct LunarShadowElements {
    std::array<double, kLunarElementSampleCount> offsets_days;
    std::array<LunarShadowGeometry, kLunarElementSampleCount> geometries;
    std::array<std::array<double, 4>, 3> q_coefficients;
    std::array<std::array<double, 4>, 3> radius_coefficients;
};

double contact_function_value(
    const LunarShadowGeometry& geometry,
    LunarContactBoundary boundary
) noexcept {
    const double radius = contact_boundary_radius_km(geometry, boundary);
    return transverse_distance_squared(geometry) - radius * radius;
}

double vector_component(const Vector3& value, size_t component) noexcept {
    return component == 0 ? value.x : (component == 1 ? value.y : value.z);
}

bool fit_cubic_element(
    const std::array<double, kLunarElementSampleCount>& values,
    std::array<double, 4>* out
) noexcept {
    if (!out) return false;
    double normal[4][5] = {};
    for (size_t sample = 0; sample < kLunarElementSampleCount; ++sample) {
        const double x = -1.0 + 2.0 * static_cast<double>(sample)
            / static_cast<double>(kLunarElementSampleCount - 1);
        double powers[7] = {1.0};
        for (size_t power = 1; power < 7; ++power) {
            powers[power] = powers[power - 1] * x;
        }
        for (size_t row = 0; row < 4; ++row) {
            for (size_t column = 0; column < 4; ++column) {
                normal[row][column] += powers[row + column];
            }
            normal[row][4] += values[sample] * powers[row];
        }
    }
    for (size_t column = 0; column < 4; ++column) {
        size_t pivot = column;
        for (size_t row = column + 1; row < 4; ++row) {
            if (std::fabs(normal[row][column]) > std::fabs(normal[pivot][column])) {
                pivot = row;
            }
        }
        if (std::fabs(normal[pivot][column]) < 1.0e-18) return false;
        if (pivot != column) {
            for (size_t item = column; item < 5; ++item) {
                std::swap(normal[pivot][item], normal[column][item]);
            }
        }
        const double divisor = normal[column][column];
        for (size_t item = column; item < 5; ++item) {
            normal[column][item] /= divisor;
        }
        for (size_t row = 0; row < 4; ++row) {
            if (row == column) continue;
            const double factor = normal[row][column];
            for (size_t item = column; item < 5; ++item) {
                normal[row][item] -= factor * normal[column][item];
            }
        }
    }
    for (size_t index = 0; index < 4; ++index) {
        (*out)[index] = normal[index][4];
    }
    return true;
}

void evaluate_cubic_element(
    const std::array<double, 4>& coefficients,
    double offset_days,
    double* value,
    double* derivative_per_day = nullptr
) noexcept {
    const double x = offset_days / kLunarElementHalfSpanDays;
    if (value) {
        *value = coefficients[0]
            + x * (coefficients[1]
                   + x * (coefficients[2] + x * coefficients[3]));
    }
    if (derivative_per_day) {
        *derivative_per_day =
            (coefficients[1] + x * (2.0 * coefficients[2]
                                    + 3.0 * x * coefficients[3]))
            / kLunarElementHalfSpanDays;
    }
}

Status fit_lunar_shadow_elements(
    const NativeCalcContext* context,
    SplitJulianDate jd_max,
    uint64_t flags,
    LunarShadowElements* out,
    EphemerisEvalDiagnostic* diagnostic
) noexcept {
    if (!context || !out) return TAIYIN_ERROR_INVALID_ARGUMENT;
    const double spacing = 2.0 * kLunarElementHalfSpanDays
        / static_cast<double>(kLunarElementSampleCount - 1);
    for (size_t index = 0; index < kLunarElementSampleCount; ++index) {
        const double offset = -kLunarElementHalfSpanDays
            + spacing * static_cast<double>(index);
        out->offsets_days[index] = offset;
        const Status status = evaluate_lunar_shadow_geometry(
            context,
            jd_max + offset,
            flags,
            &out->geometries[index],
            diagnostic);
        if (status != TAIYIN_STATUS_OK) return status;
    }
    std::array<double, kLunarElementSampleCount> values;
    for (size_t component = 0; component < 3; ++component) {
        for (size_t sample = 0; sample < kLunarElementSampleCount; ++sample) {
            values[sample] = vector_component(
                out->geometries[sample].transverse_offset_km, component);
        }
        if (!fit_cubic_element(values, &out->q_coefficients[component])) {
            return TAIYIN_ERROR_UNSUPPORTED;
        }
    }
    for (int boundary = LUNAR_CONTACT_PENUMBRAL_OUTER;
         boundary <= LUNAR_CONTACT_UMBRAL_INNER;
         ++boundary) {
        const LunarContactBoundary kind = static_cast<LunarContactBoundary>(boundary);
        for (size_t sample = 0; sample < kLunarElementSampleCount; ++sample) {
            values[sample] = contact_boundary_radius_km(out->geometries[sample], kind);
        }
        if (!fit_cubic_element(values, &out->radius_coefficients[boundary])) {
            return TAIYIN_ERROR_UNSUPPORTED;
        }
    }
    return TAIYIN_STATUS_OK;
}

double evaluate_fitted_contact_function(
    const LunarShadowElements& elements,
    LunarContactBoundary boundary,
    double offset_days,
    double* derivative_per_day = nullptr
) noexcept {
    Vector3 q{0.0, 0.0, 0.0};
    Vector3 velocity{0.0, 0.0, 0.0};
    for (size_t component = 0; component < 3; ++component) {
        double value = 0.0;
        double derivative = 0.0;
        evaluate_cubic_element(
            elements.q_coefficients[component], offset_days,
            &value, derivative_per_day ? &derivative : nullptr);
        if (component == 0) {
            q.x = value;
            velocity.x = derivative;
        } else if (component == 1) {
            q.y = value;
            velocity.y = derivative;
        } else {
            q.z = value;
            velocity.z = derivative;
        }
    }
    double radius = 0.0;
    double radius_rate = 0.0;
    evaluate_cubic_element(
        elements.radius_coefficients[boundary], offset_days,
        &radius, derivative_per_day ? &radius_rate : nullptr);
    if (derivative_per_day) {
        *derivative_per_day = 2.0 * vector3_dot(q, velocity)
            - 2.0 * radius * radius_rate;
    }
    return vector3_dot(q, q) - radius * radius;
}

bool solve_fitted_shadow_boundary(
    const LunarShadowElements& elements,
    LunarContactBoundary boundary,
    bool later_contact,
    double* out_dt_days
) noexcept {
    if (!out_dt_days) return false;
    const size_t center = kLunarElementSampleCount / 2;
    size_t bracket = kLunarElementSampleCount;
    if (later_contact) {
        for (size_t index = center; index + 1 < kLunarElementSampleCount; ++index) {
            const double left = contact_function_value(elements.geometries[index], boundary);
            const double right = contact_function_value(elements.geometries[index + 1], boundary);
            if (left <= 0.0 && right >= 0.0) {
                bracket = index;
                break;
            }
        }
    } else {
        for (size_t index = center; index > 0; --index) {
            const double left = contact_function_value(elements.geometries[index - 1], boundary);
            const double right = contact_function_value(elements.geometries[index], boundary);
            if (left >= 0.0 && right <= 0.0) {
                bracket = index - 1;
                break;
            }
        }
    }
    if (bracket >= kLunarElementSampleCount - 1) return false;

    double left = elements.offsets_days[bracket];
    double right = elements.offsets_days[bracket + 1];
    double f_left = evaluate_fitted_contact_function(elements, boundary, left);
    const double f_right = evaluate_fitted_contact_function(elements, boundary, right);
    if (!std::isfinite(f_left) || !std::isfinite(f_right) || f_left * f_right > 0.0) {
        return false;
    }
    for (int iteration = 0; iteration < 64; ++iteration) {
        const double middle = 0.5 * (left + right);
        const double f_middle = evaluate_fitted_contact_function(
            elements, boundary, middle);
        if (!std::isfinite(f_middle)) return false;
        if (f_left * f_middle <= 0.0) {
            right = middle;
        } else {
            left = middle;
            f_left = f_middle;
        }
        if (right - left < 1.0e-11) break;
    }
    *out_dt_days = 0.5 * (left + right);
    return std::isfinite(*out_dt_days);
}

Status refine_tll1_contact_once(
    const NativeCalcContext* context,
    SplitJulianDate jd_max,
    uint64_t flags,
    const LunarShadowElements& elements,
    LunarContactBoundary boundary,
    double* dt_days,
    EphemerisEvalDiagnostic* diagnostic
) noexcept {
    if (!context || !dt_days || !std::isfinite(*dt_days)) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    if ((flags & TAIYIN_ECLIPSE_LUNAR_LIMB_CORRECTION) == 0u) {
        return TAIYIN_STATUS_OK;
    }
    LunarShadowGeometry exact;
    const Status status = evaluate_lunar_shadow_geometry(
        context, jd_max + *dt_days, flags, &exact, diagnostic);
    if (status != TAIYIN_STATUS_OK) return status;
    const double exact_f = contact_function_value(exact, boundary);
    double fitted_derivative = std::nan("");
    evaluate_fitted_contact_function(
        elements, boundary, *dt_days, &fitted_derivative);
    if (!std::isfinite(exact_f) || !std::isfinite(fitted_derivative)
        || std::fabs(fitted_derivative) < 1.0e-12) {
        return TAIYIN_ERROR_UNSUPPORTED;
    }
    const double correction = -exact_f / fitted_derivative;
    constexpr double kMaximumCorrectionDays = 30.0 / 86400.0;
    if (!std::isfinite(correction) || std::fabs(correction) > kMaximumCorrectionDays) {
        return TAIYIN_ERROR_UNSUPPORTED;
    }
    *dt_days += correction;
    return TAIYIN_STATUS_OK;
}

void init_result(LunarEclipseResult* out) noexcept {
    init_contacts(out);
    out->kind = TAIYIN_ECLIPSE_NONE;
    out->maximum_jd_tt = invalid_jd();
    out->umbral_magnitude = std::nan("");
    out->penumbral_magnitude = std::nan("");
    out->axis_distance_rad = std::nan("");
    out->umbra_radius_rad = std::nan("");
    out->penumbra_radius_rad = std::nan("");
    out->moon_radius_rad = std::nan("");
}

void init_result(LunarEclipseResultUt* out) noexcept {
    init_contacts(out);
    out->kind = TAIYIN_ECLIPSE_NONE;
    out->maximum_jd_ut = invalid_jd();
    out->delta_t_seconds = std::nan("");
    out->umbral_magnitude = std::nan("");
    out->penumbral_magnitude = std::nan("");
    out->axis_distance_rad = std::nan("");
    out->umbra_radius_rad = std::nan("");
    out->penumbra_radius_rad = std::nan("");
    out->moon_radius_rad = std::nan("");
}

Status solve_lunar_eclipse_for_meeus_k(
    const NativeCalcContext* context,
    int k,
    uint64_t flags,
    LunarEclipseResult* out,
    EphemerisEvalDiagnostic* diagnostic
) noexcept {
    init_result(out);

    if (!meeus_filter_passes(static_cast<double>(k))) {
        return TAIYIN_STATUS_OK;
    }
    const SplitJulianDate jd_seed = meeus_max_jd(k);

    SplitJulianDate jd_max;
    LunarShadowGeometry geo_max;
    {
        const Status st = refine_lunar_greatest(
            context, jd_seed, flags, &jd_max, &geo_max, diagnostic);
        if (st != TAIYIN_STATUS_OK) return st;
    }

    const uint32_t kind = classify_eclipse(geo_max, include_penumbral(flags));
    out->kind = kind;
    fill_result(geo_max, jd_max, out);
    if (kind == TAIYIN_ECLIPSE_NONE) {
        return TAIYIN_STATUS_OK;
    }

    if (include_contacts(flags)) {
        LunarShadowElements elements;
        const Status elements_status = fit_lunar_shadow_elements(
            context, jd_max, flags, &elements, diagnostic);
        if (elements_status != TAIYIN_STATUS_OK) return elements_status;
        LunarShadowLocalMotion fallback_motion;
        bool fallback_attempted = false;
        Status fallback_status = TAIYIN_STATUS_OK;
        Status contact_solve_status = TAIYIN_STATUS_OK;
        const auto solve_boundary = [&](LunarContactBoundary boundary,
                                        bool later,
                                        double* out_dt) noexcept -> bool {
            bool solved = solve_fitted_shadow_boundary(
                elements, boundary, later, out_dt);
            if (!solved) {
                if (!fallback_attempted) {
                    fallback_attempted = true;
                    fallback_status = evaluate_lunar_shadow_local_motion(
                        context, jd_max, flags, &fallback_motion, diagnostic);
                }
                solved = fallback_status == TAIYIN_STATUS_OK
                    && solve_moving_shadow_boundary(
                        fallback_motion, boundary, later, out_dt);
            }
            if (!solved) return false;
            contact_solve_status = refine_tll1_contact_once(
                context, jd_max, flags, elements, boundary, out_dt, diagnostic);
            return contact_solve_status == TAIYIN_STATUS_OK;
        };
        const auto contact_failure_status = [&]() noexcept -> Status {
            if (contact_solve_status != TAIYIN_STATUS_OK) {
                return contact_solve_status;
            }
            return fallback_attempted && fallback_status != TAIYIN_STATUS_OK
                ? fallback_status
                : TAIYIN_ERROR_UNSUPPORTED;
        };

        double dt = std::nan("");
        if (!solve_boundary(LUNAR_CONTACT_PENUMBRAL_OUTER, false, &dt)) {
            return contact_failure_status();
        }
        out->contact_jd_tt[TAIYIN_LUNAR_ECLIPSE_CONTACT_P1] = jd_max + dt;
        if (!solve_boundary(LUNAR_CONTACT_PENUMBRAL_OUTER, true, &dt)) {
            return contact_failure_status();
        }
        out->contact_jd_tt[TAIYIN_LUNAR_ECLIPSE_CONTACT_P4] = jd_max + dt;
        out->contact_jd_tt[TAIYIN_LUNAR_ECLIPSE_CONTACT_GREATEST] = jd_max;

        if (kind & (TAIYIN_ECLIPSE_PARTIAL | TAIYIN_ECLIPSE_TOTAL)) {
            if (!solve_boundary(LUNAR_CONTACT_UMBRAL_OUTER, false, &dt)) {
                return contact_failure_status();
            }
            out->contact_jd_tt[TAIYIN_LUNAR_ECLIPSE_CONTACT_U1] = jd_max + dt;
            if (!solve_boundary(LUNAR_CONTACT_UMBRAL_OUTER, true, &dt)) {
                return contact_failure_status();
            }
            out->contact_jd_tt[TAIYIN_LUNAR_ECLIPSE_CONTACT_U4] = jd_max + dt;
        }

        if (kind & TAIYIN_ECLIPSE_TOTAL) {
            if (!solve_boundary(LUNAR_CONTACT_UMBRAL_INNER, false, &dt)) {
                return contact_failure_status();
            }
            out->contact_jd_tt[TAIYIN_LUNAR_ECLIPSE_CONTACT_U2] = jd_max + dt;
            if (!solve_boundary(LUNAR_CONTACT_UMBRAL_INNER, true, &dt)) {
                return contact_failure_status();
            }
            out->contact_jd_tt[TAIYIN_LUNAR_ECLIPSE_CONTACT_U3] = jd_max + dt;
        }
    }

    return TAIYIN_STATUS_OK;
}

}  // namespace

// ===========================================================================
// Public API
// ===========================================================================
Status solve_lunar_eclipse_at(
    const NativeCalcContext* context,
    SplitJulianDate jd_tt,
    uint64_t flags,
    LunarEclipseResult* out,
    EphemerisEvalDiagnostic* diagnostic
) noexcept {
    if (context == nullptr || out == nullptr || !split_julian_date_is_finite(jd_tt)
        || !valid_eclipse_flags(flags)) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }

    return solve_lunar_eclipse_for_meeus_k(
        context, meeus_k_for_jd(jd_tt), flags, out, diagnostic);
}

Status solve_lunar_eclipse_at_ut(
    const NativeCalcContext* context,
    SplitJulianDate jd_ut,
    uint64_t flags,
    LunarEclipseResultUt* out,
    EphemerisEvalDiagnostic* diagnostic
) noexcept {
    if (context == nullptr || out == nullptr || !split_julian_date_is_finite(jd_ut)
        || !valid_eclipse_flags(flags)) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }

    init_result(out);

    LunarEclipseResult tt_result;
    SplitJulianDate jd_tt;
    Status st = eclipse_ut_to_tt(*context, jd_ut, &jd_tt, nullptr, diagnostic);
    if (st != TAIYIN_STATUS_OK) return st;
    st = solve_lunar_eclipse_at(context, jd_tt, flags, &tt_result, diagnostic);
    if (st != TAIYIN_STATUS_OK) {
        return st;
    }
    return fill_ut_result(*context, tt_result, out, diagnostic);
}

Status search_next_lunar_eclipse_tt(
    const NativeCalcContext* context,
    SplitJulianDate jd_start_tt,
    uint32_t kind_filter,
    uint64_t flags,
    LunarEclipseResult* out,
    EphemerisEvalDiagnostic* diagnostic
) noexcept {
    if (context == nullptr || out == nullptr || !split_julian_date_is_finite(jd_start_tt)
        || !valid_eclipse_flags(flags)) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    if (kind_filter == 0) {
        kind_filter = TAIYIN_ECLIPSE_ALL_LUNAR;
    }

    const bool backward = (flags & TAIYIN_ECLIPSE_BACKWARD) != 0;
    int k = meeus_k_for_jd(jd_start_tt);
    if (!backward) {
        // For forward search, the Meeus max_jd for k may be slightly
        // before jd_start_tt; we want the next eclipse after jd_start_tt.
        // meeus_max_jd(k) is typically within ~1 day of the actual max,
        // so if it's before jd_start_tt, advance to k+1.
        if (meeus_max_jd(k) < jd_start_tt - 1.0) {
            k += 1;
        }
    }

    const int direction = backward ? -1 : 1;
    const int k_limit = backward ? k - 100000 : k + 100000;  // safety bound

    for (int ki = k; backward ? ki > k_limit : ki < k_limit; ki += direction) {
        // K+F filter
        if (!meeus_filter_passes(static_cast<double>(ki))) {
            continue;
        }

        // Candidate: solve at this lunation.
        const Status st = solve_lunar_eclipse_for_meeus_k(
            context, ki, flags, out, diagnostic);
        if (st != TAIYIN_STATUS_OK) {
            return st;
        }

        // Check kind filter.
        if (out->kind == TAIYIN_ECLIPSE_NONE) {
            continue;
        }
        if ((out->kind & kind_filter) == 0) {
            continue;
        }

        // Check time direction.
        if (backward && out->maximum_jd_tt >= jd_start_tt) {
            continue;
        }
        if (!backward && out->maximum_jd_tt <= jd_start_tt) {
            continue;
        }

        return TAIYIN_STATUS_OK;
    }

    return TAIYIN_EVENT_ERROR_NOT_FOUND;
}

Status search_next_lunar_eclipse_ut(
    const NativeCalcContext* context,
    SplitJulianDate jd_start_ut,
    uint32_t kind_filter,
    uint64_t flags,
    LunarEclipseResultUt* out,
    EphemerisEvalDiagnostic* diagnostic
) noexcept {
    if (context == nullptr || out == nullptr || !split_julian_date_is_finite(jd_start_ut)
        || !valid_eclipse_flags(flags)) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }

    init_result(out);

    LunarEclipseResult tt_result;
    SplitJulianDate jd_start_tt;
    Status st = eclipse_ut_to_tt(*context, jd_start_ut, &jd_start_tt, nullptr, diagnostic);
    if (st != TAIYIN_STATUS_OK) return st;
    st = search_next_lunar_eclipse_tt(
        context,
        jd_start_tt,
        kind_filter,
        flags,
        &tt_result,
        diagnostic);
    if (st != TAIYIN_STATUS_OK) {
        return st;
    }
    return fill_ut_result(*context, tt_result, out, diagnostic);
}

Status search_lunar_eclipses_tt(
    const NativeCalcContext* context,
    SplitJulianDate start_jd_tt,
    SplitJulianDate end_jd_tt,
    uint32_t kind_filter,
    uint64_t flags,
    LunarEclipseResult* out_results,
    size_t max_result_count,
    size_t* out_result_count,
    EphemerisEvalDiagnostic* diagnostic
) noexcept {
    if (context == nullptr || out_results == nullptr || out_result_count == nullptr
        || !split_julian_date_is_finite(start_jd_tt) || !split_julian_date_is_finite(end_jd_tt)
        || !valid_eclipse_flags(flags)) {
        if (out_result_count) *out_result_count = 0;
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    if (end_jd_tt <= start_jd_tt) {
        *out_result_count = 0;
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    if (kind_filter == 0) {
        kind_filter = TAIYIN_ECLIPSE_ALL_LUNAR;
    }

    *out_result_count = 0;
    size_t count = 0;

    int k = meeus_k_for_jd(start_jd_tt);
    const int k_end = meeus_k_for_jd(end_jd_tt) + 1;

    for (; k <= k_end; ++k) {
        if (!meeus_filter_passes(static_cast<double>(k))) {
            continue;
        }

        const SplitJulianDate jd_candidate = meeus_max_jd(k);
        if (jd_candidate < start_jd_tt - 1.0) {
            continue;
        }
        if (jd_candidate > end_jd_tt + 1.0) {
            break;
        }

        LunarEclipseResult result;
        const Status st = solve_lunar_eclipse_for_meeus_k(
            context, k, flags, &result, diagnostic);
        if (st != TAIYIN_STATUS_OK) {
            return st;
        }

        if (result.kind == TAIYIN_ECLIPSE_NONE) {
            continue;
        }
        if ((result.kind & kind_filter) == 0) {
            continue;
        }
        if (result.maximum_jd_tt < start_jd_tt
            || result.maximum_jd_tt > end_jd_tt) {
            continue;
        }

        if (count >= max_result_count) {
            return TAIYIN_ERROR_OUT_OF_MEMORY;
        }
        out_results[count] = result;
        ++count;
    }

    *out_result_count = count;
    return TAIYIN_STATUS_OK;
}

Status search_lunar_eclipses_ut(
    const NativeCalcContext* context,
    SplitJulianDate start_jd_ut,
    SplitJulianDate end_jd_ut,
    uint32_t kind_filter,
    uint64_t flags,
    LunarEclipseResultUt* out_results,
    size_t max_result_count,
    size_t* out_result_count,
    EphemerisEvalDiagnostic* diagnostic
) noexcept {
    if (context == nullptr || out_results == nullptr || out_result_count == nullptr
        || !valid_eclipse_flags(flags)) {
        if (out_result_count) *out_result_count = 0;
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    if (!split_julian_date_is_finite(start_jd_ut) || !split_julian_date_is_finite(end_jd_ut)
        || end_jd_ut <= start_jd_ut) {
        *out_result_count = 0;
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }

    *out_result_count = 0;
    std::vector<LunarEclipseResult> tt_results(max_result_count > 0 ? max_result_count : 1);
    size_t tt_count = 0;
    SplitJulianDate start_jd_tt;
    SplitJulianDate end_jd_tt;
    Status st = eclipse_ut_to_tt(*context, start_jd_ut, &start_jd_tt, nullptr, diagnostic);
    if (st != TAIYIN_STATUS_OK) {
        *out_result_count = 0;
        return st;
    }
    st = eclipse_ut_to_tt(*context, end_jd_ut, &end_jd_tt, nullptr, diagnostic);
    if (st != TAIYIN_STATUS_OK) {
        *out_result_count = 0;
        return st;
    }
    st = search_lunar_eclipses_tt(
        context,
        start_jd_tt,
        end_jd_tt,
        kind_filter,
        flags,
        tt_results.data(),
        max_result_count,
        &tt_count,
        diagnostic);
    if (st != TAIYIN_STATUS_OK) {
        return st;
    }

    for (size_t i = 0; i < tt_count; ++i) {
        const Status fill_status = fill_ut_result(*context, tt_results[i], &out_results[i], diagnostic);
        if (fill_status != TAIYIN_STATUS_OK) {
            *out_result_count = i;
            return fill_status;
        }
    }
    *out_result_count = tt_count;
    return TAIYIN_STATUS_OK;
}

}  // namespace runtime
}  // namespace taiyin
