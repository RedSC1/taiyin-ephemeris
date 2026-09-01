#include "taiyin/ziwei/rules.h"
#include "taiyin/ziwei/chart.h"
#include "taiyin/ziwei/flow.h"
#include "taiyin/ziwei/rule_modules.h"
#include "taiyin/ziwei/rules_loader.h"
#include "taiyin/ziwei/star_registry.h"

#include <cstddef>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>

#ifndef TAIYIN_ZIWEI_TEST_ROOT
#define TAIYIN_ZIWEI_TEST_ROOT "."
#endif

namespace {

void expect(bool condition, const char* message, int* failures) {
    if (condition) return;
    std::cerr << "FAIL: " << message << '\n';
    ++*failures;
}

taiyin::ziwei::Ganzhi ganzhi(uint8_t stem, uint8_t branch) {
    return taiyin::ziwei::Ganzhi{
        static_cast<taiyin::ziwei::Stem>(stem),
        static_cast<taiyin::ziwei::Branch>(branch),
    };
}

taiyin::ziwei::Anchors sample_anchors() {
    using namespace taiyin::ziwei;
    Anchors anchors = {};
    anchors.solar_term = Pillars{
        ganzhi(0u, 0u), ganzhi(2u, 2u),
        ganzhi(4u, 4u), ganzhi(6u, 6u),
    };
    anchors.lunar = Pillars{
        ganzhi(1u, 1u), ganzhi(3u, 3u),
        ganzhi(5u, 5u), ganzhi(7u, 7u),
    };
    anchors.bureau = Bureau::Wood3;
    anchors.ziwei = Branch::Shen;
    anchors.tianfu = Branch::Chen;
    for (std::size_t i = 0u; i < anchors.palace_positions.size(); ++i) {
        anchors.palace_positions[i] = static_cast<Branch>(i);
    }
    return anchors;
}

taiyin::ziwei::CalendarFacts sample_facts() {
    taiyin::ziwei::CalendarFacts facts = {};
    facts.lunar_date.year = 2026;
    facts.lunar_date.month = 4u;
    facts.lunar_date.day = 12u;
    facts.effective_lunar_year = 2026;
    facts.effective_lunar_month = 4u;
    facts.solar_day_from_previous_jie = 7u;
    facts.birth.gender = taiyin::ziwei::Gender::Male;
    return facts;
}

bool load_fails_with(const std::string& path, const std::string& needle) {
    try {
        taiyin::ziwei::load_rules_from_toml(path);
    } catch (const taiyin::ziwei::RuleLoadError& error) {
        return std::string(error.what()).find(needle) != std::string::npos;
    }
    return false;
}

const taiyin::ziwei::PlacementRule* find_rule(
    const std::vector<taiyin::ziwei::PlacementRule>& rules,
    taiyin::ziwei::StarId star
) {
    for (std::size_t i = 0u; i < rules.size(); ++i) {
        if (rules[i].star_id == star) return &rules[i];
    }
    return NULL;
}

}  // namespace

