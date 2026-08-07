#include "taiyin/field_set.h"

#include <iostream>

namespace {

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

void expect_status(taiyin::Status actual, taiyin::Status expected, const char* label, int* failures) {
    if (actual != expected) {
        std::cerr << "FAIL: " << label << ": actual=" << actual << " expected=" << expected << "\n";
        ++(*failures);
    }
}

void expect_size(size_t actual, size_t expected, const char* label, int* failures) {
    if (actual != expected) {
        std::cerr << "FAIL: " << label << ": actual=" << actual << " expected=" << expected << "\n";
        ++(*failures);
    }
}

void test_empty_and_set(int* failures) {
    taiyin::FieldSet fields;
    expect_true(fields.empty(), "new set is empty", failures);
    expect_size(fields.byte_size(), 0, "new set has no storage", failures);
    expect_false(fields.has(0), "new set lacks field zero", failures);

    expect_status(fields.set(0), taiyin::TAIYIN_STATUS_OK, "set field zero", failures);
    expect_true(fields.has(0), "has field zero", failures);
    expect_false(fields.empty(), "set is no longer empty", failures);
    expect_size(fields.byte_size(), 1, "field zero uses one byte", failures);

    expect_status(fields.set(9), taiyin::TAIYIN_STATUS_OK, "set field nine", failures);
    expect_true(fields.has(9), "has field nine", failures);
    expect_false(fields.has(8), "does not have neighboring field", failures);
    expect_size(fields.byte_size(), 2, "field nine expands to two bytes", failures);
}

void test_dynamic_growth_and_clear(int* failures) {
    taiyin::FieldSet fields;
    expect_status(fields.set(1024), taiyin::TAIYIN_STATUS_OK, "set distant field", failures);
    expect_true(fields.has(1024), "has distant field", failures);
    expect_size(fields.byte_size(), 129, "distant field expands storage", failures);

    fields.clear(1024);
    expect_false(fields.has(1024), "cleared distant field", failures);
    expect_true(fields.empty(), "clear trims trailing storage", failures);
    expect_size(fields.byte_size(), 0, "clear trims byte storage", failures);

    expect_status(fields.set(7), taiyin::TAIYIN_STATUS_OK, "set high bit in first byte", failures);
    expect_status(fields.set(15), taiyin::TAIYIN_STATUS_OK, "set high bit in second byte", failures);
    fields.clear(7);
    expect_false(fields.has(7), "cleared first-byte high bit", failures);
    expect_true(fields.has(15), "second-byte high bit remains", failures);
    fields.clear_all();
    expect_true(fields.empty(), "clear_all empties set", failures);
}

void test_contains_and_missing(int* failures) {
    taiyin::FieldSet valid;
    taiyin::FieldSet required;
    expect_status(valid.set(1), taiyin::TAIYIN_STATUS_OK, "valid set 1", failures);
    expect_status(valid.set(5), taiyin::TAIYIN_STATUS_OK, "valid set 5", failures);
    expect_status(required.set(1), taiyin::TAIYIN_STATUS_OK, "required set 1", failures);
    expect_status(required.set(5), taiyin::TAIYIN_STATUS_OK, "required set 5", failures);

    expect_true(valid.contains(required), "valid contains required", failures);
    expect_true(valid.missing(required).empty(), "no missing fields", failures);

    expect_status(required.set(130), taiyin::TAIYIN_STATUS_OK, "required set 130", failures);
    expect_false(valid.contains(required), "valid does not contain distant required", failures);
    taiyin::FieldSet missing = valid.missing(required);
    expect_true(missing.has(130), "missing contains field 130", failures);
    expect_false(missing.has(1), "missing does not contain present field 1", failures);
    expect_false(missing.has(5), "missing does not contain present field 5", failures);

    size_t first = 0;
    expect_true(missing.first_set(&first), "missing has first field", failures);
    expect_size(first, 130, "first missing field", failures);
}

void test_operators(int* failures) {
    taiyin::FieldSet a;
    taiyin::FieldSet b;
    expect_status(a.set(2), taiyin::TAIYIN_STATUS_OK, "a set 2", failures);
    expect_status(a.set(20), taiyin::TAIYIN_STATUS_OK, "a set 20", failures);
    expect_status(b.set(20), taiyin::TAIYIN_STATUS_OK, "b set 20", failures);
    expect_status(b.set(200), taiyin::TAIYIN_STATUS_OK, "b set 200", failures);

    taiyin::FieldSet combined = a | b;
    expect_true(combined.has(2), "or has a-only field", failures);
    expect_true(combined.has(20), "or has shared field", failures);
    expect_true(combined.has(200), "or has b-only field", failures);

    taiyin::FieldSet common = a & b;
    expect_false(common.has(2), "and omits a-only field", failures);
    expect_true(common.has(20), "and keeps shared field", failures);
    expect_false(common.has(200), "and omits b-only field", failures);

    taiyin::FieldSet same_as_a;
    expect_status(same_as_a.set(2), taiyin::TAIYIN_STATUS_OK, "same set 2", failures);
    expect_status(same_as_a.set(20), taiyin::TAIYIN_STATUS_OK, "same set 20", failures);
    expect_true(a == same_as_a, "equal sets compare equal", failures);
    expect_true(a != b, "different sets compare unequal", failures);
}

void test_first_set(int* failures) {
    taiyin::FieldSet fields;
    size_t first = 999;
    expect_false(fields.first_set(&first), "empty set has no first", failures);
    expect_size(first, 999, "first unchanged on empty", failures);
    expect_false(fields.first_set(0), "null first output fails", failures);

    expect_status(fields.set(73), taiyin::TAIYIN_STATUS_OK, "set 73", failures);
    expect_status(fields.set(3), taiyin::TAIYIN_STATUS_OK, "set 3", failures);
    expect_true(fields.first_set(&first), "non-empty set has first", failures);
    expect_size(first, 3, "first set field is lowest field", failures);
}

}  // namespace

int main() {
    int failures = 0;

    test_empty_and_set(&failures);
    test_dynamic_growth_and_clear(&failures);
    test_contains_and_missing(&failures);
    test_operators(&failures);
    test_first_set(&failures);

    if (failures != 0) {
        std::cerr << failures << " field set test(s) failed\n";
        return 1;
    }

    std::cout << "All field set tests passed.\n";
    return 0;
}
