#include "taiyin/c/events.h"

#include "c_api_internal.h"
#include "taiyin/runtime/event_search.h"

#include <cstring>
#include <limits>
#include <new>
#include <vector>

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

void copy_phenomena(
    const taiyin::runtime::BodyPhenomena& source,
    taiyin_body_phenomena* out
) noexcept {
    out->struct_size = sizeof(*out);
    out->phase_angle_rad = source.phase_angle_rad;
    out->illuminated_fraction = source.illuminated_fraction;
    out->solar_elongation_rad = source.solar_elongation_rad;
    out->apparent_diameter_rad = source.apparent_diameter_rad;
    out->apparent_magnitude = source.apparent_magnitude;
    out->horizontal_parallax_rad = source.horizontal_parallax_rad;
}

void copy_elongation(
    const taiyin::runtime::GreatestElongationSearchResult& source,
    taiyin_greatest_elongation_result* out
) noexcept {
    out->struct_size = sizeof(*out);
    taiyin_c_internal::from_cpp_split_jd(source.jd_ut, &out->jd_ut);
    out->elongation_rad = source.elongation_rad;
    out->relative_longitude_rad = source.relative_longitude_rad;
    out->kind = source.kind;
    out->body_id = source.body_id;
    out->iteration_count = source.iteration_count;
    out->evaluation_count = source.evaluation_count;
    copy_phenomena(source.phenomena, &out->phenomena);
}

void copy_separation(
    const taiyin::runtime::AngularSeparationSearchResult& source,
    taiyin_angular_separation_result* out
) noexcept {
    out->struct_size = sizeof(*out);
    taiyin_c_internal::from_cpp_split_jd(source.jd, &out->jd);
    out->separation_rad = source.separation_rad;
    out->separation_rate_rad_per_day = source.separation_rate_rad_per_day;
    out->body_a_id = source.body_a_id;
    out->body_b_id = source.body_b_id;
    out->iteration_count = source.iteration_count;
    out->evaluation_count = source.evaluation_count;
}

void copy_body_star_separation(
    const taiyin::runtime::BodyStarAngularSeparationSearchResult& source,
    taiyin_body_star_angular_separation_result* out
) noexcept {
    out->struct_size = sizeof(*out);
    taiyin_c_internal::from_cpp_split_jd(source.jd, &out->jd);
    out->separation_rad = source.separation_rad;
    out->separation_rate_rad_per_day = source.separation_rate_rad_per_day;
    out->body_id = source.body_id;
    out->iteration_count = source.iteration_count;
    out->evaluation_count = source.evaluation_count;
}

void copy_transit(
    const taiyin::runtime::SolarTransitSearchResult& source,
    taiyin_solar_transit_result* out
) noexcept {
    out->struct_size = sizeof(*out);
    out->body_id = source.body_id;
    out->kind = source.kind;
    taiyin_c_internal::from_cpp_split_jd(
        source.greatest_jd_ut, &out->greatest_jd_ut);
    out->minimum_separation_rad = source.minimum_separation_rad;
    out->sun_radius_rad = source.sun_radius_rad;
    out->body_radius_rad = source.body_radius_rad;
    taiyin_c_internal::from_cpp_split_jd(source.t1_jd_ut, &out->t1_jd_ut);
    taiyin_c_internal::from_cpp_split_jd(source.t2_jd_ut, &out->t2_jd_ut);
    taiyin_c_internal::from_cpp_split_jd(source.t3_jd_ut, &out->t3_jd_ut);
    taiyin_c_internal::from_cpp_split_jd(source.t4_jd_ut, &out->t4_jd_ut);
    out->iteration_count = source.iteration_count;
    out->evaluation_count = source.evaluation_count;
}

taiyin::runtime::SolarTransitSearchResult to_cpp_transit(
    const taiyin_solar_transit_result& source
) noexcept {
    taiyin::runtime::SolarTransitSearchResult out;
    out.body_id = source.body_id;
    out.kind = source.kind;
    out.greatest_jd_ut =
        taiyin_c_internal::to_cpp_split_jd(source.greatest_jd_ut);
    out.minimum_separation_rad = source.minimum_separation_rad;
    out.sun_radius_rad = source.sun_radius_rad;
    out.body_radius_rad = source.body_radius_rad;
    out.t1_jd_ut = taiyin_c_internal::to_cpp_split_jd(source.t1_jd_ut);
    out.t2_jd_ut = taiyin_c_internal::to_cpp_split_jd(source.t2_jd_ut);
    out.t3_jd_ut = taiyin_c_internal::to_cpp_split_jd(source.t3_jd_ut);
    out.t4_jd_ut = taiyin_c_internal::to_cpp_split_jd(source.t4_jd_ut);
    out.iteration_count = source.iteration_count;
    out.evaluation_count = source.evaluation_count;
    return out;
}

