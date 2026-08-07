#include "taiyin/c/orbital.h"

#include "c_api_internal.h"
#include "taiyin/runtime/orbital_events.h"

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

void copy_point(
    const taiyin::runtime::BodyOrbitReferencePoint& source,
    taiyin_body_orbit_reference_point* out
) noexcept {
    taiyin_c_internal::from_cpp_vector(source.position_au, &out->position_au);
    out->longitude_rad = source.longitude_rad;
    out->latitude_rad = source.latitude_rad;
    out->distance_au = source.distance_au;
}

void copy_orbit(
    const taiyin::runtime::BodyOsculatingOrbit& source,
    taiyin_body_osculating_orbit* out
) noexcept {
    out->struct_size = sizeof(*out);
    out->body_id = source.body_id;
    out->center_id = source.center_id;
    out->reference_frame_id = source.reference_frame_id;
    out->gravitational_parameter_au3_per_day2 =
        source.gravitational_parameter_au3_per_day2;
    out->semi_major_axis_au = source.semi_major_axis_au;
    out->eccentricity = source.eccentricity;
    out->inclination_rad = source.inclination_rad;
    out->longitude_of_ascending_node_rad =
        source.longitude_of_ascending_node_rad;
    out->argument_of_periapsis_rad = source.argument_of_periapsis_rad;
    out->true_anomaly_rad = source.true_anomaly_rad;
    out->mean_anomaly_rad = source.mean_anomaly_rad;
    out->periapsis_distance_au = source.periapsis_distance_au;
    out->apoapsis_distance_au = source.apoapsis_distance_au;
    out->osculating_period_days = source.osculating_period_days;
    out->current_distance_au = source.current_distance_au;
    out->radial_velocity_au_per_day = source.radial_velocity_au_per_day;
}

void copy_reference_points(
    const taiyin::runtime::BodyOrbitReferencePoints& source,
    taiyin_body_orbit_reference_points* out
) noexcept {
    out->struct_size = sizeof(*out);
    out->body_id = source.body_id;
    out->center_id = source.center_id;
    out->reference_frame_id = source.reference_frame_id;
    out->model_id = static_cast<int32_t>(source.model);
    copy_point(source.ascending_node, &out->ascending_node);
    copy_point(source.descending_node, &out->descending_node);
    copy_point(source.periapsis, &out->periapsis);
    copy_point(source.apoapsis, &out->apoapsis);
    copy_point(source.second_focus, &out->second_focus);
}

void copy_apsis(
    const taiyin::runtime::BodyApsisSearchResult& source,
    taiyin_body_apsis_search_result* out
) noexcept {
    out->struct_size = sizeof(*out);
    out->body_id = source.body_id;
    out->center_id = source.center_id;
    out->kind = static_cast<int32_t>(source.kind);
    taiyin_c_internal::from_cpp_split_jd(source.jd, &out->jd);
    out->distance_au = source.distance_au;
    out->radial_velocity_au_per_day = source.radial_velocity_au_per_day;
    out->iteration_count = source.iteration_count;
    out->evaluation_count = source.evaluation_count;
}

void copy_node(
    const taiyin::runtime::BodyNodeSearchResult& source,
    taiyin_body_node_search_result* out
) noexcept {
    out->struct_size = sizeof(*out);
    out->body_id = source.body_id;
    out->center_id = source.center_id;
    out->reference_frame_id = source.reference_frame_id;
    out->kind = static_cast<int32_t>(source.kind);
    taiyin_c_internal::from_cpp_split_jd(source.jd, &out->jd);
    out->reference_plane_angle_rad = source.reference_plane_angle_rad;
    out->distance_au = source.distance_au;
    out->iteration_count = source.iteration_count;
    out->evaluation_count = source.evaluation_count;
}

template <typename CppResult, typename CResult, typename Eval, typename Copy>
taiyin_status run(
    const taiyin_context* context,
    const taiyin_split_julian_date* required_jd,
    CResult* out,
    taiyin_ephemeris_diagnostic* diagnostic,
    const Eval& eval,
    const Copy& copy
) {
    if (!context || !taiyin_c_internal::valid_split_jd(required_jd)
        || !taiyin_c_internal::valid_struct(out)
        || !valid_diagnostic(diagnostic)) {
        return taiyin_c_internal::invalid_argument();
    }
    CppResult cpp_out;
    taiyin::runtime::EphemerisEvalDiagnostic cpp_diagnostic;
    const taiyin::Status status =
        eval(&cpp_out, diagnostic ? &cpp_diagnostic : 0);
    if (status == taiyin::TAIYIN_STATUS_OK) copy(cpp_out, out);
    taiyin_c_internal::from_cpp_diagnostic(cpp_diagnostic, diagnostic);
    return status;
}

}  // namespace

