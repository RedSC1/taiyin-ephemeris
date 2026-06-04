#ifndef TAIYIN_RUNTIME_EPHEMERIS_SERVICE_H
#define TAIYIN_RUNTIME_EPHEMERIS_SERVICE_H

#include "taiyin/internal/ephemeris_cache.h"
#include "taiyin/internal/ephemeris_catalog.h"
#include "taiyin/internal/route_inflight_map.h"
#include "taiyin/state.h"

namespace taiyin {
namespace runtime {

struct EphemerisRequest {
    int target_id;
    int center_id;
    internal::EphemerisFrame frame;
    double jd_tdb;

    EphemerisRequest()
        : target_id(0),
          center_id(0),
          frame(internal::EphemerisFrame::FrameUnknown),
          jd_tdb(0.0) {}
};

struct EphemerisResult {
    CartesianState state;
    internal::EphemerisBlockDescriptor descriptor;
    bool cache_hit;

    EphemerisResult()
        : state(), descriptor(), cache_hit(false) {}
};

struct EphemerisSelectionResult {
    internal::EphemerisBlockDescriptor source_descriptor;
    internal::EphemerisBlockDescriptor bucket_descriptor;
    bool cache_hit;
    bool loaded;

    EphemerisSelectionResult()
        : source_descriptor(), bucket_descriptor(), cache_hit(false), loaded(false) {}
};

class EphemerisService {
public:
    EphemerisService() noexcept;
    ~EphemerisService() noexcept;

    EphemerisService(const EphemerisService&) = delete;
    EphemerisService& operator=(const EphemerisService&) = delete;

    void set_catalog(const internal::EphemerisBlockCatalog* catalog) noexcept;
    void set_priorities(const internal::EphemerisPriorityRegistry* priorities) noexcept;
    void set_cache(internal::EphemerisBlockCache* cache) noexcept;

    const internal::EphemerisBlockCatalog* catalog() const noexcept;
    const internal::EphemerisPriorityRegistry* priorities() const noexcept;
    internal::EphemerisBlockCache* cache() const noexcept;

    bool find_descriptor(
        const EphemerisRequest& request,
        const internal::EphemerisBlockDescriptor** out
    ) const noexcept;
    bool select_calculation_route(
        const EphemerisRequest& request,
        EphemerisSelectionResult* out
    ) noexcept;
    bool eval_state(const EphemerisRequest& request, EphemerisResult* out) noexcept;

private:
    const internal::EphemerisBlockCatalog* catalog_;
    const internal::EphemerisPriorityRegistry* priorities_;
    internal::EphemerisBlockCache* cache_;
    internal::RouteInflightMap inflight_;
};

}  // namespace runtime
}  // namespace taiyin

#endif  // TAIYIN_RUNTIME_EPHEMERIS_SERVICE_H
