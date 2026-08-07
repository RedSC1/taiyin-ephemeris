#include "taiyin/internal/ephemeris_route_rule.h"

#include <cassert>
#include <cstdio>
#include <vector>

namespace {

void expect_true(bool value, const char* label) {
    if (!value) {
        std::fprintf(stderr, "expected true: %s\n", label);
        assert(false);
    }
}

void expect_size(size_t actual, size_t expected, const char* label) {
    if (actual != expected) {
        std::fprintf(stderr, "expected size %zu got %zu: %s\n", expected, actual, label);
        assert(false);
    }
}

void expect_int(int actual, int expected, const char* label) {
    if (actual != expected) {
        std::fprintf(stderr, "expected int %d got %d: %s\n", expected, actual, label);
        assert(false);
    }
}

}  // namespace

int main() {
    taiyin::internal::EphemerisRouteRuleTable table;
    const std::vector<taiyin::internal::EphemerisRouteRule>* rules = 0;

    expect_true(table.upsert_source_method(1, 100, 300, "first"), "insert first");
    expect_true(table.upsert_source_method(2, 200, 200, "second"), "insert second");
    expect_true(table.upsert_source_method(3, 300, 100, "third"), "insert third");
    rules = &table.rules();
    expect_size(rules->size(), 3, "rule count");
    expect_int((*rules)[0].source_id, 1, "first source wins");
    expect_int((*rules)[0].method_id, 100, "first method wins");
    expect_int((*rules)[1].method_id, 200, "second method wins");
    expect_int((*rules)[2].method_id, 300, "third method wins");

    expect_true(table.upsert_source_method(0, 900, 1000, "custom high priority"), "insert custom high priority");
    rules = &table.rules();
    expect_size(rules->size(), 4, "custom method count");
    expect_int((*rules)[0].source_id, 0, "custom any-source rule wins");
    expect_int((*rules)[0].method_id, 900, "custom priority wins");
    expect_int((*rules)[1].method_id, 100, "old first shifts down");

    expect_true(table.upsert_source_method(3, 300, 1200, "promote existing"), "promote existing method");
    rules = &table.rules();
    expect_int((*rules)[0].method_id, 300, "promoted method wins");
    expect_int((*rules)[1].method_id, 900, "custom high priority second");

    expect_true(table.upsert_source_method(0, 901, 1000, "same priority after 900"), "insert same priority");
    rules = &table.rules();
    expect_int((*rules)[1].method_id, 900, "same priority keeps old order first");
    expect_int((*rules)[2].method_id, 901, "same priority keeps new order second");

    expect_int((*rules)[0].method_id, 300, "rule first method");
    expect_int((*rules)[0].priority, 1200, "rule first priority");

    expect_true(table.upsert_source_method(42, 300, 1300, "same method different source"), "insert source-specific method");
    rules = &table.rules();
    expect_int((*rules)[0].source_id, 42, "source-specific rule wins");
    expect_int((*rules)[0].method_id, 300, "source-specific method id");
    expect_true(table.upsert_source_method(42, 300, 900, "demote same route"), "update source-specific method");
    rules = &table.rules();
    expect_int((*rules)[0].method_id, 300, "original promoted route restored");
    expect_int((*rules)[0].source_id, 3, "original promoted source restored");

    return 0;
}