extern "C" {

void TAIYIN_C_CALL taiyin_body_osculating_orbit_init(
    taiyin_body_osculating_orbit* value
) {
    if (!value) return;
    std::memset(value, 0, sizeof(*value));
    value->struct_size = sizeof(*value);
}

void TAIYIN_C_CALL taiyin_body_orbit_reference_points_init(
    taiyin_body_orbit_reference_points* value
) {
    if (!value) return;
    std::memset(value, 0, sizeof(*value));
    value->struct_size = sizeof(*value);
}

void TAIYIN_C_CALL taiyin_body_apsis_search_result_init(
    taiyin_body_apsis_search_result* value
) {
    if (!value) return;
    std::memset(value, 0, sizeof(*value));
    value->struct_size = sizeof(*value);
}

void TAIYIN_C_CALL taiyin_body_node_search_result_init(
    taiyin_body_node_search_result* value
) {
    if (!value) return;
    std::memset(value, 0, sizeof(*value));
    value->struct_size = sizeof(*value);
}

taiyin_status TAIYIN_C_CALL taiyin_calc_body_osculating_orbit_tt(
    const taiyin_context* context,
    int32_t body_id,
    const taiyin_split_julian_date* jd_tt,
    int32_t reference_frame_id,
    uint64_t flags,
    taiyin_body_osculating_orbit* out,
    taiyin_ephemeris_diagnostic* diagnostic
) {
    return run<taiyin::runtime::BodyOsculatingOrbit>(
        context, jd_tt, out, diagnostic,
        [&](taiyin::runtime::BodyOsculatingOrbit* cpp_out,
            taiyin::runtime::EphemerisEvalDiagnostic* cpp_diagnostic) {
            return taiyin::runtime::calc_body_osculating_orbit_tt(
                &context->value, body_id, cpp_date(jd_tt), reference_frame_id, flags,
                cpp_out, cpp_diagnostic);
        },
        &copy_orbit);
}

taiyin_status TAIYIN_C_CALL taiyin_calc_body_osculating_orbit_ut(
    const taiyin_context* context,
    int32_t body_id,
    const taiyin_split_julian_date* jd_ut,
    int32_t reference_frame_id,
    uint64_t flags,
    taiyin_body_osculating_orbit* out,
    taiyin_ephemeris_diagnostic* diagnostic
) {
    return run<taiyin::runtime::BodyOsculatingOrbit>(
        context, jd_ut, out, diagnostic,
        [&](taiyin::runtime::BodyOsculatingOrbit* cpp_out,
            taiyin::runtime::EphemerisEvalDiagnostic* cpp_diagnostic) {
            return taiyin::runtime::calc_body_osculating_orbit_ut(
                &context->value, body_id, cpp_date(jd_ut), reference_frame_id, flags,
                cpp_out, cpp_diagnostic);
        },
        &copy_orbit);
}

taiyin_status TAIYIN_C_CALL taiyin_calc_body_orbit_reference_points_tt(
    const taiyin_context* context,
    int32_t body_id,
    const taiyin_split_julian_date* jd_tt,
    int32_t reference_frame_id,
    uint64_t flags,
    taiyin_body_orbit_reference_points* out,
    taiyin_ephemeris_diagnostic* diagnostic
) {
    return run<taiyin::runtime::BodyOrbitReferencePoints>(
        context, jd_tt, out, diagnostic,
        [&](taiyin::runtime::BodyOrbitReferencePoints* cpp_out,
            taiyin::runtime::EphemerisEvalDiagnostic* cpp_diagnostic) {
            return taiyin::runtime::calc_body_orbit_reference_points_tt(
                &context->value, body_id, cpp_date(jd_tt), reference_frame_id, flags,
                cpp_out, cpp_diagnostic);
        },
        &copy_reference_points);
}

taiyin_status TAIYIN_C_CALL taiyin_calc_body_orbit_reference_points_ut(
    const taiyin_context* context,
    int32_t body_id,
    const taiyin_split_julian_date* jd_ut,
    int32_t reference_frame_id,
    uint64_t flags,
    taiyin_body_orbit_reference_points* out,
    taiyin_ephemeris_diagnostic* diagnostic
) {
    return run<taiyin::runtime::BodyOrbitReferencePoints>(
        context, jd_ut, out, diagnostic,
        [&](taiyin::runtime::BodyOrbitReferencePoints* cpp_out,
            taiyin::runtime::EphemerisEvalDiagnostic* cpp_diagnostic) {
            return taiyin::runtime::calc_body_orbit_reference_points_ut(
                &context->value, body_id, cpp_date(jd_ut), reference_frame_id, flags,
                cpp_out, cpp_diagnostic);
        },
        &copy_reference_points);
}

taiyin_status TAIYIN_C_CALL taiyin_search_next_body_apsis_tt(
    const taiyin_context* context,
    int32_t body_id,
    int32_t kind,
    const taiyin_split_julian_date* jd_start_tt,
    uint64_t flags,
    taiyin_body_apsis_search_result* out,
    taiyin_ephemeris_diagnostic* diagnostic
) {
    return run<taiyin::runtime::BodyApsisSearchResult>(
        context, jd_start_tt, out, diagnostic,
        [&](taiyin::runtime::BodyApsisSearchResult* cpp_out,
            taiyin::runtime::EphemerisEvalDiagnostic* cpp_diagnostic) {
            return taiyin::runtime::search_next_body_apsis_tt(
                &context->value, body_id,
                static_cast<taiyin::runtime::BodyApsisKind>(kind),
                cpp_date(jd_start_tt), flags, cpp_out, cpp_diagnostic);
        },
        &copy_apsis);
}

taiyin_status TAIYIN_C_CALL taiyin_search_next_body_apsis_ut(
    const taiyin_context* context,
    int32_t body_id,
    int32_t kind,
    const taiyin_split_julian_date* jd_start_ut,
    uint64_t flags,
    taiyin_body_apsis_search_result* out,
    taiyin_ephemeris_diagnostic* diagnostic
) {
    return run<taiyin::runtime::BodyApsisSearchResult>(
        context, jd_start_ut, out, diagnostic,
        [&](taiyin::runtime::BodyApsisSearchResult* cpp_out,
            taiyin::runtime::EphemerisEvalDiagnostic* cpp_diagnostic) {
            return taiyin::runtime::search_next_body_apsis_ut(
                &context->value, body_id,
                static_cast<taiyin::runtime::BodyApsisKind>(kind),
                cpp_date(jd_start_ut), flags, cpp_out, cpp_diagnostic);
        },
        &copy_apsis);
}

taiyin_status TAIYIN_C_CALL taiyin_search_next_body_plane_node_tt(
    const taiyin_context* context,
    int32_t body_id,
    int32_t kind,
    const taiyin_split_julian_date* jd_start_tt,
    int32_t reference_frame_id,
    uint64_t flags,
    taiyin_body_node_search_result* out,
    taiyin_ephemeris_diagnostic* diagnostic
) {
    return run<taiyin::runtime::BodyNodeSearchResult>(
        context, jd_start_tt, out, diagnostic,
        [&](taiyin::runtime::BodyNodeSearchResult* cpp_out,
            taiyin::runtime::EphemerisEvalDiagnostic* cpp_diagnostic) {
            return taiyin::runtime::search_next_body_plane_node_tt(
                &context->value, body_id,
                static_cast<taiyin::runtime::BodyNodeKind>(kind), cpp_date(jd_start_tt),
                reference_frame_id, flags, cpp_out, cpp_diagnostic);
        },
        &copy_node);
}

taiyin_status TAIYIN_C_CALL taiyin_search_next_body_plane_node_ut(
    const taiyin_context* context,
    int32_t body_id,
    int32_t kind,
    const taiyin_split_julian_date* jd_start_ut,
    int32_t reference_frame_id,
    uint64_t flags,
    taiyin_body_node_search_result* out,
    taiyin_ephemeris_diagnostic* diagnostic
) {
    return run<taiyin::runtime::BodyNodeSearchResult>(
        context, jd_start_ut, out, diagnostic,
        [&](taiyin::runtime::BodyNodeSearchResult* cpp_out,
            taiyin::runtime::EphemerisEvalDiagnostic* cpp_diagnostic) {
            return taiyin::runtime::search_next_body_plane_node_ut(
                &context->value, body_id,
                static_cast<taiyin::runtime::BodyNodeKind>(kind), cpp_date(jd_start_ut),
                reference_frame_id, flags, cpp_out, cpp_diagnostic);
        },
        &copy_node);
}

}  // extern "C"
