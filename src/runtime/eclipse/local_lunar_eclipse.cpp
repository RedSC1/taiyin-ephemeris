#include "taiyin/runtime/eclipse_search.h"

#include "runtime/visibility/moon_visibility_internal.h"
#include "runtime/visibility/visibility_math_internal.h"
#include "runtime/visibility/visibility_sampling_internal.h"
#include "runtime/visibility/visibility_search_internal.h"

#include "runtime/eclipse/eclipse_time.h"

#include "taiyin/angle.h"
#include "taiyin/body_id.h"
#include "taiyin/runtime/native_context.h"
#include "runtime/core/native_context_checks.h"
#include "taiyin/runtime/observed_position.h"

#include <algorithm>
#include <cmath>

namespace taiyin {
namespace runtime {
namespace {

double nan_value() noexcept {
    return std::nan("");
}

SplitJulianDate invalid_jd() noexcept {
    return SplitJulianDate(0, nan_value());
}

void init_local_lunar_result(LocalLunarEclipseResultUt* out) noexcept {
    if (!out) return;
    out->eclipse_kind = TAIYIN_ECLIPSE_NONE;
    out->visibility_flags = 0u;
    out->maximum_jd_ut = invalid_jd();
    out->delta_t_seconds = nan_value();
    out->umbral_magnitude = nan_value();
    out->penumbral_magnitude = nan_value();
    out->moonrise_jd_ut = invalid_jd();
    out->moonset_jd_ut = invalid_jd();
    for (size_t i = 0; i < TAIYIN_LUNAR_ECLIPSE_CONTACT_COUNT; ++i) {
        out->contact_jd_ut[i] = invalid_jd();
        out->contact_moon_altitude_deg[i] = nan_value();
        out->contact_moon_azimuth_deg[i] = nan_value();
    }
}

void init_local_lunar_result(LocalLunarEclipseResult* out) noexcept {
    if (!out) return;
    out->eclipse_kind = TAIYIN_ECLIPSE_NONE;
    out->visibility_flags = 0u;
    out->maximum_jd_tt = invalid_jd();
    out->umbral_magnitude = nan_value();
    out->penumbral_magnitude = nan_value();
    out->moonrise_jd_tt = invalid_jd();
    out->moonset_jd_tt = invalid_jd();
    for (size_t i = 0; i < TAIYIN_LUNAR_ECLIPSE_CONTACT_COUNT; ++i) {
        out->contact_jd_tt[i] = invalid_jd();
        out->contact_moon_altitude_deg[i] = nan_value();
        out->contact_moon_azimuth_deg[i] = nan_value();
    }
}

bool valid_local_lunar_eclipse_flags(uint64_t flags) noexcept {
    return (flags & ~(TAIYIN_ECLIPSE_KNOWN_FLAGS | TAIYIN_LOCAL_LUNAR_ECLIPSE_REFRACTION)) == 0u;
}

uint32_t contact_visibility_flag(size_t index) noexcept {
    switch (index) {
    case TAIYIN_LUNAR_ECLIPSE_CONTACT_P1:
        return TAIYIN_ECLIPSE_PENUMBRAL_BEGIN_VISIBLE;
    case TAIYIN_LUNAR_ECLIPSE_CONTACT_U1:
        return TAIYIN_ECLIPSE_PARTIAL_BEGIN_VISIBLE;
    case TAIYIN_LUNAR_ECLIPSE_CONTACT_U2:
        return TAIYIN_ECLIPSE_TOTAL_BEGIN_VISIBLE;
    case TAIYIN_LUNAR_ECLIPSE_CONTACT_GREATEST:
        return TAIYIN_ECLIPSE_MAXIMUM_VISIBLE;
    case TAIYIN_LUNAR_ECLIPSE_CONTACT_U3:
        return TAIYIN_ECLIPSE_TOTAL_END_VISIBLE;
    case TAIYIN_LUNAR_ECLIPSE_CONTACT_U4:
        return TAIYIN_ECLIPSE_PARTIAL_END_VISIBLE;
    case TAIYIN_LUNAR_ECLIPSE_CONTACT_P4:
        return TAIYIN_ECLIPSE_PENUMBRAL_END_VISIBLE;
    default:
        return 0u;
    }
}

void copy_global_lunar_fields(
    const LunarEclipseResultUt& eclipse,
    LocalLunarEclipseResultUt* out
) noexcept {
    out->eclipse_kind = eclipse.kind;
    out->maximum_jd_ut = eclipse.maximum_jd_ut;
    out->delta_t_seconds = eclipse.delta_t_seconds;
    out->umbral_magnitude = eclipse.umbral_magnitude;
    out->penumbral_magnitude = eclipse.penumbral_magnitude;
    for (size_t i = 0; i < TAIYIN_LUNAR_ECLIPSE_CONTACT_COUNT; ++i) {
        out->contact_jd_ut[i] = eclipse.contact_jd_ut[i];
    }
}

Status fill_lunar_ut_result_from_tt(
    const NativeCalcContext& context,
    const LunarEclipseResult& src,
    LunarEclipseResultUt* out,
    EphemerisEvalDiagnostic* diagnostic
) noexcept {
    if (!out) return TAIYIN_ERROR_INVALID_ARGUMENT;
    out->kind = src.kind;
    if (split_julian_date_is_finite(src.maximum_jd_tt)) {
        const Status st = eclipse_tt_to_ut(
            context,
            src.maximum_jd_tt,
            &out->maximum_jd_ut,
            &out->delta_t_seconds,
            diagnostic);
        if (st != TAIYIN_STATUS_OK) return st;
    } else {
        out->maximum_jd_ut = invalid_jd();
        out->delta_t_seconds = nan_value();
    }
    out->umbral_magnitude = src.umbral_magnitude;
    out->penumbral_magnitude = src.penumbral_magnitude;
    out->axis_distance_rad = src.axis_distance_rad;
    out->umbra_radius_rad = src.umbra_radius_rad;
    out->penumbra_radius_rad = src.penumbra_radius_rad;
    out->moon_radius_rad = src.moon_radius_rad;
    for (size_t i = 0; i < TAIYIN_LUNAR_ECLIPSE_CONTACT_COUNT; ++i) {
        if (split_julian_date_is_finite(src.contact_jd_tt[i])) {
            const Status st = eclipse_tt_to_ut(
                context,
                src.contact_jd_tt[i],
                &out->contact_jd_ut[i],
                nullptr,
                diagnostic);
            if (st != TAIYIN_STATUS_OK) return st;
        } else {
            out->contact_jd_ut[i] = invalid_jd();
        }
    }
    return TAIYIN_STATUS_OK;
}

Status fill_local_lunar_tt_result_from_ut(
    const NativeCalcContext& context,
    const LocalLunarEclipseResultUt& src,
    LocalLunarEclipseResult* out,
    EphemerisEvalDiagnostic* diagnostic
) noexcept {
    if (!out) return TAIYIN_ERROR_INVALID_ARGUMENT;
    init_local_lunar_result(out);
    out->eclipse_kind = src.eclipse_kind;
    out->visibility_flags = src.visibility_flags;
    out->umbral_magnitude = src.umbral_magnitude;
    out->penumbral_magnitude = src.penumbral_magnitude;
    if (split_julian_date_is_finite(src.maximum_jd_ut)) {
        const Status st = eclipse_ut_to_tt(context, src.maximum_jd_ut, &out->maximum_jd_tt, nullptr, diagnostic);
        if (st != TAIYIN_STATUS_OK) return st;
    }
    if (split_julian_date_is_finite(src.moonrise_jd_ut)) {
        const Status st = eclipse_ut_to_tt(context, src.moonrise_jd_ut, &out->moonrise_jd_tt, nullptr, diagnostic);
        if (st != TAIYIN_STATUS_OK) return st;
    }
    if (split_julian_date_is_finite(src.moonset_jd_ut)) {
        const Status st = eclipse_ut_to_tt(context, src.moonset_jd_ut, &out->moonset_jd_tt, nullptr, diagnostic);
        if (st != TAIYIN_STATUS_OK) return st;
    }
    for (size_t i = 0; i < TAIYIN_LUNAR_ECLIPSE_CONTACT_COUNT; ++i) {
        out->contact_moon_altitude_deg[i] = src.contact_moon_altitude_deg[i];
        out->contact_moon_azimuth_deg[i] = src.contact_moon_azimuth_deg[i];
        if (!split_julian_date_is_finite(src.contact_jd_ut[i])) {
            out->contact_jd_tt[i] = invalid_jd();
            continue;
        }
        const Status st = eclipse_ut_to_tt(context, src.contact_jd_ut[i], &out->contact_jd_tt[i], nullptr, diagnostic);
        if (st != TAIYIN_STATUS_OK) return st;
    }
    return TAIYIN_STATUS_OK;
}

bool finite_contact_interval(
    const LunarEclipseResultUt& eclipse,
    SplitJulianDate* out_start_jd_ut,
    SplitJulianDate* out_end_jd_ut
) noexcept {
    if (!out_start_jd_ut || !out_end_jd_ut) return false;
    SplitJulianDate start = invalid_jd();
    SplitJulianDate end = invalid_jd();
    for (size_t i = 0; i < TAIYIN_LUNAR_ECLIPSE_CONTACT_COUNT; ++i) {
        const SplitJulianDate t = eclipse.contact_jd_ut[i];
        if (!split_julian_date_is_finite(t)) continue;
        if (!split_julian_date_is_finite(start) || t < start) start = t;
        if (!split_julian_date_is_finite(end) || t > end) end = t;
    }
    if (!split_julian_date_is_finite(start) || !split_julian_date_is_finite(end)
        || !(end > start)) {
        return false;
    }
    *out_start_jd_ut = start;
    *out_end_jd_ut = end;
    return true;
}

bool has_required_local_lunar_contacts(const LunarEclipseResultUt& eclipse) noexcept {
    SplitJulianDate start;
    SplitJulianDate end;
    return split_julian_date_is_finite(eclipse.contact_jd_ut[TAIYIN_LUNAR_ECLIPSE_CONTACT_GREATEST])
        && finite_contact_interval(eclipse, &start, &end);
}

Status sample_contact_visibility(
    const NativeCalcContext* context,
    const LunarEclipseResultUt& eclipse,
    uint32_t observed_flags,
    LocalLunarEclipseResultUt* out,
    EphemerisEvalDiagnostic* diagnostic
) noexcept {
    for (size_t i = 0; i < TAIYIN_LUNAR_ECLIPSE_CONTACT_COUNT; ++i) {
        const SplitJulianDate jd_ut = eclipse.contact_jd_ut[i];
        if (!split_julian_date_is_finite(jd_ut)) continue;
        double altitude = 0.0;
        double azimuth = 0.0;
        double hour_angle = 0.0;
        double distance = 0.0;
        const Status st = visibility_sample_body_center_horizontal_ut(
            context,
            TAIYIN_BODY_MOON,
            jd_ut,
            observed_flags,
            &altitude,
            &azimuth,
            &hour_angle,
            &distance,
            nullptr,
            nullptr,
            diagnostic);
        if (st != TAIYIN_STATUS_OK) return st;
        out->contact_moon_altitude_deg[i] = altitude * TAIYIN_RAD_TO_DEG;
        out->contact_moon_azimuth_deg[i] = azimuth * TAIYIN_RAD_TO_DEG;
        if (altitude >= 0.0) {
            out->visibility_flags |= contact_visibility_flag(i);
        }
        (void)hour_angle;
        (void)distance;
    }
    return TAIYIN_STATUS_OK;
}

Status sample_rise_set_during_eclipse(
    const NativeCalcContext* context,
    const LunarEclipseResultUt& eclipse,
    uint32_t moon_visibility_flags,
    LocalLunarEclipseResultUt* out,
    EphemerisEvalDiagnostic* diagnostic
) noexcept {
    SplitJulianDate start;
    SplitJulianDate end;
    if (!finite_contact_interval(eclipse, &start, &end)) {
        return TAIYIN_STATUS_OK;
    }

    VisibilityAltitudeSearchResult rise;
    Status st = moon_visibility_search_rise_set_at_horizon_ut(
        context,
        start,
        end,
        TAIYIN_MOON_VISIBILITY_EVENT_RISE,
        TAIYIN_MOON_VISIBILITY_LIMB_CENTER,
        0.0,
        moon_visibility_flags,
        &rise,
        diagnostic);
    if (st != TAIYIN_STATUS_OK) return st;
    if (rise.altitude_state == TAIYIN_VISIBILITY_ALTITUDE_STATE_CROSSES
        && split_julian_date_is_finite(rise.jd_ut)) {
        out->moonrise_jd_ut = rise.jd_ut;
        out->visibility_flags |= TAIYIN_ECLIPSE_VISIBLE_AT_OBSERVER;
    } else if (rise.altitude_state == TAIYIN_VISIBILITY_ALTITUDE_STATE_ALWAYS_ABOVE) {
        out->visibility_flags |= TAIYIN_ECLIPSE_VISIBLE_AT_OBSERVER;
    }

    VisibilityAltitudeSearchResult set;
    st = moon_visibility_search_rise_set_at_horizon_ut(
        context,
        start,
        end,
        TAIYIN_MOON_VISIBILITY_EVENT_SET,
        TAIYIN_MOON_VISIBILITY_LIMB_CENTER,
        0.0,
        moon_visibility_flags,
        &set,
        diagnostic);
    if (st != TAIYIN_STATUS_OK) return st;
    if (set.altitude_state == TAIYIN_VISIBILITY_ALTITUDE_STATE_CROSSES
        && split_julian_date_is_finite(set.jd_ut)) {
        out->moonset_jd_ut = set.jd_ut;
        out->visibility_flags |= TAIYIN_ECLIPSE_VISIBLE_AT_OBSERVER;
    } else if (set.altitude_state == TAIYIN_VISIBILITY_ALTITUDE_STATE_ALWAYS_ABOVE) {
        out->visibility_flags |= TAIYIN_ECLIPSE_VISIBLE_AT_OBSERVER;
    }

    return TAIYIN_STATUS_OK;
}

}  // namespace

Status compute_local_lunar_eclipse_visibility_ut(
    const NativeCalcContext* context,
    const LunarEclipseResultUt* eclipse,
    uint64_t flags,
    LocalLunarEclipseResultUt* out,
    EphemerisEvalDiagnostic* diagnostic
) noexcept {
    init_local_lunar_result(out);
    if (!context
        || !eclipse
        || !out
        || !native_context_has_observer_location(*context)
        || !valid_local_lunar_eclipse_flags(flags)) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    if (eclipse->kind == TAIYIN_ECLIPSE_NONE) {
        copy_global_lunar_fields(*eclipse, out);
        return TAIYIN_STATUS_OK;
    }
    if (!has_required_local_lunar_contacts(*eclipse)) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }

