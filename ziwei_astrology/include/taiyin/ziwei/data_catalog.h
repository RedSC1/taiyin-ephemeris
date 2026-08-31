#ifndef TAIYIN_ZIWEI_DATA_CATALOG_H
#define TAIYIN_ZIWEI_DATA_CATALOG_H

#include "taiyin/ziwei/rules.h"
#include "taiyin/ziwei/rule_modules.h"
#include "taiyin/ziwei/star_registry.h"

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>

namespace taiyin {
namespace ziwei {

namespace detail {
struct ZiweiCatalogSnapshot;
}

// Empty fields inherit the selections declared by the catalog profile. A
// component default applies to every entry not present in its per-entry map.
// This object changes selections only; it never changes table dimensions,
// star declarations, or the finite evaluator schema.
struct ZiweiOptionSelection {
    std::string placement_default;
    std::string brightness_default;
    std::string sihua_default;
    std::string masters;
    // One coherent table choice for the twelve life stages.  Unlike ordinary
    // placements, these stars must never be selected independently.
    std::string longevity;
    std::unordered_map<std::string, std::string> placement;
    std::unordered_map<std::string, std::string> brightness;
    std::unordered_map<std::string, std::string> sihua;

    ZiweiOptionSelection();
};

// A lightweight, immutable calculation view. Creating another context with a
// different option selection reuses the same parsed TOML catalog.
class ZiweiContext {
public:
    ZiweiContext();

    const StarRegistry& star_registry() const;
    const CompiledRules& compiled_tables() const noexcept;
    const ZiweiOptionSelection& selected_options() const noexcept;
    uint64_t catalog_generation() const noexcept;
    bool valid() const noexcept;

private:
    friend class ZiweiDataCatalog;
    ZiweiContext(
        std::shared_ptr<const detail::ZiweiCatalogSnapshot> snapshot,
        std::shared_ptr<const StarRegistry> registry,
        CompiledRules compiled,
        ZiweiOptionSelection selected
    );

    std::shared_ptr<const detail::ZiweiCatalogSnapshot> snapshot_;
    std::shared_ptr<const StarRegistry> registry_;
    CompiledRules compiled_;
    ZiweiOptionSelection selected_;
};

// Owns one reloadable TOML source set. Parsing and validation happen only at
// construction/reload. Contexts capture immutable snapshots, so calculations
// never lock and contexts created before reload continue to use the old data.
class ZiweiDataCatalog {
public:
    explicit ZiweiDataCatalog(const std::string& profile_path);
    ZiweiDataCatalog(const ZiweiDataCatalog& other);
    ZiweiDataCatalog(ZiweiDataCatalog&& other) noexcept;

    ZiweiDataCatalog& operator=(const ZiweiDataCatalog&) = delete;
    ZiweiDataCatalog& operator=(ZiweiDataCatalog&&) = delete;

    ZiweiContext create_context() const;
    ZiweiContext create_context(const ZiweiOptionSelection& selection) const;
    ZiweiContext create_context(
        const ZiweiOptionSelection& selection,
        const ZiweiRuleset& ruleset) const;

    // Strong exception guarantee: a parse/validation error leaves the current
    // snapshot untouched. Concurrent create_context() calls see either the old
    // or new complete snapshot.
    void reload();

    const std::string& profile_path() const noexcept;
    uint64_t generation() const noexcept;

private:
    std::string profile_path_;
    std::shared_ptr<const detail::ZiweiCatalogSnapshot> snapshot_;
};

}  // namespace ziwei
}  // namespace taiyin

#endif  // TAIYIN_ZIWEI_DATA_CATALOG_H
