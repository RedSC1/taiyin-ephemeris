#include "taiyin/dispatch.h"
#include "taiyin/observer.h"

namespace taiyin {
namespace dispatch {
namespace wrappers {

static bool iau2000b(double jd_tt, const void* /*data*/, NutationAngles* out) {
    return iau2000b_nutation(jd_tt, out);
}

static bool iau2000a(double jd_tt, const void* /*data*/, NutationAngles* out) {
    return iau2000a_nutation(jd_tt, out);
}

void register_builtin_nutation_wrappers() {
    static bool registered = (
        register_nutation_model(NUTATION_IAU2000B, iau2000b),
        register_nutation_model(NUTATION_IAU2000A, iau2000a),
        true);
    static bool prioritized = []() -> bool {
        const int order[] = { NUTATION_IAU2000B, NUTATION_IAU2000A };
        return set_nutation_priority_order(order, sizeof(order) / sizeof(order[0]));
    }();
    (void)registered;
    (void)prioritized;
}

}  // namespace wrappers
}  // namespace dispatch
}  // namespace taiyin
