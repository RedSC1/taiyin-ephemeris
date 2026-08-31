#include "taiyin/ziwei/limits.h"
#include "taiyin/ziwei/rules_loader.h"

#include <cstddef>
#include <cstdint>
#include <iostream>
#include <string>
#include <utility>

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
        ganzhi(9u, 11u), ganzhi(0u, 0u),
        ganzhi(2u, 2u), ganzhi(4u, 4u),
    };
    anchors.lunar = anchors.solar_term;
    anchors.bureau = Bureau::Wood3;
    anchors.ziwei = Branch::Zi;
    anchors.tianfu = Branch::Zi;
    for (std::size_t i = 0u; i < kPalaceCount; ++i) {
        anchors.palace_positions[i] = static_cast<Branch>(i);
    }
    return anchors;
}

taiyin::ziwei::CalendarFacts sample_facts() {
    using namespace taiyin::ziwei;
    CalendarFacts facts = {};
    facts.birth.gender = Gender::Female;
    facts.lunar_date.year = 2023;
    facts.lunar_date.month = 4u;
    facts.lunar_date.day = 12u;
    facts.effective_lunar_year = 2023;
    facts.effective_lunar_month = 4u;
    facts.solar_day_from_previous_jie = 7u;
    facts.solar_term_pillars = sample_anchors().solar_term;
    facts.lunar_pillars = sample_anchors().lunar;
    return facts;
}

}  // namespace

