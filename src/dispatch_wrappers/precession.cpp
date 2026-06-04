#include "taiyin/dispatch.h"
#include "taiyin/observer.h"

namespace taiyin {
namespace dispatch {
namespace wrappers {

static bool vondrak2011(double jd_tt, const void* /*data*/, Matrix3x3* out, double* out_mean_obliquity_rad) {
    return vondrak2011_precession_matrix(jd_tt, out, out_mean_obliquity_rad);
}

static bool iau2006(double jd_tt, const void* /*data*/, Matrix3x3* out, double* out_mean_obliquity_rad) {
    return iau2006_precession_matrix(jd_tt, out, out_mean_obliquity_rad);
}

void register_builtin_precession_wrappers() {
    static bool registered = (
        register_precession_model(PRECESSION_VONDRAK2011, vondrak2011),
        register_precession_model(PRECESSION_IAU2006, iau2006),
        true);
    static bool prioritized = []() -> bool {
        const int order[] = { PRECESSION_IAU2006, PRECESSION_VONDRAK2011 };
        return set_precession_priority_order(order, sizeof(order) / sizeof(order[0]));
    }();
    (void)registered;
    (void)prioritized;
}

}  // namespace wrappers
}  // namespace dispatch
}  // namespace taiyin
