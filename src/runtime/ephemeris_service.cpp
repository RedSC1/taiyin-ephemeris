#include "taiyin/runtime/ephemeris_service.h"

#include "taiyin/body_id.h"
#include "taiyin/internal/descriptor_loader.h"
#include "taiyin/internal/ephemeris_discovery.h"
#include "taiyin/physical_constants.h"

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <limits>
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

EphemerisRequest make_component_request(
    int target_id,
    int center_id,
    internal::EphemerisFrame frame,
    double jd_tdb
) noexcept {
    EphemerisRequest request;
    request.target_id = target_id;
    request.center_id = center_id;
    request.frame = frame;
    request.jd_tdb = jd_tdb;
    return request;
}

bool append_diagnostic_text(
    char* buffer,
    size_t buffer_size,
    size_t* offset,
    const char* format,
    ...
) noexcept {
    if (!offset || !format) {
        return false;
    }

    char part[512];
    va_list args;
    va_start(args, format);
    const int written = std::vsnprintf(part, sizeof(part), format, args);
    va_end(args);
    if (written < 0) {
        return false;
    }

    const size_t actual = static_cast<size_t>(written);
    if (buffer && buffer_size > 0 && *offset < buffer_size - 1) {
        const size_t available = buffer_size - 1 - *offset;
        const size_t part_len = actual < sizeof(part) ? actual : sizeof(part) - 1;
        const size_t copy_len = part_len < available ? part_len : available;
        if (copy_len > 0) {
            std::memcpy(buffer + *offset, part, copy_len);
        }
        buffer[*offset + copy_len] = '\0';
    }

    *offset += actual;
    return true;
}

void reset_diagnostic(
    EphemerisEvalDiagnostic* diagnostic,
    const EphemerisRequest& request,
    TaiyinStatus status
) noexcept {
    if (!diagnostic) {
        return;
    }
    *diagnostic = EphemerisEvalDiagnostic();
    diagnostic->status = status;
    diagnostic->target_id = request.target_id;
    diagnostic->center_id = request.center_id;
    diagnostic->frame = request.frame;
    diagnostic->jd_tdb = request.jd_tdb;
}

TaiyinStatus set_diagnostic_status(
    EphemerisEvalDiagnostic* diagnostic,
    TaiyinStatus status
) noexcept {
    if (diagnostic) {
        diagnostic->status = status;
    }
    return status;
}

bool descriptor_same_route(
    const internal::EphemerisBlockDescriptor& descriptor,
    const internal::EphemerisBlockQuery& query
) noexcept {
    return descriptor.target_id == query.target_id
        && descriptor.center_id == query.center_id
        && descriptor.frame == query.frame;
}

bool descriptor_covers_jd(
    const internal::EphemerisBlockDescriptor& descriptor,
    double jd_tdb
) noexcept {
    return descriptor.jd_tdb_end > descriptor.jd_tdb_start
        && jd_tdb >= descriptor.jd_tdb_start
        && jd_tdb < descriptor.jd_tdb_end;
}

double coverage_distance(
    const internal::EphemerisBlockDescriptor& descriptor,
    double jd_tdb
) noexcept {
    if (descriptor_covers_jd(descriptor, jd_tdb)) {
        return 0.0;
    }
    if (jd_tdb < descriptor.jd_tdb_start) {
        return descriptor.jd_tdb_start - jd_tdb;
    }
    return jd_tdb - descriptor.jd_tdb_end;
}

