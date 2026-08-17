#include "taiyin/ziwei/anchors.h"
#include "taiyin/ziwei/types.h"

#include <array>
#include <cstddef>
#include <iostream>

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

taiyin::ziwei::Pillars sample_solar_pillars() {
    return taiyin::ziwei::Pillars{
        ganzhi(0u, 0u),
        ganzhi(2u, 2u),
        ganzhi(4u, 4u),
        ganzhi(6u, 6u),
    };
}

taiyin::ziwei::Pillars sample_lunar_pillars() {
    return taiyin::ziwei::Pillars{
        ganzhi(1u, 1u),
        ganzhi(3u, 3u),
        ganzhi(5u, 5u),
        ganzhi(7u, 7u),
    };
}

taiyin::ziwei::Anchors sample_anchors() {
    taiyin::ziwei::Anchors result = {};
    result.solar_term = sample_solar_pillars();
    result.lunar = sample_lunar_pillars();
    result.bureau = taiyin::ziwei::Bureau::Wood3;
    result.ziwei = taiyin::ziwei::Branch::Shen;
    result.tianfu = taiyin::ziwei::Branch::Chen;
    for (std::size_t i = 0u; i < result.palace_positions.size(); ++i) {
        result.palace_positions[i] = static_cast<taiyin::ziwei::Branch>(i);
    }
    return result;
}

}  // namespace

int main() {
    using namespace taiyin::ziwei;
    int failures = 0;

    static_assert(kStemCount == 10u, "stable stem count");
    static_assert(kBranchCount == 12u, "stable branch count");
    static_assert(kPalaceCount == 12u, "stable palace count");
    static_assert(kAnchorCount == 31u, "stable anchor count");
    static_assert(static_cast<uint8_t>(AnchorSlot::PalaceParents) == 30u,
        "stable final anchor slot");

    expect(to_index(Stem::Jia) == 0u && to_index(Stem::Gui) == 9u,
        "stem encoding", &failures);
    expect(to_index(Branch::Zi) == 0u && to_index(Branch::Hai) == 11u,
        "branch encoding", &failures);
    expect(to_index(Gender::Male) == 0u && to_index(Gender::Female) == 1u,
        "gender encoding", &failures);

    for (uint8_t stem = 0u; stem < kStemCount; ++stem) {
        const Stem value = static_cast<Stem>(stem);
        expect(is_forward(value, Gender::Male) == ((stem & 1u) == 0u),
            "male direction over all stems", &failures);
        expect(is_forward(value, Gender::Female) == ((stem & 1u) == 1u),
            "female direction over all stems", &failures);
    }

    for (uint8_t branch = 0u; branch < kBranchCount; ++branch) {
        const Branch value = static_cast<Branch>(branch);
        expect(advance_branch(value, 12) == value,
            "full-cycle branch advance", &failures);
        expect(advance_branch(advance_branch(value, 6), 6) == value,
            "two half-cycle advances", &failures);
        expect(advance_branch(advance_branch(value, -1), 1) == value,
            "negative branch advance", &failures);
    }

    expect(bureau_number(Bureau::Water2) == 2u, "water bureau number", &failures);
    expect(bureau_number(Bureau::Wood3) == 3u, "wood bureau number", &failures);
    expect(bureau_number(Bureau::Metal4) == 4u, "metal bureau number", &failures);
    expect(bureau_number(Bureau::Earth5) == 5u, "earth bureau number", &failures);
    expect(bureau_number(Bureau::Fire6) == 6u, "fire bureau number", &failures);

    expect(is_valid(ganzhi(0u, 0u)), "valid Jia-Zi", &failures);
    expect(!is_valid(ganzhi(0u, 1u)), "reject parity-invalid Ganzhi", &failures);
    expect(is_valid(sample_solar_pillars()), "valid solar pillars", &failures);
    expect(is_valid(sample_lunar_pillars()), "valid lunar pillars", &failures);

    Anchors anchors = sample_anchors();
    expect(validate_anchors(anchors), "valid 31 anchors", &failures);
    const std::array<uint8_t, kAnchorCount> flat = flatten_anchors(anchors);
    expect(flat[static_cast<std::size_t>(AnchorSlot::SolarYearStem)] == 0u,
        "flatten solar year stem", &failures);
    expect(flat[static_cast<std::size_t>(AnchorSlot::LunarYearStem)] == 1u,
        "flatten lunar year stem remains independent", &failures);
    expect(flat[static_cast<std::size_t>(AnchorSlot::Bureau)] == 1u,
        "flatten bureau enum index", &failures);
    expect(flat[static_cast<std::size_t>(AnchorSlot::Ziwei)] == 8u,
        "flatten Ziwei position", &failures);
    expect(flat[static_cast<std::size_t>(AnchorSlot::PalaceParents)] == 11u,
        "flatten final palace", &failures);

    anchors.palace_positions[11] = Branch::Zi;
    expect(!validate_anchors(anchors), "reject duplicate palace positions", &failures);
    anchors = sample_anchors();
    anchors.solar_term.year = ganzhi(0u, 1u);
    expect(!validate_anchors(anchors), "reject invalid pillar in anchors", &failures);

    expect(!is_valid(static_cast<Stem>(10u)), "invalid stem range", &failures);
    expect(!is_valid(static_cast<Branch>(12u)), "invalid branch range", &failures);
    expect(!is_valid(static_cast<Bureau>(5u)), "invalid bureau range", &failures);

    if (failures != 0) {
        std::cerr << failures << " Ziwei type/anchor checks failed\n";
        return 1;
    }
    std::cout << "Ziwei type/anchor checks passed\n";
    return 0;
}
