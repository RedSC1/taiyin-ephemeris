#include "taiyin/c/heliacal.h"

#include "c_api_internal.h"
#include "taiyin/runtime/heliacal_visibility.h"

#include <cstring>

namespace {

bool valid_diagnostic(taiyin_ephemeris_diagnostic* diagnostic) noexcept {
    return !diagnostic || taiyin_c_internal::valid_struct(diagnostic);
}

taiyin::runtime::HeliacalVisibilityConditions to_cpp_conditions(
    const taiyin_heliacal_visibility_conditions& source
) noexcept {
    taiyin::runtime::HeliacalVisibilityConditions out;
    out.extinction_mag_per_airmass = source.extinction_mag_per_airmass;
    out.sky_brightness_nanolambert = source.sky_brightness_nanolambert;
    out.night_sky_brightness_nanolambert =
        source.night_sky_brightness_nanolambert;
    return out;
}

void copy_result(
    const taiyin::runtime::HeliacalVisibilityResult& source,
    taiyin_heliacal_visibility_result* out
) noexcept {
    out->struct_size = sizeof(*out);
    out->visible = source.visible ? 1u : 0u;
    out->model_id = source.model_id;
    out->extinction_model_id = source.extinction_model_id;
    out->twilight_model_id = source.twilight_model_id;
    out->moonlight_model_id = source.moonlight_model_id;
    out->visual_threshold_model_id = source.visual_threshold_model_id;
    out->target_magnitude = source.target_magnitude;
    out->limiting_magnitude = source.limiting_magnitude;
    out->target_altitude_rad = source.target_altitude_rad;
    out->target_azimuth_rad = source.target_azimuth_rad;
    out->sun_altitude_rad = source.sun_altitude_rad;
    out->sun_azimuth_rad = source.sun_azimuth_rad;
    out->target_sun_separation_rad = source.target_sun_separation_rad;
    out->airmass = source.airmass;
    out->extinction_mag_per_airmass = source.extinction_mag_per_airmass;
    out->extinction_mag = source.extinction_mag;
    out->sky_brightness_nanolambert = source.sky_brightness_nanolambert;
    out->moonlight_brightness_nanolambert =
        source.moonlight_brightness_nanolambert;
    out->threshold_illuminance_footcandles =
        source.threshold_illuminance_footcandles;
    out->target_illuminance_footcandles =
        source.target_illuminance_footcandles;
    out->visibility_margin_magnitude = source.visibility_margin_magnitude;
    out->required_sun_altitude_rad = source.required_sun_altitude_rad;
    out->solar_depression_margin_rad = source.solar_depression_margin_rad;
}

void copy_search(
    const taiyin::runtime::HeliacalVisibilitySearchResult& source,
    taiyin_heliacal_visibility_search_result* out
) noexcept {
    out->struct_size = sizeof(*out);
    out->event_kind = source.event_kind;
    taiyin_c_internal::from_cpp_split_jd(source.jd_ut, &out->jd_ut);
    taiyin_c_internal::from_cpp_split_jd(
        source.window_start_jd_ut, &out->window_start_jd_ut);
    taiyin_c_internal::from_cpp_split_jd(
        source.window_end_jd_ut, &out->window_end_jd_ut);
    out->scanned_day_count = source.scanned_day_count;
    out->sampled_window_count = source.sampled_window_count;
    out->visibility_evaluation_count = source.visibility_evaluation_count;
    copy_result(source.visibility, &out->visibility);
}

template <typename Out, typename CppOut, typename Eval, typename Copy>
taiyin_call_result run(
    const taiyin_context* context,
    const taiyin_heliacal_visibility_conditions* conditions,
    Out* out,
    taiyin_ephemeris_diagnostic* diagnostic,
    const Eval& eval,
    const Copy& copy
) {
    if (!context || !taiyin_c_internal::valid_struct(conditions)
        || !taiyin_c_internal::valid_struct(out)
        || !valid_diagnostic(diagnostic)) {
        return taiyin_c_internal::pack_call_result(
            taiyin_c_internal::invalid_argument());
    }
    taiyin_c_internal::TrackedCalcContext tracked(context->value);
    const taiyin::runtime::HeliacalVisibilityConditions cpp_conditions =
        to_cpp_conditions(*conditions);
    CppOut cpp_out;
    taiyin::runtime::EphemerisEvalDiagnostic cpp_diagnostic;
    const taiyin::Status status = eval(
        &tracked.value, &cpp_conditions, &cpp_out,
        diagnostic ? &cpp_diagnostic : nullptr);
    if (status == taiyin::TAIYIN_STATUS_OK) copy(cpp_out, out);
    taiyin_c_internal::from_cpp_diagnostic(cpp_diagnostic, diagnostic);
    return taiyin_c_internal::pack_call_result(status, tracked.flags);
}

}  // namespace

