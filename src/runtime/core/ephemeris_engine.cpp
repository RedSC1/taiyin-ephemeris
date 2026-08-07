#include "taiyin/runtime/ephemeris_engine.h"

#include "taiyin/body_id.h"
#include "taiyin/internal/descriptor_loader.h"
#include "taiyin/internal/ephemeris_discovery.h"
#include "taiyin/physical_constants.h"
#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <limits>
#include <new>
#include <string>
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
    const SplitJulianDate& jd_tdb,
    uint32_t components,
    uint64_t route_rule_id,
    const internal::EphemerisRouteRuleTable* route_rules,
    bool include_descriptor
) noexcept {
    EphemerisRequest request;
    request.target_id = target_id;
    request.center_id = center_id;
    request.frame = frame;
    request.jd_tdb = jd_tdb;
    request.components = components;
    request.route_rule_id = route_rule_id;
    request.route_rules = route_rules;
    request.include_descriptor = include_descriptor;
    return request;
}

bool descriptor_matches_rule(
    const internal::EphemerisBlockDescriptor& descriptor,
    const internal::EphemerisRouteRule& rule
) noexcept {
    return rule.source_id == 0 || descriptor.source_key.source_id == rule.source_id;
}

bool status_allows_route_fallback(Status status) noexcept {
    return status == TAIYIN_EPHEMERIS_ERROR_NO_ROUTE
        || status == TAIYIN_EPHEMERIS_ERROR_COVERAGE_GAP
        || status == TAIYIN_EPHEMERIS_ERROR_COMPOSITE_MISSING_COMPONENT
        || status == TAIYIN_EPHEMERIS_ERROR_COMPOSITE_COVERAGE_GAP;
}

int barycenter_for_physical_body(int body_id) noexcept {
    switch (body_id) {
    case TAIYIN_BODY_MERCURY:
        return TAIYIN_BODY_MERCURY_BARYCENTER;
    case TAIYIN_BODY_VENUS:
        return TAIYIN_BODY_VENUS_BARYCENTER;
    case TAIYIN_BODY_MARS:
        return TAIYIN_BODY_MARS_BARYCENTER;
    case TAIYIN_BODY_JUPITER:
        return TAIYIN_BODY_JUPITER_BARYCENTER;
    case TAIYIN_BODY_SATURN:
        return TAIYIN_BODY_SATURN_BARYCENTER;
    case TAIYIN_BODY_URANUS:
        return TAIYIN_BODY_URANUS_BARYCENTER;
    case TAIYIN_BODY_NEPTUNE:
        return TAIYIN_BODY_NEPTUNE_BARYCENTER;
    case TAIYIN_BODY_PLUTO:
        return TAIYIN_BODY_PLUTO_BARYCENTER;
    default:
        return 0;
    }
}

bool body_is_barycenter_alias(int body_id) noexcept {
    return body_id == TAIYIN_BODY_MERCURY || body_id == TAIYIN_BODY_VENUS;
}