    NativeCalcContext local;
    Status st = native_context_copy_geocentric_with_observer(*context, &local);
    if (st != TAIYIN_STATUS_OK) return st;
    copy_global_lunar_fields(*eclipse, out);

    const bool use_refraction = (flags & TAIYIN_LOCAL_LUNAR_ECLIPSE_REFRACTION) != 0u;
    const uint32_t observed_flags = use_refraction ? TAIYIN_OBSERVED_REFRACTION : 0u;
    const uint32_t moon_visibility_flags = use_refraction
        ? TAIYIN_MOON_VISIBILITY_FLAG_REFRACTION
        : 0u;

    st = sample_contact_visibility(
        &local,
        *eclipse,
        observed_flags,
        out,
        diagnostic);
    if (st != TAIYIN_STATUS_OK) return st;
    if ((out->visibility_flags
            & (TAIYIN_ECLIPSE_PENUMBRAL_BEGIN_VISIBLE
               | TAIYIN_ECLIPSE_PARTIAL_BEGIN_VISIBLE
               | TAIYIN_ECLIPSE_TOTAL_BEGIN_VISIBLE
               | TAIYIN_ECLIPSE_MAXIMUM_VISIBLE
               | TAIYIN_ECLIPSE_TOTAL_END_VISIBLE
               | TAIYIN_ECLIPSE_PARTIAL_END_VISIBLE
               | TAIYIN_ECLIPSE_PENUMBRAL_END_VISIBLE)) != 0u) {
        out->visibility_flags |= TAIYIN_ECLIPSE_VISIBLE_AT_OBSERVER;
    }

