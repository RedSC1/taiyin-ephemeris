#include "taiyin/runtime/eclipse_search.h"

#include "runtime/eclipse/eclipse_time.h"
#include "runtime/eclipse/lunar_eclipse_sxwnl.h"

#include "taiyin/time.h"

#include <algorithm>
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
                          - 0.02665 * std::sin(Omega * M_PI / 180.0);

    // Convert to radians for trig calls.
    const double M_rad       = M * M_PI / 180.0;
    const double M_prime_rad = M_prime * M_PI / 180.0;
    const double F1_rad      = F1_deg * M_PI / 180.0;
    const double A1_rad      = A1 * M_PI / 180.0;
    const double Omega_rad   = Omega * M_PI / 180.0;

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
uint32_t classify_eclipse(const sxwnl::lunar::LecGeometry& geo, bool include_penumbral) {
    const double rmin = geo.rmin_rad;
    const double mr_toward = geo.moon_radius_toward_shadow_rad;
    const double mr_away = geo.moon_radius_away_from_shadow_rad;
    const double er   = geo.umbra_radius_rad;
    const double Er   = geo.penumbra_radius_rad;

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
    const sxwnl::lunar::LecGeometry& geo,
    SplitJulianDate jd_max_tt,
    LunarEclipseResult* out
) noexcept {
    out->maximum_jd_tt = jd_max_tt;
    out->axis_distance_rad = geo.rmin_rad;
    out->umbra_radius_rad = geo.umbra_radius_rad;
    out->penumbra_radius_rad = geo.penumbra_radius_rad;
    out->moon_radius_rad = geo.moon_radius_rad;

    // Magnitude (taiyin-ephemeris-ts/src/events/eclipse.ts:253-254)
    const double mr = geo.moon_radius_rad;
    const double rho = geo.rmin_rad;
    out->umbral_magnitude =
        (geo.umbra_radius_rad + mr - rho) / (2.0 * mr);
    out->penumbral_magnitude =
        (geo.penumbra_radius_rad + mr - rho) / (2.0 * mr);
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

Status eval_lunar_contact_scalar(
    const NativeCalcContext* context,
    SplitJulianDate jd_tt,
    uint64_t flags,
    LunarContactBoundary boundary,
    double* out,
    EphemerisEvalDiagnostic* diagnostic
) noexcept {
    if (!out) return TAIYIN_ERROR_INVALID_ARGUMENT;
    sxwnl::lunar::LecGeometry geometry;
    const Status status = sxwnl::lunar::lecXY(
        context, jd_tt, flags, &geometry, diagnostic);
    if (status != TAIYIN_STATUS_OK) return status;
    double contact_radius = std::nan("");
    switch (boundary) {
    case LUNAR_CONTACT_PENUMBRAL_OUTER:
        contact_radius = geometry.penumbra_radius_rad
            + geometry.moon_radius_toward_shadow_rad;
        break;
    case LUNAR_CONTACT_UMBRAL_OUTER:
        contact_radius = geometry.umbra_radius_rad
            + geometry.moon_radius_toward_shadow_rad;
        break;
    case LUNAR_CONTACT_UMBRAL_INNER:
        contact_radius = geometry.umbra_radius_rad
            - geometry.moon_radius_away_from_shadow_rad;
        break;
    }
    *out = geometry.rmin_rad - contact_radius;
    return std::isfinite(*out) ? TAIYIN_STATUS_OK : TAIYIN_EPHEMERIS_ERROR_EVAL_FAILED;
}

// Compute a single contact time using the original linear estimate. When a
// limb model is requested, polish that seed against the directional profile.
Status compute_contact_time(
    const NativeCalcContext* context,
    SplitJulianDate jd_max,
    double x, double y, double vx, double vy,
    double r,
    int n,
    LunarContactBoundary boundary,
    uint64_t flags,
    SplitJulianDate* out,
    EphemerisEvalDiagnostic* diagnostic
) noexcept {
    if (!out) return TAIYIN_ERROR_INVALID_ARGUMENT;
    *out = invalid_jd();
    // First estimate
    const double dt1 = sxwnl::lunar::lineT(x, y, vx, vy, r, n);
    if (!std::isfinite(dt1)) {
        return TAIYIN_ERROR_UNSUPPORTED;
    }
    const SplitJulianDate jd_contact1 = jd_max + dt1;

    // Refine: re-eval at contact time, recompute velocity, lineT again
    sxwnl::lunar::LecMaxResult eval_contact;
    {
        const Status st = sxwnl::lunar::lecMax(
            context, jd_contact1, flags, &eval_contact, diagnostic);
        if (st != TAIYIN_STATUS_OK) {
            return st;
        }
    }
    const double dt2 = sxwnl::lunar::lineT(
        eval_contact.geometry.x_rad,
        eval_contact.geometry.y_rad,
        eval_contact.vx_rad_per_day,
        eval_contact.vy_rad_per_day,
        r,
        n);
    SplitJulianDate result = std::isfinite(dt2) ? jd_contact1 + dt2 : jd_contact1;
    if ((flags & TAIYIN_ECLIPSE_LUNAR_LIMB_CORRECTION) == 0u) {
        *out = result;
        return TAIYIN_STATUS_OK;
    }

    constexpr double kDerivativeStepDays = 0.5 / 86400.0;
    constexpr double kMaxCorrectionDays = 30.0 / 86400.0;
    for (int iteration = 0; iteration < 4; ++iteration) {
        double f0 = std::nan("");
        double fm = std::nan("");
        double fp = std::nan("");
        Status status = eval_lunar_contact_scalar(
            context, result, flags, boundary, &f0, diagnostic);
        if (status != TAIYIN_STATUS_OK) return status;
        status = eval_lunar_contact_scalar(
            context, result - kDerivativeStepDays, flags, boundary, &fm, diagnostic);
        if (status != TAIYIN_STATUS_OK) return status;
        status = eval_lunar_contact_scalar(
            context, result + kDerivativeStepDays, flags, boundary, &fp, diagnostic);
        if (status != TAIYIN_STATUS_OK) return status;
        const double slope = (fp - fm) / (2.0 * kDerivativeStepDays);
        if (!std::isfinite(slope) || std::fabs(slope) < 1.0e-12) {
            return TAIYIN_ERROR_UNSUPPORTED;
        }
        double correction = -f0 / slope;
        correction = std::max(-kMaxCorrectionDays, std::min(kMaxCorrectionDays, correction));
        result += correction;
        if (std::fabs(correction) < 0.01 / 86400.0) break;
    }
    *out = result;
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

    // Linear extrapolation (寿星 style).
    sxwnl::lunar::LecMaxResult eval1;
    {
        const Status st = sxwnl::lunar::lecMax(
            context, jd_seed, flags, &eval1, diagnostic);
        if (st != TAIYIN_STATUS_OK) return st;
    }
    const SplitJulianDate jd1 = jd_seed + eval1.dt_days;

    sxwnl::lunar::LecMaxResult eval2;
    {
        const Status st = sxwnl::lunar::lecMax(
            context, jd1, flags, &eval2, diagnostic);
        if (st != TAIYIN_STATUS_OK) return st;
    }
    const SplitJulianDate jd_max = jd1 + eval2.dt_days;

    sxwnl::lunar::LecGeometry geo_max;
    {
        const Status st = sxwnl::lunar::lecXY(
            context, jd_max, flags, &geo_max, diagnostic);
        if (st != TAIYIN_STATUS_OK) return st;
    }

    const uint32_t kind = classify_eclipse(geo_max, include_penumbral(flags));
    out->kind = kind;
    fill_result(geo_max, jd_max, out);
    if (kind == TAIYIN_ECLIPSE_NONE) {
        return TAIYIN_STATUS_OK;
    }

    if (include_contacts(flags)) {
        sxwnl::lunar::LecMaxResult eval_max;
        const Status st = sxwnl::lunar::lecMax(
            context, jd_max, flags, &eval_max, diagnostic);
        if (st == TAIYIN_STATUS_OK) {
            const double mr = geo_max.moon_radius_rad;
            const double er = geo_max.umbra_radius_rad;
            const double Er = geo_max.penumbra_radius_rad;
            const double vx = eval_max.vx_rad_per_day;
            const double vy = eval_max.vy_rad_per_day;
            const double x  = eval_max.geometry.x_rad;
            const double y  = eval_max.geometry.y_rad;

            SplitJulianDate p1 = invalid_jd();
            SplitJulianDate p4 = invalid_jd();
            Status contact_status = compute_contact_time(
                context, jd_max, x, y, vx, vy, mr + Er, 0,
                LUNAR_CONTACT_PENUMBRAL_OUTER, flags, &p1, diagnostic);
            if (contact_status != TAIYIN_STATUS_OK) return contact_status;
            contact_status = compute_contact_time(
                context, jd_max, x, y, vx, vy, mr + Er, 1,
                LUNAR_CONTACT_PENUMBRAL_OUTER, flags, &p4, diagnostic);
            if (contact_status != TAIYIN_STATUS_OK) return contact_status;
            out->contact_jd_tt[TAIYIN_LUNAR_ECLIPSE_CONTACT_P1] = p1;
            out->contact_jd_tt[TAIYIN_LUNAR_ECLIPSE_CONTACT_P4] = p4;
            out->contact_jd_tt[TAIYIN_LUNAR_ECLIPSE_CONTACT_GREATEST] = jd_max;

            if (kind & (TAIYIN_ECLIPSE_PARTIAL | TAIYIN_ECLIPSE_TOTAL)) {
                SplitJulianDate u1 = invalid_jd();
                SplitJulianDate u4 = invalid_jd();
                contact_status = compute_contact_time(
                    context, jd_max, x, y, vx, vy, mr + er, 0,
                    LUNAR_CONTACT_UMBRAL_OUTER, flags, &u1, diagnostic);
                if (contact_status != TAIYIN_STATUS_OK) return contact_status;
                contact_status = compute_contact_time(
                    context, jd_max, x, y, vx, vy, mr + er, 1,
                    LUNAR_CONTACT_UMBRAL_OUTER, flags, &u4, diagnostic);
                if (contact_status != TAIYIN_STATUS_OK) return contact_status;
                out->contact_jd_tt[TAIYIN_LUNAR_ECLIPSE_CONTACT_U1] = u1;
                out->contact_jd_tt[TAIYIN_LUNAR_ECLIPSE_CONTACT_U4] = u4;
            }

            if (kind & TAIYIN_ECLIPSE_TOTAL) {
                SplitJulianDate u2 = invalid_jd();
                SplitJulianDate u3 = invalid_jd();
                contact_status = compute_contact_time(
                    context, jd_max, x, y, vx, vy, er - mr, 0,
                    LUNAR_CONTACT_UMBRAL_INNER, flags, &u2, diagnostic);
                if (contact_status != TAIYIN_STATUS_OK) return contact_status;
                contact_status = compute_contact_time(
                    context, jd_max, x, y, vx, vy, er - mr, 1,
                    LUNAR_CONTACT_UMBRAL_INNER, flags, &u3, diagnostic);
                if (contact_status != TAIYIN_STATUS_OK) return contact_status;
                out->contact_jd_tt[TAIYIN_LUNAR_ECLIPSE_CONTACT_U2] = u2;
                out->contact_jd_tt[TAIYIN_LUNAR_ECLIPSE_CONTACT_U3] = u3;
            }
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
