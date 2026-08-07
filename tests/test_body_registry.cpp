#include "taiyin/runtime/body_registry.h"

#include <iostream>

namespace {

using namespace taiyin;
using namespace taiyin::runtime;

Status test_fallback(
    EphemerisEngine*,
    const EphemerisRequest&,
    EphemerisResult*,
    EphemerisEvalDiagnostic*
) {
    return TAIYIN_STATUS_OK;
}

Status other_test_fallback(
    EphemerisEngine*,
    const EphemerisRequest&,
    EphemerisResult*,
    EphemerisEvalDiagnostic*
) {
    return TAIYIN_ERROR_UNSUPPORTED;
}

void expect_true(bool value, const char* label, int* failures) {
    if (!value) {
        std::cerr << "FAIL: expected true: " << label << "\n";
        ++(*failures);
    }
}

void expect_false(bool value, const char* label, int* failures) {
    if (value) {
        std::cerr << "FAIL: expected false: " << label << "\n";
        ++(*failures);
    }
}

void expect_size(size_t actual, size_t expected, const char* label, int* failures) {
    if (actual != expected) {
        std::cerr << "FAIL: " << label << ": actual=" << actual << " expected=" << expected << "\n";
        ++(*failures);
    }
}

void test_direct_and_fallback_share_entry(int* failures) {
    EphemerisBodyRegistry registry;
    EphemerisBodyRouteEntry entry;

    expect_true(registry.mark_direct(499), "mark direct", failures);
    expect_true(registry.find(499, &entry), "find direct", failures);
    expect_true(entry.has_direct, "entry has direct", failures);
    expect_true(entry.fallback == 0, "entry has no fallback", failures);
    expect_size(registry.size(), 1, "size after direct", failures);

    expect_true(registry.set_fallback(499, test_fallback), "set fallback", failures);
    expect_true(registry.find(499, &entry), "find direct+fallback", failures);
    expect_true(entry.has_direct, "entry keeps direct", failures);
    expect_true(entry.fallback == test_fallback, "entry fallback set", failures);
    expect_size(registry.size(), 1, "size after fallback", failures);

    expect_true(registry.set_fallback(499, other_test_fallback), "replace fallback", failures);
    expect_true(registry.find(499, &entry), "find replaced fallback", failures);
    expect_true(entry.fallback == other_test_fallback, "entry fallback replaced", failures);

    expect_true(registry.remove_fallback(499), "remove fallback", failures);
    expect_true(registry.find(499, &entry), "direct survives fallback removal", failures);
    expect_true(entry.has_direct, "direct still set", failures);
    expect_true(entry.fallback == 0, "fallback cleared", failures);

    expect_true(registry.unmark_direct(499), "unmark direct", failures);
    expect_false(registry.find(499, &entry), "entry removed when empty", failures);
    expect_size(registry.size(), 0, "size after removing empty entry", failures);
}

void test_invalid_and_clear(int* failures) {
    EphemerisBodyRegistry registry;
    EphemerisBodyRouteEntry entry;

    expect_false(registry.mark_direct(0), "zero direct rejected", failures);
    expect_false(registry.set_fallback(499, 0), "null fallback rejected", failures);
    expect_false(registry.find(499, 0), "null find rejected", failures);

    expect_true(registry.set_fallback(399, test_fallback), "set fallback-only", failures);
    expect_true(registry.find(399, &entry), "find fallback-only", failures);
    expect_false(entry.has_direct, "fallback-only no direct", failures);
    expect_true(entry.fallback == test_fallback, "fallback-only function", failures);
    expect_true(registry.remove_fallback(399), "remove fallback-only", failures);
    expect_false(registry.find(399, &entry), "fallback-only removed", failures);

    expect_true(registry.mark_direct(1), "mark one", failures);
    expect_true(registry.mark_direct(2), "mark two", failures);
    registry.clear();
    expect_size(registry.size(), 0, "size after clear", failures);
}

}  // namespace

int main() {
    int failures = 0;
    test_direct_and_fallback_share_entry(&failures);
    test_invalid_and_clear(&failures);

    if (failures == 0) {
        std::cout << "test_body_registry: ALL TESTS PASSED\n";
        return 0;
    }
    std::cerr << failures << " test_body_registry failure(s)\n";
    return 1;
}
