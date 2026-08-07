#include "taiyin/dispatch.h"

#include "runtime/visibility/heliacal_visibility_internal.h"

namespace taiyin {
namespace dispatch {
namespace wrappers {

namespace {

bool belokrylov_2011(const void* input, void* output) {
    return runtime::heliacal_visibility_eval_belokrylov_2011(
        static_cast<const runtime::HeliacalVisibilityModelInput*>(input),
        static_cast<runtime::HeliacalVisibilityResult*>(output));
}

bool schaefer_1993(const void* input, void* output) {
    return runtime::heliacal_visibility_eval_schaefer_1993(
        static_cast<const runtime::HeliacalVisibilityModelInput*>(input),
        static_cast<runtime::HeliacalVisibilityResult*>(output));
}

}  // namespace

void register_builtin_heliacal_visibility_wrappers() {
    static bool registered = []() -> bool {
        register_heliacal_visibility_model(HeliacalVisibilityModelEntry(
            HELIACAL_VISIBILITY_BELOKRYLOV_2011,
            belokrylov_2011,
            HELIACAL_EXTINCTION_BELOKRYLOV_2011,
            HELIACAL_TWILIGHT_BELOKRYLOV_2011,
            HELIACAL_VISUAL_THRESHOLD_BELOKRYLOV_2011,
            0.25));
        register_heliacal_visibility_model(HeliacalVisibilityModelEntry(
            HELIACAL_VISIBILITY_SCHAEFER_1993,
            schaefer_1993,
            HELIACAL_EXTINCTION_SCHAEFER_2000,
            HELIACAL_TWILIGHT_SCHAEFER_1993,
            HELIACAL_MOONLIGHT_KRISCIUNAS_SCHAEFER_1991,
            HELIACAL_VISUAL_THRESHOLD_HECHT_1947,
            0.25));
        return true;
    }();
    (void)registered;
}

}  // namespace wrappers
}  // namespace dispatch
}  // namespace taiyin