    st = sample_rise_set_during_eclipse(
        &local,
        *eclipse,
        moon_visibility_flags,
        out,
        diagnostic);
    if (st != TAIYIN_STATUS_OK) return st;

    return TAIYIN_STATUS_OK;
}

Status compute_local_lunar_eclipse_visibility_tt(
    const NativeCalcContext* context,
    const LunarEclipseResult* eclipse,
    uint64_t flags,
    LocalLunarEclipseResult* out,
    EphemerisEvalDiagnostic* diagnostic
) noexcept {
    init_local_lunar_result(out);
    if (!context
        || !eclipse
        || !out
        || !native_context_has_observer_location(*context)
        || !valid_local_lunar_eclipse_flags(flags)) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }

    LunarEclipseResultUt eclipse_ut;
    Status st = fill_lunar_ut_result_from_tt(*context, *eclipse, &eclipse_ut, diagnostic);
    if (st != TAIYIN_STATUS_OK) return st;

    LocalLunarEclipseResultUt local_ut;
    st = compute_local_lunar_eclipse_visibility_ut(
        context,
        &eclipse_ut,
        flags,
        &local_ut,
        diagnostic);
    if (st != TAIYIN_STATUS_OK) return st;

    return fill_local_lunar_tt_result_from_ut(*context, local_ut, out, diagnostic);
}

