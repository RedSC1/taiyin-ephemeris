#include "taiyin/c/solar_time.h"

#include "c_api_internal.h"
#include "taiyin/runtime/solar_time.h"

#include <cmath>
#include <cstring>

namespace {

bool valid_diagnostic(taiyin_ephemeris_diagnostic* diagnostic) noexcept {
    return !diagnostic || taiyin_c_internal::valid_struct(diagnostic);
}

void copy_result(
    const taiyin::runtime::EquationOfTimeResult& source,
    taiyin_equation_of_time_result* out
) noexcept {
    out->struct_size = sizeof(*out);
    taiyin_c_internal::from_cpp_split_jd(source.jd_ut, &out->jd_ut);
    taiyin_c_internal::from_cpp_split_jd(source.jd_tt, &out->jd_tt);
    out->equation_days = source.equation_days;
    out->equation_seconds = source.equation_seconds;
    out->apparent_sun_right_ascension_rad =
        source.apparent_sun_right_ascension_rad;
    out->gast_rad = source.gast_rad;
}

}  // namespace

extern "C" {

void TAIYIN_C_CALL taiyin_equation_of_time_result_init(
    taiyin_equation_of_time_result* value
) {
    if (!value) return;
    std::memset(value, 0, sizeof(*value));
    value->struct_size = sizeof(*value);
}

taiyin_call_result TAIYIN_C_CALL taiyin_calc_equation_of_time_ut(
    const taiyin_context* context,
    const taiyin_split_julian_date* jd_ut,
    taiyin_equation_of_time_result* out,
    taiyin_ephemeris_diagnostic* diagnostic
) {
    if (!context || !taiyin_c_internal::valid_split_jd(jd_ut)
        || !taiyin_c_internal::valid_struct(out)
        || !valid_diagnostic(diagnostic)) {
        return taiyin_c_internal::pack_call_result(taiyin_c_internal::invalid_argument());
    }
    taiyin::runtime::EquationOfTimeResult cpp_out;
    taiyin::runtime::EphemerisEvalDiagnostic cpp_diagnostic;
    taiyin_c_internal::TrackedCalcContext tracked(context->value);
    const taiyin::Status status = taiyin::runtime::calc_equation_of_time_ut(
        &tracked.value, taiyin_c_internal::to_cpp_split_jd(*jd_ut),
        &cpp_out, diagnostic ? &cpp_diagnostic : 0);
    if (status == taiyin::TAIYIN_STATUS_OK) copy_result(cpp_out, out);
    taiyin_c_internal::from_cpp_diagnostic(cpp_diagnostic, diagnostic);
    return taiyin_c_internal::pack_call_result(status, tracked.flags);
}

taiyin_call_result TAIYIN_C_CALL taiyin_calc_equation_of_time_tt(
    const taiyin_context* context,
    const taiyin_split_julian_date* jd_tt,
    taiyin_equation_of_time_result* out,
    taiyin_ephemeris_diagnostic* diagnostic
) {
    if (!context || !taiyin_c_internal::valid_split_jd(jd_tt)
        || !taiyin_c_internal::valid_struct(out)
        || !valid_diagnostic(diagnostic)) {
        return taiyin_c_internal::pack_call_result(taiyin_c_internal::invalid_argument());
    }
    taiyin::runtime::EquationOfTimeResult cpp_out;
    taiyin::runtime::EphemerisEvalDiagnostic cpp_diagnostic;
    taiyin_c_internal::TrackedCalcContext tracked(context->value);
    const taiyin::Status status = taiyin::runtime::calc_equation_of_time_tt(
        &tracked.value, taiyin_c_internal::to_cpp_split_jd(*jd_tt),
        &cpp_out, diagnostic ? &cpp_diagnostic : 0);
    if (status == taiyin::TAIYIN_STATUS_OK) copy_result(cpp_out, out);
    taiyin_c_internal::from_cpp_diagnostic(cpp_diagnostic, diagnostic);
    return taiyin_c_internal::pack_call_result(status, tracked.flags);
}

taiyin_call_result TAIYIN_C_CALL taiyin_local_mean_to_apparent_solar_time(
    const taiyin_context* context,
    const taiyin_split_julian_date* jd_local_mean,
    double longitude_rad,
    taiyin_split_julian_date* out_jd_local_apparent,
    taiyin_ephemeris_diagnostic* diagnostic
) {
    if (!context || !taiyin_c_internal::valid_split_jd(jd_local_mean)
        || !out_jd_local_apparent
        || !valid_diagnostic(diagnostic)) {
        return taiyin_c_internal::pack_call_result(taiyin_c_internal::invalid_argument());
    }
    taiyin::runtime::EphemerisEvalDiagnostic cpp_diagnostic;
    taiyin_c_internal::TrackedCalcContext tracked(context->value);
    taiyin::SplitJulianDate cpp_out;
    const taiyin::Status status =
        taiyin::runtime::local_mean_to_apparent_solar_time(
            &tracked.value,
            taiyin_c_internal::to_cpp_split_jd(*jd_local_mean),
            longitude_rad, &cpp_out, diagnostic ? &cpp_diagnostic : 0);
    if (status == taiyin::TAIYIN_STATUS_OK) {
        taiyin_c_internal::from_cpp_split_jd(cpp_out, out_jd_local_apparent);
    }
    taiyin_c_internal::from_cpp_diagnostic(cpp_diagnostic, diagnostic);
    return taiyin_c_internal::pack_call_result(status, tracked.flags);
}

taiyin_call_result TAIYIN_C_CALL taiyin_local_apparent_to_mean_solar_time(
    const taiyin_context* context,
    const taiyin_split_julian_date* jd_local_apparent,
    double longitude_rad,
    taiyin_split_julian_date* out_jd_local_mean,
    taiyin_ephemeris_diagnostic* diagnostic
) {
    if (!context || !taiyin_c_internal::valid_split_jd(jd_local_apparent)
        || !out_jd_local_mean
        || !valid_diagnostic(diagnostic)) {
        return taiyin_c_internal::pack_call_result(taiyin_c_internal::invalid_argument());
    }
    taiyin::runtime::EphemerisEvalDiagnostic cpp_diagnostic;
    taiyin_c_internal::TrackedCalcContext tracked(context->value);
    taiyin::SplitJulianDate cpp_out;
    const taiyin::Status status =
        taiyin::runtime::local_apparent_to_mean_solar_time(
            &tracked.value,
            taiyin_c_internal::to_cpp_split_jd(*jd_local_apparent),
            longitude_rad, &cpp_out, diagnostic ? &cpp_diagnostic : 0);
    if (status == taiyin::TAIYIN_STATUS_OK) {
        taiyin_c_internal::from_cpp_split_jd(cpp_out, out_jd_local_mean);
    }
    taiyin_c_internal::from_cpp_diagnostic(cpp_diagnostic, diagnostic);
    return taiyin_c_internal::pack_call_result(status, tracked.flags);
}

}  // extern "C"