int main() {
    using namespace taiyin;
    using namespace taiyin::ziwei;
    int failures = 0;

    const LoadedRules loaded = load_rules_from_toml(
        std::string(TAIYIN_ZIWEI_TEST_ROOT) + "/rules/default.toml");
    const Anchors anchors = sample_anchors();
    const CalendarFacts facts = sample_facts();
    NatalChart natal;
    expect(make_natal_chart(
        facts,
        anchors,
        Branch::Hai,
        default_natal_rule_options(),
        loaded.compiled,
        &natal) == TAIYIN_STATUS_OK,
        "build natal fixture", &failures);

    // 2023 is Gui-Mao. Yin female therefore walks regular decades forward;
    // Wood-3 begins its first decade in physical year 2025.
    DecadeLimit decade;
    expect(make_decade_by_index(natal, 2023, 1u, &decade)
        == TAIYIN_STATUS_OK
        && decade.index == 1u
        && decade.start_age == 3 && decade.end_age == 12
        && decade.start_year == 2025 && decade.end_year == 2034
        && decade.limit.coordinate.branch == Branch::Zi,
        "first regular decade", &failures);
    expect(make_decade_by_index(natal, 2023, 2u, &decade)
        == TAIYIN_STATUS_OK
        && decade.limit.coordinate.branch == Branch::Chou
        && decade.start_age == 13 && decade.end_age == 22,
        "forward second decade", &failures);
    expect(make_decade_for_year(
        natal, 2023, 2034, ChildhoodStrategy::Skip, &decade)
        == TAIYIN_STATUS_OK && decade.index == 1u,
        "last year remains in first decade", &failures);
    expect(make_decade_for_year(
        natal, 2023, 2035, ChildhoodStrategy::Skip, &decade)
        == TAIYIN_STATUS_OK && decade.index == 2u,
        "next physical year enters second decade", &failures);

    expect(make_childhood_decade(
        natal, 2023, 2024, ChildhoodStrategy::Skip, &decade)
        == TAIYIN_STATUS_OK
        && decade.is_childhood && decade.index == 0u
        && decade.start_age == 2
        && decade.limit.coordinate.branch == Branch::Shen,
        "skip-formula childhood age two", &failures);
    expect(make_childhood_decade(
        natal, 2023, 2024, ChildhoodStrategy::Sequential, &decade)
        == TAIYIN_STATUS_OK
        && decade.limit.coordinate.branch == Branch::Chou,
        "sequential childhood follows decade direction", &failures);

    SmallLimit small;
    expect(make_small_limit(natal, Branch::Mao, 3, &small)
        == TAIYIN_STATUS_OK
        && small.coordinate.branch == Branch::Hai
        && small.virtual_age == 3,
        "small limit uses branch group and female reverse direction", &failures);

    FlowYearLimit year;
    expect(make_flow_year(natal, 2025, &year) == TAIYIN_STATUS_OK
        && year.limit.coordinate.stem == Stem::Yi
        && year.limit.coordinate.branch == Branch::Si
        && year.limit.natal_role == PalaceId::Health,
        "flow year hybrid coordinate", &failures);

    FlowMonthLimit month;
    expect(make_flow_month(
        natal,
        2025,
        1u,
        1u,
        false,
        4u,
        Branch::Chou,
        &month) == TAIYIN_STATUS_OK
        && month.doujun == Branch::Mao
        && month.limit.coordinate.stem == Stem::Wu
        && month.limit.coordinate.branch == Branch::Mao,
        "flow month Dou-Jun and Wu-Hu-Dun", &failures);
    expect(!is_valid(Ganzhi{
        month.limit.coordinate.stem, month.limit.coordinate.branch}),
        "flow month fixture is intentionally not a sexagenary pair", &failures);
    expect(make_flow_month(
        natal,
        2025,
        12u,
        13u,
        true,
        4u,
        Branch::Chou,
        &month) == TAIYIN_STATUS_OK
        && month.limit.coordinate.stem == Stem::Geng
        && month.limit.coordinate.branch == Branch::Mao,
        "legacy thirteenth month preserves its sequence-based Wu-Hu-Dun stem",
        &failures);

    FlowMonthLimit early_leap;
    FlowMonthLimit late_leap;
    expect(make_flow_month_from_lunar_month_branch(
            natal, 2033, 2033, 11u, 11u, 12u, true, Branch::Zi,
            FlowMonthPalaceStrategy::PhysicalSequence,
            4u, Branch::Chou, &early_leap) == TAIYIN_STATUS_OK
        && make_flow_month_from_lunar_month_branch(
            natal, 2033, 2033, 11u, 12u, 12u, true, Branch::Zi,
            FlowMonthPalaceStrategy::PhysicalSequence,
            4u, Branch::Chou, &late_leap) == TAIYIN_STATUS_OK
        && early_leap.limit.coordinate.branch
            == late_leap.limit.coordinate.branch
        && early_leap.limit.coordinate.stem
            != late_leap.limit.coordinate.stem
        && early_leap.palace_month_index == 12u
        && late_leap.palace_month_index == 12u,
        "split leap month changes effective stem without moving its physical palace",
        &failures);

    FlowMonthLimit effective_palace;
    expect(make_flow_month_from_lunar_month_branch(
            natal, 2033, 2033, 11u, 11u, 12u, true, Branch::Zi,
            FlowMonthPalaceStrategy::EffectiveMonth,
            4u, Branch::Chou, &effective_palace) == TAIYIN_STATUS_OK
        && effective_palace.palace_month_index == 11u
        && effective_palace.limit.coordinate.branch
            != early_leap.limit.coordinate.branch,
        "effective-month palace strategy remains independently selectable",
        &failures);

    FlowMonthLimit carried_leap_twelve;
    FlowMonthLimit following_first;
    expect(make_flow_month_from_lunar_month_branch(
            natal, 2033, 2034, 12u, 1u, 13u, true, Branch::Chou,
            FlowMonthPalaceStrategy::PhysicalSequence,
            4u, Branch::Chou, &carried_leap_twelve) == TAIYIN_STATUS_OK
        && make_flow_month_from_lunar_month_branch(
            natal, 2034, 2034, 1u, 1u, 1u, false, Branch::Yin,
            FlowMonthPalaceStrategy::PhysicalSequence,
            4u, Branch::Chou, &following_first) == TAIYIN_STATUS_OK
        && carried_leap_twelve.effective_year == 2034
        && carried_leap_twelve.effective_month == 1u
        && carried_leap_twelve.limit.coordinate.stem
            == following_first.limit.coordinate.stem
        && carried_leap_twelve.limit.coordinate.branch
            == following_first.limit.coordinate.branch,
        "leap-twelve carry rebuilds the following annual month coordinate",
        &failures);

    FlowDayLimit day;
    expect(make_flow_day(natal, month, 2u, Stem::Yi, &day)
        == TAIYIN_STATUS_OK
        && day.limit.coordinate.stem == Stem::Yi
        && day.limit.coordinate.branch == Branch::Chen,
        "flow day starts at flow-month palace", &failures);

    FlowHourLimit hour;
    expect(make_flow_hour(natal, day, 1u, &hour)
        == TAIYIN_STATUS_OK
        && hour.limit.coordinate.stem == Stem::Ding
        && hour.limit.coordinate.branch == Branch::Si,
        "flow hour uses Wu-Zi-Dun and advances from flow day", &failures);

    Chart dynamic_chart;
    dynamic_chart.natal = natal;
    FlowLayer layer;
    expect(make_decade_for_year(
        natal, 2023, 2025, ChildhoodStrategy::Skip, &decade)
        == TAIYIN_STATUS_OK
        && make_limit_flow_layer(
            decade.limit, natal, loaded.compiled, &layer)
            == TAIYIN_STATUS_OK
        && push_flow_layer(&dynamic_chart, std::move(layer))
            == TAIYIN_STATUS_OK,
        "materialize and push decade layer", &failures);
    expect(push_limit_flow_layer(&dynamic_chart, year.limit, loaded.compiled)
        == TAIYIN_STATUS_OK,
        "materialize and push year layer", &failures);
    expect(push_limit_flow_layer(&dynamic_chart, month.limit, loaded.compiled)
        == TAIYIN_STATUS_OK,
        "materialize and push month layer", &failures);
    expect(push_limit_flow_layer(&dynamic_chart, day.limit, loaded.compiled)
        == TAIYIN_STATUS_OK,
        "materialize and push day layer", &failures);
    expect(push_limit_flow_layer(&dynamic_chart, hour.limit, loaded.compiled)
        == TAIYIN_STATUS_OK,
        "materialize and push hour layer", &failures);
    expect(dynamic_chart.flow_stack.size() == kFlowLevelCount,
        "formal flow stack has exactly five levels", &failures);
    expect(truncate_flow_stack(&dynamic_chart, FlowLevel::Month)
        == TAIYIN_STATUS_OK
        && dynamic_chart.flow_stack.size() == 2u
        && dynamic_chart.flow_stack.back().level == FlowLevel::Year,
        "changing month cascades through month/day/hour", &failures);
    expect(push_limit_flow_layer(&dynamic_chart, month.limit, loaded.compiled)
        == TAIYIN_STATUS_OK
        && dynamic_chart.flow_stack.size() == 3u,
        "partial stack can be rebuilt from its truncation point", &failures);

    expect(make_decade_by_index(natal, 2023, 0u, &decade)
        == TAIYIN_ERROR_INVALID_ARGUMENT,
        "regular decade indices are one-based", &failures);
    expect(make_flow_month(
        natal, 2025, 1u, 14u, false, 4u, Branch::Zi, &month)
        == TAIYIN_ERROR_INVALID_ARGUMENT,
        "flow month rejects impossible sequence", &failures);
    expect(make_flow_day(natal, month, 31u, Stem::Jia, &day)
        == TAIYIN_STATUS_OK
        && day.limit.coordinate.branch == Branch::You,
        "Jie-based flow day accepts a 31st labeled day", &failures);
    expect(make_flow_day(natal, month, 33u, Stem::Jia, &day)
        == TAIYIN_STATUS_OK
        && day.limit.coordinate.branch == Branch::Hai,
        "Jie-based flow day accepts an exceptional 33rd labeled day",
        &failures);
    expect(make_flow_day(natal, month, 34u, Stem::Jia, &day)
        == TAIYIN_ERROR_INVALID_ARGUMENT,
        "flow day rejects a Jie-relative day beyond the compiled domain",
        &failures);

    if (failures != 0) {
        std::cerr << failures << " Ziwei limit checks failed\n";
        return 1;
    }
    std::cout << "Ziwei limit checks passed\n";
    return 0;
}
