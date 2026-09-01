#include "taiyin/ziwei/rules_loader.h"

#include "rule_modules_internal.h"
#include "toml.hpp"

#include <atomic>
#include <array>
#include <cerrno>
#include <cstdio>
#include <cstdint>
#include <fstream>
#include <limits>
#include <memory>
#include <set>
#include <sstream>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#if defined(_WIN32)
#include <windows.h>
#endif

namespace taiyin {
namespace ziwei {

namespace detail {

struct ZiweiCatalogSnapshot {
    std::string profile_filename;
    uint64_t generation;
    StarRegistry registry;
    std::size_t natal_star_count;
    ZiweiOptionSelection profile_selection;
    std::unordered_map<std::string, PlacementRule> placement_variants;
    std::unordered_map<
        std::string,
        std::array<int8_t, kBranchCount> > brightness_variants;
    std::unordered_map<std::string, TransformSet> sihua_variants;
    std::unordered_map<std::string, CompiledMasterRules> master_variants;
    std::set<std::string> module_owned_stars;
};

}  // namespace detail

namespace {

std::atomic<uint64_t> g_catalog_generation(1u);

const char* kLongevityStarKeys[] = {
    "changsheng", "muyu", "guandai", "linguan", "diwang", "shuai",
    "bing", "si", "mu", "jue", "tai", "yang",
};

const std::size_t kLongevityStarCount =
    sizeof(kLongevityStarKeys) / sizeof(kLongevityStarKeys[0]);

bool is_longevity_star_key(const std::string& key) noexcept {
    for (std::size_t i = 0u; i < kLongevityStarCount; ++i) {
        if (key == kLongevityStarKeys[i]) return true;
    }
    return false;
}

RuleLoadError semantic_error(const std::string& filename,
    const std::string& path, const std::string& detail) {
    return RuleLoadError(filename + ": " + path + ": " + detail);
}

void require_version_one(const std::string& filename,
    const toml::value& root) {
    const int64_t version = toml::find<int64_t>(root, "format_version");
    if (version != 1) {
        std::ostringstream message;
        message << "expected format_version 1, actual " << version;
        throw semantic_error(filename, "format_version", message.str());
    }
}

std::string directory_name(const std::string& filename) {
    const std::string::size_type slash = filename.find_last_of("/\\");
    return slash == std::string::npos ? std::string(".")
                                      : filename.substr(0u, slash);
}

bool is_absolute_path(const std::string& path) {
    return !path.empty() && (path[0] == '/' || path[0] == '\\'
        || (path.size() >= 2u && path[1] == ':'));
}

std::string resolve_path(const std::string& profile_filename,
    const std::string& path) {
    if (is_absolute_path(path)) return path;
    return directory_name(profile_filename) + "/" + path;
}

toml::value parse_toml_file(const std::string& filename) {
#if defined(_WIN32)
    if (filename.size() > static_cast<std::size_t>(
            (std::numeric_limits<int>::max)())) {
        throw std::runtime_error("TOML path is too long");
    }
    const int required = MultiByteToWideChar(
        CP_UTF8, MB_ERR_INVALID_CHARS, filename.data(),
        static_cast<int>(filename.size()), NULL, 0);
    if (required <= 0) {
        throw std::runtime_error("TOML path is not valid UTF-8");
    }
    std::vector<wchar_t> wide(static_cast<std::size_t>(required) + 1u, L'\0');
    if (MultiByteToWideChar(
            CP_UTF8, MB_ERR_INVALID_CHARS, filename.data(),
            static_cast<int>(filename.size()), &wide[0], required) != required) {
        throw std::runtime_error("failed to convert TOML path to UTF-16");
    }
    FILE* input = _wfopen(&wide[0], L"rb");
    if (input == NULL) {
        const DWORD error = GetLastError();
        if (errno == ENOENT || error == ERROR_FILE_NOT_FOUND
            || error == ERROR_PATH_NOT_FOUND) {
            throw RuleFileNotFoundError(
                "TOML file not found '" + filename + "'");
        }
        throw std::runtime_error("failed to open TOML file '" + filename + "'");
    }
    try {
        toml::value result = toml::parse(input, filename);
        std::fclose(input);
        return result;
    } catch (...) {
        std::fclose(input);
        throw;
    }
#else
    std::ifstream input(filename.c_str(), std::ios::in | std::ios::binary);
    if (!input) {
        if (errno == ENOENT || errno == ENOTDIR) {
            throw RuleFileNotFoundError(
                "TOML file not found '" + filename + "'");
        }
        throw std::runtime_error("failed to open TOML file '" + filename + "'");
    }
    return toml::parse(input, filename);
#endif
}

StarCategory parse_category(const std::string& filename,
    const std::string& path, const std::string& value) {
    if (value == "major") return StarCategory::Major;
    if (value == "lucky") return StarCategory::Lucky;
    if (value == "minor") return StarCategory::Minor;
    if (value == "bad" || value == "malefic") return StarCategory::Malefic;
    if (value == "cycle") return StarCategory::Cycle;
    if (value == "other") return StarCategory::Other;
    throw semantic_error(filename, path, "unknown category '" + value + "'");
}

RuleInputSource parse_input_source(const std::string& filename,
    const std::string& path, const std::string& value) {
    if (value == "solar.year_stem") return RuleInputSource::SolarYearStem;
    if (value == "solar.year_branch") return RuleInputSource::SolarYearBranch;
    if (value == "solar.month_stem") return RuleInputSource::SolarMonthStem;
    if (value == "solar.month_branch") return RuleInputSource::SolarMonthBranch;
    if (value == "solar.day_stem") return RuleInputSource::SolarDayStem;
    if (value == "solar.day_branch") return RuleInputSource::SolarDayBranch;
    if (value == "solar.hour_stem") return RuleInputSource::SolarHourStem;
    if (value == "solar.hour_branch") return RuleInputSource::SolarHourBranch;
    if (value == "lunar.year_stem") return RuleInputSource::LunarYearStem;
    if (value == "lunar.year_branch") return RuleInputSource::LunarYearBranch;
    if (value == "lunar.month_stem") return RuleInputSource::LunarMonthStem;
    if (value == "lunar.month_branch") return RuleInputSource::LunarMonthBranch;
    if (value == "lunar.day_stem") return RuleInputSource::LunarDayStem;
    if (value == "lunar.day_branch") return RuleInputSource::LunarDayBranch;
    if (value == "lunar.hour_stem") return RuleInputSource::LunarHourStem;
    if (value == "lunar.hour_branch") return RuleInputSource::LunarHourBranch;
    if (value == "anchor.bureau") return RuleInputSource::Bureau;
    if (value == "anchor.ziwei") return RuleInputSource::Ziwei;
    if (value == "anchor.tianfu") return RuleInputSource::Tianfu;
    if (value == "anchor.life") return RuleInputSource::Life;
    if (value == "anchor.body") return RuleInputSource::Body;
    if (value == "solar.zheng_kong") return RuleInputSource::SolarZhengKong;
    if (value == "solar.fu_kong") return RuleInputSource::SolarFuKong;
    if (value == "lunar.zheng_kong") return RuleInputSource::LunarZhengKong;
    if (value == "lunar.fu_kong") return RuleInputSource::LunarFuKong;
    if (value == "solar.month_index") return RuleInputSource::SolarMonthIndex;
    if (value == "lunar.month_index") return RuleInputSource::LunarMonthIndex;
    if (value == "lunar.day_index") return RuleInputSource::LunarDayIndex;
    if (value == "solar.day_index") return RuleInputSource::SolarDayIndex;
    if (value == "birth.gender") return RuleInputSource::BirthGender;
    throw semantic_error(filename, path, "unknown input source '" + value + "'");
}

StarId resolve_star(const std::string& filename, const std::string& path,
    const StarRegistry& registry, const std::string& key) {
    StarId id = kInvalidStarId;
    if (!registry.find(key, &id)) {
        throw semantic_error(filename, path, "unknown star key '" + key + "'");
    }
    return id;
}

std::string default_option(const toml::value& profile,
    const std::string& component) {
    if (profile.as_table().count("defaults") == 0u) return "option1";
    const toml::value& defaults = profile.at("defaults");
    if (defaults.as_table().count(component) == 0u) return "option1";
    return toml::find<std::string>(defaults, component);
}

// toml::find_or<T> intentionally returns its fallback for a value of the
// wrong type. That behavior is useful for optional application settings, but
// dangerous for rule resources: option = 2 must not silently select
// "option1". Optional here means absent, not malformed.
std::string optional_option_name(const toml::value& entry) {
    if (entry.as_table().count("option") == 0u) return "option1";
    return toml::find<std::string>(entry, "option");
}

std::string selected_option(const toml::value& profile,
    const std::string& component, const std::string& key) {
    const std::string fallback = default_option(profile, component);
    if (profile.as_table().count(component) == 0u) return fallback;
    const toml::value& selections = profile.at(component);
    // A compact test or maintainer profile may also use this file as the
    // component resource (for example [[brightness]]). Only a TOML table is
    // a profile-selection map.
    if (!selections.is_table()) return fallback;
    if (selections.as_table().count(key) == 0u) return fallback;
    return toml::find<std::string>(selections, key);
}

std::string entry_key(const std::string& star, const std::string& option) {
    return star + "\n" + option;
}

PlacementRule compile_flat_placement(const std::string& filename,
    const std::string& path, StarId star_id, const toml::value& source) {
    PlacementRule result = {};
    result.star_id = star_id;

    const std::vector<std::string> input_names =
        toml::find<std::vector<std::string> >(source, "inputs");
    const std::vector<int64_t> shape =
        toml::find<std::vector<int64_t> >(source, "shape");
    if (input_names.empty() || shape.size() != input_names.size()) {
        throw semantic_error(filename, path,
            "inputs/shape must have the same non-zero dimensions");
    }
    result.inputs.resize(input_names.size());
    result.strides.resize(input_names.size());
    std::size_t expected = 1u;
    for (std::size_t i = input_names.size(); i-- > 0u;) {
        result.inputs[i] = parse_input_source(filename,
            path + ".inputs[" + std::to_string(i) + "]", input_names[i]);
        const std::size_t domain = rule_input_domain_size(result.inputs[i]);
        if (shape[i] <= 0 || static_cast<std::size_t>(shape[i]) != domain) {
            std::ostringstream message;
            message << "shape entry " << i << " expected " << domain
                    << ", actual " << shape[i];
            throw semantic_error(filename, path + ".shape", message.str());
        }
        result.strides[i] = expected;
        if (expected > (std::numeric_limits<std::size_t>::max)() / domain) {
            throw semantic_error(filename, path,
                "flattened table size overflows size_t");
        }
        expected *= domain;
    }
    const std::vector<int64_t> positions =
        toml::find<std::vector<int64_t> >(source, "positions");
    if (positions.size() != expected) {
        std::ostringstream message;
        message << "expected " << expected
                << " positions, actual " << positions.size();
        throw semantic_error(filename, path + ".positions", message.str());
    }
    for (std::size_t i = 0u; i < positions.size(); ++i) {
        if (positions[i] < 0 || positions[i] >= 12) {
            std::ostringstream message;
            message << "entry " << i
                    << " expected branch index 0..11, actual " << positions[i];
            throw semantic_error(filename, path + ".positions", message.str());
        }
        result.table.push_back(static_cast<uint8_t>(positions[i]));
    }
    return result;
}

void add_stars(const std::string& filename, const toml::value& root,
    const std::string& array_name, bool natal, StarRegistry* registry) {
    const toml::array& stars = root.at(array_name).as_array();
    for (std::size_t i = 0u; i < stars.size(); ++i) {
        const std::string path = array_name + "[" + std::to_string(i) + "]";
        const std::string key = toml::find<std::string>(stars[i], "key");
        const std::string category =
            toml::find<std::string>(stars[i], "category");
        try {
            registry->add(key,
                parse_category(filename, path + ".category", category), natal);
        } catch (const std::bad_alloc&) {
            throw;
        } catch (const std::exception& error) {
            throw semantic_error(filename, path + ".key", error.what());
        }
    }
}

const char* kStemKeys[kStemCount] = {
    "jia", "yi", "bing", "ding", "wu",
    "ji", "geng", "xin", "ren", "gui",
};

std::string choose_option(
    const std::unordered_map<std::string, std::string>& overrides,
    const std::string& override_default,
    const std::unordered_map<std::string, std::string>& profile,
    const std::string& key
) {
    const std::unordered_map<std::string, std::string>::const_iterator direct =
        overrides.find(key);
    if (direct != overrides.end()) return direct->second;
    if (!override_default.empty()) return override_default;
    const std::unordered_map<std::string, std::string>::const_iterator inherited =
        profile.find(key);
    return inherited == profile.end() ? std::string("option1")
                                      : inherited->second;
}

std::string choose_component_option(
    const std::string& override_option,
    const std::string& profile_option
) {
    if (!override_option.empty()) return override_option;
    return profile_option.empty() ? std::string("option1") : profile_option;
}

template <typename T>
const T& require_variant(
    const detail::ZiweiCatalogSnapshot& snapshot,
    const std::unordered_map<std::string, T>& variants,
    const std::string& component,
    const std::string& key,
    const std::string& option
) {
    const typename std::unordered_map<std::string, T>::const_iterator found =
        variants.find(entry_key(key, option));
    if (found == variants.end()) {
        throw semantic_error(snapshot.profile_filename,
            component + "." + key,
            "missing selected option '" + option + "'");
    }
    return found->second;
}

bool is_stem_key(const std::string& key) noexcept {
    for (std::size_t stem = 0u; stem < kStemCount; ++stem) {
        if (key == kStemKeys[stem]) return true;
    }
    return false;
}

bool is_embedded_component_resource(
    const std::string& filename,
    const toml::value& profile,
    const std::string& component,
    const toml::value& value
) {
    if (!value.is_array()) return false;
    const char* resource_key = component == "brightness"
        ? "brightness_rules"
        : component == "sihua" ? "sihua_rules" : NULL;
    if (resource_key == NULL
        || profile.as_table().count(resource_key) == 0u) {
        return false;
    }
    const std::string resource_path =
        toml::find<std::string>(profile, resource_key);
    if (resource_path == filename) return true;
    const std::string resolved = resolve_path(filename, resource_path);
    return resolved == filename
        || (filename.find_first_of("/\\") == std::string::npos
            && resolved == "./" + filename);
}

void validate_profile_defaults(
    const std::string& filename,
    const toml::value& profile,
    bool has_master_resource
) {
    if (profile.as_table().count("defaults") == 0u) return;
    const toml::value& value = profile.at("defaults");
    if (!value.is_table()) {
        throw semantic_error(filename, "defaults",
            "expected an option-default table");
    }
    const toml::table& defaults = value.as_table();
    for (toml::table::const_iterator it = defaults.begin();
         it != defaults.end(); ++it) {
        const bool known = it->first == "placement"
            || it->first == "brightness"
            || it->first == "sihua"
            || it->first == "masters"
            || it->first == "longevity";
        if (!known) {
            throw semantic_error(filename, "defaults." + it->first,
                "unknown rule component");
        }
        if (!it->second.is_string()) {
            throw semantic_error(filename, "defaults." + it->first,
                "expected a string option name");
        }
        if (it->first == "masters" && !has_master_resource) {
            throw semantic_error(filename, "defaults.masters",
                "masters resource is not available");
        }
    }
}

void validate_longevity_variants(
    const std::string& filename,
    const std::unordered_map<std::string, PlacementRule>& variants
) {
    std::unordered_map<std::string, std::size_t> option_counts;
    for (std::unordered_map<std::string, PlacementRule>::const_iterator it =
            variants.begin(); it != variants.end(); ++it) {
        const std::string::size_type separator = it->first.find('\n');
        if (separator == std::string::npos) {
            throw semantic_error(filename, "placements",
                "internal longevity variant key is malformed");
        }
        if (!is_longevity_star_key(it->first.substr(0u, separator))) continue;
        ++option_counts[it->first.substr(separator + 1u)];
    }
    if (option_counts.empty()) {
        // Minimal maintainer fixtures may define a catalog without the
        // twelve-life-stage stars at all.
        return;
    }
    for (std::unordered_map<std::string, std::size_t>::const_iterator option =
            option_counts.begin(); option != option_counts.end(); ++option) {
        if (option->second != kLongevityStarCount) {
            throw semantic_error(filename, "placements",
                "option '" + option->first
                    + "' must define every one of the 12 life stages");
        }
        for (std::size_t i = 0u; i < kLongevityStarCount; ++i) {
            if (variants.find(std::string(kLongevityStarKeys[i]) + "\n"
                    + option->first)
                    == variants.end()) {
                throw semantic_error(filename, "placements",
                    "option '" + option->first
                        + "' is missing '" + kLongevityStarKeys[i] + "'");
            }
        }
    }
}

void validate_profile_selection_keys(
    const std::string& filename,
    const toml::value& profile,
    const StarRegistry& registry
) {
    const char* star_components[2] = {"placement", "brightness"};
    for (std::size_t component = 0u; component < 2u; ++component) {
        const std::string name(star_components[component]);
        if (profile.as_table().count(name) == 0u) continue;
        const toml::value& selection_value = profile.at(name);
        // Combined maintainer fixtures use [[brightness]] in the same file as
        // the profile.  Such arrays are component resources, not selection
        // maps, and are intentionally ignored here.  A scalar, however, is
        // neither a resource nor a valid selection section and must not
        // silently fall back to option1.
        if (is_embedded_component_resource(
                filename, profile, name, selection_value)) continue;
        if (!selection_value.is_table()) {
            throw semantic_error(filename, name,
                "expected an option-selection table");
        }
        const toml::table& selection = selection_value.as_table();
        for (toml::table::const_iterator it = selection.begin();
             it != selection.end(); ++it) {
            StarId ignored = kInvalidStarId;
            if (!registry.find(it->first, &ignored)) {
                throw semantic_error(filename, name + "." + it->first,
                    "unknown star key");
            }
            if (name == "placement" && is_longevity_star_key(it->first)) {
                throw semantic_error(filename, name + "." + it->first,
                    "twelve-life-stage stars must use the longevity option");
            }
        }
    }
    if (profile.as_table().count("sihua") == 0u) return;
    const toml::value& sihua_value = profile.at("sihua");
    if (is_embedded_component_resource(
            filename, profile, "sihua", sihua_value)) return;
    if (!sihua_value.is_table()) {
        throw semantic_error(filename, "sihua",
            "expected an option-selection table");
    }
    const toml::table& sihua = sihua_value.as_table();
    for (toml::table::const_iterator it = sihua.begin(); it != sihua.end(); ++it) {
        if (!is_stem_key(it->first)) {
            throw semantic_error(filename, "sihua." + it->first,
                "unknown stem key");
        }
    }
}

void validate_override_keys(
    const detail::ZiweiCatalogSnapshot& snapshot,
    const ZiweiOptionSelection& overrides
) {
    const std::unordered_map<std::string, std::string>* star_maps[2] = {
        &overrides.placement, &overrides.brightness,
    };
    const char* component_names[2] = {"placement", "brightness"};
    for (std::size_t component = 0u; component < 2u; ++component) {
        for (std::unordered_map<std::string, std::string>::const_iterator it =
                star_maps[component]->begin();
             it != star_maps[component]->end(); ++it) {
            StarId ignored = kInvalidStarId;
            if (!snapshot.registry.find(it->first, &ignored)) {
                throw semantic_error(snapshot.profile_filename,
                    std::string("overrides.") + component_names[component]
                        + "." + it->first,
                    "unknown star key");
            }
            if (component == 0u && is_longevity_star_key(it->first)) {
                throw semantic_error(snapshot.profile_filename,
                    std::string("overrides.placement.") + it->first,
                    "twelve-life-stage stars must use the longevity option");
            }
        }
    }
    for (std::unordered_map<std::string, std::string>::const_iterator it =
            overrides.sihua.begin(); it != overrides.sihua.end(); ++it) {
        if (!is_stem_key(it->first)) {
            throw semantic_error(snapshot.profile_filename,
                "overrides.sihua." + it->first,
                "unknown stem key");
        }
    }
}

void compile_context(
    const std::shared_ptr<const detail::ZiweiCatalogSnapshot>& snapshot,
    const ZiweiOptionSelection& overrides,
    CompiledRules* out,
    ZiweiOptionSelection* out_selection
) {
    validate_override_keys(*snapshot, overrides);
    CompiledRules compiled = {};
    compiled.format_version = 2u;
    compiled.star_count = snapshot->registry.size();
    compiled.registry_fingerprint = snapshot->registry.fingerprint();
    compiled.natal_star_count = snapshot->natal_star_count;
    compiled.natal_by_star.resize(compiled.star_count, 0u);
    compiled.placement.natal.reserve(compiled.natal_star_count);
    compiled.placement.flow.reserve(
        compiled.star_count - compiled.natal_star_count);
    compiled.brightness.values.resize(compiled.star_count);

    ZiweiOptionSelection selected;
    for (StarId id = 0u; id < compiled.star_count; ++id) {
        const std::string& star = snapshot->registry.at(id).key;
        const bool is_natal = snapshot->registry.at(id).natal;
        compiled.natal_by_star[id] = is_natal ? 1u : 0u;
        const bool is_longevity = is_natal
            && is_longevity_star_key(star);
        const bool is_module_owned =
            snapshot->module_owned_stars.count(star) != 0u;
        const std::string placement_option = is_longevity
            ? choose_component_option(
                overrides.longevity, snapshot->profile_selection.longevity)
            : choose_option(
                overrides.placement,
                is_module_owned ? std::string()
                                : overrides.placement_default,
                snapshot->profile_selection.placement,
                star);
        const PlacementRule placement = require_variant(
            *snapshot,
            snapshot->placement_variants,
            is_longevity ? "longevity" : "placement",
            star,
            placement_option);
        if (is_natal) {
            compiled.placement.natal.push_back(placement);
        } else {
            compiled.placement.flow.push_back(placement);
        }
        if (is_longevity) selected.longevity = placement_option;
        else selected.placement[star] = placement_option;

        const std::string brightness_option = choose_option(
            overrides.brightness,
            is_module_owned ? std::string()
                            : overrides.brightness_default,
            snapshot->profile_selection.brightness,
            star);
        compiled.brightness.values[id] = require_variant(
            *snapshot,
            snapshot->brightness_variants,
            "brightness",
            star,
            brightness_option);
        selected.brightness[star] = brightness_option;
    }

    for (std::size_t stem = 0u; stem < kStemCount; ++stem) {
        const std::string key = kStemKeys[stem];
        const std::string option = choose_option(
            overrides.sihua,
            overrides.sihua_default,
            snapshot->profile_selection.sihua,
            key);
        compiled.sihua.by_stem[stem] = require_variant(
            *snapshot,
            snapshot->sihua_variants,
            "sihua",
            key,
            option);
        selected.sihua[key] = option;
    }

    compiled.masters.enabled = false;
    compiled.masters.life.fill(kInvalidStarId);
    compiled.masters.body.fill(kInvalidStarId);
    if (!snapshot->master_variants.empty()) {
        const std::string option = !overrides.masters.empty()
            ? overrides.masters
            : snapshot->profile_selection.masters;
        if (!option.empty()) {
            const std::unordered_map<std::string,
                CompiledMasterRules>::const_iterator found =
                snapshot->master_variants.find(option);
            if (found == snapshot->master_variants.end()) {
                throw semantic_error(snapshot->profile_filename, "masters",
                    "missing selected option '" + option + "'");
            }
            compiled.masters = found->second;
            selected.masters = option;
        }
    } else if (!overrides.masters.empty()) {
        throw semantic_error(snapshot->profile_filename, "overrides.masters",
            "masters resource is not available");
    }

    compiled.registry_fingerprint = compiled_rules_fingerprint(
        compiled, snapshot->registry.fingerprint());

    if (!validate_compiled_rules(compiled, snapshot->registry.size())) {
        throw semantic_error(snapshot->profile_filename, "compiled_tables",
            "compiled invariant validation failed");
    }
    *out = std::move(compiled);
    *out_selection = std::move(selected);
}

StarId resolve_module_star(
    const detail::RuleStarReference& reference,
    const StarRegistry& registry,
    std::size_t catalog_star_count,
    const std::string& module,
    const std::string& path
) {
    if (reference.by_id) {
        if (reference.id >= registry.size()) {
            throw semantic_error(module, path, "unknown numeric star id");
        }
        if (reference.id >= catalog_star_count) {
            throw semantic_error(module, path,
                "numeric references to user-added stars are unstable; "
                "use the star key instead");
        }
        return reference.id;
    }
    StarId id = kInvalidStarId;
    if (!registry.find(reference.key, &id)) {
        throw semantic_error(module, path,
            "unknown star key '" + reference.key + "'");
    }
    return id;
}

std::size_t stem_index(const std::string& key) {
    for (std::size_t stem = 0u; stem < kStemCount; ++stem) {
        if (key == kStemKeys[stem]) return stem;
    }
    return kStemCount;
}

std::string variant_option(const std::string& key) {
    const std::string::size_type separator = key.find('\n');
    return separator == std::string::npos
        ? std::string() : key.substr(separator + 1u);
}

template <typename T>
bool variants_contain_option(
    const std::unordered_map<std::string, T>& variants,
    const std::string& option
) {
    for (typename std::unordered_map<std::string, T>::const_iterator it =
            variants.begin(); it != variants.end(); ++it) {
        if (variant_option(it->first) == option) return true;
    }
    return false;
}

bool catalog_contains_option(
    const detail::ZiweiCatalogSnapshot& snapshot,
    const std::string& option
) {
    return variants_contain_option(snapshot.placement_variants, option)
        || variants_contain_option(snapshot.brightness_variants, option)
        || variants_contain_option(snapshot.sihua_variants, option)
        || snapshot.master_variants.find(option)
            != snapshot.master_variants.end();
}

template <typename T>
void add_module_variant(
    std::unordered_map<std::string, T>* variants,
    const std::string& module,
    const std::string& component,
    const std::string& key,
    const T& value
) {
    if (!variants->insert(std::make_pair(
            entry_key(key, module), value)).second) {
        throw semantic_error(module, component + "." + key,
            "option '" + module + "' already exists");
    }
}

std::shared_ptr<const detail::ZiweiCatalogSnapshot> extend_catalog_snapshot(
    const std::shared_ptr<const detail::ZiweiCatalogSnapshot>& base,
    const ZiweiRuleset& ruleset,
    const ZiweiOptionSelection& selection
) {
    std::shared_ptr<detail::ZiweiCatalogSnapshot> snapshot(
        new detail::ZiweiCatalogSnapshot(*base));
    const std::size_t catalog_star_count = base->registry.size();
    const std::vector<ZiweiRuleModule>& modules = ruleset.modules();
    for (std::size_t module_index = 0u;
         module_index < modules.size(); ++module_index) {
        const detail::ZiweiRuleModuleData& module =
            detail::ZiweiRuleModuleAccess::get(modules[module_index]);
        if (catalog_contains_option(*snapshot, module.label)) {
            throw semantic_error(module.label, "option",
                "the catalog or another module already defines option '"
                    + module.label + "'");
        }

        std::set<std::string> new_stars;

        for (std::size_t i = 0u; i < module.stars.size(); ++i) {
            const detail::RuleStarDefinition& definition = module.stars[i];
            StarId id = kInvalidStarId;
            if (snapshot->registry.find(definition.key, &id)) {
                const StarMetadata& existing = snapshot->registry.at(id);
                if (existing.natal != definition.natal) {
                    throw semantic_error(module.label, definition.key,
                        "cannot change natal/flow scope");
                }
                if (definition.has_category
                    && existing.category != definition.category) {
                    throw semantic_error(module.label, definition.key,
                        "cannot replace a catalog star category");
                }
                continue;
            }
            id = snapshot->registry.add(
                definition.key, definition.category, definition.natal);
            if (definition.natal) ++snapshot->natal_star_count;
            snapshot->module_owned_stars.insert(definition.key);
            snapshot->profile_selection.placement[definition.key] = module.label;
            snapshot->profile_selection.brightness[definition.key] = module.label;
            new_stars.insert(definition.key);
        }

        for (std::map<std::string, PlacementRule>::const_iterator it =
                module.natal_placements.begin();
             it != module.natal_placements.end(); ++it) {
            StarId id = kInvalidStarId;
            if (!snapshot->registry.find(it->first, &id)
                || !snapshot->registry.at(id).natal) {
                throw semantic_error(module.label, "natalPlacements." + it->first,
                    "unknown or non-natal star");
            }
            PlacementRule rule = it->second;
            rule.star_id = id;
            add_module_variant(&snapshot->placement_variants, module.label,
                "placement", it->first, rule);
        }
        for (std::map<std::string, PlacementRule>::const_iterator it =
                module.flow_placements.begin();
             it != module.flow_placements.end(); ++it) {
            StarId id = kInvalidStarId;
            if (!snapshot->registry.find(it->first, &id)
                || snapshot->registry.at(id).natal) {
                throw semantic_error(module.label, "flowPlacements." + it->first,
                    "unknown or non-flow star");
            }
            PlacementRule rule = it->second;
            rule.star_id = id;
            add_module_variant(&snapshot->placement_variants, module.label,
                "placement", it->first, rule);
        }
        for (std::map<std::string,
                std::array<int8_t, kBranchCount> >::const_iterator it =
                module.brightness.begin();
             it != module.brightness.end(); ++it) {
            StarId id = kInvalidStarId;
            if (!snapshot->registry.find(it->first, &id)) {
                throw semantic_error(module.label, "brightness." + it->first,
                    "unknown star key");
            }
            add_module_variant(&snapshot->brightness_variants, module.label,
                "brightness", it->first, it->second);
            new_stars.erase(it->first);
        }
        for (std::set<std::string>::const_iterator it = new_stars.begin();
             it != new_stars.end(); ++it) {
            std::array<int8_t, kBranchCount> none;
            none.fill(static_cast<int8_t>(Brightness::None));
            add_module_variant(&snapshot->brightness_variants, module.label,
                "brightness", *it, none);
        }
        for (std::map<std::string, detail::RuleTransformPatch>::const_iterator it =
                module.sihua.begin(); it != module.sihua.end(); ++it) {
            const std::size_t stem = stem_index(it->first);
            if (stem >= kStemCount) {
                throw semantic_error(module.label, "sihua." + it->first,
                    "unknown stem key");
            }
            const std::unordered_map<std::string, std::string>::const_iterator
                inherited = snapshot->profile_selection.sihua.find(it->first);
            const std::string inherited_option = inherited
                == snapshot->profile_selection.sihua.end()
                ? std::string("option1") : inherited->second;
            TransformSet target = require_variant(*snapshot,
                snapshot->sihua_variants, "sihua", it->first,
                inherited_option);
            StarId* fields[4] = {&target.lu, &target.quan, &target.ke, &target.ji};
            for (std::size_t kind = 0u; kind < 4u; ++kind) {
                if (it->second.present[kind] == 0u) continue;
                *fields[kind] = resolve_module_star(
                    it->second.stars[kind], snapshot->registry,
                    catalog_star_count, module.label, "sihua." + it->first);
            }
            add_module_variant(&snapshot->sihua_variants, module.label,
                "sihua", it->first, target);
        }
        if (module.masters.has_life || module.masters.has_body) {
            CompiledMasterRules masters;
            const std::string inherited_option =
                snapshot->profile_selection.masters.empty()
                ? std::string("option1")
                : snapshot->profile_selection.masters;
            const std::unordered_map<std::string,
                CompiledMasterRules>::const_iterator inherited =
                snapshot->master_variants.find(inherited_option);
            if (inherited == snapshot->master_variants.end()) {
                if (!module.masters.has_life || !module.masters.has_body) {
                    throw semantic_error(module.label, "masters",
                        "a one-sided master patch requires a catalog master table");
                }
                masters.enabled = true;
                masters.life.fill(kInvalidStarId);
                masters.body.fill(kInvalidStarId);
            } else {
                masters = inherited->second;
            }
            if (module.masters.has_life) {
                masters.life_input = module.masters.life_input;
                for (std::size_t branch = 0u; branch < kBranchCount; ++branch) {
                    masters.life[branch] = resolve_module_star(
                        module.masters.life[branch], snapshot->registry,
                        catalog_star_count, module.label, "masters.life");
                }
            }
            if (module.masters.has_body) {
                masters.body_input = module.masters.body_input;
                for (std::size_t branch = 0u; branch < kBranchCount; ++branch) {
                    masters.body[branch] = resolve_module_star(
                        module.masters.body[branch], snapshot->registry,
                        catalog_star_count, module.label, "masters.body");
                }
            }
            if (!snapshot->master_variants.insert(std::make_pair(
                    module.label, masters)).second) {
                throw semantic_error(module.label, "masters",
                    "option '" + module.label + "' already exists");
            }
        }
    }
    validate_longevity_variants(
        snapshot->profile_filename, snapshot->placement_variants);
    CompiledRules validation;
    ZiweiOptionSelection selected;
    const std::shared_ptr<const detail::ZiweiCatalogSnapshot> result(snapshot);
    compile_context(result, selection, &validation, &selected);
    return result;
}

std::shared_ptr<const detail::ZiweiCatalogSnapshot> load_catalog_snapshot(
    const std::string& filename
) {
    try {
        const toml::value profile = parse_toml_file(filename);
        require_version_one(filename, profile);
        const std::string stars_filename = resolve_path(filename,
            toml::find<std::string>(profile, "stars"));
        const std::string placement_filename = resolve_path(filename,
            toml::find<std::string>(profile, "placement_rules"));
        const std::string brightness_filename = resolve_path(filename,
            toml::find<std::string>(profile, "brightness_rules"));
        const std::string sihua_filename = resolve_path(filename,
            toml::find<std::string>(profile, "sihua_rules"));

        const toml::value stars_root = parse_toml_file(stars_filename);
        const toml::value placement_root = parse_toml_file(placement_filename);
        const toml::value brightness_root = parse_toml_file(brightness_filename);
        const toml::value sihua_root = parse_toml_file(sihua_filename);
        require_version_one(stars_filename, stars_root);
        require_version_one(placement_filename, placement_root);
        require_version_one(brightness_filename, brightness_root);
        require_version_one(sihua_filename, sihua_root);

        std::shared_ptr<detail::ZiweiCatalogSnapshot> snapshot(
            new detail::ZiweiCatalogSnapshot());
        snapshot->profile_filename = filename;
        snapshot->generation = g_catalog_generation.fetch_add(
            1u, std::memory_order_relaxed);
        add_stars(stars_filename, stars_root,
            "natal_stars", true, &snapshot->registry);
        snapshot->natal_star_count = snapshot->registry.size();
        add_stars(stars_filename, stars_root,
            "flow_stars", false, &snapshot->registry);

        const toml::array& placements =
            placement_root.at("placements").as_array();
        for (std::size_t i = 0u; i < placements.size(); ++i) {
            const std::string path =
                "placements[" + std::to_string(i) + "]";
            const std::string star =
                toml::find<std::string>(placements[i], "star");
            const std::string option = optional_option_name(placements[i]);
            const StarId id = resolve_star(
                placement_filename, path + ".star", snapshot->registry, star);
            const PlacementRule compiled = compile_flat_placement(
                placement_filename, path, id, placements[i]);
            if (!snapshot->placement_variants.insert(std::make_pair(
                    entry_key(star, option), compiled)).second) {
                throw semantic_error(placement_filename, path,
                    "duplicate star/option entry '" + star + "/" + option + "'");
            }
        }

        validate_longevity_variants(
            placement_filename, snapshot->placement_variants);

        const toml::array& brightness_entries =
            brightness_root.at("brightness").as_array();
        for (std::size_t i = 0u; i < brightness_entries.size(); ++i) {
            const std::string path =
                "brightness[" + std::to_string(i) + "]";
            const std::string star =
                toml::find<std::string>(brightness_entries[i], "star");
            const std::string option = optional_option_name(
                brightness_entries[i]);
            resolve_star(brightness_filename,
                path + ".star", snapshot->registry, star);
            const std::vector<int64_t> values =
                toml::find<std::vector<int64_t> >(
                    brightness_entries[i], "values");
            if (values.size() != kBranchCount) {
                std::ostringstream message;
                message << "expected 12 entries, actual " << values.size();
                throw semantic_error(brightness_filename, path, message.str());
            }
            std::array<int8_t, kBranchCount> compiled = {};
            for (std::size_t branch = 0u; branch < kBranchCount; ++branch) {
                if (values[branch] < -1 || values[branch] > 6) {
                    throw semantic_error(brightness_filename, path,
                        "brightness values must be in -1..6");
                }
                compiled[branch] = static_cast<int8_t>(values[branch]);
            }
            if (!snapshot->brightness_variants.insert(std::make_pair(
                    entry_key(star, option), compiled)).second) {
                throw semantic_error(brightness_filename, path,
                    "duplicate star/option entry '" + star + "/" + option + "'");
            }
        }

        const toml::array& sihua_entries = sihua_root.at("sihua").as_array();
        for (std::size_t i = 0u; i < sihua_entries.size(); ++i) {
            const std::string path = "sihua[" + std::to_string(i) + "]";
            const std::string stem =
                toml::find<std::string>(sihua_entries[i], "stem");
            bool known_stem = false;
            for (std::size_t j = 0u; j < kStemCount; ++j) {
                if (stem == kStemKeys[j]) known_stem = true;
            }
            if (!known_stem) {
                throw semantic_error(sihua_filename, path + ".stem",
                    "unknown stem key '" + stem + "'");
            }
            const std::string option = optional_option_name(sihua_entries[i]);
            const TransformSet compiled = {
                resolve_star(sihua_filename, path + ".lu", snapshot->registry,
                    toml::find<std::string>(sihua_entries[i], "lu")),
                resolve_star(sihua_filename, path + ".quan", snapshot->registry,
                    toml::find<std::string>(sihua_entries[i], "quan")),
                resolve_star(sihua_filename, path + ".ke", snapshot->registry,
                    toml::find<std::string>(sihua_entries[i], "ke")),
                resolve_star(sihua_filename, path + ".ji", snapshot->registry,
                    toml::find<std::string>(sihua_entries[i], "ji")),
            };
            if (!snapshot->sihua_variants.insert(std::make_pair(
                    entry_key(stem, option), compiled)).second) {
                throw semantic_error(sihua_filename, path,
                    "duplicate stem/option entry '" + stem + "/" + option + "'");
            }
        }

        if (profile.as_table().count("master_rules") != 0u) {
            const std::string masters_filename = resolve_path(filename,
                toml::find<std::string>(profile, "master_rules"));
            const toml::value masters_root = parse_toml_file(masters_filename);
            require_version_one(masters_filename, masters_root);
            const toml::array& entries = masters_root.at("masters").as_array();
            for (std::size_t i = 0u; i < entries.size(); ++i) {
                const std::string path =
                    "masters[" + std::to_string(i) + "]";
                const std::string option = optional_option_name(entries[i]);
                const std::vector<std::string> life =
                    toml::find<std::vector<std::string> >(entries[i], "life");
                const std::vector<std::string> body =
                    toml::find<std::vector<std::string> >(entries[i], "body");
                if (life.size() != kBranchCount || body.size() != kBranchCount) {
                    throw semantic_error(masters_filename, path,
                        "life and body tables must each contain 12 star keys");
                }
                CompiledMasterRules compiled;
                compiled.enabled = true;
                compiled.life_input = MasterLookupSource::LifePalace;
                compiled.body_input = MasterLookupSource::SelectedYearBranch;
                for (std::size_t branch = 0u;
                     branch < kBranchCount; ++branch) {
                    compiled.life[branch] = resolve_star(
                        masters_filename, path + ".life", snapshot->registry,
                        life[branch]);
                    compiled.body[branch] = resolve_star(
                        masters_filename, path + ".body", snapshot->registry,
                        body[branch]);
                }
                if (!snapshot->master_variants.insert(std::make_pair(
                        option, compiled)).second) {
                    throw semantic_error(masters_filename, path,
                        "duplicate option '" + option + "'");
                }
            }
        }

        validate_profile_defaults(
            filename, profile, !snapshot->master_variants.empty());
        validate_profile_selection_keys(
            filename, profile, snapshot->registry);
        for (StarId id = 0u; id < snapshot->registry.size(); ++id) {
            const std::string& star = snapshot->registry.at(id).key;
            snapshot->profile_selection.placement[star] =
                selected_option(profile, "placement", star);
            snapshot->profile_selection.brightness[star] =
                selected_option(profile, "brightness", star);
        }
        for (std::size_t stem = 0u; stem < kStemCount; ++stem) {
            const std::string key = kStemKeys[stem];
            snapshot->profile_selection.sihua[key] =
                selected_option(profile, "sihua", key);
        }
        snapshot->profile_selection.masters =
            snapshot->master_variants.empty()
                ? std::string() : default_option(profile, "masters");
        snapshot->profile_selection.longevity =
            default_option(profile, "longevity");

        CompiledRules default_compiled;
        ZiweiOptionSelection default_selected;
        compile_context(snapshot, ZiweiOptionSelection(),
            &default_compiled, &default_selected);
        return snapshot;
    } catch (const RuleLoadError&) {
        throw;
    } catch (const std::bad_alloc&) {
        throw;
    } catch (const std::exception& error) {
        throw RuleLoadError(filename + ": " + error.what());
    }
}

}  // namespace

RuleLoadError::RuleLoadError(const std::string& message)
    : std::runtime_error(message) {}

RuleFileNotFoundError::RuleFileNotFoundError(const std::string& message)
    : RuleLoadError(message) {}

ZiweiOptionSelection::ZiweiOptionSelection()
    : placement_default(),
      brightness_default(),
      sihua_default(),
      masters(),
      longevity(),
      placement(),
      brightness(),
      sihua() {}

ZiweiContext::ZiweiContext()
    : snapshot_(), registry_(), compiled_(), selected_() {}

ZiweiContext::ZiweiContext(
    std::shared_ptr<const detail::ZiweiCatalogSnapshot> snapshot,
    std::shared_ptr<const StarRegistry> registry,
    CompiledRules compiled,
    ZiweiOptionSelection selected
) : snapshot_(std::move(snapshot)),
    registry_(std::move(registry)),
    compiled_(std::move(compiled)),
    selected_(std::move(selected)) {}

const StarRegistry& ZiweiContext::star_registry() const {
    if (!registry_) throw std::logic_error("ZiweiContext is not initialized");
    return *registry_;
}

const CompiledRules& ZiweiContext::compiled_tables() const noexcept {
    return compiled_;
}

const ZiweiOptionSelection& ZiweiContext::selected_options() const noexcept {
    return selected_;
}

uint64_t ZiweiContext::catalog_generation() const noexcept {
    return snapshot_ ? snapshot_->generation : 0u;
}

bool ZiweiContext::valid() const noexcept {
    return snapshot_.get() != NULL && registry_.get() != NULL
        && compiled_rules_match_registry(compiled_, *registry_);
}

ZiweiDataCatalog::ZiweiDataCatalog(const std::string& profile_path)
    : profile_path_(profile_path),
      snapshot_(load_catalog_snapshot(profile_path)) {}

ZiweiDataCatalog::ZiweiDataCatalog(const ZiweiDataCatalog& other)
    : profile_path_(other.profile_path_),
      snapshot_(std::atomic_load(&other.snapshot_)) {}

ZiweiDataCatalog::ZiweiDataCatalog(ZiweiDataCatalog&& other) noexcept
    : profile_path_(std::move(other.profile_path_)),
      snapshot_(std::atomic_load(&other.snapshot_)) {}

ZiweiContext ZiweiDataCatalog::create_context() const {
    return create_context(ZiweiOptionSelection());
}

ZiweiContext ZiweiDataCatalog::create_context(
    const ZiweiOptionSelection& selection
) const {
    return create_context(selection, ZiweiRuleset());
}

ZiweiContext ZiweiDataCatalog::create_context(
    const ZiweiOptionSelection& selection,
    const ZiweiRuleset& ruleset
) const {
    const std::shared_ptr<const detail::ZiweiCatalogSnapshot> snapshot =
        std::atomic_load(&snapshot_);
    if (!snapshot) throw std::logic_error("ZiweiDataCatalog is not initialized");
    if (ruleset.modules().empty()) {
        CompiledRules compiled;
        ZiweiOptionSelection selected;
        compile_context(snapshot, selection, &compiled, &selected);
        const std::shared_ptr<const StarRegistry> registry(
            snapshot, &snapshot->registry);
        return ZiweiContext(snapshot, registry,
            std::move(compiled), std::move(selected));
    }
    const std::shared_ptr<const detail::ZiweiCatalogSnapshot> extended =
        extend_catalog_snapshot(snapshot, ruleset, selection);
    CompiledRules compiled;
    ZiweiOptionSelection selected;
    compile_context(extended, selection, &compiled, &selected);
    const std::shared_ptr<const StarRegistry> registry(
        extended, &extended->registry);
    return ZiweiContext(extended, registry,
        std::move(compiled), std::move(selected));
}

void ZiweiDataCatalog::reload() {
    const std::shared_ptr<const detail::ZiweiCatalogSnapshot> replacement =
        load_catalog_snapshot(profile_path_);
    std::atomic_store(&snapshot_, replacement);
}

const std::string& ZiweiDataCatalog::profile_path() const noexcept {
    return profile_path_;
}

uint64_t ZiweiDataCatalog::generation() const noexcept {
    const std::shared_ptr<const detail::ZiweiCatalogSnapshot> snapshot =
        std::atomic_load(&snapshot_);
    return snapshot ? snapshot->generation : 0u;
}

LoadedRules load_rules_from_toml(const std::string& filename) {
    const ZiweiDataCatalog catalog(filename);
    const ZiweiContext context = catalog.create_context();
    LoadedRules result;
    result.registry = context.star_registry();
    result.compiled = context.compiled_tables();
    return result;
}

}  // namespace ziwei
}  // namespace taiyin
