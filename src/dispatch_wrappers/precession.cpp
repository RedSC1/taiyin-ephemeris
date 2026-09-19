#include "taiyin/dispatch.h"
#include "taiyin/observer.h"

namespace taiyin {
namespace dispatch {
namespace wrappers {

static bool vondrak2011(const SplitJulianDate& jd_tt, const void* /*data*/, Matrix3x3* out, double* out_mean_obliquity_rad) {
    return vondrak2011_precession_matrix(jd_tt, out, out_mean_obliquity_rad);
}

static bool iau2006(const SplitJulianDate& jd_tt, const void* /*data*/, Matrix3x3* out, double* out_mean_obliquity_rad) {
    return iau2006_precession_matrix(jd_tt, out, out_mean_obliquity_rad);
}

static bool iau1976(const SplitJulianDate& jd_tt, const void* /*data*/, Matrix3x3* out, double* out_mean_obliquity_rad) {
    return iau1976_precession_matrix(jd_tt, out, out_mean_obliquity_rad);
}

static bool newcomb1895(const SplitJulianDate& jd_tt, const void* /*data*/, Matrix3x3* out, double* out_mean_obliquity_rad) {
    return newcomb1895_precession_matrix(jd_tt, out, out_mean_obliquity_rad);
}

void register_builtin_precession_wrappers() {
    static bool registered = (
        register_precession_model(PRECESSION_VONDRAK2011, vondrak2011),
        register_precession_model(PRECESSION_IAU2006, iau2006),
        register_precession_model(PRECESSION_IAU1976, iau1976),
        register_precession_model(PRECESSION_NEWCOMB1895, newcomb1895),
        true);
    static bool prioritized = []() -> bool {
        // The default runtime supports epochs far outside the intended
        // polynomial span of IAU 2006.  Prefer the coherent long-term
        // equator/ecliptic model; callers that require the IAU standard can
        // continue to select PRECESSION_IAU2006 explicitly.
        const int order[] = { PRECESSION_VONDRAK2011, PRECESSION_IAU2006 };
        return set_precession_priority_order(order, sizeof(order) / sizeof(order[0]));
    }();
    (void)registered;
    (void)prioritized;
}

}  // namespace wrappers
}  // namespace dispatch
}  // namespace taiyin
