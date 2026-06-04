#include "taiyin/runtime/runtime_registry.h"

#include <iostream>

namespace {

using namespace taiyin::runtime;

const uint32_t METHOD_CATEGORY_HOUSES = 1;
const uint32_t METHOD_CATEGORY_BAZI = 2;

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

void expect_u32(uint32_t actual, uint32_t expected, const char* label, int* failures) {
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

bool test_method_fn(const void* request, void* out) {
    if (!request || !out) {
        return false;
    }
    const int* lhs = static_cast<const int*>(request);
    int* rhs = static_cast<int*>(out);
    *rhs = *lhs + 7;
    return true;
}

void test_handle_types(int* failures) {
    MethodId invalid_method;
    ServiceId invalid_service;
    expect_false(invalid_method.is_valid(), "default MethodId invalid", failures);
    expect_false(invalid_service.is_valid(), "default ServiceId invalid", failures);
}



void test_method_registry(int* failures) {
    RuntimeRegistry registry;
    MethodId placidus = registry.register_method(METHOD_CATEGORY_HOUSES, "placidus", &test_method_fn);
    MethodId bazi_default = registry.register_method(METHOD_CATEGORY_BAZI, "default", &test_method_fn);
    MethodId duplicate = registry.register_method(METHOD_CATEGORY_HOUSES, "placidus", &test_method_fn);
    MethodId same_name_other_category = registry.register_method(METHOD_CATEGORY_BAZI, "placidus", &test_method_fn);

    expect_true(placidus.is_valid(), "placidus method id valid", failures);
    expect_true(bazi_default.is_valid(), "bazi method id valid", failures);
    expect_true(same_name_other_category.is_valid(), "same method name in other category valid", failures);
    expect_u32(placidus.value, 1, "first method id", failures);
    expect_u32(bazi_default.value, 2, "second method id", failures);
    expect_u32(duplicate.value, placidus.value, "duplicate method returns same id", failures);
    expect_u32(same_name_other_category.value, 3, "method category separates names", failures);
    expect_size(registry.method_count(), 3, "method count", failures);

    MethodId resolved;
    expect_true(registry.resolve_method(METHOD_CATEGORY_HOUSES, "placidus", &resolved), "resolve method", failures);
    expect_u32(resolved.value, placidus.value, "resolved method id", failures);
    expect_false(registry.resolve_method(METHOD_CATEGORY_HOUSES, "default", &resolved), "missing method in category fails", failures);
    expect_false(resolved.is_valid(), "missing method clears output", failures);

    const MethodDescriptor* descriptor = registry.method(placidus);
    expect_true(descriptor != 0, "method descriptor exists", failures);
    if (descriptor) {
        expect_true(descriptor->name == "placidus", "method descriptor name", failures);
        expect_u32(descriptor->category, METHOD_CATEGORY_HOUSES, "method descriptor category", failures);
        int request = 5;
        int result = 0;
        expect_true(descriptor->execute(&request, &result), "method function executes", failures);
        expect_u32(static_cast<uint32_t>(result), 12, "method function result", failures);
    }

    expect_false(registry.register_method(METHOD_CATEGORY_HOUSES, "bad", 0).is_valid(), "null method function rejected", failures);
    expect_false(registry.register_method(METHOD_CATEGORY_HOUSES, "", &test_method_fn).is_valid(), "empty method name rejected", failures);
    expect_true(registry.method(MethodId()) == 0, "invalid method lookup fails", failures);
}

void test_clear_and_default_registry(int* failures) {
    RuntimeRegistry registry;
    registry.register_method(METHOD_CATEGORY_HOUSES, "equal", &test_method_fn);
    registry.clear();
    expect_size(registry.method_count(), 0, "clear methods", failures);

    RuntimeRegistry& global = default_runtime_registry();
    global.clear();
    MethodId global_method = global.register_method(METHOD_CATEGORY_HOUSES, "global_equal", &test_method_fn);
    expect_true(global_method.is_valid(), "default runtime registry registers method", failures);
    MethodId resolved;
    expect_true(global.resolve_method(METHOD_CATEGORY_HOUSES, "global_equal", &resolved), "default runtime registry resolves method", failures);
    expect_u32(resolved.value, global_method.value, "default runtime registry resolved id", failures);
    global.clear();
}

}  // namespace

int main() {
    int failures = 0;
    test_handle_types(&failures);
    test_method_registry(&failures);
    test_clear_and_default_registry(&failures);

    if (failures == 0) {
        std::cout << "test_runtime_registry: ALL TESTS PASSED\n";
        return 0;
    }
    std::cerr << failures << " test_runtime_registry failure(s)\n";
    return 1;
}
