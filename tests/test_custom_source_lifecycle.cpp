#include "taiyin/internal/custom_ephemeris_source_registry.h"
#include "taiyin/runtime/ephemeris_service.h"
#include "taiyin/time.h"

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <new>
#include <string>
#include <thread>
#include <unistd.h>

namespace {

using namespace taiyin;
using namespace taiyin::internal;
using namespace taiyin::runtime;

const double JD0 = taiyin::JD_J2000;
const int METHOD_A = 92001;
const int METHOD_B = 92002;
const int METHOD_HIGH = 92003;
const int METHOD_LOW = 92004;

struct LifecycleData {
    double x;
    double y;
    double z;
    bool slow;
    int* clone_count;
    int* destroy_count;
};

std::atomic<int> g_slow_entries(0);
std::atomic<int> g_slow_exits(0);

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

void reset_slow_counters() {
    g_slow_entries.store(0);
    g_slow_exits.store(0);
}

bool lifecycle_clone(const void* source_data, size_t source_bytes, void** out_data, size_t* out_bytes) {
    if (out_data) {
        *out_data = 0;
    }
    if (out_bytes) {
        *out_bytes = 0;
    }
    if (!source_data || source_bytes != sizeof(LifecycleData) || !out_data || !out_bytes) {
        return false;
    }

    LifecycleData* clone = new (std::nothrow) LifecycleData(*static_cast<const LifecycleData*>(source_data));
    if (!clone) {
        return false;
    }
    if (clone->clone_count) {
        ++(*clone->clone_count);
    }
    *out_data = clone;
    *out_bytes = sizeof(LifecycleData);
    return true;
}

void lifecycle_destroy(void* data) {
    LifecycleData* lifecycle = static_cast<LifecycleData*>(data);
    if (lifecycle && lifecycle->destroy_count) {
        ++(*lifecycle->destroy_count);
    }
    delete lifecycle;
}

bool lifecycle_position(double, const void* data, Vector3* out) {
    const LifecycleData* lifecycle = static_cast<const LifecycleData*>(data);
    if (!lifecycle || !out) {
        return false;
    }
    if (lifecycle->slow) {
        g_slow_entries.fetch_add(1);
        std::this_thread::sleep_for(std::chrono::milliseconds(80));
        g_slow_exits.fetch_add(1);
    }
    out->x = lifecycle->x;
    out->y = lifecycle->y;
    out->z = lifecycle->z;
    return true;
}

bool lifecycle_velocity(double, const void* data, Vector3* out) {
    const LifecycleData* lifecycle = static_cast<const LifecycleData*>(data);
    if (!lifecycle || !out) {
        return false;
    }
    out->x = lifecycle->x + 0.1;
    out->y = lifecycle->y + 0.1;
    out->z = lifecycle->z + 0.1;
    return true;
}

bool lifecycle_acceleration(double, const void* data, Vector3* out) {
    const LifecycleData* lifecycle = static_cast<const LifecycleData*>(data);
    if (!lifecycle || !out) {
        return false;
    }
    out->x = lifecycle->x + 0.2;
    out->y = lifecycle->y + 0.2;
    out->z = lifecycle->z + 0.2;
    return true;
}

EphemerisRequest make_request(int target_id) {
    EphemerisRequest request;
    request.target_id = target_id;
    request.center_id = 0;
    request.frame = EphemerisFrame::IcrfJ2000Equatorial;
    request.jd_tdb = JD0;
    return request;
}

bool register_memory_source(
    int target_id,
    int method_id,
    double x,
    bool slow,
    int* clone_count,
    int* destroy_count,
    uint64_t* out_source_id,
    EphemerisBlockDescriptor* out_descriptor
) {
    LifecycleData data;
    data.x = x;
    data.y = x + 1.0;
    data.z = x + 2.0;
    data.slow = slow;
    data.clone_count = clone_count;
    data.destroy_count = destroy_count;

    CustomEphemerisSourceDefinition definition;
    definition.target_id = target_id;
    definition.center_id = 0;
    definition.method_id = method_id;
    definition.frame = EphemerisFrame::IcrfJ2000Equatorial;
    definition.jd_tdb_start = JD0 - 10.0;
    definition.jd_tdb_end = JD0 + 10.0;
    definition.data = &data;
    definition.bytes = sizeof(data);
    definition.position = lifecycle_position;
    definition.velocity = lifecycle_velocity;
    definition.acceleration = lifecycle_acceleration;
    definition.clone = lifecycle_clone;
    definition.destroy = lifecycle_destroy;

    return register_custom_ephemeris_source(definition, out_source_id)
        && make_custom_ephemeris_descriptor(*out_source_id, out_descriptor);
}

std::string make_temp_file_path() {
    char templ[] = "/tmp/taiyin-custom-lifecycle-XXXXXX";
    const int fd = mkstemp(templ);
    if (fd >= 0) {
        close(fd);
    }
    return std::string(templ);
}

void write_lifecycle_file(const std::string& path, double x) {
    std::ofstream file(path.c_str(), std::ios::out | std::ios::trunc);
    file.precision(17);
    file << x << "\n" << (x + 1.0) << "\n" << (x + 2.0) << "\n";
}

bool lifecycle_file_load(const char* path, void** out_data, size_t* out_bytes) {
    if (out_data) {
        *out_data = 0;
    }
    if (out_bytes) {
        *out_bytes = 0;
    }
    if (!path || !out_data || !out_bytes) {
        return false;
    }

    std::ifstream file(path);
    if (!file) {
        return false;
    }

    LifecycleData* data = new (std::nothrow) LifecycleData();
    if (!data) {
        return false;
    }
    data->slow = false;
    data->clone_count = 0;
    data->destroy_count = 0;
    if (!(file >> data->x >> data->y >> data->z)) {
        delete data;
        return false;
    }

    *out_data = data;
    *out_bytes = sizeof(LifecycleData);
    return true;
}

bool register_file_source(
    int target_id,
    int method_id,
    const std::string& path,
    uint64_t* out_source_id,
    EphemerisBlockDescriptor* out_descriptor
) {
    CustomEphemerisFileSourceDefinition definition;
    definition.target_id = target_id;
    definition.center_id = 0;
    definition.method_id = method_id;
    definition.frame = EphemerisFrame::IcrfJ2000Equatorial;
    definition.jd_tdb_start = JD0 - 10.0;
    definition.jd_tdb_end = JD0 + 10.0;
    definition.path = path.c_str();
    definition.load = lifecycle_file_load;
    definition.position = lifecycle_position;
    definition.velocity = lifecycle_velocity;
    definition.acceleration = lifecycle_acceleration;
    definition.destroy = lifecycle_destroy;

    return register_custom_ephemeris_file_source(definition, out_source_id)
        && make_custom_ephemeris_descriptor(*out_source_id, out_descriptor);
}

void bind_service(
    EphemerisService* service,
    EphemerisBlockCatalog* catalog,
    EphemerisPriorityRegistry* priorities,
    EphemerisBlockCache* cache
) {
    service->set_catalog(catalog);
    service->set_priorities(priorities);
    service->set_cache(cache);
}

bool eval_x(EphemerisService* service, int target_id, double* out_x, int* out_method_id) {
    EphemerisResult result;
    if (service->eval_state(make_request(target_id), &result, 0) != taiyin::TAIYIN_STATUS_OK) {
        return false;
    }
    if (out_x) {
        *out_x = result.state.position_au.x;
    }
    if (out_method_id) {
        *out_method_id = result.descriptor.method_id;
    }
    return true;
}

void test_memory_cached_hit_survives_unregister(int* failures) {
    clear_custom_ephemeris_sources();
    int clone_count = 0;
    int destroy_count = 0;
    uint64_t source_id = 0;
    EphemerisBlockDescriptor descriptor;
    expect_true(register_memory_source(920100, METHOD_A, 1.0, false, &clone_count, &destroy_count, &source_id, &descriptor),
                "register memory source for unregister cache hit", failures);

    EphemerisBlockCatalog catalog;
    EphemerisPriorityRegistry priorities;
    EphemerisBlockCache cache(1024 * 1024);
    EphemerisService service;
    expect_true(catalog.add(descriptor), "add unregister cache-hit descriptor", failures);
    expect_true(priorities.set_global_method_priority(METHOD_A, 100), "set unregister cache-hit priority", failures);
    bind_service(&service, &catalog, &priorities, &cache);

    double x = 0.0;
    expect_true(eval_x(&service, 920100, &x, 0), "initial memory eval before unregister", failures);
    expect_near(x, 1.0, 1.0e-12, "initial memory eval value", failures);
    expect_true(unregister_custom_ephemeris_source(source_id), "unregister memory source", failures);
    expect_true(eval_x(&service, 920100, &x, 0), "cache hit survives unregister", failures);
    expect_near(x, 1.0, 1.0e-12, "cache hit value after unregister", failures);

    EphemerisBlockDescriptor missing;
    expect_false(make_custom_ephemeris_descriptor(source_id, &missing), "descriptor lookup fails after unregister", failures);
    cache.clear();
    clear_custom_ephemeris_sources();
}

void test_memory_reload_fails_after_unregister(int* failures) {
    clear_custom_ephemeris_sources();
    int clone_a = 0;
    int destroy_a = 0;
    int clone_b = 0;
    int destroy_b = 0;
    uint64_t source_a = 0;
    uint64_t source_b = 0;
    EphemerisBlockDescriptor descriptor_a;
    EphemerisBlockDescriptor descriptor_b;
    expect_true(register_memory_source(920200, METHOD_A, 2.0, false, &clone_a, &destroy_a, &source_a, &descriptor_a),
                "register reload-fail source A", failures);
    expect_true(register_memory_source(920201, METHOD_B, 3.0, false, &clone_b, &destroy_b, &source_b, &descriptor_b),
                "register reload-fail source B", failures);

    EphemerisBlockCatalog catalog;
    EphemerisPriorityRegistry priorities;
    EphemerisBlockCache cache(1);
    EphemerisService service;
    expect_true(catalog.add(descriptor_a), "add reload-fail descriptor A", failures);
    expect_true(catalog.add(descriptor_b), "add reload-fail descriptor B", failures);
    expect_true(priorities.set_global_method_priority(METHOD_A, 100), "set reload-fail priority A", failures);
    expect_true(priorities.set_global_method_priority(METHOD_B, 100), "set reload-fail priority B", failures);
    bind_service(&service, &catalog, &priorities, &cache);

    double x = 0.0;
    expect_true(eval_x(&service, 920200, &x, 0), "load source A before unregister", failures);
    expect_true(unregister_custom_ephemeris_source(source_a), "unregister source A before reload", failures);
    expect_true(eval_x(&service, 920201, &x, 0), "load source B to evict A", failures);
    expect_false(eval_x(&service, 920200, &x, 0), "reload A fails after unregister", failures);
    cache.clear();
    clear_custom_ephemeris_sources();
}

void test_cached_hits_survive_clear_registry(int* failures) {
    clear_custom_ephemeris_sources();
    int clone_a = 0;
    int destroy_a = 0;
    int clone_b = 0;
    int destroy_b = 0;
    uint64_t source_a = 0;
    uint64_t source_b = 0;
    EphemerisBlockDescriptor descriptor_a;
    EphemerisBlockDescriptor descriptor_b;
    expect_true(register_memory_source(920300, METHOD_A, 4.0, false, &clone_a, &destroy_a, &source_a, &descriptor_a),
                "register clear-registry source A", failures);
    expect_true(register_memory_source(920301, METHOD_B, 5.0, false, &clone_b, &destroy_b, &source_b, &descriptor_b),
                "register clear-registry source B", failures);

    EphemerisBlockCatalog catalog;
    EphemerisPriorityRegistry priorities;
    EphemerisBlockCache cache(1024 * 1024);
    EphemerisService service;
    expect_true(catalog.add(descriptor_a), "add clear-registry descriptor A", failures);
    expect_true(catalog.add(descriptor_b), "add clear-registry descriptor B", failures);
    expect_true(priorities.set_global_method_priority(METHOD_A, 100), "set clear-registry priority A", failures);
    expect_true(priorities.set_global_method_priority(METHOD_B, 100), "set clear-registry priority B", failures);
    bind_service(&service, &catalog, &priorities, &cache);

    double x = 0.0;
    expect_true(eval_x(&service, 920300, &x, 0), "load clear-registry source A", failures);
    expect_true(eval_x(&service, 920301, &x, 0), "load clear-registry source B", failures);
    clear_custom_ephemeris_sources();
    expect_true(eval_x(&service, 920300, &x, 0), "cache hit A survives registry clear", failures);
    expect_near(x, 4.0, 1.0e-12, "cache hit A value after registry clear", failures);
    expect_true(eval_x(&service, 920301, &x, 0), "cache hit B survives registry clear", failures);
    expect_near(x, 5.0, 1.0e-12, "cache hit B value after registry clear", failures);

    cache.clear();
    expect_false(eval_x(&service, 920300, &x, 0), "reload A fails after registry clear", failures);
}

void test_file_backed_deleted_file_reload_fails(int* failures) {
    clear_custom_ephemeris_sources();
    const std::string path = make_temp_file_path();
    write_lifecycle_file(path, 6.0);

    uint64_t source_file = 0;
    uint64_t source_b = 0;
    EphemerisBlockDescriptor descriptor_file;
    EphemerisBlockDescriptor descriptor_b;
    int clone_b = 0;
    int destroy_b = 0;
    expect_true(register_file_source(920400, METHOD_A, path, &source_file, &descriptor_file),
                "register deleted-file source", failures);
    expect_true(register_memory_source(920401, METHOD_B, 7.0, false, &clone_b, &destroy_b, &source_b, &descriptor_b),
                "register deleted-file eviction source", failures);

    EphemerisBlockCatalog catalog;
    EphemerisPriorityRegistry priorities;
    EphemerisBlockCache cache(1);
    EphemerisService service;
    expect_true(catalog.add(descriptor_file), "add deleted-file descriptor", failures);
    expect_true(catalog.add(descriptor_b), "add deleted-file eviction descriptor", failures);
    expect_true(priorities.set_global_method_priority(METHOD_A, 100), "set deleted-file priority A", failures);
    expect_true(priorities.set_global_method_priority(METHOD_B, 100), "set deleted-file priority B", failures);
    bind_service(&service, &catalog, &priorities, &cache);

    double x = 0.0;
    expect_true(eval_x(&service, 920400, &x, 0), "load file-backed source before delete", failures);
    expect_near(x, 6.0, 1.0e-12, "file-backed value before delete", failures);
    std::remove(path.c_str());
    expect_true(eval_x(&service, 920401, &x, 0), "load eviction source after file delete", failures);
    expect_false(eval_x(&service, 920400, &x, 0), "file-backed reload fails after file delete", failures);

    cache.clear();
    clear_custom_ephemeris_sources();
    std::remove(path.c_str());
}

void test_preferred_unregistered_descriptor_falls_back(int* failures) {
    clear_custom_ephemeris_sources();
    int clone_high = 0;
    int destroy_high = 0;
    int clone_low = 0;
    int destroy_low = 0;
    uint64_t source_high = 0;
    uint64_t source_low = 0;
    EphemerisBlockDescriptor descriptor_high;
    EphemerisBlockDescriptor descriptor_low;
    expect_true(register_memory_source(920500, METHOD_HIGH, 8.0, false, &clone_high, &destroy_high, &source_high, &descriptor_high),
                "register fallback high source", failures);
    expect_true(register_memory_source(920500, METHOD_LOW, 9.0, false, &clone_low, &destroy_low, &source_low, &descriptor_low),
                "register fallback low source", failures);

    EphemerisBlockCatalog catalog;
    EphemerisPriorityRegistry priorities;
    expect_true(catalog.add(descriptor_high), "add fallback high descriptor", failures);
    expect_true(catalog.add(descriptor_low), "add fallback low descriptor", failures);
    expect_true(priorities.set_global_method_priority(METHOD_HIGH, 200), "set fallback high priority", failures);
    expect_true(priorities.set_global_method_priority(METHOD_LOW, 100), "set fallback low priority", failures);

    {
        EphemerisBlockCache cache(1024 * 1024);
        EphemerisService service;
        bind_service(&service, &catalog, &priorities, &cache);
        double x = 0.0;
        int method_id = 0;
        expect_true(eval_x(&service, 920500, &x, &method_id), "fallback initial high eval", failures);
        expect_int(method_id, METHOD_HIGH, "fallback initial method is high priority", failures);
        expect_near(x, 8.0, 1.0e-12, "fallback initial high value", failures);
    }

    expect_true(unregister_custom_ephemeris_source(source_high), "unregister fallback high source", failures);

    EphemerisBlockCache fresh_cache(1024 * 1024);
    EphemerisService fresh_service;
    bind_service(&fresh_service, &catalog, &priorities, &fresh_cache);
    double x = 0.0;
    int method_id = 0;
    expect_true(eval_x(&fresh_service, 920500, &x, &method_id), "fallback eval after high unregister", failures);
    expect_int(method_id, METHOD_LOW, "fallback selected low method", failures);
    expect_near(x, 9.0, 1.0e-12, "fallback low value", failures);

    fresh_cache.clear();
    clear_custom_ephemeris_sources();
}

void test_active_eval_survives_registry_and_cache_clear(int* failures) {
    clear_custom_ephemeris_sources();
    reset_slow_counters();
    int clone_count = 0;
    int destroy_count = 0;
    uint64_t source_id = 0;
    EphemerisBlockDescriptor descriptor;
    expect_true(register_memory_source(920600, METHOD_A, 10.0, true, &clone_count, &destroy_count, &source_id, &descriptor),
                "register active-eval slow source", failures);

    EphemerisBlockCatalog catalog;
    EphemerisPriorityRegistry priorities;
    EphemerisBlockCache cache(1024 * 1024);
    EphemerisService service;
    expect_true(catalog.add(descriptor), "add active-eval descriptor", failures);
    expect_true(priorities.set_global_method_priority(METHOD_A, 100), "set active-eval priority", failures);
    bind_service(&service, &catalog, &priorities, &cache);

    double x = 0.0;
    expect_true(eval_x(&service, 920600, &x, 0), "load active-eval source", failures);
    reset_slow_counters();

    std::atomic<int> read_failures(0);
    std::atomic<bool> eval_done(false);
    std::thread reader([&]() {
        Vector3 position;
        if (!cache.eval_position(descriptor.route_key, JD0, &position)
            || std::fabs(position.x - 10.0) > 1.0e-12) {
            read_failures.fetch_add(1);
        }
        eval_done.store(true);
    });

    while (g_slow_entries.load() == 0) {
        std::this_thread::yield();
    }
    clear_custom_ephemeris_sources();
    cache.clear();
    expect_false(eval_done.load(), "active eval still running after registry/cache clear", failures);

    reader.join();
    expect_int(read_failures.load(), 0, "active eval survives registry/cache clear", failures);
    expect_int(g_slow_exits.load(), 1, "active eval slow callback exits once", failures);
}

void test_invalid_unregister_and_empty_clear(int* failures) {
    clear_custom_ephemeris_sources();
    expect_false(unregister_custom_ephemeris_source(0), "unregister zero source id fails", failures);
    expect_false(unregister_custom_ephemeris_source(999999), "unregister missing source id fails", failures);
    clear_custom_ephemeris_sources();
}

}  // namespace

int main() {
    int failures = 0;
    test_memory_cached_hit_survives_unregister(&failures);
    test_memory_reload_fails_after_unregister(&failures);
    test_cached_hits_survive_clear_registry(&failures);
    test_file_backed_deleted_file_reload_fails(&failures);
    test_preferred_unregistered_descriptor_falls_back(&failures);
    test_active_eval_survives_registry_and_cache_clear(&failures);
    test_invalid_unregister_and_empty_clear(&failures);

    if (failures != 0) {
        std::cerr << failures << " custom source lifecycle test(s) failed\n";
        return 1;
    }
    return 0;
}
