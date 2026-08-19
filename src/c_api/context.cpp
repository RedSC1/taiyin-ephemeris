#include "taiyin/c/context.h"

#include "c_api_internal.h"

#include "taiyin/apparent_position.h"
#include "taiyin/corrections.h"
#include "taiyin/dispatch.h"

#include <cmath>
#include <cstring>
#include <new>
#include <vector>

namespace {

const uint32_t kSupportedApparentFlags =
    taiyin::TAIYIN_APPARENT_LIGHT_TIME
    | taiyin::TAIYIN_APPARENT_SPHERICAL
    | taiyin::TAIYIN_APPARENT_ABERRATION
    | taiyin::TAIYIN_APPARENT_DEFLECTION
    | taiyin::TAIYIN_APPARENT_VELOCITY
    | taiyin::TAIYIN_APPARENT_ACCELERATION
    | taiyin::TAIYIN_APPARENT_SHAPIRO_DELAY;

bool valid_output_frame(int32_t frame_id) noexcept {
    return frame_id >= taiyin::TAIYIN_APPARENT_FRAME_ICRF
        && frame_id <= taiyin::TAIYIN_APPARENT_FRAME_CIRS;
}

bool valid_apparent_config(const taiyin_apparent_config& config) noexcept {
    return (config.flags & ~kSupportedApparentFlags) == 0u
        && valid_output_frame(config.output_frame_id)
        && config.light_time_method_id == 0
        && config.shapiro_delay_model_id == 0
        && config.aberration_model_id
            == taiyin::dispatch::ABERRATION_ANNUAL_RELATIVISTIC
        && (config.deflection_model_id == taiyin::TAIYIN_DEFLECTION_MODEL_ERFA
            || config.deflection_model_id
                == taiyin::TAIYIN_DEFLECTION_MODEL_SOLAR_DISK)
        && config.max_light_time_iterations >= 0
        && std::isfinite(config.light_time_tolerance_days)
        && config.light_time_tolerance_days >= 0.0
        && std::isfinite(config.matrix_derivative_step_days)
        && config.matrix_derivative_step_days > 0.0
        && ((config.flags & taiyin::TAIYIN_APPARENT_SHAPIRO_DELAY) == 0u
            || (config.flags & taiyin::TAIYIN_APPARENT_LIGHT_TIME) != 0u);
}

bool valid_astro_models(const taiyin_astro_model_config& config) noexcept {
    taiyin::dispatch::PrecessionModelEntry precession;
    taiyin::dispatch::NutationModelEntry nutation;
    return (config.tdb_model_id == TAIYIN_TDB_FAST_PERIODIC
            || config.tdb_model_id == TAIYIN_TDB_SOFA_FULL)
        && taiyin::dispatch::select_precession_model(
            config.precession_model_id, &precession)
        && taiyin::dispatch::select_nutation_model(
            config.nutation_model_id, &nutation)
        && config.obliquity_model_id == 0
        && (config.frame_route_id == taiyin::dispatch::FRAME_ROUTE_EQUINOX
            || config.frame_route_id == taiyin::dispatch::FRAME_ROUTE_CIRS);
}

bool valid_delta_t_model(
    int32_t delta_t_model_id,
    int32_t ephemeris_family_id
) noexcept {
    return delta_t_model_id == TAIYIN_DELTA_T_ESTIMATED_DEFAULT
        && (ephemeris_family_id == TAIYIN_EPHEMERIS_FAMILY_UNKNOWN
            || ephemeris_family_id == TAIYIN_EPHEMERIS_FAMILY_DE431
            || ephemeris_family_id == TAIYIN_EPHEMERIS_FAMILY_DE441);
}

bool valid_state(const taiyin_cartesian_state& state) noexcept {
    return std::isfinite(state.position_au.x)
        && std::isfinite(state.position_au.y)
        && std::isfinite(state.position_au.z)
        && std::isfinite(state.velocity_au_per_day.x)
        && std::isfinite(state.velocity_au_per_day.y)
        && std::isfinite(state.velocity_au_per_day.z)
        && std::isfinite(state.acceleration_au_per_day2.x)
        && std::isfinite(state.acceleration_au_per_day2.y)
        && std::isfinite(state.acceleration_au_per_day2.z);
}

taiyin::runtime::NativeObserverLocation to_cpp_location(
    const taiyin_observer_location& location
) noexcept {
    return taiyin::runtime::native_observer_location_degrees(
        location.longitude_deg,
        location.latitude_deg,
        location.height_m);
}

}  // namespace

