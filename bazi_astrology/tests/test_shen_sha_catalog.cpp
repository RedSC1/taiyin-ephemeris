#include "taiyin/bazi/shen_sha_catalog.h"
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <future>

using namespace taiyin::bazi;
static void check_at(bool value, int line) { if (!value) { std::cerr << "check failed at " << line << "\n"; std::exit(1); } }
#define check(value) check_at((value), __LINE__)
template<class F> static void rejects(F f) {
    try { f(); } catch (const std::invalid_argument&) { return; }
    check(false);
}
int main() {
    BaziChart chart;
    chart.pillars.year = 0x00;
    chart.pillars.month = 0x22;
    chart.pillars.day = 0x42;
    chart.pillars.hour = 0x31;
    const auto yes = [](const BaziShenShaInput&) { return true; };
    BaziShenShaCatalog base;
    auto defaults = base.create_context();
    const BaziShenShaModule module("my-school", {
        {"one", "First", yes}, {"two", "Second", yes}});
    auto extended = base.add_module(module);
    auto old = extended.create_context();
    auto removed = extended.remove_module("my-school");
    check(removed.modules().empty() && base.modules().empty());
    rejects([&] { base.remove_module("option1"); });
    rejects([&] { base.remove_module("builtin"); });
    rejects([&] { base.remove_module("missing"); });
    rejects([&] { extended.add_module(module); });
    rejects([&] { BaziShenShaModule bad("builtin", {{"a", "A", yes}}); });
    rejects([&] { BaziShenShaModule bad("my-school", {{"a", "A", yes}, {"a", "B", yes}}); });
    rejects([&] { BaziShenShaModule bad("x:y", {{"a", "A", yes}}); });
    rejects([&] { BaziShenShaModule bad("x", {{"a", "A", {}}}); });
    rejects([&] { BaziShenShaModule bad("x", {{"a", "  ", yes}}); });
    BaziShenShaSelection selection;
    selection.disabled_ids = {"builtin:66"};
    rejects([&] { base.create_context(selection); });
    selection.disabled_ids = {"builtin:0", "builtin:0"};
    rejects([&] { base.create_context(selection); });

    // All target kinds and gender modes must preserve the original bitset.
    for (int gender = -1; gender <= 1; ++gender) {
        for (int kind = 0; kind <= 12; ++kind) {
            uint64_t words[kBaziShenShaWordCount] = {};
            std::size_t count = 0;
            const auto status = gender == -1
                ? collect_target_shen_sha(&chart, 0x51, kind, words, kBaziShenShaWordCount, &count)
                : collect_target_shen_sha_with_gender(&chart, 0x51, kind, gender, words, kBaziShenShaWordCount, &count);
            check(status == taiyin::TAIYIN_STATUS_OK);
            uint64_t actual[kBaziShenShaWordCount] = {};
            const auto matches = defaults.evaluate(chart, 0x51, kind, gender);
            for (const auto& match : matches) {
                check(match.builtin_id >= 0);
                actual[match.builtin_id / 64] |= uint64_t(1) << (match.builtin_id % 64);
            }
            for (std::size_t i = 0; i < count; ++i) check(actual[i] == words[i]);
            check(old.evaluate(chart, 0x51, kind, gender).size() == matches.size() + 2);
            check(removed.create_context().evaluate(chart, 0x51, kind, gender).size() == matches.size());
        }
    }
    // Unknown-hour neutral charts retain exactly the original built-in bits.
    BaziChart hourless = chart;
    hourless.pillars.hour = 0xff;
    int callback_count = 0;
    auto hourless_custom = base.add_module(BaziShenShaModule("hourless", {
        {"marker", "Unknown hour", [&](const BaziShenShaInput& input) {
            ++callback_count;
            return input.chart.pillars.hour == 0xff && input.gender == -1;
        }}})).create_context();
    for (int kind = 0; kind <= 12; ++kind) {
        uint64_t expected[kBaziShenShaWordCount] = {};
        uint64_t actual[kBaziShenShaWordCount] = {};
        std::size_t count = 0;
        check(collect_target_shen_sha(&hourless, 0x51, kind, expected,
            kBaziShenShaWordCount, &count) == taiyin::TAIYIN_STATUS_OK);
        const auto neutral = defaults.evaluate(hourless, 0x51, kind);
        for (const auto& match : neutral)
            actual[match.builtin_id / 64] |= uint64_t(1) << (match.builtin_id % 64);
        for (std::size_t i = 0; i < count; ++i) check(actual[i] == expected[i]);
        const auto custom = hourless_custom.evaluate(hourless, 0x51, kind);
        check(custom.size() == neutral.size() + 1);
        check(custom.back().id == "hourless:marker");
        const int before = callback_count;
        for (int gender = 0; gender <= 1; ++gender) {
            check(collect_target_shen_sha_with_gender(&hourless, 0x51, kind,
                gender, expected, kBaziShenShaWordCount, &count) == taiyin::TAIYIN_ERROR_INVALID_ARGUMENT);
            rejects([&] { hourless_custom.evaluate(hourless, 0x51, kind, gender); });
        }
        check(callback_count == before); // Invalid inputs never invoke callbacks.
    }
    rejects([&] { defaults.evaluate(hourless, 0xff, 2); }); // target still required
    for (int pillar = 0; pillar < 3; ++pillar) {
        BaziChart invalid = hourless;
        if (pillar == 0) invalid.pillars.year = 0xff;
        if (pillar == 1) invalid.pillars.month = 0xff;
        if (pillar == 2) invalid.pillars.day = 0xff;
        rejects([&] { hourless_custom.evaluate(invalid, 0x51, 2); });
    }
    // Disable definitions without modifying them; retain only one custom rule.
    selection.disabled_ids.clear();
    for (std::size_t i = 0; i < kBaziShenShaStableIdCount; ++i)
        selection.disabled_ids.push_back("builtin:" + std::to_string(i));
    selection.disabled_ids.push_back("my-school:one");
    auto selected = extended.create_context(selection);
    selection.disabled_ids.clear();
    auto result = selected.evaluate(chart, 0x51, 2);
    check(result.size() == 1 && result[0].id == "my-school:two" && result[0].builtin_id == -1);
    check(selected.evaluate(hourless, 0x51, 2).size() == 1);
    rejects([&] { selected.evaluate(hourless, 0x51, 2, BaziGenderMale); });
    rejects([&] { selected.evaluate(chart, 0xff, 2); });
    rejects([&] { selected.evaluate(chart, 0x51, 13); });
    rejects([&] { selected.evaluate(chart, 0x51, 2, 99); });
    auto throwing = base.add_module(BaziShenShaModule("fail", {{"x", "X",
        [](const BaziShenShaInput&) -> bool { throw std::runtime_error("callback"); }}})).create_context();
    bool propagated = false;
    try { throwing.evaluate(chart, 0x51, 2); }
    catch (const std::runtime_error&) { propagated = true; }
    check(propagated);
    // Immutable snapshots outlive catalogs; pure callbacks permit parallel use.
    auto future = std::async(std::launch::async, [&] { return selected.evaluate(chart, 0x51, 2).size(); });
    check(selected.evaluate(chart, 0x51, 2).size() == future.get());
    std::cout << "Shen Sha catalog tests passed\n";
}