TaiyinStatus diagnose_route_availability(
    const internal::EphemerisBlockCatalog* catalog,
    const EphemerisRequest& request,
    EphemerisEvalDiagnostic* diagnostic
) noexcept {
    if (!catalog) {
        return set_diagnostic_status(diagnostic, TAIYIN_RUNTIME_ERROR_NOT_INITIALIZED);
    }

    internal::EphemerisBlockQuery query = make_query(request);
    int route_count = 0;
    const internal::EphemerisBlockDescriptor* nearest = 0;
    double nearest_distance = std::numeric_limits<double>::infinity();

    for (size_t i = 0; i < catalog->size(); ++i) {
        const internal::EphemerisBlockDescriptor* descriptor = catalog->at(i);
        if (!descriptor || !descriptor_same_route(*descriptor, query)) {
            continue;
        }
        ++route_count;
        const double distance = coverage_distance(*descriptor, request.jd_tdb);
        if (!nearest || distance < nearest_distance) {
            nearest = descriptor;
            nearest_distance = distance;
        }
    }

    if (diagnostic) {
        diagnostic->candidate_count = route_count;
        if (nearest) {
            diagnostic->nearest_coverage_start = nearest->jd_tdb_start;
            diagnostic->nearest_coverage_end = nearest->jd_tdb_end;
            diagnostic->attempted_method_id = nearest->method_id;
        }
    }

    return set_diagnostic_status(
        diagnostic,
        route_count == 0 ? TAIYIN_EPHEMERIS_ERROR_NO_ROUTE : TAIYIN_EPHEMERIS_ERROR_COVERAGE_GAP);
}

