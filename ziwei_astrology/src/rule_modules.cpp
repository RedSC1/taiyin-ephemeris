#include "taiyin/ziwei/rule_modules.h"
#include "taiyin/ziwei/rules_loader.h"

#include "json_internal.h"
#include "rule_modules_internal.h"

#include <cmath>
#include <cstdint>
#include <limits>
#include <set>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace taiyin {
namespace ziwei {
namespace {

using detail::JsonValue;
using detail::RuleMasterPatch;
using detail::RuleStarDefinition;
using detail::RuleStarReference;
using detail::RuleTransformPatch;
using detail::ZiweiRuleModuleData;

const char* kStemKeys[kStemCount] = {
    "jia", "yi", "bing", "ding", "wu",
    "ji", "geng", "xin", "ren", "gui",
};
const char* kBranchKeys[kBranchCount] = {
    "zi", "chou", "yin", "mao", "chen", "si",
    "wu", "wei", "shen", "you", "xu", "hai",
};
const char* kBureauKeys[5] = {
    "water2", "wood3", "metal4", "earth5", "fire6",
};

std::string trimmed(const std::string& value) {
    const std::string whitespace(" \t\r\n");
    const std::string::size_type first = value.find_first_not_of(whitespace);
    if (first == std::string::npos) return std::string();
    return value.substr(first, value.find_last_not_of(whitespace) - first + 1u);
}

std::string require_label(const std::string& label) {
    const std::string result = trimmed(label);
    if (result.empty()) throw RuleLoadError("rule module label must not be empty");
    return result;
}

bool is_reserved_label(const std::string& label) {
    if (label.size() != 7u) return false;
    const char expected[] = "option";
    for (std::size_t i = 0u; i < 6u; ++i) {
        char value = label[i];
        if (value >= 'A' && value <= 'Z') value = static_cast<char>(value + ('a' - 'A'));
        if (value != expected[i]) return false;
    }
    return label[6] >= '1' && label[6] <= '4';
}

const JsonValue::ObjectValue& require_object(
    const JsonValue& value,
    const std::string& path
) {
    if (!value.is_object()) throw RuleLoadError(path + " must be an object");
    return value.object();
}

const JsonValue::ArrayValue& require_array(
    const JsonValue& value,
    const std::string& path
) {
    if (!value.is_array()) throw RuleLoadError(path + " must be an array");
    return value.array();
}

std::string require_string(const JsonValue& value, const std::string& path) {
    if (!value.is_string()) throw RuleLoadError(path + " must be a string");
    if (trimmed(value.string()).empty()) throw RuleLoadError(path + " must not be empty");
    return value.string();
}

int require_integer(const JsonValue& value, const std::string& path) {
    if (!value.is_number() || std::floor(value.number()) != value.number()
        || value.number() < static_cast<double>((std::numeric_limits<int>::min)())
        || value.number() > static_cast<double>((std::numeric_limits<int>::max)())) {
        throw RuleLoadError(path + " must be an integer");
    }
    return static_cast<int>(value.number());
}

const JsonValue* find_value(
    const JsonValue::ObjectValue& object,
    const std::string& key
) {
    const JsonValue::ObjectValue::const_iterator found = object.find(key);
    return found == object.end() ? NULL : &found->second;
}

std::string optional_string(
    const JsonValue::ObjectValue& object,
    const std::string& key,
    const std::string& fallback
) {
    const JsonValue* value = find_value(object, key);
    return value == NULL ? fallback : require_string(*value, key);
}

StarCategory parse_category(const std::string& value) {
    if (value == "major") return StarCategory::Major;
    if (value == "lucky") return StarCategory::Lucky;
    if (value == "minor") return StarCategory::Minor;
    if (value == "bad" || value == "malefic") return StarCategory::Malefic;
    if (value == "cycle" || value == "boshi12"
        || value == "jiangqian12" || value == "suijian12"
        || value == "changsheng12") {
        return StarCategory::Cycle;
    }
    if (value == "other" || value == "custom") return StarCategory::Other;
    throw RuleLoadError("unknown star category '" + value + "'");
}

std::string boundary_of(
    const JsonValue::ObjectValue& rule,
    const std::string& inherited
) {
    const std::string value = optional_string(rule, "boundary", inherited);
    if (value != "lunar" && value != "solar") {
        throw RuleLoadError("unknown rule boundary '" + value + "'");
    }
    return value;
}

RuleInputSource source_for(
    const std::string& raw,
    const std::string& boundary
) {
    const bool solar = boundary == "solar";
    if (raw == "ziwei") return RuleInputSource::Ziwei;
    if (raw == "tianfu") return RuleInputSource::Tianfu;
    if (raw == "ming") return RuleInputSource::Life;
    if (raw == "body" || raw == "shen") return RuleInputSource::Body;
    if (raw == "wuxingjv") return RuleInputSource::Bureau;
    if (raw == "month") return solar
        ? RuleInputSource::SolarMonthIndex : RuleInputSource::LunarMonthIndex;
    if (raw == "day" || raw == "day_number") return solar
        ? RuleInputSource::SolarDayIndex : RuleInputSource::LunarDayIndex;
    if (raw == "hour") return solar
        ? RuleInputSource::SolarHourBranch : RuleInputSource::LunarHourBranch;
    if (raw == "year_stem") return solar
        ? RuleInputSource::SolarYearStem : RuleInputSource::LunarYearStem;
    if (raw == "year_branch") return solar
        ? RuleInputSource::SolarYearBranch : RuleInputSource::LunarYearBranch;
    if (raw == "month_stem") return solar
        ? RuleInputSource::SolarMonthStem : RuleInputSource::LunarMonthStem;
    if (raw == "month_branch") return solar
        ? RuleInputSource::SolarMonthBranch : RuleInputSource::LunarMonthBranch;
    if (raw == "zheng_kong") return solar
        ? RuleInputSource::SolarZhengKong : RuleInputSource::LunarZhengKong;
    if (raw == "fu_kong") return solar
        ? RuleInputSource::SolarFuKong : RuleInputSource::LunarFuKong;
    throw RuleLoadError("unsupported runtime rule anchor '" + raw + "'");
}

void add_input(std::vector<RuleInputSource>* result, RuleInputSource source) {
    for (std::size_t i = 0u; i < result->size(); ++i) {
        if ((*result)[i] == source) return;
    }
    result->push_back(source);
}

void collect_inputs(
    const JsonValue& value,
    const std::string& inherited,
    std::vector<RuleInputSource>* result
) {
    const JsonValue::ObjectValue& rule = require_object(value, "star rule");
    const std::string boundary = boundary_of(rule, inherited);
    const std::string type = optional_string(rule, "type", std::string());
    if (type == "pipeline") {
        const JsonValue* steps = find_value(rule, "steps");
        if (steps == NULL) throw RuleLoadError("pipeline.steps is required");
        const JsonValue::ArrayValue& entries = require_array(*steps, "pipeline.steps");
        for (std::size_t i = 0u; i < entries.size(); ++i) {
            collect_inputs(entries[i], boundary, result);
        }
        return;
    }
    if (type == "constant") return;
    const JsonValue* anchor = find_value(rule, "anchor");
    if (anchor == NULL) throw RuleLoadError(type + ".anchor is required");
    add_input(result, source_for(require_string(*anchor, type + ".anchor"), boundary));
    if (type == "lookup_offset") {
        const JsonValue* shift = find_value(rule, "shift_anchor");
        if (shift == NULL) throw RuleLoadError("lookup_offset.shift_anchor is required");
        add_input(result, source_for(require_string(*shift,
            "lookup_offset.shift_anchor"), boundary));
    }
    const JsonValue* direction = find_value(rule, "direction");
    if (direction != NULL && direction->is_string()
        && direction->string() == "gender_shun_ni") {
        add_input(result, RuleInputSource::BirthGender);
        add_input(result, boundary == "solar"
            ? RuleInputSource::SolarYearStem : RuleInputSource::LunarYearStem);
    }
}

uint8_t value_for(
    const std::vector<RuleInputSource>& inputs,
    const std::vector<uint8_t>& values,
    RuleInputSource source
) {
    for (std::size_t i = 0u; i < inputs.size(); ++i) {
        if (inputs[i] == source) return values[i];
    }
    throw RuleLoadError("compiled rule is missing a required input");
}

int direction_for(
    const JsonValue::ObjectValue& rule,
    const std::string& boundary,
    const std::vector<RuleInputSource>& inputs,
    const std::vector<uint8_t>& values
) {
    const JsonValue* raw = find_value(rule, "direction");
    if (raw == NULL) return 1;
    if (raw->is_number()) {
        const int value = require_integer(*raw, "direction");
        if (value == 1 || value == -1) return value;
        throw RuleLoadError("direction must be 1, -1, 'ni', or 'gender_shun_ni'");
    }
    if (!raw->is_string()) {
        throw RuleLoadError("direction must be 1, -1, 'ni', or 'gender_shun_ni'");
    }
    if (raw->string() == "ni") return -1;
    if (raw->string() != "gender_shun_ni") {
        throw RuleLoadError("direction must be 1, -1, 'ni', or 'gender_shun_ni'");
    }
    const uint8_t gender = value_for(inputs, values, RuleInputSource::BirthGender);
    const uint8_t stem = value_for(inputs, values, boundary == "solar"
        ? RuleInputSource::SolarYearStem : RuleInputSource::LunarYearStem);
    return (stem & 1u) == gender ? 1 : -1;
}

std::string lookup_key(RuleInputSource source, uint8_t value) {
    switch (source) {
    case RuleInputSource::SolarYearStem:
    case RuleInputSource::SolarMonthStem:
    case RuleInputSource::SolarDayStem:
    case RuleInputSource::SolarHourStem:
    case RuleInputSource::LunarYearStem:
    case RuleInputSource::LunarMonthStem:
    case RuleInputSource::LunarDayStem:
    case RuleInputSource::LunarHourStem:
        return kStemKeys[value];
    case RuleInputSource::SolarYearBranch:
    case RuleInputSource::SolarMonthBranch:
    case RuleInputSource::SolarDayBranch:
    case RuleInputSource::SolarHourBranch:
    case RuleInputSource::LunarYearBranch:
    case RuleInputSource::LunarMonthBranch:
    case RuleInputSource::LunarDayBranch:
    case RuleInputSource::LunarHourBranch:
    case RuleInputSource::Ziwei:
    case RuleInputSource::Tianfu:
    case RuleInputSource::Life:
    case RuleInputSource::Body:
    case RuleInputSource::SolarZhengKong:
    case RuleInputSource::SolarFuKong:
    case RuleInputSource::LunarZhengKong:
    case RuleInputSource::LunarFuKong:
        return kBranchKeys[value];
    case RuleInputSource::Bureau:
        return kBureauKeys[value];
    default:
        return std::to_string(value);
    }
}

int optional_integer(
    const JsonValue::ObjectValue& object,
    const std::string& key,
    int fallback
) {
    const JsonValue* value = find_value(object, key);
    return value == NULL ? fallback : require_integer(*value, key);
}

int position_modulo(int64_t value) {
    return static_cast<int>(value % static_cast<int64_t>(kBranchCount));
}

int evaluate_rule(
    const JsonValue& value,
    const std::string& inherited,
    const std::vector<RuleInputSource>& inputs,
    const std::vector<uint8_t>& values
) {
    const JsonValue::ObjectValue& rule = require_object(value, "star rule");
    const std::string boundary = boundary_of(rule, inherited);
    const std::string type = optional_string(rule, "type", std::string());
    if (type == "constant") return optional_integer(rule, "value", 0);
    if (type == "pipeline") {
        const JsonValue* raw_steps = find_value(rule, "steps");
        if (raw_steps == NULL) throw RuleLoadError("pipeline.steps is required");
        const JsonValue::ArrayValue& steps = require_array(*raw_steps, "pipeline.steps");
        int total = 0;
        for (std::size_t i = 0u; i < steps.size(); ++i) {
            const int step = evaluate_rule(steps[i], boundary, inputs, values);
            total = position_modulo(static_cast<int64_t>(total) + step);
        }
        return total;
    }
    const JsonValue* raw_anchor = find_value(rule, "anchor");
    if (raw_anchor == NULL) throw RuleLoadError(type + ".anchor is required");
    const std::string anchor = require_string(*raw_anchor, type + ".anchor");
    const RuleInputSource source = source_for(anchor, boundary);
    const int anchor_value = value_for(inputs, values, source);
    const int direction = direction_for(rule, boundary, inputs, values);
    if (type == "anchor_offset") {
        const int offset = optional_integer(rule, "offset", 0);
        const bool time_anchor = anchor == "month" || anchor == "day"
            || anchor == "day_number" || anchor == "hour" || anchor == "year";
        return time_anchor
            ? position_modulo(static_cast<int64_t>(offset)
                + static_cast<int64_t>(anchor_value) * direction)
            : position_modulo(static_cast<int64_t>(anchor_value)
                + static_cast<int64_t>(offset) * direction);
    }
    const JsonValue* raw_table = find_value(rule, "table");
    if (raw_table == NULL) throw RuleLoadError(type + ".table is required");
    const JsonValue::ObjectValue& table = require_object(*raw_table, type + ".table");
    const std::string key = lookup_key(source, static_cast<uint8_t>(anchor_value));
    const JsonValue::ObjectValue::const_iterator found = table.find(key);
    if (found == table.end()) throw RuleLoadError(type + ".table has no value for '" + key + "'");
    const int base = require_integer(found->second, type + ".table." + key);
    if (type == "lookup") {
        return position_modulo(static_cast<int64_t>(base)
            + static_cast<int64_t>(optional_integer(rule, "offset", 0))
                * direction);
    }
    if (type == "lookup_offset") {
        const JsonValue* raw_shift = find_value(rule, "shift_anchor");
        if (raw_shift == NULL) throw RuleLoadError("lookup_offset.shift_anchor is required");
        const RuleInputSource shift = source_for(require_string(
            *raw_shift, "lookup_offset.shift_anchor"), boundary);
        return position_modulo(static_cast<int64_t>(base)
            + static_cast<int64_t>(value_for(inputs, values, shift))
                * direction
            + optional_integer(rule, "offset", 0));
    }
    throw RuleLoadError("unsupported runtime rule type '" + type + "'");
}

PlacementRule compile_placement(const JsonValue& rule) {
    PlacementRule result;
    result.star_id = kInvalidStarId;
    collect_inputs(rule, "lunar", &result.inputs);
    if (result.inputs.empty()) {
        // Constant rules still use a one-entry table and no runtime facts.
        result.strides.clear();
        result.table.push_back(static_cast<uint8_t>(
            ((evaluate_rule(rule, "lunar", result.inputs,
                std::vector<uint8_t>()) % 12) + 12) % 12));
        return result;
    }
    result.strides.resize(result.inputs.size());
    std::size_t count = 1u;
    for (std::size_t i = result.inputs.size(); i-- > 0u;) {
        result.strides[i] = count;
        const std::size_t domain = rule_input_domain_size(result.inputs[i]);
        if (domain == 0u || count > (std::numeric_limits<std::size_t>::max)() / domain) {
            throw RuleLoadError("compiled rule table size overflows size_t");
        }
        count *= domain;
    }
    result.table.resize(count);
    std::vector<uint8_t> values(result.inputs.size(), 0u);
    for (std::size_t flat = 0u; flat < count; ++flat) {
        std::size_t remainder = flat;
        for (std::size_t i = result.inputs.size(); i-- > 0u;) {
            const std::size_t domain = rule_input_domain_size(result.inputs[i]);
            values[i] = static_cast<uint8_t>(remainder % domain);
            remainder /= domain;
        }
        const int position = evaluate_rule(rule, "lunar", result.inputs, values);
        result.table[flat] = static_cast<uint8_t>(((position % 12) + 12) % 12);
    }
    return result;
}

RuleStarDefinition parse_star_definition(
    const JsonValue::ObjectValue& star,
    bool natal,
    std::size_t index
) {
    const JsonValue* raw_key = find_value(star, "key");
    if (raw_key == NULL) throw RuleLoadError("star[" + std::to_string(index) + "].key is required");
    RuleStarDefinition result;
    result.key = require_string(*raw_key, "star.key");
    const JsonValue* raw_category = find_value(star, "type");
    if (raw_category == NULL) raw_category = find_value(star, "category");
    result.has_category = raw_category != NULL;
    result.category = raw_category == NULL
        ? (natal ? StarCategory::Other : StarCategory::Cycle)
        : parse_category(require_string(*raw_category, "star.category"));
    result.natal = natal;
    return result;
}

void parse_star_array(
    const std::string& source,
    bool natal,
    ZiweiRuleModuleData* out
) {
    if (source.empty()) return;
    const JsonValue root = detail::parse_json(source);
    const JsonValue::ArrayValue& entries = require_array(root,
        natal ? "starsJson" : "flowJson");
    std::set<std::string> keys;
    for (std::size_t i = 0u; i < entries.size(); ++i) {
        const JsonValue::ObjectValue& star = require_object(entries[i], "star");
        const RuleStarDefinition definition = parse_star_definition(star, natal, i);
        if (!keys.insert(definition.key).second) {
            throw RuleLoadError("duplicate star declaration '" + definition.key + "'");
        }
        const JsonValue* rule = find_value(star, "rule");
        if (rule == NULL) throw RuleLoadError(definition.key + ".rule is required");
        out->stars.push_back(definition);
        (natal ? out->natal_placements : out->flow_placements)
            .insert(std::make_pair(definition.key, compile_placement(*rule)));
        if (!natal) {
            const JsonValue* raw_brightness = find_value(star, "brightness");
            if (raw_brightness != NULL) {
                const JsonValue::ArrayValue& values = require_array(
                    *raw_brightness, definition.key + ".brightness");
                if (values.size() != kBranchCount) {
                    throw RuleLoadError(definition.key + ".brightness must contain 12 values");
                }
                std::array<int8_t, kBranchCount> compiled;
                for (std::size_t branch = 0u; branch < kBranchCount; ++branch) {
                    const int value = require_integer(values[branch], "brightness");
                    if (value < -1 || value > 6) throw RuleLoadError("brightness must be in -1..6");
                    compiled[branch] = static_cast<int8_t>(value);
                }
                out->brightness[definition.key] = compiled;
            }
        }
    }
}

void read_brightness_table(
    const JsonValue& value,
    bool root_level,
    ZiweiRuleModuleData* out
) {
    const JsonValue::ObjectValue& table = require_object(value, "brightness table");
    for (JsonValue::ObjectValue::const_iterator it = table.begin();
         it != table.end(); ++it) {
        if (root_level && (it->first == "brightness_labels"
                || it->first == "_comment" || it->first == "static_stars"
                || it->first == "flow_stars")) {
            continue;
        }
        if (!it->second.is_array()) {
            throw RuleLoadError(it->first + " brightness must be an array");
        }
        const JsonValue::ArrayValue& values = it->second.array();
        if (values.size() != kBranchCount) {
            throw RuleLoadError(it->first + " brightness must contain 12 values");
        }
        std::array<int8_t, kBranchCount> compiled;
        for (std::size_t branch = 0u; branch < kBranchCount; ++branch) {
            const int entry = require_integer(values[branch], "brightness");
            if (entry < -1 || entry > 6) throw RuleLoadError("brightness must be in -1..6");
            compiled[branch] = static_cast<int8_t>(entry);
        }
        out->brightness[it->first] = compiled;
    }
}

void parse_brightness(const std::string& source, ZiweiRuleModuleData* out) {
    if (source.empty()) return;
    const JsonValue root = detail::parse_json(source);
    const JsonValue::ObjectValue& object = require_object(root, "brightnessJson");
    const JsonValue* natal = find_value(object, "static_stars");
    const JsonValue* flow = find_value(object, "flow_stars");
    if (natal != NULL) read_brightness_table(*natal, false, out);
    if (flow != NULL) read_brightness_table(*flow, false, out);
    read_brightness_table(root, true, out);
}

RuleStarReference parse_reference(const JsonValue& value, const std::string& path) {
    RuleStarReference result;
    result.by_id = value.is_number();
    result.id = kInvalidStarId;
    if (result.by_id) {
        const int id = require_integer(value, path);
        if (id < 0 || id >= static_cast<int>(kInvalidStarId)) {
            throw RuleLoadError(path + " contains an invalid star id");
        }
        result.id = static_cast<StarId>(id);
    } else {
        result.key = require_string(value, path);
    }
    return result;
}

void parse_sihua(const std::string& source, ZiweiRuleModuleData* out) {
    if (source.empty()) return;
    const JsonValue root = detail::parse_json(source);
    const JsonValue::ObjectValue& object = require_object(root, "sihuaJson");
    const char* kinds[4] = {"lu", "quan", "ke", "ji"};
    for (JsonValue::ObjectValue::const_iterator it = object.begin();
         it != object.end(); ++it) {
        bool known = false;
        for (std::size_t stem = 0u; stem < kStemCount; ++stem) {
            if (it->first == kStemKeys[stem]) known = true;
        }
        if (!known) throw RuleLoadError("unknown sihua stem '" + it->first + "'");
        const JsonValue::ObjectValue& set = require_object(it->second,
            "sihua." + it->first);
        RuleTransformPatch patch = {};
        bool any = false;
        for (std::size_t kind = 0u; kind < 4u; ++kind) {
            const JsonValue* value = find_value(set, kinds[kind]);
            if (value == NULL) continue;
            patch.present[kind] = 1u;
            patch.stars[kind] = parse_reference(*value,
                "sihua." + it->first + "." + kinds[kind]);
            any = true;
        }
        if (!any) throw RuleLoadError("sihua." + it->first + " is empty");
        out->sihua[it->first] = patch;
    }
}

void parse_master_table(
    const JsonValue& value,
    const std::string& path,
    bool life,
    MasterLookupSource* out_input,
    std::array<RuleStarReference, kBranchCount>* out
) {
    const JsonValue::ObjectValue& rule = require_object(value, path);
    const JsonValue* boundary = find_value(rule, "boundary");
    if (boundary == NULL) {
        *out_input = life ? MasterLookupSource::LifePalace
                          : MasterLookupSource::SelectedYearBranch;
    } else {
        const std::string boundary_name =
            require_string(*boundary, path + ".boundary");
        if (boundary_name == "lunar") {
            *out_input = MasterLookupSource::LunarYearBranch;
        } else if (boundary_name == "solar") {
            *out_input = MasterLookupSource::SolarYearBranch;
        } else {
            throw RuleLoadError(path + ".boundary must be 'lunar' or 'solar'");
        }
    }
    const JsonValue* raw_table = find_value(rule, "table");
    if (raw_table == NULL) throw RuleLoadError(path + ".table is required");
    const JsonValue::ObjectValue& table = require_object(*raw_table, path + ".table");
    for (std::size_t branch = 0u; branch < kBranchCount; ++branch) {
        const std::string key = std::to_string(branch);
        const JsonValue::ObjectValue::const_iterator found = table.find(key);
        if (found == table.end()) throw RuleLoadError(path + ".table." + key + " is required");
        (*out)[branch] = parse_reference(found->second, path + ".table." + key);
    }
}

void parse_masters(const std::string& source, ZiweiRuleModuleData* out) {
    if (source.empty()) return;
    const JsonValue root = detail::parse_json(source);
    const JsonValue::ObjectValue& object = require_object(root, "mastersJson");
    const JsonValue* life = find_value(object, "ming_zhu");
    const JsonValue* body = find_value(object, "shen_zhu");
    if (life != NULL) {
        parse_master_table(*life, "ming_zhu", true,
            &out->masters.life_input, &out->masters.life);
        out->masters.has_life = true;
    }
    if (body != NULL) {
        parse_master_table(*body, "shen_zhu", false,
            &out->masters.body_input, &out->masters.body);
        out->masters.has_body = true;
    }
}

}  // namespace

ZiweiRuleModule::ZiweiRuleModule() : data_() {}

ZiweiRuleModule::ZiweiRuleModule(
    std::shared_ptr<const detail::ZiweiRuleModuleData> data
) : data_(std::move(data)) {}

const std::string& ZiweiRuleModule::label() const {
    if (!data_) throw std::logic_error("ZiweiRuleModule is not initialized");
    return data_->label;
}

bool ZiweiRuleModule::valid() const noexcept { return data_.get() != NULL; }

ZiweiRuleset::ZiweiRuleset() : modules_() {}

ZiweiRuleset::ZiweiRuleset(const std::vector<ZiweiRuleModule>& modules)
    : modules_(modules) {
    std::set<std::string> labels;
    for (std::size_t i = 0u; i < modules_.size(); ++i) {
        if (!modules_[i].valid()) throw std::invalid_argument("ruleset contains an invalid module");
        if (!labels.insert(modules_[i].label()).second) {
            throw std::invalid_argument("duplicate rule module label '" + modules_[i].label() + "'");
        }
    }
}

const std::vector<ZiweiRuleModule>& ZiweiRuleset::modules() const noexcept {
    return modules_;
}

ZiweiRuleset ZiweiRuleset::with(const ZiweiRuleModule& module) const {
    if (!module.valid()) throw std::invalid_argument("rule module is not initialized");
    std::vector<ZiweiRuleModule> result(modules_);
    result.push_back(module);
    return ZiweiRuleset(result);
}

ZiweiRuleset ZiweiConfigLoader::get_default() { return ZiweiRuleset(); }

ZiweiRuleModule ZiweiConfigLoader::compile_json(
    const ZiweiJsonRuleModuleInput& input
) {
    try {
        std::shared_ptr<ZiweiRuleModuleData> data(new ZiweiRuleModuleData());
        data->label = require_label(input.label);
        if (is_reserved_label(data->label)) {
            throw RuleLoadError("custom JSON label is reserved by a built-in option: "
                + data->label);
        }
        data->masters.has_life = false;
        data->masters.has_body = false;
        data->masters.life_input = MasterLookupSource::LifePalace;
        data->masters.body_input = MasterLookupSource::SelectedYearBranch;
        parse_star_array(input.stars_json, true, data.get());
        parse_star_array(input.flow_json, false, data.get());
        std::set<std::string> declared_keys;
        for (std::size_t i = 0u; i < data->stars.size(); ++i) {
            if (!declared_keys.insert(data->stars[i].key).second) {
                throw RuleLoadError("duplicate star declaration '"
                    + data->stars[i].key + "'");
            }
        }
        parse_brightness(input.brightness_json, data.get());
        parse_sihua(input.sihua_json, data.get());
        parse_masters(input.masters_json, data.get());
        return ZiweiRuleModule(data);
    } catch (const RuleLoadError&) {
        throw;
    } catch (const std::bad_alloc&) {
        throw;
    } catch (const std::exception& error) {
        throw RuleLoadError("JSON rule module '" + input.label + "': " + error.what());
    }
}

ZiweiRuleset ZiweiConfigLoader::override_with(
    const ZiweiRuleset& base,
    const ZiweiJsonRuleModuleInput& input
) {
    return base.with(compile_json(input));
}

}  // namespace ziwei
}  // namespace taiyin
