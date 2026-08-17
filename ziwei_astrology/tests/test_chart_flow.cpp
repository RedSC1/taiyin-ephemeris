#include "taiyin/ziwei/chart.h"
#include "taiyin/ziwei/flow.h"
#include "taiyin/ziwei/rules_loader.h"

#include <cstddef>
#include <cstdint>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

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

taiyin::ziwei::FlowCoordinate flow_coordinate(uint8_t stem, uint8_t branch) {
    return taiyin::ziwei::FlowCoordinate{
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
    facts.birth.gender = taiyin::ziwei::Gender::Female;
    return facts;
}

}  // namespace

int main() {
    using namespace taiyin;
    using namespace taiyin::ziwei;
    int failures = 0;

    const LoadedRules loaded = load_rules_from_toml(
        std::string(TAIYIN_ZIWEI_TEST_ROOT)
        + "/rules/default.toml");
    const Anchors anchors = sample_anchors();
    const CalendarFacts facts = sample_facts();

    NatalChart natal;
    expect(make_natal_chart(
        facts,
        anchors,
        Branch::Hai,
        default_natal_rule_options(),
        loaded.compiled,
        &natal)
        == TAIYIN_STATUS_OK, "build natal chart", &failures);

    CompiledRules different_registry_rules = loaded.compiled;
    different_registry_rules.registry_fingerprint ^= UINT64_C(0x1);
    FlowLayer mismatched_registry_flow;
    expect(make_flow_layer(
            FlowLevel::Year, flow_coordinate(0u, 0u), natal,
            different_registry_rules, &mismatched_registry_flow)
            == TAIYIN_ERROR_INVALID_ARGUMENT,
        "flow rejects rules from a different StarId registry", &failures);

    std::vector<uint8_t> positions;
    expect(dump_natal_star_positions(natal, &positions) == TAIYIN_STATUS_OK,
        "dump numeric star positions", &failures);
    expect(positions.size() == loaded.registry.size(),
        "one dumped position per StarId", &failures);
    expect(natal.rule_registry_fingerprint
            == loaded.compiled.registry_fingerprint
        && natal.body_palace == Branch::Hai
        && natal.gender == Gender::Female,
        "chart retains its rule registry and birth metadata", &failures);

    StarId ziwei = kInvalidStarId;
    StarId tianji = kInvalidStarId;
    StarId tianfu = kInvalidStarId;
    loaded.registry.find("ziwei", &ziwei);
    loaded.registry.find("tianji", &tianji);
    loaded.registry.find("tianfu", &tianfu);
    expect(positions[ziwei] == to_index(Branch::Shen),
        "Ziwei anchor placement", &failures);
    expect(positions[tianji] == to_index(Branch::Wei),
        "Tianji main-star offset", &failures);
    expect(positions[tianfu] == to_index(Branch::Chen),
        "Tianfu anchor placement", &failures);

    // The sample lunar year stem is Yi, so the default natal policy uses the
    // Yi row rather than the distinct solar-term Jia row.
    expect(natal.transformations.birth_year.lu
            == loaded.compiled.sihua.by_stem[to_index(Stem::Yi)].lu,
        "Si-Hua defaults to the lunar-year policy", &failures);

    // 生年、自化、向心 all normalize into one per-StarId mask. Reconstruct the
    // expected 12 bits from the source tables and physical placements.
    std::vector<StarTransformMask> expected_masks(loaded.registry.size(), 0u);
    const TransformSet& birth_transforms = natal.transformations.birth_year;
    const StarId birth_ids[4] = {
        birth_transforms.lu, birth_transforms.quan,
        birth_transforms.ke, birth_transforms.ji};
    for (std::size_t kind = 0u; kind < 4u; ++kind) {
        expected_masks[birth_ids[kind]] = static_cast<StarTransformMask>(
            expected_masks[birth_ids[kind]] | (1u << kind));
    }
    for (std::size_t branch = 0u; branch < kBranchCount; ++branch) {
        const Branch opposite = static_cast<Branch>((branch + 6u) % kBranchCount);
        const TransformSet& own = loaded.compiled.sihua.by_stem[
            to_index(natal.palace_stems[branch])];
        const TransformSet& opposite_transforms = loaded.compiled.sihua.by_stem[
            to_index(natal.palace_stems[to_index(opposite)])];
        const StarId own_ids[4] = {own.lu, own.quan, own.ke, own.ji};
        const StarId opposite_ids[4] = {
            opposite_transforms.lu, opposite_transforms.quan,
            opposite_transforms.ke, opposite_transforms.ji};
        for (std::size_t kind = 0u; kind < 4u; ++kind) {
            if (natal.palaces[branch].stars.test(own_ids[kind])) {
                expected_masks[own_ids[kind]] = static_cast<StarTransformMask>(
                    expected_masks[own_ids[kind]] | (1u << (4u + kind)));
            }
            if (natal.palaces[branch].stars.test(opposite_ids[kind])) {
                expected_masks[opposite_ids[kind]] =
                    static_cast<StarTransformMask>(expected_masks[opposite_ids[kind]]
                        | (1u << (8u + kind)));
            }
        }
    }
    for (std::size_t star = 0u; star < expected_masks.size(); ++star) {
        expect(star_transform_mask(natal, static_cast<StarId>(star))
                == expected_masks[star],
            "per-star transformation overlay follows all three sources",
            &failures);
    }
    NatalRuleOptions solar_sihua = default_natal_rule_options();
    solar_sihua.sihua_year_boundary = PillarBoundary::SolarTerm;
    NatalChart solar_sihua_chart;
    expect(make_natal_chart(
        facts,
        anchors,
        Branch::Hai,
        solar_sihua,
        loaded.compiled,
        &solar_sihua_chart) == TAIYIN_STATUS_OK
        && solar_sihua_chart.transformations.birth_year.lu
            == loaded.compiled.sihua.by_stem[to_index(Stem::Jia)].lu,
        "Si-Hua year source is independently selectable", &failures);
    NatalRuleOptions solar_body_master = default_natal_rule_options();
    solar_body_master.body_master_year_boundary = PillarBoundary::SolarTerm;
    NatalChart solar_body_chart;
    expect(make_natal_chart(
        facts,
        anchors,
        Branch::Hai,
        solar_body_master,
        loaded.compiled,
        &solar_body_chart) == TAIYIN_STATUS_OK
        && solar_body_chart.body_master != natal.body_master,
        "body-master year source is independently selectable", &failures);

    std::size_t placed_count = 0u;
    for (std::size_t branch = 0u; branch < natal.palaces.size(); ++branch) {
        placed_count += natal.palaces[branch].stars.count();
    }
    expect(placed_count == loaded.compiled.natal_star_count,
        "each natal star placed exactly once", &failures);

    Chart chart;
    chart.natal = natal;
    FlowLayer decade;
    expect(initialize_flow_layer(
        FlowLevel::Decade,
        flow_coordinate(0u, 2u),
        loaded.compiled.registry_fingerprint,
        loaded.registry.size(),
        loaded.compiled.sihua.by_stem[to_index(Stem::Jia)],
        &decade) == TAIYIN_STATUS_OK,
        "initialize decade layer", &failures);
    decade.stars[to_index(Branch::Zi)].set(ziwei);
    FlowLayer duplicate_decade = decade;
    duplicate_decade.stars[to_index(Branch::Chou)].set(ziwei);
    Chart duplicate_chart;
    duplicate_chart.natal = natal;
    expect(push_flow_layer(&duplicate_chart, std::move(duplicate_decade))
            == TAIYIN_ERROR_INVALID_ARGUMENT,
        "flow layer rejects a star placed in two palaces", &failures);

    FlowLayer foreign_decade = decade;
    foreign_decade.rule_registry_fingerprint ^= UINT64_C(0x1);
    Chart foreign_chart;
    foreign_chart.natal = natal;
    expect(push_flow_layer(&foreign_chart, std::move(foreign_decade))
            == TAIYIN_ERROR_INVALID_ARGUMENT,
        "chart rejects a flow layer from another StarId registry", &failures);

    expect(push_flow_layer(&chart, std::move(decade)) == TAIYIN_STATUS_OK,
        "push decade layer", &failures);
    expect(chart.flow_stack.size() == 1u
        && chart.flow_stack[0].level == FlowLevel::Decade,
        "decade is flow_stack[0]", &failures);

    FlowLayer month;
    expect(initialize_flow_layer(
        FlowLevel::Month,
        flow_coordinate(2u, 3u),
        loaded.compiled.registry_fingerprint,
        loaded.registry.size(),
        loaded.compiled.sihua.by_stem[to_index(Stem::Bing)],
        &month) == TAIYIN_STATUS_OK,
        "initialize month layer", &failures);
    expect(push_flow_layer(&chart, std::move(month)) == TAIYIN_ERROR_INVALID_ARGUMENT,
        "cannot skip year layer", &failures);
    expect(chart.flow_stack.size() == 1u,
        "failed push leaves stack unchanged", &failures);

    FlowLayer year;
    expect(initialize_flow_layer(
        FlowLevel::Year,
        flow_coordinate(1u, 3u),
        loaded.compiled.registry_fingerprint,
        loaded.registry.size(),
        loaded.compiled.sihua.by_stem[to_index(Stem::Yi)],
        &year) == TAIYIN_STATUS_OK,
        "initialize year layer", &failures);
    Chart corrupt_existing_stack = chart;
    corrupt_existing_stack.flow_stack[0]
        .stars[to_index(Branch::Chou)].set(ziwei);
    FlowLayer year_for_corrupt_stack = year;
    expect(push_flow_layer(
            &corrupt_existing_stack, std::move(year_for_corrupt_stack))
            == TAIYIN_ERROR_INVALID_ARGUMENT,
        "push rejects an already-corrupt flow stack", &failures);

    Chart corrupt_natal_chart = chart;
    corrupt_natal_chart.natal.palaces[to_index(Branch::Chou)]
        .stars.set(ziwei);
    FlowLayer year_for_corrupt_natal = year;
    expect(push_flow_layer(
            &corrupt_natal_chart, std::move(year_for_corrupt_natal))
            == TAIYIN_ERROR_INVALID_ARGUMENT,
        "push rejects ambiguous natal star placement", &failures);

    expect(push_flow_layer(&chart, std::move(year)) == TAIYIN_STATUS_OK,
        "push year layer", &failures);
    expect(chart.flow_stack.size() == 2u
        && chart.flow_stack[1].level == FlowLevel::Year,
        "partial stack through year", &failures);

    expect(chart.flow_stack[0].stars[to_index(Branch::Zi)].test(ziwei),
        "decade star remains in decade bitset", &failures);
    expect(chart.flow_stack[1].stars[to_index(Branch::Zi)].none(),
        "year star bitset is isolated", &failures);
    expect(natal.palaces[to_index(Branch::Zi)].stars
        == chart.natal.palaces[to_index(Branch::Zi)].stars,
        "flow mutation does not alter natal bitsets", &failures);

    {
        const LoadedRules complete = load_rules_from_toml(
            std::string(TAIYIN_ZIWEI_TEST_ROOT) + "/rules/default.toml");
        NatalChart complete_chart;
        expect(make_natal_chart(
            facts,
            anchors,
            Branch::Hai,
            default_natal_rule_options(),
            complete.compiled,
            &complete_chart) == TAIYIN_STATUS_OK,
            "build complete 115-star natal chart", &failures);
        std::size_t complete_count = 0u;
        for (std::size_t branch = 0u;
             branch < complete_chart.palaces.size(); ++branch) {
            complete_count += complete_chart.palaces[branch].stars.count();
        }
        expect(complete_count == 115u,
            "complete rules place every natal star exactly once", &failures);

        FlowLayer complete_flow;
        expect(make_flow_layer(
            FlowLevel::Year,
            flow_coordinate(0u, 4u),
            complete_chart,
            complete.compiled,
            &complete_flow) == TAIYIN_STATUS_OK,
            "build complete 44-star flow layer", &failures);
        std::size_t flow_count = 0u;
        for (std::size_t branch = 0u;
             branch < complete_flow.stars.size(); ++branch) {
            flow_count += complete_flow.stars[branch].count();
        }
        expect(flow_count == 44u,
            "complete flow rules place every flow star exactly once", &failures);
        StarId flow_lucun = kInvalidStarId;
        StarId flow_lishi = kInvalidStarId;
        complete.registry.find("flow_lucun", &flow_lucun);
        complete.registry.find("flow_lishi_boshi12", &flow_lishi);
        expect(complete_flow.stars[to_index(Branch::Yin)].test(flow_lucun),
            "flow Lucun uses the layer stem", &failures);
        expect(complete_flow.stars[to_index(Branch::Chou)].test(flow_lishi),
            "flow Boshi cycle uses natal gender and layer stem", &failures);

        FlowLayer hybrid_flow;
        expect(make_flow_layer(
            FlowLevel::Month,
            // Bing/Mao is not a valid sexagenary pair, but it is a valid
            // hybrid flow coordinate and must remain representable.
            flow_coordinate(2u, 3u),
            complete_chart,
            complete.compiled,
            &hybrid_flow) == TAIYIN_STATUS_OK,
            "hybrid flow coordinates do not require Ganzhi parity", &failures);
        expect(hybrid_flow.coordinate.stem == Stem::Bing
            && hybrid_flow.coordinate.branch == Branch::Mao,
            "flow layer preserves both hybrid coordinate components", &failures);

        std::vector<uint8_t> flow_positions;
        expect(dump_flow_star_positions(hybrid_flow, &flow_positions)
            == TAIYIN_STATUS_OK
            && flow_positions.size() == complete.registry.size(),
            "flow layer has stable numeric dump", &failures);
        std::size_t dumped_flow_count = 0u;
        for (std::size_t star = 0u; star < flow_positions.size(); ++star) {
            if (flow_positions[star] != 0xffu) ++dumped_flow_count;
        }
        expect(dumped_flow_count == 44u,
            "numeric flow dump contains all flow-only stars", &failures);

        std::size_t exhaustive_flow_cases = 0u;
        for (uint8_t stem = 0u; stem < kStemCount; ++stem) {
            for (uint8_t branch = 0u; branch < kBranchCount; ++branch) {
                for (uint8_t gender = 0u; gender < 2u; ++gender) {
                    NatalChart candidate = complete_chart;
                    candidate.gender = static_cast<Gender>(gender);
                    FlowLayer candidate_flow;
                    const Status candidate_status = make_flow_layer(
                        FlowLevel::Year,
                        flow_coordinate(stem, branch),
                        candidate,
                        complete.compiled,
                        &candidate_flow);
                    expect(candidate_status == TAIYIN_STATUS_OK,
                        "exhaustive flow coordinate", &failures);
                    std::size_t candidate_count = 0u;
                    if (candidate_status == TAIYIN_STATUS_OK) {
                        for (std::size_t palace = 0u;
                             palace < candidate_flow.stars.size(); ++palace) {
                            candidate_count +=
                                candidate_flow.stars[palace].count();
                        }
                    }
                    expect(candidate_count == 44u,
                        "every finite flow state places 44 stars", &failures);
                    ++exhaustive_flow_cases;
                }
            }
        }
        expect(exhaustive_flow_cases == 240u,
            "exhausted stem x branch x gender flow states", &failures);
    }

    Anchors invalid = anchors;
    invalid.palace_positions[11] = invalid.palace_positions[0];
    expect(make_natal_chart(
        facts,
        invalid,
        Branch::Hai,
        default_natal_rule_options(),
        loaded.compiled,
        &natal)
        == TAIYIN_ERROR_INVALID_ARGUMENT,
        "natal rejects malformed anchors", &failures);

    if (failures != 0) {
        std::cerr << failures << " Ziwei chart/flow checks failed\n";
        return 1;
    }
    std::cout << "Ziwei chart/flow checks passed\n";
    return 0;
}
