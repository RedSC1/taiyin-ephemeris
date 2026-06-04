#include "taiyin/dispatch.h"
#include "taiyin/observer.h"

namespace taiyin {
namespace dispatch {
namespace wrappers {

static bool equinox(double /*jd_ut1*/, double jd_tt, const void* data, Matrix3x3* out) {
    const FrameRouteDispatchData* d = static_cast<const FrameRouteDispatchData*>(data);
    Matrix3x3 precession;
    double mean_obliquity = 0.0;
    if (!eval_precession(d->precession_model, jd_tt, data, &precession, &mean_obliquity)) return false;
    NutationAngles nutation;
    if (!eval_nutation(d->nutation_model, jd_tt, data, &nutation)) return false;
    nutation.mean_obliquity_rad = mean_obliquity;
    nutation.true_obliquity_rad = mean_obliquity + nutation.deps_rad;
    *out = true_equator_of_date_matrix(precession, nutation);
    return true;
}

static bool cirs(double /*jd_ut1*/, double jd_tt, const void* data, Matrix3x3* out) {
    const FrameRouteDispatchData* d = static_cast<const FrameRouteDispatchData*>(data);
    return cirs_matrix_iau2006a(jd_tt, d->dx_rad, d->dy_rad, out);
}

void register_builtin_frame_route_wrappers() {
    static bool registered = (
        register_frame_route(FRAME_ROUTE_EQUINOX, equinox),
        register_frame_route(FRAME_ROUTE_CIRS, cirs),
        true);
    (void)registered;
}

}  // namespace wrappers
}  // namespace dispatch
}  // namespace taiyin
