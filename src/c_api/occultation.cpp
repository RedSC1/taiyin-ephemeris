#include "taiyin/c/occultation.h"

#include "c_api_internal.h"
#include "taiyin/runtime/occultation_search.h"

#include <cstring>

namespace {

using CppResult = taiyin::runtime::LunarStarOccultationSearchResult;

bool valid_diagnostic(taiyin_ephemeris_diagnostic* diagnostic) noexcept {
    return !diagnostic || taiyin_c_internal::valid_struct(diagnostic);
}

void copy_phenomena(
    const taiyin::runtime::LunarOccultationPhenomena& source,
    taiyin_lunar_occultation_phenomena* out
) noexcept {
    out->struct_size = sizeof(*out);
    out->angular_distance_rad = source.angular_distance_rad;
    out->diameter_ratio = source.diameter_ratio;
    out->magnitude = source.magnitude;
    out->obscuration = source.obscuration;
    out->occulted_fraction = source.occulted_fraction;
}

void copy_result(
    const CppResult& source,
    taiyin_lunar_occultation_result* out
) noexcept {
    out->struct_size = sizeof(*out);
    out->kind = source.kind;
    out->type_flags = source.type_flags;
    taiyin_c_internal::from_cpp_split_jd(source.jd_ut, &out->jd_ut);
    taiyin_c_internal::from_cpp_split_jd(source.begin_jd_ut, &out->begin_jd_ut);
    taiyin_c_internal::from_cpp_split_jd(source.end_jd_ut, &out->end_jd_ut);
    taiyin_c_internal::from_cpp_split_jd(source.first_contact_jd_ut, &out->first_contact_jd_ut);
    taiyin_c_internal::from_cpp_split_jd(source.second_contact_jd_ut, &out->second_contact_jd_ut);
    taiyin_c_internal::from_cpp_split_jd(source.third_contact_jd_ut, &out->third_contact_jd_ut);
    taiyin_c_internal::from_cpp_split_jd(source.fourth_contact_jd_ut, &out->fourth_contact_jd_ut);
    out->separation_rad = source.separation_rad;
    out->moon_radius_rad = source.moon_radius_rad;
    out->target_radius_rad = source.target_radius_rad;
    out->margin_rad = source.margin_rad;
    copy_phenomena(source.phenomena, &out->phenomena);
    taiyin_c_internal::from_cpp_split_jd(source.candidate_jd_ut, &out->candidate_jd_ut);
    taiyin_c_internal::from_cpp_split_jd(source.next_search_jd_ut, &out->next_search_jd_ut);
    out->candidate_count = source.candidate_count;
    out->iteration_count = source.iteration_count;
    out->evaluation_count = source.evaluation_count;
}

CppResult to_cpp_result(
    const taiyin_lunar_occultation_result& source
) noexcept {
    CppResult out;
    out.kind = source.kind;
    out.type_flags = source.type_flags;
    out.jd_ut = taiyin_c_internal::to_cpp_split_jd(source.jd_ut);
    out.begin_jd_ut = taiyin_c_internal::to_cpp_split_jd(source.begin_jd_ut);
    out.end_jd_ut = taiyin_c_internal::to_cpp_split_jd(source.end_jd_ut);
    out.first_contact_jd_ut = taiyin_c_internal::to_cpp_split_jd(source.first_contact_jd_ut);
    out.second_contact_jd_ut = taiyin_c_internal::to_cpp_split_jd(source.second_contact_jd_ut);
    out.third_contact_jd_ut = taiyin_c_internal::to_cpp_split_jd(source.third_contact_jd_ut);
    out.fourth_contact_jd_ut = taiyin_c_internal::to_cpp_split_jd(source.fourth_contact_jd_ut);
    out.separation_rad = source.separation_rad;
    out.moon_radius_rad = source.moon_radius_rad;
    out.target_radius_rad = source.target_radius_rad;
    out.margin_rad = source.margin_rad;
    out.phenomena.angular_distance_rad = source.phenomena.angular_distance_rad;
    out.phenomena.diameter_ratio = source.phenomena.diameter_ratio;
    out.phenomena.magnitude = source.phenomena.magnitude;
    out.phenomena.obscuration = source.phenomena.obscuration;
    out.phenomena.occulted_fraction = source.phenomena.occulted_fraction;
    out.candidate_jd_ut = taiyin_c_internal::to_cpp_split_jd(source.candidate_jd_ut);
    out.next_search_jd_ut = taiyin_c_internal::to_cpp_split_jd(source.next_search_jd_ut);
    out.candidate_count = source.candidate_count;
    out.iteration_count = source.iteration_count;
    out.evaluation_count = source.evaluation_count;
    return out;
}

void copy_sample(
    const taiyin::runtime::LunarOccultationLocalVisibilitySample& source,
    taiyin_lunar_occultation_visibility_sample* out
) noexcept {
    out->struct_size = sizeof(*out);
    out->valid = source.valid ? 1u : 0u;
    taiyin_c_internal::from_cpp_split_jd(source.jd_ut, &out->jd_ut);
    out->moon_altitude_rad = source.moon_altitude_rad;
    out->moon_azimuth_rad = source.moon_azimuth_rad;
    out->target_altitude_rad = source.target_altitude_rad;
    out->target_azimuth_rad = source.target_azimuth_rad;
    out->sun_altitude_rad = source.sun_altitude_rad;
    out->sun_azimuth_rad = source.sun_azimuth_rad;
    out->visibility_flags = source.visibility_flags;
}

void copy_interval(
    const taiyin::runtime::LunarOccultationVisibilityInterval& source,
    taiyin_lunar_occultation_visibility_interval* out
) noexcept {
    out->struct_size = sizeof(*out);
    out->valid = source.valid ? 1u : 0u;
    taiyin_c_internal::from_cpp_split_jd(source.begin_jd_ut, &out->begin_jd_ut);
    taiyin_c_internal::from_cpp_split_jd(source.end_jd_ut, &out->end_jd_ut);
}

void copy_visibility(
    const taiyin::runtime::LunarOccultationLocalVisibility& source,
    taiyin_lunar_occultation_local_visibility* out
) noexcept {
    out->struct_size = sizeof(*out);
    copy_sample(source.first_contact, &out->first_contact);
    copy_sample(source.second_contact, &out->second_contact);
    copy_sample(source.maximum, &out->maximum);
    copy_sample(source.third_contact, &out->third_contact);
    copy_sample(source.fourth_contact, &out->fourth_contact);
    taiyin_c_internal::from_cpp_split_jd(source.target_rise_jd_ut, &out->target_rise_jd_ut);
    taiyin_c_internal::from_cpp_split_jd(source.target_set_jd_ut, &out->target_set_jd_ut);
    taiyin_c_internal::from_cpp_split_jd(source.visible_begin_jd_ut, &out->visible_begin_jd_ut);
    taiyin_c_internal::from_cpp_split_jd(source.visible_end_jd_ut, &out->visible_end_jd_ut);
    taiyin_c_internal::from_cpp_split_jd(source.dark_visible_begin_jd_ut, &out->dark_visible_begin_jd_ut);
    taiyin_c_internal::from_cpp_split_jd(source.dark_visible_end_jd_ut, &out->dark_visible_end_jd_ut);
    out->visible_interval_count = source.visible_interval_count;
    out->dark_visible_interval_count = source.dark_visible_interval_count;
    for (int i = 0; i < TAIYIN_C_OCCULTATION_MAX_VISIBILITY_INTERVALS; ++i) {
        copy_interval(source.visible_intervals[i], &out->visible_intervals[i]);
        copy_interval(
            source.dark_visible_intervals[i], &out->dark_visible_intervals[i]);
    }
    out->visibility_flags = source.visibility_flags;
}

void copy_path_point(
    const taiyin::runtime::LunarOccultationWherePathPoint& source,
    taiyin_lunar_occultation_path_point* out
) noexcept {
    out->struct_size = sizeof(*out);
    out->valid = source.valid ? 1u : 0u;
    taiyin_c_internal::from_cpp_split_jd(source.jd_ut, &out->jd_ut);
    out->longitude_deg = source.longitude_deg;
    out->latitude_deg = source.latitude_deg;
    out->height_m = source.height_m;
}

void copy_where(
    const taiyin::runtime::LunarOccultationWhereResult& source,
    taiyin_lunar_occultation_where_result* out
) noexcept {
    out->struct_size = sizeof(*out);
    out->center_line_hits_earth = source.center_line_hits_earth ? 1u : 0u;
    out->type_flags = source.type_flags;
    taiyin_c_internal::from_cpp_split_jd(source.jd_ut, &out->jd_ut);
    taiyin_c_internal::from_cpp_split_jd(source.center_line_begin_jd_ut, &out->center_line_begin_jd_ut);
    taiyin_c_internal::from_cpp_split_jd(source.center_line_end_jd_ut, &out->center_line_end_jd_ut);
    out->center_line_path_count = source.center_line_path_count;
    out->outer_limit_path_count = source.outer_limit_path_count;
    for (int i = 0; i < TAIYIN_C_OCCULTATION_WHERE_MAX_PATH_POINTS; ++i) {
        copy_path_point(source.center_line_path[i], &out->center_line_path[i]);
        copy_path_point(source.outer_north_path[i], &out->outer_north_path[i]);
        copy_path_point(source.outer_south_path[i], &out->outer_south_path[i]);
    }
    out->center_line_min_longitude_deg = source.center_line_min_longitude_deg;
    out->center_line_max_longitude_deg = source.center_line_max_longitude_deg;
    out->center_line_min_latitude_deg = source.center_line_min_latitude_deg;
    out->center_line_max_latitude_deg = source.center_line_max_latitude_deg;
    out->center_line_path_distance_km = source.center_line_path_distance_km;
    out->outer_limit_mean_width_km = source.outer_limit_mean_width_km;
    out->outer_limit_max_width_km = source.outer_limit_max_width_km;
    out->visible_region_polygon_count = source.visible_region_polygon_count;
    for (int i = 0; i < TAIYIN_C_OCCULTATION_WHERE_MAX_POLYGON_POINTS; ++i) {
        copy_path_point(
            source.visible_region_polygon[i],
            &out->visible_region_polygon[i]);
    }
    out->visible_region_min_longitude_deg =
        source.visible_region_min_longitude_deg;
    out->visible_region_max_longitude_deg =
        source.visible_region_max_longitude_deg;
    out->visible_region_min_latitude_deg = source.visible_region_min_latitude_deg;
    out->visible_region_max_latitude_deg = source.visible_region_max_latitude_deg;
    out->longitude_deg = source.longitude_deg;
    out->latitude_deg = source.latitude_deg;
    out->height_m = source.height_m;
    out->separation_rad = source.separation_rad;
    out->moon_radius_rad = source.moon_radius_rad;
    out->target_radius_rad = source.target_radius_rad;
    out->margin_rad = source.margin_rad;
    copy_phenomena(source.phenomena, &out->phenomena);
    copy_sample(source.local_sample, &out->local_sample);
    out->visibility_flags = source.visibility_flags;
}

void init_sample(taiyin_lunar_occultation_visibility_sample* value) noexcept {
    value->struct_size = sizeof(*value);
}

void init_interval(
    taiyin_lunar_occultation_visibility_interval* value
) noexcept {
    value->struct_size = sizeof(*value);
}

void init_path(taiyin_lunar_occultation_path_point* value) noexcept {
    value->struct_size = sizeof(*value);
}

template <typename Eval>
taiyin_status run_search(
    const taiyin_context* context,
    taiyin_lunar_occultation_result* out,
    taiyin_ephemeris_diagnostic* diagnostic,
    const Eval& eval
) {
    if (!context || !taiyin_c_internal::valid_struct(out)
        || !valid_diagnostic(diagnostic)) {
        return taiyin_c_internal::invalid_argument();
    }
    CppResult cpp_out;
    taiyin::runtime::EphemerisEvalDiagnostic cpp_diagnostic;
    const taiyin::Status status =
        eval(&cpp_out, diagnostic ? &cpp_diagnostic : 0);
    if (status == taiyin::TAIYIN_STATUS_OK) copy_result(cpp_out, out);
    taiyin_c_internal::from_cpp_diagnostic(cpp_diagnostic, diagnostic);
    return status;
}

template <typename Eval>
taiyin_status run_visibility(
    const taiyin_context* context,
    const taiyin_lunar_occultation_result* occultation,
    taiyin_lunar_occultation_local_visibility* out,
    taiyin_ephemeris_diagnostic* diagnostic,
    const Eval& eval
) {
    if (!context || !taiyin_c_internal::valid_struct(occultation)
        || !taiyin_c_internal::valid_struct(out)
        || !valid_diagnostic(diagnostic)) {
        return taiyin_c_internal::invalid_argument();
    }
    const CppResult cpp_occultation = to_cpp_result(*occultation);
    taiyin::runtime::LunarOccultationLocalVisibility cpp_out;
    taiyin::runtime::EphemerisEvalDiagnostic cpp_diagnostic;
    const taiyin::Status status = eval(
        &cpp_occultation, &cpp_out, diagnostic ? &cpp_diagnostic : 0);
    if (status == taiyin::TAIYIN_STATUS_OK) copy_visibility(cpp_out, out);
    taiyin_c_internal::from_cpp_diagnostic(cpp_diagnostic, diagnostic);
    return status;
}

template <typename Eval>
taiyin_status run_where(
    const taiyin_context* context,
    const taiyin_lunar_occultation_result* occultation,
    taiyin_lunar_occultation_where_result* out,
    taiyin_ephemeris_diagnostic* diagnostic,
    const Eval& eval
) {
    if (!context || !taiyin_c_internal::valid_struct(occultation)
        || !taiyin_c_internal::valid_struct(out)
        || !valid_diagnostic(diagnostic)) {
        return taiyin_c_internal::invalid_argument();
    }
    const CppResult cpp_occultation = to_cpp_result(*occultation);
    taiyin::runtime::LunarOccultationWhereResult cpp_out;
    taiyin::runtime::EphemerisEvalDiagnostic cpp_diagnostic;
    const taiyin::Status status = eval(
        &cpp_occultation, &cpp_out, diagnostic ? &cpp_diagnostic : 0);
    if (status == taiyin::TAIYIN_STATUS_OK) copy_where(cpp_out, out);
    taiyin_c_internal::from_cpp_diagnostic(cpp_diagnostic, diagnostic);
    return status;
}

}  // namespace

