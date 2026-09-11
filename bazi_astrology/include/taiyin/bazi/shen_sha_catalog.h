#ifndef TAIYIN_BAZI_SHEN_SHA_CATALOG_H
#define TAIYIN_BAZI_SHEN_SHA_CATALOG_H

#include "taiyin/bazi/bazi.h"
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace taiyin { namespace bazi {

// Synchronous callbacks receive a per-evaluation value snapshot. They must not
// retain its address. External state captured by closures is NOT snapshotted.
// With unspecified gender the hour may be unknown (0xff), just as in the
// original neutral collector. Predicates needing an hour must handle that.
struct BaziShenShaInput {
    BaziChart chart;
    uint8_t target;
    int32_t target_kind;
    int32_t gender; // -1: unspecified; otherwise BaziGender
};

struct BaziShenShaRule {
    std::string id;
    std::string name;
    std::function<bool(const BaziShenShaInput&)> test;
};

struct BaziShenShaMatch {
    std::string id; // builtin:N, or module-label:rule-id
    std::string name; // custom display name; empty for built-ins (use builtin_id)
    int32_t builtin_id; // -1 for user rules
};

// Immutable additions, just like ZiweiRuleModule. Labels and rule IDs may not
// contain ':'. The builtin and option1 namespaces are reserved.
class BaziShenShaModule {
public:
    BaziShenShaModule(const std::string& label,
                     const std::vector<BaziShenShaRule>& rules);
    const std::string& label() const noexcept;
    const std::vector<BaziShenShaRule>& rules() const noexcept;
private:
    std::string label_;
    std::vector<BaziShenShaRule> rules_;
};

struct BaziShenShaSelection {
    // Selection only: disabling an entry never modifies its definition.
    std::vector<std::string> disabled_ids;
};

class BaziShenShaContext;

// The catalog always retains the default 66 definitions. add/remove return
// NEW catalogs; old catalogs and contexts are unchanged. No global registry.
class BaziShenShaCatalog {
public:
    BaziShenShaCatalog();
    BaziShenShaCatalog add_module(const BaziShenShaModule& module) const;
    BaziShenShaCatalog remove_module(const std::string& label) const;
    const std::vector<BaziShenShaModule>& modules() const noexcept;
    BaziShenShaContext create_context(
        const BaziShenShaSelection& selection = BaziShenShaSelection()) const;
private:
    std::vector<BaziShenShaModule> modules_;
};

// A bound selection snapshot. Calculation takes chart data explicitly, not
// an ephemeris/calendar context. Callback exceptions propagate to the caller;
// no partial result escapes. Concurrent calls require thread-safe callbacks.
class BaziShenShaContext {
public:
    std::vector<BaziShenShaMatch> evaluate(
        const BaziChart& chart, uint8_t target, int32_t target_kind,
        int32_t gender = -1) const;
private:
    friend class BaziShenShaCatalog;
    struct Data;
    explicit BaziShenShaContext(std::shared_ptr<const Data> data);
    std::shared_ptr<const Data> data_;
};

} }
#endif
