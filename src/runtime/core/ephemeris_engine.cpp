#include "taiyin/runtime/ephemeris_engine.h"

#include "taiyin/body_id.h"
#include "taiyin/internal/descriptor_loader.h"
#include "taiyin/internal/ephemeris_discovery.h"
#include "taiyin/internal/ephemeris_source_identity.h"
#include "taiyin/internal/opm2.h"
#include "taiyin/internal/semi_analytic.h"
#include "taiyin/internal/spk_catalog_discovery.h"
#include "taiyin/physical_constants.h"
#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <limits>
#include <new>
#include <string>
#include <unordered_map>
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

int descriptor_match_rank(
    const internal::EphemerisBlockDescriptor& descriptor,
    const internal::EphemerisRouteRule& rule
) noexcept {
    if (rule.source_id == 0 || descriptor.source_key.source_id == rule.source_id) {
        return 0;
    }
    if (rule.allow_non_de_spk_auxiliary
        && descriptor.format == internal::EphemerisBlockFormat::Spk
        && !internal::is_jpl_de_spk_source_id(descriptor.source_key.source_id)) {
        return 1;
    }
    return -1;
}

void order_candidates_by_source_preference(
    std::vector<internal::EphemerisBlockDescriptor>* candidates,
    const internal::EphemerisSourcePriorityTable* priorities
) {
    if (!candidates || !priorities || candidates->size() < 2) {
        return;
    }
    std::stable_sort(
        candidates->begin(),
        candidates->end(),
        [priorities](
            const internal::EphemerisBlockDescriptor& lhs,
            const internal::EphemerisBlockDescriptor& rhs
        ) {
            return priorities->compare(lhs, rhs) < 0;
        });
}

struct RouteRulePreference {
    const internal::EphemerisRouteRule* rule;
    int64_t source_priority;
    size_t original_index;
    bool has_catalog_candidate;

    RouteRulePreference() noexcept
        : rule(0),
          source_priority(0),
          original_index(0),
          has_catalog_candidate(false) {}
};

internal::EphemerisBlockFormat provider_format_for_method(int method_id) noexcept {
    if (method_id == internal::SPK_METHOD_ID) {
        return internal::EphemerisBlockFormat::Spk;
    }
    if (method_id == static_cast<int>(internal::OPM2_METHOD_ID)) {
        return internal::EphemerisBlockFormat::Opm2;
    }
    return internal::EphemerisBlockFormat::FormatUnknown;
}

int64_t default_route_rule_source_priority(
    const internal::EphemerisRouteRule& rule,
    const internal::EphemerisSourcePriorityTable& priorities
) noexcept {
    internal::EphemerisBlockDescriptor product;
    product.method_id = rule.method_id;
    product.format = provider_format_for_method(rule.method_id);
    product.source_key.source_id = rule.source_id;
    return priorities.priority_for(product);
}

