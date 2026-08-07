#include "taiyin/dispatch.h"
#include "taiyin/time.h"

namespace taiyin {
namespace dispatch {
namespace wrappers {

static double fast_periodic(const SplitJulianDate& jd_tt, const void* /*data*/) {
    return tdb_minus_tt_fast_seconds(jd_tt);
}

static double sofa_full(const SplitJulianDate& jd_tt, const void* /*data*/) {
    return tdb_minus_tt_sofa_seconds(jd_tt);
}

static bool inverse_fast_periodic(
    const SplitJulianDate& jd_tdb,
    const void* data,
    SplitJulianDate* out_jd_tt
) {
    const TdbInverseDispatchData* d = static_cast<const TdbInverseDispatchData*>(data);
    const int max_iterations = d && d->max_iterations > 0
        ? d->max_iterations
        : 4;
    const double tolerance_days = d && d->tolerance_days >= 0.0
        ? d->tolerance_days
        : 0.0;
    return tdb_to_tt_split_jd(
        jd_tdb, TdbModel::FastPeriodic, max_iterations, tolerance_days,
        out_jd_tt);
}

static bool inverse_sofa_full(
    const SplitJulianDate& jd_tdb,
    const void* data,
    SplitJulianDate* out_jd_tt
) {
    const TdbInverseDispatchData* d = static_cast<const TdbInverseDispatchData*>(data);
    const int max_iterations = d && d->max_iterations > 0
        ? d->max_iterations
        : 4;
    const double tolerance_days = d && d->tolerance_days >= 0.0
        ? d->tolerance_days
        : 0.0;
    return tdb_to_tt_split_jd(
        jd_tdb, TdbModel::SofaFull, max_iterations, tolerance_days,
        out_jd_tt);
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