void copy_local_transit(
    const taiyin::runtime::LocalSolarTransitSearchResult& source,
    taiyin_local_solar_transit_result* out
) noexcept {
    out->struct_size = sizeof(*out);
    copy_transit(source.global, &out->global);
    copy_transit(source.topocentric, &out->topocentric);
    out->visibility_flags = source.visibility_flags;
    for (size_t i = 0; i < TAIYIN_SOLAR_TRANSIT_CONTACT_SLOT_COUNT; ++i) {
        out->contact_sun_altitude_deg[i] = source.contact_sun_altitude_deg[i];
        out->contact_sun_azimuth_deg[i] = source.contact_sun_azimuth_deg[i];
    }
    taiyin_c_internal::from_cpp_split_jd(
        source.sunrise_jd_ut, &out->sunrise_jd_ut);
    taiyin_c_internal::from_cpp_split_jd(
        source.sunset_jd_ut, &out->sunset_jd_ut);
}

template <typename Eval>
taiyin_status scalar_search(
    const taiyin_context* context,
    const taiyin_split_julian_date* required_jd,
    taiyin_split_julian_date* out_jd,
    taiyin_ephemeris_diagnostic* diagnostic,
    const Eval& eval
) {
    if (!context || !taiyin_c_internal::valid_split_jd(required_jd)
        || !out_jd || !valid_diagnostic(diagnostic)) {
        return taiyin_c_internal::invalid_argument();
    }
    taiyin::SplitJulianDate cpp_jd;
    taiyin::runtime::EphemerisEvalDiagnostic cpp_diagnostic;
    const taiyin::Status status =
        eval(&cpp_jd, diagnostic ? &cpp_diagnostic : 0);
    if (status == taiyin::TAIYIN_STATUS_OK) {
        taiyin_c_internal::from_cpp_split_jd(cpp_jd, out_jd);
    }
    taiyin_c_internal::from_cpp_diagnostic(cpp_diagnostic, diagnostic);
    return status;
}

template <typename Eval>
taiyin_status array_search(
    const taiyin_context* context,
    const taiyin_split_julian_date* start_jd,
    const taiyin_split_julian_date* end_jd,
    taiyin_split_julian_date* out_primary,
    double* out_secondary,
    size_t capacity,
    size_t* out_count,
    taiyin_ephemeris_diagnostic* diagnostic,
    const Eval& eval
) {
    if (!context || !taiyin_c_internal::valid_split_jd(start_jd)
        || !taiyin_c_internal::valid_split_jd(end_jd)
        || !out_count || !valid_diagnostic(diagnostic)
        || (capacity != 0 && !out_primary)) {
        return taiyin_c_internal::invalid_argument();
    }
    try {
        std::vector<taiyin::SplitJulianDate> cpp_primary(capacity);
        taiyin::runtime::EphemerisEvalDiagnostic cpp_diagnostic;
        const taiyin::Status status = eval(
            capacity ? cpp_primary.data() : nullptr, out_secondary, capacity,
            out_count, diagnostic ? &cpp_diagnostic : 0);
        if (status == taiyin::TAIYIN_STATUS_OK) {
            const size_t copied = *out_count < capacity ? *out_count : capacity;
            for (size_t i = 0; i < copied; ++i) {
                taiyin_c_internal::from_cpp_split_jd(
                    cpp_primary[i], &out_primary[i]);
            }
        }
        taiyin_c_internal::from_cpp_diagnostic(cpp_diagnostic, diagnostic);
        return status;
    } catch (const std::bad_alloc&) {
        *out_count = 0;
        return taiyin_c_internal::out_of_memory();
    } catch (...) {
        *out_count = 0;
        return TAIYIN_ERROR_INTERNAL;
    }
}

}  // namespace

