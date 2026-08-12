#include "taiyin/internal/ephemeris_route_rule.h"

#include <algorithm>

namespace taiyin {
namespace internal {
namespace {

bool route_rule_less(const EphemerisRouteRule& lhs, const EphemerisRouteRule& rhs) noexcept {
    if (lhs.priority != rhs.priority) {
        return lhs.priority > rhs.priority;
    }
    return lhs.order < rhs.order;
}

std::vector<EphemerisRouteRule>::iterator find_rule(
    std::vector<EphemerisRouteRule>* rules,
    uint64_t source_id,
    int method_id
) noexcept {
    if (!rules) {
        return std::vector<EphemerisRouteRule>::iterator();
    }
    return std::find_if(
        rules->begin(),
        rules->end(),
        [source_id, method_id](const EphemerisRouteRule& rule) {
            return rule.source_id == source_id && rule.method_id == method_id;
        });
}

}  // namespace

bool ephemeris_route_source_usage_is_anchored(
    const EphemerisRouteRule& rule,
    bool used_exact_source,
    bool used_auxiliary_source
) noexcept {
    return (!rule.allow_non_de_spk_auxiliary
            && !rule.allow_builtin_semi_analytic_auxiliary)
        || !used_auxiliary_source
        || used_exact_source;
}

EphemerisRouteRuleTable::EphemerisRouteRuleTable() noexcept
    : rules_(),
      next_order_(0) {}

void EphemerisRouteRuleTable::clear() noexcept {
    rules_.clear();
    next_order_ = 0;
}

bool EphemerisRouteRuleTable::upsert_source_method(
    uint64_t source_id,
    int method_id,
    int priority,
    const char* description,
    bool allow_non_de_spk_auxiliary,
    bool allow_builtin_semi_analytic_auxiliary
) noexcept {
    if (method_id == 0) {
        return false;
    }
    try {
        std::vector<EphemerisRouteRule>::iterator it = find_rule(&rules_, source_id, method_id);
        if (it == rules_.end()) {
            EphemerisRouteRule rule;
            rule.source_id = source_id;
            rule.method_id = method_id;
            rule.priority = priority;
            rule.order = next_order_++;
            rule.allow_non_de_spk_auxiliary = allow_non_de_spk_auxiliary;
            rule.allow_builtin_semi_analytic_auxiliary = allow_builtin_semi_analytic_auxiliary;
            if (description) {
                rule.description = description;
            }
            rules_.push_back(rule);
        } else {
            it->priority = priority;
            it->allow_non_de_spk_auxiliary = allow_non_de_spk_auxiliary;
            it->allow_builtin_semi_analytic_auxiliary = allow_builtin_semi_analytic_auxiliary;
            if (description) {
                it->description = description;
            }
        }
        std::stable_sort(rules_.begin(), rules_.end(), route_rule_less);
    } catch (...) {
        return false;
    }
    return true;
}

const std::vector<EphemerisRouteRule>& EphemerisRouteRuleTable::rules() const noexcept {
    return rules_;
}

size_t EphemerisRouteRuleTable::method_count() const noexcept {
    return rules_.size();
}

}  // namespace internal
}  // namespace taiyin
