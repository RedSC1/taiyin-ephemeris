#include "taiyin/physical_constants.h"
#include "taiyin/internal/ephemeris_catalog.h"
#include "taiyin/internal/kepler_source_registry.h"
#include "taiyin/runtime/ephemeris_service.h"

#include <cmath>
#include <iostream>

namespace {

const double JD0 = taiyin::JD_J2000;

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

void expect_near(double actual, double expected, double tolerance, const char* label, int* failures) {
    if (std::fabs(actual - expected) > tolerance) {
        std::cerr << "FAIL: " << label << ": actual=" << actual << " expected=" << expected
                  << " tolerance=" << tolerance << "\n";
        ++(*failures);
    }
}

taiyin::internal::KeplerElements make_element(int target_id, double a_au) {
    taiyin::internal::KeplerElements element;
    taiyin::internal::make_elliptic_kepler_elements(
        target_id,
        10,
        JD0 - 1000.0,
        JD0 + 1000.0,
        JD0,
        taiyin::TAIYIN_SOLAR_MU_AU3_DAY2,
        a_au,
        0.2,
        0.1,
        0.2,
        0.3,
        0.4,
        &element);
    return element;
}

void test_memory_source_reload_after_eviction(int* failures) {
    using namespace taiyin::internal;
    using namespace taiyin::runtime;

    clear_memory_kepler_sources();

    KeplerElements first_element = make_element(2000101, 2.0);
    KeplerElements second_element = make_element(2000102, 3.0);
    uint64_t first_id = 0;
    uint64_t second_id = 0;
    expect_true(
        register_memory_kepler_source(
            &first_element,
            1,
            TAIYIN_KEPLER_MEMORY_METHOD_ID,
            EphemerisFrame::IcrfJ2000Equatorial,
            JD0 - 500.0,
            JD0 + 500.0,
            &first_id),
        "register first memory Kepler source",
        failures);
    expect_true(
        register_memory_kepler_source(
            &second_element,
            1,
            TAIYIN_KEPLER_MEMORY_METHOD_ID,
            EphemerisFrame::IcrfJ2000Equatorial,
            JD0 - 500.0,
            JD0 + 500.0,
            &second_id),
        "register second memory Kepler source",
        failures);

    EphemerisBlockDescriptor first_descriptor;
    EphemerisBlockDescriptor second_descriptor;
    expect_true(make_memory_kepler_descriptor(first_id, &first_descriptor), "make first descriptor", failures);
    expect_true(make_memory_kepler_descriptor(second_id, &second_descriptor), "make second descriptor", failures);
    expect_true(first_descriptor.path.empty(), "memory descriptor has no path", failures);
    expect_true(first_descriptor.source_key.source_id == TAIYIN_KEPLER_MEMORY_SOURCE_ID, "memory descriptor source id", failures);

    EphemerisBlockCatalog catalog;
    expect_true(catalog.add(first_descriptor), "add first descriptor", failures);
    expect_true(catalog.add(second_descriptor), "add second descriptor", failures);

    EphemerisBlockCache cache(1);
    EphemerisService service;
    service.set_catalog(&catalog);
    service.set_cache(&cache);

    EphemerisRequest request;
    request.target_id = 2000101;
    request.center_id = 10;
    request.frame = EphemerisFrame::IcrfJ2000Equatorial;
    request.jd_tdb = JD0 + 10.0;

    EphemerisResult first_result;
    expect_true(taiyin::taiyin_status_ok(service.eval_state(request, &first_result, 0)), "first memory source eval", failures);
    expect_false(first_result.cache_hit, "first eval loads from memory source", failures);
    expect_true(cache.entry_count() == 1, "first eval inserts one oversized entry", failures);

    EphemerisResult first_hit;
    expect_true(taiyin::taiyin_status_ok(service.eval_state(request, &first_hit, 0)), "first memory source cache hit", failures);
    expect_true(first_hit.cache_hit, "second first eval hits cache", failures);
    expect_near(first_hit.state.position_au.x, first_result.state.position_au.x, 0.0, "cached state stable", failures);

    EphemerisRequest second_request = request;
    second_request.target_id = 2000102;
    EphemerisResult second_result;
    expect_true(taiyin::taiyin_status_ok(service.eval_state(second_request, &second_result, 0)), "second memory source eval evicts first", failures);
    expect_false(second_result.cache_hit, "second source loads from memory source", failures);
    expect_true(cache.entry_count() == 1, "oversized second keeps one entry", failures);

    EphemerisResult reloaded_first;
    expect_true(taiyin::taiyin_status_ok(service.eval_state(request, &reloaded_first, 0)), "first source reloads after eviction", failures);
    expect_false(reloaded_first.cache_hit, "reloaded first reports cache miss", failures);
    expect_near(reloaded_first.state.position_au.x, first_result.state.position_au.x, 0.0, "reloaded state stable", failures);

    expect_true(unregister_memory_kepler_source(first_id), "unregister first source", failures);
    cache.clear();
    EphemerisResult missing_result;
    expect_false(taiyin::taiyin_status_ok(service.eval_state(request, &missing_result, 0)), "unregistered source cannot reload", failures);

    clear_memory_kepler_sources();
}

}  // namespace

int main() {
    int failures = 0;
    test_memory_source_reload_after_eviction(&failures);

    if (failures == 0) {
        std::cout << "test_kepler_source_registry: ALL TESTS PASSED\n";
        return 0;
    }
    std::cerr << failures << " test_kepler_source_registry failure(s)\n";
    return 1;
}