extern "C" {

void TAIYIN_C_CALL taiyin_lunar_occultation_result_init(
    taiyin_lunar_occultation_result* value
) {
    if (!value) return;
    std::memset(value, 0, sizeof(*value));
    value->struct_size = sizeof(*value);
    value->phenomena.struct_size = sizeof(value->phenomena);
}

void TAIYIN_C_CALL taiyin_lunar_occultation_local_visibility_init(
    taiyin_lunar_occultation_local_visibility* value
) {
    if (!value) return;
    std::memset(value, 0, sizeof(*value));
    value->struct_size = sizeof(*value);
    init_sample(&value->first_contact);
    init_sample(&value->second_contact);
    init_sample(&value->maximum);
    init_sample(&value->third_contact);
    init_sample(&value->fourth_contact);
    for (int i = 0; i < TAIYIN_C_OCCULTATION_MAX_VISIBILITY_INTERVALS; ++i) {
        init_interval(&value->visible_intervals[i]);
        init_interval(&value->dark_visible_intervals[i]);
    }
}

void TAIYIN_C_CALL taiyin_lunar_occultation_where_result_init(
    taiyin_lunar_occultation_where_result* value
) {
    if (!value) return;
    std::memset(value, 0, sizeof(*value));
    value->struct_size = sizeof(*value);
    value->phenomena.struct_size = sizeof(value->phenomena);
    init_sample(&value->local_sample);
    for (int i = 0; i < TAIYIN_C_OCCULTATION_WHERE_MAX_PATH_POINTS; ++i) {
        init_path(&value->center_line_path[i]);
        init_path(&value->outer_north_path[i]);
        init_path(&value->outer_south_path[i]);
    }
    for (int i = 0; i < TAIYIN_C_OCCULTATION_WHERE_MAX_POLYGON_POINTS; ++i) {
        init_path(&value->visible_region_polygon[i]);
    }
}

taiyin_status TAIYIN_C_CALL
taiyin_search_next_geocentric_lunar_star_occultation_ut(
    const taiyin_context* context,
    const char* star_key,
    const taiyin_split_julian_date* jd_start_ut,
    uint64_t flags,
    taiyin_lunar_occultation_result* out,
    taiyin_ephemeris_diagnostic* diagnostic
) {
    if (!star_key || star_key[0] == '\0'
        || !taiyin_c_internal::valid_split_jd(jd_start_ut)) {
        return taiyin_c_internal::invalid_argument();
    }
    return run_search(
        context, out, diagnostic,
        [&](CppResult* cpp_out,
            taiyin::runtime::EphemerisEvalDiagnostic* cpp_diagnostic) {
            return taiyin::runtime::
                search_next_geocentric_lunar_star_occultation_ut(
                    &context->value, star_key,
                    taiyin_c_internal::to_cpp_split_jd(*jd_start_ut), flags,
                    cpp_out,
                    cpp_diagnostic);
        });
}

taiyin_status TAIYIN_C_CALL
taiyin_search_next_local_lunar_star_occultation_ut(
    const taiyin_context* context,
    const char* star_key,
    const taiyin_split_julian_date* jd_start_ut,
    uint64_t flags,
    taiyin_lunar_occultation_result* out,
    taiyin_ephemeris_diagnostic* diagnostic
) {
    if (!star_key || star_key[0] == '\0'
        || !taiyin_c_internal::valid_split_jd(jd_start_ut)) {
        return taiyin_c_internal::invalid_argument();
    }
    return run_search(
        context, out, diagnostic,
        [&](CppResult* cpp_out,
            taiyin::runtime::EphemerisEvalDiagnostic* cpp_diagnostic) {
            return taiyin::runtime::search_next_local_lunar_star_occultation_ut(
                &context->value, star_key,
                taiyin_c_internal::to_cpp_split_jd(*jd_start_ut), flags,
                cpp_out,
                cpp_diagnostic);
        });
}

taiyin_status TAIYIN_C_CALL
taiyin_search_next_geocentric_lunar_body_occultation_ut(
    const taiyin_context* context,
    int32_t body_id,
    const taiyin_split_julian_date* jd_start_ut,
    uint64_t flags,
    taiyin_lunar_occultation_result* out,
    taiyin_ephemeris_diagnostic* diagnostic
) {
    if (!taiyin_c_internal::valid_split_jd(jd_start_ut)) {
        return taiyin_c_internal::invalid_argument();
    }
    return run_search(
        context, out, diagnostic,
        [&](CppResult* cpp_out,
            taiyin::runtime::EphemerisEvalDiagnostic* cpp_diagnostic) {
            return taiyin::runtime::
                search_next_geocentric_lunar_body_occultation_ut(
                    &context->value, body_id,
                    taiyin_c_internal::to_cpp_split_jd(*jd_start_ut), flags,
                    cpp_out,
                    cpp_diagnostic);
        });
}

taiyin_status TAIYIN_C_CALL
taiyin_search_next_geocentric_lunar_body_occultation_with_radius_ut(
    const taiyin_context* context,
    int32_t body_id,
    double target_radius_km,
    const taiyin_split_julian_date* jd_start_ut,
    uint64_t flags,
    taiyin_lunar_occultation_result* out,
    taiyin_ephemeris_diagnostic* diagnostic
) {
    if (!taiyin_c_internal::valid_split_jd(jd_start_ut)) {
        return taiyin_c_internal::invalid_argument();
    }
    return run_search(
        context, out, diagnostic,
        [&](CppResult* cpp_out,
            taiyin::runtime::EphemerisEvalDiagnostic* cpp_diagnostic) {
            return taiyin::runtime::
                search_next_geocentric_lunar_body_occultation_ut(
                    &context->value, body_id, target_radius_km,
                    taiyin_c_internal::to_cpp_split_jd(*jd_start_ut), flags,
                    cpp_out, cpp_diagnostic);
        });
}

taiyin_status TAIYIN_C_CALL
taiyin_search_next_local_lunar_body_occultation_ut(
    const taiyin_context* context,
    int32_t body_id,
    const taiyin_split_julian_date* jd_start_ut,
    uint64_t flags,
    taiyin_lunar_occultation_result* out,
    taiyin_ephemeris_diagnostic* diagnostic
) {
    if (!taiyin_c_internal::valid_split_jd(jd_start_ut)) {
        return taiyin_c_internal::invalid_argument();
    }
    return run_search(
        context, out, diagnostic,
        [&](CppResult* cpp_out,
            taiyin::runtime::EphemerisEvalDiagnostic* cpp_diagnostic) {
            return taiyin::runtime::search_next_local_lunar_body_occultation_ut(
                &context->value, body_id,
                taiyin_c_internal::to_cpp_split_jd(*jd_start_ut), flags,
                cpp_out,
                cpp_diagnostic);
        });
}

taiyin_status TAIYIN_C_CALL
taiyin_search_next_local_lunar_body_occultation_with_radius_ut(
    const taiyin_context* context,
    int32_t body_id,
    double target_radius_km,
    const taiyin_split_julian_date* jd_start_ut,
    uint64_t flags,
    taiyin_lunar_occultation_result* out,
    taiyin_ephemeris_diagnostic* diagnostic
) {
    if (!taiyin_c_internal::valid_split_jd(jd_start_ut)) {
        return taiyin_c_internal::invalid_argument();
    }
    return run_search(
        context, out, diagnostic,
        [&](CppResult* cpp_out,
            taiyin::runtime::EphemerisEvalDiagnostic* cpp_diagnostic) {
            return taiyin::runtime::search_next_local_lunar_body_occultation_ut(
                &context->value, body_id, target_radius_km,
                taiyin_c_internal::to_cpp_split_jd(*jd_start_ut), flags,
                cpp_out, cpp_diagnostic);
        });
}

taiyin_status TAIYIN_C_CALL
taiyin_compute_lunar_star_occultation_local_visibility_ut(
    const taiyin_context* context,
    const char* star_key,
    const taiyin_lunar_occultation_result* occultation,
    uint64_t visibility_flags,
    taiyin_lunar_occultation_local_visibility* out,
    taiyin_ephemeris_diagnostic* diagnostic
) {
    if (!star_key || star_key[0] == '\0') {
        return taiyin_c_internal::invalid_argument();
    }
    return run_visibility(
        context, occultation, out, diagnostic,
        [&](const CppResult* cpp_occultation,
            taiyin::runtime::LunarOccultationLocalVisibility* cpp_out,
            taiyin::runtime::EphemerisEvalDiagnostic* cpp_diagnostic) {
            return taiyin::runtime::
                compute_lunar_star_occultation_local_visibility_ut(
                    &context->value, star_key, cpp_occultation,
                    visibility_flags, cpp_out, cpp_diagnostic);
        });
}

taiyin_status TAIYIN_C_CALL
taiyin_compute_lunar_body_occultation_local_visibility_ut(
    const taiyin_context* context,
    int32_t body_id,
    const taiyin_lunar_occultation_result* occultation,
    uint64_t visibility_flags,
    taiyin_lunar_occultation_local_visibility* out,
    taiyin_ephemeris_diagnostic* diagnostic
) {
    return run_visibility(
        context, occultation, out, diagnostic,
        [&](const CppResult* cpp_occultation,
            taiyin::runtime::LunarOccultationLocalVisibility* cpp_out,
            taiyin::runtime::EphemerisEvalDiagnostic* cpp_diagnostic) {
            return taiyin::runtime::
                compute_lunar_body_occultation_local_visibility_ut(
                    &context->value, body_id, cpp_occultation, visibility_flags,
                    cpp_out, cpp_diagnostic);
        });
}

taiyin_status TAIYIN_C_CALL taiyin_compute_lunar_star_occultation_where_ut(
    const taiyin_context* context,
    const char* star_key,
    const taiyin_lunar_occultation_result* occultation,
    uint64_t flags,
    taiyin_lunar_occultation_where_result* out,
    taiyin_ephemeris_diagnostic* diagnostic
) {
    if (!star_key || star_key[0] == '\0') {
        return taiyin_c_internal::invalid_argument();
    }
    return run_where(
        context, occultation, out, diagnostic,
        [&](const CppResult* cpp_occultation,
            taiyin::runtime::LunarOccultationWhereResult* cpp_out,
            taiyin::runtime::EphemerisEvalDiagnostic* cpp_diagnostic) {
            return taiyin::runtime::compute_lunar_star_occultation_where_ut(
                &context->value, star_key, cpp_occultation, flags, cpp_out,
                cpp_diagnostic);
        });
}

taiyin_status TAIYIN_C_CALL taiyin_compute_lunar_body_occultation_where_ut(
    const taiyin_context* context,
    int32_t body_id,
    const taiyin_lunar_occultation_result* occultation,
    uint64_t flags,
    taiyin_lunar_occultation_where_result* out,
    taiyin_ephemeris_diagnostic* diagnostic
) {
    return run_where(
        context, occultation, out, diagnostic,
        [&](const CppResult* cpp_occultation,
            taiyin::runtime::LunarOccultationWhereResult* cpp_out,
            taiyin::runtime::EphemerisEvalDiagnostic* cpp_diagnostic) {
            return taiyin::runtime::compute_lunar_body_occultation_where_ut(
                &context->value, body_id, cpp_occultation, flags, cpp_out,
                cpp_diagnostic);
        });
}

taiyin_status TAIYIN_C_CALL
taiyin_compute_lunar_body_occultation_where_with_radius_ut(
    const taiyin_context* context,
    int32_t body_id,
    double target_radius_km,
    const taiyin_lunar_occultation_result* occultation,
    uint64_t flags,
    taiyin_lunar_occultation_where_result* out,
    taiyin_ephemeris_diagnostic* diagnostic
) {
    return run_where(
        context, occultation, out, diagnostic,
        [&](const CppResult* cpp_occultation,
            taiyin::runtime::LunarOccultationWhereResult* cpp_out,
            taiyin::runtime::EphemerisEvalDiagnostic* cpp_diagnostic) {
            return taiyin::runtime::compute_lunar_body_occultation_where_ut(
                &context->value, body_id, target_radius_km, cpp_occultation,
                flags, cpp_out, cpp_diagnostic);
        });
}

}  // extern "C"
