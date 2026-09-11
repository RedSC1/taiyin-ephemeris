#include "taiyin/bazi/shen_sha_catalog.h"
#include <algorithm>
#include <set>
#include <stdexcept>

namespace taiyin { namespace bazi {
namespace {
void require_key(const std::string& value) {
    if (value.empty() || value.find_first_of(": \t\r\n") != std::string::npos)
        throw std::invalid_argument("Shen Sha keys must be nonempty and contain no whitespace or colon");
}
void require_label(const std::string& value) {
    require_key(value);
    if (value == "builtin" || value == "option1")
        throw std::invalid_argument("Default Shen Sha definitions cannot be replaced or removed");
}
std::string builtin_key(std::size_t id) {
    return "builtin:" + std::to_string(id);
}
}

BaziShenShaModule::BaziShenShaModule(
    const std::string& label, const std::vector<BaziShenShaRule>& rules)
    : label_(label), rules_(rules) {
    require_label(label);
    if (rules.empty()) throw std::invalid_argument("Empty Shen Sha module");
    std::set<std::string> seen;
    for (const auto& rule : rules_) {
        require_key(rule.id);
        if (!seen.insert(rule.id).second || !rule.test ||
            rule.name.find_first_not_of(" \t\r\n") == std::string::npos)
            throw std::invalid_argument("Invalid or duplicate Shen Sha rule");
    }
}
const std::string& BaziShenShaModule::label() const noexcept { return label_; }
const std::vector<BaziShenShaRule>& BaziShenShaModule::rules() const noexcept { return rules_; }

struct BaziShenShaContext::Data {
    std::vector<BaziShenShaModule> modules;
    std::set<std::string> disabled;
};

BaziShenShaCatalog::BaziShenShaCatalog() {}
BaziShenShaCatalog BaziShenShaCatalog::add_module(const BaziShenShaModule& module) const {
    // Revalidate value objects, including a caller's moved-from module.
    const BaziShenShaModule checked(module.label(), module.rules());
    for (const auto& existing : modules_)
        if (existing.label() == module.label())
            throw std::invalid_argument("Duplicate Shen Sha module label");
    BaziShenShaCatalog result(*this);
    result.modules_.push_back(checked);
    return result;
}
BaziShenShaCatalog BaziShenShaCatalog::remove_module(const std::string& label) const {
    require_label(label);
    BaziShenShaCatalog result(*this);
    auto it = std::find_if(result.modules_.begin(), result.modules_.end(),
        [&](const BaziShenShaModule& module) { return module.label() == label; });
    if (it == result.modules_.end()) throw std::invalid_argument("Unknown Shen Sha module");
    result.modules_.erase(it);
    return result;
}
const std::vector<BaziShenShaModule>& BaziShenShaCatalog::modules() const noexcept { return modules_; }
BaziShenShaContext BaziShenShaCatalog::create_context(const BaziShenShaSelection& selection) const {
    std::set<std::string> known;
    for (std::size_t i = 0; i < kBaziShenShaStableIdCount; ++i) known.insert(builtin_key(i));
    for (const auto& module : modules_)
        for (const auto& rule : module.rules()) known.insert(module.label() + ":" + rule.id);
    std::shared_ptr<BaziShenShaContext::Data> data(new BaziShenShaContext::Data);
    data->modules = modules_;
    for (const auto& id : selection.disabled_ids) {
        if (!known.count(id) || !data->disabled.insert(id).second)
            throw std::invalid_argument("Unknown or duplicate disabled Shen Sha ID");
    }
    return BaziShenShaContext(data);
}
BaziShenShaContext::BaziShenShaContext(std::shared_ptr<const Data> data) : data_(data) {}
std::vector<BaziShenShaMatch> BaziShenShaContext::evaluate(
    const BaziChart& chart, uint8_t target, int32_t target_kind, int32_t gender) const {
    if (!data_) throw std::invalid_argument("Moved-from Shen Sha context");
    if (gender != -1 && gender != BaziGenderFemale && gender != BaziGenderMale)
        throw std::invalid_argument("Invalid Shen Sha gender");
    // Always validate inputs through the original evaluator, even when every
    // built-in is disabled. Neutral evaluation deliberately allows an unknown
    // hour; the gender-dependent collector requires a valid hour itself.
    const BaziShenShaInput input = {chart, target, target_kind, gender};
    uint64_t words[kBaziShenShaWordCount] = {};
    std::size_t count = 0;
    const Status status = gender == -1
        ? collect_target_shen_sha(&input.chart, target, target_kind, words, kBaziShenShaWordCount, &count)
        : collect_target_shen_sha_with_gender(&input.chart, target, target_kind, gender, words, kBaziShenShaWordCount, &count);
    if (status != TAIYIN_STATUS_OK) throw std::invalid_argument("Invalid Shen Sha chart or target");
    std::vector<BaziShenShaMatch> result;
    for (std::size_t i = 0; i < kBaziShenShaStableIdCount; ++i) {
        const std::string id = builtin_key(i);
        if (!data_->disabled.count(id) && (words[i / 64] & (uint64_t(1) << (i % 64))))
            result.push_back(BaziShenShaMatch{id, "", static_cast<int32_t>(i)});
    }
    for (const auto& module : data_->modules) {
        for (const auto& rule : module.rules()) {
            const std::string id = module.label() + ":" + rule.id;
            if (!data_->disabled.count(id) && rule.test(input))
                result.push_back(BaziShenShaMatch{id, rule.name, -1});
        }
    }
    return result;
}
} }