extern "C" {

void TAIYIN_C_CALL taiyin_observer_location_init(
    taiyin_observer_location* location
) {
    if (!location) return;
    std::memset(location, 0, sizeof(*location));
    location->struct_size = sizeof(*location);
}

void TAIYIN_C_CALL taiyin_atmosphere_init(taiyin_atmosphere* atmosphere) {
    if (!atmosphere) return;
    std::memset(atmosphere, 0, sizeof(*atmosphere));
    atmosphere->struct_size = sizeof(*atmosphere);
    atmosphere->pressure_mbar = 1013.25;
    atmosphere->temperature_celsius = 15.0;
    atmosphere->wavelength_micrometer = 0.55;
}

void TAIYIN_C_CALL taiyin_astro_model_config_init(
    taiyin_astro_model_config* config
) {
    if (!config) return;
    const taiyin::runtime::AstroModelContext defaults;
    std::memset(config, 0, sizeof(*config));
    config->struct_size = sizeof(*config);
    config->tdb_model_id = defaults.tdb_model_id;
    config->precession_model_id = defaults.precession_model_id;
    config->nutation_model_id = defaults.nutation_model_id;
    config->obliquity_model_id = defaults.obliquity_model_id;
    config->frame_route_id = defaults.frame_route_id;
}

void TAIYIN_C_CALL taiyin_apparent_config_init(
    taiyin_apparent_config* config
) {
    if (!config) return;
    const taiyin::runtime::ApparentOptions defaults;
    std::memset(config, 0, sizeof(*config));
    config->struct_size = sizeof(*config);
    config->flags = defaults.flags
        | taiyin::TAIYIN_APPARENT_ABERRATION
        | taiyin::TAIYIN_APPARENT_DEFLECTION;
    config->output_frame_id = defaults.output_frame_id;
    config->light_time_method_id = defaults.light_time_method_id;
    config->shapiro_delay_model_id = defaults.shapiro_delay_model_id;
    config->aberration_model_id = defaults.aberration_model_id;
    config->deflection_model_id = defaults.deflection_model_id;
    config->max_light_time_iterations = defaults.max_light_time_iterations;
    config->light_time_tolerance_days = defaults.light_time_tolerance_days;
    config->matrix_derivative_step_days =
        defaults.matrix_derivative_step_days;
}

void TAIYIN_C_CALL taiyin_apparent_deflector_init(
    taiyin_apparent_deflector* deflector
) {
    if (!deflector) return;
    std::memset(deflector, 0, sizeof(*deflector));
    deflector->struct_size = sizeof(*deflector);
}

taiyin_call_result TAIYIN_C_CALL taiyin_context_create(taiyin_context** out_context) {
    if (!out_context) return taiyin_c_internal::pack_call_result(taiyin_c_internal::invalid_argument());
    *out_context = new (std::nothrow) taiyin_context();
    if (*out_context) {
        taiyin_c_internal::repair_context_pointers(*out_context);
    }
    return taiyin_c_internal::pack_call_result(*out_context
        ? taiyin::TAIYIN_STATUS_OK
        : taiyin::TAIYIN_ERROR_OUT_OF_MEMORY);
}

taiyin_call_result TAIYIN_C_CALL taiyin_context_clone(
    const taiyin_context* source,
    taiyin_context** out_context
) {
    if (!source || !out_context) return taiyin_c_internal::pack_call_result(taiyin_c_internal::invalid_argument());
    *out_context = new (std::nothrow) taiyin_context();
    if (!*out_context) return taiyin_c_internal::pack_call_result(taiyin::TAIYIN_ERROR_OUT_OF_MEMORY);
    (*out_context)->value = source->value;
    try {
        (*out_context)->deflectors = source->deflectors;
    } catch (...) {
        delete *out_context;
        *out_context = 0;
        return taiyin_c_internal::pack_call_result(taiyin::TAIYIN_ERROR_OUT_OF_MEMORY);
    }
    taiyin_c_internal::repair_context_pointers(*out_context);
    return taiyin_c_internal::pack_call_result(taiyin::TAIYIN_STATUS_OK);
}

void TAIYIN_C_CALL taiyin_context_destroy(taiyin_context* context) {
    delete context;
}

taiyin_call_result TAIYIN_C_CALL taiyin_context_reset(taiyin_context* context) {
    if (!context) return taiyin_c_internal::pack_call_result(taiyin_c_internal::invalid_argument());
    context->value = taiyin::runtime::NativeCalcContext();
    context->deflectors.clear();
    taiyin_c_internal::repair_context_pointers(context);
    return taiyin_c_internal::pack_call_result(taiyin::TAIYIN_STATUS_OK);
}

taiyin_call_result TAIYIN_C_CALL taiyin_context_set_observer_location(
    taiyin_context* context,
    const taiyin_observer_location* location
) {
    if (!context || !taiyin_c_internal::valid_struct(location)) {
        return taiyin_c_internal::pack_call_result(taiyin_c_internal::invalid_argument());
    }
    const taiyin::runtime::NativeObserverLocation cpp =
        to_cpp_location(*location);
    return taiyin_c_internal::pack_call_result(taiyin::runtime::native_context_set_observer_location(
        &context->value, cpp));
}

taiyin_call_result TAIYIN_C_CALL taiyin_context_clear_observer_location(
    taiyin_context* context
) {
    if (!context) return taiyin_c_internal::pack_call_result(taiyin_c_internal::invalid_argument());
    context->value.fields.clear(taiyin::runtime::TAIYIN_NATIVE_FIELD_OBSERVER_LOCATION);
    context->value.observer_location = taiyin::runtime::NativeObserverLocation();
    return taiyin_c_internal::pack_call_result(taiyin::TAIYIN_STATUS_OK);
}

taiyin_call_result TAIYIN_C_CALL taiyin_context_set_atmosphere(
    taiyin_context* context,
    const taiyin_atmosphere* atmosphere
) {
    if (!context || !taiyin_c_internal::valid_struct(atmosphere)) {
        return taiyin_c_internal::pack_call_result(taiyin_c_internal::invalid_argument());
    }
    taiyin::runtime::NativeAtmosphere cpp;
    cpp.pressure_mbar = atmosphere->pressure_mbar;
    cpp.temperature_celsius = atmosphere->temperature_celsius;
    cpp.relative_humidity = atmosphere->relative_humidity_percent;
    cpp.wavelength_micrometer = atmosphere->wavelength_micrometer;
    return taiyin_c_internal::pack_call_result(taiyin::runtime::native_context_set_atmosphere(&context->value, cpp));
}

taiyin_call_result TAIYIN_C_CALL taiyin_context_set_atmosphere_pressure_temperature(
    taiyin_context* context,
    double pressure_mbar,
    double temperature_celsius
) {
    return taiyin_c_internal::pack_call_result(context
        ? taiyin::runtime::native_context_set_atmosphere_pressure_temperature(
            &context->value, pressure_mbar, temperature_celsius)
        : taiyin_c_internal::invalid_argument());
}

taiyin_call_result TAIYIN_C_CALL taiyin_context_set_standard_atmosphere(
    taiyin_context* context
) {
    if (!context) return taiyin_c_internal::pack_call_result(taiyin_c_internal::invalid_argument());
    const taiyin::runtime::NativeAtmosphere atmosphere =
        taiyin::runtime::native_standard_atmosphere();
    return taiyin_c_internal::pack_call_result(taiyin::runtime::native_context_set_atmosphere(
        &context->value, atmosphere));
}

taiyin_call_result TAIYIN_C_CALL taiyin_context_set_atmosphere_policy(
    taiyin_context* context,
    uint32_t flags
) {
    return taiyin_c_internal::pack_call_result(context
        ? taiyin::runtime::native_context_set_atmosphere_policy_flags(
            &context->value, flags)
        : taiyin_c_internal::invalid_argument());
}

taiyin_call_result TAIYIN_C_CALL taiyin_context_set_meteorological_range_km(
    taiyin_context* context,
    double range_km
) {
    return taiyin_c_internal::pack_call_result(context
        ? taiyin::runtime::native_context_set_meteorological_range_km(
            &context->value, range_km)
        : taiyin_c_internal::invalid_argument());
}

taiyin_call_result TAIYIN_C_CALL taiyin_context_set_geocentric_observer(
    taiyin_context* context,
    int32_t observer_id,
    int32_t center_id
) {
    if (!context) return taiyin_c_internal::pack_call_result(taiyin_c_internal::invalid_argument());
    const taiyin::Status status =
        taiyin::runtime::native_context_set_geocentric_observer(
            &context->value, observer_id, center_id);
    if (status == taiyin::TAIYIN_STATUS_OK) {
        taiyin_c_internal::repair_context_pointers(context);
    }
    return taiyin_c_internal::pack_call_result(status);
}

taiyin_call_result TAIYIN_C_CALL taiyin_context_set_topocentric_observer_offset(
    taiyin_context* context,
    const taiyin_cartesian_state* observer_offset
) {
    if (!context || !taiyin_c_internal::valid_struct(observer_offset)) {
        return taiyin_c_internal::pack_call_result(taiyin_c_internal::invalid_argument());
    }
    if (!valid_state(*observer_offset)) {
        return taiyin_c_internal::pack_call_result(taiyin_c_internal::invalid_argument());
    }
    return taiyin_c_internal::pack_call_result(taiyin::runtime::native_context_set_topocentric_observer_offset(
        &context->value, taiyin_c_internal::to_cpp_state(*observer_offset)));
}

taiyin_call_result TAIYIN_C_CALL taiyin_context_set_simple_topocentric_observer(
    taiyin_context* context,
    const taiyin_observer_location* location,
    const taiyin_split_julian_date* jd_ut1,
    const taiyin_split_julian_date* jd_tt
) {
    if (!context || !taiyin_c_internal::valid_struct(location)
        || !taiyin_c_internal::valid_split_jd(jd_ut1)
        || !taiyin_c_internal::valid_split_jd(jd_tt)) {
        return taiyin_c_internal::pack_call_result(taiyin_c_internal::invalid_argument());
    }
    return taiyin_c_internal::pack_call_result(taiyin::runtime::native_context_set_simple_topocentric_observer(
        &context->value, to_cpp_location(*location),
        taiyin_c_internal::to_cpp_split_jd(*jd_ut1),
        taiyin_c_internal::to_cpp_split_jd(*jd_tt)));
}

taiyin_call_result TAIYIN_C_CALL taiyin_context_set_precise_topocentric_observer(
    taiyin_context* context,
    const taiyin_observer_location* location,
    const taiyin_split_julian_date* jd_utc,
    const taiyin_split_julian_date* jd_tt
) {
    if (!context || !taiyin_c_internal::valid_struct(location)
        || !taiyin_c_internal::valid_split_jd(jd_utc)
        || !taiyin_c_internal::valid_split_jd(jd_tt)) {
        return taiyin_c_internal::pack_call_result(taiyin_c_internal::invalid_argument());
    }
    return taiyin_c_internal::pack_call_result(taiyin::runtime::native_context_set_precise_topocentric_observer(
        &context->value, to_cpp_location(*location),
        taiyin_c_internal::to_cpp_split_jd(*jd_utc),
        taiyin_c_internal::to_cpp_split_jd(*jd_tt)));
}

taiyin_call_result TAIYIN_C_CALL taiyin_context_set_route_rule(
    taiyin_context* context,
    uint64_t route_rule_id
) {
    return taiyin_c_internal::pack_call_result(context
        ? taiyin::runtime::native_context_set_route_rule(
            &context->value, route_rule_id)
        : taiyin_c_internal::invalid_argument());
}

taiyin_call_result TAIYIN_C_CALL taiyin_context_set_allow_utc_out_of_range_estimate(
    taiyin_context* context,
    taiyin_bool allow
) {
    if (!context) {
        return taiyin_c_internal::pack_call_result(taiyin_c_internal::invalid_argument());
    }
    return taiyin_c_internal::pack_call_result(taiyin::runtime::native_context_set_allow_utc_out_of_range_estimate(
        &context->value, allow != 0u));
}

taiyin_call_result TAIYIN_C_CALL taiyin_context_set_delta_t_model(
    taiyin_context* context,
    int32_t delta_t_model_id,
    int32_t ephemeris_family_id
) {
    return taiyin_c_internal::pack_call_result(context && valid_delta_t_model(
            delta_t_model_id, ephemeris_family_id)
        ? taiyin::runtime::native_context_set_delta_t_model(
            &context->value, delta_t_model_id, ephemeris_family_id)
        : taiyin_c_internal::invalid_argument());
}

taiyin_call_result TAIYIN_C_CALL taiyin_context_set_tdb_model(
    taiyin_context* context,
    int32_t tdb_model_id
) {
    return taiyin_c_internal::pack_call_result(context
            && (tdb_model_id == TAIYIN_TDB_FAST_PERIODIC
                || tdb_model_id == TAIYIN_TDB_SOFA_FULL)
        ? taiyin::runtime::native_context_set_tdb_model(
            &context->value, tdb_model_id)
        : taiyin_c_internal::invalid_argument());
}

taiyin_call_result TAIYIN_C_CALL taiyin_context_set_astro_models(
    taiyin_context* context,
    const taiyin_astro_model_config* config
) {
    if (!context || !taiyin_c_internal::valid_struct(config)
        || !valid_astro_models(*config)) {
        return taiyin_c_internal::pack_call_result(taiyin_c_internal::invalid_argument());
    }
    context->value.model_context.tdb_model_id = config->tdb_model_id;
    context->value.model_context.precession_model_id =
        config->precession_model_id;
    context->value.model_context.nutation_model_id = config->nutation_model_id;
    context->value.model_context.obliquity_model_id =
        config->obliquity_model_id;
    context->value.model_context.frame_route_id = config->frame_route_id;
    taiyin_c_internal::repair_context_pointers(context);
    return taiyin_c_internal::pack_call_result(taiyin::TAIYIN_STATUS_OK);
}

taiyin_call_result TAIYIN_C_CALL taiyin_context_set_apparent_config(
    taiyin_context* context,
    const taiyin_apparent_config* config
) {
    if (!context || !taiyin_c_internal::valid_struct(config)
        || !valid_apparent_config(*config)) {
        return taiyin_c_internal::pack_call_result(taiyin_c_internal::invalid_argument());
    }
    taiyin::runtime::ApparentOptions& options =
        context->value.apparent_options;
    const uint32_t topocentric =
        options.flags & taiyin::TAIYIN_APPARENT_TOPOCENTRIC;
    options.flags = config->flags | topocentric;
    options.output_frame_id = config->output_frame_id;
    options.light_time_method_id = config->light_time_method_id;
    options.shapiro_delay_model_id = config->shapiro_delay_model_id;
    options.aberration_model_id = config->aberration_model_id;
    options.deflection_model_id = config->deflection_model_id;
    options.max_light_time_iterations = config->max_light_time_iterations;
    options.light_time_tolerance_days = config->light_time_tolerance_days;
    options.matrix_derivative_step_days =
        config->matrix_derivative_step_days;
    taiyin_c_internal::repair_context_pointers(context);
    return taiyin_c_internal::pack_call_result(taiyin::TAIYIN_STATUS_OK);
}

taiyin_call_result TAIYIN_C_CALL taiyin_context_set_celestial_pole_offset(
    taiyin_context* context,
    double dx_rad,
    double dy_rad,
    double dx_rate_rad_per_day,
    double dy_rate_rad_per_day
) {
    return taiyin_c_internal::pack_call_result(context
        ? taiyin::runtime::native_context_set_celestial_pole_offset(
            &context->value,
            dx_rad,
            dy_rad,
            dx_rate_rad_per_day,
            dy_rate_rad_per_day)
        : taiyin_c_internal::invalid_argument());
}

taiyin_call_result TAIYIN_C_CALL taiyin_context_set_refraction_model(
    taiyin_context* context,
    int32_t refraction_model_id
) {
    return taiyin_c_internal::pack_call_result(context
        ? taiyin::runtime::native_context_set_refraction_model(
            &context->value, refraction_model_id)
        : taiyin_c_internal::invalid_argument());
}

taiyin_call_result TAIYIN_C_CALL taiyin_context_set_heliacal_visibility_model(
    taiyin_context* context,
    int32_t model_id
) {
    return taiyin_c_internal::pack_call_result(context
        ? taiyin::runtime::native_context_set_heliacal_visibility_model(
            &context->value, model_id)
        : taiyin_c_internal::invalid_argument());
}

taiyin_call_result TAIYIN_C_CALL taiyin_context_use_solar_deflector(
    taiyin_context* context
) {
    if (!context) return taiyin_c_internal::pack_call_result(taiyin_c_internal::invalid_argument());
    context->deflectors.clear();
    return taiyin_c_internal::pack_call_result(taiyin::runtime::native_context_use_solar_deflector(&context->value));
}

taiyin_call_result TAIYIN_C_CALL taiyin_context_clear_deflectors(
    taiyin_context* context
) {
    if (!context) return taiyin_c_internal::pack_call_result(taiyin_c_internal::invalid_argument());
    context->deflectors.clear();
    return taiyin_c_internal::pack_call_result(taiyin::runtime::native_context_clear_deflectors(&context->value));
}

taiyin_call_result TAIYIN_C_CALL taiyin_context_set_deflectors(
    taiyin_context* context,
    const taiyin_apparent_deflector* deflectors,
    size_t deflector_count,
    int32_t solar_deflector_index
) {
    if (!context || (!deflectors && deflector_count > 0)
        || solar_deflector_index < -1
        || (solar_deflector_index >= 0
            && static_cast<size_t>(solar_deflector_index) >= deflector_count)) {
        return taiyin_c_internal::pack_call_result(taiyin_c_internal::invalid_argument());
    }
    std::vector<taiyin::runtime::ApparentDeflector> replacement;
    try {
        replacement.reserve(deflector_count);
        for (size_t i = 0; i < deflector_count; ++i) {
            if (!taiyin_c_internal::valid_struct(&deflectors[i])
                || !std::isfinite(deflectors[i].schwarzschild_radius_au)
                || deflectors[i].schwarzschild_radius_au < 0.0
                || !std::isfinite(deflectors[i].limit)
                || deflectors[i].limit < 0.0) {
                return taiyin_c_internal::pack_call_result(taiyin_c_internal::invalid_argument());
            }
            taiyin::runtime::ApparentDeflector value;
            value.body_id = deflectors[i].body_id;
            value.schwarzschild_radius_au =
                deflectors[i].schwarzschild_radius_au;
            value.limit = deflectors[i].limit;
            replacement.push_back(value);
        }
    } catch (...) {
        return taiyin_c_internal::pack_call_result(taiyin::TAIYIN_ERROR_OUT_OF_MEMORY);
    }
    const taiyin::Status status = taiyin::runtime::native_context_set_deflectors(
        &context->value,
        replacement.empty() ? 0 : replacement.data(),
        replacement.size(),
        solar_deflector_index);
    if (status != taiyin::TAIYIN_STATUS_OK) return taiyin_c_internal::pack_call_result(status);
    context->deflectors.swap(replacement);
    taiyin_c_internal::repair_context_pointers(context);
    return taiyin_c_internal::pack_call_result(status);
}

taiyin_call_result TAIYIN_C_CALL taiyin_context_set_light_time_iteration(
    taiyin_context* context,
    int32_t max_iterations,
    double tolerance_days
) {
    if (!context || max_iterations < 0 || !std::isfinite(tolerance_days)
        || tolerance_days < 0.0) {
        return taiyin_c_internal::pack_call_result(taiyin_c_internal::invalid_argument());
    }
    return taiyin_c_internal::pack_call_result(taiyin::runtime::native_context_set_light_time_iteration(
        &context->value, max_iterations, tolerance_days));
}

taiyin_call_result TAIYIN_C_CALL taiyin_context_enable_shapiro_delay(
    taiyin_context* context,
    int32_t shapiro_delay_model_id
) {
    if (!context || shapiro_delay_model_id != 0) {
        return taiyin_c_internal::pack_call_result(taiyin_c_internal::invalid_argument());
    }
    return taiyin_c_internal::pack_call_result(taiyin::runtime::native_context_enable_shapiro_delay(
        &context->value, shapiro_delay_model_id));
}

taiyin_call_result TAIYIN_C_CALL taiyin_context_disable_shapiro_delay(
    taiyin_context* context
) {
    return taiyin_c_internal::pack_call_result(context
        ? taiyin::runtime::native_context_disable_shapiro_delay(
            &context->value)
        : taiyin_c_internal::invalid_argument());
}

taiyin_call_result TAIYIN_C_CALL taiyin_context_set_eclipse_models(
    taiyin_context* context,
    int32_t shadow_model_id,
    int32_t moon_radius_model_id
) {
    if (!context) return taiyin_c_internal::pack_call_result(taiyin_c_internal::invalid_argument());
    taiyin::dispatch::EclipseShadowModelEntry shadow;
    taiyin::dispatch::EclipseMoonRadiusModelEntry moon;
    if (shadow_model_id < 0 || shadow_model_id > 255
        || moon_radius_model_id < 0 || moon_radius_model_id > 255
        || !taiyin::dispatch::select_eclipse_shadow_model(
            shadow_model_id, &shadow)
        || !taiyin::dispatch::find_eclipse_moon_radius_model(
            moon_radius_model_id, &moon)) {
        return taiyin_c_internal::pack_call_result(taiyin_c_internal::invalid_argument());
    }
    const taiyin::Status status =
        taiyin::runtime::native_context_set_eclipse_shadow_model(
            &context->value, shadow_model_id);
    if (status != taiyin::TAIYIN_STATUS_OK) return taiyin_c_internal::pack_call_result(status);
    return taiyin_c_internal::pack_call_result(taiyin::runtime::native_context_set_eclipse_moon_radius_model(
        &context->value, moon_radius_model_id));
}

}  // extern "C"