Status search_next_local_lunar_eclipse_ut(
    const NativeCalcContext* context,
    SplitJulianDate jd_start_ut,
    uint32_t kind_filter,
    uint64_t flags,
    LocalLunarEclipseResultUt* out,
    EphemerisEvalDiagnostic* diagnostic
) noexcept {
    init_local_lunar_result(out);
    if (!context
        || !out
        || !split_julian_date_is_finite(jd_start_ut)
        || !native_context_has_observer_location(*context)
        || !valid_local_lunar_eclipse_flags(flags)) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }

    LunarEclipseResultUt global;
    const uint64_t global_flags =
        (flags & ~TAIYIN_LOCAL_LUNAR_ECLIPSE_REFRACTION)
        | TAIYIN_ECLIPSE_INCLUDE_CONTACTS;
    NativeCalcContext local;
    Status st = native_context_copy_geocentric_with_observer(*context, &local);
    if (st != TAIYIN_STATUS_OK) return st;
    st = search_next_lunar_eclipse_ut(
        &local,
        jd_start_ut,
        kind_filter,
        global_flags,
        &global,
        diagnostic);
    if (st != TAIYIN_STATUS_OK) return st;

    return compute_local_lunar_eclipse_visibility_ut(
        &local,
        &global,
        flags,
        out,
        diagnostic);
}

