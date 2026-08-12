#ifndef TAIYIN_INTERNAL_EPHEMERIS_ROUTE_RULE_H
#define TAIYIN_INTERNAL_EPHEMERIS_ROUTE_RULE_H

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace taiyin {
namespace internal {

struct EphemerisRouteRule {
    uint64_t source_id;
    int method_id;
    int priority;
    uint64_t order;
    bool allow_non_de_spk_auxiliary;
    bool allow_builtin_semi_analytic_auxiliary;
    std::string description;

    EphemerisRouteRule() noexcept
        : source_id(0),
          method_id(0),
          priority(0),
          order(0),
          allow_non_de_spk_auxiliary(false),
          allow_builtin_semi_analytic_auxiliary(false),
          description() {}
};

// A named-DE rule may use explicitly permitted satellite descriptors only as
// auxiliaries.
// Such a composite is valid only when at least one successfully used
// descriptor came from the named DE source itself.
bool ephemeris_route_source_usage_is_anchored(
    const EphemerisRouteRule& rule,
    bool used_exact_source,
    bool used_auxiliary_source
) noexcept;

class EphemerisRouteRuleTable {
public:
    EphemerisRouteRuleTable() noexcept;

    void clear() noexcept;
    bool upsert_source_method(
        uint64_t source_id,
        int method_id,
        int priority,
        const char* description,
        bool allow_non_de_spk_auxiliary = false,
        bool allow_builtin_semi_analytic_auxiliary = false
    ) noexcept;
    const std::vector<EphemerisRouteRule>& rules() const noexcept;
    size_t method_count() const noexcept;

private:
    std::vector<EphemerisRouteRule> rules_;
    uint64_t next_order_;
};

}  // namespace internal
}  // namespace taiyin

#endif  // TAIYIN_INTERNAL_EPHEMERIS_ROUTE_RULE_H
