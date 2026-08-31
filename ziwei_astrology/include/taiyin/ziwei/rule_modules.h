#ifndef TAIYIN_ZIWEI_RULE_MODULES_H
#define TAIYIN_ZIWEI_RULE_MODULES_H

#include <memory>
#include <string>
#include <vector>

namespace taiyin {
namespace ziwei {

namespace detail {
struct ZiweiRuleModuleData;
struct ZiweiRuleModuleAccess;
}

struct ZiweiJsonRuleModuleInput {
    std::string label;
    std::string stars_json;
    std::string brightness_json;
    std::string sihua_json;
    std::string flow_json;
    std::string masters_json;
};

// A labelled immutable unit of compiled answer tables. JSON arithmetic and
// pipelines exist only while constructing this object; chart evaluation never
// interprets JSON or executes a rule expression.
class ZiweiRuleModule {
public:
    ZiweiRuleModule();
    const std::string& label() const;
    bool valid() const noexcept;

private:
    friend class ZiweiConfigLoader;
    friend class ZiweiDataCatalog;
    friend struct detail::ZiweiRuleModuleAccess;
    explicit ZiweiRuleModule(
        std::shared_ptr<const detail::ZiweiRuleModuleData> data);
    std::shared_ptr<const detail::ZiweiRuleModuleData> data_;
};

// Ordered immutable modules. Later modules override overlapping tables.
class ZiweiRuleset {
public:
    ZiweiRuleset();
    explicit ZiweiRuleset(const std::vector<ZiweiRuleModule>& modules);

    const std::vector<ZiweiRuleModule>& modules() const noexcept;
    ZiweiRuleset with(const ZiweiRuleModule& module) const;

private:
    std::vector<ZiweiRuleModule> modules_;
};

class ZiweiConfigLoader {
public:
    static ZiweiRuleset get_default();
    static ZiweiRuleModule compile_json(
        const ZiweiJsonRuleModuleInput& input);
    static ZiweiRuleset override_with(
        const ZiweiRuleset& base,
        const ZiweiJsonRuleModuleInput& input);
};

}  // namespace ziwei
}  // namespace taiyin

#endif  // TAIYIN_ZIWEI_RULE_MODULES_H
