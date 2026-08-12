#include "taiyin/c/visibility.h"

#include "c_api_internal.h"
#include "taiyin/runtime/moon_visibility.h"
#include "taiyin/runtime/planet_visibility.h"
#include "taiyin/runtime/solar_visibility.h"
#include "taiyin/runtime/star_visibility.h"

#include <cstring>
#include <limits>

namespace {

bool valid_diagnostic(taiyin_ephemeris_diagnostic* diagnostic) noexcept {
    return !diagnostic || taiyin_c_internal::valid_struct(diagnostic);
}

taiyin::SplitJulianDate cpp_date(
    const taiyin_split_julian_date* value
) noexcept {
    return value
        ? taiyin_c_internal::to_cpp_split_jd(*value)
        : taiyin::SplitJulianDate(
            0, std::numeric_limits<double>::quiet_NaN());
}

template <typename Source>
void copy_result(
    const Source& source,
    taiyin_visibility_event_result* out
) noexcept {
    out->struct_size = sizeof(*out);
    out->altitude_state = source.altitude_state;
    out->crossing_direction = source.crossing_direction;
    taiyin_c_internal::from_cpp_split_jd(source.jd_ut, &out->jd_ut);
    out->residual_rad = source.residual_rad;
    out->min_residual_rad = source.min_residual_rad;
    out->max_residual_rad = source.max_residual_rad;
    taiyin_c_internal::from_cpp_split_jd(
        source.min_residual_jd_ut, &out->min_residual_jd_ut);
    taiyin_c_internal::from_cpp_split_jd(
        source.max_residual_jd_ut, &out->max_residual_jd_ut);
    out->sample_count = source.sample_count;
    out->refine_count = source.refine_count;
}

template <typename Result, typename Eval>
taiyin_status search_visibility(
    const taiyin_context* context,
    const taiyin_split_julian_date* start_jd,
    const taiyin_split_julian_date* end_jd,
    taiyin_visibility_event_result* out,
    taiyin_ephemeris_diagnostic* diagnostic,
    const Eval& eval
) {
    if (!context || !taiyin_c_internal::valid_split_jd(start_jd)
        || !taiyin_c_internal::valid_split_jd(end_jd)
        || !taiyin_c_internal::valid_struct(out)
        || !valid_diagnostic(diagnostic)) {
        return taiyin_c_internal::invalid_argument();
    }
    Result cpp_out;
    taiyin::runtime::EphemerisEvalDiagnostic cpp_diagnostic;
    const taiyin::Status status = eval(
        &cpp_out, diagnostic ? &cpp_diagnostic : 0);
    if (status == taiyin::TAIYIN_STATUS_OK) copy_result(cpp_out, out);
    taiyin_c_internal::from_cpp_diagnostic(cpp_diagnostic, diagnostic);
    return status;
}

bool convert_planet_flags(uint64_t flags, uint64_t* out) noexcept {
    const uint64_t known = TAIYIN_VISIBILITY_REFRACTION
        | TAIYIN_VISIBILITY_FIXED_DISC_SIZE
        | TAIYIN_VISIBILITY_NO_REFRACTION
        | TAIYIN_VISIBILITY_STRICT_METEOROLOGY;
    if (!out || (flags & ~known) != 0u
        || (flags & TAIYIN_VISIBILITY_FIXED_DISC_SIZE) != 0u) {
        return false;
    }
    *out = 0u;
    if ((flags & TAIYIN_VISIBILITY_REFRACTION) != 0u) {
        *out |= taiyin::runtime::TAIYIN_PLANET_VISIBILITY_FLAG_REFRACTION;
    }
    if ((flags & TAIYIN_VISIBILITY_NO_REFRACTION) != 0u) {
        *out |= taiyin::runtime::TAIYIN_PLANET_VISIBILITY_FLAG_NO_REFRACTION;
    }
    if ((flags & TAIYIN_VISIBILITY_STRICT_METEOROLOGY) != 0u) {
        *out |= taiyin::runtime::TAIYIN_PLANET_VISIBILITY_STRICT_METEOROLOGY;
    }
    return true;
}

bool convert_planet_transit_flags(uint64_t flags, uint64_t* out) noexcept {
    if (!out || (flags & UINT64_C(0xffffffff00000000)) != 0u) {
        return false;
    }
    *out = flags;
    return true;
}

bool convert_star_flags(uint64_t flags, uint64_t* out) noexcept {
    uint64_t planet_flags = 0u;
    if (!convert_planet_flags(flags, &planet_flags)) return false;
    *out = 0u;
    if ((flags & TAIYIN_VISIBILITY_REFRACTION) != 0u) {
        *out |= taiyin::runtime::TAIYIN_STAR_VISIBILITY_FLAG_REFRACTION;
    }
    if ((flags & TAIYIN_VISIBILITY_NO_REFRACTION) != 0u) {
        *out |= taiyin::runtime::TAIYIN_STAR_VISIBILITY_FLAG_NO_REFRACTION;
    }
    if ((flags & TAIYIN_VISIBILITY_STRICT_METEOROLOGY) != 0u) {
        *out |= taiyin::runtime::TAIYIN_STAR_VISIBILITY_STRICT_METEOROLOGY;
    }
    return true;
}

}  // namespace

