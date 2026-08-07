#include "taiyin/dispatch.h"
#include "taiyin/time.h"

namespace taiyin {
namespace dispatch {
namespace wrappers {

static double estimated_default_delta_t(const SplitJulianDate& jd_ut, const void*) {
    return estimated_delta_t_seconds_from_ut1_jd(jd_ut);
}

static double no_correction(const SplitJulianDate&, int, int, const void*) {
    return 0.0;
}

void register_builtin_delta_t_wrappers() {
    static bool registered = (
        register_delta_t_model(
            DELTA_T_ESTIMATED_DEFAULT,
            estimated_default_delta_t
        ),
        register_delta_t_ephemeris_correction(
            DELTA_T_EPHEMERIS_CORRECTION_NONE,
            no_correction
        ),
        true);
    static bool bound = []() -> bool {
        bool ok = true;
        ok = bind_delta_t_ephemeris_correction(
            DELTA_T_ESTIMATED_DEFAULT,
            EPHEMERIS_FAMILY_DE441,
            DELTA_T_EPHEMERIS_CORRECTION_NONE
        ) && ok;
        ok = bind_delta_t_ephemeris_correction(
            DELTA_T_ESTIMATED_DEFAULT,
            EPHEMERIS_FAMILY_DE431,
            DELTA_T_EPHEMERIS_CORRECTION_NONE
        ) && ok;
        return ok;
    }();
    (void)registered;
    (void)bound;
}

}  // namespace wrappers
}  // namespace dispatch
}  // namespace taiyin