TaiyinStatus diagnose_composite_component(
    const internal::EphemerisBlockCatalog* catalog,
    const EphemerisRequest& component_request,
    EphemerisEvalDiagnostic* diagnostic
) noexcept {
    TaiyinStatus status = diagnose_route_availability(catalog, component_request, diagnostic);
    if (diagnostic) {
        diagnostic->component_target_id = component_request.target_id;
        diagnostic->component_center_id = component_request.center_id;
        diagnostic->component_method_id = diagnostic->attempted_method_id;
    }
    if (status == TAIYIN_EPHEMERIS_ERROR_NO_ROUTE) {
        return set_diagnostic_status(diagnostic, TAIYIN_EPHEMERIS_ERROR_COMPOSITE_MISSING_COMPONENT);
    }
    if (status == TAIYIN_EPHEMERIS_ERROR_COVERAGE_GAP) {
        return set_diagnostic_status(diagnostic, TAIYIN_EPHEMERIS_ERROR_COMPOSITE_COVERAGE_GAP);
    }
    return status;
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

size_t format_ephemeris_eval_diagnostic(
    const EphemerisEvalDiagnostic& diagnostic,
    char* buffer,
    size_t buffer_size
) noexcept {
    if (buffer && buffer_size > 0) {
        buffer[0] = '\0';
    }

    size_t length = 0;
    if (!append_diagnostic_text(
            buffer,
            buffer_size,
            &length,
            "%s (%d): %s; target=%d center=%d frame=%d jd_tdb=%.17g",
            taiyin_status_name(diagnostic.status),
            static_cast<int>(diagnostic.status),
            taiyin_status_message(diagnostic.status),
            diagnostic.target_id,
            diagnostic.center_id,
            static_cast<int>(diagnostic.frame),
            diagnostic.jd_tdb)) {
        return 0;
    }

    if (diagnostic.candidate_count > 0) {
        append_diagnostic_text(
            buffer,
            buffer_size,
            &length,
            "; candidates=%d",
            diagnostic.candidate_count);
    }
    if (diagnostic.attempted_method_id != 0) {
        append_diagnostic_text(
            buffer,
            buffer_size,
            &length,
            "; method=%d",
            diagnostic.attempted_method_id);
    }
    if (diagnostic.nearest_coverage_start != 0.0 || diagnostic.nearest_coverage_end != 0.0) {
        append_diagnostic_text(
            buffer,
            buffer_size,
            &length,
            "; nearest_coverage=[%.17g, %.17g)",
            diagnostic.nearest_coverage_start,
            diagnostic.nearest_coverage_end);
    }
    if (diagnostic.component_target_id != 0 || diagnostic.component_center_id != 0
        || diagnostic.component_method_id != 0) {
        append_diagnostic_text(
            buffer,
            buffer_size,
            &length,
            "; component target=%d center=%d method=%d",
            diagnostic.component_target_id,
            diagnostic.component_center_id,
            diagnostic.component_method_id);
    }

    if (buffer && buffer_size > 0) {
        if (length >= buffer_size) {
            buffer[buffer_size - 1] = '\0';
        }
    }
    return length;
}

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

TaiyinStatus EphemerisService::select_calculation_route(
    const EphemerisRequest& request,
    EphemerisSelectionResult* out,
    EphemerisEvalDiagnostic* diagnostic
) noexcept {
    if (out) {
        *out = EphemerisSelectionResult();
    }
    reset_diagnostic(diagnostic, request, TAIYIN_STATUS_OK);
    if (!out) {
        return set_diagnostic_status(diagnostic, TAIYIN_ERROR_INVALID_ARGUMENT);
    }
    if (!catalog_ || !cache_) {
        return set_diagnostic_status(diagnostic, TAIYIN_RUNTIME_ERROR_NOT_INITIALIZED);
    }

    internal::EphemerisBlockQuery query = make_query(request);
    std::vector<const internal::EphemerisBlockDescriptor*> ranked;
    if (priorities_) {
        if (!catalog_->rank_candidates(query, *priorities_, &ranked)) {
            return diagnose_route_availability(catalog_, request, diagnostic);
        }
    } else if (!catalog_->find_candidates(query, &ranked)) {
        return diagnose_route_availability(catalog_, request, diagnostic);
    }

    TaiyinStatus last_status = TAIYIN_EPHEMERIS_ERROR_EVAL_FAILED;
    for (size_t i = 0; i < ranked.size(); ++i) {
        const internal::EphemerisBlockDescriptor* source = ranked[i];
        if (!source) {
            continue;
        }
        if (diagnostic) {
            diagnostic->attempted_method_id = source->method_id;
            diagnostic->candidate_count = static_cast<int>(ranked.size());
        }

        internal::EphemerisBlockDescriptor bucket;
        if (!internal::make_cache_bucket_descriptor_for_jd(*source, request.jd_tdb, &bucket)) {
            last_status = TAIYIN_EPHEMERIS_ERROR_COVERAGE_GAP;
            continue;
        }

        CartesianState scratch;
        if (cache_->eval_state(bucket.route_key, request.jd_tdb, &scratch)) {
            out->source_descriptor = *source;
            out->bucket_descriptor = bucket;
            out->cache_hit = true;
            out->loaded = false;
            return set_diagnostic_status(diagnostic, TAIYIN_STATUS_OK);
        }

        const int priority_bias = cache_priority_bias_for_descriptor(*source, priorities_);
        internal::RouteInflightGuard guard(inflight_, bucket.route_key);
        const internal::RouteInflightAction action = guard.begin();
        if (action == internal::RouteInflightLoadNow) {
            const bool loaded = internal::load_descriptor_into_cache(bucket, cache_, priority_bias);
            guard.end(loaded);
            if (!loaded) {
                last_status = TAIYIN_EPHEMERIS_ERROR_LOAD_FAILED;
                continue;
            }
            if (!cache_->eval_state(bucket.route_key, request.jd_tdb, &scratch)) {
                last_status = TAIYIN_EPHEMERIS_ERROR_EVAL_FAILED;
                continue;
            }

            out->source_descriptor = *source;
            out->bucket_descriptor = bucket;
            out->cache_hit = false;
            out->loaded = true;
            return set_diagnostic_status(diagnostic, TAIYIN_STATUS_OK);
        }

        if (!cache_->eval_state(bucket.route_key, request.jd_tdb, &scratch)) {
            last_status = TAIYIN_EPHEMERIS_ERROR_LOAD_FAILED;
            continue;
        }

        out->source_descriptor = *source;
        out->bucket_descriptor = bucket;
        out->cache_hit = true;
        out->loaded = false;
        return set_diagnostic_status(diagnostic, TAIYIN_STATUS_OK);
    }

    return set_diagnostic_status(diagnostic, last_status);
}

TaiyinStatus EphemerisService::eval_direct_state(
    const EphemerisRequest& request,
    EphemerisSelectionResult* selection,
    CartesianState* out,
    EphemerisEvalDiagnostic* diagnostic
) noexcept {
    if (!selection || !out) {
        reset_diagnostic(diagnostic, request, TAIYIN_ERROR_INVALID_ARGUMENT);
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    TaiyinStatus status = select_calculation_route(request, selection, diagnostic);
    if (status != TAIYIN_STATUS_OK) {
        return status;
    }
    if (!cache_ || !cache_->eval_state(selection->bucket_descriptor.route_key, request.jd_tdb, out)) {
        return set_diagnostic_status(diagnostic, TAIYIN_EPHEMERIS_ERROR_EVAL_FAILED);
    }
    return set_diagnostic_status(diagnostic, TAIYIN_STATUS_OK);
}

TaiyinStatus EphemerisService::eval_descriptor_state_direct(
    const internal::EphemerisBlockDescriptor& source,
    double jd_tdb,
    int priority_bias,
    EphemerisSelectionResult* selection,
    CartesianState* out,
    EphemerisEvalDiagnostic* diagnostic
) noexcept {
    if (selection) {
        *selection = EphemerisSelectionResult();
    }
    if (!selection || !out) {
        return set_diagnostic_status(diagnostic, TAIYIN_ERROR_INVALID_ARGUMENT);
    }
    if (!cache_) {
        return set_diagnostic_status(diagnostic, TAIYIN_RUNTIME_ERROR_NOT_INITIALIZED);
    }
    if (diagnostic) {
        diagnostic->attempted_method_id = source.method_id;
    }

    internal::EphemerisBlockDescriptor bucket;
    if (!internal::make_cache_bucket_descriptor_for_jd(source, jd_tdb, &bucket)) {
        if (diagnostic) {
            diagnostic->nearest_coverage_start = source.jd_tdb_start;
            diagnostic->nearest_coverage_end = source.jd_tdb_end;
        }
        return set_diagnostic_status(diagnostic, TAIYIN_EPHEMERIS_ERROR_COVERAGE_GAP);
    }

    if (cache_->eval_state(bucket.route_key, jd_tdb, out)) {
        selection->source_descriptor = source;
        selection->bucket_descriptor = bucket;
        selection->cache_hit = true;
        selection->loaded = false;
        return set_diagnostic_status(diagnostic, TAIYIN_STATUS_OK);
    }

    internal::RouteInflightGuard guard(inflight_, bucket.route_key);
    const internal::RouteInflightAction action = guard.begin();
    if (action == internal::RouteInflightLoadNow) {
        const bool loaded = internal::load_descriptor_into_cache(bucket, cache_, priority_bias);
        guard.end(loaded);
        if (!loaded) {
            return set_diagnostic_status(diagnostic, TAIYIN_EPHEMERIS_ERROR_LOAD_FAILED);
        }
        if (!cache_->eval_state(bucket.route_key, jd_tdb, out)) {
            return set_diagnostic_status(diagnostic, TAIYIN_EPHEMERIS_ERROR_EVAL_FAILED);
        }

        selection->source_descriptor = source;
        selection->bucket_descriptor = bucket;
        selection->cache_hit = false;
        selection->loaded = true;
        return set_diagnostic_status(diagnostic, TAIYIN_STATUS_OK);
    }

    if (!cache_->eval_state(bucket.route_key, jd_tdb, out)) {
        return set_diagnostic_status(diagnostic, TAIYIN_EPHEMERIS_ERROR_LOAD_FAILED);
    }

    selection->source_descriptor = source;
    selection->bucket_descriptor = bucket;
    selection->cache_hit = true;
    selection->loaded = false;
    return set_diagnostic_status(diagnostic, TAIYIN_STATUS_OK);
}

TaiyinStatus EphemerisService::eval_composite_state(
    const EphemerisRequest& request,
    EphemerisResult* out,
    EphemerisEvalDiagnostic* diagnostic
) noexcept {
    if (!out) {
        reset_diagnostic(diagnostic, request, TAIYIN_ERROR_INVALID_ARGUMENT);
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    if (!catalog_ || !cache_) {
        return set_diagnostic_status(diagnostic, TAIYIN_RUNTIME_ERROR_NOT_INITIALIZED);
    }
    if (request.target_id != TAIYIN_BODY_EARTH && request.target_id != TAIYIN_BODY_MOON) {
        return set_diagnostic_status(diagnostic, TAIYIN_EPHEMERIS_ERROR_NO_ROUTE);
    }
    if (request.target_id == TAIYIN_BODY_MOON && request.center_id == TAIYIN_BODY_EARTH) {
        return set_diagnostic_status(diagnostic, TAIYIN_EPHEMERIS_ERROR_NO_ROUTE);
    }

    const EphemerisRequest emb_request = make_component_request(
        TAIYIN_BODY_EMB,
        request.center_id,
        request.frame,
        request.jd_tdb);
    const EphemerisRequest moon_request = make_component_request(
        TAIYIN_BODY_MOON,
        TAIYIN_BODY_EARTH,
        request.frame,
        request.jd_tdb);

    internal::EphemerisBlockQuery emb_query = make_query(emb_request);
    internal::EphemerisBlockQuery moon_query = make_query(moon_request);

    std::vector<const internal::EphemerisBlockDescriptor*> emb_ranked;
    std::vector<const internal::EphemerisBlockDescriptor*> moon_ranked;
    if (priorities_) {
        if (!catalog_->rank_candidates(emb_query, *priorities_, &emb_ranked)) {
            return diagnose_composite_component(catalog_, emb_request, diagnostic);
        }
        if (!catalog_->rank_candidates(moon_query, *priorities_, &moon_ranked)) {
            return diagnose_composite_component(catalog_, moon_request, diagnostic);
        }
    } else {
        if (!catalog_->find_candidates(emb_query, &emb_ranked)) {
            return diagnose_composite_component(catalog_, emb_request, diagnostic);
        }
        if (!catalog_->find_candidates(moon_query, &moon_ranked)) {
            return diagnose_composite_component(catalog_, moon_request, diagnostic);
        }
    }

    const double earth_factor = 1.0 / (1.0 + TAIYIN_EARTH_MOON_MASS_RATIO);
    const double moon_factor = TAIYIN_EARTH_MOON_MASS_RATIO / (1.0 + TAIYIN_EARTH_MOON_MASS_RATIO);
    bool saw_same_method_pair = false;
    TaiyinStatus last_status = TAIYIN_EPHEMERIS_ERROR_COMPOSITE_METHOD_MISMATCH;

    for (size_t emb_index = 0; emb_index < emb_ranked.size(); ++emb_index) {
        const internal::EphemerisBlockDescriptor* emb_source = emb_ranked[emb_index];
        if (!emb_source) {
            continue;
        }

        for (size_t moon_index = 0; moon_index < moon_ranked.size(); ++moon_index) {
            const internal::EphemerisBlockDescriptor* moon_source = moon_ranked[moon_index];
            if (!moon_source || moon_source->method_id != emb_source->method_id) {
                continue;
            }
            saw_same_method_pair = true;
            if (diagnostic) {
                diagnostic->candidate_count = static_cast<int>(emb_ranked.size() + moon_ranked.size());
                diagnostic->attempted_method_id = emb_source->method_id;
            }

            EphemerisSelectionResult emb_selection;
            EphemerisSelectionResult moon_selection;
            CartesianState emb_state;
            CartesianState moon_state;
            const int emb_priority_bias = cache_priority_bias_for_descriptor(*emb_source, priorities_);
            const int moon_priority_bias = cache_priority_bias_for_descriptor(*moon_source, priorities_);
            last_status = eval_descriptor_state_direct(
                *emb_source,
                request.jd_tdb,
                emb_priority_bias,
                &emb_selection,
                &emb_state,
                diagnostic);
            if (last_status != TAIYIN_STATUS_OK) {
                if (diagnostic) {
                    diagnostic->component_target_id = emb_source->target_id;
                    diagnostic->component_center_id = emb_source->center_id;
                    diagnostic->component_method_id = emb_source->method_id;
                }
                continue;
            }
            last_status = eval_descriptor_state_direct(
                *moon_source,
                request.jd_tdb,
                moon_priority_bias,
                &moon_selection,
                &moon_state,
                diagnostic);
            if (last_status != TAIYIN_STATUS_OK) {
                if (diagnostic) {
                    diagnostic->component_target_id = moon_source->target_id;
                    diagnostic->component_center_id = moon_source->center_id;
                    diagnostic->component_method_id = moon_source->method_id;
                }
                continue;
            }

            if (request.target_id == TAIYIN_BODY_EARTH) {
                out->state = cartesian_state_subtract(
                    emb_state,
                    cartesian_state_scale(moon_state, earth_factor));
            } else {
                out->state = cartesian_state_add(
                    emb_state,
                    cartesian_state_scale(moon_state, moon_factor));
            }

            internal::EphemerisBlockDescriptor descriptor = emb_selection.source_descriptor;
            descriptor.target_id = request.target_id;
            descriptor.center_id = request.center_id;
            descriptor.method_id = emb_selection.source_descriptor.method_id;
            descriptor.frame = request.frame;
            descriptor.route_key = internal::EphemerisRouteKey(
                request.target_id,
                request.center_id,
                emb_selection.bucket_descriptor.route_key.method_id,
                emb_selection.bucket_descriptor.route_key.bucket_id);
            descriptor.jd_tdb_start = std::max(
                emb_selection.bucket_descriptor.jd_tdb_start,
                moon_selection.bucket_descriptor.jd_tdb_start);
            descriptor.jd_tdb_end = std::min(
                emb_selection.bucket_descriptor.jd_tdb_end,
                moon_selection.bucket_descriptor.jd_tdb_end);
            descriptor.path.clear();

            out->descriptor = descriptor;
            out->cache_hit = emb_selection.cache_hit && moon_selection.cache_hit;
            return set_diagnostic_status(diagnostic, TAIYIN_STATUS_OK);
        }
    }

    if (!saw_same_method_pair) {
        return set_diagnostic_status(diagnostic, TAIYIN_EPHEMERIS_ERROR_COMPOSITE_METHOD_MISMATCH);
    }
    return set_diagnostic_status(diagnostic, last_status);
}

TaiyinStatus EphemerisService::eval_state(
    const EphemerisRequest& request,
    EphemerisResult* out,
    EphemerisEvalDiagnostic* diagnostic
) noexcept {
    if (out) {
        *out = EphemerisResult();
    }
    reset_diagnostic(diagnostic, request, TAIYIN_STATUS_OK);
    if (!out) {
        return set_diagnostic_status(diagnostic, TAIYIN_ERROR_INVALID_ARGUMENT);
    }

    EphemerisSelectionResult selection;
    CartesianState state;
    const TaiyinStatus direct_status = eval_direct_state(request, &selection, &state, diagnostic);
    if (direct_status == TAIYIN_STATUS_OK) {
        out->state = state;
        out->descriptor = selection.source_descriptor;
        out->cache_hit = selection.cache_hit;
        return set_diagnostic_status(diagnostic, TAIYIN_STATUS_OK);
    }

    if (request.target_id == TAIYIN_BODY_EARTH || request.target_id == TAIYIN_BODY_MOON) {
        const TaiyinStatus composite_status = eval_composite_state(request, out, diagnostic);
        if (composite_status == TAIYIN_STATUS_OK) {
            return TAIYIN_STATUS_OK;
        }
        return composite_status;
    }

    return set_diagnostic_status(diagnostic, direct_status);
}

}  // namespace runtime
}  // namespace taiyin