std::vector<const internal::EphemerisRouteRule*> order_route_rules_by_source_preference(
    const std::vector<internal::EphemerisRouteRule>& rules,
    const internal::EphemerisBlockCatalog* catalog,
    const internal::EphemerisSourcePriorityTable* priorities
) {
    std::vector<const internal::EphemerisRouteRule*> ordered;
    ordered.reserve(rules.size());
    for (size_t i = 0; i < rules.size(); ++i) {
        ordered.push_back(&rules[i]);
    }
    if (!catalog || !priorities || priorities->empty()) {
        return ordered;
    }

    const int provider_methods[] = {
        internal::SPK_METHOD_ID,
        static_cast<int>(internal::OPM2_METHOD_ID),
    };
    for (size_t method_index = 0;
         method_index < sizeof(provider_methods) / sizeof(provider_methods[0]);
         ++method_index) {
        bool provider_has_explicit_product_override = false;
        std::vector<size_t> slots;
        std::vector<RouteRulePreference> products;
        std::unordered_map<uint64_t, size_t> product_by_source;
        for (size_t i = 0; i < rules.size(); ++i) {
            if (rules[i].method_id != provider_methods[method_index]
                || rules[i].source_id == 0) {
                continue;
            }
            RouteRulePreference preference;
            preference.rule = &rules[i];
            preference.source_priority = default_route_rule_source_priority(
                rules[i], *priorities);
            preference.original_index = i;
            slots.push_back(i);
            products.push_back(preference);
            product_by_source[rules[i].source_id] = products.size() - 1;
        }

        // A path override names a file but selects the product rule that owns
        // it. Scan the catalog once per provider, not once per product rule:
        // event searches may evaluate thousands of epochs with one stable
        // override table.
        const size_t descriptor_count = catalog->size();
        for (size_t i = 0; i < descriptor_count; ++i) {
            internal::EphemerisBlockDescriptor descriptor;
            if (!catalog->get(i, &descriptor)
                || descriptor.method_id != provider_methods[method_index]) {
                continue;
            }
            std::unordered_map<uint64_t, size_t>::const_iterator product =
                product_by_source.find(descriptor.source_key.source_id);
            if (product == product_by_source.end()) {
                continue;
            }
            RouteRulePreference& preference = products[product->second];
            int explicit_priority = 0;
            if (priorities->explicit_priority(
                    descriptor, &explicit_priority)) {
                provider_has_explicit_product_override = true;
            }
            const int64_t candidate_priority =
                priorities->priority_for(descriptor);
            if (!preference.has_catalog_candidate
                || candidate_priority > preference.source_priority) {
                preference.source_priority = candidate_priority;
                preference.has_catalog_candidate = true;
            }
        }
        // An override for another provider must not implicitly replace this
        // provider's route-table ordering with its numeric source defaults.
        if (!provider_has_explicit_product_override) {
            continue;
        }
        std::stable_sort(
            products.begin(),
            products.end(),
            [](const RouteRulePreference& lhs, const RouteRulePreference& rhs) {
                if (lhs.source_priority != rhs.source_priority) {
                    return lhs.source_priority > rhs.source_priority;
                }
                return lhs.original_index < rhs.original_index;
            });
        for (size_t i = 0; i < slots.size(); ++i) {
            ordered[slots[i]] = products[i].rule;
        }
    }
    return ordered;
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

int physical_primary_for_satellite(int body_id) noexcept {
    if (body_id <= 0) {
        return 0;
    }

    // NAIF encodes permanent natural satellites as PNN (NN=01..98),
    // extended permanent satellites as P0NNN, and provisional satellites as
    // P5NNN.  Recover the physical planet P99 from either representation so
    // kernels can expose satellites that do not have named Taiyin constants.
    int planetary_system = 0;
    if (body_id <= 999) {
        const int satellite_number = body_id % 100;
        if (satellite_number >= 1 && satellite_number <= 98) {
            planetary_system = body_id / 100;
        }
    } else if (body_id >= 10000 && body_id <= 99999) {
        const int range_separator = (body_id / 1000) % 10;
        const int satellite_number = body_id % 1000;
        if ((range_separator == 0 || range_separator == 5)
            && satellite_number >= 1 && satellite_number <= 999) {
            planetary_system = body_id / 10000;
        }
    }
    if (planetary_system < 1 || planetary_system > 9) {
        return 0;
    }
    return planetary_system * 100 + 99;
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

void EphemerisEngine::merge_rule_source_usage(
    RuleSourceUsage* out,
    const RuleSourceUsage& first,
    const RuleSourceUsage& second
) noexcept {
    if (!out) return;
    out->used_exact_source =
        first.used_exact_source || second.used_exact_source;
    out->used_auxiliary_source =
        first.used_auxiliary_source || second.used_auxiliary_source;
}

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
      source_priorities_(0),
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

void EphemerisEngine::set_source_priorities(
    const internal::EphemerisSourcePriorityTable* priorities
) noexcept {
    source_priorities_ = priorities;
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

const internal::EphemerisSourcePriorityTable* EphemerisEngine::source_priorities() const noexcept {
    return source_priorities_;
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
    std::vector<const internal::EphemerisRouteRule*> ordered_rules;
    try {
        ordered_rules = order_route_rules_by_source_preference(
            rules, catalog_, source_priorities_);
    } catch (...) {
        return false;
    }
    for (size_t rule_index = 0; rule_index < ordered_rules.size(); ++rule_index) {
        const internal::EphemerisRouteRule& rule = *ordered_rules[rule_index];
        std::vector<internal::EphemerisBlockDescriptor> candidates;
        if (!catalog_->find_method_candidates(query, rule.method_id, &candidates)) {
            continue;
        }
        order_candidates_by_source_preference(&candidates, source_priorities_);
        for (int match_rank = 0; match_rank <= 1; ++match_rank) {
            for (size_t i = 0; i < candidates.size(); ++i) {
                if (descriptor_match_rank(candidates[i], rule) == match_rank) {
                    *out = candidates[i];
                    return true;
                }
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

    std::vector<const internal::EphemerisRouteRule*> ordered_rules;
    try {
        ordered_rules = order_route_rules_by_source_preference(
            rules, catalog_, source_priorities_);
    } catch (...) {
        return set_diagnostic_status(diagnostic, TAIYIN_ERROR_OUT_OF_MEMORY);
    }

    int candidate_count = 0;
    Status last_status = TAIYIN_EPHEMERIS_ERROR_NO_ROUTE;
    std::vector<internal::EphemerisBlockDescriptor> candidates;
    for (size_t rule_index = 0; rule_index < ordered_rules.size(); ++rule_index) {
        const internal::EphemerisRouteRule& rule = *ordered_rules[rule_index];
        if (!catalog_->find_method_candidates(query, rule.method_id, &candidates)) {
            continue;
        }
        order_candidates_by_source_preference(&candidates, source_priorities_);
        for (int match_rank = 0; match_rank <= 1; ++match_rank) {
            for (size_t i = 0; i < candidates.size(); ++i) {
                if (descriptor_match_rank(candidates[i], rule) != match_rank) {
                    continue;
                }
                ++candidate_count;
                Status status = eval_method_descriptor_state(
                    candidates[i],
                    request.jd_tdb,
                    request.components,
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
    order_candidates_by_source_preference(&candidates, source_priorities_);

    int candidate_count = 0;
    Status last_status = TAIYIN_EPHEMERIS_ERROR_NO_ROUTE;
    for (int match_rank = 0; match_rank <= 1; ++match_rank) {
        for (size_t i = 0; i < candidates.size(); ++i) {
            if (descriptor_match_rank(candidates[i], rule) != match_rank) {
                continue;
            }
            ++candidate_count;
            const Status status = eval_method_descriptor_state(
                candidates[i],
                request.jd_tdb,
                request.components,
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

Status EphemerisEngine::eval_method_descriptor_state(
    const internal::EphemerisBlockDescriptor& source,
    const SplitJulianDate& jd_tdb,
    uint32_t components,
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
        selection->source_descriptor = source;
        selection->has_source_descriptor = true;
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

        selection->source_descriptor = source;
        selection->has_source_descriptor = true;
        selection->cache_hit = false;
        selection->loaded = true;
        return set_diagnostic_status(diagnostic, TAIYIN_STATUS_OK);
    }

    if (!segment_cache_->with_data(cache_key, eval_storage_cache_data, &context)) {
        return set_diagnostic_status(diagnostic, TAIYIN_EPHEMERIS_ERROR_LOAD_FAILED);
    }

    selection->source_descriptor = source;
    selection->has_source_descriptor = true;
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
    RuleSourceUsage* usage,
    EphemerisEvalDiagnostic* diagnostic
) noexcept {
    if (out) {
        *out = EphemerisResult();
    }
    if (usage) {
        *usage = RuleSourceUsage();
    }
    if (!out || !usage) {
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
    const int match_rank = selection.has_source_descriptor
        ? descriptor_match_rank(selection.source_descriptor, rule)
        : -1;
    usage->used_exact_source = match_rank == 0 && rule.source_id != 0;
    usage->used_auxiliary_source = match_rank == 1;
    return set_diagnostic_status(diagnostic, TAIYIN_STATUS_OK);
}

Status EphemerisEngine::eval_builtin_semi_analytic_auxiliary(
    const EphemerisRequest& request,
    const internal::EphemerisRouteRule& primary_rule,
    EphemerisResult* out,
    RuleSourceUsage* usage,
    EphemerisEvalDiagnostic* diagnostic
) noexcept {
    if (out) {
        *out = EphemerisResult();
    }
    if (usage) {
        *usage = RuleSourceUsage();
    }
    if (!out || !usage || !primary_rule.allow_builtin_semi_analytic_auxiliary) {
        return set_diagnostic_status(diagnostic, TAIYIN_EPHEMERIS_ERROR_NO_ROUTE);
    }

    // The main named product remains the anchor. This restricted path is only
    // for an explicitly opted-in built-in relative-body correction (currently
    // Mars versus its barycenter), never for a standalone fallback route.
    internal::EphemerisRouteRule auxiliary_rule;
    auxiliary_rule.source_id = internal::SEMI_ANALYTIC_SOURCE_ID;
    auxiliary_rule.method_id = internal::SEMI_ANALYTIC_METHOD_ID;
    EphemerisResult auxiliary;
    RuleSourceUsage ignored_usage;
    const Status status = eval_direct_body_state_for_rule(
        request, auxiliary_rule, &auxiliary, &ignored_usage, diagnostic);
    if (status != TAIYIN_STATUS_OK) {
        return status;
    }

    *out = auxiliary;
    usage->used_auxiliary_source = true;
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
    RuleSourceUsage* usage,
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
    if (usage) {
        *usage = RuleSourceUsage();
    }
    if (!out || !usage) {
        return set_diagnostic_status(diagnostic, TAIYIN_ERROR_INVALID_ARGUMENT);
    }
    if (body_id == TAIYIN_BODY_SUN) {
        return make_zero_result(request, out, diagnostic);
    }
    if (body_id == TAIYIN_BODY_SSB) {
        EphemerisResult sun_ssb;
        RuleSourceUsage sun_ssb_usage;
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
            eval_direct_body_state_for_rule(
                sun_ssb_request, rule, &sun_ssb, &sun_ssb_usage, diagnostic);
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
        *usage = sun_ssb_usage;
        return set_diagnostic_status(diagnostic, TAIYIN_STATUS_OK);
    }

    EphemerisResult direct;
    RuleSourceUsage direct_usage;
    Status direct_status = eval_direct_body_state_for_rule(
        request, rule, &direct, &direct_usage, diagnostic);
    if (direct_status == TAIYIN_STATUS_OK) {
        *out = direct;
        *usage = direct_usage;
        return TAIYIN_STATUS_OK;
    }

    if (body_id == TAIYIN_BODY_EARTH || body_id == TAIYIN_BODY_MOON) {
        EphemerisResult emb_sun;
        RuleSourceUsage emb_sun_usage;
        Status emb_status = eval_body_wrt_sun_for_rule(
            TAIYIN_BODY_EMB,
            frame,
            jd_tdb,
            components,
            route_rule_id,
            rule,
            include_descriptor,
            &emb_sun,
            &emb_sun_usage,
            diagnostic);
        if (emb_status != TAIYIN_STATUS_OK) {
            return set_diagnostic_status(diagnostic, emb_status);
        }

        EphemerisResult moon_earth;
        RuleSourceUsage moon_earth_usage;
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
            eval_direct_body_state_for_rule(
                moon_earth_request,
                rule,
                &moon_earth,
                &moon_earth_usage,
                diagnostic);
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
        merge_rule_source_usage(usage, emb_sun_usage, moon_earth_usage);
        return set_diagnostic_status(diagnostic, TAIYIN_STATUS_OK);
    }

    const int physical_primary_id = physical_primary_for_satellite(body_id);
    Status satellite_composite_status = direct_status;
    if (physical_primary_id != 0) {
        EphemerisResult primary_sun;
        RuleSourceUsage primary_sun_usage;
        const Status primary_status = eval_body_wrt_sun_for_rule(
            physical_primary_id,
            frame,
            jd_tdb,
            components,
            route_rule_id,
            rule,
            include_descriptor,
            &primary_sun,
            &primary_sun_usage,
            diagnostic);
        if (primary_status == TAIYIN_STATUS_OK) {
            EphemerisResult satellite_primary;
            RuleSourceUsage satellite_primary_usage;
            const EphemerisRequest satellite_primary_request = make_component_request(
                body_id,
                physical_primary_id,
                frame,
                jd_tdb,
                components,
                route_rule_id,
                0,
                include_descriptor);
            const Status satellite_status = eval_direct_body_state_for_rule(
                satellite_primary_request,
                rule,
                &satellite_primary,
                &satellite_primary_usage,
                diagnostic);
            if (satellite_status == TAIYIN_STATUS_OK) {
                out->state = cartesian_state_add(primary_sun.state, satellite_primary.state);
                if (include_descriptor) {
                    out->descriptor = make_synthetic_descriptor(request, primary_sun.descriptor);
                    out->descriptor.jd_tdb_start = std::max(
                        primary_sun.descriptor.jd_tdb_start,
                        satellite_primary.descriptor.jd_tdb_start);
                    out->descriptor.jd_tdb_end = std::min(
                        primary_sun.descriptor.jd_tdb_end,
                        satellite_primary.descriptor.jd_tdb_end);
                }
                out->cache_hit = primary_sun.cache_hit && satellite_primary.cache_hit;
                merge_rule_source_usage(
                    usage, primary_sun_usage, satellite_primary_usage);
                return set_diagnostic_status(diagnostic, TAIYIN_STATUS_OK);
            }
            satellite_composite_status = satellite_status;
            if (diagnostic) {
                diagnostic->component_target_id = satellite_primary_request.target_id;
                diagnostic->component_center_id = satellite_primary_request.center_id;
                diagnostic->component_method_id = satellite_primary.descriptor.method_id;
            }
        } else {
            satellite_composite_status = primary_status;
        }
    }

    EphemerisResult body_ssb;
    RuleSourceUsage body_ssb_usage;
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
        eval_direct_body_state_for_rule(
            body_ssb_request, rule, &body_ssb, &body_ssb_usage, diagnostic);
    if (body_ssb_status == TAIYIN_STATUS_OK) {
        EphemerisResult sun_ssb;
        RuleSourceUsage sun_ssb_usage;
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
            eval_direct_body_state_for_rule(
                sun_ssb_request, rule, &sun_ssb, &sun_ssb_usage, diagnostic);
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
            merge_rule_source_usage(usage, body_ssb_usage, sun_ssb_usage);
            return set_diagnostic_status(diagnostic, TAIYIN_STATUS_OK);
        }
    }

    const int barycenter_id = barycenter_for_physical_body(body_id);
    if (barycenter_id != 0) {
        EphemerisResult barycenter_sun;
        RuleSourceUsage barycenter_sun_usage;
        Status barycenter_status = eval_body_wrt_sun_for_rule(
            barycenter_id,
            frame,
            jd_tdb,
            components,
            route_rule_id,
            rule,
            include_descriptor,
            &barycenter_sun,
            &barycenter_sun_usage,
            diagnostic);
        if (barycenter_status == TAIYIN_STATUS_OK) {
            if (body_is_barycenter_alias(body_id)) {
                *out = barycenter_sun;
                if (include_descriptor) {
                    out->descriptor = make_synthetic_descriptor(request, barycenter_sun.descriptor);
                }
                *usage = barycenter_sun_usage;
                return set_diagnostic_status(diagnostic, TAIYIN_STATUS_OK);
            }

            EphemerisResult offset;
            RuleSourceUsage offset_usage;
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
                eval_direct_body_state_for_rule(
                    offset_request, rule, &offset, &offset_usage, diagnostic);
            if (offset_status != TAIYIN_STATUS_OK) {
                offset_status = eval_builtin_semi_analytic_auxiliary(
                    offset_request, rule, &offset, &offset_usage, diagnostic);
            }
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
                merge_rule_source_usage(usage, barycenter_sun_usage, offset_usage);
                return set_diagnostic_status(diagnostic, TAIYIN_STATUS_OK);
            }
        }
    }

    return set_diagnostic_status(
        diagnostic,
        physical_primary_id != 0 ? satellite_composite_status : direct_status);
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

    // A direct auxiliary edge cannot anchor a named-DE route by itself. Do
    // not numerically evaluate the same satellite descriptor once for every
    // DE rule merely to reject it after the fact; auxiliaries remain enabled
    // for the component-composition paths below, where an exact DE component
    // can supply the required anchor.
    internal::EphemerisRouteRule exact_direct_rule;
    exact_direct_rule.source_id = rule.source_id;
    exact_direct_rule.method_id = rule.method_id;
    exact_direct_rule.priority = rule.priority;
    exact_direct_rule.order = rule.order;
    const internal::EphemerisRouteRule& direct_rule =
        rule.allow_non_de_spk_auxiliary ? exact_direct_rule : rule;
    EphemerisResult direct;
    RuleSourceUsage direct_usage;
    Status direct_status = eval_direct_body_state_for_rule(
        request, direct_rule, &direct, &direct_usage, diagnostic);
    if (direct_status == TAIYIN_STATUS_OK) {
        if (!internal::ephemeris_route_source_usage_is_anchored(
                rule,
                direct_usage.used_exact_source,
                direct_usage.used_auxiliary_source)) {
            return set_diagnostic_status(
                diagnostic, TAIYIN_EPHEMERIS_ERROR_NO_ROUTE);
        }
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
    RuleSourceUsage target_sun_usage;
    Status status = eval_body_wrt_sun_for_rule(
        request.target_id,
        request.frame,
        request.jd_tdb,
        request.components,
        request.route_rule_id,
        rule,
        request.include_descriptor,
        &target_sun,
        &target_sun_usage,
        diagnostic);
    if (status != TAIYIN_STATUS_OK) {
        return set_diagnostic_status(diagnostic, status);
    }

    if (request.center_id == TAIYIN_BODY_SUN) {
        if (!internal::ephemeris_route_source_usage_is_anchored(
                rule,
                target_sun_usage.used_exact_source,
                target_sun_usage.used_auxiliary_source)) {
            return set_diagnostic_status(
                diagnostic, TAIYIN_EPHEMERIS_ERROR_NO_ROUTE);
        }
        *out = target_sun;
        if (request.include_descriptor) {
            out->descriptor = make_synthetic_descriptor(request, target_sun.descriptor);
        }
        return set_diagnostic_status(diagnostic, TAIYIN_STATUS_OK);
    }

    EphemerisResult center_sun;
    RuleSourceUsage center_sun_usage;
    status = eval_body_wrt_sun_for_rule(
        request.center_id,
        request.frame,
        request.jd_tdb,
        request.components,
        request.route_rule_id,
        rule,
        request.include_descriptor,
        &center_sun,
        &center_sun_usage,
        diagnostic);
    if (status != TAIYIN_STATUS_OK) {
        return set_diagnostic_status(diagnostic, status);
    }

    RuleSourceUsage combined_usage;
    merge_rule_source_usage(
        &combined_usage, target_sun_usage, center_sun_usage);
    if (!internal::ephemeris_route_source_usage_is_anchored(
            rule,
            combined_usage.used_exact_source,
            combined_usage.used_auxiliary_source)) {
        return set_diagnostic_status(
            diagnostic, TAIYIN_EPHEMERIS_ERROR_NO_ROUTE);
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

    std::vector<const internal::EphemerisRouteRule*> ordered_rules;
    try {
        ordered_rules = order_route_rules_by_source_preference(
            rules, catalog_, source_priorities_);
    } catch (...) {
        return set_diagnostic_status(diagnostic, TAIYIN_ERROR_OUT_OF_MEMORY);
    }

    Status last_status = TAIYIN_EPHEMERIS_ERROR_NO_ROUTE;
    for (size_t i = 0; i < ordered_rules.size(); ++i) {
        Status status = eval_state_for_rule(
            request, *ordered_rules[i], out, diagnostic);
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