extern "C" {

void TAIYIN_C_CALL taiyin_heliacal_visibility_conditions_init(
    taiyin_heliacal_visibility_conditions* value
) {
    if (!value) return;
    taiyin::runtime::HeliacalVisibilityConditions defaults;
    std::memset(value, 0, sizeof(*value));
    value->struct_size = sizeof(*value);
    value->extinction_mag_per_airmass =
        defaults.extinction_mag_per_airmass;
    value->sky_brightness_nanolambert =
        defaults.sky_brightness_nanolambert;
    value->night_sky_brightness_nanolambert =
        defaults.night_sky_brightness_nanolambert;
}

void TAIYIN_C_CALL taiyin_heliacal_visibility_result_init(
    taiyin_heliacal_visibility_result* value
) {
    if (!value) return;
    std::memset(value, 0, sizeof(*value));
    value->struct_size = sizeof(*value);
}

void TAIYIN_C_CALL taiyin_heliacal_visibility_search_result_init(
    taiyin_heliacal_visibility_search_result* value
) {
    if (!value) return;
    std::memset(value, 0, sizeof(*value));
    value->struct_size = sizeof(*value);
    value->visibility.struct_size = sizeof(value->visibility);
}

taiyin_call_result TAIYIN_C_CALL taiyin_calc_body_heliacal_visibility_ut(
    const taiyin_context* context, int32_t body_id,
    const taiyin_split_julian_date* jd_ut,
    uint64_t flags, const taiyin_heliacal_visibility_conditions* conditions,
    taiyin_heliacal_visibility_result* out,
    taiyin_ephemeris_diagnostic* diagnostic
) {
    if (!taiyin_c_internal::valid_split_jd(jd_ut)) {
        return taiyin_c_internal::pack_call_result(taiyin_c_internal::invalid_argument());
    }
    return run<taiyin_heliacal_visibility_result,
               taiyin::runtime::HeliacalVisibilityResult>(
        context, conditions, out, diagnostic,
        [&](const taiyin::runtime::NativeCalcContext* calc,
            const taiyin::runtime::HeliacalVisibilityConditions* cpp_conditions,
            taiyin::runtime::HeliacalVisibilityResult* cpp_out,
            taiyin::runtime::EphemerisEvalDiagnostic* cpp_diagnostic) {
            return taiyin::runtime::calc_body_heliacal_visibility_ut(
                calc, body_id,
                taiyin_c_internal::to_cpp_split_jd(*jd_ut), flags,
                cpp_conditions, cpp_out, cpp_diagnostic);
        }, copy_result);
}

taiyin_call_result TAIYIN_C_CALL taiyin_calc_star_heliacal_visibility_ut(
    const taiyin_context* context, const char* star_key,
    const taiyin_split_julian_date* jd_ut,
    uint64_t flags, const taiyin_heliacal_visibility_conditions* conditions,
    taiyin_heliacal_visibility_result* out,
    taiyin_ephemeris_diagnostic* diagnostic
) {
    if (!star_key || star_key[0] == '\0'
        || !taiyin_c_internal::valid_split_jd(jd_ut)) {
        return taiyin_c_internal::pack_call_result(taiyin_c_internal::invalid_argument());
    }
    return run<taiyin_heliacal_visibility_result,
               taiyin::runtime::HeliacalVisibilityResult>(
        context, conditions, out, diagnostic,
        [&](const taiyin::runtime::NativeCalcContext* calc,
            const taiyin::runtime::HeliacalVisibilityConditions* cpp_conditions,
            taiyin::runtime::HeliacalVisibilityResult* cpp_out,
            taiyin::runtime::EphemerisEvalDiagnostic* cpp_diagnostic) {
            return taiyin::runtime::calc_star_heliacal_visibility_ut(
                calc, star_key,
                taiyin_c_internal::to_cpp_split_jd(*jd_ut), flags,
                cpp_conditions, cpp_out, cpp_diagnostic);
        }, copy_result);
}

taiyin_call_result TAIYIN_C_CALL taiyin_search_next_body_heliacal_visibility_ut(
    const taiyin_context* context, int32_t body_id,
    const taiyin_split_julian_date* jd_start_ut,
    int32_t event_kind, double max_search_days, uint64_t flags,
    const taiyin_heliacal_visibility_conditions* conditions,
    taiyin_heliacal_visibility_search_result* out,
    taiyin_ephemeris_diagnostic* diagnostic
) {
    if (!taiyin_c_internal::valid_split_jd(jd_start_ut)) {
        return taiyin_c_internal::pack_call_result(taiyin_c_internal::invalid_argument());
    }
    return run<taiyin_heliacal_visibility_search_result,
               taiyin::runtime::HeliacalVisibilitySearchResult>(
        context, conditions, out, diagnostic,
        [&](const taiyin::runtime::NativeCalcContext* calc,
            const taiyin::runtime::HeliacalVisibilityConditions* cpp_conditions,
            taiyin::runtime::HeliacalVisibilitySearchResult* cpp_out,
            taiyin::runtime::EphemerisEvalDiagnostic* cpp_diagnostic) {
            return taiyin::runtime::search_next_body_heliacal_visibility_ut(
                calc, body_id,
                taiyin_c_internal::to_cpp_split_jd(*jd_start_ut), event_kind,
                max_search_days, flags, cpp_conditions, cpp_out,
                cpp_diagnostic);
        }, copy_search);
}

taiyin_call_result TAIYIN_C_CALL taiyin_search_next_star_heliacal_visibility_ut(
    const taiyin_context* context, const char* star_key,
    const taiyin_split_julian_date* jd_start_ut,
    int32_t event_kind, double max_search_days, uint64_t flags,
    const taiyin_heliacal_visibility_conditions* conditions,
    taiyin_heliacal_visibility_search_result* out,
    taiyin_ephemeris_diagnostic* diagnostic
) {
    if (!star_key || star_key[0] == '\0'
        || !taiyin_c_internal::valid_split_jd(jd_start_ut)) {
        return taiyin_c_internal::pack_call_result(taiyin_c_internal::invalid_argument());
    }
    return run<taiyin_heliacal_visibility_search_result,
               taiyin::runtime::HeliacalVisibilitySearchResult>(
        context, conditions, out, diagnostic,
        [&](const taiyin::runtime::NativeCalcContext* calc,
            const taiyin::runtime::HeliacalVisibilityConditions* cpp_conditions,
            taiyin::runtime::HeliacalVisibilitySearchResult* cpp_out,
            taiyin::runtime::EphemerisEvalDiagnostic* cpp_diagnostic) {
            return taiyin::runtime::search_next_star_heliacal_visibility_ut(
                calc, star_key,
                taiyin_c_internal::to_cpp_split_jd(*jd_start_ut), event_kind,
                max_search_days, flags, cpp_conditions, cpp_out,
                cpp_diagnostic);
        }, copy_search);
}

}  // extern "C"