Status search_next_local_lunar_eclipse_tt(
    const NativeCalcContext* context,
    SplitJulianDate jd_start_tt,
    uint32_t kind_filter,
    uint64_t flags,
    LocalLunarEclipseResult* out,
    EphemerisEvalDiagnostic* diagnostic
) noexcept {
    init_local_lunar_result(out);
    if (!context
        || !out
        || !split_julian_date_is_finite(jd_start_tt)
        || !native_context_has_observer_location(*context)
        || !valid_local_lunar_eclipse_flags(flags)) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }

    LunarEclipseResult global;
    const uint64_t global_flags =
        (flags & ~TAIYIN_LOCAL_LUNAR_ECLIPSE_REFRACTION)
        | TAIYIN_ECLIPSE_INCLUDE_CONTACTS;
    NativeCalcContext local;
    Status st = native_context_copy_geocentric_with_observer(*context, &local);
    if (st != TAIYIN_STATUS_OK) return st;
    st = search_next_lunar_eclipse_tt(
        &local,
        jd_start_tt,
        kind_filter,
        global_flags,
        &global,
        diagnostic);
    if (st != TAIYIN_STATUS_OK) return st;

    return compute_local_lunar_eclipse_visibility_tt(
        &local,
        &global,
        flags,
        out,
        diagnostic);
}

}  // namespace runtime
}  // namespace taiyin
