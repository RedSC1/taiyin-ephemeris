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
    std::string description;

    EphemerisRouteRule() noexcept
        : source_id(0),
          method_id(0),
          priority(0),
          order(0),
          description() {}
};

class EphemerisRouteRuleTable {
public:
    EphemerisRouteRuleTable() noexcept;

    void clear() noexcept;
    bool upsert_source_method(uint64_t source_id, int method_id, int priority, const char* description) noexcept;
    const std::vector<EphemerisRouteRule>& rules() const noexcept;
    size_t method_count() const noexcept;

private:
    std::vector<EphemerisRouteRule> rules_;
    uint64_t next_order_;
};

}  // namespace internal
}  // namespace taiyin

#endif  // TAIYIN_INTERNAL_EPHEMERIS_ROUTE_RULE_H
