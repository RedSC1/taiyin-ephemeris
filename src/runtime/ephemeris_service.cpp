#include "taiyin/runtime/ephemeris_service.h"

#include "taiyin/internal/descriptor_loader.h"
#include "taiyin/internal/ephemeris_discovery.h"

#include <vector>

namespace taiyin {
namespace runtime {
namespace {

internal::EphemerisBlockQuery make_query(const EphemerisRequest& request) noexcept {
    internal::EphemerisBlockQuery query;
    query.target_id = request.target_id;
    query.center_id = request.center_id;
    query.frame = request.frame;
    query.jd_tdb = request.jd_tdb;
    return query;
}

int cache_priority_bias_for_descriptor(
    const internal::EphemerisBlockDescriptor& descriptor,
    const internal::EphemerisPriorityRegistry* priorities
) noexcept {
    if (!priorities) {
        return 0;
    }

    size_t priority_count = 0;
    const internal::MethodPriorityEntry* entries =
        priorities->method_priorities_for_target(descriptor.target_id, &priority_count);
    if (!entries || priority_count == 0) {
        return 0;
    }

    for (size_t i = 0; i < priority_count; ++i) {
        if (entries[i].method_id == descriptor.method_id) {
            return entries[i].priority;
        }
    }
    return 0;
}

}  // namespace

EphemerisService::EphemerisService() noexcept
    : catalog_(0), priorities_(0), cache_(0), inflight_() {}

EphemerisService::~EphemerisService() noexcept {}

void EphemerisService::set_catalog(const internal::EphemerisBlockCatalog* catalog) noexcept {
    catalog_ = catalog;
}

void EphemerisService::set_priorities(const internal::EphemerisPriorityRegistry* priorities) noexcept {
    priorities_ = priorities;
}

void EphemerisService::set_cache(internal::EphemerisBlockCache* cache) noexcept {
    cache_ = cache;
}

const internal::EphemerisBlockCatalog* EphemerisService::catalog() const noexcept {
    return catalog_;
}

const internal::EphemerisPriorityRegistry* EphemerisService::priorities() const noexcept {
    return priorities_;
}

internal::EphemerisBlockCache* EphemerisService::cache() const noexcept {
    return cache_;
}

bool EphemerisService::find_descriptor(
    const EphemerisRequest& request,
    const internal::EphemerisBlockDescriptor** out
) const noexcept {
    if (out) {
        *out = 0;
    }
    if (!out || !catalog_) {
        return false;
    }

    internal::EphemerisBlockQuery query = make_query(request);

    const internal::EphemerisBlockDescriptor* descriptor = priorities_
        ? catalog_->select_best(query, *priorities_)
        : catalog_->find_first(query);
    if (!descriptor) {
        return false;
    }
    *out = descriptor;
    return true;
}

bool EphemerisService::select_calculation_route(
    const EphemerisRequest& request,
    EphemerisSelectionResult* out
) noexcept {
    if (out) {
        *out = EphemerisSelectionResult();
    }
    if (!out || !catalog_ || !cache_) {
        return false;
    }

    internal::EphemerisBlockQuery query = make_query(request);
    std::vector<const internal::EphemerisBlockDescriptor*> ranked;
    if (priorities_) {
        if (!catalog_->rank_candidates(query, *priorities_, &ranked)) {
            return false;
        }
    } else if (!catalog_->find_candidates(query, &ranked)) {
        return false;
    }

    for (size_t i = 0; i < ranked.size(); ++i) {
        const internal::EphemerisBlockDescriptor* source = ranked[i];
        if (!source) {
            continue;
        }

        internal::EphemerisBlockDescriptor bucket;
        if (!internal::make_cache_bucket_descriptor_for_jd(*source, request.jd_tdb, &bucket)) {
            continue;
        }

        CartesianState scratch;
        if (cache_->eval_state(bucket.route_key, request.jd_tdb, &scratch)) {
            out->source_descriptor = *source;
            out->bucket_descriptor = bucket;
            out->cache_hit = true;
            out->loaded = false;
            return true;
        }

        const int priority_bias = cache_priority_bias_for_descriptor(*source, priorities_);
        internal::RouteInflightGuard guard(inflight_, bucket.route_key);
        const internal::RouteInflightAction action = guard.begin();
        if (action == internal::RouteInflightLoadNow) {
            const bool loaded = internal::load_descriptor_into_cache(bucket, cache_, priority_bias);
            guard.end(loaded);
            if (!loaded || !cache_->eval_state(bucket.route_key, request.jd_tdb, &scratch)) {
                continue;
            }

            out->source_descriptor = *source;
            out->bucket_descriptor = bucket;
            out->cache_hit = false;
            out->loaded = true;
            return true;
        }

        if (!cache_->eval_state(bucket.route_key, request.jd_tdb, &scratch)) {
            continue;
        }

        out->source_descriptor = *source;
        out->bucket_descriptor = bucket;
        out->cache_hit = true;
        out->loaded = false;
        return true;
    }

    return false;
}

bool EphemerisService::eval_state(const EphemerisRequest& request, EphemerisResult* out) noexcept {
    if (out) {
        *out = EphemerisResult();
    }
    if (!out) {
        return false;
    }

    EphemerisSelectionResult selection;
    if (!select_calculation_route(request, &selection)
        || !cache_
        || !cache_->eval_state(selection.bucket_descriptor.route_key, request.jd_tdb, &out->state)) {
        return false;
    }

    out->descriptor = selection.source_descriptor;
    out->cache_hit = selection.cache_hit;
    return true;
}

}  // namespace runtime
}  // namespace taiyin
