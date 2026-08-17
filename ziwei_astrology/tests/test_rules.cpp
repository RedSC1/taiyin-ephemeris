#include "taiyin/ziwei/rules.h"
#include "taiyin/ziwei/rules_loader.h"
#include "taiyin/ziwei/star_registry.h"

#include <cstddef>
#include <iostream>
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

        PlacementRule malformed = {};
        malformed.star_id = 0u;
        malformed.input_count = 1u;
        malformed.inputs.fill(RuleInputSource::SolarYearStem);
        malformed.strides.fill(0u);
        malformed.strides[0u] = 40u;
        malformed.table_size = static_cast<uint16_t>(
            kMaxPlacementTableEntries + 1u);
        malformed.table.fill(0u);
        Anchors malformed_anchors = sample_anchors();
        malformed_anchors.solar_term.year.stem = Stem::Gui;
        Branch ignored = Branch::Zi;
        expect(!evaluate_placement(malformed, sample_facts(),
                malformed_anchors, Branch::Hai, &ignored),
            "natal placement bounds indexes by fixed table capacity", &failures);
        const FlowCoordinate coordinate = {Stem::Gui, Branch::Hai};
        expect(!evaluate_flow_placement(malformed, coordinate, Gender::Male,
                &ignored),
            "flow placement bounds indexes by fixed table capacity", &failures);

        PlacementRule solar_day_rule = {};
        solar_day_rule.star_id = 0u;
        solar_day_rule.input_count = 1u;
        solar_day_rule.inputs.fill(RuleInputSource::SolarYearStem);
        solar_day_rule.inputs[0u] = RuleInputSource::SolarDayIndex;
        solar_day_rule.strides.fill(0u);
        solar_day_rule.strides[0u] = 1u;
        solar_day_rule.table_size = 32u;
        solar_day_rule.table.fill(0u);
        solar_day_rule.table[31u] = to_index(Branch::Hai);
        CalendarFacts exceptional_solar_day = sample_facts();
        exceptional_solar_day.solar_day_from_previous_jie = 32u;
        expect(rule_input_domain_size(RuleInputSource::SolarDayIndex) == 32u
                && evaluate_placement(solar_day_rule, exceptional_solar_day,
                    sample_anchors(), Branch::Hai, &position)
                && position == Branch::Hai,
            "solar day index accepts the exceptional 32nd Jie-bounded day",
            &failures);
    }

    if (failures != 0) {
        std::cerr << failures << " Ziwei rule compiler checks failed\n";
        return 1;
    }
    std::cout << "Ziwei rule compiler checks passed\n";
    return 0;
}
