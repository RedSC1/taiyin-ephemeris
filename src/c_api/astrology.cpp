#include "taiyin/c/astrology.h"

#include "astrology_lifecycle_internal.h"
#include "c_api_internal.h"
#include "taiyin/astrology/houses.h"
#include "taiyin/astrology/lunar_points.h"
#include "taiyin/astrology/sidereal.h"
#include "taiyin/astrology/targets.h"

#include <cstring>
#include <memory>
#include <mutex>
#include <new>
#include <unordered_map>
#include <utility>

namespace {

bool valid_diagnostic(taiyin_ephemeris_diagnostic* diagnostic) noexcept {
    return !diagnostic || taiyin_c_internal::valid_struct(diagnostic);
}

struct CAyanamshaBridge {
    taiyin_ayanamsha_evaluator_fn evaluator;
    void* user_data;
    uint64_t registration_token;
};

struct CHouseBridge {
    taiyin_house_system_evaluator_fn evaluator;
    void* user_data;
    uint64_t registration_token;
};

std::mutex g_c_ayanamsha_model_mutex;
std::unordered_map<int, std::unique_ptr<CAyanamshaBridge>>
    g_c_ayanamsha_models;
std::mutex g_c_house_system_model_mutex;
std::unordered_map<int, std::unique_ptr<CHouseBridge>>
    g_c_house_system_models;
uint64_t g_next_c_model_registration_token = 1;

uint64_t next_c_model_registration_token() noexcept {
    const uint64_t token = g_next_c_model_registration_token++;
    if (g_next_c_model_registration_token == 0) {
        g_next_c_model_registration_token = 1;
    }
    return token;
}

taiyin::Status c_ayanamsha_bridge(
    const taiyin::astrology::AyanamshaDispatchData* data,
    double* out_ayanamsha_rad
) {
    if (!data || !data->native_context || !data->model_data
        || !out_ayanamsha_rad) {
        return taiyin::TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    const CAyanamshaBridge* bridge =
        static_cast<const CAyanamshaBridge*>(data->model_data);
    if (!bridge->evaluator) return taiyin::TAIYIN_ERROR_INVALID_ARGUMENT;
    try {
        taiyin_context borrowed;
        const taiyin::runtime::NativeCalcContext* native = data->native_context;
        if (!native) return taiyin::TAIYIN_ERROR_INVALID_ARGUMENT;
        borrowed.value = *native;
        if (native->apparent_options.deflectors
            && native->apparent_options.deflector_count > 0) {
            borrowed.deflectors.assign(
                native->apparent_options.deflectors,
                native->apparent_options.deflectors
                    + native->apparent_options.deflector_count);
        }
        taiyin_c_internal::repair_context_pointers(&borrowed);
        taiyin_split_julian_date c_jd_tt;
        taiyin_c_internal::from_cpp_split_jd(data->jd_tt, &c_jd_tt);
        return static_cast<taiyin::Status>(bridge->evaluator(
            &borrowed, &c_jd_tt, data->native_position_flags,
            out_ayanamsha_rad, bridge->user_data));
    } catch (const std::bad_alloc&) {
        return taiyin::TAIYIN_ERROR_OUT_OF_MEMORY;
    } catch (...) {
        return taiyin::TAIYIN_ERROR_INTERNAL;
    }
}

bool c_house_bridge(
    const taiyin::astrology::HouseSystemDispatchData* data,
    double out_cusp_longitude_rad[12]
) {
    if (!data || !data->model_data || !out_cusp_longitude_rad) return false;
    const CHouseBridge* bridge =
        static_cast<const CHouseBridge*>(data->model_data);
    if (!bridge->evaluator) return false;
    try {
        taiyin_house_system_dispatch_data c_data;
        std::memset(&c_data, 0, sizeof(c_data));
        c_data.struct_size = sizeof(c_data);
        c_data.armc_rad = data->armc_rad;
        c_data.observer_latitude_rad = data->observer_latitude_rad;
        c_data.true_obliquity_rad = data->true_obliquity_rad;
        c_data.ascendant_rad = data->ascendant_rad;
        c_data.midheaven_rad = data->midheaven_rad;
        return bridge->evaluator(
            &c_data, out_cusp_longitude_rad, bridge->user_data) != 0;
    } catch (...) {
        return false;
    }
}

template <typename T>
void init_struct(T* value) noexcept {
    if (!value) return;
    std::memset(value, 0, sizeof(*value));
    value->struct_size = sizeof(*value);
}

void copy_sidereal(
    const taiyin::astrology::SiderealPosition& source,
    taiyin_sidereal_position* out
) noexcept {
    out->struct_size = sizeof(*out);
    out->coordinate_frame_id = source.coordinate_frame_id;
    out->tropical_longitude_rad = source.tropical_longitude_rad;
    out->sidereal_longitude_rad = source.sidereal_longitude_rad;
    out->latitude_rad = source.latitude_rad;
    out->distance_au = source.distance_au;
    out->tropical_longitude_rate_rad_per_day =
        source.tropical_longitude_rate_rad_per_day;
    out->sidereal_longitude_rate_rad_per_day =
        source.sidereal_longitude_rate_rad_per_day;
}

void copy_sidereal_coordinates(
    const taiyin::astrology::SiderealCoordinates& source,
    taiyin_sidereal_coordinates* out
) noexcept {
    out->struct_size = sizeof(*out);
    out->coordinate_frame_id = source.coordinate_frame_id;
    out->position_flags = source.position_flags;
    for (size_t i = 0; i < 6; ++i) {
        out->values[i] = source.values[i];
    }
}

void copy_house(
    const taiyin::astrology::HouseResult& source,
    taiyin_house_result* out
) noexcept {
    out->struct_size = sizeof(*out);
    out->requested_system_id = source.requested_system_id;
    out->resolved_system_id = source.resolved_system_id;
    out->flags = source.flags;
    out->armc_rad = source.armc_rad;
    out->ascendant_rad = source.ascendant_rad;
    out->midheaven_rad = source.midheaven_rad;
    out->vertex_rad = source.vertex_rad;
    out->east_point_rad = source.east_point_rad;
    out->armc_rate_rad_per_day = source.armc_rate_rad_per_day;
    out->ascendant_rate_rad_per_day = source.ascendant_rate_rad_per_day;
    out->midheaven_rate_rad_per_day = source.midheaven_rate_rad_per_day;
    out->vertex_rate_rad_per_day = source.vertex_rate_rad_per_day;
    out->east_point_rate_rad_per_day = source.east_point_rate_rad_per_day;
    for (size_t i = 0; i < 12; ++i) {
        out->cusp_longitude_rad[i] = source.cusp_longitude_rad[i];
        out->cusp_longitude_rate_rad_per_day[i] =
            source.cusp_longitude_rate_rad_per_day[i];
    }
}

taiyin::astrology::HouseResult to_cpp_house(
    const taiyin_house_result& source
) noexcept {
    taiyin::astrology::HouseResult out;
    out.requested_system_id = source.requested_system_id;
    out.resolved_system_id = source.resolved_system_id;
    out.flags = source.flags;
    out.armc_rad = source.armc_rad;
    out.ascendant_rad = source.ascendant_rad;
    out.midheaven_rad = source.midheaven_rad;
    out.vertex_rad = source.vertex_rad;
    out.east_point_rad = source.east_point_rad;
    out.armc_rate_rad_per_day = source.armc_rate_rad_per_day;
    out.ascendant_rate_rad_per_day = source.ascendant_rate_rad_per_day;
    out.midheaven_rate_rad_per_day = source.midheaven_rate_rad_per_day;
    out.vertex_rate_rad_per_day = source.vertex_rate_rad_per_day;
    out.east_point_rate_rad_per_day = source.east_point_rate_rad_per_day;
    for (size_t i = 0; i < 12; ++i) {
        out.cusp_longitude_rad[i] = source.cusp_longitude_rad[i];
        out.cusp_longitude_rate_rad_per_day[i] =
            source.cusp_longitude_rate_rad_per_day[i];
    }
    return out;
}

void copy_node(
    const taiyin::astrology::LunarNodePosition& source,
    taiyin_lunar_node_position* out
) noexcept {
    out->struct_size = sizeof(*out);
    out->reference_frame_id = source.reference_frame_id;
    out->longitude_rad = source.longitude_rad;
    out->longitude_rate_rad_per_day = source.longitude_rate_rad_per_day;
}

void copy_apsis(
    const taiyin::astrology::LunarApsisPosition& source,
    taiyin_lunar_apsis_position* out
) noexcept {
    out->struct_size = sizeof(*out);
    out->reference_frame_id = source.reference_frame_id;
    out->definition = source.definition;
    out->longitude_rad = source.longitude_rad;
    out->latitude_rad = source.latitude_rad;
    out->longitude_rate_rad_per_day = source.longitude_rate_rad_per_day;
    out->latitude_rate_rad_per_day = source.latitude_rate_rad_per_day;
    out->distance_au = source.distance_au;
    out->distance_rate_au_per_day = source.distance_rate_au_per_day;
    out->extrapolated = source.extrapolated ? 1u : 0u;
}

template <typename CppOut, typename COut, typename Eval, typename Copy>
taiyin_status run_with_diagnostic(
    const taiyin_context* context,
    COut* out,
    taiyin_ephemeris_diagnostic* diagnostic,
    const Eval& eval,
    const Copy& copy
) {
    if (!context || !taiyin_c_internal::valid_struct(out)
        || !valid_diagnostic(diagnostic)) {
        return taiyin_c_internal::invalid_argument();
    }
    CppOut cpp_out;
    taiyin::runtime::EphemerisEvalDiagnostic cpp_diagnostic;
    const taiyin::Status status =
        eval(&cpp_out, diagnostic ? &cpp_diagnostic : nullptr);
    if (status == taiyin::TAIYIN_STATUS_OK) copy(cpp_out, out);
    taiyin_c_internal::from_cpp_diagnostic(cpp_diagnostic, diagnostic);
    return status;
}

}  // namespace

namespace taiyin_c_internal {

std::mutex& astrology_model_lifecycle_mutex() noexcept {
    static std::mutex mutex;
    return mutex;
}

void clear_c_ayanamsha_models_locked() noexcept {
    try {
        std::lock_guard<std::mutex> lock(g_c_ayanamsha_model_mutex);
        for (std::unordered_map<int, std::unique_ptr<CAyanamshaBridge>>::iterator
                 it = g_c_ayanamsha_models.begin();
             it != g_c_ayanamsha_models.end();) {
            // Always drop our bridge ownership. A failed identity check means
            // another C++ caller already removed/replaced the entry, so it is
            // unsafe to remove the current same-ID model and safe to release
            // this now-detached bridge.
            taiyin::astrology::remove_ayanamsha_model_if_matches(
                it->first, &c_ayanamsha_bridge, it->second.get());
            it = g_c_ayanamsha_models.erase(it);
        }
    } catch (...) {
        // This is a best-effort setup-time cleanup API with no failure channel.
        // Any entry not erased remains registered and retains its callback.
    }
}

void clear_c_house_system_models_locked() noexcept {
    try {
        std::lock_guard<std::mutex> lock(g_c_house_system_model_mutex);
        // Remove dependents before their fallback targets. A direct C++ model
        // can also reference a C-owned fallback; that bridge must remain alive
        // until the C++ dependent is removed, because clear must not make its
        // fallback target dangle.
        bool made_progress = true;
        while (made_progress && !g_c_house_system_models.empty()) {
            made_progress = false;
            for (std::unordered_map<int, std::unique_ptr<CHouseBridge>>::iterator
                     it = g_c_house_system_models.begin();
                 it != g_c_house_system_models.end();) {
                const taiyin::astrology::HouseSystemModelRemovalResult result =
                    taiyin::astrology::remove_house_system_model_if_matches(
                        it->first, &c_house_bridge, it->second.get());
                if (result
                    != taiyin::astrology::HouseSystemModelRemovalResult::still_referenced) {
                    it = g_c_house_system_models.erase(it);
                    made_progress = true;
                } else {
                    ++it;
                }
            }
        }
    } catch (...) {
        // This is a best-effort setup-time cleanup API with no failure channel.
        // Any entry not erased remains registered and retains its callback.
    }
}

}  // namespace taiyin_c_internal

namespace {

taiyin_status register_ayanamsha_model_impl(
    int32_t model_id,
    taiyin_ayanamsha_evaluator_fn evaluator,
    int32_t reference_precession_model_id,
    void* user_data,
    uint64_t* out_registration_token
) {
    if (out_registration_token) *out_registration_token = 0;
    if (model_id < taiyin::astrology::TAIYIN_SIDEREAL_AYANAMSHA_CUSTOM_START
        || !evaluator) {
        return taiyin_c_internal::invalid_argument();
    }
    try {
        std::lock_guard<std::mutex> lifecycle_lock(
            taiyin_c_internal::astrology_model_lifecycle_mutex());
        std::lock_guard<std::mutex> lock(g_c_ayanamsha_model_mutex);
        if (g_c_ayanamsha_models.find(model_id) != g_c_ayanamsha_models.end()) {
            return TAIYIN_ERROR_INVALID_ARGUMENT;
        }
        std::unique_ptr<CAyanamshaBridge> bridge(new CAyanamshaBridge{
            evaluator, user_data, next_c_model_registration_token()});
        const std::pair<
            std::unordered_map<int, std::unique_ptr<CAyanamshaBridge>>::iterator,
            bool> inserted = g_c_ayanamsha_models.emplace(
                model_id, std::move(bridge));
        if (!inserted.second) return TAIYIN_ERROR_INVALID_ARGUMENT;
        const taiyin::astrology::AyanamshaModelEntry entry(
            model_id, &c_ayanamsha_bridge, reference_precession_model_id,
            inserted.first->second.get());
        if (!taiyin::astrology::add_ayanamsha_model(entry)) {
            g_c_ayanamsha_models.erase(inserted.first);
            return TAIYIN_ERROR_INVALID_ARGUMENT;
        }
        if (out_registration_token) {
            *out_registration_token = inserted.first->second->registration_token;
        }
        return TAIYIN_STATUS_OK;
    } catch (const std::bad_alloc&) {
        return TAIYIN_ERROR_OUT_OF_MEMORY;
    } catch (...) {
        return TAIYIN_ERROR_INTERNAL;
    }
}

taiyin_status register_house_system_model_impl(
    int32_t model_id,
    taiyin_house_system_evaluator_fn evaluator,
    int32_t fallback_model_id,
    void* user_data,
    uint64_t* out_registration_token
) {
    if (out_registration_token) *out_registration_token = 0;
    if (model_id < taiyin::astrology::TAIYIN_HOUSE_SYSTEM_CUSTOM_START
        || !evaluator) {
        return taiyin_c_internal::invalid_argument();
    }
    try {
        std::lock_guard<std::mutex> lifecycle_lock(
            taiyin_c_internal::astrology_model_lifecycle_mutex());
        std::lock_guard<std::mutex> lock(g_c_house_system_model_mutex);
        if (g_c_house_system_models.find(model_id)
            != g_c_house_system_models.end()) {
            return TAIYIN_ERROR_INVALID_ARGUMENT;
        }
        std::unique_ptr<CHouseBridge> bridge(new CHouseBridge{
            evaluator, user_data, next_c_model_registration_token()});
        const std::pair<
            std::unordered_map<int, std::unique_ptr<CHouseBridge>>::iterator,
            bool> inserted = g_c_house_system_models.emplace(
                model_id, std::move(bridge));
        if (!inserted.second) return TAIYIN_ERROR_INVALID_ARGUMENT;
        const taiyin::astrology::HouseSystemModelEntry entry(
            model_id, &c_house_bridge, fallback_model_id,
            inserted.first->second.get());
        if (!taiyin::astrology::add_house_system_model(entry)) {
            g_c_house_system_models.erase(inserted.first);
            return TAIYIN_ERROR_INVALID_ARGUMENT;
        }
        if (out_registration_token) {
            *out_registration_token = inserted.first->second->registration_token;
        }
        return TAIYIN_STATUS_OK;
    } catch (const std::bad_alloc&) {
        return TAIYIN_ERROR_OUT_OF_MEMORY;
    } catch (...) {
        return TAIYIN_ERROR_INTERNAL;
    }
}

taiyin_status unregister_ayanamsha_model_impl(
    int32_t model_id,
    uint64_t registration_token,
    bool require_matching_token
) {
    if (model_id < taiyin::astrology::TAIYIN_SIDEREAL_AYANAMSHA_CUSTOM_START
        || (require_matching_token && registration_token == 0)) {
        return taiyin_c_internal::invalid_argument();
    }
    try {
        std::lock_guard<std::mutex> lifecycle_lock(
            taiyin_c_internal::astrology_model_lifecycle_mutex());
        std::lock_guard<std::mutex> lock(g_c_ayanamsha_model_mutex);
        const std::unordered_map<int, std::unique_ptr<CAyanamshaBridge>>::iterator
            it = g_c_ayanamsha_models.find(model_id);
        if (it == g_c_ayanamsha_models.end()
            || (require_matching_token
                && it->second->registration_token != registration_token)) {
            return TAIYIN_ERROR_INVALID_ARGUMENT;
        }
        const bool removed = taiyin::astrology::remove_ayanamsha_model_if_matches(
            model_id, &c_ayanamsha_bridge, it->second.get());
        g_c_ayanamsha_models.erase(it);
        return removed ? TAIYIN_STATUS_OK : TAIYIN_ERROR_INVALID_ARGUMENT;
    } catch (...) {
        return TAIYIN_ERROR_INTERNAL;
    }
}

taiyin_status unregister_house_system_model_impl(
    int32_t model_id,
    uint64_t registration_token,
    bool require_matching_token
) {
    if (model_id < taiyin::astrology::TAIYIN_HOUSE_SYSTEM_CUSTOM_START
        || (require_matching_token && registration_token == 0)) {
        return taiyin_c_internal::invalid_argument();
    }
    try {
        std::lock_guard<std::mutex> lifecycle_lock(
            taiyin_c_internal::astrology_model_lifecycle_mutex());
        std::lock_guard<std::mutex> lock(g_c_house_system_model_mutex);
        const std::unordered_map<int, std::unique_ptr<CHouseBridge>>::iterator
            it = g_c_house_system_models.find(model_id);
        if (it == g_c_house_system_models.end()
            || (require_matching_token
                && it->second->registration_token != registration_token)) {
            return TAIYIN_ERROR_INVALID_ARGUMENT;
        }
        const taiyin::astrology::HouseSystemModelRemovalResult result =
            taiyin::astrology::remove_house_system_model_if_matches(
                model_id, &c_house_bridge, it->second.get());
        if (result
            == taiyin::astrology::HouseSystemModelRemovalResult::still_referenced) {
            return TAIYIN_ERROR_UNSUPPORTED;
        }
        g_c_house_system_models.erase(it);
        return result == taiyin::astrology::HouseSystemModelRemovalResult::removed
            ? TAIYIN_STATUS_OK
            : TAIYIN_ERROR_INVALID_ARGUMENT;
    } catch (...) {
        return TAIYIN_ERROR_INTERNAL;
    }
}

}  // namespace

extern "C" {

void TAIYIN_C_CALL taiyin_sidereal_position_init(taiyin_sidereal_position* value) {
    init_struct(value);
}
void TAIYIN_C_CALL taiyin_sidereal_coordinates_init(
    taiyin_sidereal_coordinates* value
) {
    init_struct(value);
}
void TAIYIN_C_CALL taiyin_house_result_init(taiyin_house_result* value) {
    init_struct(value);
}
void TAIYIN_C_CALL taiyin_house_position_result_init(
    taiyin_house_position_result* value
) {
    init_struct(value);
}
void TAIYIN_C_CALL taiyin_lunar_node_position_init(
    taiyin_lunar_node_position* value
) {
    init_struct(value);
}
void TAIYIN_C_CALL taiyin_lunar_apsis_position_init(
    taiyin_lunar_apsis_position* value
) {
    init_struct(value);
}

taiyin_status TAIYIN_C_CALL taiyin_calc_ayanamsha_tt(
    const taiyin_context* context, int32_t ayanamsha_id,
    const taiyin_split_julian_date* jd_tt, uint64_t flags,
    double* out_ayanamsha_rad
) {
    if (!context || !taiyin_c_internal::valid_split_jd(jd_tt)
        || !out_ayanamsha_rad) {
        return taiyin_c_internal::invalid_argument();
    }
    return taiyin::astrology::calc_ayanamsha_tt(
        &context->value, ayanamsha_id,
        taiyin_c_internal::to_cpp_split_jd(*jd_tt), flags,
        out_ayanamsha_rad);
}

}  // extern "C"

template <typename Eval>
taiyin_status calc_sidereal(
    const taiyin_context* context, int32_t ayanamsha_id,
    taiyin_sidereal_position* out,
    taiyin_ephemeris_diagnostic* diagnostic, const Eval& eval
) {
    if (!context || !taiyin_c_internal::valid_struct(out)
        || !valid_diagnostic(diagnostic)) {
        return taiyin_c_internal::invalid_argument();
    }
    taiyin::astrology::SiderealPosition cpp_out;
    taiyin::runtime::EphemerisEvalDiagnostic cpp_diagnostic;
    const taiyin::Status status = eval(
        &context->value, ayanamsha_id, &cpp_out,
        diagnostic ? &cpp_diagnostic : nullptr);
    if (status == taiyin::TAIYIN_STATUS_OK) copy_sidereal(cpp_out, out);
    taiyin_c_internal::from_cpp_diagnostic(cpp_diagnostic, diagnostic);
    return status;
}

extern "C" {

taiyin_status TAIYIN_C_CALL taiyin_calc_sidereal_position_tt(
    const taiyin_context* context, int32_t ayanamsha_id,
    int32_t body_id, const taiyin_split_julian_date* jd_tt,
    uint64_t flags, const taiyin_split_julian_date* reference_epoch_jd,
    taiyin_sidereal_position* out,
    taiyin_ephemeris_diagnostic* diagnostic
) {
    if (!taiyin_c_internal::valid_split_jd(jd_tt)
        || (reference_epoch_jd
            && !taiyin_c_internal::valid_split_jd(reference_epoch_jd))) {
        return taiyin_c_internal::invalid_argument();
    }
    const taiyin::SplitJulianDate cpp_jd_tt =
        taiyin_c_internal::to_cpp_split_jd(*jd_tt);
    return calc_sidereal(
        context, ayanamsha_id, out, diagnostic,
        [&](const taiyin::runtime::NativeCalcContext* native_context,
            int ayanamsha_model_id,
            taiyin::astrology::SiderealPosition* cpp_out,
            taiyin::runtime::EphemerisEvalDiagnostic* cpp_diagnostic) {
            return taiyin::astrology::calc_sidereal_position_tt(
                native_context, ayanamsha_model_id, body_id, cpp_jd_tt, flags, cpp_out, cpp_diagnostic,
                reference_epoch_jd
                    ? taiyin_c_internal::to_cpp_split_jd(*reference_epoch_jd)
                    : taiyin::SplitJulianDate(0, NAN));
        });
}

taiyin_status TAIYIN_C_CALL taiyin_calc_sidereal_position_ut(
    const taiyin_context* context, int32_t ayanamsha_id,
    int32_t body_id, const taiyin_split_julian_date* jd_ut,
    uint64_t flags, const taiyin_split_julian_date* reference_epoch_jd,
    taiyin_sidereal_position* out,
    taiyin_ephemeris_diagnostic* diagnostic
) {
    if (!taiyin_c_internal::valid_split_jd(jd_ut)
        || (reference_epoch_jd
            && !taiyin_c_internal::valid_split_jd(reference_epoch_jd))) {
        return taiyin_c_internal::invalid_argument();
    }
    const taiyin::SplitJulianDate cpp_jd_ut =
        taiyin_c_internal::to_cpp_split_jd(*jd_ut);
    return calc_sidereal(
        context, ayanamsha_id, out, diagnostic,
        [&](const taiyin::runtime::NativeCalcContext* native_context,
            int ayanamsha_model_id,
            taiyin::astrology::SiderealPosition* cpp_out,
            taiyin::runtime::EphemerisEvalDiagnostic* cpp_diagnostic) {
            return taiyin::astrology::calc_sidereal_position_ut(
                native_context, ayanamsha_model_id, body_id, cpp_jd_ut, flags, cpp_out, cpp_diagnostic,
                reference_epoch_jd
                    ? taiyin_c_internal::to_cpp_split_jd(*reference_epoch_jd)
                    : taiyin::SplitJulianDate(0, NAN));
        });
}

}  // extern "C"

template <typename Eval>
taiyin_status calc_sidereal_coordinates(
    const taiyin_context* context,
    int32_t ayanamsha_id,
    taiyin_sidereal_coordinates* out,
    taiyin_ephemeris_diagnostic* diagnostic,
    const Eval& eval
) {
    if (!context || !taiyin_c_internal::valid_struct(out)
        || !valid_diagnostic(diagnostic)) {
        return taiyin_c_internal::invalid_argument();
    }
    taiyin::astrology::SiderealCoordinates cpp_out;
    taiyin::runtime::EphemerisEvalDiagnostic cpp_diagnostic;
    const taiyin::Status status = eval(
        &context->value, ayanamsha_id, &cpp_out,
        diagnostic ? &cpp_diagnostic : nullptr);
    if (status == taiyin::TAIYIN_STATUS_OK) {
        copy_sidereal_coordinates(cpp_out, out);
    }
    taiyin_c_internal::from_cpp_diagnostic(cpp_diagnostic, diagnostic);
    return status;
}

extern "C" {

taiyin_status TAIYIN_C_CALL taiyin_calc_sidereal_coordinates_tt(
    const taiyin_context* context,
    int32_t ayanamsha_id,
    int32_t body_id,
    const taiyin_split_julian_date* jd_tt,
    uint64_t flags,
    const taiyin_split_julian_date* reference_epoch_jd,
    taiyin_sidereal_coordinates* out,
    taiyin_ephemeris_diagnostic* diagnostic
) {
    if (!taiyin_c_internal::valid_split_jd(jd_tt)
        || (reference_epoch_jd
            && !taiyin_c_internal::valid_split_jd(reference_epoch_jd))) {
        return taiyin_c_internal::invalid_argument();
    }
    const taiyin::SplitJulianDate cpp_jd_tt =
        taiyin_c_internal::to_cpp_split_jd(*jd_tt);
    return calc_sidereal_coordinates(
        context, ayanamsha_id, out, diagnostic,
        [&](const taiyin::runtime::NativeCalcContext* native_context,
            int ayanamsha_model_id,
            taiyin::astrology::SiderealCoordinates* cpp_out,
            taiyin::runtime::EphemerisEvalDiagnostic* cpp_diagnostic) {
            return taiyin::astrology::calc_sidereal_coordinates_tt(
                native_context, ayanamsha_model_id, body_id, cpp_jd_tt, flags, cpp_out, cpp_diagnostic,
                reference_epoch_jd
                    ? taiyin_c_internal::to_cpp_split_jd(*reference_epoch_jd)
                    : taiyin::SplitJulianDate(0, NAN));
        });
}

taiyin_status TAIYIN_C_CALL taiyin_calc_sidereal_coordinates_ut(
    const taiyin_context* context,
    int32_t ayanamsha_id,
    int32_t body_id,
    const taiyin_split_julian_date* jd_ut,
    uint64_t flags,
    const taiyin_split_julian_date* reference_epoch_jd,
    taiyin_sidereal_coordinates* out,
    taiyin_ephemeris_diagnostic* diagnostic
) {
    if (!taiyin_c_internal::valid_split_jd(jd_ut)
        || (reference_epoch_jd
            && !taiyin_c_internal::valid_split_jd(reference_epoch_jd))) {
        return taiyin_c_internal::invalid_argument();
    }
    const taiyin::SplitJulianDate cpp_jd_ut =
        taiyin_c_internal::to_cpp_split_jd(*jd_ut);
    return calc_sidereal_coordinates(
        context, ayanamsha_id, out, diagnostic,
        [&](const taiyin::runtime::NativeCalcContext* native_context,
            int ayanamsha_model_id,
            taiyin::astrology::SiderealCoordinates* cpp_out,
            taiyin::runtime::EphemerisEvalDiagnostic* cpp_diagnostic) {
            return taiyin::astrology::calc_sidereal_coordinates_ut(
                native_context, ayanamsha_model_id, body_id, cpp_jd_ut, flags, cpp_out, cpp_diagnostic,
                reference_epoch_jd
                    ? taiyin_c_internal::to_cpp_split_jd(*reference_epoch_jd)
                    : taiyin::SplitJulianDate(0, NAN));
        });
}

taiyin_status TAIYIN_C_CALL taiyin_calc_houses_from_armc(
    double armc_rad, double observer_latitude_rad, double true_obliquity_rad,
    int32_t house_system_id, taiyin_house_result* out
) {
    if (!taiyin_c_internal::valid_struct(out)) {
        return taiyin_c_internal::invalid_argument();
    }
    taiyin::astrology::HouseResult cpp_out;
    const taiyin::Status status = taiyin::astrology::calc_houses_from_armc(
        armc_rad, observer_latitude_rad, true_obliquity_rad, house_system_id,
        &cpp_out);
    if (status == taiyin::TAIYIN_STATUS_OK) copy_house(cpp_out, out);
    return status;
}

taiyin_status TAIYIN_C_CALL taiyin_calc_houses_ut(
    const taiyin_context* context, const taiyin_split_julian_date* jd_ut,
    int32_t house_system_id, taiyin_house_result* out
) {
    if (!context || !taiyin_c_internal::valid_split_jd(jd_ut)
        || !taiyin_c_internal::valid_struct(out)) {
        return taiyin_c_internal::invalid_argument();
    }
    taiyin::astrology::HouseResult cpp_out;
    const taiyin::Status status = taiyin::astrology::calc_houses_ut(
        &context->value, taiyin_c_internal::to_cpp_split_jd(*jd_ut),
        house_system_id, &cpp_out);
    if (status == taiyin::TAIYIN_STATUS_OK) copy_house(cpp_out, out);
    return status;
}

taiyin_status TAIYIN_C_CALL taiyin_calc_houses_tt(
    const taiyin_context* context, const taiyin_split_julian_date* jd_tt,
    int32_t house_system_id, taiyin_house_result* out
) {
    if (!context || !taiyin_c_internal::valid_split_jd(jd_tt)
        || !taiyin_c_internal::valid_struct(out)) {
        return taiyin_c_internal::invalid_argument();
    }
    taiyin::astrology::HouseResult cpp_out;
    const taiyin::Status status = taiyin::astrology::calc_houses_tt(
        &context->value, taiyin_c_internal::to_cpp_split_jd(*jd_tt),
        house_system_id, &cpp_out);
    if (status == taiyin::TAIYIN_STATUS_OK) copy_house(cpp_out, out);
    return status;
}

taiyin_status TAIYIN_C_CALL taiyin_calc_house_position_from_longitude(
    const taiyin_house_result* houses, double ecliptic_longitude_rad,
    taiyin_house_position_result* out
) {
    if (!taiyin_c_internal::valid_struct(houses)
        || !taiyin_c_internal::valid_struct(out)) {
        return taiyin_c_internal::invalid_argument();
    }
    const taiyin::astrology::HouseResult cpp_houses = to_cpp_house(*houses);
    taiyin::astrology::HousePositionResult cpp_out;
    const taiyin::Status status =
        taiyin::astrology::calc_house_position_from_longitude(
            &cpp_houses, ecliptic_longitude_rad, &cpp_out);
    if (status == taiyin::TAIYIN_STATUS_OK) {
        out->house_number = cpp_out.house_number;
        out->fraction = cpp_out.fraction;
        out->continuous_house_position = cpp_out.continuous_house_position;
    }
    return status;
}

taiyin_bool TAIYIN_C_CALL taiyin_has_house_system_model(int32_t model_id) {
    return taiyin::astrology::has_house_system_model(model_id) ? 1u : 0u;
}

taiyin_bool TAIYIN_C_CALL taiyin_has_ayanamsha_model(int32_t model_id) {
    return taiyin::astrology::has_ayanamsha_model(model_id) ? 1u : 0u;
}

taiyin_status TAIYIN_C_CALL taiyin_register_ayanamsha_model(
    int32_t model_id,
    taiyin_ayanamsha_evaluator_fn evaluator,
    int32_t reference_precession_model_id,
    void* user_data
) {
    return register_ayanamsha_model_impl(
        model_id, evaluator, reference_precession_model_id, user_data, nullptr);
}

taiyin_status TAIYIN_C_CALL taiyin_register_house_system_model(
    int32_t model_id,
    taiyin_house_system_evaluator_fn evaluator,
    int32_t fallback_model_id,
    void* user_data
) {
    return register_house_system_model_impl(
        model_id, evaluator, fallback_model_id, user_data, nullptr);
}

taiyin_status TAIYIN_C_CALL taiyin_register_ayanamsha_model_with_token(
    int32_t model_id,
    taiyin_ayanamsha_evaluator_fn evaluator,
    int32_t reference_precession_model_id,
    void* user_data,
    uint64_t* out_registration_token
) {
    if (!out_registration_token) return taiyin_c_internal::invalid_argument();
    return register_ayanamsha_model_impl(
        model_id, evaluator, reference_precession_model_id, user_data,
        out_registration_token);
}

taiyin_status TAIYIN_C_CALL taiyin_register_house_system_model_with_token(
    int32_t model_id,
    taiyin_house_system_evaluator_fn evaluator,
    int32_t fallback_model_id,
    void* user_data,
    uint64_t* out_registration_token
) {
    if (!out_registration_token) return taiyin_c_internal::invalid_argument();
    return register_house_system_model_impl(
        model_id, evaluator, fallback_model_id, user_data,
        out_registration_token);
}

taiyin_status TAIYIN_C_CALL taiyin_unregister_ayanamsha_model(
    int32_t model_id
) {
    return unregister_ayanamsha_model_impl(model_id, 0, false);
}

taiyin_status TAIYIN_C_CALL taiyin_unregister_house_system_model(
    int32_t model_id
) {
    return unregister_house_system_model_impl(model_id, 0, false);
}

taiyin_status TAIYIN_C_CALL taiyin_unregister_ayanamsha_model_with_token(
    int32_t model_id,
    uint64_t registration_token
) {
    return unregister_ayanamsha_model_impl(
        model_id, registration_token, true);
}

taiyin_status TAIYIN_C_CALL taiyin_unregister_house_system_model_with_token(
    int32_t model_id,
    uint64_t registration_token
) {
    return unregister_house_system_model_impl(
        model_id, registration_token, true);
}

void TAIYIN_C_CALL taiyin_clear_ayanamsha_models(void) {
    try {
        std::lock_guard<std::mutex> lifecycle_lock(
            taiyin_c_internal::astrology_model_lifecycle_mutex());
        taiyin_c_internal::clear_c_ayanamsha_models_locked();
    } catch (...) {
        // The public clear API has no failure channel.
    }
}

void TAIYIN_C_CALL taiyin_clear_house_system_models(void) {
    try {
        std::lock_guard<std::mutex> lifecycle_lock(
            taiyin_c_internal::astrology_model_lifecycle_mutex());
        taiyin_c_internal::clear_c_house_system_models_locked();
    } catch (...) {
        // The public clear API has no failure channel.
    }
}

#define TAIYIN_NODE_WRAPPER(c_name, cpp_name) \
    taiyin_status TAIYIN_C_CALL c_name( \
        const taiyin_context* context, const taiyin_split_julian_date* jd, \
        int32_t kind, \
        uint32_t flags, taiyin_lunar_node_position* out, \
        taiyin_ephemeris_diagnostic* diagnostic) { \
        if (!taiyin_c_internal::valid_split_jd(jd)) \
            return taiyin_c_internal::invalid_argument(); \
        return run_with_diagnostic<taiyin::astrology::LunarNodePosition>( \
            context, out, diagnostic, \
            [&](taiyin::astrology::LunarNodePosition* cpp_out, \
                taiyin::runtime::EphemerisEvalDiagnostic* cpp_diagnostic) { \
                return taiyin::astrology::cpp_name( \
                    &context->value, taiyin_c_internal::to_cpp_split_jd(*jd), \
                    static_cast<taiyin::astrology::LunarNodeKind>(kind), \
                    flags, cpp_out, cpp_diagnostic); \
            }, copy_node); \
    }

TAIYIN_NODE_WRAPPER(taiyin_calc_lunar_true_node_tt, calc_lunar_true_node_tt)
TAIYIN_NODE_WRAPPER(taiyin_calc_lunar_true_node_ut, calc_lunar_true_node_ut)
TAIYIN_NODE_WRAPPER(taiyin_calc_lunar_mean_node_tt, calc_lunar_mean_node_tt)
TAIYIN_NODE_WRAPPER(taiyin_calc_lunar_mean_node_ut, calc_lunar_mean_node_ut)

#undef TAIYIN_NODE_WRAPPER

#define TAIYIN_APSIS_WRAPPER(c_name, cpp_name) \
    taiyin_status TAIYIN_C_CALL c_name( \
        const taiyin_context* context, const taiyin_split_julian_date* jd, \
        uint32_t flags, \
        taiyin_lunar_apsis_position* out, \
        taiyin_ephemeris_diagnostic* diagnostic) { \
        if (!taiyin_c_internal::valid_split_jd(jd)) \
            return taiyin_c_internal::invalid_argument(); \
        return run_with_diagnostic<taiyin::astrology::LunarApsisPosition>( \
            context, out, diagnostic, \
            [&](taiyin::astrology::LunarApsisPosition* cpp_out, \
                taiyin::runtime::EphemerisEvalDiagnostic* cpp_diagnostic) { \
                return taiyin::astrology::cpp_name( \
                    &context->value, taiyin_c_internal::to_cpp_split_jd(*jd), \
                    flags, cpp_out, cpp_diagnostic); \
            }, copy_apsis); \
    }

TAIYIN_APSIS_WRAPPER(taiyin_calc_lunar_mean_apogee_tt, calc_lunar_mean_apogee_tt)
TAIYIN_APSIS_WRAPPER(taiyin_calc_lunar_mean_apogee_ut, calc_lunar_mean_apogee_ut)
TAIYIN_APSIS_WRAPPER(taiyin_calc_lunar_osculating_apogee_tt, calc_lunar_osculating_apogee_tt)
TAIYIN_APSIS_WRAPPER(taiyin_calc_lunar_osculating_apogee_ut, calc_lunar_osculating_apogee_ut)
TAIYIN_APSIS_WRAPPER(taiyin_calc_lunar_fitted_apogee_tt, calc_lunar_fitted_apogee_tt)
TAIYIN_APSIS_WRAPPER(taiyin_calc_lunar_fitted_apogee_ut, calc_lunar_fitted_apogee_ut)

#undef TAIYIN_APSIS_WRAPPER

taiyin_status TAIYIN_C_CALL taiyin_register_builtin_astrology_targets(void) {
    return taiyin::astrology::register_builtin_astrology_targets();
}

}  // extern "C"
