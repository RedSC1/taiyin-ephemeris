#include "taiyin/c/taiyin.h"
#include "taiyin/astrology/houses.h"
#include "taiyin/astrology/sidereal.h"
#include "../src/c_api/c_api_internal.h"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <type_traits>

namespace {

taiyin_status TAIYIN_C_CALL c_ayanamsha_callback(
    const taiyin_context*, const taiyin_split_julian_date*, uint64_t,
    double* out, void*
) {
    if (!out) return TAIYIN_ERROR_INVALID_ARGUMENT;
    *out = 0.1;
    return TAIYIN_STATUS_OK;
}

taiyin_bool TAIYIN_C_CALL c_house_callback(
    const taiyin_house_system_dispatch_data*, double out[12], void*
) {
    if (!out) return 0u;
    for (int i = 0; i < 12; ++i) out[i] = static_cast<double>(i) * 0.1;
    return 1u;
}

taiyin::Status cpp_ayanamsha_callback(
    const taiyin::astrology::AyanamshaDispatchData*, double* out
) {
    if (!out) return taiyin::TAIYIN_ERROR_INVALID_ARGUMENT;
    *out = 0.2;
    return taiyin::TAIYIN_STATUS_OK;
}

bool cpp_house_callback(
    const taiyin::astrology::HouseSystemDispatchData*, double out[12]
) {
    if (!out) return false;
    for (int i = 0; i < 12; ++i) out[i] = static_cast<double>(i) * 0.2;
    return true;
}

bool test_mixed_c_and_cpp_model_ownership() {
    uint64_t ayanamsha_token = 0;
    if (taiyin_register_ayanamsha_model_with_token(
            10071, &c_ayanamsha_callback, -1, nullptr, &ayanamsha_token)
            < 0
        || ayanamsha_token == 0
        || !taiyin::astrology::remove_ayanamsha_model(10071)
        || !taiyin::astrology::add_ayanamsha_model(
            taiyin::astrology::AyanamshaModelEntry(
                10071, &cpp_ayanamsha_callback))
        || taiyin_call_result_status(taiyin_unregister_ayanamsha_model_with_token(10071, ayanamsha_token))
            != TAIYIN_ERROR_INVALID_ARGUMENT
        || !taiyin::astrology::has_ayanamsha_model(10071)
        || !taiyin::astrology::remove_ayanamsha_model(10071)) {
        return false;
    }

    uint64_t house_token = 0;
    if (taiyin_register_house_system_model_with_token(
            10072, &c_house_callback, -1, nullptr, &house_token)
            < 0
        || house_token == 0
        || !taiyin::astrology::remove_house_system_model(10072)
        || !taiyin::astrology::add_house_system_model(
            taiyin::astrology::HouseSystemModelEntry(
                10072, &cpp_house_callback))
        || taiyin_call_result_status(taiyin_unregister_house_system_model_with_token(10072, house_token))
            != TAIYIN_ERROR_INVALID_ARGUMENT
        || !taiyin::astrology::has_house_system_model(10072)
        || !taiyin::astrology::remove_house_system_model(10072)) {
        return false;
    }

    uint64_t fallback_token = 0;
    uint64_t dependent_token = 0;
    if (taiyin_register_house_system_model_with_token(
            10073, &c_house_callback, -1, nullptr, &fallback_token)
            < 0
        || taiyin_register_house_system_model_with_token(
            10074, &c_house_callback, 10073, nullptr, &dependent_token)
            < 0
        || taiyin_call_result_status(taiyin_unregister_house_system_model_with_token(10073, fallback_token))
            != TAIYIN_ERROR_UNSUPPORTED) {
        return false;
    }
    taiyin_clear_house_system_models();
    if (taiyin::astrology::has_house_system_model(10073)
        || taiyin::astrology::has_house_system_model(10074)) {
        return false;
    }

    uint64_t cpp_dependent_token = 0;
    if (taiyin_register_house_system_model_with_token(
            10075, &c_house_callback, -1, nullptr, &cpp_dependent_token)
            < 0
        || !taiyin::astrology::add_house_system_model(
            taiyin::astrology::HouseSystemModelEntry(
                10076, &cpp_house_callback, 10075))) {
        return false;
    }
    taiyin_clear_house_system_models();
    if (!taiyin::astrology::has_house_system_model(10075)
        || !taiyin::astrology::remove_house_system_model(10076)) {
        return false;
    }
    taiyin_clear_house_system_models();
    return !taiyin::astrology::has_house_system_model(10075);
}

bool test_split_jd_output_normalization() {
    taiyin_split_julian_date output = {0, 0.0};
    taiyin_c_internal::from_cpp_split_jd(
        taiyin::SplitJulianDate(2451545, 1.25), &output);
    return output.day_number == 2451546
        && output.day_fraction == 0.25;
}

}  // namespace

static_assert(std::is_standard_layout<taiyin_calendar_datetime>::value,
              "C ABI date must be standard-layout");
static_assert(std::is_standard_layout<taiyin_split_julian_date>::value,
              "C ABI split Julian date must be standard-layout");
static_assert(std::is_standard_layout<taiyin_split_precise_time_scales>::value,
              "C ABI split time scales must be standard-layout");
static_assert(std::is_standard_layout<taiyin_ephemeris_diagnostic>::value,
              "C ABI diagnostic must be standard-layout");

int main() {
    taiyin_calendar_datetime date;
    taiyin_calendar_datetime_init(&date);

    const std::uint64_t required =
        TAIYIN_CAPABILITY_RUNTIME | TAIYIN_CAPABILITY_POSITION;
    return date.struct_size == sizeof(date)
            && taiyin_get_c_abi_version() == TAIYIN_C_ABI_VERSION
            && std::strcmp(
                taiyin_get_library_version(),
                TAIYIN_LIBRARY_VERSION_STRING) == 0
            && std::strcmp(
                taiyin_get_library_codename(),
                TAIYIN_LIBRARY_CODENAME) == 0
            && std::strcmp(
                taiyin_get_library_codename(),
                "Singularity") == 0
            && (taiyin_get_capabilities() & required) == required
            && test_mixed_c_and_cpp_model_ownership()
            && test_split_jd_output_normalization()
        ? 0
        : 1;
}