extern "C" {

void TAIYIN_C_CALL taiyin_visibility_event_result_init(
    taiyin_visibility_event_result* value
) {
    if (!value) return;
    std::memset(value, 0, sizeof(*value));
    value->struct_size = sizeof(*value);
}

void TAIYIN_C_CALL taiyin_solar_rise_set_fast_result_init(
    taiyin_solar_rise_set_fast_result* value
) {
    if (!value) return;
    std::memset(value, 0, sizeof(*value));
    value->struct_size = sizeof(*value);
}

void TAIYIN_C_CALL taiyin_solar_transit_fast_result_init(
    taiyin_solar_transit_fast_result* value
) {
    if (!value) return;
    std::memset(value, 0, sizeof(*value));
    value->struct_size = sizeof(*value);
}

taiyin_status TAIYIN_C_CALL taiyin_search_moon_rise_set_ut(
    const taiyin_context* context,
    const taiyin_split_julian_date* start_jd_ut,
    const taiyin_split_julian_date* end_jd_ut,
    int32_t event_kind,
    int32_t limb_kind,
    uint64_t flags,
    taiyin_visibility_event_result* out,
    taiyin_ephemeris_diagnostic* diagnostic
) {
    return search_visibility<taiyin::runtime::MoonVisibilityEventResult>(
        context, start_jd_ut, end_jd_ut, out, diagnostic,
        [&](taiyin::runtime::MoonVisibilityEventResult* cpp_out,
            taiyin::runtime::EphemerisEvalDiagnostic* cpp_diagnostic) {
            return taiyin::runtime::search_moon_rise_set_ut(
                &context->value, cpp_date(start_jd_ut), cpp_date(end_jd_ut), event_kind, limb_kind,
                flags, cpp_out, cpp_diagnostic);
        });
}

taiyin_status TAIYIN_C_CALL taiyin_search_moon_rise_set_at_horizon_ut(
    const taiyin_context* context,
    const taiyin_split_julian_date* start_jd_ut,
    const taiyin_split_julian_date* end_jd_ut,
    int32_t event_kind,
    int32_t limb_kind,
    double horizon_altitude_rad,
    uint64_t flags,
    taiyin_visibility_event_result* out,
    taiyin_ephemeris_diagnostic* diagnostic
) {
    return search_visibility<taiyin::runtime::MoonVisibilityEventResult>(
        context, start_jd_ut, end_jd_ut, out, diagnostic,
        [&](taiyin::runtime::MoonVisibilityEventResult* cpp_out,
            taiyin::runtime::EphemerisEvalDiagnostic* cpp_diagnostic) {
            return taiyin::runtime::search_moon_rise_set_at_horizon_ut(
                &context->value, cpp_date(start_jd_ut), cpp_date(end_jd_ut), event_kind, limb_kind,
                horizon_altitude_rad, flags, cpp_out, cpp_diagnostic);
        });
}

taiyin_status TAIYIN_C_CALL taiyin_search_moon_transit_ut(
    const taiyin_context* context,
    const taiyin_split_julian_date* start_jd_ut,
    const taiyin_split_julian_date* end_jd_ut,
    int32_t event_kind,
    taiyin_visibility_event_result* out,
    taiyin_ephemeris_diagnostic* diagnostic
) {
    return search_visibility<taiyin::runtime::MoonVisibilityEventResult>(
        context, start_jd_ut, end_jd_ut, out, diagnostic,
        [&](taiyin::runtime::MoonVisibilityEventResult* cpp_out,
            taiyin::runtime::EphemerisEvalDiagnostic* cpp_diagnostic) {
            return taiyin::runtime::search_moon_transit_ut(
                &context->value, cpp_date(start_jd_ut), cpp_date(end_jd_ut), event_kind,
                cpp_out, cpp_diagnostic);
        });
}

taiyin_status TAIYIN_C_CALL taiyin_search_planet_rise_set_ut(
    const taiyin_context* context,
    int32_t body_id,
    const taiyin_split_julian_date* start_jd_ut,
    const taiyin_split_julian_date* end_jd_ut,
    int32_t event_kind,
    int32_t limb_kind,
    uint64_t flags,
    taiyin_visibility_event_result* out,
    taiyin_ephemeris_diagnostic* diagnostic
) {
    uint64_t cpp_flags = 0u;
    if (!convert_planet_flags(flags, &cpp_flags)) {
        return taiyin_c_internal::invalid_argument();
    }
    return search_visibility<taiyin::runtime::PlanetVisibilityEventResult>(
        context, start_jd_ut, end_jd_ut, out, diagnostic,
        [&](taiyin::runtime::PlanetVisibilityEventResult* cpp_out,
            taiyin::runtime::EphemerisEvalDiagnostic* cpp_diagnostic) {
            return taiyin::runtime::search_planet_rise_set_ut(
                &context->value, body_id, cpp_date(start_jd_ut), cpp_date(end_jd_ut), event_kind,
                limb_kind, cpp_flags, cpp_out, cpp_diagnostic);
        });
}

taiyin_status TAIYIN_C_CALL taiyin_search_planet_rise_set_at_horizon_ut(
    const taiyin_context* context,
    int32_t body_id,
    const taiyin_split_julian_date* start_jd_ut,
    const taiyin_split_julian_date* end_jd_ut,
    int32_t event_kind,
    int32_t limb_kind,
    double horizon_altitude_rad,
    uint64_t flags,
    taiyin_visibility_event_result* out,
    taiyin_ephemeris_diagnostic* diagnostic
) {
    uint64_t cpp_flags = 0u;
    if (!convert_planet_flags(flags, &cpp_flags)) {
        return taiyin_c_internal::invalid_argument();
    }
    return search_visibility<taiyin::runtime::PlanetVisibilityEventResult>(
        context, start_jd_ut, end_jd_ut, out, diagnostic,
        [&](taiyin::runtime::PlanetVisibilityEventResult* cpp_out,
            taiyin::runtime::EphemerisEvalDiagnostic* cpp_diagnostic) {
            return taiyin::runtime::search_planet_rise_set_at_horizon_ut(
                &context->value, body_id, cpp_date(start_jd_ut), cpp_date(end_jd_ut), event_kind,
                limb_kind, horizon_altitude_rad, cpp_flags, cpp_out,
                cpp_diagnostic);
        });
}

taiyin_status TAIYIN_C_CALL taiyin_search_planet_transit_ut(
    const taiyin_context* context,
    int32_t body_id,
    const taiyin_split_julian_date* start_jd_ut,
    const taiyin_split_julian_date* end_jd_ut,
    int32_t event_kind,
    uint64_t flags,
    taiyin_visibility_event_result* out,
    taiyin_ephemeris_diagnostic* diagnostic
) {
    uint64_t cpp_flags = 0u;
    if (!convert_planet_transit_flags(flags, &cpp_flags)) {
        return taiyin_c_internal::invalid_argument();
    }
    return search_visibility<taiyin::runtime::PlanetVisibilityEventResult>(
        context, start_jd_ut, end_jd_ut, out, diagnostic,
        [&](taiyin::runtime::PlanetVisibilityEventResult* cpp_out,
            taiyin::runtime::EphemerisEvalDiagnostic* cpp_diagnostic) {
            return taiyin::runtime::search_planet_transit_ut(
                &context->value, body_id, cpp_date(start_jd_ut), cpp_date(end_jd_ut), event_kind,
                cpp_flags, cpp_out, cpp_diagnostic);
        });
}

taiyin_status TAIYIN_C_CALL taiyin_search_solar_rise_set_ut(
    const taiyin_context* context,
    const taiyin_split_julian_date* start_jd_ut,
    const taiyin_split_julian_date* end_jd_ut,
    int32_t event_kind,
    int32_t limb_kind,
    uint64_t flags,
    taiyin_visibility_event_result* out,
    taiyin_ephemeris_diagnostic* diagnostic
) {
    return search_visibility<taiyin::runtime::SolarVisibilityEventResult>(
        context, start_jd_ut, end_jd_ut, out, diagnostic,
        [&](taiyin::runtime::SolarVisibilityEventResult* cpp_out,
            taiyin::runtime::EphemerisEvalDiagnostic* cpp_diagnostic) {
            return taiyin::runtime::search_solar_rise_set_ut(
                &context->value, cpp_date(start_jd_ut), cpp_date(end_jd_ut), event_kind, limb_kind,
                flags, cpp_out, cpp_diagnostic);
        });
}

taiyin_status TAIYIN_C_CALL taiyin_search_solar_rise_set_at_horizon_ut(
    const taiyin_context* context,
    const taiyin_split_julian_date* start_jd_ut,
    const taiyin_split_julian_date* end_jd_ut,
    int32_t event_kind,
    int32_t limb_kind,
    double horizon_altitude_rad,
    uint64_t flags,
    taiyin_visibility_event_result* out,
    taiyin_ephemeris_diagnostic* diagnostic
) {
    return search_visibility<taiyin::runtime::SolarVisibilityEventResult>(
        context, start_jd_ut, end_jd_ut, out, diagnostic,
        [&](taiyin::runtime::SolarVisibilityEventResult* cpp_out,
            taiyin::runtime::EphemerisEvalDiagnostic* cpp_diagnostic) {
            return taiyin::runtime::search_solar_rise_set_at_horizon_ut(
                &context->value, cpp_date(start_jd_ut), cpp_date(end_jd_ut), event_kind, limb_kind,
                horizon_altitude_rad, flags, cpp_out, cpp_diagnostic);
        });
}

taiyin_status TAIYIN_C_CALL taiyin_search_solar_twilight_ut(
    const taiyin_context* context,
    const taiyin_split_julian_date* start_jd_ut,
    const taiyin_split_julian_date* end_jd_ut,
    int32_t event_kind,
    int32_t twilight_kind,
    taiyin_visibility_event_result* out,
    taiyin_ephemeris_diagnostic* diagnostic
) {
    return search_visibility<taiyin::runtime::SolarVisibilityEventResult>(
        context, start_jd_ut, end_jd_ut, out, diagnostic,
        [&](taiyin::runtime::SolarVisibilityEventResult* cpp_out,
            taiyin::runtime::EphemerisEvalDiagnostic* cpp_diagnostic) {
            return taiyin::runtime::search_solar_twilight_ut(
                &context->value, cpp_date(start_jd_ut), cpp_date(end_jd_ut), event_kind,
                twilight_kind, cpp_out, cpp_diagnostic);
        });
}

taiyin_status TAIYIN_C_CALL taiyin_search_solar_transit_ut(
    const taiyin_context* context,
    const taiyin_split_julian_date* start_jd_ut,
    const taiyin_split_julian_date* end_jd_ut,
    int32_t event_kind,
    taiyin_visibility_event_result* out,
    taiyin_ephemeris_diagnostic* diagnostic
) {
    return search_visibility<taiyin::runtime::SolarVisibilityEventResult>(
        context, start_jd_ut, end_jd_ut, out, diagnostic,
        [&](taiyin::runtime::SolarVisibilityEventResult* cpp_out,
            taiyin::runtime::EphemerisEvalDiagnostic* cpp_diagnostic) {
            return taiyin::runtime::search_solar_transit_ut(
                &context->value, cpp_date(start_jd_ut), cpp_date(end_jd_ut), event_kind,
                cpp_out, cpp_diagnostic);
        });
}

taiyin_status TAIYIN_C_CALL taiyin_compute_solar_rise_set_fast_tt(
    const taiyin_context* context,
    const taiyin_split_julian_date* center_jd_tt,
    double longitude_deg,
    double latitude_deg,
    double height_m,
    int32_t limb_kind,
    double horizon_altitude_rad,
    uint64_t visibility_flags,
    taiyin_solar_rise_set_fast_result* out,
    taiyin_ephemeris_diagnostic* diagnostic
) {
    if (!context || !taiyin_c_internal::valid_split_jd(center_jd_tt)
        || !taiyin_c_internal::valid_struct(out)
        || !valid_diagnostic(diagnostic)) {
        return taiyin_c_internal::invalid_argument();
    }
    taiyin::runtime::SolarRiseSetFastResult cpp_out;
    taiyin::runtime::EphemerisEvalDiagnostic cpp_diagnostic;
    const taiyin::Status status =
        taiyin::runtime::compute_solar_rise_set_fast_tt(
            &context->value, cpp_date(center_jd_tt), longitude_deg, latitude_deg,
            height_m, limb_kind, horizon_altitude_rad, visibility_flags, &cpp_out,
            diagnostic ? &cpp_diagnostic : 0);
    if (status == taiyin::TAIYIN_STATUS_OK) {
        out->altitude_state = cpp_out.altitude_state;
        taiyin_c_internal::from_cpp_split_jd(
            cpp_out.rise_jd_tt, &out->rise_jd_tt);
        taiyin_c_internal::from_cpp_split_jd(
            cpp_out.set_jd_tt, &out->set_jd_tt);
        out->sample_count = cpp_out.sample_count;
        out->refine_count = cpp_out.refine_count;
    }
    taiyin_c_internal::from_cpp_diagnostic(cpp_diagnostic, diagnostic);
    return status;
}

taiyin_status TAIYIN_C_CALL taiyin_compute_solar_transit_fast_tt(
    const taiyin_context* context,
    const taiyin_split_julian_date* center_jd_tt,
    double longitude_deg,
    double latitude_deg,
    double height_m,
    taiyin_solar_transit_fast_result* out,
    taiyin_ephemeris_diagnostic* diagnostic
) {
    if (!context || !taiyin_c_internal::valid_split_jd(center_jd_tt)
        || !taiyin_c_internal::valid_struct(out)
        || !valid_diagnostic(diagnostic)) {
        return taiyin_c_internal::invalid_argument();
    }
    taiyin::runtime::SolarTransitFastResult cpp_out;
    taiyin::runtime::EphemerisEvalDiagnostic cpp_diagnostic;
    const taiyin::Status status =
        taiyin::runtime::compute_solar_transit_fast_tt(
            &context->value, cpp_date(center_jd_tt), longitude_deg, latitude_deg,
            height_m, &cpp_out, diagnostic ? &cpp_diagnostic : 0);
    if (status == taiyin::TAIYIN_STATUS_OK) {
        taiyin_c_internal::from_cpp_split_jd(
            cpp_out.transit_jd_tt, &out->transit_jd_tt);
        out->altitude_rad = cpp_out.altitude_rad;
        out->azimuth_rad = cpp_out.azimuth_rad;
        out->sample_count = cpp_out.sample_count;
        out->refine_count = cpp_out.refine_count;
    }
    taiyin_c_internal::from_cpp_diagnostic(cpp_diagnostic, diagnostic);
    return status;
}

taiyin_status TAIYIN_C_CALL taiyin_search_star_rise_set_ut(
    const taiyin_context* context,
    const char* star_key,
    const taiyin_split_julian_date* start_jd_ut,
    const taiyin_split_julian_date* end_jd_ut,
    int32_t event_kind,
    uint64_t flags,
    taiyin_visibility_event_result* out,
    taiyin_ephemeris_diagnostic* diagnostic
) {
    uint64_t cpp_flags = 0u;
    if (!star_key || star_key[0] == '\0'
        || !convert_star_flags(flags, &cpp_flags)) {
        return taiyin_c_internal::invalid_argument();
    }
    return search_visibility<taiyin::runtime::StarVisibilityEventResult>(
        context, start_jd_ut, end_jd_ut, out, diagnostic,
        [&](taiyin::runtime::StarVisibilityEventResult* cpp_out,
            taiyin::runtime::EphemerisEvalDiagnostic* cpp_diagnostic) {
            return taiyin::runtime::search_star_rise_set_ut(
                &context->value, star_key, cpp_date(start_jd_ut), cpp_date(end_jd_ut), event_kind,
                cpp_flags, cpp_out, cpp_diagnostic);
        });
}

taiyin_status TAIYIN_C_CALL taiyin_search_star_rise_set_at_horizon_ut(
    const taiyin_context* context,
    const char* star_key,
    const taiyin_split_julian_date* start_jd_ut,
    const taiyin_split_julian_date* end_jd_ut,
    int32_t event_kind,
    double horizon_altitude_rad,
    uint64_t flags,
    taiyin_visibility_event_result* out,
    taiyin_ephemeris_diagnostic* diagnostic
) {
    uint64_t cpp_flags = 0u;
    if (!star_key || star_key[0] == '\0'
        || !convert_star_flags(flags, &cpp_flags)) {
        return taiyin_c_internal::invalid_argument();
    }
    return search_visibility<taiyin::runtime::StarVisibilityEventResult>(
        context, start_jd_ut, end_jd_ut, out, diagnostic,
        [&](taiyin::runtime::StarVisibilityEventResult* cpp_out,
            taiyin::runtime::EphemerisEvalDiagnostic* cpp_diagnostic) {
            return taiyin::runtime::search_star_rise_set_at_horizon_ut(
                &context->value, star_key, cpp_date(start_jd_ut), cpp_date(end_jd_ut), event_kind,
                horizon_altitude_rad, cpp_flags, cpp_out, cpp_diagnostic);
        });
}

taiyin_status TAIYIN_C_CALL taiyin_search_star_transit_ut(
    const taiyin_context* context,
    const char* star_key,
    const taiyin_split_julian_date* start_jd_ut,
    const taiyin_split_julian_date* end_jd_ut,
    int32_t event_kind,
    taiyin_visibility_event_result* out,
    taiyin_ephemeris_diagnostic* diagnostic
) {
    if (!star_key || star_key[0] == '\0') {
        return taiyin_c_internal::invalid_argument();
    }
    return search_visibility<taiyin::runtime::StarVisibilityEventResult>(
        context, start_jd_ut, end_jd_ut, out, diagnostic,
        [&](taiyin::runtime::StarVisibilityEventResult* cpp_out,
            taiyin::runtime::EphemerisEvalDiagnostic* cpp_diagnostic) {
            return taiyin::runtime::search_star_transit_ut(
                &context->value, star_key, cpp_date(start_jd_ut), cpp_date(end_jd_ut), event_kind,
                cpp_out, cpp_diagnostic);
        });
}

}  // extern "C"