extern "C" {

void TAIYIN_C_CALL taiyin_greatest_elongation_result_init(
    taiyin_greatest_elongation_result* value
) {
    if (!value) return;
    std::memset(value, 0, sizeof(*value));
    value->struct_size = sizeof(*value);
    value->phenomena.struct_size = sizeof(value->phenomena);
}

void TAIYIN_C_CALL taiyin_angular_separation_result_init(
    taiyin_angular_separation_result* value
) {
    if (!value) return;
    std::memset(value, 0, sizeof(*value));
    value->struct_size = sizeof(*value);
}

void TAIYIN_C_CALL taiyin_body_star_angular_separation_result_init(
    taiyin_body_star_angular_separation_result* value
) {
    if (!value) return;
    std::memset(value, 0, sizeof(*value));
    value->struct_size = sizeof(*value);
}

void TAIYIN_C_CALL taiyin_solar_transit_result_init(
    taiyin_solar_transit_result* value
) {
    if (!value) return;
    std::memset(value, 0, sizeof(*value));
    value->struct_size = sizeof(*value);
}

void TAIYIN_C_CALL taiyin_local_solar_transit_result_init(
    taiyin_local_solar_transit_result* value
) {
    if (!value) return;
    std::memset(value, 0, sizeof(*value));
    value->struct_size = sizeof(*value);
    value->global.struct_size = sizeof(value->global);
    value->topocentric.struct_size = sizeof(value->topocentric);
}

double TAIYIN_C_CALL taiyin_recommended_longitude_search_step_days(
    int32_t body_id
) {
    return taiyin::runtime::recommended_longitude_search_step_days(body_id);
}

double TAIYIN_C_CALL taiyin_recommended_aspect_search_step_days(
    int32_t body_a_id,
    int32_t body_b_id
) {
    return taiyin::runtime::recommended_aspect_search_step_days(
        body_a_id, body_b_id);
}

taiyin_status TAIYIN_C_CALL taiyin_search_solar_longitude_ut(
    const taiyin_context* context,
    double target_longitude_rad,
    const taiyin_split_julian_date* estimate_jd_ut,
    uint64_t flags,
    taiyin_split_julian_date* out_jd_ut,
    taiyin_ephemeris_diagnostic* diagnostic
) {
    return scalar_search(
        context, estimate_jd_ut, out_jd_ut, diagnostic,
        [&](taiyin::SplitJulianDate* cpp_out_jd,
            taiyin::runtime::EphemerisEvalDiagnostic* cpp_diagnostic) {
            return taiyin::runtime::search_solar_longitude_ut(
                &context->value, target_longitude_rad,
                cpp_date(estimate_jd_ut), flags, cpp_out_jd, cpp_diagnostic);
        });
}

taiyin_status TAIYIN_C_CALL taiyin_search_solar_longitude_tt(
    const taiyin_context* context,
    double target_longitude_rad,
    const taiyin_split_julian_date* estimate_jd_tt,
    uint64_t flags,
    taiyin_split_julian_date* out_jd_tt,
    taiyin_ephemeris_diagnostic* diagnostic
) {
    return scalar_search(
        context, estimate_jd_tt, out_jd_tt, diagnostic,
        [&](taiyin::SplitJulianDate* cpp_out_jd,
            taiyin::runtime::EphemerisEvalDiagnostic* cpp_diagnostic) {
            return taiyin::runtime::search_solar_longitude_tt(
                &context->value, target_longitude_rad,
                cpp_date(estimate_jd_tt), flags, cpp_out_jd, cpp_diagnostic);
        });
}

taiyin_status TAIYIN_C_CALL taiyin_search_moon_longitude_ut(
    const taiyin_context* context,
    double target_longitude_rad,
    const taiyin_split_julian_date* estimate_jd_ut,
    uint64_t flags,
    taiyin_split_julian_date* out_jd_ut,
    taiyin_ephemeris_diagnostic* diagnostic
) {
    return scalar_search(
        context, estimate_jd_ut, out_jd_ut, diagnostic,
        [&](taiyin::SplitJulianDate* cpp_out_jd,
            taiyin::runtime::EphemerisEvalDiagnostic* cpp_diagnostic) {
            return taiyin::runtime::search_moon_longitude_ut(
                &context->value, target_longitude_rad,
                cpp_date(estimate_jd_ut), flags, cpp_out_jd, cpp_diagnostic);
        });
}

taiyin_status TAIYIN_C_CALL taiyin_search_moon_longitude_tt(
    const taiyin_context* context,
    double target_longitude_rad,
    const taiyin_split_julian_date* estimate_jd_tt,
    uint64_t flags,
    taiyin_split_julian_date* out_jd_tt,
    taiyin_ephemeris_diagnostic* diagnostic
) {
    return scalar_search(
        context, estimate_jd_tt, out_jd_tt, diagnostic,
        [&](taiyin::SplitJulianDate* cpp_out_jd,
            taiyin::runtime::EphemerisEvalDiagnostic* cpp_diagnostic) {
            return taiyin::runtime::search_moon_longitude_tt(
                &context->value, target_longitude_rad,
                cpp_date(estimate_jd_tt), flags, cpp_out_jd, cpp_diagnostic);
        });
}

taiyin_status TAIYIN_C_CALL taiyin_search_body_longitude_crossings_ut(
    const taiyin_context* context,
    int32_t body_id,
    double target_longitude_rad,
    const taiyin_split_julian_date* start_jd_ut,
    const taiyin_split_julian_date* end_jd_ut,
    double max_step_days,
    uint64_t flags,
    taiyin_split_julian_date* out_jd_ut,
    size_t capacity,
    size_t* out_count,
    taiyin_ephemeris_diagnostic* diagnostic
) {
    return array_search(
        context, start_jd_ut, end_jd_ut, out_jd_ut, 0, capacity, out_count, diagnostic,
        [&](taiyin::SplitJulianDate* primary, double*, size_t cap, size_t* count,
            taiyin::runtime::EphemerisEvalDiagnostic* cpp_diagnostic) {
            return taiyin::runtime::search_body_longitude_crossings_ut(
                &context->value, body_id, target_longitude_rad, cpp_date(start_jd_ut),
                cpp_date(end_jd_ut), max_step_days, flags, primary, cap, count,
                cpp_diagnostic);
        });
}

taiyin_status TAIYIN_C_CALL taiyin_search_body_longitude_crossings_tt(
    const taiyin_context* context,
    int32_t body_id,
    double target_longitude_rad,
    const taiyin_split_julian_date* start_jd_tt,
    const taiyin_split_julian_date* end_jd_tt,
    double max_step_days,
    uint64_t flags,
    taiyin_split_julian_date* out_jd_tt,
    size_t capacity,
    size_t* out_count,
    taiyin_ephemeris_diagnostic* diagnostic
) {
    return array_search(
        context, start_jd_tt, end_jd_tt, out_jd_tt, nullptr, capacity, out_count, diagnostic,
        [&](taiyin::SplitJulianDate* primary, double*, size_t cap, size_t* count,
            taiyin::runtime::EphemerisEvalDiagnostic* cpp_diagnostic) {
            return taiyin::runtime::search_body_longitude_crossings_tt(
                &context->value, body_id, target_longitude_rad, cpp_date(start_jd_tt),
                cpp_date(end_jd_tt), max_step_days, flags, primary, cap, count,
                cpp_diagnostic);
        });
}

taiyin_status TAIYIN_C_CALL taiyin_search_body_longitude_stations_ut(
    const taiyin_context* context,
    int32_t body_id,
    const taiyin_split_julian_date* start_jd_ut,
    const taiyin_split_julian_date* end_jd_ut,
    double max_step_days,
    uint64_t flags,
    taiyin_split_julian_date* out_jd_ut,
    double* out_longitude_rad,
    size_t capacity,
    size_t* out_count,
    taiyin_ephemeris_diagnostic* diagnostic
) {
    if (capacity != 0 && !out_longitude_rad) {
        return taiyin_c_internal::invalid_argument();
    }
    return array_search(
        context, start_jd_ut, end_jd_ut, out_jd_ut, out_longitude_rad,
        capacity, out_count, diagnostic,
        [&](taiyin::SplitJulianDate* primary, double* secondary, size_t cap, size_t* count,
            taiyin::runtime::EphemerisEvalDiagnostic* cpp_diagnostic) {
            return taiyin::runtime::search_body_longitude_stations_ut(
                &context->value, body_id, cpp_date(start_jd_ut), cpp_date(end_jd_ut),
                max_step_days, flags, primary, secondary, cap, count,
                cpp_diagnostic);
        });
}

taiyin_status TAIYIN_C_CALL taiyin_search_body_longitude_stations_tt(
    const taiyin_context* context,
    int32_t body_id,
    const taiyin_split_julian_date* start_jd_tt,
    const taiyin_split_julian_date* end_jd_tt,
    double max_step_days,
    uint64_t flags,
    taiyin_split_julian_date* out_jd_tt,
    double* out_longitude_rad,
    size_t capacity,
    size_t* out_count,
    taiyin_ephemeris_diagnostic* diagnostic
) {
    if (capacity != 0 && !out_longitude_rad) {
        return taiyin_c_internal::invalid_argument();
    }
    return array_search(
        context, start_jd_tt, end_jd_tt, out_jd_tt, out_longitude_rad,
        capacity, out_count, diagnostic,
        [&](taiyin::SplitJulianDate* primary, double* secondary, size_t cap, size_t* count,
            taiyin::runtime::EphemerisEvalDiagnostic* cpp_diagnostic) {
            return taiyin::runtime::search_body_longitude_stations_tt(
                &context->value, body_id, cpp_date(start_jd_tt), cpp_date(end_jd_tt),
                max_step_days, flags, primary, secondary, cap, count,
                cpp_diagnostic);
        });
}

taiyin_status TAIYIN_C_CALL taiyin_search_body_aspect_crossings_ut(
    const taiyin_context* context,
    int32_t body_a_id,
    int32_t body_b_id,
    double aspect_rad,
    const taiyin_split_julian_date* start_jd_ut,
    const taiyin_split_julian_date* end_jd_ut,
    double max_step_days,
    uint64_t flags,
    taiyin_split_julian_date* out_jd_ut,
    size_t capacity,
    size_t* out_count,
    taiyin_ephemeris_diagnostic* diagnostic
) {
    return array_search(
        context, start_jd_ut, end_jd_ut, out_jd_ut, 0, capacity, out_count, diagnostic,
        [&](taiyin::SplitJulianDate* primary, double*, size_t cap, size_t* count,
            taiyin::runtime::EphemerisEvalDiagnostic* cpp_diagnostic) {
            return taiyin::runtime::search_body_aspect_crossings_ut(
                &context->value, body_a_id, body_b_id, aspect_rad, cpp_date(start_jd_ut),
                cpp_date(end_jd_ut), max_step_days, flags, primary, cap, count,
                cpp_diagnostic);
        });
}

taiyin_status TAIYIN_C_CALL taiyin_search_body_aspect_crossings_tt(
    const taiyin_context* context,
    int32_t body_a_id,
    int32_t body_b_id,
    double aspect_rad,
    const taiyin_split_julian_date* start_jd_tt,
    const taiyin_split_julian_date* end_jd_tt,
    double max_step_days,
    uint64_t flags,
    taiyin_split_julian_date* out_jd_tt,
    size_t capacity,
    size_t* out_count,
    taiyin_ephemeris_diagnostic* diagnostic
) {
    return array_search(
        context, start_jd_tt, end_jd_tt, out_jd_tt, nullptr, capacity, out_count, diagnostic,
        [&](taiyin::SplitJulianDate* primary, double*, size_t cap, size_t* count,
            taiyin::runtime::EphemerisEvalDiagnostic* cpp_diagnostic) {
            return taiyin::runtime::search_body_aspect_crossings_tt(
                &context->value, body_a_id, body_b_id, aspect_rad, cpp_date(start_jd_tt),
                cpp_date(end_jd_tt), max_step_days, flags, primary, cap, count,
                cpp_diagnostic);
        });
}

taiyin_status TAIYIN_C_CALL taiyin_search_body_exact_aspects_ut(
    const taiyin_context* context,
    int32_t body_a_id,
    int32_t body_b_id,
    const double* aspect_separations_rad,
    size_t aspect_count,
    const taiyin_split_julian_date* start_jd_ut,
    const taiyin_split_julian_date* end_jd_ut,
    double max_step_days,
    uint64_t flags,
    taiyin_split_julian_date* out_jd_ut,
    double* out_target_aspect_rad,
    size_t capacity,
    size_t* out_count,
    taiyin_ephemeris_diagnostic* diagnostic
) {
    if ((!aspect_separations_rad && aspect_count != 0)
        || (capacity != 0 && !out_target_aspect_rad)) {
        return taiyin_c_internal::invalid_argument();
    }
    return array_search(
        context, start_jd_ut, end_jd_ut, out_jd_ut, out_target_aspect_rad,
        capacity, out_count,
        diagnostic,
        [&](taiyin::SplitJulianDate* primary, double* secondary, size_t cap, size_t* count,
            taiyin::runtime::EphemerisEvalDiagnostic* cpp_diagnostic) {
            return taiyin::runtime::search_body_exact_aspects_ut(
                &context->value, body_a_id, body_b_id, aspect_separations_rad,
                aspect_count, cpp_date(start_jd_ut), cpp_date(end_jd_ut), max_step_days, flags,
                primary, secondary, cap, count, cpp_diagnostic);
        });
}

taiyin_status TAIYIN_C_CALL taiyin_search_body_exact_aspects_tt(
    const taiyin_context* context,
    int32_t body_a_id,
    int32_t body_b_id,
    const double* aspect_separations_rad,
    size_t aspect_count,
    const taiyin_split_julian_date* start_jd_tt,
    const taiyin_split_julian_date* end_jd_tt,
    double max_step_days,
    uint64_t flags,
    taiyin_split_julian_date* out_jd_tt,
    double* out_target_aspect_rad,
    size_t capacity,
    size_t* out_count,
    taiyin_ephemeris_diagnostic* diagnostic
) {
    if ((!aspect_separations_rad && aspect_count != 0)
        || (capacity != 0 && !out_target_aspect_rad)) {
        return taiyin_c_internal::invalid_argument();
    }
    return array_search(
        context, start_jd_tt, end_jd_tt, out_jd_tt, out_target_aspect_rad,
        capacity, out_count,
        diagnostic,
        [&](taiyin::SplitJulianDate* primary, double* secondary, size_t cap, size_t* count,
            taiyin::runtime::EphemerisEvalDiagnostic* cpp_diagnostic) {
            return taiyin::runtime::search_body_exact_aspects_tt(
                &context->value, body_a_id, body_b_id, aspect_separations_rad,
                aspect_count, cpp_date(start_jd_tt), cpp_date(end_jd_tt), max_step_days, flags,
                primary, secondary, cap, count, cpp_diagnostic);
        });
}

taiyin_status TAIYIN_C_CALL taiyin_search_greatest_elongation_ut(
    const taiyin_context* context,
    int32_t body_id,
    const taiyin_split_julian_date* start_jd_ut,
    const taiyin_split_julian_date* end_jd_ut,
    uint64_t flags,
    taiyin_greatest_elongation_result* out,
    taiyin_ephemeris_diagnostic* diagnostic
) {
    if (!context || !taiyin_c_internal::valid_split_jd(start_jd_ut)
        || !taiyin_c_internal::valid_split_jd(end_jd_ut)
        || !taiyin_c_internal::valid_struct(out)
        || !valid_diagnostic(diagnostic)) {
        return taiyin_c_internal::invalid_argument();
    }
    taiyin::runtime::GreatestElongationSearchResult cpp_out;
    taiyin::runtime::EphemerisEvalDiagnostic cpp_diagnostic;
    const taiyin::Status status =
        taiyin::runtime::search_greatest_elongation_ut(
            &context->value, body_id, cpp_date(start_jd_ut), cpp_date(end_jd_ut), flags,
            &cpp_out, diagnostic ? &cpp_diagnostic : 0);
    if (status == taiyin::TAIYIN_STATUS_OK) copy_elongation(cpp_out, out);
    taiyin_c_internal::from_cpp_diagnostic(cpp_diagnostic, diagnostic);
    return status;
}

taiyin_status TAIYIN_C_CALL taiyin_search_minimum_angular_separation_ut(
    const taiyin_context* context,
    int32_t body_a_id,
    int32_t body_b_id,
    const taiyin_split_julian_date* start_jd_ut,
    const taiyin_split_julian_date* end_jd_ut,
    double max_step_days,
    uint64_t flags,
    taiyin_angular_separation_result* out,
    taiyin_ephemeris_diagnostic* diagnostic
) {
    if (!context || !taiyin_c_internal::valid_split_jd(start_jd_ut)
        || !taiyin_c_internal::valid_split_jd(end_jd_ut)
        || !taiyin_c_internal::valid_struct(out)
        || !valid_diagnostic(diagnostic)) {
        return taiyin_c_internal::invalid_argument();
    }
    taiyin::runtime::AngularSeparationSearchResult cpp_out;
    taiyin::runtime::EphemerisEvalDiagnostic cpp_diagnostic;
    const taiyin::Status status =
        taiyin::runtime::search_minimum_angular_separation_ut(
            &context->value, body_a_id, body_b_id, cpp_date(start_jd_ut), cpp_date(end_jd_ut),
            max_step_days, flags, &cpp_out,
            diagnostic ? &cpp_diagnostic : 0);
    if (status == taiyin::TAIYIN_STATUS_OK) copy_separation(cpp_out, out);
    taiyin_c_internal::from_cpp_diagnostic(cpp_diagnostic, diagnostic);
    return status;
}

taiyin_status TAIYIN_C_CALL taiyin_search_minimum_angular_separation_tt(
    const taiyin_context* context,
    int32_t body_a_id,
    int32_t body_b_id,
    const taiyin_split_julian_date* start_jd_tt,
    const taiyin_split_julian_date* end_jd_tt,
    double max_step_days,
    uint64_t flags,
    taiyin_angular_separation_result* out,
    taiyin_ephemeris_diagnostic* diagnostic
) {
    if (!context || !taiyin_c_internal::valid_split_jd(start_jd_tt)
        || !taiyin_c_internal::valid_split_jd(end_jd_tt)
        || !taiyin_c_internal::valid_struct(out)
        || !valid_diagnostic(diagnostic)) {
        return taiyin_c_internal::invalid_argument();
    }
    taiyin::runtime::AngularSeparationSearchResult cpp_out;
    taiyin::runtime::EphemerisEvalDiagnostic cpp_diagnostic;
    const taiyin::Status status =
        taiyin::runtime::search_minimum_angular_separation_tt(
            &context->value, body_a_id, body_b_id, cpp_date(start_jd_tt), cpp_date(end_jd_tt),
            max_step_days, flags, &cpp_out,
            diagnostic ? &cpp_diagnostic : 0);
    if (status == taiyin::TAIYIN_STATUS_OK) copy_separation(cpp_out, out);
    taiyin_c_internal::from_cpp_diagnostic(cpp_diagnostic, diagnostic);
    return status;
}

taiyin_status TAIYIN_C_CALL
taiyin_search_minimum_body_star_angular_separation_ut(
    const taiyin_context* context,
    int32_t body_id,
    const char* star_key,
    const taiyin_split_julian_date* start_jd_ut,
    const taiyin_split_julian_date* end_jd_ut,
    double max_step_days,
    uint64_t flags,
    taiyin_body_star_angular_separation_result* out,
    taiyin_ephemeris_diagnostic* diagnostic
) {
    if (!context || !star_key || star_key[0] == '\0'
        || !taiyin_c_internal::valid_split_jd(start_jd_ut)
        || !taiyin_c_internal::valid_split_jd(end_jd_ut)
        || !taiyin_c_internal::valid_struct(out)
        || !valid_diagnostic(diagnostic)) {
        return taiyin_c_internal::invalid_argument();
    }
    taiyin::runtime::BodyStarAngularSeparationSearchResult cpp_out;
    taiyin::runtime::EphemerisEvalDiagnostic cpp_diagnostic;
    const taiyin::Status status =
        taiyin::runtime::search_minimum_body_star_angular_separation_ut(
            &context->value, body_id, star_key,
            cpp_date(start_jd_ut), cpp_date(end_jd_ut), max_step_days, flags,
            &cpp_out, diagnostic ? &cpp_diagnostic : 0);
    if (status == taiyin::TAIYIN_STATUS_OK) {
        copy_body_star_separation(cpp_out, out);
    }
    taiyin_c_internal::from_cpp_diagnostic(cpp_diagnostic, diagnostic);
    return status;
}

taiyin_status TAIYIN_C_CALL
taiyin_search_minimum_body_star_angular_separation_tt(
    const taiyin_context* context,
    int32_t body_id,
    const char* star_key,
    const taiyin_split_julian_date* start_jd_tt,
    const taiyin_split_julian_date* end_jd_tt,
    double max_step_days,
    uint64_t flags,
    taiyin_body_star_angular_separation_result* out,
    taiyin_ephemeris_diagnostic* diagnostic
) {
    if (!context || !star_key || star_key[0] == '\0'
        || !taiyin_c_internal::valid_split_jd(start_jd_tt)
        || !taiyin_c_internal::valid_split_jd(end_jd_tt)
        || !taiyin_c_internal::valid_struct(out)
        || !valid_diagnostic(diagnostic)) {
        return taiyin_c_internal::invalid_argument();
    }
    taiyin::runtime::BodyStarAngularSeparationSearchResult cpp_out;
    taiyin::runtime::EphemerisEvalDiagnostic cpp_diagnostic;
    const taiyin::Status status =
        taiyin::runtime::search_minimum_body_star_angular_separation_tt(
            &context->value, body_id, star_key,
            cpp_date(start_jd_tt), cpp_date(end_jd_tt), max_step_days, flags,
            &cpp_out, diagnostic ? &cpp_diagnostic : 0);
    if (status == taiyin::TAIYIN_STATUS_OK) {
        copy_body_star_separation(cpp_out, out);
    }
    taiyin_c_internal::from_cpp_diagnostic(cpp_diagnostic, diagnostic);
    return status;
}

taiyin_status TAIYIN_C_CALL taiyin_search_next_solar_transit_ut(
    const taiyin_context* context,
    int32_t body_id,
    const taiyin_split_julian_date* jd_start_ut,
    uint64_t flags,
    taiyin_solar_transit_result* out,
    taiyin_ephemeris_diagnostic* diagnostic
) {
    if (!context || !taiyin_c_internal::valid_split_jd(jd_start_ut)
        || !taiyin_c_internal::valid_struct(out)
        || !valid_diagnostic(diagnostic)) {
        return taiyin_c_internal::invalid_argument();
    }
    taiyin::runtime::SolarTransitSearchResult cpp_out;
    taiyin::runtime::EphemerisEvalDiagnostic cpp_diagnostic;
    const taiyin::Status status = taiyin::runtime::search_next_solar_transit_ut(
        &context->value, body_id, cpp_date(jd_start_ut), flags, &cpp_out,
        diagnostic ? &cpp_diagnostic : 0);
    if (status == taiyin::TAIYIN_STATUS_OK) copy_transit(cpp_out, out);
    taiyin_c_internal::from_cpp_diagnostic(cpp_diagnostic, diagnostic);
    return status;
}

taiyin_status TAIYIN_C_CALL taiyin_compute_local_solar_transit_ut(
    const taiyin_context* context,
    const taiyin_solar_transit_result* global_transit,
    double longitude_deg,
    double latitude_deg,
    double height_m,
    uint64_t flags,
    taiyin_local_solar_transit_result* out,
    taiyin_ephemeris_diagnostic* diagnostic
) {
    if (!context || !taiyin_c_internal::valid_struct(global_transit)
        || !taiyin_c_internal::valid_struct(out)
        || !valid_diagnostic(diagnostic)) {
        return taiyin_c_internal::invalid_argument();
    }
    const taiyin::runtime::SolarTransitSearchResult cpp_global =
        to_cpp_transit(*global_transit);
    taiyin::runtime::LocalSolarTransitSearchResult cpp_out;
    taiyin::runtime::EphemerisEvalDiagnostic cpp_diagnostic;
    const taiyin::Status status =
        taiyin::runtime::compute_local_solar_transit_ut(
            &context->value, &cpp_global, longitude_deg, latitude_deg, height_m,
            flags, &cpp_out, diagnostic ? &cpp_diagnostic : 0);
    if (status == taiyin::TAIYIN_STATUS_OK) copy_local_transit(cpp_out, out);
    taiyin_c_internal::from_cpp_diagnostic(cpp_diagnostic, diagnostic);
    return status;
}

taiyin_status TAIYIN_C_CALL taiyin_search_next_local_solar_transit_ut(
    const taiyin_context* context,
    int32_t body_id,
    const taiyin_split_julian_date* jd_start_ut,
    double longitude_deg,
    double latitude_deg,
    double height_m,
    uint64_t flags,
    taiyin_local_solar_transit_result* out,
    taiyin_ephemeris_diagnostic* diagnostic
) {
    if (!context || !taiyin_c_internal::valid_split_jd(jd_start_ut)
        || !taiyin_c_internal::valid_struct(out)
        || !valid_diagnostic(diagnostic)) {
        return taiyin_c_internal::invalid_argument();
    }
    taiyin::runtime::LocalSolarTransitSearchResult cpp_out;
    taiyin::runtime::EphemerisEvalDiagnostic cpp_diagnostic;
    const taiyin::Status status =
        taiyin::runtime::search_next_local_solar_transit_ut(
            &context->value, body_id, cpp_date(jd_start_ut), longitude_deg, latitude_deg,
            height_m, flags, &cpp_out, diagnostic ? &cpp_diagnostic : 0);
    if (status == taiyin::TAIYIN_STATUS_OK) copy_local_transit(cpp_out, out);
    taiyin_c_internal::from_cpp_diagnostic(cpp_diagnostic, diagnostic);
    return status;
}

taiyin_status TAIYIN_C_CALL taiyin_search_lunar_phase_crossings_ut(
    const taiyin_context* context,
    double phase_rad,
    const taiyin_split_julian_date* start_jd_ut,
    const taiyin_split_julian_date* end_jd_ut,
    double max_step_days,
    uint64_t flags,
    taiyin_split_julian_date* out_jd_ut,
    size_t capacity,
    size_t* out_count,
    taiyin_ephemeris_diagnostic* diagnostic
) {
    return array_search(
        context, start_jd_ut, end_jd_ut, out_jd_ut, 0, capacity, out_count, diagnostic,
        [&](taiyin::SplitJulianDate* primary, double*, size_t cap, size_t* count,
            taiyin::runtime::EphemerisEvalDiagnostic* cpp_diagnostic) {
            return taiyin::runtime::search_lunar_phase_crossings_ut(
                &context->value, phase_rad, cpp_date(start_jd_ut), cpp_date(end_jd_ut),
                max_step_days, flags, primary, cap, count, cpp_diagnostic);
        });
}

taiyin_status TAIYIN_C_CALL taiyin_search_lunar_phase_crossings_tt(
    const taiyin_context* context,
    double phase_rad,
    const taiyin_split_julian_date* start_jd_tt,
    const taiyin_split_julian_date* end_jd_tt,
    double max_step_days,
    uint64_t flags,
    taiyin_split_julian_date* out_jd_tt,
    size_t capacity,
    size_t* out_count,
    taiyin_ephemeris_diagnostic* diagnostic
) {
    return array_search(
        context, start_jd_tt, end_jd_tt, out_jd_tt, nullptr, capacity, out_count, diagnostic,
        [&](taiyin::SplitJulianDate* primary, double*, size_t cap, size_t* count,
            taiyin::runtime::EphemerisEvalDiagnostic* cpp_diagnostic) {
            return taiyin::runtime::search_lunar_phase_crossings_tt(
                &context->value, phase_rad, cpp_date(start_jd_tt), cpp_date(end_jd_tt),
                max_step_days, flags, primary, cap, count, cpp_diagnostic);
        });
}

}  // extern "C"
