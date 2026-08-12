#ifndef TAIYIN_RUNTIME_NATIVE_CONTEXT_H
#define TAIYIN_RUNTIME_NATIVE_CONTEXT_H

#include "taiyin/field_set.h"
#include "taiyin/runtime/ephemeris_route.h"
#include "taiyin/runtime/major_body_apparent.h"
#include "taiyin/status.h"
#include "taiyin/time.h"

#include <stdint.h>

namespace taiyin {
namespace internal {
class EphemerisRouteRuleTable;
}
namespace runtime {

enum NativeCalcField {
    TAIYIN_NATIVE_FIELD_TOPOCENTRIC_OFFSET = 2,
    TAIYIN_NATIVE_FIELD_OBSERVER_LOCATION = 3,
    TAIYIN_NATIVE_FIELD_ATMOSPHERE = 4,
    TAIYIN_NATIVE_FIELD_DEFLECTORS = 5,
    TAIYIN_NATIVE_FIELD_ATMOSPHERE_PRESSURE = 6,
    TAIYIN_NATIVE_FIELD_ATMOSPHERE_TEMPERATURE = 7,
    TAIYIN_NATIVE_FIELD_ATMOSPHERE_HUMIDITY = 8,
    TAIYIN_NATIVE_FIELD_ATMOSPHERE_WAVELENGTH = 9,
    TAIYIN_NATIVE_FIELD_CELESTIAL_POLE_OFFSET = 10,
    TAIYIN_NATIVE_FIELD_METEOROLOGICAL_RANGE = 11,
};

constexpr uint32_t TAIYIN_NATIVE_ATMOSPHERE_ALLOW_STANDARD_FALLBACK = 1u << 0;

// Records how the current topocentric offset was produced. Event searches
// copy a context for each sample and must only recompute offsets that are
// derived from an observer location; an explicit caller-supplied offset is
// already the requested state and must be preserved verbatim.
enum NativeTopocentricObserverModel {
    TAIYIN_NATIVE_TOPOCENTRIC_OBSERVER_NONE = 0,
    TAIYIN_NATIVE_TOPOCENTRIC_OBSERVER_EXPLICIT_OFFSET = 1,
    TAIYIN_NATIVE_TOPOCENTRIC_OBSERVER_SIMPLE = 2,
    TAIYIN_NATIVE_TOPOCENTRIC_OBSERVER_PRECISE = 3,
};

struct NativeObserverLocation {
    double longitude_rad;
    double latitude_rad;
    double height_m;

    NativeObserverLocation() noexcept;
};

struct NativeAtmosphere {
    double pressure_mbar;
    double temperature_celsius;
    // Relative humidity in percent, in the inclusive range [0, 100].
    double relative_humidity;
    double wavelength_micrometer;

    NativeAtmosphere() noexcept;
};

struct NativeCalcContext {
    FieldSet fields;
    AstroModelContext model_context;
    ApparentOptions apparent_options;
    TimeScalePolicy time_scale_policy;
    int delta_t_model_id;
    int ephemeris_family_id;
    int observer_id;
    int center_id;
    uint8_t topocentric_observer_model;
    NativeObserverLocation observer_location;
    NativeAtmosphere atmosphere;
    // Controls whether refraction and visibility models may fill missing
    // atmosphere inputs with the documented standard-atmosphere convention.
    uint32_t atmosphere_policy_flags;
    // Horizontal meteorological range in km, used by visibility models that
    // derive aerosol extinction. It is meaningful only when the matching
    // field bit is set.
    double meteorological_range_km;
    int refraction_model_id;
    int heliacal_visibility_model_id;
    uint8_t eclipse_shadow_model_id;
    uint8_t eclipse_moon_radius_model_id;
    uint64_t route_rule_id;
    // Resolved at setRouteRule time. Route rule tables are registered during
    // setup and treated as immutable while calculations are running.
    const internal::EphemerisRouteRuleTable* route_rules;