int main() {
    using namespace taiyin::ziwei;
    int failures = 0;
    const std::string root = TAIYIN_ZIWEI_TEST_ROOT;
    const std::string rules_path = root + "/rules/default.toml";

    const LoadedRules loaded = load_rules_from_toml(rules_path);
    expect(loaded.registry.size() == 159u, "default registry size", &failures);
    expect(loaded.compiled.star_count == 159u, "compiled star count", &failures);
    expect(loaded.compiled.placement.natal.size() == 115u,
        "one placement per natal star", &failures);
    expect(validate_compiled_rules(loaded.compiled, loaded.registry.size()),
        "compiled invariants", &failures);

    StarId ziwei = kInvalidStarId;
    StarId tianji = kInvalidStarId;
    StarId lianzhen = kInvalidStarId;
    StarId pojun = kInvalidStarId;
    StarId wuqu = kInvalidStarId;
    StarId taiyang = kInvalidStarId;
    StarId zuofu = kInvalidStarId;
    StarId wenchang = kInvalidStarId;
    expect(loaded.registry.find("ziwei", &ziwei) && ziwei == 0u,
        "declaration-order Ziwei ID", &failures);
    expect(loaded.registry.find("tianji", &tianji) && tianji == 1u,
        "declaration-order Tianji ID", &failures);
    expect(loaded.registry.find("lianzhen", &lianzhen), "resolve Lianzhen", &failures);
    expect(loaded.registry.find("pojun", &pojun), "resolve Pojun", &failures);
    expect(loaded.registry.find("wuqu", &wuqu), "resolve Wuqu", &failures);
    expect(loaded.registry.find("taiyang", &taiyang), "resolve Taiyang", &failures);
    expect(loaded.registry.find("zuofu", &zuofu), "resolve Zuofu", &failures);
    expect(loaded.registry.find("wenchang", &wenchang), "resolve Wenchang", &failures);

    const TransformSet jia = loaded.compiled.sihua.by_stem[to_index(Stem::Jia)];
    expect(jia.lu == lianzhen && jia.quan == pojun
        && jia.ke == wuqu && jia.ji == taiyang,
        "Jia Si-Hua migrated from Dart oracle", &failures);
    expect(loaded.compiled.brightness.values[ziwei][to_index(Branch::Chou)] == 6,
        "brightness compiled by StarId", &failures);
    Brightness brightness = Brightness::None;
    expect(brightness_at(
        loaded.compiled, ziwei, Branch::Chou, &brightness)
            == taiyin::TAIYIN_STATUS_OK
        && brightness == Brightness::Miao,
        "public brightness query returns typed Miao", &failures);
    expect(brightness_at(
        loaded.compiled, kInvalidStarId, Branch::Chou, &brightness)
            == taiyin::TAIYIN_ERROR_INVALID_ARGUMENT,
        "brightness query rejects an invalid StarId", &failures);

    {
        ZiweiDataCatalog catalog(rules_path);
        const ZiweiContext water_earth = catalog.create_context();
        ZiweiOptionSelection selection;
        selection.longevity = "option2";
        const ZiweiContext fire_earth = catalog.create_context(selection);
        StarId changsheng = kInvalidStarId;
        water_earth.star_registry().find("changsheng", &changsheng);
        Anchors earth_anchors = sample_anchors();
        earth_anchors.bureau = Bureau::Earth5;
        Branch earth_position = Branch::Zi;
        expect(water_earth.selected_options().longevity == "option1"
                && fire_earth.selected_options().longevity == "option2",
            "twelve-life-stage option defaults independently to option1",
            &failures);
        expect(evaluate_placement(
                water_earth.compiled_tables().placement.natal[changsheng],
                sample_facts(), earth_anchors, Branch::Hai, &earth_position)
                && earth_position == Branch::Shen,
            "water/earth convention starts earth-five at Shen", &failures);
        expect(evaluate_placement(
                fire_earth.compiled_tables().placement.natal[changsheng],
                sample_facts(), earth_anchors, Branch::Hai, &earth_position)
                && earth_position == Branch::Yin,
            "fire/earth convention starts earth-five at Yin", &failures);
        bool partial_override_threw = false;
        try {
            ZiweiOptionSelection invalid;
            invalid.placement["changsheng"] = "option2";
            catalog.create_context(invalid);
        } catch (const RuleLoadError&) {
            partial_override_threw = true;
        }
        expect(partial_override_threw,
            "life stages reject a partial placement override", &failures);
    }

    const Anchors anchors = sample_anchors();
    const CalendarFacts facts = sample_facts();
    Branch position = Branch::Zi;
    expect(evaluate_placement(loaded.compiled.placement.natal[tianji],
        facts, anchors, Branch::Hai, &position) && position == Branch::Wei,
        "Tianji expands from Ziwei anchor", &failures);
    expect(evaluate_placement(loaded.compiled.placement.natal[zuofu],
        facts, anchors, Branch::Hai, &position) && position == Branch::Wei,
        "Zuofu consumes effective lunar month outside anchors", &failures);
    expect(evaluate_placement(loaded.compiled.placement.natal[wenchang],
        facts, anchors, Branch::Hai, &position) && position == Branch::Mao,
        "Wenchang consumes lunar hour branch", &failures);

    {
        const LoadedRules directional = load_rules_from_toml(
            root + "/tests/data/gender_direction.toml");
        CalendarFacts directional_facts = facts;
        directional_facts.birth.gender = Gender::Male;
        expect(evaluate_placement(
            directional.compiled.placement.natal[0],
            directional_facts,
            anchors,
            Branch::Hai,
            &position) && position == Branch::Xu,
            "Yin-stem male uses reverse direction", &failures);
        directional_facts.birth.gender = Gender::Female;
        expect(evaluate_placement(
            directional.compiled.placement.natal[0],
            directional_facts,
            anchors,
            Branch::Hai,
            &position) && position == Branch::Yin,
            "Yin-stem female uses forward direction", &failures);
    }

    {
        const LoadedRules complete = load_rules_from_toml(
            root + "/rules/default.toml");
        expect(complete.registry.size() == 159u
            && complete.compiled.natal_star_count == 115u
            && complete.compiled.placement.flow.size() == 44u,
            "complete oracle registry contains 115 natal and 44 flow stars",
            &failures);
        expect(validate_compiled_rules(
            complete.compiled, complete.registry.size()),
            "complete oracle rules compile to integer-only runtime data",
            &failures);
        CompiledRules invalid_flow = complete.compiled;
        invalid_flow.placement.flow[0].inputs[0] =
            RuleInputSource::LunarMonthStem;
        expect(!validate_compiled_rules(
            invalid_flow, complete.registry.size()),
            "flow rules reject inputs unavailable from a FlowCoordinate",
            &failures);
        CompiledRules invalid_partition = complete.compiled;
        const PlacementRule swapped = invalid_partition.placement.natal[0];
        invalid_partition.placement.natal[0] =
            invalid_partition.placement.flow[0];
        invalid_partition.placement.flow[0] = swapped;
        expect(!validate_compiled_rules(
            invalid_partition, complete.registry.size()),
            "rules reject a natal/flow StarId partition swap", &failures);

        StarId santai = kInvalidStarId;
        StarId bazuo = kInvalidStarId;
        StarId tiancai = kInvalidStarId;
        StarId tianshou = kInvalidStarId;
        StarId xunkong = kInvalidStarId;
        StarId fuxun = kInvalidStarId;
        StarId lishi = kInvalidStarId;
        complete.registry.find("santai", &santai);
        complete.registry.find("bazuo", &bazuo);
        complete.registry.find("tiancai", &tiancai);
        complete.registry.find("tianshou", &tianshou);
        complete.registry.find("xunkong", &xunkong);
        complete.registry.find("fuxun", &fuxun);
        complete.registry.find("lishi_boshi12", &lishi);

        expect(evaluate_placement(complete.compiled.placement.natal[santai],
            facts, anchors, Branch::Hai, &position)
            && position == Branch::Wu,
            "Santai pipeline is a finite 12x30 table", &failures);
        expect(evaluate_placement(complete.compiled.placement.natal[bazuo],
            facts, anchors, Branch::Hai, &position)
            && position == Branch::Shen,
            "Bazuo pipeline is a finite 12x30 table", &failures);
        expect(evaluate_placement(complete.compiled.placement.natal[tiancai],
            facts, anchors, Branch::Hai, &position)
            && position == Branch::Chou,
            "Tiancai consumes life palace without adding an anchor", &failures);
        expect(evaluate_placement(complete.compiled.placement.natal[tianshou],
            facts, anchors, Branch::Hai, &position)
            && position == Branch::Zi,
            "Tianshou consumes body palace chart metadata", &failures);
        expect(evaluate_placement(complete.compiled.placement.natal[xunkong],
            facts, anchors, Branch::Hai, &position)
            && position == Branch::Hai,
            "Xunkong derives ordered Zheng Kong from Ganzhi", &failures);
        expect(evaluate_placement(complete.compiled.placement.natal[fuxun],
            facts, anchors, Branch::Hai, &position)
            && position == Branch::Xu,
            "Fuxun derives ordered Fu Kong from Ganzhi", &failures);
        expect(evaluate_placement(complete.compiled.placement.natal[lishi],
            facts, anchors, Branch::Hai, &position)
            && position == Branch::Yin,
            "complete rules preserve gender-dependent reverse cycles",
            &failures);
    }

    {
        const LoadedRules defaults = load_rules_from_toml(
            root + "/tests/data/option_default.toml");
        StarId alpha = kInvalidStarId;
        StarId beta = kInvalidStarId;
        defaults.registry.find("alpha", &alpha);
        defaults.registry.find("beta", &beta);
        expect(evaluate_placement(
                defaults.compiled.placement.natal[alpha],
                facts, anchors, Branch::Hai, &position)
                && position == anchors.ziwei,
            "missing placement selection defaults to option1", &failures);
        expect(defaults.compiled.brightness.values[alpha][0] == -1,
            "missing brightness selection defaults to option1", &failures);
        expect(defaults.compiled.sihua.by_stem[to_index(Stem::Jia)].lu
                == alpha,
            "missing Si-Hua selection defaults to option1", &failures);

        const LoadedRules mixed = load_rules_from_toml(
            root + "/tests/data/option_mixed.toml");
        expect(evaluate_placement(
                mixed.compiled.placement.natal[alpha],
                facts, anchors, Branch::Hai, &position)
                && position == Branch::Mao,
            "placement option can be selected independently", &failures);
        expect(mixed.compiled.brightness.values[alpha][0] == 6,
            "brightness option can be selected independently", &failures);
        expect(mixed.compiled.sihua.by_stem[to_index(Stem::Jia)].lu == beta,
            "Si-Hua stem option can be selected independently", &failures);
    }

    {
        ZiweiDataCatalog catalog(
            root + "/tests/data/option_default.toml");
        const ZiweiContext defaults = catalog.create_context();
        expect(defaults.valid(), "catalog creates a valid default context",
            &failures);

        StarId alpha = kInvalidStarId;
        StarId beta = kInvalidStarId;
        defaults.star_registry().find("alpha", &alpha);
        defaults.star_registry().find("beta", &beta);
        expect(evaluate_placement(
                defaults.compiled_tables().placement.natal[alpha],
                facts, anchors, Branch::Hai, &position)
                && position == anchors.ziwei,
            "default context inherits option1 from the catalog profile",
            &failures);

        ZiweiOptionSelection selection;
        selection.placement["alpha"] = "option2";
        selection.brightness["alpha"] = "option2";
        selection.sihua["jia"] = "option2";
        const ZiweiContext mixed = catalog.create_context(selection);
        expect(mixed.valid(), "catalog creates a valid mixed-option context",
            &failures);
        expect(mixed.catalog_generation() == defaults.catalog_generation(),
            "contexts share one parsed catalog snapshot", &failures);
        expect(evaluate_placement(
                mixed.compiled_tables().placement.natal[alpha],
                facts, anchors, Branch::Hai, &position)
                && position == Branch::Mao,
            "context selects placement option without reparsing TOML",
            &failures);
        expect(mixed.compiled_tables().brightness.values[alpha][0] == 6,
            "context selects brightness independently", &failures);
        expect(mixed.compiled_tables().sihua.by_stem[to_index(Stem::Jia)].lu
                == beta,
            "context selects Si-Hua independently", &failures);

        const uint64_t old_generation = defaults.catalog_generation();
        catalog.reload();
        const ZiweiContext reloaded = catalog.create_context();
        expect(catalog.generation() != old_generation
                && reloaded.catalog_generation() == catalog.generation(),
            "reload atomically publishes a new catalog generation", &failures);
        expect(defaults.catalog_generation() == old_generation
                && defaults.valid(),
            "contexts created before reload retain the old immutable snapshot",
            &failures);

        bool invalid_selection_threw = false;
        try {
            ZiweiOptionSelection invalid;
            invalid.placement["alpha"] = "missing-option";
            catalog.create_context(invalid);
        } catch (const RuleLoadError&) {
            invalid_selection_threw = true;
        }
        expect(invalid_selection_threw,
            "invalid option selection is rejected without changing the catalog",
            &failures);
        bool unknown_key_threw = false;
        try {
            ZiweiOptionSelection invalid;
            invalid.placement["not-a-star"] = "option1";
            catalog.create_context(invalid);
        } catch (const RuleLoadError&) {
            unknown_key_threw = true;
        }
        expect(unknown_key_threw,
            "unknown placement override key is rejected", &failures);
        bool unknown_stem_threw = false;
        try {
            ZiweiOptionSelection invalid;
            invalid.sihua["not-a-stem"] = "option1";
            catalog.create_context(invalid);
        } catch (const RuleLoadError&) {
            unknown_stem_threw = true;
        }
        expect(unknown_stem_threw,
            "unknown Si-Hua override key is rejected", &failures);
        bool unsupported_masters_threw = false;
        try {
            ZiweiOptionSelection invalid;
            invalid.masters = "option1";
            catalog.create_context(invalid);
        } catch (const RuleLoadError&) {
            unsupported_masters_threw = true;
        }
        expect(unsupported_masters_threw,
            "master override rejects a catalog without master rules", &failures);

        ZiweiJsonRuleModuleInput complete_masters_input;
        complete_masters_input.label = "custom-masters";
        complete_masters_input.masters_json =
            "{\"ming_zhu\":{\"table\":{"
            "\"0\":\"alpha\",\"1\":\"alpha\",\"2\":\"alpha\","
            "\"3\":\"alpha\",\"4\":\"alpha\",\"5\":\"alpha\","
            "\"6\":\"alpha\",\"7\":\"alpha\",\"8\":\"alpha\","
            "\"9\":\"alpha\",\"10\":\"alpha\",\"11\":\"alpha\"}},"
            "\"shen_zhu\":{\"table\":{"
            "\"0\":\"beta\",\"1\":\"beta\",\"2\":\"beta\","
            "\"3\":\"beta\",\"4\":\"beta\",\"5\":\"beta\","
            "\"6\":\"beta\",\"7\":\"beta\",\"8\":\"beta\","
            "\"9\":\"beta\",\"10\":\"beta\",\"11\":\"beta\"}}}";
        const ZiweiRuleset complete_masters_ruleset =
            ZiweiConfigLoader::get_default().add_module(
                ZiweiConfigLoader::compile_json(complete_masters_input));
        const ZiweiContext unselected_masters = catalog.create_context(
            ZiweiOptionSelection(), complete_masters_ruleset);
        ZiweiOptionSelection complete_masters_selection;
        complete_masters_selection.masters = "custom-masters";
        const ZiweiContext selected_masters = catalog.create_context(
            complete_masters_selection, complete_masters_ruleset);
        expect(!unselected_masters.compiled_tables().masters.enabled
                && unselected_masters.selected_options().masters.empty(),
            "adding a complete master option does not select it by default",
            &failures);
        expect(selected_masters.compiled_tables().masters.enabled
                && selected_masters.compiled_tables().masters.life[0] == alpha
                && selected_masters.compiled_tables().masters.body[0] == beta,
            "a complete JSON master option works without catalog master rules",
            &failures);

        bool one_sided_masters_threw = false;
        try {
            ZiweiJsonRuleModuleInput partial_masters_input;
            partial_masters_input.label = "custom-life-only";
            partial_masters_input.masters_json =
                "{\"ming_zhu\":{\"table\":{"
                "\"0\":\"alpha\",\"1\":\"alpha\",\"2\":\"alpha\","
                "\"3\":\"alpha\",\"4\":\"alpha\",\"5\":\"alpha\","
                "\"6\":\"alpha\",\"7\":\"alpha\",\"8\":\"alpha\","
                "\"9\":\"alpha\",\"10\":\"alpha\","
                "\"11\":\"alpha\"}}}";
            const ZiweiRuleset partial_masters_ruleset =
                ZiweiConfigLoader::get_default().add_module(
                    ZiweiConfigLoader::compile_json(partial_masters_input));
            ZiweiOptionSelection partial_selection;
            partial_selection.masters = "custom-life-only";
            catalog.create_context(partial_selection, partial_masters_ruleset);
        } catch (const RuleLoadError&) {
            one_sided_masters_threw = true;
        }
        expect(one_sided_masters_threw,
            "a one-sided master patch still requires an inherited master table",
            &failures);
        expect(catalog.create_context().valid(),
            "catalog remains usable after an invalid option selection",
            &failures);
    }

    {
        StarRegistry registry;
        expect(registry.add("alpha", StarCategory::Other) == 0u,
            "registry first ID", &failures);
        bool threw = false;
        try {
            registry.add("alpha", StarCategory::Other);
        } catch (const std::invalid_argument&) {
            threw = true;
        }
        expect(threw, "registry rejects duplicate key", &failures);
    }

    {
        ZiweiDataCatalog catalog(rules_path);
        ZiweiJsonRuleModuleInput first_input;
        first_input.label = "custom-base";
        first_input.stars_json =
            "[{\"key\":\"ziwei\",\"rule\":{\"type\":\"constant\",\"value\":0}},"
            "{\"key\":\"custom_star\",\"type\":\"minor\","
            "\"rule\":{\"type\":\"pipeline\",\"steps\":["
            "{\"type\":\"anchor_offset\",\"anchor\":\"ziwei\",\"offset\":2},"
            "{\"type\":\"constant\",\"value\":1}]}}]";
        first_input.brightness_json =
            "{\"static_stars\":{\"custom_star\":[6,6,6,6,6,6,6,6,6,6,6,6]}}";
        first_input.flow_json =
            "[{\"key\":\"custom_flow\",\"type\":\"cycle\","
            "\"rule\":{\"type\":\"anchor_offset\","
            "\"anchor\":\"ziwei\",\"offset\":1}}]";
        first_input.sihua_json =
            "{\"jia\":{\"lu\":\"custom_star\"}}";
        first_input.masters_json =
            "{\"ming_zhu\":{\"boundary\":\"solar\",\"table\":{"
            "\"0\":\"custom_star\",\"1\":\"ziwei\",\"2\":\"ziwei\","
            "\"3\":\"ziwei\",\"4\":\"ziwei\",\"5\":\"ziwei\","
            "\"6\":\"ziwei\",\"7\":\"ziwei\",\"8\":\"ziwei\","
            "\"9\":\"ziwei\",\"10\":\"ziwei\",\"11\":\"ziwei\"}},"
            "\"shen_zhu\":{\"boundary\":\"solar\",\"table\":{"
            "\"0\":\"custom_star\",\"1\":\"ziwei\",\"2\":\"ziwei\","
            "\"3\":\"ziwei\",\"4\":\"ziwei\",\"5\":\"ziwei\","
            "\"6\":\"ziwei\",\"7\":\"ziwei\",\"8\":\"ziwei\","
            "\"9\":\"ziwei\",\"10\":\"ziwei\",\"11\":\"ziwei\"}}}";
        const ZiweiRuleModule first = ZiweiConfigLoader::compile_json(first_input);
        const ZiweiRuleset ruleset =
            ZiweiConfigLoader::get_default().add_module(first);
        const ZiweiContext unselected = catalog.create_context(
            ZiweiOptionSelection(), ruleset);
        expect(unselected.selected_options().placement.at("ziwei") == "option1",
            "adding a module does not replace the catalog-selected option",
            &failures);
        ZiweiOptionSelection custom_selection;
        custom_selection.placement["ziwei"] = "custom-base";
        custom_selection.sihua["jia"] = "custom-base";
        custom_selection.masters = "custom-base";
        const ZiweiContext custom = catalog.create_context(
            custom_selection, ruleset);
        StarId custom_star = kInvalidStarId;
        expect(custom.star_registry().size() == 161u
                && custom.star_registry().find("custom_star", &custom_star)
                && custom_star == 159u
                && custom.star_registry().at(custom_star).natal,
            "JSON module appends a ruleset-local natal star without renumbering built-ins",
            &failures);
        StarId custom_flow = kInvalidStarId;
        const PlacementRule* custom_flow_rule = NULL;
        Branch custom_position = Branch::Zi;
        expect(custom.star_registry().find("custom_flow", &custom_flow)
                && custom_flow == 160u
                && !custom.star_registry().at(custom_flow).natal
                && (custom_flow_rule = find_rule(
                    custom.compiled_tables().placement.flow, custom_flow)) != NULL
                && evaluate_flow_placement(
                    *custom_flow_rule, FlowCoordinate{Stem::Jia, Branch::Zi},
                    Gender::Male, &custom_position, &anchors, Branch::Hai)
                && custom_position == Branch::You,
            "custom flow rules can reuse immutable natal anchors", &failures);
        ZiweiOptionSelection component_defaults;
        component_defaults.placement_default = "option1";
        component_defaults.brightness_default = "option1";
        const ZiweiContext with_component_defaults = catalog.create_context(
            component_defaults, ruleset);
        expect(with_component_defaults.selected_options().placement.at("ziwei")
                    == "option1"
                && with_component_defaults.selected_options().placement.at(
                    "custom_star") == "custom-base"
                && with_component_defaults.selected_options().placement.at(
                    "custom_flow") == "custom-base"
                && with_component_defaults.selected_options().brightness.at(
                    "custom_star") == "custom-base",
            "component defaults do not displace module-owned star options",
            &failures);
        const PlacementRule* ziwei_rule = find_rule(
            custom.compiled_tables().placement.natal, ziwei);
        const PlacementRule* custom_rule = find_rule(
            custom.compiled_tables().placement.natal, custom_star);
        expect(ziwei_rule != NULL && evaluate_placement(
                *ziwei_rule, facts, anchors, Branch::Hai, &custom_position)
                && custom_position == Branch::Zi,
            "a JSON placement is exposed as a separately selected option",
            &failures);
        expect(custom_rule != NULL && evaluate_placement(
                *custom_rule, facts, anchors, Branch::Hai, &custom_position)
                && custom_position == Branch::Hai,
            "JSON pipeline is flattened once into a runtime answer table",
            &failures);
        expect(custom.compiled_tables().brightness.values[custom_star][0] == 6
                && custom.compiled_tables().sihua.by_stem[0].lu == custom_star
                && custom.valid(),
            "custom brightness and partial Si-Hua target the local star id",
            &failures);
        Anchors chart_anchors = anchors;
        chart_anchors.palace_positions[to_index(PalaceId::Life)] = Branch::Xu;
        chart_anchors.palace_positions[to_index(PalaceId::Fortune)] = Branch::Zi;
        NatalRuleOptions chart_options = default_natal_rule_options();
        chart_options.sihua_year_boundary = PillarBoundary::SolarTerm;
        NatalChart custom_chart;
        FlowLayer custom_flow_layer;
        expect(custom.compiled_tables().masters.life_input
                    == MasterLookupSource::SolarYearBranch
                && custom.compiled_tables().masters.body_input
                    == MasterLookupSource::SolarYearBranch
                && make_natal_chart(facts, chart_anchors, Branch::Hai,
                    chart_options, custom.compiled_tables(), &custom_chart)
                    == taiyin::TAIYIN_STATUS_OK
                && custom_chart.palaces[to_index(Branch::Hai)].stars.test(custom_star)
                && custom_chart.life_master == custom_star
                && custom_chart.body_master == custom_star
                && has_star_transform_mark(custom_chart,
                    StarTransformMark::BirthYearLu, custom_star),
            "custom stars, master boundaries and Si-Hua survive real chart construction",
            &failures);
        expect(make_flow_layer(FlowLevel::Year,
                    FlowCoordinate{Stem::Jia, Branch::Zi}, custom_chart,
                    custom.compiled_tables(), &custom_flow_layer)
                    == taiyin::TAIYIN_STATUS_OK
                && custom_flow_layer.stars[to_index(Branch::You)].test(custom_flow),
            "custom flow stars survive real flow-layer construction",
            &failures);
        expect(catalog.create_context().star_registry().size() == 159u,
            "custom ruleset does not mutate the catalog default snapshot",
            &failures);

        ZiweiJsonRuleModuleInput legacy_input;
        legacy_input.label = "legacy-cycle-compatibility";
        legacy_input.stars_json =
            "[{\"key\":\"legacy_boshi\",\"type\":\"boshi12\",\"rule\":{"
            "\"type\":\"lookup_offset\",\"anchor\":\"year_branch\","
            "\"shift_anchor\":\"hour\",\"offset\":2,\"table\":{"
            "\"zi\":0,\"chou\":0,\"yin\":0,\"mao\":0,\"chen\":0,\"si\":0,"
            "\"wu\":0,\"wei\":0,\"shen\":0,\"you\":0,\"xu\":0,\"hai\":0}}},"
            "{\"key\":\"legacy_jiangqian\",\"type\":\"jiangqian12\","
            "\"rule\":{\"type\":\"constant\",\"value\":0}},"
            "{\"key\":\"legacy_suijian\",\"type\":\"suijian12\","
            "\"rule\":{\"type\":\"constant\",\"value\":0}},"
            "{\"key\":\"legacy_changsheng\",\"type\":\"changsheng12\","
            "\"rule\":{\"type\":\"constant\",\"value\":0}},"
            "{\"key\":\"legacy_ziwei_lookup\",\"type\":\"minor\","
            "\"rule\":{\"type\":\"lookup\",\"anchor\":\"ziwei\",\"table\":{"
            "\"zi\":0,\"chou\":1,\"yin\":2,\"mao\":3,\"chen\":4,\"si\":5,"
            "\"wu\":6,\"wei\":7,\"shen\":4,\"you\":9,\"xu\":10,\"hai\":11}}}]";
        legacy_input.brightness_json =
            "{\"_comment\":\"presentation metadata\","
            "\"brightness_labels\":[\"陷\",\"不\",\"平\",\"利\",\"得\",\"旺\",\"庙\"]}";
        const ZiweiRuleset legacy_ruleset = ZiweiConfigLoader::get_default().add_module(
            ZiweiConfigLoader::compile_json(legacy_input));
        const ZiweiContext legacy = catalog.create_context(
            ZiweiOptionSelection(), legacy_ruleset);
        const char* legacy_keys[4] = {
            "legacy_boshi", "legacy_jiangqian",
            "legacy_suijian", "legacy_changsheng",
        };
        bool legacy_categories_ok = true;
        for (std::size_t i = 0u; i < 4u; ++i) {
            StarId id = kInvalidStarId;
            legacy_categories_ok = legacy_categories_ok
                && legacy.star_registry().find(legacy_keys[i], &id)
                && legacy.star_registry().at(id).category == StarCategory::Cycle
                && legacy.compiled_tables().brightness.values[id][0]
                    == static_cast<int8_t>(Brightness::None);
        }
        expect(legacy_categories_ok,
            "legacy cycle categories compile and omitted brightness stays None",
            &failures);
        StarId legacy_boshi = kInvalidStarId;
        legacy.star_registry().find("legacy_boshi", &legacy_boshi);
        const PlacementRule* legacy_boshi_rule = find_rule(
            legacy.compiled_tables().placement.natal, legacy_boshi);
        expect(legacy_boshi_rule != NULL && evaluate_placement(
                *legacy_boshi_rule, facts, anchors, Branch::Hai,
                &custom_position)
                && custom_position == Branch::You,
            "legacy lookup_offset retains its independent constant offset",
            &failures);
        StarId legacy_ziwei_lookup = kInvalidStarId;
        legacy.star_registry().find("legacy_ziwei_lookup", &legacy_ziwei_lookup);
        const PlacementRule* legacy_ziwei_lookup_rule = find_rule(
            legacy.compiled_tables().placement.natal, legacy_ziwei_lookup);
        expect(legacy_ziwei_lookup_rule != NULL && evaluate_placement(
                *legacy_ziwei_lookup_rule, facts, anchors, Branch::Hai,
                &custom_position)
                && custom_position == Branch::Chen,
            "legacy branch-valued lookup anchors use zi-through-hai keys",
            &failures);

        ZiweiJsonRuleModuleInput wide_pipeline_input;
        wide_pipeline_input.label = "wide-pipeline";
        wide_pipeline_input.stars_json =
            "[{\"key\":\"wide_pipeline\",\"rule\":{\"type\":\"pipeline\","
            "\"steps\":[{\"type\":\"constant\",\"value\":2147483647},"
            "{\"type\":\"constant\",\"value\":2147483647}]}}]";
        const ZiweiRuleset wide_pipeline_ruleset =
            ZiweiConfigLoader::get_default().add_module(
                ZiweiConfigLoader::compile_json(wide_pipeline_input));
        const ZiweiContext wide_pipeline = catalog.create_context(
            ZiweiOptionSelection(), wide_pipeline_ruleset);
        StarId wide_pipeline_star = kInvalidStarId;
        wide_pipeline.star_registry().find(
            "wide_pipeline", &wide_pipeline_star);
        const PlacementRule* wide_pipeline_rule = find_rule(
            wide_pipeline.compiled_tables().placement.natal,
            wide_pipeline_star);
        expect(wide_pipeline_rule != NULL && evaluate_placement(
                *wide_pipeline_rule, facts, anchors, Branch::Hai,
                &custom_position)
                && custom_position == Branch::Yin,
            "pipeline accumulation preserves mathematical modulo without int overflow",
            &failures);

        ZiweiJsonRuleModuleInput wide_offset_input;
        wide_offset_input.label = "wide-offsets";
        wide_offset_input.stars_json =
            "[{\"key\":\"wide_anchor_offset\",\"rule\":{"
            "\"type\":\"anchor_offset\",\"anchor\":\"ziwei\","
            "\"offset\":2147483647}},"
            "{\"key\":\"wide_lookup_offset\",\"rule\":{"
            "\"type\":\"lookup_offset\",\"anchor\":\"year_branch\","
            "\"shift_anchor\":\"hour\",\"offset\":2147483647,\"table\":{"
            "\"zi\":2147483647,\"chou\":2147483647,\"yin\":2147483647,"
            "\"mao\":2147483647,\"chen\":2147483647,\"si\":2147483647,"
            "\"wu\":2147483647,\"wei\":2147483647,\"shen\":2147483647,"
            "\"you\":2147483647,\"xu\":2147483647,\"hai\":2147483647}}}]";
        const ZiweiRuleset wide_offset_ruleset =
            ZiweiConfigLoader::get_default().add_module(
                ZiweiConfigLoader::compile_json(wide_offset_input));
        const ZiweiContext wide_offsets = catalog.create_context(
            ZiweiOptionSelection(), wide_offset_ruleset);
        Anchors hai_ziwei = anchors;
        hai_ziwei.ziwei = Branch::Hai;
        StarId wide_anchor_offset = kInvalidStarId;
        wide_offsets.star_registry().find(
            "wide_anchor_offset", &wide_anchor_offset);
        const PlacementRule* wide_anchor_offset_rule = find_rule(
            wide_offsets.compiled_tables().placement.natal,
            wide_anchor_offset);
        expect(wide_anchor_offset_rule != NULL && evaluate_placement(
                *wide_anchor_offset_rule, facts, hai_ziwei, Branch::Hai,
                &custom_position)
                && custom_position == Branch::Wu,
            "anchor offsets widen arithmetic before modulo reduction",
            &failures);
        StarId wide_lookup_offset = kInvalidStarId;
        wide_offsets.star_registry().find(
            "wide_lookup_offset", &wide_lookup_offset);
        const PlacementRule* wide_lookup_offset_rule = find_rule(
            wide_offsets.compiled_tables().placement.natal,
            wide_lookup_offset);
        expect(wide_lookup_offset_rule != NULL && evaluate_placement(
                *wide_lookup_offset_rule, facts, anchors, Branch::Hai,
                &custom_position)
                && custom_position == Branch::You,
            "lookup offsets widen table, shift and constant arithmetic",
            &failures);

        bool malformed_brightness_threw = false;
        try {
            ZiweiJsonRuleModuleInput invalid;
            invalid.label = "malformed-brightness";
            invalid.brightness_json =
                "{\"static_stars\":{\"ziwei\":\"6,6,6\"}}";
            ZiweiConfigLoader::compile_json(invalid);
        } catch (const RuleLoadError&) {
            malformed_brightness_threw = true;
        }
        expect(malformed_brightness_threw,
            "nested brightness entries reject non-array values", &failures);

        const char* invalid_directions[3] = {
            "2", "\"gender_shun_n\"", "true",
        };
        bool invalid_directions_rejected = true;
        for (std::size_t i = 0u; i < 3u; ++i) {
            try {
                ZiweiJsonRuleModuleInput invalid;
                invalid.label = std::string("invalid-direction-")
                    + std::to_string(i);
                invalid.stars_json =
                    "[{\"key\":\"bad_direction\",\"rule\":{"
                    "\"type\":\"anchor_offset\",\"anchor\":\"ziwei\","
                    "\"direction\":" + std::string(invalid_directions[i])
                    + "}}]";
                ZiweiConfigLoader::compile_json(invalid);
                invalid_directions_rejected = false;
            } catch (const RuleLoadError&) {
            }
        }
        expect(invalid_directions_rejected,
            "JSON compiler rejects unsupported direction values", &failures);

        ZiweiJsonRuleModuleInput second_input;
        second_input.label = "custom-last";
        second_input.stars_json =
            "[{\"key\":\"ziwei\",\"rule\":{\"type\":\"constant\",\"value\":5}}]";
        const ZiweiRuleset layered = ruleset.add_module(
            ZiweiConfigLoader::compile_json(second_input));
        ZiweiOptionSelection last_selection = custom_selection;
        last_selection.placement["ziwei"] = "custom-last";
        const ZiweiContext custom_last = catalog.create_context(
            last_selection, layered);
        ziwei_rule = find_rule(
            custom_last.compiled_tables().placement.natal, ziwei);
        expect(ziwei_rule != NULL && evaluate_placement(
                *ziwei_rule, facts, anchors, Branch::Hai, &custom_position)
                && custom_position == Branch::Si,
            "separate labelled modules expose separate placement options",
            &failures);
        expect(custom.star_registry().fingerprint()
                    == custom_last.star_registry().fingerprint()
                && custom.compiled_tables().registry_fingerprint
                    != custom_last.compiled_tables().registry_fingerprint
                && custom.valid() && custom_last.valid(),
            "compiled rule identity distinguishes contexts with the same star registry",
            &failures);
        FlowLayer mismatched_flow_layer;
        expect(make_flow_layer(FlowLevel::Year,
                FlowCoordinate{Stem::Jia, Branch::Zi}, custom_chart,
                custom_last.compiled_tables(), &mismatched_flow_layer)
                    == taiyin::TAIYIN_ERROR_INVALID_ARGUMENT,
            "charts reject a same-registry context with different compiled rules",
            &failures);

        const ZiweiRuleset without_last = layered.remove_module("custom-last");
        const ZiweiContext restored = catalog.create_context(
            custom_selection, without_last);
        ziwei_rule = find_rule(
            restored.compiled_tables().placement.natal, ziwei);
        expect(ziwei_rule != NULL && evaluate_placement(
                *ziwei_rule, facts, anchors, Branch::Hai, &custom_position)
                && custom_position == Branch::Zi,
            "removing one user module removes all of its option tables",
            &failures);
        bool missing_module_threw = false;
        try {
            without_last.remove_module("option1");
        } catch (const std::invalid_argument&) {
            missing_module_threw = true;
        }
        expect(missing_module_threw,
            "catalog options cannot be removed as user modules", &failures);
        const ZiweiRuleset without_custom = ruleset.remove_module("custom-base");
        const ZiweiContext catalog_only = catalog.create_context(
            ZiweiOptionSelection(), without_custom);
        expect(catalog_only.star_registry().size() == 159u,
            "removing a module removes all stars and tables it contributed",
            &failures);
        bool removed_options_rejected = true;
        ZiweiOptionSelection removed_selection;
        removed_selection.placement["ziwei"] = "custom-base";
        try {
            catalog.create_context(removed_selection, without_custom);
            removed_options_rejected = false;
        } catch (const RuleLoadError&) {
        }
        removed_selection = ZiweiOptionSelection();
        removed_selection.sihua["jia"] = "custom-base";
        try {
            catalog.create_context(removed_selection, without_custom);
            removed_options_rejected = false;
        } catch (const RuleLoadError&) {
        }
        removed_selection = ZiweiOptionSelection();
        removed_selection.masters = "custom-base";
        try {
            catalog.create_context(removed_selection, without_custom);
            removed_options_rejected = false;
        } catch (const RuleLoadError&) {
        }
        expect(removed_options_rejected,
            "removing a module removes its placement, Si-Hua and master options",
            &failures);
        expect(custom.star_registry().find("custom_star", &custom_star)
                && custom.compiled_tables().sihua.by_stem[0].lu == custom_star
                && custom.compiled_tables().masters.life[0] == custom_star,
            "removing a module does not mutate an existing context snapshot",
            &failures);

        bool unstable_numeric_reference_threw = false;
        try {
            ZiweiJsonRuleModuleInput numeric_reference_input;
            numeric_reference_input.label = "unstable-numeric-reference";
            numeric_reference_input.stars_json =
                "[{\"key\":\"retarget_star\","
                "\"rule\":{\"type\":\"constant\",\"value\":0}}]";
            numeric_reference_input.sihua_json = "{\"jia\":{\"lu\":159}}";
            const ZiweiRuleset numeric_reference_ruleset = ruleset.add_module(
                ZiweiConfigLoader::compile_json(numeric_reference_input));
            ZiweiOptionSelection numeric_reference_selection;
            numeric_reference_selection.sihua["jia"] =
                "unstable-numeric-reference";
            catalog.create_context(
                numeric_reference_selection, numeric_reference_ruleset);
        } catch (const RuleLoadError&) {
            unstable_numeric_reference_threw = true;
        }
        expect(unstable_numeric_reference_threw,
            "numeric references cannot target removable user-star IDs",
            &failures);

        ZiweiJsonRuleModuleInput stable_numeric_input;
        stable_numeric_input.label = "stable-numeric-reference";
        stable_numeric_input.sihua_json = "{\"jia\":{\"lu\":0}}";
        const ZiweiRuleset stable_numeric_ruleset =
            ZiweiConfigLoader::get_default().add_module(
                ZiweiConfigLoader::compile_json(stable_numeric_input));
        ZiweiOptionSelection stable_numeric_selection;
        stable_numeric_selection.sihua["jia"] = "stable-numeric-reference";
        const ZiweiContext stable_numeric_context = catalog.create_context(
            stable_numeric_selection, stable_numeric_ruleset);
        expect(stable_numeric_context.compiled_tables().sihua.by_stem[0].lu
                == 0u,
            "numeric references to stable catalog star IDs remain supported",
            &failures);

        bool excessive_nesting_threw = false;
        try {
            ZiweiJsonRuleModuleInput nested;
            nested.label = "excessive-nesting";
            nested.stars_json = "0";
            for (std::size_t depth = 0u; depth < 140u; ++depth) {
                nested.stars_json = "[" + nested.stars_json + "]";
            }
            ZiweiConfigLoader::compile_json(nested);
        } catch (const RuleLoadError& error) {
            excessive_nesting_threw = std::string(error.what()).find(
                "maximum nesting depth") != std::string::npos;
        }
        expect(excessive_nesting_threw,
            "JSON compiler rejects excessive nesting before recursive overflow",
            &failures);

        bool duplicate_label_threw = false;
        try {
            layered.add_module(first);
        } catch (const std::invalid_argument&) {
            duplicate_label_threw = true;
        }
        expect(duplicate_label_threw,
            "rulesets reject duplicate module labels", &failures);

        bool reserved_label_threw = false;
        try {
            ZiweiJsonRuleModuleInput reserved;
            reserved.label = "option2";
            ZiweiConfigLoader::compile_json(reserved);
        } catch (const RuleLoadError&) {
            reserved_label_threw = true;
        }
        expect(reserved_label_threw,
            "custom JSON cannot impersonate a built-in option label", &failures);

        bool duplicate_scope_threw = false;
        try {
            ZiweiJsonRuleModuleInput duplicate;
            duplicate.label = "duplicate-scope";
            duplicate.stars_json =
                "[{\"key\":\"same_star\",\"rule\":{\"type\":\"constant\"}}]";
            duplicate.flow_json =
                "[{\"key\":\"same_star\",\"rule\":{\"type\":\"constant\"}}]";
            ZiweiConfigLoader::compile_json(duplicate);
        } catch (const RuleLoadError&) {
            duplicate_scope_threw = true;
        }
        expect(duplicate_scope_threw,
            "one JSON module cannot declare a star in both scopes", &failures);

        bool invalid_master_boundary_threw = false;
        try {
            ZiweiJsonRuleModuleInput invalid;
            invalid.label = "invalid-master-boundary";
            invalid.masters_json =
                "{\"ming_zhu\":{\"boundary\":\"civil\",\"table\":{}}}";
            ZiweiConfigLoader::compile_json(invalid);
        } catch (const RuleLoadError&) {
            invalid_master_boundary_threw = true;
        }
        expect(invalid_master_boundary_threw,
            "master JSON rejects an unknown year boundary", &failures);
    }

    expect(load_fails_with(root + "/tests/data/duplicate_star.toml",
        "duplicate star key"), "loader reports duplicate star", &failures);
    expect(load_fails_with(root + "/tests/data/bad_brightness.toml",
        "expected 12 entries"), "loader reports brightness dimension", &failures);
    expect(load_fails_with(root + "/tests/data/bad_lookup_size.toml",
        "expected 10 positions"), "loader reports flattened table dimension", &failures);
    expect(load_fails_with(root + "/tests/data/bad_profile_selection.toml",
        "placement.ziwie"),
        "loader rejects an unknown profile placement selection key", &failures);
    expect(load_fails_with(root + "/tests/data/bad_profile_selection_type.toml",
        "type"),
        "loader rejects a non-string profile selection", &failures);
    expect(load_fails_with(root + "/tests/data/bad_profile_section_type.toml",
        "placement"),
        "loader rejects a scalar profile selection section", &failures);
    expect(load_fails_with(root + "/tests/data/bad_profile_section_array.toml",
        "brightness"),
        "loader rejects an unrelated array as a profile selection section",
        &failures);
    expect(load_fails_with(root + "/tests/data/bad_profile_default_component.toml",
        "placemnt"),
        "loader rejects an unknown default component", &failures);
    expect(load_fails_with(root + "/tests/data/bad_profile_default_masters.toml",
        "masters resource"),
        "loader rejects a master default without a master resource", &failures);
    expect(load_fails_with(root + "/tests/data/bad_resource_option_type.toml",
        "option"),
        "loader rejects a non-string rule-resource option", &failures);

    {
        CompiledRules malformed_sihua = loaded.compiled;
        malformed_sihua.sihua.by_stem[0u].lu = static_cast<StarId>(
            malformed_sihua.natal_star_count);
        expect(!validate_compiled_rules(
                malformed_sihua, loaded.registry.size()),
            "Si-Hua targets must belong to the natal-star partition", &failures);

        CompiledRules malformed_masters = loaded.compiled;
        malformed_masters.masters.life[0u] = static_cast<StarId>(
            malformed_masters.natal_star_count);
        expect(!validate_compiled_rules(
                malformed_masters, loaded.registry.size()),
            "master tables must target the natal-star partition", &failures);
        CompiledRules malformed_master_input = loaded.compiled;
        malformed_master_input.masters.life_input =
            static_cast<MasterLookupSource>(255u);
        expect(!validate_compiled_rules(
                malformed_master_input, loaded.registry.size()),
            "master lookup sources are validated before chart construction",
            &failures);

        PlacementRule malformed = {};
        malformed.star_id = 0u;
        malformed.inputs.push_back(RuleInputSource::SolarYearStem);
        malformed.strides.push_back(40u);
        malformed.table.assign(10u, 0u);
        Anchors malformed_anchors = sample_anchors();
        malformed_anchors.solar_term.year.stem = Stem::Gui;
        Branch ignored = Branch::Zi;
        expect(!evaluate_placement(malformed, sample_facts(),
                malformed_anchors, Branch::Hai, &ignored),
            "natal placement bounds indexes by the compiled table", &failures);
        const FlowCoordinate coordinate = {Stem::Gui, Branch::Hai};
        expect(!evaluate_flow_placement(malformed, coordinate, Gender::Male,
                &ignored),
            "flow placement bounds indexes by the compiled table", &failures);

        PlacementRule wrapping = {};
        wrapping.star_id = 0u;
        wrapping.inputs.push_back(RuleInputSource::SolarYearStem);
        wrapping.strides.push_back(
            (std::numeric_limits<std::size_t>::max)() / 2u + 2u);
        wrapping.table.assign(4u, 0u);
        malformed_anchors.solar_term.year.stem = Stem::Bing;
        expect(!evaluate_placement(wrapping, sample_facts(),
                malformed_anchors, Branch::Hai, &ignored),
            "natal placement rejects a wrapping stride product", &failures);
        const FlowCoordinate wrapping_coordinate = {Stem::Bing, Branch::Hai};
        expect(!evaluate_flow_placement(
                wrapping, wrapping_coordinate, Gender::Male, &ignored),
            "flow placement rejects a wrapping stride product", &failures);

        PlacementRule solar_day_rule = {};
        solar_day_rule.star_id = 0u;
        solar_day_rule.inputs.push_back(RuleInputSource::SolarDayIndex);
        solar_day_rule.strides.push_back(1u);
        solar_day_rule.table.assign(33u, 0u);
        solar_day_rule.table[32u] = to_index(Branch::Hai);
        CalendarFacts exceptional_solar_day = sample_facts();
        exceptional_solar_day.solar_day_from_previous_jie = 33u;
        expect(rule_input_domain_size(RuleInputSource::SolarDayIndex) == 33u
                && evaluate_placement(solar_day_rule, exceptional_solar_day,
                    sample_anchors(), Branch::Hai, &position)
                && position == Branch::Hai,
            "solar day index accepts the exceptional 33rd Jie-bounded day",
            &failures);
    }

    if (failures != 0) {
        std::cerr << failures << " Ziwei rule compiler checks failed\n";
        return 1;
    }
    std::cout << "Ziwei rule compiler checks passed\n";
    return 0;
}
