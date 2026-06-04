#include "taiyin/dispatch.h"
#include "taiyin/time.h"

namespace taiyin {
namespace dispatch {
namespace wrappers {

static double fast_periodic(double jd_tt, const void* /*data*/) {
    return tdb_minus_tt_fast_seconds(jd_tt);
}

static double sofa_full(double jd_tt, const void* /*data*/) {
    return tdb_minus_tt_sofa_seconds(jd_tt);
}

static double inverse_fast_periodic(double jd_tdb, const void* data) {
    const TdbInverseDispatchData* d = static_cast<const TdbInverseDispatchData*>(data);
    return tdb_to_tt_jd(jd_tdb, TdbModel::FastPeriodic, d->max_iterations, d->tolerance_days);
}

static double inverse_sofa_full(double jd_tdb, const void* data) {
    const TdbInverseDispatchData* d = static_cast<const TdbInverseDispatchData*>(data);
    return tdb_to_tt_jd(jd_tdb, TdbModel::SofaFull, d->max_iterations, d->tolerance_days);
}

void register_builtin_tdb_wrappers() {
    static bool registered = (
        register_tdb_model(TDB_FAST_PERIODIC, fast_periodic),
        register_tdb_model(TDB_SOFA_FULL, sofa_full),
        register_tdb_inverse_model(TDB_FAST_PERIODIC, inverse_fast_periodic),
        register_tdb_inverse_model(TDB_SOFA_FULL, inverse_sofa_full),
        true);
    (void)registered;
}

}  // namespace wrappers
}  // namespace dispatch
}  // namespace taiyin
