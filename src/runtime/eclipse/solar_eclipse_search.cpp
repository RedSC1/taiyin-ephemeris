#include "taiyin/runtime/eclipse_search.h"

#include "runtime/eclipse/eclipse_time.h"
#include "runtime/apparent/fast_apparent.h"
#include "runtime/eclipse/solar_eclipse_direct_solver.h"
#include "runtime/core/native_context_checks.h"

#include "taiyin/body_id.h"
#include "taiyin/time.h"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <vector>

namespace taiyin {
namespace runtime {

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
) noexcept;

Status complete_local_solar_eclipse_contacts_from_max(
    const NativeCalcContext* context,
    double longitude_deg,
    double latitude_deg,
    double height_m,
    uint64_t flags,
    bool complete_visibility,
    LocalSolarEclipseResult* out,
    EphemerisEvalDiagnostic* diagnostic
) noexcept;

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
) noexcept;

namespace {

SplitJulianDate invalid_jd() noexcept {
    return SplitJulianDate(0, std::numeric_limits<double>::quiet_NaN());
}

// Meeus ch.52 node-distance threshold for eclipse possibility.
constexpr double kSolarNodeLimitDeg = 23.0;

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

double solar_meeus_f(int k) {
    const double T = static_cast<double>(k) / 1236.85;
    const double T2 = T * T;
    const double T3 = T2 * T;
    const double T4 = T3 * T;
    return degnorm(160.7108 + 390.67050274 * k
                   - 0.0016341 * T2
                   - 0.00000227 * T3
                   + 0.000000011 * T4);
}

double solar_meeus_f_normalized(int k) {
    double F = solar_meeus_f(k);
    if (F > 180.0) F -= 180.0;
    return F;
}

bool solar_meeus_filter_passes(int k) {
    const double F = solar_meeus_f_normalized(k);
    return F <= kSolarNodeLimitDeg || F >= (180.0 - kSolarNodeLimitDeg);
}

SplitJulianDate solar_meeus_new_moon_jd(int k) {
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

int solar_meeus_k_for_jd(SplitJulianDate jd_tt) {
    const double k_continuous = (jd_tt - SplitJulianDate(2451545, 0.0))
        / 365.2425 * 12.3685;
    return static_cast<int>(std::floor(k_continuous));
}

bool include_contacts(uint64_t flags) {
    return (flags & TAIYIN_ECLIPSE_INCLUDE_CONTACTS) != 0;
}

bool solar_kind_matches_filter(uint32_t kind, uint32_t kind_filter) noexcept {
    if (kind == TAIYIN_ECLIPSE_NONE) {
        return false;
    }
    if (kind_filter == 0) {
        kind_filter = TAIYIN_ECLIPSE_ALL_SOLAR;
    }
    const uint32_t type_mask = TAIYIN_ECLIPSE_PARTIAL
                             | TAIYIN_ECLIPSE_TOTAL
                             | TAIYIN_ECLIPSE_ANNULAR
                             | TAIYIN_ECLIPSE_HYBRID;
    const uint32_t centrality_mask = TAIYIN_ECLIPSE_CENTRAL
                                   | TAIYIN_ECLIPSE_NONCENTRAL;
    const uint32_t requested_types = kind_filter & type_mask;
    const uint32_t requested_centrality = kind_filter & centrality_mask;
    if (requested_types != 0 && (kind & requested_types) == 0) {
        return false;
    }
    if (requested_centrality != 0 && (kind & requested_centrality) == 0) {
        return false;
    }
    return requested_types != 0 || requested_centrality != 0;
}

bool local_solar_eclipse_visible_for_search(const LocalSolarEclipseResult& result) noexcept {
    if ((result.kind & TAIYIN_ECLIPSE_VISIBLE_AT_OBSERVER) != 0) {
        return true;
    }
    if ((result.kind & TAIYIN_ECLIPSE_MAXIMUM_VISIBLE) != 0) {
        return true;
    }
    return result.sunrise_magnitude > 0.0 || result.sunset_magnitude > 0.0;
}

bool local_solar_central_filter_only(uint32_t kind_filter) noexcept {
    const uint32_t central_types = TAIYIN_ECLIPSE_TOTAL
                                 | TAIYIN_ECLIPSE_ANNULAR
                                 | TAIYIN_ECLIPSE_HYBRID;
    return (kind_filter & central_types) != 0
        && (kind_filter & TAIYIN_ECLIPSE_PARTIAL) == 0;
}

FastApparentOptions local_solar_eclipse_window_options(uint64_t flags) noexcept {
    FastApparentOptions options;
    options.frame = FAST_APPARENT_TRUE_EQUATOR_OF_DATE;
    options.with_velocity = false;
    options.true_position = (flags & TAIYIN_ECLIPSE_TRUEPOS) != 0;
    return options;
}

void clear_local_solar_contact_outputs(LocalSolarEclipseResult* result) noexcept {
    for (size_t i = 0; i < TAIYIN_LOCAL_SOLAR_CONTACT_COUNT; ++i) {
        result->contact_jd_tt[i] = invalid_jd();
    }
    result->position_angle_c1_deg = std::nan("");
    result->position_angle_c4_deg = std::nan("");
    result->vertex_angle_c1_deg = std::nan("");
    result->vertex_angle_c4_deg = std::nan("");
    result->duration_seconds = 0.0;
}

void init_local_solar_result(LocalSolarEclipseResult* out) noexcept {
    std::memset(out, 0, sizeof(*out));
    out->kind = TAIYIN_ECLIPSE_NONE;
    out->maximum_jd_tt = invalid_jd();
    out->magnitude = std::nan("");
    out->obscuration = std::nan("");
    out->sun_altitude_deg = std::nan("");
    out->sun_azimuth_deg = std::nan("");
    out->sunrise_magnitude = std::nan("");
    out->sunset_magnitude = std::nan("");
    out->moon_sun_radius_ratio = std::nan("");
    clear_local_solar_contact_outputs(out);
}

void init_local_solar_ut_result(LocalSolarEclipseResultUt* out) noexcept {
    std::memset(out, 0, sizeof(*out));
    out->kind = TAIYIN_ECLIPSE_NONE;
    out->maximum_jd_ut = invalid_jd();
    out->delta_t_seconds = std::nan("");
    out->magnitude = std::nan("");
    out->obscuration = std::nan("");
    out->sun_altitude_deg = std::nan("");
    out->sun_azimuth_deg = std::nan("");
    for (size_t i = 0; i < TAIYIN_LOCAL_SOLAR_CONTACT_COUNT; ++i) {
        out->contact_jd_ut[i] = invalid_jd();
    }
    out->position_angle_c1_deg = std::nan("");
    out->position_angle_c4_deg = std::nan("");
    out->vertex_angle_c1_deg = std::nan("");
    out->vertex_angle_c4_deg = std::nan("");
    out->sunrise_magnitude = std::nan("");
    out->sunset_magnitude = std::nan("");
    out->duration_seconds = 0.0;
    out->moon_sun_radius_ratio = std::nan("");
}

void init_solar_result(SolarEclipseResult* out) noexcept {
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

void init_solar_result(SolarEclipseResultUt* out) noexcept {
    out->kind = TAIYIN_ECLIPSE_NONE;
    out->maximum_jd_ut = invalid_jd();
    out->delta_t_seconds = std::nan("");
    out->axis_distance_km = std::nan("");
    out->penumbra_radius_km = std::nan("");
    out->core_radius_km = std::nan("");
    out->penumbral_margin_km = std::nan("");
    out->central_margin_km = std::nan("");
    out->maximum_latitude_deg = std::nan("");
    out->maximum_longitude_deg = std::nan("");
    for (size_t i = 0; i < TAIYIN_SOLAR_ECLIPSE_CONTACT_COUNT; ++i) {
        out->contact_jd_ut[i] = invalid_jd();
    }
}

Status fill_solar_ut_result(
    const NativeCalcContext& context,
    const SolarEclipseResult& src,
    SolarEclipseResultUt* out,
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
    out->axis_distance_km = src.axis_distance_km;
    out->penumbra_radius_km = src.penumbra_radius_km;
    out->core_radius_km = src.core_radius_km;
    out->penumbral_margin_km = src.penumbral_margin_km;
    out->central_margin_km = src.central_margin_km;
    out->maximum_latitude_deg = src.maximum_latitude_deg;
    out->maximum_longitude_deg = src.maximum_longitude_deg;
    for (size_t i = 0; i < TAIYIN_SOLAR_ECLIPSE_CONTACT_COUNT; ++i) {
        if (split_julian_date_is_finite(src.contact_jd_tt[i])) {
            const Status st = eclipse_tt_to_ut(context, src.contact_jd_tt[i], &out->contact_jd_ut[i], nullptr, diagnostic);
            if (st != TAIYIN_STATUS_OK) return st;
        } else {
            out->contact_jd_ut[i] = invalid_jd();
        }
    }
    return TAIYIN_STATUS_OK;
}

}  // namespace

// ===========================================================================
// Public API
// ===========================================================================
Status solve_solar_eclipse_at(
    const NativeCalcContext* context,
    SplitJulianDate jd_tt,
    uint64_t flags,
    SolarEclipseResult* out,
    EphemerisEvalDiagnostic* diagnostic
) noexcept {
    if (context == nullptr || out == nullptr || !split_julian_date_is_finite(jd_tt)
        || !valid_eclipse_flags(flags)) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    init_solar_result(out);

    return solve_solar_eclipse_direct_for_meeus_k(
        context, solar_meeus_k_for_jd(jd_tt), flags, 0, true, true, out, diagnostic);
}

Status solve_solar_eclipse_at_ut(
    const NativeCalcContext* context,
    SplitJulianDate jd_ut,
    uint64_t flags,
    SolarEclipseResultUt* out,
    EphemerisEvalDiagnostic* diagnostic
) noexcept {
    if (context == nullptr || out == nullptr || !split_julian_date_is_finite(jd_ut)
        || !valid_eclipse_flags(flags)) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    init_solar_result(out);

    SolarEclipseResult tt_result;
    SplitJulianDate jd_tt;
    Status st = eclipse_ut_to_tt(*context, jd_ut, &jd_tt, nullptr, diagnostic);
    if (st != TAIYIN_STATUS_OK) return st;
    st = solve_solar_eclipse_at(context, jd_tt, flags, &tt_result, diagnostic);
    if (st != TAIYIN_STATUS_OK) {
        return st;
    }
    return fill_solar_ut_result(*context, tt_result, out, diagnostic);
}

Status search_next_solar_eclipse_tt(
    const NativeCalcContext* context,
    SplitJulianDate jd_start_tt,
    uint32_t kind_filter,
    uint64_t flags,
    SolarEclipseResult* out,
    EphemerisEvalDiagnostic* diagnostic
) noexcept {
    if (context == nullptr || out == nullptr || !split_julian_date_is_finite(jd_start_tt)
        || !valid_eclipse_flags(flags)) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    init_solar_result(out);

    const bool backward = (flags & TAIYIN_ECLIPSE_BACKWARD) != 0;
    int k = solar_meeus_k_for_jd(jd_start_tt);
    if (!backward && solar_meeus_new_moon_jd(k) < jd_start_tt - 1.0) {
        k += 1;
    }

    const int direction = backward ? -1 : 1;
    const int k_limit = backward ? k - 100000 : k + 100000;
    for (int ki = k; backward ? ki > k_limit : ki < k_limit; ki += direction) {
        if (!solar_meeus_filter_passes(ki)) {
            continue;
        }
        SolarEclipseResult result;
        Status st = solve_solar_eclipse_direct_for_meeus_k(
            context, ki, flags, kind_filter, true, true, &result, diagnostic);
        if (st != TAIYIN_STATUS_OK) {
            return st;
        }
        if (result.kind == TAIYIN_ECLIPSE_NONE) {
            continue;
        }
        if (!solar_kind_matches_filter(result.kind, kind_filter)) {
            continue;
        }
        if (backward && result.maximum_jd_tt >= jd_start_tt) {
            continue;
        }
        if (!backward && result.maximum_jd_tt <= jd_start_tt) {
            continue;
        }
        *out = result;
        return TAIYIN_STATUS_OK;
    }
    return TAIYIN_EVENT_ERROR_NOT_FOUND;
}

Status search_next_solar_eclipse_ut(
    const NativeCalcContext* context,
    SplitJulianDate jd_start_ut,
    uint32_t kind_filter,
    uint64_t flags,
    SolarEclipseResultUt* out,
    EphemerisEvalDiagnostic* diagnostic
) noexcept {
    if (context == nullptr || out == nullptr || !split_julian_date_is_finite(jd_start_ut)
        || !valid_eclipse_flags(flags)) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    init_solar_result(out);

    SolarEclipseResult tt_result;
    SplitJulianDate jd_start_tt;
    Status st = eclipse_ut_to_tt(*context, jd_start_ut, &jd_start_tt, nullptr, diagnostic);
    if (st != TAIYIN_STATUS_OK) return st;
    st = search_next_solar_eclipse_tt(
        context,
        jd_start_tt,
        kind_filter,
        flags,
        &tt_result,
        diagnostic);
    if (st != TAIYIN_STATUS_OK) {
        return st;
    }
    return fill_solar_ut_result(*context, tt_result, out, diagnostic);
}

Status search_solar_eclipses_tt(
    const NativeCalcContext* context,
    SplitJulianDate start_jd_tt,
    SplitJulianDate end_jd_tt,
    uint32_t kind_filter,
    uint64_t flags,
    SolarEclipseResult* out_results,
    size_t max_result_count,
    size_t* out_result_count,
    EphemerisEvalDiagnostic* diagnostic
) noexcept {
    if (context == nullptr || out_results == nullptr || out_result_count == nullptr
        || !split_julian_date_is_finite(start_jd_tt) || !split_julian_date_is_finite(end_jd_tt)
        || end_jd_tt <= start_jd_tt
        || !valid_eclipse_flags(flags)) {
        if (out_result_count) *out_result_count = 0;
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    *out_result_count = 0;
    size_t count = 0;

    int k = solar_meeus_k_for_jd(start_jd_tt) - 1;
    const int k_end = solar_meeus_k_for_jd(end_jd_tt) + 1;
    for (; k <= k_end; ++k) {
        if (!solar_meeus_filter_passes(k)) {
            continue;
        }
        const SplitJulianDate seed = solar_meeus_new_moon_jd(k);
        if (seed < start_jd_tt - 5.0) {
            continue;
        }
        if (seed > end_jd_tt + 5.0) {
            break;
        }

        SolarEclipseResult result;
        Status st = solve_solar_eclipse_direct_for_meeus_k(
            context, k, flags, kind_filter, true, true, &result, diagnostic);
        if (st != TAIYIN_STATUS_OK) {
            return st;
        }
        if (result.kind == TAIYIN_ECLIPSE_NONE) {
            continue;
        }
        if (!solar_kind_matches_filter(result.kind, kind_filter)) {
            continue;
        }
        if (result.maximum_jd_tt < start_jd_tt || result.maximum_jd_tt > end_jd_tt) {
            continue;
        }
        if (count >= max_result_count) {
            return TAIYIN_ERROR_OUT_OF_MEMORY;
        }
        out_results[count++] = result;
    }
    *out_result_count = count;
    return TAIYIN_STATUS_OK;
}

Status search_solar_eclipses_ut(
    const NativeCalcContext* context,
    SplitJulianDate start_jd_ut,
    SplitJulianDate end_jd_ut,
    uint32_t kind_filter,
    uint64_t flags,
    SolarEclipseResultUt* out_results,
    size_t max_result_count,
    size_t* out_result_count,
    EphemerisEvalDiagnostic* diagnostic
) noexcept {
    if (context == nullptr || out_results == nullptr || out_result_count == nullptr
        || !split_julian_date_is_finite(start_jd_ut) || !split_julian_date_is_finite(end_jd_ut)
        || end_jd_ut <= start_jd_ut
        || !valid_eclipse_flags(flags)) {
        if (out_result_count) *out_result_count = 0;
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }

    std::vector<SolarEclipseResult> tt_results(max_result_count > 0 ? max_result_count : 1);
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
    st = search_solar_eclipses_tt(
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
        *out_result_count = 0;
        return st;
    }
    for (size_t i = 0; i < tt_count; ++i) {
        const Status fill_status = fill_solar_ut_result(*context, tt_results[i], &out_results[i], diagnostic);
        if (fill_status != TAIYIN_STATUS_OK) {
            *out_result_count = i;
            return fill_status;
        }
    }
    *out_result_count = tt_count;
    return TAIYIN_STATUS_OK;
}

Status search_next_local_solar_eclipse_tt(
    const NativeCalcContext* context,
    SplitJulianDate jd_start_tt,
    uint32_t kind_filter,
    uint64_t flags,
    LocalSolarEclipseResult* out,
    EphemerisEvalDiagnostic* diagnostic
) noexcept {
    if (out != nullptr) {
        init_local_solar_result(out);
    }
    double longitude_deg = 0.0;
    double latitude_deg = 0.0;
    double height_m = 0.0;
    if (context == nullptr
        || out == nullptr
        || !split_julian_date_is_finite(jd_start_tt)
        || !valid_local_solar_eclipse_flags(flags)
        || !native_context_observer_degrees(*context, &longitude_deg, &latitude_deg, &height_m)) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    NativeCalcContext local;
    Status st = native_context_copy_geocentric_with_observer(*context, &local);
    if (st != TAIYIN_STATUS_OK) return st;
    if (kind_filter == 0) {
        kind_filter = TAIYIN_ECLIPSE_ALL_SOLAR;
    }

    const bool backward = (flags & TAIYIN_ECLIPSE_BACKWARD) != 0;
    int k = solar_meeus_k_for_jd(jd_start_tt);
    if (!backward && solar_meeus_new_moon_jd(k) < jd_start_tt - 1.0) {
        k += 1;
    }

    const int direction = backward ? -1 : 1;
    const int k_limit = backward ? k - 100000 : k + 100000;
    // Local solve treats flags==0 as include-contacts; keep a harmless local-ignored
    // eclipse flag set so search probes can classify candidates without contacts.
    const uint64_t probe_flags = (flags & ~TAIYIN_ECLIPSE_INCLUDE_CONTACTS)
        | TAIYIN_ECLIPSE_EXCLUDE_PENUMBRAL;
    const bool central_filter = local_solar_central_filter_only(kind_filter);
    for (int ki = k; backward ? ki > k_limit : ki < k_limit; ki += direction) {
        if (!solar_meeus_filter_passes(ki)) {
            continue;
        }
        const SplitJulianDate seed_jd_tt = solar_meeus_new_moon_jd(ki);
        bool local_possible = true;
        SplitJulianDate local_seed_jd_tt = seed_jd_tt;
        st = probe_local_solar_eclipse_for_search(
            &local,
            seed_jd_tt,
            longitude_deg,
            latitude_deg,
            height_m,
            probe_flags,
            central_filter,
            &local_possible,
            &local_seed_jd_tt,
            diagnostic);
        if (st != TAIYIN_STATUS_OK) {
            return st;
        }
        if (!local_possible) {
            continue;
        }

        const FastApparentOptions probe_window_options = local_solar_eclipse_window_options(probe_flags);
        FastApparentCorrectionConfig probe_correction_config;
        probe_correction_config.initial_half_days = 3.0 / 24.0;
        probe_correction_config.sample_step_days = 3.0 / 24.0;
        FastApparentCorrectionSeries window_series;
        FastApparentCorrectionEpochSample correction_sample;
        Status correction_status = init_fast_correction_series(
            &local,
            TAIYIN_BODY_MOON,
            TAIYIN_BODY_SUN,
            probe_window_options,
            probe_correction_config,
            local_seed_jd_tt,
            &window_series,
            diagnostic);
        if (correction_status != TAIYIN_STATUS_OK) return correction_status;

        LocalSolarEclipseResult candidate;
        st = solve_local_solar_eclipse_at_tt_with_besselian_seed(
            &local, local_seed_jd_tt,
            longitude_deg, latitude_deg, height_m,
            probe_flags, local_seed_jd_tt, nullptr, &window_series, false, &candidate, diagnostic);
        if (st != TAIYIN_STATUS_OK) {
            return st;
        }

        if (candidate.kind == TAIYIN_ECLIPSE_NONE || candidate.magnitude <= 0.0) {
            continue;
        }

        if (!local_solar_eclipse_visible_for_search(candidate)) {
            if (!solar_kind_matches_filter(candidate.kind, kind_filter)) {
                continue;
            }
            FastApparentCorrectionConfig horizon_config = probe_correction_config;
            horizon_config.initial_half_days = 6.0 / 24.0;
            correction_status = get_fast_correction(
                &local,
                TAIYIN_BODY_MOON,
                TAIYIN_BODY_SUN,
                probe_window_options,
                horizon_config,
                candidate.maximum_jd_tt,
                &window_series,
                diagnostic,
                &correction_sample);
            if (correction_status != TAIYIN_STATUS_OK) return correction_status;
            LocalSolarEclipseResult horizon_candidate = candidate;
            clear_local_solar_contact_outputs(&horizon_candidate);
            st = complete_local_solar_eclipse_contacts_from_max(
                &local,
                longitude_deg, latitude_deg, height_m,
                probe_flags, true,
                &window_series, nullptr,
                &horizon_candidate, diagnostic);
            if (st != TAIYIN_STATUS_OK) {
                return st;
            }
            if (horizon_candidate.kind == TAIYIN_ECLIPSE_NONE
                || horizon_candidate.magnitude <= 0.0
                || !local_solar_eclipse_visible_for_search(horizon_candidate)) {
                continue;
            }
            candidate = horizon_candidate;
        }

        if (!solar_kind_matches_filter(candidate.kind, kind_filter)) {
            continue;
        }

        if (backward && candidate.maximum_jd_tt >= jd_start_tt) {
            continue;
        }
        if (!backward && candidate.maximum_jd_tt <= jd_start_tt) {
            continue;
        }

        if (include_contacts(flags) || flags == 0) {
            const FastApparentOptions contact_window_options = local_solar_eclipse_window_options(flags);
            FastApparentCorrectionConfig contact_config = probe_correction_config;
            contact_config.initial_half_days = 6.0 / 24.0;
            correction_status = init_fast_correction_series(
                &local,
                TAIYIN_BODY_MOON,
                TAIYIN_BODY_SUN,
                contact_window_options,
                contact_config,
                candidate.maximum_jd_tt,
                &window_series,
                diagnostic);
            if (correction_status != TAIYIN_STATUS_OK) return correction_status;
            SolarBesselianPolynomial contact_seed;
            const Status seed_status = compute_solar_besselian_polynomial_tt_with_corrections(
                &local, candidate.maximum_jd_tt, 12.0, 3.0, 2,
                flags, &window_series, &contact_seed, diagnostic);
            const SolarBesselianPolynomial* contact_seed_ptr =
                seed_status == TAIYIN_STATUS_OK ? &contact_seed : nullptr;
            *out = candidate;
            clear_local_solar_contact_outputs(out);
            st = complete_local_solar_eclipse_contacts_from_max(
                &local,
                longitude_deg, latitude_deg, height_m,
                flags, true,
                &window_series, contact_seed_ptr,
                out, diagnostic);
            if (st != TAIYIN_STATUS_OK) {
                return st;
            }
        } else {
            *out = candidate;
            clear_local_solar_contact_outputs(out);
        }

        return TAIYIN_STATUS_OK;
    }

    return TAIYIN_EVENT_ERROR_NOT_FOUND;
}

Status search_next_local_solar_eclipse_ut(
    const NativeCalcContext* context,
    SplitJulianDate jd_start_ut,
    uint32_t kind_filter,
    uint64_t flags,
    LocalSolarEclipseResultUt* out,
    EphemerisEvalDiagnostic* diagnostic
) noexcept {
    if (out != nullptr) {
        init_local_solar_ut_result(out);
    }
    if (context == nullptr
        || out == nullptr
        || !split_julian_date_is_finite(jd_start_ut)
        || !valid_local_solar_eclipse_flags(flags)
        || !native_context_has_observer_location(*context)) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }

    LocalSolarEclipseResult tt_out;
    SplitJulianDate jd_start_tt;
    Status st = eclipse_ut_to_tt(*context, jd_start_ut, &jd_start_tt, nullptr, diagnostic);
    if (st != TAIYIN_STATUS_OK) return st;
    st = search_next_local_solar_eclipse_tt(
        context, jd_start_tt,
        kind_filter, flags, &tt_out, diagnostic);
    if (st != TAIYIN_STATUS_OK) return st;

    out->kind = tt_out.kind;
    st = eclipse_tt_to_ut(*context, tt_out.maximum_jd_tt, &out->maximum_jd_ut, &out->delta_t_seconds, diagnostic);
    if (st != TAIYIN_STATUS_OK) return st;
    out->magnitude = tt_out.magnitude;
    out->obscuration = tt_out.obscuration;
    out->sun_altitude_deg = tt_out.sun_altitude_deg;
    out->sun_azimuth_deg = tt_out.sun_azimuth_deg;
    out->duration_seconds = tt_out.duration_seconds;
    out->moon_sun_radius_ratio = tt_out.moon_sun_radius_ratio;
    out->position_angle_c1_deg = tt_out.position_angle_c1_deg;
    out->position_angle_c4_deg = tt_out.position_angle_c4_deg;
    out->vertex_angle_c1_deg = tt_out.vertex_angle_c1_deg;
    out->vertex_angle_c4_deg = tt_out.vertex_angle_c4_deg;
    out->sunrise_magnitude = tt_out.sunrise_magnitude;
    out->sunset_magnitude = tt_out.sunset_magnitude;
    for (size_t i = 0; i < TAIYIN_LOCAL_SOLAR_CONTACT_COUNT; ++i) {
        if (split_julian_date_is_finite(tt_out.contact_jd_tt[i])) {
            st = eclipse_tt_to_ut(*context, tt_out.contact_jd_tt[i], &out->contact_jd_ut[i], nullptr, diagnostic);
            if (st != TAIYIN_STATUS_OK) return st;
        } else {
            out->contact_jd_ut[i] = invalid_jd();
        }
    }
    return TAIYIN_STATUS_OK;
}

}  // namespace runtime
}  // namespace taiyin