const internal::EphemerisRouteRuleTable* request_route_rules(
    const EphemerisRequest& request,
    const internal::EphemerisRouteRuleTable* default_rules
) noexcept {
    return request.route_rules ? request.route_rules : default_rules;
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
    Status status
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

Status set_diagnostic_status(
    EphemerisEvalDiagnostic* diagnostic,
    Status status
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
    const SplitJulianDate& jd_tdb
) noexcept {
    return descriptor.jd_tdb_end > descriptor.jd_tdb_start
        && days_between_split_jd(descriptor.jd_tdb_start_split, jd_tdb) >= 0.0
        && days_between_split_jd(descriptor.jd_tdb_end_split, jd_tdb) < 0.0;
}

double coverage_distance(
    const internal::EphemerisBlockDescriptor& descriptor,
    const SplitJulianDate& jd_tdb
) noexcept {
    if (descriptor_covers_jd(descriptor, jd_tdb)) {
        return 0.0;
    }
    const double before_start = days_between_split_jd(jd_tdb, descriptor.jd_tdb_start_split);
    if (before_start > 0.0) {
        return before_start;
    }
    return days_between_split_jd(descriptor.jd_tdb_end_split, jd_tdb);
}

Status diagnose_route_availability(
    const internal::EphemerisBlockCatalog* catalog,
    const EphemerisRequest& request,
    EphemerisEvalDiagnostic* diagnostic
) noexcept {
    if (!catalog) {
        return set_diagnostic_status(diagnostic, TAIYIN_RUNTIME_ERROR_NOT_INITIALIZED);
    }

    internal::EphemerisBlockQuery query = make_query(request);
    int route_count = 0;
    internal::EphemerisBlockDescriptor nearest;
    bool has_nearest = false;
    double nearest_distance = std::numeric_limits<double>::infinity();

    for (size_t i = 0; i < catalog->size(); ++i) {
        internal::EphemerisBlockDescriptor descriptor;
        if (!catalog->get(i, &descriptor) || !descriptor_same_route(descriptor, query)) {
            continue;
        }
        ++route_count;
        const double distance = coverage_distance(descriptor, request.jd_tdb);
        if (!has_nearest || distance < nearest_distance) {
            nearest = descriptor;
            has_nearest = true;
            nearest_distance = distance;
        }
    }

    if (diagnostic) {
        diagnostic->candidate_count = route_count;
        if (has_nearest) {
            diagnostic->nearest_coverage_start = nearest.jd_tdb_start;
            diagnostic->nearest_coverage_end = nearest.jd_tdb_end;
            diagnostic->attempted_method_id = nearest.method_id;
        }
    }

    return set_diagnostic_status(
        diagnostic,
        route_count == 0 ? TAIYIN_EPHEMERIS_ERROR_NO_ROUTE : TAIYIN_EPHEMERIS_ERROR_COVERAGE_GAP);
}

Status diagnose_composite_component(
    const internal::EphemerisBlockCatalog* catalog,
    const EphemerisRequest& component_request,
    EphemerisEvalDiagnostic* diagnostic
) noexcept {
    Status status = diagnose_route_availability(catalog, component_request, diagnostic);
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

internal::EphemerisBlockDescriptor make_synthetic_descriptor(
    const EphemerisRequest& request,
    const internal::EphemerisBlockDescriptor& source
) noexcept {
    internal::EphemerisBlockDescriptor descriptor = source;
    descriptor.target_id = request.target_id;
    descriptor.center_id = request.center_id;
    descriptor.frame = request.frame;
    descriptor.route_key = internal::EphemerisRouteKey(
        request.target_id,
        request.center_id,
        source.route_key.method_id,
        source.route_key.bucket_id);
    descriptor.path.clear();
    return descriptor;
}

Status make_zero_result(
    const EphemerisRequest& request,
    EphemerisResult* out,
    EphemerisEvalDiagnostic* diagnostic
) noexcept {
    if (!out) {
        return set_diagnostic_status(diagnostic, TAIYIN_ERROR_INVALID_ARGUMENT);
    }
    out->state = CartesianState();
    out->descriptor = internal::EphemerisBlockDescriptor();
    out->descriptor.target_id = request.target_id;
    out->descriptor.center_id = request.center_id;
    out->descriptor.frame = request.frame;
    out->descriptor.route_key = internal::EphemerisRouteKey(request.target_id, request.center_id, 0, 0);
    out->descriptor.jd_tdb_start = split_julian_date_to_double(request.jd_tdb);
    out->descriptor.jd_tdb_end = split_julian_date_to_double(request.jd_tdb);
    out->descriptor.jd_tdb_start_split = request.jd_tdb;
    out->descriptor.jd_tdb_end_split = request.jd_tdb;
    out->cache_hit = true;
    return set_diagnostic_status(diagnostic, TAIYIN_STATUS_OK);
}

internal::EphemerisSegmentCacheKey make_method_cache_key(
    const internal::EphemerisBlockDescriptor& source,
    int bucket_id
) noexcept {
    return internal::EphemerisSegmentCacheKey(
        static_cast<uint32_t>(source.format),
        source.target_id,
        source.center_id,
        source.method_id,
        source.frame,
        source.source_key,
        static_cast<int64_t>(bucket_id));
}

void destroy_storage_cache_data(void* data) {
    internal::StorageEphemerisBlock* storage =
        static_cast<internal::StorageEphemerisBlock*>(data);
    if (storage) {
        internal::destroy_storage_ephemeris_block(storage);
        delete storage;
    }
}

struct EvalStorageCacheDataContext {
    int target_id;
    SplitJulianDate jd_tdb;
    uint32_t components;
    CartesianState* out;
};

bool eval_storage_cache_data(const void* data, void* user) {
    const internal::StorageEphemerisBlock* storage =
        static_cast<const internal::StorageEphemerisBlock*>(data);
    EvalStorageCacheDataContext* context =
        static_cast<EvalStorageCacheDataContext*>(user);
    if (!storage || !context || !context->out) {
        return false;
    }

    internal::CompiledEphemerisBlock block;
    if (!internal::get_compiled_block_from_storage(storage, context->target_id, &block)) {
        return false;
    }
    return internal::eval_compiled_ephemeris_block_components(
        context->jd_tdb,
        &block,
        context->components,
        context->out);
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
            status_name(diagnostic.status),
            static_cast<int>(diagnostic.status),
            status_message(diagnostic.status),
            diagnostic.target_id,
            diagnostic.center_id,
            static_cast<int>(diagnostic.frame),
            split_julian_date_to_double(diagnostic.jd_tdb))) {
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
    if (diagnostic.time_scale_route != 0
        || diagnostic.time_scale_fallback_reason != 0
        || diagnostic.time_scale_flags != 0) {
        append_diagnostic_text(
            buffer,
            buffer_size,
            &length,
            "; time_scale route=%u fallback=%u flags=%u delta_t=%.17g tai_minus_utc=%.17g dut1=%.17g",
            static_cast<unsigned>(diagnostic.time_scale_route),
            static_cast<unsigned>(diagnostic.time_scale_fallback_reason),
            static_cast<unsigned>(diagnostic.time_scale_flags),
            diagnostic.delta_t_seconds,
            diagnostic.tai_minus_utc_seconds,
            diagnostic.dut1_seconds);
    }

    if (buffer && buffer_size > 0) {
        if (length >= buffer_size) {
            buffer[buffer_size - 1] = '\0';
        }
    }
    return length;
}

EphemerisEngine::EphemerisEngine() noexcept
    : catalog_(0),
      segment_cache_(0),
      default_route_rules_(0),
      body_registry_(0),
      inflight_() {}

EphemerisEngine::~EphemerisEngine() noexcept {}

void EphemerisEngine::set_catalog(internal::EphemerisBlockCatalog* catalog) noexcept {
    catalog_ = catalog;
}

void EphemerisEngine::set_segment_cache(internal::EphemerisSegmentCache* cache) noexcept {
    segment_cache_ = cache;
}

void EphemerisEngine::set_body_registry(const EphemerisBodyRegistry* registry) noexcept {
    body_registry_ = registry;
}

void EphemerisEngine::set_default_route_rules(const internal::EphemerisRouteRuleTable* rules) noexcept {
    default_route_rules_ = rules;
}

const internal::EphemerisBlockCatalog* EphemerisEngine::catalog() const noexcept {
    return catalog_;
}

internal::EphemerisSegmentCache* EphemerisEngine::segment_cache() const noexcept {
    return segment_cache_;
}

const EphemerisBodyRegistry* EphemerisEngine::body_registry() const noexcept {
    return body_registry_;
}

const internal::EphemerisRouteRuleTable* EphemerisEngine::default_route_rules() const noexcept {
    return default_route_rules_;
}

bool EphemerisEngine::find_descriptor(
    const EphemerisRequest& request,
    internal::EphemerisBlockDescriptor* out
) const noexcept {
    if (out) {
        *out = internal::EphemerisBlockDescriptor();
    }
    if (!out || !catalog_) {
        return false;
    }

    internal::EphemerisBlockQuery query = make_query(request);
    if (!default_route_rules_) {
        return false;
    }
    const internal::EphemerisRouteRuleTable* rules_table = request_route_rules(request, default_route_rules_);
    if (!rules_table) {
        return false;
    }
    const std::vector<internal::EphemerisRouteRule>& rules = rules_table->rules();
    if (rules.empty()) {
        return false;
    }
    for (size_t rule_index = 0; rule_index < rules.size(); ++rule_index) {
        std::vector<internal::EphemerisBlockDescriptor> candidates;
        if (!catalog_->find_method_candidates(query, rules[rule_index].method_id, &candidates)) {
            continue;
        }
        for (size_t i = 0; i < candidates.size(); ++i) {
            if (descriptor_matches_rule(candidates[i], rules[rule_index])) {
                *out = candidates[i];
                return true;
            }
        }
    }
    return false;
}

Status EphemerisEngine::eval_direct_state(
    const EphemerisRequest& request,
    EphemerisSelectionResult* selection,
    CartesianState* out,
    EphemerisEvalDiagnostic* diagnostic
) noexcept {
    if (!selection || !out) {
        reset_diagnostic(diagnostic, request, TAIYIN_ERROR_INVALID_ARGUMENT);
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }

    return eval_method_queue_state(request, selection, out, diagnostic);
}

Status EphemerisEngine::eval_method_queue_state(
    const EphemerisRequest& request,
    EphemerisSelectionResult* selection,
    CartesianState* out,
    EphemerisEvalDiagnostic* diagnostic
) noexcept {
    if (!catalog_ || !segment_cache_ || !default_route_rules_ || !selection || !out) {
        return set_diagnostic_status(diagnostic, TAIYIN_RUNTIME_ERROR_NOT_INITIALIZED);
    }

    internal::EphemerisBlockQuery query = make_query(request);
    const internal::EphemerisRouteRuleTable* rules_table = request_route_rules(request, default_route_rules_);
    if (!rules_table) {
        return set_diagnostic_status(diagnostic, TAIYIN_EPHEMERIS_ERROR_NO_ROUTE);
    }
    const std::vector<internal::EphemerisRouteRule>& rules = rules_table->rules();
    if (rules.empty()) {
        return set_diagnostic_status(diagnostic, TAIYIN_EPHEMERIS_ERROR_NO_ROUTE);
    }

    int candidate_count = 0;
    Status last_status = TAIYIN_EPHEMERIS_ERROR_NO_ROUTE;
    std::vector<internal::EphemerisBlockDescriptor> candidates;
    for (size_t rule_index = 0; rule_index < rules.size(); ++rule_index) {
        if (!catalog_->find_method_candidates(query, rules[rule_index].method_id, &candidates)) {
            continue;
        }
        for (size_t i = 0; i < candidates.size(); ++i) {
            if (!descriptor_matches_rule(candidates[i], rules[rule_index])) {
                continue;
            }
            ++candidate_count;
            Status status = eval_method_descriptor_state(
                candidates[i],
                request.jd_tdb,
                request.components,
                request.include_descriptor,
                selection,
                out,
                diagnostic);
            if (status == TAIYIN_STATUS_OK) {
                if (diagnostic) {
                    diagnostic->candidate_count = candidate_count;
                }
                return TAIYIN_STATUS_OK;
            }
            last_status = status;
        }
    }

    if (candidate_count == 0) {
        return set_diagnostic_status(diagnostic, TAIYIN_EPHEMERIS_ERROR_NO_ROUTE);
    }
    if (diagnostic) {
        diagnostic->candidate_count = candidate_count;
    }
    return set_diagnostic_status(diagnostic, last_status);
}

Status EphemerisEngine::eval_direct_state_for_rule(
    const EphemerisRequest& request,
    const internal::EphemerisRouteRule& rule,
    EphemerisSelectionResult* selection,
    CartesianState* out,
    EphemerisEvalDiagnostic* diagnostic
) noexcept {
    if (!catalog_ || !segment_cache_ || !selection || !out) {
        return set_diagnostic_status(diagnostic, TAIYIN_RUNTIME_ERROR_NOT_INITIALIZED);
    }

    internal::EphemerisBlockQuery query = make_query(request);
    std::vector<internal::EphemerisBlockDescriptor> candidates;
    if (!catalog_->find_method_candidates(query, rule.method_id, &candidates)) {
        return set_diagnostic_status(diagnostic, TAIYIN_EPHEMERIS_ERROR_NO_ROUTE);
    }

    int candidate_count = 0;
    Status last_status = TAIYIN_EPHEMERIS_ERROR_NO_ROUTE;
    for (size_t i = 0; i < candidates.size(); ++i) {
        if (!descriptor_matches_rule(candidates[i], rule)) {
            continue;
        }
        ++candidate_count;
        const Status status = eval_method_descriptor_state(
            candidates[i],
            request.jd_tdb,
            request.components,
            request.include_descriptor,
            selection,
            out,
            diagnostic);
        if (status == TAIYIN_STATUS_OK) {
            if (diagnostic) {
                diagnostic->candidate_count = candidate_count;
            }
            return TAIYIN_STATUS_OK;
        }
        last_status = status;
    }

    if (candidate_count == 0) {
        return set_diagnostic_status(diagnostic, TAIYIN_EPHEMERIS_ERROR_NO_ROUTE);
    }
    if (diagnostic) {
        diagnostic->candidate_count = candidate_count;
    }
    return set_diagnostic_status(diagnostic, last_status);
}

Status EphemerisEngine::eval_method_descriptor_state(
    const internal::EphemerisBlockDescriptor& source,
    const SplitJulianDate& jd_tdb,
    uint32_t components,
    bool include_descriptor,
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
    if (!segment_cache_) {
        return set_diagnostic_status(diagnostic, TAIYIN_RUNTIME_ERROR_NOT_INITIALIZED);
    }
    if (diagnostic) {
        diagnostic->attempted_method_id = source.method_id;
    }

    int bucket_id = 0;
    if (!internal::cache_bucket_id_for_jd(source, jd_tdb, &bucket_id)) {
        if (diagnostic) {
            diagnostic->nearest_coverage_start = source.jd_tdb_start;
            diagnostic->nearest_coverage_end = source.jd_tdb_end;
        }
        return set_diagnostic_status(diagnostic, TAIYIN_EPHEMERIS_ERROR_COVERAGE_GAP);
    }

    const internal::EphemerisSegmentCacheKey cache_key = make_method_cache_key(source, bucket_id);
    EvalStorageCacheDataContext context;
    context.target_id = source.target_id;
    context.jd_tdb = jd_tdb;
    context.components = components;
    context.out = out;

    if (segment_cache_->with_data(cache_key, eval_storage_cache_data, &context)) {
        if (include_descriptor) {
            selection->source_descriptor = source;
            selection->has_source_descriptor = true;
        }
        selection->cache_hit = true;
        selection->loaded = false;
        return set_diagnostic_status(diagnostic, TAIYIN_STATUS_OK);
    }

    internal::RouteInflightGuard guard(inflight_, cache_key);
    const internal::RouteInflightAction action = guard.begin();
    if (action == internal::RouteInflightLoadNow) {
        internal::EphemerisBlockDescriptor bucket;
        if (!internal::make_cache_bucket_descriptor_for_jd(source, jd_tdb, &bucket)) {
            guard.end(false);
            return set_diagnostic_status(diagnostic, TAIYIN_EPHEMERIS_ERROR_COVERAGE_GAP);
        }
        internal::StorageEphemerisBlock* storage =
            new (std::nothrow) internal::StorageEphemerisBlock();
        if (!storage) {
            guard.end(false);
            return set_diagnostic_status(diagnostic, TAIYIN_EPHEMERIS_ERROR_LOAD_FAILED);
        }
        internal::EphemerisSourceIndex source_index;
        const internal::EphemerisSourceIndex* source_index_ptr = 0;
        if (catalog_) {
            if (catalog_->find_source_index(bucket.source_key, &source_index)
                && source_index.payload) {
                source_index_ptr = &source_index;
            } else if (internal::load_descriptor_source_index(bucket, &source_index)
                && catalog_->add_source_index(source_index)) {
                source_index_ptr = &source_index;
            }
        }
        if (!internal::load_descriptor_ephemeris_block(bucket, source_index_ptr, storage)) {
            guard.end(false);
            delete storage;
            return set_diagnostic_status(diagnostic, TAIYIN_EPHEMERIS_ERROR_LOAD_FAILED);
        }
        if (bucket.format == internal::EphemerisBlockFormat::Opm2 && source_index_ptr) {
            storage->source_owner = source_index_ptr->payload;
        }

        internal::EphemerisSegmentCacheData data(storage, destroy_storage_cache_data);
        const bool inserted = segment_cache_->insert(cache_key, data);
        guard.end(inserted);
        if (!inserted) {
            destroy_storage_cache_data(storage);
            return set_diagnostic_status(diagnostic, TAIYIN_EPHEMERIS_ERROR_LOAD_FAILED);
        }

        if (!segment_cache_->with_data(cache_key, eval_storage_cache_data, &context)) {
            return set_diagnostic_status(diagnostic, TAIYIN_EPHEMERIS_ERROR_EVAL_FAILED);
        }

        if (include_descriptor) {
            selection->source_descriptor = source;
            selection->has_source_descriptor = true;
        }
        selection->cache_hit = false;
        selection->loaded = true;
        return set_diagnostic_status(diagnostic, TAIYIN_STATUS_OK);
    }

    if (!segment_cache_->with_data(cache_key, eval_storage_cache_data, &context)) {
        return set_diagnostic_status(diagnostic, TAIYIN_EPHEMERIS_ERROR_LOAD_FAILED);
    }

    if (include_descriptor) {
        selection->source_descriptor = source;
        selection->has_source_descriptor = true;
    }
    selection->cache_hit = true;
    selection->loaded = false;
    return set_diagnostic_status(diagnostic, TAIYIN_STATUS_OK);
}

Status EphemerisEngine::eval_direct_body_state(
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
    const Status status = eval_direct_state(request, &selection, &state, diagnostic);
    if (status != TAIYIN_STATUS_OK) {
        return set_diagnostic_status(diagnostic, status);
    }

    out->state = state;
    if (request.include_descriptor) {
        out->descriptor = selection.has_source_descriptor
            ? selection.source_descriptor
            : internal::EphemerisBlockDescriptor();
    }
    out->cache_hit = selection.cache_hit;
    return set_diagnostic_status(diagnostic, TAIYIN_STATUS_OK);
}

Status EphemerisEngine::eval_direct_body_state_for_rule(
    const EphemerisRequest& request,
    const internal::EphemerisRouteRule& rule,
    EphemerisResult* out,
    EphemerisEvalDiagnostic* diagnostic
) noexcept {
    if (out) {
        *out = EphemerisResult();
    }
    if (!out) {
        return set_diagnostic_status(diagnostic, TAIYIN_ERROR_INVALID_ARGUMENT);
    }

    EphemerisSelectionResult selection;
    CartesianState state;
    const Status status = eval_direct_state_for_rule(request, rule, &selection, &state, diagnostic);
    if (status != TAIYIN_STATUS_OK) {
        return set_diagnostic_status(diagnostic, status);
    }

    out->state = state;
    if (request.include_descriptor) {
        out->descriptor = selection.has_source_descriptor
            ? selection.source_descriptor
            : internal::EphemerisBlockDescriptor();
    }
    out->cache_hit = selection.cache_hit;
    return set_diagnostic_status(diagnostic, TAIYIN_STATUS_OK);
}

Status EphemerisEngine::eval_body_wrt_sun_for_rule(
    int body_id,
    internal::EphemerisFrame frame,
    const SplitJulianDate& jd_tdb,
    uint32_t components,
    uint64_t route_rule_id,
    const internal::EphemerisRouteRule& rule,
    bool include_descriptor,
    EphemerisResult* out,
    EphemerisEvalDiagnostic* diagnostic
) noexcept {
    EphemerisRequest request = make_component_request(
        body_id,
        TAIYIN_BODY_SUN,
        frame,
        jd_tdb,
        components,
        route_rule_id,
        0,
        include_descriptor);
    if (out) {
        *out = EphemerisResult();
    }
    if (!out) {
        return set_diagnostic_status(diagnostic, TAIYIN_ERROR_INVALID_ARGUMENT);
    }
    if (body_id == TAIYIN_BODY_SUN) {
        return make_zero_result(request, out, diagnostic);
    }
    if (body_id == TAIYIN_BODY_SSB) {
        EphemerisResult sun_ssb;
        const EphemerisRequest sun_ssb_request = make_component_request(
            TAIYIN_BODY_SUN,
            TAIYIN_BODY_SSB,
            frame,
            jd_tdb,
            components,
            route_rule_id,
            0,
            include_descriptor);
        const Status sun_ssb_status =
            eval_direct_body_state_for_rule(sun_ssb_request, rule, &sun_ssb, diagnostic);
        if (sun_ssb_status != TAIYIN_STATUS_OK) {
            if (diagnostic) {
                diagnostic->component_target_id = sun_ssb_request.target_id;
                diagnostic->component_center_id = sun_ssb_request.center_id;
                diagnostic->component_method_id = sun_ssb.descriptor.method_id;
            }
            return set_diagnostic_status(diagnostic, sun_ssb_status);
        }
        out->state = cartesian_state_negate(sun_ssb.state);
        if (include_descriptor) {
            out->descriptor = make_synthetic_descriptor(request, sun_ssb.descriptor);
        }
        out->cache_hit = sun_ssb.cache_hit;
        return set_diagnostic_status(diagnostic, TAIYIN_STATUS_OK);
    }

    EphemerisResult direct;
    Status direct_status = eval_direct_body_state_for_rule(request, rule, &direct, diagnostic);
    if (direct_status == TAIYIN_STATUS_OK) {
        *out = direct;
        return TAIYIN_STATUS_OK;
    }

    if (body_id == TAIYIN_BODY_EARTH || body_id == TAIYIN_BODY_MOON) {
        EphemerisResult emb_sun;
        Status emb_status = eval_body_wrt_sun_for_rule(
            TAIYIN_BODY_EMB,
            frame,
            jd_tdb,
            components,
            route_rule_id,
            rule,
            include_descriptor,
            &emb_sun,
            diagnostic);
        if (emb_status != TAIYIN_STATUS_OK) {
            return set_diagnostic_status(diagnostic, emb_status);
        }

        EphemerisResult moon_earth;
        const EphemerisRequest moon_earth_request = make_component_request(
            TAIYIN_BODY_MOON,
            TAIYIN_BODY_EARTH,
            frame,
            jd_tdb,
            components,
            route_rule_id,
            0,
            include_descriptor);
        Status moon_status =
            eval_direct_body_state_for_rule(moon_earth_request, rule, &moon_earth, diagnostic);
        if (moon_status != TAIYIN_STATUS_OK) {
            if (diagnostic) {
                diagnostic->component_target_id = moon_earth_request.target_id;
                diagnostic->component_center_id = moon_earth_request.center_id;
                diagnostic->component_method_id = moon_earth.descriptor.method_id;
            }
            return set_diagnostic_status(diagnostic, moon_status);
        }

        const double earth_factor = 1.0 / (1.0 + TAIYIN_EARTH_MOON_MASS_RATIO);
        const double moon_factor = TAIYIN_EARTH_MOON_MASS_RATIO / (1.0 + TAIYIN_EARTH_MOON_MASS_RATIO);
        if (body_id == TAIYIN_BODY_EARTH) {
            out->state = cartesian_state_subtract(
                emb_sun.state,
                cartesian_state_scale(moon_earth.state, earth_factor));
        } else {
            out->state = cartesian_state_add(
                emb_sun.state,
                cartesian_state_scale(moon_earth.state, moon_factor));
        }
        if (include_descriptor) {
            out->descriptor = make_synthetic_descriptor(request, emb_sun.descriptor);
            out->descriptor.jd_tdb_start = std::max(
                emb_sun.descriptor.jd_tdb_start,
                moon_earth.descriptor.jd_tdb_start);
            out->descriptor.jd_tdb_end = std::min(
                emb_sun.descriptor.jd_tdb_end,
                moon_earth.descriptor.jd_tdb_end);
        }
        out->cache_hit = emb_sun.cache_hit && moon_earth.cache_hit;
        return set_diagnostic_status(diagnostic, TAIYIN_STATUS_OK);
    }

    EphemerisResult body_ssb;
    const EphemerisRequest body_ssb_request = make_component_request(
        body_id,
        TAIYIN_BODY_SSB,
        frame,
        jd_tdb,
        components,
        route_rule_id,
        0,
        include_descriptor);
    Status body_ssb_status =
        eval_direct_body_state_for_rule(body_ssb_request, rule, &body_ssb, diagnostic);
    if (body_ssb_status == TAIYIN_STATUS_OK) {
        EphemerisResult sun_ssb;
        const EphemerisRequest sun_ssb_request = make_component_request(
            TAIYIN_BODY_SUN,
            TAIYIN_BODY_SSB,
            frame,
            jd_tdb,
            components,
            route_rule_id,
            0,
            include_descriptor);
        Status sun_ssb_status =
            eval_direct_body_state_for_rule(sun_ssb_request, rule, &sun_ssb, diagnostic);
        if (sun_ssb_status == TAIYIN_STATUS_OK) {
            out->state = cartesian_state_subtract(body_ssb.state, sun_ssb.state);
            if (include_descriptor) {
                out->descriptor = make_synthetic_descriptor(request, body_ssb.descriptor);
                out->descriptor.jd_tdb_start = std::max(
                    body_ssb.descriptor.jd_tdb_start,
                    sun_ssb.descriptor.jd_tdb_start);
                out->descriptor.jd_tdb_end = std::min(
                    body_ssb.descriptor.jd_tdb_end,
                    sun_ssb.descriptor.jd_tdb_end);
            }
            out->cache_hit = body_ssb.cache_hit && sun_ssb.cache_hit;
            return set_diagnostic_status(diagnostic, TAIYIN_STATUS_OK);
        }
    }

    const int barycenter_id = barycenter_for_physical_body(body_id);
    if (barycenter_id != 0) {
        EphemerisResult barycenter_sun;
        Status barycenter_status = eval_body_wrt_sun_for_rule(
            barycenter_id,
            frame,
            jd_tdb,
            components,
            route_rule_id,
            rule,
            include_descriptor,
            &barycenter_sun,
            diagnostic);
        if (barycenter_status == TAIYIN_STATUS_OK) {
            if (body_is_barycenter_alias(body_id)) {
                *out = barycenter_sun;
                if (include_descriptor) {
                    out->descriptor = make_synthetic_descriptor(request, barycenter_sun.descriptor);
                }
                return set_diagnostic_status(diagnostic, TAIYIN_STATUS_OK);
            }

            EphemerisResult offset;
            const EphemerisRequest offset_request = make_component_request(
                body_id,
                barycenter_id,
                frame,
                jd_tdb,
                components,
                route_rule_id,
                0,
                include_descriptor);
            Status offset_status =
                eval_direct_body_state_for_rule(offset_request, rule, &offset, diagnostic);
            if (offset_status == TAIYIN_STATUS_OK) {
                out->state = cartesian_state_add(barycenter_sun.state, offset.state);
                if (include_descriptor) {
                    out->descriptor = make_synthetic_descriptor(request, barycenter_sun.descriptor);
                    out->descriptor.jd_tdb_start = std::max(
                        barycenter_sun.descriptor.jd_tdb_start,
                        offset.descriptor.jd_tdb_start);
                    out->descriptor.jd_tdb_end = std::min(
                        barycenter_sun.descriptor.jd_tdb_end,
                        offset.descriptor.jd_tdb_end);
                }
                out->cache_hit = barycenter_sun.cache_hit && offset.cache_hit;
                return set_diagnostic_status(diagnostic, TAIYIN_STATUS_OK);
            }
        }
    }

    return set_diagnostic_status(diagnostic, direct_status);
}

Status EphemerisEngine::eval_state_for_rule(
    const EphemerisRequest& request,
    const internal::EphemerisRouteRule& rule,
    EphemerisResult* out,
    EphemerisEvalDiagnostic* diagnostic
) noexcept {
    if (out) {
        *out = EphemerisResult();
    }
    if (!out) {
        return set_diagnostic_status(diagnostic, TAIYIN_ERROR_INVALID_ARGUMENT);
    }

    EphemerisResult direct;
    Status direct_status = eval_direct_body_state_for_rule(request, rule, &direct, diagnostic);
    if (direct_status == TAIYIN_STATUS_OK) {
        *out = direct;
        return TAIYIN_STATUS_OK;
    }

    if (request.frame != internal::IcrfJ2000Equatorial) {
        return set_diagnostic_status(diagnostic, direct_status);
    }
    if (request.target_id == request.center_id) {
        return make_zero_result(request, out, diagnostic);
    }

    EphemerisResult target_sun;
    Status status = eval_body_wrt_sun_for_rule(
        request.target_id,
        request.frame,
        request.jd_tdb,
        request.components,
        request.route_rule_id,
        rule,
        request.include_descriptor,
        &target_sun,
        diagnostic);
    if (status != TAIYIN_STATUS_OK) {
        return set_diagnostic_status(diagnostic, status);
    }

    if (request.center_id == TAIYIN_BODY_SUN) {
        *out = target_sun;
        if (request.include_descriptor) {
            out->descriptor = make_synthetic_descriptor(request, target_sun.descriptor);
        }
        return set_diagnostic_status(diagnostic, TAIYIN_STATUS_OK);
    }

    EphemerisResult center_sun;
    status = eval_body_wrt_sun_for_rule(
        request.center_id,
        request.frame,
        request.jd_tdb,
        request.components,
        request.route_rule_id,
        rule,
        request.include_descriptor,
        &center_sun,
        diagnostic);
    if (status != TAIYIN_STATUS_OK) {
        return set_diagnostic_status(diagnostic, status);
    }

    out->state = cartesian_state_subtract(target_sun.state, center_sun.state);
    if (request.include_descriptor) {
        out->descriptor = make_synthetic_descriptor(request, target_sun.descriptor);
        out->descriptor.jd_tdb_start = std::max(
            target_sun.descriptor.jd_tdb_start,
            center_sun.descriptor.jd_tdb_start);
        out->descriptor.jd_tdb_end = std::min(
            target_sun.descriptor.jd_tdb_end,
            center_sun.descriptor.jd_tdb_end);
    }
    out->cache_hit = target_sun.cache_hit && center_sun.cache_hit;
    return set_diagnostic_status(diagnostic, TAIYIN_STATUS_OK);
}

Status EphemerisEngine::eval_state(
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
    const internal::EphemerisRouteRuleTable* rules_table =
        request_route_rules(request, default_route_rules_);
    if (!rules_table) {
        return set_diagnostic_status(diagnostic, TAIYIN_EPHEMERIS_ERROR_NO_ROUTE);
    }
    const std::vector<internal::EphemerisRouteRule>& rules = rules_table->rules();
    if (rules.empty()) {
        return set_diagnostic_status(diagnostic, TAIYIN_EPHEMERIS_ERROR_NO_ROUTE);
    }

    Status last_status = TAIYIN_EPHEMERIS_ERROR_NO_ROUTE;
    for (size_t i = 0; i < rules.size(); ++i) {
        Status status = eval_state_for_rule(request, rules[i], out, diagnostic);
        if (status == TAIYIN_STATUS_OK) {
            return TAIYIN_STATUS_OK;
        }
        last_status = status;
        if (!status_allows_route_fallback(status)) {
            return set_diagnostic_status(diagnostic, status);
        }
    }

    return set_diagnostic_status(diagnostic, last_status);
}

}  // namespace runtime
}  // namespace taiyin
