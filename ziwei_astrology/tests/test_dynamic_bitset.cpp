#include "taiyin/ziwei/dynamic_bitset.h"

#include <cstddef>
#include <iostream>
#include <stdexcept>

namespace {

void expect(bool condition, const char* message, int* failures) {
    if (condition) return;
    std::cerr << "FAIL: " << message << '\n';
    ++*failures;
}

}  // namespace

int main() {
    using taiyin::ziwei::DynamicBitset;
    int failures = 0;

    {
        DynamicBitset bits;
        expect(bits.size() == 0u, "empty size", &failures);
        expect(bits.empty(), "empty reports empty", &failures);
        expect(bits.none(), "empty reports none", &failures);
        expect(!bits.any(), "empty reports not any", &failures);
        expect(bits.count() == 0u, "empty count", &failures);
        bits.reset();
        bits.clear();
        expect(bits == DynamicBitset(), "empty equality", &failures);
    }

    {
        DynamicBitset bits(130u);
        bits.set(0u).set(63u).set(64u).set(65u).set(129u);
        expect(bits.test(0u), "bit zero", &failures);
        expect(bits.test(63u), "bit 63", &failures);
        expect(bits.test(64u), "bit 64", &failures);
        expect(bits.test(65u), "bit 65", &failures);
        expect(bits.test(129u), "bit 129", &failures);
        expect(bits.count() == 5u, "boundary count", &failures);
        bits.reset(64u);
        expect(!bits.test(64u), "reset bit 64", &failures);
        expect(bits.count() == 4u, "count after reset", &failures);
        bits.reset();
        expect(bits.none(), "reset all", &failures);
    }

    {
        DynamicBitset bits(63u);
        bits.set(62u);
        bits.resize(65u, false);
        expect(bits.test(62u), "grow preserves old bit", &failures);
        expect(!bits.test(63u) && !bits.test(64u),
            "grow false clears new boundary bits", &failures);

        bits.resize(130u, true);
        expect(bits.test(62u), "grow true preserves old bit", &failures);
        expect(!bits.test(63u) && !bits.test(64u),
            "grow true preserves existing clear bits", &failures);
        expect(bits.test(65u) && bits.test(129u),
            "grow true sets the newly added range", &failures);
        expect(bits.count() == 66u, "grow true count", &failures);

        bits.resize(65u);
        expect(bits.count() == 1u, "shrink masks discarded bits", &failures);
        bits.reset(64u);
        bits.resize(130u, false);
        expect(bits.count() == 1u, "regrow does not reveal discarded tail", &failures);
        expect(!bits.test(64u) && !bits.test(65u) && !bits.test(129u),
            "regrown bits are clear", &failures);
    }

    {
        DynamicBitset all(65u, true);
        expect(all.count() == 65u, "constructor masks unused tail bits", &failures);
        all.resize(64u);
        expect(all.count() == 64u, "shrink on block boundary", &failures);
        all.resize(65u, false);
        expect(all.count() == 64u && !all.test(64u),
            "grow after block-boundary shrink", &failures);
    }

    {
        DynamicBitset lhs(65u);
        lhs.set(1u).set(64u);
        DynamicBitset rhs(65u);
        rhs.set(2u).set(64u);

        const DynamicBitset both = lhs & rhs;
        const DynamicBitset either = lhs | rhs;
        const DynamicBitset different = lhs ^ rhs;
        expect(both.count() == 1u && both.test(64u), "bitwise and", &failures);
        expect(either.count() == 3u && either.test(1u) && either.test(2u)
            && either.test(64u), "bitwise or", &failures);
        expect(different.count() == 2u && different.test(1u)
            && different.test(2u), "bitwise xor", &failures);
        expect(lhs == lhs && lhs != rhs, "equality", &failures);
    }

    {
        DynamicBitset bits(1u);
        bool threw = false;
        try {
            bits.set(1u);
        } catch (const std::out_of_range&) {
            threw = true;
        }
        expect(threw, "out-of-range set throws", &failures);

        threw = false;
        try {
            bits |= DynamicBitset(2u);
        } catch (const std::invalid_argument&) {
            threw = true;
        }
        expect(threw, "different-sized bitwise operation throws", &failures);

        bits.clear();
        expect(bits.empty() && bits.none(), "clear releases logical storage", &failures);
    }

    if (failures != 0) {
        std::cerr << failures << " DynamicBitset checks failed\n";
        return 1;
    }
    std::cout << "DynamicBitset checks passed\n";
    return 0;
}