    NativeCalcContext() noexcept;
};

NativeObserverLocation native_observer_location_degrees(
    double longitude_deg,
    double latitude_deg,
    double height_m
) noexcept;
NativeAtmosphere native_standard_atmosphere() noexcept;

Status native_context_set_observer_location(
    NativeCalcContext* context,
    const NativeObserverLocation& location
) noexcept;
Status native_context_set_atmosphere_pressure_temperature(
    NativeCalcContext* context,
    double pressure_mbar,
    double temperature_celsius
) noexcept;
Status native_context_set_atmosphere(
    NativeCalcContext* context,
    const NativeAtmosphere& atmosphere
) noexcept;
Status native_context_set_meteorological_range_km(
    NativeCalcContext* context,
    double meteorological_range_km
) noexcept;
Status native_context_set_atmosphere_policy_flags(
    NativeCalcContext* context,
    uint32_t atmosphere_policy_flags
) noexcept;
Status native_context_set_refraction_model(
    NativeCalcContext* context,
    int refraction_model_id
) noexcept;
Status native_context_set_heliacal_visibility_model(
    NativeCalcContext* context,
    int heliacal_visibility_model_id
) noexcept;

Status native_context_set_delta_t_model(
    NativeCalcContext* context,
    int delta_t_model_id,
    int ephemeris_family_id
) noexcept;
Status native_context_set_tdb_model(NativeCalcContext* context, int tdb_model_id) noexcept;
Status native_context_set_time_scale_policy(NativeCalcContext* context, TimeScalePolicy policy) noexcept;
Status native_context_set_route_rule(NativeCalcContext* context, uint64_t route_rule_id) noexcept;

Status native_context_set_celestial_pole_offset(
    NativeCalcContext* context,
    double dx_rad,
    double dy_rad,
    double dx_rate_rad_per_day = 0.0,
    double dy_rate_rad_per_day = 0.0
) noexcept;

Status native_context_set_geocentric_observer(
    NativeCalcContext* context,
    int observer_id,
    int center_id
) noexcept;

// Topocentric observer models are Earth-only in the 1.0 API. These setters
// return TAIYIN_ERROR_UNSUPPORTED when context->observer_id is not Earth.
Status native_context_set_topocentric_observer_offset(
    NativeCalcContext* context,
    const CartesianState& observer_offset
) noexcept;

Status native_context_set_simple_topocentric_observer(
    NativeCalcContext* context,
    const NativeObserverLocation& location,
    const SplitJulianDate& jd_ut1,
    const SplitJulianDate& jd_tt
) noexcept;

Status native_context_set_precise_topocentric_observer(
    NativeCalcContext* context,
    const NativeObserverLocation& location,
    const SplitJulianDate& jd_utc,
    const SplitJulianDate& jd_tt
) noexcept;

// Rebuild a location-derived observer at a search sample. Explicit offsets
// are intentionally left unchanged.
Status native_context_refresh_topocentric_observer(
    NativeCalcContext* context,
    const SplitJulianDate& jd_ut1,
    const SplitJulianDate& jd_tt
) noexcept;

const ApparentDeflector* native_solar_deflector() noexcept;
Status native_context_set_deflectors(
    NativeCalcContext* context,
    const ApparentDeflector* deflectors,
    size_t deflector_count,
    int solar_deflector_index
) noexcept;
Status native_context_use_solar_deflector(NativeCalcContext* context) noexcept;
Status native_context_clear_deflectors(NativeCalcContext* context) noexcept;

Status native_context_set_light_time_iteration(
    NativeCalcContext* context,
    int max_iterations,
    double tolerance_days
) noexcept;
Status native_context_enable_shapiro_delay(
    NativeCalcContext* context,
    int shapiro_delay_model_id
) noexcept;
Status native_context_disable_shapiro_delay(NativeCalcContext* context) noexcept;

Status native_context_set_eclipse_shadow_model(
    NativeCalcContext* context,
    int eclipse_shadow_model_id
) noexcept;
Status native_context_set_eclipse_moon_radius_model(
    NativeCalcContext* context,
    int eclipse_moon_radius_model_id
) noexcept;
NativeCalcContext get_default_native_calc_context() noexcept;
Status set_default_native_calc_context(const NativeCalcContext& context) noexcept;
void reset_default_native_calc_context() noexcept;

}  // namespace runtime
}  // namespace taiyin

#endif  // TAIYIN_RUNTIME_NATIVE_CONTEXT_H
