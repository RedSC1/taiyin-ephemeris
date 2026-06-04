#include "taiyin/angle.h"
#include "taiyin/physical_constants.h"
#include "taiyin/runtime/ephemeris_service.h"
#include "taiyin/runtime/service_locator.h"
#include "taiyin/runtime/taiyin_runtime.h"

#include <cmath>
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

namespace {

using namespace taiyin;
using namespace taiyin::internal;
using namespace taiyin::runtime;

struct TestEphemerisData {
    double base;
};

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

void expect_near(double actual, double expected, const char* label, int* failures) {
    if (std::fabs(actual - expected) > 1.0e-12) {
        std::cerr << "FAIL: " << label << ": actual=" << actual << " expected=" << expected << "\n";
        ++(*failures);
    }
}

void expect_near_tol(double actual, double expected, double tolerance, const char* label, int* failures) {
    if (std::fabs(actual - expected) > tolerance) {
        std::cerr << "FAIL: " << label << ": actual=" << actual << " expected=" << expected
                  << " tolerance=" << tolerance << "\n";
        ++(*failures);
    }
}

bool test_runtime_method(const void*, void*) {
    return true;
}

void push_u8(std::vector<uint8_t>* data, uint8_t value) {
    data->push_back(value);
}

void push_u16_le(std::vector<uint8_t>* data, uint16_t value) {
    data->push_back(static_cast<uint8_t>(value & 0xFF));
    data->push_back(static_cast<uint8_t>((value >> 8) & 0xFF));
}

void push_u32_le(std::vector<uint8_t>* data, uint32_t value) {
    data->push_back(static_cast<uint8_t>(value & 0xFF));
    data->push_back(static_cast<uint8_t>((value >> 8) & 0xFF));
    data->push_back(static_cast<uint8_t>((value >> 16) & 0xFF));
    data->push_back(static_cast<uint8_t>((value >> 24) & 0xFF));
}

void push_i32_le(std::vector<uint8_t>* data, int32_t value) {
    push_u32_le(data, static_cast<uint32_t>(value));
}

void push_u64_le(std::vector<uint8_t>* data, uint64_t value) {
    for (int i = 0; i < 8; ++i) {
        data->push_back(static_cast<uint8_t>((value >> (i * 8)) & 0xFF));
    }
}

void push_f64_le(std::vector<uint8_t>* data, double value) {
    uint8_t bytes[8];
    std::memcpy(bytes, &value, sizeof(bytes));
    for (int i = 0; i < 8; ++i) {
        data->push_back(bytes[i]);
    }
}

void push_i8_coeffs(std::vector<uint8_t>* data, const int8_t* values, int count) {
    const int width_byte_count = (count + 3) / 4;
    for (int i = 0; i < width_byte_count; ++i) {
        data->push_back(0);
    }
    for (int i = 0; i < count; ++i) {
        data->push_back(static_cast<uint8_t>(values[i]));
    }
}

std::vector<uint8_t> make_test_opm4_file_bytes(int target_id = 201, int center_id = 10, int method_id = 1) {
    std::vector<uint8_t> payload;
    push_f64_le(&payload, 100.0);
    push_f64_le(&payload, 110.0);
    push_f64_le(&payload, 0.0);
    push_f64_le(&payload, taiyin::TAIYIN_PI / 2.0);
    push_f64_le(&payload, 0.0);
    const int8_t cx[] = { 1, 2 };
    const int8_t cy[] = { 3, 4 };
    const int8_t cz[] = { 5 };
    push_i8_coeffs(&payload, cx, 2);
    push_i8_coeffs(&payload, cy, 2);
    push_i8_coeffs(&payload, cz, 1);

    std::vector<uint8_t> data;
    push_u8(&data, 'O');
    push_u8(&data, 'P');
    push_u8(&data, 'M');
    push_u8(&data, '4');
    push_u8(&data, 1);
    push_u8(&data, 0);
    push_u16_le(&data, 128);
    push_u64_le(&data, 128);
    push_u64_le(&data, static_cast<uint64_t>(payload.size()));
    push_i32_le(&data, target_id);
    push_i32_le(&data, center_id);
    push_i32_le(&data, method_id);
    push_i32_le(&data, 1);
    push_f64_le(&data, 100.0);
    push_f64_le(&data, 110.0);
    push_u32_le(&data, 1);
    push_u8(&data, 1);
    push_u8(&data, 0);
    push_u8(&data, 1);
    push_u8(&data, 1);
    push_u8(&data, 1);
    for (int i = 0; i < 7; ++i) {
        push_u8(&data, 0);
    }
    push_f64_le(&data, 0.005);
    while (data.size() < 128) {
        push_u8(&data, 0);
    }
    data.insert(data.end(), payload.begin(), payload.end());
    return data;
}

bool write_test_opm4_file_for(const char* path, int target_id, int center_id, int method_id) {
    const std::vector<uint8_t> bytes = make_test_opm4_file_bytes(target_id, center_id, method_id);
    std::ofstream file(path, std::ios::binary);
    file.write(reinterpret_cast<const char*>(&bytes[0]), static_cast<std::streamsize>(bytes.size()));
    return static_cast<bool>(file);
}

bool write_test_opm4_file(const char* path) {
    return write_test_opm4_file_for(path, 201, 10, 1);
}

std::string make_temp_dir(const char* pattern) {
    char templ[128];
    std::snprintf(templ, sizeof(templ), "%s", pattern);
    char* path = mkdtemp(templ);
    return path ? std::string(path) : std::string();
}

bool calc_test_position(double jd_tdb, const void* data, Vector3* out) noexcept {
    if (!data || !out) {
        return false;
    }
    const TestEphemerisData* test_data = static_cast<const TestEphemerisData*>(data);
    out->x = test_data->base + jd_tdb;
    out->y = test_data->base + 2.0;
    out->z = test_data->base + 3.0;
    return true;
}

bool calc_test_velocity(double, const void* data, Vector3* out) noexcept {
    if (!data || !out) {
        return false;
    }
    const TestEphemerisData* test_data = static_cast<const TestEphemerisData*>(data);
    out->x = test_data->base + 4.0;
    out->y = test_data->base + 5.0;
    out->z = test_data->base + 6.0;
    return true;
}

bool calc_test_acceleration(double, const void* data, Vector3* out) noexcept {
    if (!data || !out) {
        return false;
    }
    const TestEphemerisData* test_data = static_cast<const TestEphemerisData*>(data);
    out->x = test_data->base + 7.0;
    out->y = test_data->base + 8.0;
    out->z = test_data->base + 9.0;
    return true;
}

void destroy_test_ephemeris_data(void* data) noexcept {
    TestEphemerisData* test_data = static_cast<TestEphemerisData*>(data);
    delete test_data;
}

EphemerisBlockDescriptor make_test_descriptor(int method_id, int bucket_id) {
    EphemerisBlockDescriptor descriptor;
    descriptor.route_key = EphemerisRouteKey(499, 0, method_id, bucket_id);
    descriptor.source_key = EphemerisBlockKey(1, static_cast<uint64_t>(bucket_id + 1), 1, 0);
    descriptor.target_id = 499;
    descriptor.center_id = 0;
    descriptor.method_id = method_id;
    descriptor.frame = EphemerisFrame::IcrfJ2000Equatorial;
    descriptor.format = EphemerisBlockFormat::Kepler;
    descriptor.jd_tdb_start = 100.0;
    descriptor.jd_tdb_end = 200.0;
    descriptor.path = "test";
    return descriptor;
}

EphemerisBlockDescriptor make_test_opm4_descriptor_for(
    const char* path,
    int target_id,
    int center_id,
    int method_id,
    uint64_t block_id
) {
    EphemerisBlockDescriptor descriptor;
    descriptor.route_key = EphemerisRouteKey(target_id, center_id, method_id, 0);
    descriptor.source_key = EphemerisBlockKey(2, block_id, 1, 0);
    descriptor.target_id = target_id;
    descriptor.center_id = center_id;
    descriptor.method_id = method_id;
    descriptor.frame = EphemerisFrame::IcrfJ2000Equatorial;
    descriptor.format = EphemerisBlockFormat::Opm4;
    descriptor.jd_tdb_start = 100.0;
    descriptor.jd_tdb_end = 110.0;
    descriptor.path = path;
    return descriptor;
}

EphemerisBlockDescriptor make_test_opm4_descriptor(const char* path) {
    return make_test_opm4_descriptor_for(path, 201, 10, 1, 1);
}

StorageEphemerisBlock make_test_storage(double base) {
    StorageEphemerisBlock storage;
    storage.format = EphemerisBlockFormat::Kepler;
    storage.position = &calc_test_position;
    storage.velocity = &calc_test_velocity;
    storage.acceleration = &calc_test_acceleration;
    storage.destroy_element = &destroy_test_ephemeris_data;
    TestEphemerisData* data = new TestEphemerisData();
    data->base = base;
    storage.data_vector.push_back(data);
    storage.total_bytes = sizeof(TestEphemerisData);
    return storage;
}

void test_service_locator(int* failures) {
    ServiceLocator locator;
    int first_service = 1;
    int second_service = 2;

    ServiceId first = locator.register_service("core.first", &first_service);
    ServiceId second = locator.register_service("core.second", &second_service);
    ServiceId duplicate = locator.register_service("core.first", &second_service);

    expect_true(first.is_valid(), "first service id valid", failures);
    expect_true(second.is_valid(), "second service id valid", failures);
    expect_u32(first.value, 1, "first service id", failures);
    expect_u32(second.value, 2, "second service id", failures);
    expect_u32(duplicate.value, first.value, "duplicate service returns same id", failures);
    expect_size(locator.service_count(), 2, "service count", failures);

    ServiceId resolved;
    expect_true(locator.resolve_service("core.first", &resolved), "resolve service", failures);
    expect_u32(resolved.value, first.value, "resolved service id", failures);
    expect_true(locator.service(first) == &first_service, "service pointer lookup", failures);
    expect_false(locator.resolve_service("missing", &resolved), "missing service resolve fails", failures);
    expect_false(resolved.is_valid(), "missing service clears output", failures);

    expect_false(locator.register_service("bad.null", 0).is_valid(), "null service rejected", failures);
    expect_false(locator.register_service("", &first_service).is_valid(), "empty service name rejected", failures);
    locator.clear();
    expect_size(locator.service_count(), 0, "clear service count", failures);
}

void test_taiyin_runtime_facade(int* failures) {
    TaiyinRuntime runtime;
    expect_true(runtime.ephemeris_service_id().is_valid(), "runtime ephemeris service id valid", failures);
    expect_size(runtime.services().service_count(), 1, "runtime has core ephemeris service", failures);
    expect_true(runtime.services().service(runtime.ephemeris_service_id()) == &runtime.ephemeris_service(), "runtime resolves ephemeris service pointer", failures);

    ServiceId resolved;
    expect_true(runtime.services().resolve_service("core.ephemeris", &resolved), "runtime resolves ephemeris service name", failures);
    expect_u32(resolved.value, runtime.ephemeris_service_id().value, "runtime resolved ephemeris service id", failures);

    MethodId method = runtime.registry().register_method(1, "test.runtime.method", &test_runtime_method);
    expect_true(method.is_valid(), "runtime registry usable", failures);
    runtime.clear_registries();
    expect_size(runtime.registry().method_count(), 0, "runtime clear clears registry", failures);
    expect_size(runtime.services().service_count(), 1, "runtime clear re-registers core ephemeris service", failures);
    expect_true(runtime.ephemeris_service_id().is_valid(), "runtime clear keeps ephemeris service id valid", failures);

    TaiyinRuntime& global = default_taiyin_runtime();
    expect_true(global.ephemeris_service_id().is_valid(), "default runtime ephemeris service id valid", failures);
}

void test_ephemeris_service_descriptor_lookup(int* failures) {
    EphemerisBlockCatalog catalog;
    EphemerisBlockDescriptor descriptor = make_test_descriptor(10, 0);
    expect_true(catalog.add(descriptor), "add ephemeris descriptor", failures);

    EphemerisService service;
    service.set_catalog(&catalog);

    EphemerisRequest request;
    request.target_id = 499;
    request.center_id = 0;
    request.frame = EphemerisFrame::IcrfJ2000Equatorial;
    request.jd_tdb = 150.0;

    const EphemerisBlockDescriptor* found = 0;
    expect_true(service.find_descriptor(request, &found), "find ephemeris descriptor", failures);
    expect_true(found != 0, "found descriptor pointer", failures);
    if (found) {
        expect_u32(static_cast<uint32_t>(found->method_id), 10, "found descriptor method", failures);
    }

    request.jd_tdb = 250.0;
    expect_false(service.find_descriptor(request, &found), "out-of-range descriptor lookup fails", failures);
    expect_true(found == 0, "out-of-range descriptor clears output", failures);
}

void test_ephemeris_service_cache_eval(int* failures) {
    EphemerisBlockCatalog catalog;
    EphemerisBlockDescriptor descriptor = make_test_descriptor(11, 1);
    expect_true(catalog.add(descriptor), "add cache eval descriptor", failures);

    EphemerisBlockCache cache(1024 * 1024);
    StorageEphemerisBlock storage = make_test_storage(20.0);
    expect_true(cache.insert(descriptor.route_key, 100.0, 200.0, &storage), "insert test block into cache", failures);

    EphemerisService service;
    service.set_catalog(&catalog);
    service.set_cache(&cache);

    EphemerisRequest request;
    request.target_id = 499;
    request.center_id = 0;
    request.frame = EphemerisFrame::IcrfJ2000Equatorial;
    request.jd_tdb = 150.0;

    EphemerisResult result;
    expect_true(service.eval_state(request, &result), "ephemeris service eval_state cache hit", failures);
    expect_true(result.cache_hit, "ephemeris service reports cache hit", failures);
    expect_u32(static_cast<uint32_t>(result.descriptor.method_id), 11, "ephemeris service result descriptor", failures);
    expect_near(result.state.position_au.x, 170.0, "ephemeris position x", failures);
    expect_near(result.state.velocity_au_per_day.y, 25.0, "ephemeris velocity y", failures);
    expect_near(result.state.acceleration_au_per_day2.z, 29.0, "ephemeris acceleration z", failures);

    EphemerisRequest missing = request;
    missing.jd_tdb = 250.0;
    expect_false(service.eval_state(missing, &result), "ephemeris service eval_state misses outside descriptor range", failures);
    expect_false(result.cache_hit, "failed eval clears cache hit", failures);
}

void test_ephemeris_service_cache_miss_loads_descriptor(int* failures) {
    const char* path = "test_runtime_services_cache_miss.opm4";
    std::remove(path);
    expect_true(write_test_opm4_file(path), "write service cache-miss OPM4 fixture", failures);

    EphemerisBlockCatalog catalog;
    EphemerisBlockDescriptor descriptor = make_test_opm4_descriptor(path);
    expect_true(catalog.add(descriptor), "add OPM4 descriptor", failures);

    EphemerisBlockCache cache(1024 * 1024);
    EphemerisService service;
    service.set_catalog(&catalog);
    service.set_cache(&cache);

    EphemerisRequest request;
    request.target_id = 201;
    request.center_id = 10;
    request.frame = EphemerisFrame::IcrfJ2000Equatorial;
    request.jd_tdb = 105.0;

    EphemerisResult result;
    expect_true(service.eval_state(request, &result), "ephemeris service loads descriptor on cache miss", failures);
    expect_false(result.cache_hit, "first descriptor load reports cache miss", failures);
    expect_size(cache.entry_count(), 1, "descriptor load inserts one cache entry", failures);
    expect_u32(static_cast<uint32_t>(result.descriptor.target_id), 201, "loaded descriptor target", failures);
    expect_near_tol(result.state.position_au.x, 0.005 / taiyin::TAIYIN_AU_KM, 1.0e-20, "loaded OPM4 position x", failures);
    expect_near_tol(result.state.velocity_au_per_day.x, 0.002 / taiyin::TAIYIN_AU_KM, 1.0e-20, "loaded OPM4 velocity x", failures);

    EphemerisResult second_result;
    expect_true(service.eval_state(request, &second_result), "ephemeris service reuses loaded cache bucket", failures);
    expect_true(second_result.cache_hit, "second descriptor eval reports cache hit", failures);
    expect_size(cache.entry_count(), 1, "second eval keeps one cache entry", failures);
    expect_near_tol(second_result.state.position_au.y, 0.015 / taiyin::TAIYIN_AU_KM, 1.0e-20, "cached OPM4 position y", failures);

    std::remove(path);
}

void test_ephemeris_service_falls_back_after_failed_preferred_descriptor(int* failures) {
    EphemerisBlockCatalog catalog;
    EphemerisBlockDescriptor broken = make_test_opm4_descriptor_for(
        "missing-preferred-runtime-services.opm4", 499, 0, 30, 30);
    EphemerisBlockDescriptor fallback = make_test_descriptor(20, 2);
    expect_true(catalog.add(broken), "add broken preferred descriptor", failures);
    expect_true(catalog.add(fallback), "add fallback descriptor", failures);

    EphemerisPriorityRegistry priorities;
    expect_true(priorities.set_global_method_priority(30, 100), "set broken preferred priority", failures);
    expect_true(priorities.set_global_method_priority(20, 10), "set fallback priority", failures);

    EphemerisBlockCache cache(1024 * 1024);
    StorageEphemerisBlock storage = make_test_storage(30.0);
    expect_true(cache.insert(fallback.route_key, 100.0, 200.0, &storage), "insert cached fallback descriptor", failures);

    EphemerisService service;
    service.set_catalog(&catalog);
    service.set_priorities(&priorities);
    service.set_cache(&cache);

    EphemerisRequest request;
    request.target_id = 499;
    request.center_id = 0;
    request.frame = EphemerisFrame::IcrfJ2000Equatorial;
    request.jd_tdb = 105.0;

    EphemerisSelectionResult selection;
    expect_true(service.select_calculation_route(request, &selection), "selector falls back after broken preferred descriptor", failures);
    expect_true(selection.cache_hit, "fallback selected from cache", failures);
    expect_false(selection.loaded, "fallback route did not load", failures);
    expect_u32(static_cast<uint32_t>(selection.source_descriptor.method_id), 20, "fallback source method selected", failures);

    EphemerisResult result;
    expect_true(service.eval_state(request, &result), "eval fallback after broken preferred descriptor", failures);
    expect_true(result.cache_hit, "eval fallback reports cache hit", failures);
    expect_u32(static_cast<uint32_t>(result.descriptor.method_id), 20, "eval fallback descriptor method", failures);
    expect_near(result.state.position_au.x, 135.0, "fallback eval position x", failures);
}

void test_ephemeris_service_priority_first_over_lower_cached_descriptor(int* failures) {
    const char* path = "test_runtime_services_priority_first.opm4";
    std::remove(path);
    expect_true(write_test_opm4_file_for(path, 499, 0, 40), "write priority-first OPM4 fixture", failures);

    EphemerisBlockCatalog catalog;
    EphemerisBlockDescriptor cached_low = make_test_descriptor(20, 3);
    EphemerisBlockDescriptor loadable_high = make_test_opm4_descriptor_for(path, 499, 0, 40, 40);
    expect_true(catalog.add(cached_low), "add cached low-priority descriptor", failures);
    expect_true(catalog.add(loadable_high), "add loadable high-priority descriptor", failures);

    EphemerisPriorityRegistry priorities;
    expect_true(priorities.set_global_method_priority(40, 100), "set high loadable priority", failures);
    expect_true(priorities.set_global_method_priority(20, 10), "set low cached priority", failures);

    EphemerisBlockCache cache(1024 * 1024);
    StorageEphemerisBlock storage = make_test_storage(70.0);
    expect_true(cache.insert(cached_low.route_key, 100.0, 200.0, &storage), "insert lower-priority cached descriptor", failures);

    EphemerisService service;
    service.set_catalog(&catalog);
    service.set_priorities(&priorities);
    service.set_cache(&cache);

    EphemerisRequest request;
    request.target_id = 499;
    request.center_id = 0;
    request.frame = EphemerisFrame::IcrfJ2000Equatorial;
    request.jd_tdb = 105.0;

    EphemerisResult result;
    expect_true(service.eval_state(request, &result), "priority-first selector loads high-priority descriptor", failures);
    expect_false(result.cache_hit, "high-priority load reports cache miss despite lower cached source", failures);
    expect_u32(static_cast<uint32_t>(result.descriptor.method_id), 40, "high-priority descriptor selected", failures);
    expect_near_tol(result.state.position_au.x, 0.005 / taiyin::TAIYIN_AU_KM, 1.0e-20, "high-priority OPM4 position x", failures);

    std::remove(path);
}

void test_global_ephemeris_runtime_initializes_without_paths(int* failures) {
    EphemerisRuntimeConfig config;
    config.cache_max_bytes = 4096;
    expect_true(initialize_global_ephemeris_runtime(config), "initialize global ephemeris runtime without paths", failures);
    expect_size(global_ephemeris_catalog_size(), 0, "global catalog starts empty without paths", failures);
    expect_true(global_ephemeris_cache_max_bytes() > 0, "global cache exists", failures);
    expect_size(global_ephemeris_cache_entry_count(), 0, "global cache starts empty", failures);
    expect_size(global_ephemeris_cache_max_bytes(), 4096, "global cache configured max bytes", failures);
    // Raw accessors are used here intentionally to verify single-threaded pointer bindings.
    expect_true(global_ephemeris_service().catalog() == &global_ephemeris_catalog(), "global service catalog binding", failures);
    expect_true(global_ephemeris_service().priorities() == &global_ephemeris_priorities(), "global service priority binding", failures);
    expect_true(global_ephemeris_service().cache() == global_ephemeris_cache(), "global service cache binding", failures);

    EphemerisRequest request;
    request.target_id = 201;
    request.center_id = 10;
    request.frame = EphemerisFrame::IcrfJ2000Equatorial;
    request.jd_tdb = 105.0;
    EphemerisResult result;
    expect_false(eval_global_ephemeris_state(request, &result), "global eval without descriptors fails", failures);
}

void test_global_ephemeris_runtime_explicit_directory_lazy_load(int* failures) {
    const std::string root = make_temp_dir("/tmp/taiyin-global-dir-XXXXXX");
    expect_true(!root.empty(), "make global runtime temp directory", failures);
    const std::string path = root + "/global_dir.opm4";
    expect_true(write_test_opm4_file_for(path.c_str(), 710001, 10, 1), "write global directory OPM4 fixture", failures);

    const char* paths[] = { root.c_str() };
    EphemerisRuntimeConfig config;
    config.cache_max_bytes = 1024 * 1024;
    config.source_paths = paths;
    config.source_path_count = 1;
    expect_true(initialize_global_ephemeris_runtime(config), "initialize global runtime from explicit directory", failures);
    expect_size(global_ephemeris_catalog_size(), 1, "explicit directory registers one descriptor", failures);
    expect_true(global_ephemeris_cache_max_bytes() > 0, "explicit directory global cache exists", failures);
    expect_size(global_ephemeris_cache_entry_count(), 0, "explicit directory does not preload cache", failures);

    EphemerisRequest request;
    request.target_id = 710001;
    request.center_id = 10;
    request.frame = EphemerisFrame::IcrfJ2000Equatorial;
    request.jd_tdb = 105.0;

    EphemerisResult first;
    expect_true(eval_global_ephemeris_state(request, &first), "global eval lazy loads explicit directory descriptor", failures);
    expect_false(first.cache_hit, "first global directory eval reports cache miss", failures);
    expect_u32(static_cast<uint32_t>(first.descriptor.target_id), 710001, "global directory descriptor selected", failures);
    expect_size(global_ephemeris_cache_entry_count(), 1, "global directory eval inserts cache entry", failures);

    EphemerisResult second;
    expect_true(eval_global_ephemeris_state(request, &second), "global eval reuses explicit directory cache", failures);
    expect_true(second.cache_hit, "second global directory eval reports cache hit", failures);

    std::remove(path.c_str());
    rmdir(root.c_str());
}

void test_global_ephemeris_runtime_explicit_file_lazy_load(int* failures) {
    const std::string root = make_temp_dir("/tmp/taiyin-global-file-XXXXXX");
    expect_true(!root.empty(), "make global runtime file temp directory", failures);
    const std::string path = root + "/global_file.opm4";
    expect_true(write_test_opm4_file_for(path.c_str(), 710002, 10, 1), "write global file OPM4 fixture", failures);

    const char* paths[] = { path.c_str() };
    EphemerisRuntimeConfig config;
    config.cache_max_bytes = 1024 * 1024;
    config.source_paths = paths;
    config.source_path_count = 1;
    expect_true(initialize_global_ephemeris_runtime(config), "initialize global runtime from explicit file", failures);
    expect_size(global_ephemeris_catalog_size(), 1, "explicit file registers one descriptor", failures);
    expect_size(global_ephemeris_cache_entry_count(), 0, "explicit file does not preload cache", failures);

    EphemerisRequest request;
    request.target_id = 710002;
    request.center_id = 10;
    request.frame = EphemerisFrame::IcrfJ2000Equatorial;
    request.jd_tdb = 105.0;
    EphemerisResult result;
    expect_true(eval_global_ephemeris_state(request, &result), "global eval lazy loads explicit file descriptor", failures);
    expect_false(result.cache_hit, "first global file eval reports cache miss", failures);
    expect_size(global_ephemeris_cache_entry_count(), 1, "global file eval inserts cache entry", failures);

    std::remove(path.c_str());
    rmdir(root.c_str());
}

void test_global_ephemeris_runtime_missing_path_resets_empty(int* failures) {
    const std::string root = make_temp_dir("/tmp/taiyin-global-missing-XXXXXX");
    expect_true(!root.empty(), "make global runtime missing temp directory", failures);
    const std::string path = root + "/global_present.opm4";
    expect_true(write_test_opm4_file_for(path.c_str(), 710003, 10, 1), "write global missing setup OPM4 fixture", failures);

    const char* good_paths[] = { path.c_str() };
    EphemerisRuntimeConfig good_config;
    good_config.source_paths = good_paths;
    good_config.source_path_count = 1;
    expect_true(initialize_global_ephemeris_runtime(good_config), "initialize global runtime before missing path", failures);
    expect_size(global_ephemeris_catalog_size(), 1, "global catalog has setup descriptor", failures);

    const char* missing_paths[] = { "/tmp/taiyin-definitely-missing-source-path.opm4" };
    EphemerisRuntimeConfig missing_config;
    missing_config.source_paths = missing_paths;
    missing_config.source_path_count = 1;
    expect_false(initialize_global_ephemeris_runtime(missing_config), "missing explicit path initialization fails", failures);
    expect_size(global_ephemeris_catalog_size(), 0, "missing explicit path resets catalog empty", failures);
    expect_true(global_ephemeris_cache_max_bytes() > 0, "missing explicit path leaves cache initialized", failures);
    expect_size(global_ephemeris_cache_entry_count(), 0, "missing explicit path leaves cache empty", failures);
    expect_false(add_global_ephemeris_source_path("/tmp/taiyin-definitely-missing-source-path.opm4"), "add missing global path fails", failures);
    expect_size(global_ephemeris_catalog_size(), 0, "add missing path does not add descriptors", failures);

    std::remove(path.c_str());
    rmdir(root.c_str());
}

}  // namespace

int main() {
    int failures = 0;
    test_service_locator(&failures);
    test_taiyin_runtime_facade(&failures);
    test_ephemeris_service_descriptor_lookup(&failures);
    test_ephemeris_service_cache_eval(&failures);
    test_ephemeris_service_cache_miss_loads_descriptor(&failures);
    test_ephemeris_service_falls_back_after_failed_preferred_descriptor(&failures);
    test_ephemeris_service_priority_first_over_lower_cached_descriptor(&failures);
    test_global_ephemeris_runtime_initializes_without_paths(&failures);
    test_global_ephemeris_runtime_explicit_directory_lazy_load(&failures);
    test_global_ephemeris_runtime_explicit_file_lazy_load(&failures);
    test_global_ephemeris_runtime_missing_path_resets_empty(&failures);

    if (failures == 0) {
        std::cout << "test_runtime_services: ALL TESTS PASSED\n";
        return 0;
    }
    std::cerr << failures << " test_runtime_services failure(s)\n";
    return 1;
}
