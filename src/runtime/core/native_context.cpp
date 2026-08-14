#include "taiyin/runtime/native_context.h"

#include "runtime/core/native_context_checks.h"
#include "taiyin/runtime/runtime.h"

#include "taiyin/angle.h"
#include "taiyin/body_id.h"
#include "taiyin/coordinates.h"
#include "taiyin/corrections.h"
#include "taiyin/dispatch.h"
#include "taiyin/internal/eop.h"
#include "taiyin/observer.h"

#include <cmath>
#include <limits>
#include <mutex>
#include <vector>

namespace taiyin {
namespace runtime {
namespace {

struct DefaultNativeCalcContextManager {
    NativeCalcContext context;
    std::vector<ApparentDeflector> deflectors;
    std::mutex mutex;

    DefaultNativeCalcContextManager() noexcept : context(), deflectors(), mutex() {}
};

DefaultNativeCalcContextManager& default_native_calc_context_manager() noexcept {
    static DefaultNativeCalcContextManager manager;
    return manager;
}

bool valid_solar_deflector_index(size_t deflector_count, int solar_deflector_index) noexcept {
    return solar_deflector_index < 0
        || static_cast<size_t>(solar_deflector_index) < deflector_count;
}

const uint32_t ATMOSPHERE_REQUIRES_PRESSURE = 1u << 0;
const uint32_t ATMOSPHERE_REQUIRES_TEMPERATURE = 1u << 1;
const uint32_t ATMOSPHERE_REQUIRES_HUMIDITY = 1u << 2;
const uint32_t ATMOSPHERE_REQUIRES_WAVELENGTH = 1u << 3;

Status copy_deflectors(
    const ApparentDeflector* deflectors,
    size_t deflector_count,
    int solar_deflector_index,
    std::vector<ApparentDeflector>* out
) noexcept {
    if (!out || (!deflectors && deflector_count > 0) || !valid_solar_deflector_index(deflector_count, solar_deflector_index)) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    try {
        out->clear();
        for (size_t i = 0; i < deflector_count; ++i) {
            out->push_back(deflectors[i]);
        }
    } catch (...) {
        out->clear();
        return TAIYIN_ERROR_OUT_OF_MEMORY;
    }
    return TAIYIN_STATUS_OK;
}

struct CirsToIcrfMatrixEvalData {
    double celestial_pole_offset_dx_rad;
    double celestial_pole_offset_dy_rad;
};

bool simple_topocentric_to_icrf_matrix(const SplitJulianDate& jd_tt, Matrix3x3* out_matrix) noexcept {
    if (!out_matrix) {
        return false;
    }
    Matrix3x3 precession;
    if (!dispatch::eval_selected_precession(dispatch::MODEL_SELECTION_DEFAULT, jd_tt, 0, &precession)) {
        return false;
    }
    *out_matrix = matrix3x3_transpose(precession);
    return true;
}

bool simple_topocentric_to_icrf_matrix_eval(const SplitJulianDate& jd_tt, const void*, Matrix3x3* out_matrix) noexcept {
    return simple_topocentric_to_icrf_matrix(jd_tt, out_matrix);
}

bool cirs_to_icrf_matrix_eval(const SplitJulianDate& jd_tt, const void* data, Matrix3x3* out_matrix) noexcept {
    if (!out_matrix) {
        return false;
    }
    const CirsToIcrfMatrixEvalData* eval_data = static_cast<const CirsToIcrfMatrixEvalData*>(data);
    Matrix3x3 icrf_to_cirs;
    if (!cirs_matrix_iau2006a(
            jd_tt,
            eval_data ? eval_data->celestial_pole_offset_dx_rad : 0.0,
            eval_data ? eval_data->celestial_pole_offset_dy_rad : 0.0,
            &icrf_to_cirs)) {
        return false;
    }
    *out_matrix = matrix3x3_transpose(icrf_to_cirs);
    return true;
}

bool transform_topocentric_offset_to_icrf(
    const SplitJulianDate& jd_tt,
    MatrixEvalFn matrix_eval,
    const void* matrix_eval_data,
    CartesianState* offset
) noexcept {
    if (!offset || !matrix_eval) {
        return false;
    }

    Matrix3x3 to_icrf;
    if (!matrix_eval(jd_tt, matrix_eval_data, &to_icrf)) {
        return false;
    }
    const Vector3 frame_position = offset->position_au;
    const Vector3 frame_velocity = offset->velocity_au_per_day;
    const Vector3 frame_acceleration = offset->acceleration_au_per_day2;
    const bool has_velocity = frame_velocity.x != 0.0 || frame_velocity.y != 0.0 || frame_velocity.z != 0.0;
    const bool has_acceleration = frame_acceleration.x != 0.0 || frame_acceleration.y != 0.0 || frame_acceleration.z != 0.0;

    offset->position_au = matrix3x3_multiply_vector(to_icrf, frame_position);

    if (has_velocity || has_acceleration) {
        Matrix3x3 to_icrf_dot;
        if (!matrix_derivative_central(
                matrix_eval,
                matrix_eval_data,
                jd_tt,
                1.0e-3,
                &to_icrf_dot)) {
            return false;
        }
        offset->velocity_au_per_day = transform_velocity_with_matrix(
            frame_position,
            frame_velocity,
            to_icrf,
            to_icrf_dot);

        if (has_acceleration) {
            Matrix3x3 to_icrf_ddot;
            if (!matrix_second_derivative_central(
                    matrix_eval,
                    matrix_eval_data,
                    jd_tt,
                    1.0e-3,
                    &to_icrf_ddot)) {
                return false;
            }
            offset->acceleration_au_per_day2 = transform_acceleration_with_matrix(
                frame_position,
                frame_velocity,
                frame_acceleration,
                to_icrf,
                to_icrf_dot,
                to_icrf_ddot);
        }
    }
    return true;
}

bool transform_simple_topocentric_offset_to_icrf(
    const SplitJulianDate& jd_tt,
    CartesianState* offset
) noexcept {
    return transform_topocentric_offset_to_icrf(
        jd_tt,
        &simple_topocentric_to_icrf_matrix_eval,
        0,
        offset);
}

bool transform_cirs_topocentric_offset_to_icrf(
    const SplitJulianDate& jd_tt,
    double celestial_pole_offset_dx_rad,
    double celestial_pole_offset_dy_rad,
    CartesianState* offset
) noexcept {
    const CirsToIcrfMatrixEvalData eval_data = {
        celestial_pole_offset_dx_rad,
        celestial_pole_offset_dy_rad,
    };
    return transform_topocentric_offset_to_icrf(
        jd_tt,
        &cirs_to_icrf_matrix_eval,
        &eval_data,
        offset);
}

}  // namespace

NativeObserverLocation::NativeObserverLocation() noexcept
    : longitude_rad(0.0),
      latitude_rad(0.0),
      height_m(0.0) {}

NativeAtmosphere::NativeAtmosphere() noexcept
    : pressure_mbar(0.0),
      temperature_celsius(0.0),
      relative_humidity(0.0),
      wavelength_micrometer(0.0) {}

NativeCalcContext::NativeCalcContext() noexcept
    : fields(),
      model_context(),
      apparent_options(),
      allow_utc_out_of_range_estimate(false),
      delta_t_model_id(dispatch::DELTA_T_ESTIMATED_DEFAULT),
      ephemeris_family_id(dispatch::EPHEMERIS_FAMILY_UNKNOWN),
      observer_id(TAIYIN_BODY_EARTH),
      center_id(TAIYIN_BODY_SUN),
      topocentric_observer_model(TAIYIN_NATIVE_TOPOCENTRIC_OBSERVER_NONE),
      observer_location(),
      atmosphere(),
      atmosphere_policy_flags(0u),
      meteorological_range_km(std::numeric_limits<double>::quiet_NaN()),
      refraction_model_id(dispatch::REFRACTION_BENNETT),
      heliacal_visibility_model_id(dispatch::HELIACAL_VISIBILITY_SCHAEFER_1993),
      eclipse_shadow_model_id(static_cast<uint8_t>(dispatch::ECLIPSE_SHADOW_NASA_DANJON)),
      eclipse_moon_radius_model_id(static_cast<uint8_t>(dispatch::ECLIPSE_MOON_ALMANAC)),
      route_rule_id(TAIYIN_EPHEMERIS_ROUTE_AUTO),
      route_rules(0) {
    apparent_options.model_context = &model_context;
    apparent_options.flags |= TAIYIN_APPARENT_ABERRATION
        | TAIYIN_APPARENT_DEFLECTION;
    native_context_use_solar_deflector(this);
    route_rules = global_ephemeris_route_rule(route_rule_id);
}

NativeObserverLocation native_observer_location_degrees(
    double longitude_deg,
    double latitude_deg,
    double height_m
) noexcept {
    NativeObserverLocation location;
    location.longitude_rad = longitude_deg * TAIYIN_DEG_TO_RAD;
    location.latitude_rad = latitude_deg * TAIYIN_DEG_TO_RAD;
    location.height_m = height_m;
    return location;
}

NativeAtmosphere native_standard_atmosphere() noexcept {
    NativeAtmosphere atmosphere;
    atmosphere.pressure_mbar = 1013.25;
    atmosphere.temperature_celsius = 15.0;
    atmosphere.relative_humidity = 0.0;
    atmosphere.wavelength_micrometer = 0.55;
    return atmosphere;
}

bool native_observer_location_is_finite(const NativeObserverLocation& location) noexcept {
    return std::isfinite(location.longitude_rad)
        && std::isfinite(location.latitude_rad)
        && std::isfinite(location.height_m);
}

static bool native_observer_location_is_valid_geodetic(const NativeObserverLocation& location) noexcept {
    return native_observer_location_is_finite(location)
        && location.latitude_rad >= -0.5 * TAIYIN_PI
        && location.latitude_rad <= 0.5 * TAIYIN_PI;
}

bool native_context_has_observer_location(const NativeCalcContext& context) noexcept {
    return context.fields.has(TAIYIN_NATIVE_FIELD_OBSERVER_LOCATION)
        && native_observer_location_is_valid_geodetic(context.observer_location);
}

bool native_context_observer_degrees(
    const NativeCalcContext& context,
    double* out_longitude_deg,
    double* out_latitude_deg,
    double* out_height_m
) noexcept {
    if (!out_longitude_deg || !out_latitude_deg || !out_height_m
        || !native_context_has_observer_location(context)) {
        return false;
    }
    *out_longitude_deg = context.observer_location.longitude_rad * TAIYIN_RAD_TO_DEG;
    *out_latitude_deg = context.observer_location.latitude_rad * TAIYIN_RAD_TO_DEG;
    *out_height_m = context.observer_location.height_m;
    return true;
}

Status native_context_copy_geocentric_with_observer(
    const NativeCalcContext& context,
    NativeCalcContext* out
) noexcept {
    if (!out || !native_context_has_observer_location(context)) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    const NativeObserverLocation location = context.observer_location;
    *out = context;
    // This clears topocentric apparent state and observer-location bits, so the
    // validated geographic location must be restored afterwards.
    Status status = native_context_set_geocentric_observer(
        out,
        TAIYIN_BODY_EARTH,
        TAIYIN_BODY_EARTH);
    if (status != TAIYIN_STATUS_OK) return status;
    return native_context_set_observer_location(out, location);
}

bool native_cartesian_state_is_finite(const CartesianState& state) noexcept {
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

uint32_t native_required_atmosphere_fields_for_refraction_model(int refraction_model_id) noexcept {
    switch (refraction_model_id) {
    case dispatch::REFRACTION_BENNETT:
    case dispatch::REFRACTION_SKYFIELD:
    case dispatch::REFRACTION_HYBRID:
    case dispatch::REFRACTION_AUER_STANDISH:
        return ATMOSPHERE_REQUIRES_PRESSURE | ATMOSPHERE_REQUIRES_TEMPERATURE;
    case dispatch::REFRACTION_SOFA:
        return ATMOSPHERE_REQUIRES_PRESSURE
            | ATMOSPHERE_REQUIRES_TEMPERATURE
            | ATMOSPHERE_REQUIRES_HUMIDITY
            | ATMOSPHERE_REQUIRES_WAVELENGTH;
    default:
        return 0u;
    }
}

bool native_context_has_atmosphere_fields(
    const NativeCalcContext& context,
    uint32_t required_fields
) noexcept {
    if ((required_fields & ATMOSPHERE_REQUIRES_PRESSURE) != 0u
        && !context.fields.has(TAIYIN_NATIVE_FIELD_ATMOSPHERE_PRESSURE)) {
        return false;
    }
    if ((required_fields & ATMOSPHERE_REQUIRES_TEMPERATURE) != 0u
        && !context.fields.has(TAIYIN_NATIVE_FIELD_ATMOSPHERE_TEMPERATURE)) {
        return false;
    }
    if ((required_fields & ATMOSPHERE_REQUIRES_HUMIDITY) != 0u
        && !context.fields.has(TAIYIN_NATIVE_FIELD_ATMOSPHERE_HUMIDITY)) {
        return false;
    }
    if ((required_fields & ATMOSPHERE_REQUIRES_WAVELENGTH) != 0u
        && !context.fields.has(TAIYIN_NATIVE_FIELD_ATMOSPHERE_WAVELENGTH)) {
        return false;
    }
    return true;
}

bool native_context_resolve_refraction_atmosphere(
    const NativeCalcContext& context,
    bool allow_standard_fallback,
    NativeAtmosphere* out
) noexcept {
    if (!out) return false;
    const uint32_t required_fields =
        native_required_atmosphere_fields_for_refraction_model(context.refraction_model_id);
    if (required_fields == 0u) {
        *out = context.atmosphere;
        return true;
    }
    if (native_context_has_atmosphere_fields(context, required_fields)) {
        *out = context.atmosphere;
        return true;
    }
    if (!allow_standard_fallback
        || !context.fields.has(TAIYIN_NATIVE_FIELD_OBSERVER_LOCATION)
        || !native_observer_location_is_finite(context.observer_location)) {
        return false;
    }
    const double height_m = context.observer_location.height_m;
    const double pressure_base = 1.0 - 0.0065 * height_m / 288.15;
    if (!(pressure_base > 0.0)) return false;

    NativeAtmosphere resolved = context.atmosphere;
    if ((required_fields & ATMOSPHERE_REQUIRES_PRESSURE) != 0u
        && !context.fields.has(TAIYIN_NATIVE_FIELD_ATMOSPHERE_PRESSURE)) {
        resolved.pressure_mbar = 1013.25 * std::pow(pressure_base, 5.255);
    }
    if ((required_fields & ATMOSPHERE_REQUIRES_TEMPERATURE) != 0u
        && !context.fields.has(TAIYIN_NATIVE_FIELD_ATMOSPHERE_TEMPERATURE)) {
        resolved.temperature_celsius = 15.0 - 0.0065 * height_m;
    }
    if ((required_fields & ATMOSPHERE_REQUIRES_HUMIDITY) != 0u
        && !context.fields.has(TAIYIN_NATIVE_FIELD_ATMOSPHERE_HUMIDITY)) {
        resolved.relative_humidity = 40.0;
    }
    if ((required_fields & ATMOSPHERE_REQUIRES_WAVELENGTH) != 0u
        && !context.fields.has(TAIYIN_NATIVE_FIELD_ATMOSPHERE_WAVELENGTH)) {
        resolved.wavelength_micrometer = 0.55;
    }
    if (!std::isfinite(resolved.pressure_mbar) || !(resolved.pressure_mbar > 0.0)
        || !std::isfinite(resolved.temperature_celsius)
        || ((required_fields & ATMOSPHERE_REQUIRES_HUMIDITY) != 0u
            && (!std::isfinite(resolved.relative_humidity)
                || !(resolved.relative_humidity >= 0.0)
                || !(resolved.relative_humidity <= 100.0)))
        || ((required_fields & ATMOSPHERE_REQUIRES_WAVELENGTH) != 0u
            && (!std::isfinite(resolved.wavelength_micrometer)
                || !(resolved.wavelength_micrometer > 0.0)))) {
        return false;
    }
    *out = resolved;
    return true;
}

bool native_refraction_model_from_id(int refraction_model_id, RefractionModel* out) noexcept {
    if (!out) {
        return false;
    }
    switch (refraction_model_id) {
    case dispatch::REFRACTION_BENNETT:
        *out = RefractionModel::Bennett;
        return true;
    case dispatch::REFRACTION_SKYFIELD:
        *out = RefractionModel::Skyfield;
        return true;
    case dispatch::REFRACTION_HYBRID:
        *out = RefractionModel::Hybrid;
        return true;
    case dispatch::REFRACTION_AUER_STANDISH:
        *out = RefractionModel::AuerStandish;
        return true;
    case dispatch::REFRACTION_SOFA:
        *out = RefractionModel::Sofa;
        return true;
    default:
        return false;
    }
}

Status native_context_set_observer_location(
    NativeCalcContext* context,
    const NativeObserverLocation& location
) noexcept {
    if (!context || !native_observer_location_is_valid_geodetic(location)) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    context->observer_location = location;
    context->fields.set(TAIYIN_NATIVE_FIELD_OBSERVER_LOCATION);
    return TAIYIN_STATUS_OK;
}

Status native_context_set_atmosphere_pressure_temperature(
    NativeCalcContext* context,
    double pressure_mbar,
    double temperature_celsius
) noexcept {
    if (!context || !std::isfinite(pressure_mbar) || !std::isfinite(temperature_celsius)) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    context->atmosphere.pressure_mbar = pressure_mbar;
    context->atmosphere.temperature_celsius = temperature_celsius;
    context->atmosphere.relative_humidity = 0.0;
    context->atmosphere.wavelength_micrometer = 0.0;
    context->fields.set(TAIYIN_NATIVE_FIELD_ATMOSPHERE_PRESSURE);
    context->fields.set(TAIYIN_NATIVE_FIELD_ATMOSPHERE_TEMPERATURE);
    context->fields.clear(TAIYIN_NATIVE_FIELD_ATMOSPHERE_HUMIDITY);
    context->fields.clear(TAIYIN_NATIVE_FIELD_ATMOSPHERE_WAVELENGTH);
    context->fields.clear(TAIYIN_NATIVE_FIELD_ATMOSPHERE);
    return TAIYIN_STATUS_OK;
}

Status native_context_set_atmosphere(
    NativeCalcContext* context,
    const NativeAtmosphere& atmosphere
) noexcept {
    if (!context
        || !std::isfinite(atmosphere.pressure_mbar)
        || !std::isfinite(atmosphere.temperature_celsius)
        || !std::isfinite(atmosphere.relative_humidity)
        || !std::isfinite(atmosphere.wavelength_micrometer)) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    context->atmosphere = atmosphere;
    context->fields.set(TAIYIN_NATIVE_FIELD_ATMOSPHERE_PRESSURE);
    context->fields.set(TAIYIN_NATIVE_FIELD_ATMOSPHERE_TEMPERATURE);
    context->fields.set(TAIYIN_NATIVE_FIELD_ATMOSPHERE_HUMIDITY);
    context->fields.set(TAIYIN_NATIVE_FIELD_ATMOSPHERE_WAVELENGTH);
    context->fields.set(TAIYIN_NATIVE_FIELD_ATMOSPHERE);
    return TAIYIN_STATUS_OK;
}

Status native_context_set_meteorological_range_km(
    NativeCalcContext* context,
    double meteorological_range_km
) noexcept {
    if (!context || !std::isfinite(meteorological_range_km)
        || !(meteorological_range_km >= 1.0)) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    context->meteorological_range_km = meteorological_range_km;
    context->fields.set(TAIYIN_NATIVE_FIELD_METEOROLOGICAL_RANGE);
    return TAIYIN_STATUS_OK;
}

Status native_context_set_atmosphere_policy_flags(
    NativeCalcContext* context,
    uint32_t atmosphere_policy_flags
) noexcept {
    if (!context
        || (atmosphere_policy_flags & ~TAIYIN_NATIVE_ATMOSPHERE_ALLOW_STANDARD_FALLBACK) != 0u) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    context->atmosphere_policy_flags = atmosphere_policy_flags;
    return TAIYIN_STATUS_OK;
}

Status native_context_set_refraction_model(
    NativeCalcContext* context,
    int refraction_model_id
) noexcept {
    if (!context) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    context->refraction_model_id = refraction_model_id;
    return TAIYIN_STATUS_OK;
}

Status native_context_set_heliacal_visibility_model(
    NativeCalcContext* context,
    int heliacal_visibility_model_id
) noexcept {
    if (!context || !dispatch::has_heliacal_visibility_model(heliacal_visibility_model_id)) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    context->heliacal_visibility_model_id = heliacal_visibility_model_id;
    return TAIYIN_STATUS_OK;
}

Status native_context_set_delta_t_model(
    NativeCalcContext* context,
    int delta_t_model_id,
    int ephemeris_family_id
) noexcept {
    if (!context) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    context->delta_t_model_id = delta_t_model_id;
    context->ephemeris_family_id = ephemeris_family_id;
    return TAIYIN_STATUS_OK;
}

Status native_context_set_tdb_model(NativeCalcContext* context, int tdb_model_id) noexcept {
    if (!context) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    context->model_context.tdb_model_id = tdb_model_id;
    context->apparent_options.model_context = &context->model_context;
    return TAIYIN_STATUS_OK;
}

Status native_context_set_allow_utc_out_of_range_estimate(
    NativeCalcContext* context,
    bool allow
) noexcept {
    if (!context) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    context->allow_utc_out_of_range_estimate = allow;
    return TAIYIN_STATUS_OK;
}

Status native_context_set_route_rule(NativeCalcContext* context, uint64_t route_rule_id) noexcept {
    if (!context) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    const internal::EphemerisRouteRuleTable* rules = global_ephemeris_route_rule(route_rule_id);
    if (!rules) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    context->route_rule_id = route_rule_id;
    context->route_rules = rules;
    return TAIYIN_STATUS_OK;
}

Status native_context_set_celestial_pole_offset(
    NativeCalcContext* context,
    double dx_rad,
    double dy_rad,
    double dx_rate_rad_per_day,
    double dy_rate_rad_per_day
) noexcept {
    if (!context
        || !std::isfinite(dx_rad)
        || !std::isfinite(dy_rad)
        || !std::isfinite(dx_rate_rad_per_day)
        || !std::isfinite(dy_rate_rad_per_day)) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    context->apparent_options.celestial_pole_offset_dx_rad = dx_rad;
    context->apparent_options.celestial_pole_offset_dy_rad = dy_rad;
    context->apparent_options.celestial_pole_offset_dx_rate_rad_per_day = dx_rate_rad_per_day;
    context->apparent_options.celestial_pole_offset_dy_rate_rad_per_day = dy_rate_rad_per_day;
    context->fields.set(TAIYIN_NATIVE_FIELD_CELESTIAL_POLE_OFFSET);
    return TAIYIN_STATUS_OK;
}

Status native_context_set_geocentric_observer(
    NativeCalcContext* context,
    int observer_id,
    int center_id
) noexcept {
    if (!context) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    context->observer_id = observer_id;
    context->center_id = center_id;
    context->apparent_options.flags &= ~TAIYIN_APPARENT_TOPOCENTRIC;
    context->apparent_options.observer_offset = CartesianState();
    context->topocentric_observer_model = TAIYIN_NATIVE_TOPOCENTRIC_OBSERVER_NONE;
    context->fields.clear(TAIYIN_NATIVE_FIELD_TOPOCENTRIC_OFFSET);
    context->fields.clear(TAIYIN_NATIVE_FIELD_OBSERVER_LOCATION);
    return TAIYIN_STATUS_OK;
}

Status native_context_set_topocentric_observer_offset(
    NativeCalcContext* context,
    const CartesianState& observer_offset
) noexcept {
    if (!context) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    if (context->observer_id != TAIYIN_BODY_EARTH) {
        return TAIYIN_ERROR_UNSUPPORTED;
    }
    context->apparent_options.observer_offset = observer_offset;
    context->apparent_options.flags |= TAIYIN_APPARENT_TOPOCENTRIC;
    context->topocentric_observer_model = TAIYIN_NATIVE_TOPOCENTRIC_OBSERVER_EXPLICIT_OFFSET;
    context->fields.set(TAIYIN_NATIVE_FIELD_TOPOCENTRIC_OFFSET);
    return TAIYIN_STATUS_OK;
}

Status native_context_set_simple_topocentric_observer(
    NativeCalcContext* context,
    const NativeObserverLocation& location,
    const SplitJulianDate& jd_ut1,
    const SplitJulianDate& jd_tt
) noexcept {
    if (!context || !split_julian_date_is_finite(jd_ut1)
        || !split_julian_date_is_finite(jd_tt)
        || !native_observer_location_is_valid_geodetic(location)) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    if (context->observer_id != TAIYIN_BODY_EARTH) {
        return TAIYIN_ERROR_UNSUPPORTED;
    }
    CartesianState offset;
    offset.position_au = observer_geocentric_simple_position_au(
        location.longitude_rad,
        location.latitude_rad,
        location.height_m,
        jd_ut1,
        jd_tt);
    if (!observer_geocentric_simple_velocity_au_per_day(
            location.longitude_rad,
            location.latitude_rad,
            location.height_m,
            jd_ut1,
            jd_tt,
            &offset.velocity_au_per_day)
        || !observer_geocentric_simple_acceleration_au_per_day2(
            location.longitude_rad,
            location.latitude_rad,
            location.height_m,
            jd_ut1,
            jd_tt,
            &offset.acceleration_au_per_day2)
        || !transform_simple_topocentric_offset_to_icrf(jd_tt, &offset)) {
        return TAIYIN_ERROR_UNSUPPORTED;
    }
    const Status status = native_context_set_topocentric_observer_offset(context, offset);
    if (status == TAIYIN_STATUS_OK) {
        context->topocentric_observer_model = TAIYIN_NATIVE_TOPOCENTRIC_OBSERVER_SIMPLE;
        context->observer_location = location;
        context->fields.set(TAIYIN_NATIVE_FIELD_OBSERVER_LOCATION);
    }
    return status;
}

Status native_context_set_precise_topocentric_observer(
    NativeCalcContext* context,
    const NativeObserverLocation& location,
    const SplitJulianDate& jd_utc,
    const SplitJulianDate& jd_tt
) noexcept {
    if (!context || !split_julian_date_is_finite(jd_utc)
        || !split_julian_date_is_finite(jd_tt)
        || !native_observer_location_is_valid_geodetic(location)) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    if (context->observer_id != TAIYIN_BODY_EARTH) {
        return TAIYIN_ERROR_UNSUPPORTED;
    }
    const internal::EarthOrientationTable* eop_table = global_earth_orientation_table();
    if (!eop_table || eop_table->count == 0) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }

    internal::EarthOrientationSample eop;
    internal::EarthOrientationRates rates;
    internal::EarthRotationDerivatives derivatives;
    if (!internal::interpolate_earth_orientation(eop_table, jd_utc, &eop)
        || !internal::derive_earth_orientation_rates(eop_table, jd_utc, &rates, &derivatives)) {
        return TAIYIN_ERROR_UNSUPPORTED;
    }

    CartesianState offset;
    SplitJulianDate jd_ut1;
    if (!utc_to_ut1_split_jd(jd_utc, eop.dut1_seconds, &jd_ut1)) {
        return TAIYIN_ERROR_UNSUPPORTED;
    }
    if (!observer_geocentric_cirs_position_au(
            location.longitude_rad,
            location.latitude_rad,
            location.height_m,
            jd_ut1,
            eop.xp_rad,
            eop.yp_rad,
            eop.sp_rad,
            &offset.position_au)
        || !observer_geocentric_cirs_velocity_au_per_day(
            location.longitude_rad,
            location.latitude_rad,
            location.height_m,
            jd_ut1,
            eop.xp_rad,
            eop.yp_rad,
            eop.sp_rad,
            rates.xp_rate_rad_per_day,
            rates.yp_rate_rad_per_day,
            rates.sp_rate_rad_per_day,
            derivatives.dut1_rate_seconds_per_day,
            derivatives.lod_seconds,
            &offset.velocity_au_per_day)
        || !observer_geocentric_cirs_acceleration_au_per_day2(
            location.longitude_rad,
            location.latitude_rad,
            location.height_m,
            jd_ut1,
            eop.xp_rad,
            eop.yp_rad,
            eop.sp_rad,
            rates.xp_rate_rad_per_day,
            rates.yp_rate_rad_per_day,
            rates.sp_rate_rad_per_day,
            derivatives.dut1_rate_seconds_per_day,
            derivatives.lod_seconds,
            derivatives.lod_rate_seconds_per_day,
            &offset.acceleration_au_per_day2)
        || !transform_cirs_topocentric_offset_to_icrf(jd_tt, eop.dx_rad, eop.dy_rad, &offset)) {
        return TAIYIN_ERROR_UNSUPPORTED;
    }

    const Status status = native_context_set_topocentric_observer_offset(context, offset);
    if (status == TAIYIN_STATUS_OK) {
        context->topocentric_observer_model = TAIYIN_NATIVE_TOPOCENTRIC_OBSERVER_PRECISE;
        context->observer_location = location;
        context->fields.set(TAIYIN_NATIVE_FIELD_OBSERVER_LOCATION);
    }
    return status;
}

Status native_context_refresh_topocentric_observer(
    NativeCalcContext* context,
    const SplitJulianDate& jd_ut1,
    const SplitJulianDate& jd_tt
) noexcept {
    if (!context || !split_julian_date_is_finite(jd_ut1)
        || !split_julian_date_is_finite(jd_tt)) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    switch (context->topocentric_observer_model) {
    case TAIYIN_NATIVE_TOPOCENTRIC_OBSERVER_NONE:
    case TAIYIN_NATIVE_TOPOCENTRIC_OBSERVER_EXPLICIT_OFFSET:
        return TAIYIN_STATUS_OK;
    case TAIYIN_NATIVE_TOPOCENTRIC_OBSERVER_SIMPLE:
        return native_context_set_simple_topocentric_observer(
            context, context->observer_location, jd_ut1, jd_tt);
    case TAIYIN_NATIVE_TOPOCENTRIC_OBSERVER_PRECISE: {
        const internal::EarthOrientationTable* eop_table =
            global_earth_orientation_table();
        if (!eop_table || eop_table->count == 0) {
            return TAIYIN_ERROR_INVALID_ARGUMENT;
        }
        // UT1 = UTC + DUT1. DUT1 itself is tabulated against UTC, so solve
        // this small implicit conversion before calling the precise setter.
        SplitJulianDate jd_utc = jd_ut1;
        for (int iteration = 0; iteration < 3; ++iteration) {
            internal::EarthOrientationSample eop;
            SplitJulianDate candidate_ut1;
            if (!internal::interpolate_earth_orientation(
                    eop_table, jd_utc, &eop)
                || !utc_to_ut1_split_jd(
                    jd_utc, eop.dut1_seconds, &candidate_ut1)) {
                return TAIYIN_ERROR_UNSUPPORTED;
            }
            jd_utc += jd_ut1 - candidate_ut1;
        }
        return native_context_set_precise_topocentric_observer(
            context, context->observer_location, jd_utc, jd_tt);
    }
    default:
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }
}

const ApparentDeflector* native_solar_deflector() noexcept {
    static const ApparentDeflector solar = []() noexcept {
        ApparentDeflector deflector;
        deflector.body_id = TAIYIN_BODY_SUN;
        deflector.schwarzschild_radius_au = TAIYIN_SOLAR_SCHWARZSCHILD_RADIUS_AU;
        deflector.limit = 0.0;
        return deflector;
    }();
    return &solar;
}

Status native_context_set_deflectors(
    NativeCalcContext* context,
    const ApparentDeflector* deflectors,
    size_t deflector_count,
    int solar_deflector_index
) noexcept {
    if (!context || (!deflectors && deflector_count > 0) || !valid_solar_deflector_index(deflector_count, solar_deflector_index)) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    context->apparent_options.deflectors = deflectors;
    context->apparent_options.deflector_count = deflector_count;
    context->apparent_options.solar_deflector_index = solar_deflector_index;
    if (deflectors && deflector_count > 0) {
        context->fields.set(TAIYIN_NATIVE_FIELD_DEFLECTORS);
    } else {
        context->fields.clear(TAIYIN_NATIVE_FIELD_DEFLECTORS);
    }
    return TAIYIN_STATUS_OK;
}

Status native_context_use_solar_deflector(NativeCalcContext* context) noexcept {
    return native_context_set_deflectors(context, native_solar_deflector(), 1, 0);
}

Status native_context_clear_deflectors(NativeCalcContext* context) noexcept {
    return native_context_set_deflectors(context, 0, 0, -1);
}

Status native_context_set_light_time_iteration(
    NativeCalcContext* context,
    int max_iterations,
    double tolerance_days
) noexcept {
    if (!context || max_iterations < 0 || tolerance_days < 0.0) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    context->apparent_options.max_light_time_iterations = max_iterations;
    context->apparent_options.light_time_tolerance_days = tolerance_days;
    return TAIYIN_STATUS_OK;
}

Status native_context_enable_shapiro_delay(
    NativeCalcContext* context,
    int shapiro_delay_model_id
) noexcept {
    if (!context) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    context->apparent_options.flags |= TAIYIN_APPARENT_LIGHT_TIME | TAIYIN_APPARENT_SHAPIRO_DELAY;
    context->apparent_options.shapiro_delay_model_id = shapiro_delay_model_id;
    return TAIYIN_STATUS_OK;
}

Status native_context_disable_shapiro_delay(NativeCalcContext* context) noexcept {
    if (!context) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    context->apparent_options.flags &= ~TAIYIN_APPARENT_SHAPIRO_DELAY;
    return TAIYIN_STATUS_OK;
}

Status native_context_set_eclipse_shadow_model(
    NativeCalcContext* context,
    int eclipse_shadow_model_id
) noexcept {
    if (!context) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    context->eclipse_shadow_model_id =
        static_cast<uint8_t>(eclipse_shadow_model_id);
    return TAIYIN_STATUS_OK;
}

Status native_context_set_eclipse_moon_radius_model(
    NativeCalcContext* context,
    int eclipse_moon_radius_model_id
) noexcept {
    if (!context) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    context->eclipse_moon_radius_model_id =
        static_cast<uint8_t>(eclipse_moon_radius_model_id);
    return TAIYIN_STATUS_OK;
}

NativeCalcContext get_default_native_calc_context() noexcept {
    DefaultNativeCalcContextManager& manager = default_native_calc_context_manager();
    std::lock_guard<std::mutex> lock(manager.mutex);
    NativeCalcContext context = manager.context;
    context.apparent_options.model_context = &context.model_context;
    if (!manager.deflectors.empty()) {
        context.apparent_options.deflectors = manager.deflectors.data();
        context.apparent_options.deflector_count = manager.deflectors.size();
    } else if (context.apparent_options.deflector_count == 0) {
        context.apparent_options.deflectors = 0;
    }
    return context;
}

Status set_default_native_calc_context(const NativeCalcContext& context) noexcept {
    DefaultNativeCalcContextManager& manager = default_native_calc_context_manager();
    std::lock_guard<std::mutex> lock(manager.mutex);
    manager.context = context;
    manager.context.apparent_options.model_context = &manager.context.model_context;
    const Status status = copy_deflectors(
        context.apparent_options.deflectors,
        context.apparent_options.deflector_count,
        context.apparent_options.solar_deflector_index,
        &manager.deflectors);
    if (status != TAIYIN_STATUS_OK) {
        manager.context = NativeCalcContext();
        manager.deflectors.clear();
        return status;
    }
    manager.context.apparent_options.deflectors = manager.deflectors.empty()
        ? 0 : manager.deflectors.data();
    manager.context.apparent_options.deflector_count = manager.deflectors.size();
    if (manager.deflectors.empty()) {
        manager.context.apparent_options.solar_deflector_index = -1;
    }
    return TAIYIN_STATUS_OK;
}

void reset_default_native_calc_context() noexcept {
    DefaultNativeCalcContextManager& manager = default_native_calc_context_manager();
    std::lock_guard<std::mutex> lock(manager.mutex);
    manager.context = NativeCalcContext();
    manager.deflectors.clear();
}

}  // namespace runtime
}  // namespace taiyin
