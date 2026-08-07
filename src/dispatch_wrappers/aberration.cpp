#include "taiyin/dispatch.h"
#include "taiyin/corrections.h"

namespace taiyin {
namespace dispatch {
namespace wrappers {

static bool annual_relativistic(
    const AberrationDispatchData* data,
    Vector3* out_position_au,
    Vector3* out_velocity_au_per_day,
    Vector3* out_acceleration_au_per_day2
) {
    if (!data) {
        return false;
    }
    if (data->compute_acceleration) {
        return apply_annual_aberration_acceleration(
            data->source_geocentric_position_au,
            data->source_geocentric_velocity_au_per_day,
            data->source_geocentric_acceleration_au_per_day2,
            data->observer_heliocentric_position_au,
            data->observer_heliocentric_velocity_au_per_day,
            data->observer_heliocentric_acceleration_au_per_day2,
            data->observer_barycentric_velocity_au_per_day,
            data->observer_barycentric_acceleration_au_per_day2,
            data->light_time_days_per_au,
            data->solar_schwarzschild_radius_au,
            out_position_au,
            out_velocity_au_per_day,
            out_acceleration_au_per_day2);
    }
    return apply_annual_aberration(
        data->source_geocentric_position_au,
        data->source_geocentric_velocity_au_per_day,
        data->observer_heliocentric_position_au,
        data->observer_heliocentric_velocity_au_per_day,
        data->observer_barycentric_velocity_au_per_day,
        data->observer_barycentric_acceleration_au_per_day2,
        data->light_time_days_per_au,
        data->solar_schwarzschild_radius_au,
        out_position_au,
        out_velocity_au_per_day);
}

void register_builtin_aberration_wrappers() {
    static bool registered = (
        register_aberration_model(ABERRATION_ANNUAL_RELATIVISTIC, annual_relativistic),
        true);
    static bool prioritized = []() -> bool {
        const int order[] = { ABERRATION_ANNUAL_RELATIVISTIC };
        return set_aberration_priority_order(order, sizeof(order) / sizeof(order[0]));
    }();
    (void)registered;
    (void)prioritized;
}

}  // namespace wrappers
}  // namespace dispatch
}  // namespace taiyin
