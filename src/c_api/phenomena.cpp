#include "taiyin/c/phenomena.h"

#include "c_api_internal.h"
#include "taiyin/runtime/phenomena.h"

#include <cstring>

namespace {

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

}  // namespace

extern "C" {

void TAIYIN_C_CALL taiyin_body_phenomena_init(taiyin_body_phenomena* value) {
    if (!value) return;
    std::memset(value, 0, sizeof(*value));
    value->struct_size = sizeof(*value);
}

taiyin_call_result TAIYIN_C_CALL taiyin_calc_body_phenomena_tt(
    const taiyin_context* context,
    int32_t body_id,
    const taiyin_split_julian_date* jd_tt,
    uint64_t flags,
    taiyin_body_phenomena* out,
    taiyin_ephemeris_diagnostic* diagnostic
) {
    if (!context || !taiyin_c_internal::valid_split_jd(jd_tt)
        || !taiyin_c_internal::valid_struct(out)
        || (diagnostic && !taiyin_c_internal::valid_struct(diagnostic))) {
        return taiyin_c_internal::pack_call_result(taiyin_c_internal::invalid_argument());
    }
    taiyin::runtime::BodyPhenomena cpp_out;
    taiyin::runtime::EphemerisEvalDiagnostic cpp_diagnostic;
    taiyin_c_internal::TrackedCalcContext tracked(context->value);
    const taiyin::Status status = taiyin::runtime::calc_body_phenomena_tt(
        &tracked.value, body_id,
        taiyin_c_internal::to_cpp_split_jd(*jd_tt), flags, &cpp_out,
        diagnostic ? &cpp_diagnostic : 0);
    if (status == taiyin::TAIYIN_STATUS_OK) copy_phenomena(cpp_out, out);
    taiyin_c_internal::from_cpp_diagnostic(cpp_diagnostic, diagnostic);
    return taiyin_c_internal::pack_call_result(status, tracked.flags);
}

taiyin_call_result TAIYIN_C_CALL taiyin_calc_body_phenomena_ut(
    const taiyin_context* context,
    int32_t body_id,
    const taiyin_split_julian_date* jd_ut,
    uint64_t flags,
    taiyin_body_phenomena* out,
    taiyin_ephemeris_diagnostic* diagnostic
) {
    if (!context || !taiyin_c_internal::valid_split_jd(jd_ut)
        || !taiyin_c_internal::valid_struct(out)
        || (diagnostic && !taiyin_c_internal::valid_struct(diagnostic))) {
        return taiyin_c_internal::pack_call_result(taiyin_c_internal::invalid_argument());
    }
    taiyin::runtime::BodyPhenomena cpp_out;
    taiyin::runtime::EphemerisEvalDiagnostic cpp_diagnostic;
    taiyin_c_internal::TrackedCalcContext tracked(context->value);
    const taiyin::Status status = taiyin::runtime::calc_body_phenomena_ut(
        &tracked.value, body_id,
        taiyin_c_internal::to_cpp_split_jd(*jd_ut), flags, &cpp_out,
        diagnostic ? &cpp_diagnostic : 0);
    if (status == taiyin::TAIYIN_STATUS_OK) copy_phenomena(cpp_out, out);
    taiyin_c_internal::from_cpp_diagnostic(cpp_diagnostic, diagnostic);
    return taiyin_c_internal::pack_call_result(status, tracked.flags);
}

}  // extern "C"
