#include "taiyin/dispatch.h"
#include "taiyin/observer.h"

namespace taiyin {
namespace dispatch {
namespace wrappers {

static double bennett(const void* data) {
    const RefractionDispatchData* d = static_cast<const RefractionDispatchData*>(data);
    return atmospheric_refraction_bennett_rad(d->altitude_rad, d->pressure_mbar, d->temperature_c);
}

static double skyfield(const void* data) {
    const RefractionDispatchData* d = static_cast<const RefractionDispatchData*>(data);
    return atmospheric_refraction_skyfield_rad(
        d->altitude_rad,
        d->pressure_mbar,
        d->temperature_c,
        d->max_iterations,
        d->tolerance);
}

static double hybrid(const void* data) {
    const RefractionDispatchData* d = static_cast<const RefractionDispatchData*>(data);
    return atmospheric_refraction_hybrid_rad(d->altitude_rad, d->pressure_mbar, d->temperature_c);
}

static double auer_standish(const void* data) {
    const RefractionDispatchData* d = static_cast<const RefractionDispatchData*>(data);
    return atmospheric_refraction_auer_standish_rad(
        d->altitude_rad,
        d->pressure_mbar,
        d->temperature_c,
        d->max_iterations,
        d->tolerance);
}

static double sofa(const void* data) {
    const RefractionDispatchData* d = static_cast<const RefractionDispatchData*>(data);
    return atmospheric_refraction_sofa_rad(
        d->altitude_rad,
        d->pressure_mbar,
        d->temperature_c,
        d->relative_humidity,
        d->wavelength_micrometer);
}

void register_builtin_refraction_wrappers() {
    static bool registered = (
        register_refraction_model(REFRACTION_BENNETT, bennett),
        register_refraction_model(REFRACTION_SKYFIELD, skyfield),
        register_refraction_model(REFRACTION_HYBRID, hybrid),
        register_refraction_model(REFRACTION_AUER_STANDISH, auer_standish),
        register_refraction_model(REFRACTION_SOFA, sofa),
        true);
    (void)registered;
}

}  // namespace wrappers
}  // namespace dispatch
}  // namespace taiyin
