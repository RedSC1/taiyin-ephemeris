#include "taiyin/internal/custom_ephemeris_source_registry.h"
#include "taiyin/runtime/taiyin_runtime.h"
#include "taiyin/time.h"

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <iostream>
#include <new>
#include <thread>
#include <vector>

namespace {

const double JD0 = taiyin::JD_J2000;
const int METHOD_SAME = 91001;
const int METHOD_OTHER = 91002;
const int METHOD_FAIL = 91003;
const int METHOD_FALLBACK = 91004;
const int METHOD_GLOBAL = 91005;

struct SingleflightData {
    double x;
    double y;
    double z;
};

std::atomic<int> g_same_load_count(0);
std::atomic<int> g_a_load_count(0);
std::atomic<int> g_b_load_count(0);
std::atomic<int> g_fail_load_count(0);
std::atomic<int> g_fallback_load_count(0);
std::atomic<int> g_global_load_count(0);
std::atomic<int> g_active_loaders(0);
std::atomic<int> g_peak_loaders(0);

void expect_true(bool value, const char* label, int* failures) {
    if (!value) {
        std::cerr << "FAIL: expected true: " << label << "\n";
        ++(*failures);
    }
}

void expect_int(int actual, int expected, const char* label, int* failures) {
    if (actual != expected) {
        std::cerr << "FAIL: " << label << ": actual=" << actual
                  << " expected=" << expected << "\n";
        ++(*failures);
    }
}

void expect_near(double actual, double expected, double tolerance, const char* label, int* failures) {
    if (std::fabs(actual - expected) > tolerance) {
        std::cerr << "FAIL: " << label << ": actual=" << actual
                  << " expected=" << expected << " tolerance=" << tolerance << "\n";
        ++(*failures);
    }
}

void reset_counters() {
    g_same_load_count.store(0);
    g_a_load_count.store(0);
    g_b_load_count.store(0);
    g_fail_load_count.store(0);
    g_fallback_load_count.store(0);
    g_global_load_count.store(0);
    g_active_loaders.store(0);
    g_peak_loaders.store(0);
}

void record_loader_overlap() {
    const int current = g_active_loaders.fetch_add(1) + 1;
    int observed = g_peak_loaders.load();
    while (current > observed
           && !g_peak_loaders.compare_exchange_weak(observed, current)) {}
}

void finish_loader_overlap() {
    g_active_loaders.fetch_sub(1);
}

SingleflightData* make_data(double x, double y, double z) {
    SingleflightData* data = new (std::nothrow) SingleflightData();
    if (!data) {
        return 0;
    }
    data->x = x;
    data->y = y;
    data->z = z;
    return data;
}

bool finish_load(SingleflightData* data, void** out_data, size_t* out_bytes) {
    if (!data || !out_data || !out_bytes) {
        delete data;
        return false;
    }
    *out_data = data;
    *out_bytes = sizeof(SingleflightData);
    return true;
}

bool load_same_source(const char*, void** out_data, size_t* out_bytes) {
    g_same_load_count.fetch_add(1);
    std::this_thread::sleep_for(std::chrono::milliseconds(60));
    return finish_load(make_data(1.0, 2.0, 3.0), out_data, out_bytes);
}

bool load_a_source(const char*, void** out_data, size_t* out_bytes) {
    g_a_load_count.fetch_add(1);
    record_loader_overlap();
    std::this_thread::sleep_for(std::chrono::milliseconds(80));
    finish_loader_overlap();
    return finish_load(make_data(10.0, 0.0, 0.0), out_data, out_bytes);
}

bool load_b_source(const char*, void** out_data, size_t* out_bytes) {
    g_b_load_count.fetch_add(1);
    record_loader_overlap();
    std::this_thread::sleep_for(std::chrono::milliseconds(80));
    finish_loader_overlap();
    return finish_load(make_data(20.0, 0.0, 0.0), out_data, out_bytes);
}

bool load_fail_source(const char*, void** out_data, size_t* out_bytes) {
    if (out_data) {
        *out_data = 0;
    }
    if (out_bytes) {
        *out_bytes = 0;
    }
    g_fail_load_count.fetch_add(1);
    std::this_thread::sleep_for(std::chrono::milliseconds(60));
    return false;
}

bool load_fallback_source(const char*, void** out_data, size_t* out_bytes) {
    g_fallback_load_count.fetch_add(1);
    std::this_thread::sleep_for(std::chrono::milliseconds(60));
    return finish_load(make_data(42.0, 0.0, 0.0), out_data, out_bytes);
}

bool load_global_source(const char*, void** out_data, size_t* out_bytes) {
    g_global_load_count.fetch_add(1);
    std::this_thread::sleep_for(std::chrono::milliseconds(60));
    return finish_load(make_data(7.0, 8.0, 9.0), out_data, out_bytes);
}

bool singleflight_position(double, const void* data, taiyin::Vector3* out) {
    if (!data || !out) {
        return false;
    }
    const SingleflightData* value = static_cast<const SingleflightData*>(data);
    out->x = value->x;
    out->y = value->y;
    out->z = value->z;
    return true;
}

bool zero_velocity(double, const void*, taiyin::Vector3* out) {
    if (!out) {
        return false;
    }
    out->x = 0.0;
    out->y = 0.0;
    out->z = 0.0;
    return true;
}

void singleflight_destroy(void* data) {
    delete static_cast<SingleflightData*>(data);
}

bool register_source(
    int target_id,
    int method_id,
    const char* path,
    taiyin::internal::EphemerisBlockFileLoadFn load,
    taiyin::internal::EphemerisBlockDescriptor* out
) {
    taiyin::internal::CustomEphemerisFileSourceDefinition definition;
    definition.target_id = target_id;
    definition.center_id = 0;
    definition.method_id = method_id;
    definition.frame = taiyin::internal::EphemerisFrame::IcrfJ2000Equatorial;
    definition.jd_tdb_start = JD0 - 10.0;
    definition.jd_tdb_end = JD0 + 10.0;
    definition.path = path;
    definition.load = load;
    definition.position = singleflight_position;
    definition.velocity = zero_velocity;
    definition.acceleration = zero_velocity;
    definition.destroy = singleflight_destroy;

    uint64_t source_id = 0;
    return taiyin::internal::register_custom_ephemeris_file_source(definition, &source_id)
        && taiyin::internal::make_custom_ephemeris_descriptor(source_id, out);
}

taiyin::runtime::EphemerisRequest make_request(int target_id) {
    taiyin::runtime::EphemerisRequest request;
    request.target_id = target_id;
    request.center_id = 0;
    request.frame = taiyin::internal::EphemerisFrame::IcrfJ2000Equatorial;
    request.jd_tdb = JD0;
    return request;
}

void test_same_key_service_loads_once(int* failures) {
    reset_counters();
    taiyin::internal::clear_custom_ephemeris_sources();

    taiyin::internal::EphemerisBlockDescriptor descriptor;
    expect_true(register_source(910100, METHOD_SAME, "same-singleflight", load_same_source, &descriptor),
                "register same-key custom source", failures);

    taiyin::internal::EphemerisBlockCatalog catalog;
    taiyin::internal::EphemerisPriorityRegistry priorities;
    taiyin::internal::EphemerisBlockCache cache(1024 * 1024);
    taiyin::runtime::EphemerisService service;
    expect_true(catalog.add(descriptor), "add same-key descriptor", failures);
    expect_true(priorities.set_global_method_priority(METHOD_SAME, 100),
                "set same-key priority", failures);
    service.set_catalog(&catalog);
    service.set_priorities(&priorities);
    service.set_cache(&cache);

    std::atomic<bool> start(false);
    std::atomic<int> read_failures(0);
    std::vector<std::thread> readers;
    for (int i = 0; i < 8; ++i) {
        readers.push_back(std::thread([&]() {
            while (!start.load()) {
                std::this_thread::yield();
            }
            taiyin::runtime::EphemerisResult result;
            if (!service.eval_state(make_request(910100), &result)) {
                read_failures.fetch_add(1);
                return;
            }
            if (std::fabs(result.state.position_au.x - 1.0) > 1.0e-12) {
                read_failures.fetch_add(1);
            }
        }));
    }

    start.store(true);
    for (size_t i = 0; i < readers.size(); ++i) {
        readers[i].join();
    }

    expect_int(read_failures.load(), 0, "same-key service readers succeed", failures);
    expect_int(g_same_load_count.load(), 1, "same-key service load count", failures);
    expect_true(cache.entry_count() == 1, "same-key service cache entry exists", failures);
    taiyin::internal::clear_custom_ephemeris_sources();
}

void test_different_keys_service_load_independently(int* failures) {
    reset_counters();
    taiyin::internal::clear_custom_ephemeris_sources();

    taiyin::internal::EphemerisBlockDescriptor descriptor_a;
    taiyin::internal::EphemerisBlockDescriptor descriptor_b;
    expect_true(register_source(910201, METHOD_SAME, "different-a", load_a_source, &descriptor_a),
                "register different-key source A", failures);
    expect_true(register_source(910202, METHOD_OTHER, "different-b", load_b_source, &descriptor_b),
                "register different-key source B", failures);

    taiyin::internal::EphemerisBlockCatalog catalog;
    taiyin::internal::EphemerisPriorityRegistry priorities;
    taiyin::internal::EphemerisBlockCache cache(1024 * 1024);
    taiyin::runtime::EphemerisService service;
    expect_true(catalog.add(descriptor_a), "add different-key descriptor A", failures);
    expect_true(catalog.add(descriptor_b), "add different-key descriptor B", failures);
    expect_true(priorities.set_global_method_priority(METHOD_SAME, 100),
                "set different-key priority A", failures);
    expect_true(priorities.set_global_method_priority(METHOD_OTHER, 100),
                "set different-key priority B", failures);
    service.set_catalog(&catalog);
    service.set_priorities(&priorities);
    service.set_cache(&cache);

    std::atomic<bool> start(false);
    std::atomic<int> read_failures(0);
    std::thread reader_a([&]() {
        while (!start.load()) {
            std::this_thread::yield();
        }
        taiyin::runtime::EphemerisResult result;
        if (!service.eval_state(make_request(910201), &result)
            || std::fabs(result.state.position_au.x - 10.0) > 1.0e-12) {
            read_failures.fetch_add(1);
        }
    });
    std::thread reader_b([&]() {
        while (!start.load()) {
            std::this_thread::yield();
        }
        taiyin::runtime::EphemerisResult result;
        if (!service.eval_state(make_request(910202), &result)
            || std::fabs(result.state.position_au.x - 20.0) > 1.0e-12) {
            read_failures.fetch_add(1);
        }
    });

    start.store(true);
    reader_a.join();
    reader_b.join();

    expect_int(read_failures.load(), 0, "different-key service readers succeed", failures);
    expect_int(g_a_load_count.load(), 1, "different-key source A load count", failures);
    expect_int(g_b_load_count.load(), 1, "different-key source B load count", failures);
    expect_true(g_peak_loaders.load() > 1, "different-key source loads overlap", failures);
    taiyin::internal::clear_custom_ephemeris_sources();
}

void test_failed_preferred_load_wakes_and_falls_back(int* failures) {
    reset_counters();
    taiyin::internal::clear_custom_ephemeris_sources();

    taiyin::internal::EphemerisBlockDescriptor fail_descriptor;
    taiyin::internal::EphemerisBlockDescriptor fallback_descriptor;
    expect_true(register_source(910300, METHOD_FAIL, "preferred-fail", load_fail_source, &fail_descriptor),
                "register failing preferred source", failures);
    expect_true(register_source(910300, METHOD_FALLBACK, "fallback-ok", load_fallback_source, &fallback_descriptor),
                "register fallback source", failures);

    taiyin::internal::EphemerisBlockCatalog catalog;
    taiyin::internal::EphemerisPriorityRegistry priorities;
    taiyin::internal::EphemerisBlockCache cache(1024 * 1024);
    taiyin::runtime::EphemerisService service;
    expect_true(catalog.add(fail_descriptor), "add failing preferred descriptor", failures);
    expect_true(catalog.add(fallback_descriptor), "add fallback descriptor", failures);
    expect_true(priorities.set_global_method_priority(METHOD_FAIL, 200),
                "set failing preferred priority", failures);
    expect_true(priorities.set_global_method_priority(METHOD_FALLBACK, 100),
                "set fallback priority", failures);
    service.set_catalog(&catalog);
    service.set_priorities(&priorities);
    service.set_cache(&cache);

    std::atomic<bool> start(false);
    std::atomic<int> read_failures(0);
    std::vector<std::thread> readers;
    for (int i = 0; i < 6; ++i) {
        readers.push_back(std::thread([&]() {
            while (!start.load()) {
                std::this_thread::yield();
            }
            taiyin::runtime::EphemerisResult result;
            if (!service.eval_state(make_request(910300), &result)) {
                read_failures.fetch_add(1);
                return;
            }
            if (result.descriptor.method_id != METHOD_FALLBACK
                || std::fabs(result.state.position_au.x - 42.0) > 1.0e-12) {
                read_failures.fetch_add(1);
            }
        }));
    }

    start.store(true);
    for (size_t i = 0; i < readers.size(); ++i) {
        readers[i].join();
    }

    expect_int(read_failures.load(), 0, "fallback readers succeed", failures);
    expect_int(g_fail_load_count.load(), 1, "failing preferred source load count", failures);
    expect_int(g_fallback_load_count.load(), 1, "fallback source load count", failures);
    taiyin::internal::clear_custom_ephemeris_sources();
}

void test_global_runtime_loads_once(int* failures) {
    reset_counters();
    taiyin::internal::clear_custom_ephemeris_sources();

    taiyin::runtime::EphemerisRuntimeConfig empty_config;
    expect_true(taiyin::runtime::initialize_global_ephemeris_runtime(empty_config),
                "initialize empty global runtime", failures);

    taiyin::internal::EphemerisBlockDescriptor descriptor;
    expect_true(register_source(910400, METHOD_GLOBAL, "global-singleflight", load_global_source, &descriptor),
                "register global singleflight source", failures);
    expect_true(taiyin::runtime::add_global_ephemeris_descriptor(descriptor),
                "add global singleflight descriptor", failures);

    std::atomic<bool> start(false);
    std::atomic<int> read_failures(0);
    std::vector<std::thread> readers;
    for (int i = 0; i < 8; ++i) {
        readers.push_back(std::thread([&]() {
            while (!start.load()) {
                std::this_thread::yield();
            }
            taiyin::runtime::EphemerisResult result;
            if (!taiyin::runtime::eval_global_ephemeris_state(make_request(910400), &result)) {
                read_failures.fetch_add(1);
                return;
            }
            if (std::fabs(result.state.position_au.x - 7.0) > 1.0e-12) {
                read_failures.fetch_add(1);
            }
        }));
    }

    start.store(true);
    for (size_t i = 0; i < readers.size(); ++i) {
        readers[i].join();
    }

    expect_int(read_failures.load(), 0, "global singleflight readers succeed", failures);
    expect_int(g_global_load_count.load(), 1, "global singleflight load count", failures);
    expect_true(taiyin::runtime::global_ephemeris_cache_entry_count() == 1,
                "global singleflight cache entry exists", failures);

    expect_true(taiyin::runtime::initialize_global_ephemeris_runtime(empty_config),
                "reset empty global runtime", failures);
    taiyin::internal::clear_custom_ephemeris_sources();
}

}  // namespace

int main() {
    int failures = 0;
    test_same_key_service_loads_once(&failures);
    test_different_keys_service_load_independently(&failures);
    test_failed_preferred_load_wakes_and_falls_back(&failures);
    test_global_runtime_loads_once(&failures);

    if (failures != 0) {
        std::cerr << failures << " ephemeris singleflight test(s) failed\n";
        return 1;
    }
    return 0;
}
