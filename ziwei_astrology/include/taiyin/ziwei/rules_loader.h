#ifndef TAIYIN_ZIWEI_RULES_LOADER_H
#define TAIYIN_ZIWEI_RULES_LOADER_H

#include "taiyin/ziwei/data_catalog.h"
#include "taiyin/ziwei/rules.h"
#include "taiyin/ziwei/star_registry.h"

#include <stdexcept>
#include <string>

namespace taiyin {
namespace ziwei {

class RuleLoadError : public std::runtime_error {
public:
    explicit RuleLoadError(const std::string& message);
};

// Kept distinct so the C ABI can report missing installed rule data without
// conflating it with a syntactically or semantically invalid catalog.
class RuleFileNotFoundError : public RuleLoadError {
public:
    explicit RuleFileNotFoundError(const std::string& message);
};

struct LoadedRules {
    StarRegistry registry;
    CompiledRules compiled;
};

// Compatibility one-shot loader. New code should retain a ZiweiDataCatalog and
// create one or more ZiweiContext values from it, avoiding repeated TOML parses.
LoadedRules load_rules_from_toml(const std::string& filename);

}  // namespace ziwei
}  // namespace taiyin

#endif  // TAIYIN_ZIWEI_RULES_LOADER_H
