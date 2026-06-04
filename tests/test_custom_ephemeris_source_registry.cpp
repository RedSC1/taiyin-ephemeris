#include "taiyin/angle.h"
#include "taiyin/time.h"
#include "taiyin/internal/custom_ephemeris_source_registry.h"
#include "taiyin/internal/descriptor_loader.h"
#include "taiyin/internal/ephemeris_cache.h"
#include "taiyin/runtime/ephemeris_service.h"

#include <cmath>
#include <cstddef>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <new>
#include <string>
#include <unistd.h>

namespace {

const double JD0 = taiyin::JD_J2000;
const int CUSTOM_METHOD_ID = 87001;

struct ZiQiData {
    double epoch_jd_tdb;
    double longitude_at_epoch_rad;
    double period_days;
    double radius_au;
    int* clone_count;
    int* destroy_count;
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

void expect_near(double actual, double expected, double tolerance, const char* label, int* failures) {
    if (std::fabs(actual - expected) > tolerance) {
        std::cerr << "FAIL: " << label << ": actual=" << actual << " expected=" << expected
                  << " tolerance=" << tolerance << "\n";
        ++(*failures);
    }
}

bool ziqi_clone(const void* source_data, size_t source_bytes, void** out_data, size_t* out_bytes) {
    if (out_data) {
        *out_data = 0;
    }
    if (out_bytes) {
        *out_bytes = 0;
    }
    if (!source_data || source_bytes != sizeof(ZiQiData) || !out_data || !out_bytes) {
        return false;
    }

    ZiQiData* clone = new (std::nothrow) ZiQiData(*static_cast<const ZiQiData*>(source_data));
    if (!clone) {
        return false;
    }
    if (clone->clone_count) {
        ++(*clone->clone_count);
    }
    *out_data = clone;
    *out_bytes = sizeof(ZiQiData);
    return true;
}

void ziqi_destroy(void* data) {
    ZiQiData* ziqi = static_cast<ZiQiData*>(data);
    if (ziqi && ziqi->destroy_count) {
        ++(*ziqi->destroy_count);
    }
    delete ziqi;
}

int g_file_load_count = 0;
int g_file_destroy_count = 0;

std::string make_temp_file_path() {
    char templ[] = "/tmp/taiyin-custom-file-XXXXXX";
    int fd = mkstemp(templ);
    if (fd >= 0) {
        close(fd);
    }
    return std::string(templ);
}

void write_ziqi_file(
    const std::string& path,
    double epoch_jd_tdb,
    double longitude_at_epoch_rad,
    double period_days,
    double radius_au
) {
    std::ofstream file(path.c_str(), std::ios::out | std::ios::trunc);
    file.precision(17);
    file << epoch_jd_tdb << "\n"
         << longitude_at_epoch_rad << "\n"
         << period_days << "\n"
         << radius_au << "\n";
}

bool load_ziqi_file(const char* path, void** out_data, size_t* out_bytes) {
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

    ZiQiData* data = new (std::nothrow) ZiQiData();
    if (!data) {
        return false;
    }
    data->clone_count = 0;
    data->destroy_count = &g_file_destroy_count;
    if (!(file >> data->epoch_jd_tdb
          >> data->longitude_at_epoch_rad
          >> data->period_days
          >> data->radius_au)) {
        delete data;
        return false;
    }

    ++g_file_load_count;
    *out_data = data;
    *out_bytes = sizeof(ZiQiData);
    return true;
}

double ziqi_longitude(double jd_tdb, const ZiQiData* ziqi) {
    return ziqi->longitude_at_epoch_rad
        + 2.0 * taiyin::TAIYIN_PI * (jd_tdb - ziqi->epoch_jd_tdb) / ziqi->period_days;
}

bool ziqi_position(double jd_tdb, const void* data, taiyin::Vector3* out) {
    const ZiQiData* ziqi = static_cast<const ZiQiData*>(data);
    if (!ziqi || !out || ziqi->period_days <= 0.0) {
        return false;
    }
    const double longitude = ziqi_longitude(jd_tdb, ziqi);
    out->x = ziqi->radius_au * std::cos(longitude);
    out->y = ziqi->radius_au * std::sin(longitude);
    out->z = 0.0;
    return true;
}

bool ziqi_velocity(double jd_tdb, const void* data, taiyin::Vector3* out) {
    const ZiQiData* ziqi = static_cast<const ZiQiData*>(data);
    if (!ziqi || !out || ziqi->period_days <= 0.0) {
        return false;
    }
    const double longitude = ziqi_longitude(jd_tdb, ziqi);
    const double rate = 2.0 * taiyin::TAIYIN_PI / ziqi->period_days;
    out->x = -ziqi->radius_au * rate * std::sin(longitude);
    out->y = ziqi->radius_au * rate * std::cos(longitude);
    out->z = 0.0;
    return true;
}

bool ziqi_acceleration(double jd_tdb, const void* data, taiyin::Vector3* out) {
    const ZiQiData* ziqi = static_cast<const ZiQiData*>(data);
    if (!ziqi || !out || ziqi->period_days <= 0.0) {
        return false;
    }
    const double longitude = ziqi_longitude(jd_tdb, ziqi);
    const double rate = 2.0 * taiyin::TAIYIN_PI / ziqi->period_days;
    out->x = -ziqi->radius_au * rate * rate * std::cos(longitude);
    out->y = -ziqi->radius_au * rate * rate * std::sin(longitude);
    out->z = 0.0;
    return true;
}

taiyin::internal::CustomEphemerisSourceDefinition make_definition(
    int target_id,
    const ZiQiData* data
) {
    taiyin::internal::CustomEphemerisSourceDefinition definition;
    definition.target_id = target_id;
    definition.center_id = 399;
    definition.method_id = CUSTOM_METHOD_ID;
    definition.frame = taiyin::internal::EphemerisFrame::IcrfJ2000Equatorial;
    definition.jd_tdb_start = JD0 - 12000.0;
    definition.jd_tdb_end = JD0 + 12000.0;
    definition.data = data;
    definition.bytes = sizeof(ZiQiData);
    definition.position = &ziqi_position;
    definition.velocity = &ziqi_velocity;
    definition.acceleration = &ziqi_acceleration;
    definition.clone = &ziqi_clone;
    definition.destroy = &ziqi_destroy;
    return definition;
}

void test_custom_source_cache_eviction_reload(int* failures) {
    using namespace taiyin::internal;
    using namespace taiyin::runtime;

    clear_custom_ephemeris_sources();

    int a_clone_count = 0;
    int a_destroy_count = 0;
    int b_clone_count = 0;
    int b_destroy_count = 0;

    ZiQiData source_a;
    source_a.epoch_jd_tdb = JD0;
    source_a.longitude_at_epoch_rad = 0.0;
    source_a.period_days = 28.0 * 365.25;
    source_a.radius_au = 1.0;
    source_a.clone_count = &a_clone_count;
    source_a.destroy_count = &a_destroy_count;

    ZiQiData source_b;
    source_b.epoch_jd_tdb = JD0;
    source_b.longitude_at_epoch_rad = taiyin::TAIYIN_PI / 2.0;
    source_b.period_days = 19.0 * 365.25;
    source_b.radius_au = 2.0;
    source_b.clone_count = &b_clone_count;
    source_b.destroy_count = &b_destroy_count;

    uint64_t source_id_a = 0;
    uint64_t source_id_b = 0;
    expect_true(register_custom_ephemeris_source(make_definition(900002, &source_a), &source_id_a), "register custom source A", failures);
    expect_true(register_custom_ephemeris_source(make_definition(900003, &source_b), &source_id_b), "register custom source B", failures);
    expect_true(source_id_a != 0 && source_id_b != 0 && source_id_a != source_id_b, "custom source ids", failures);
    expect_true(a_clone_count == 1, "registry cloned source A", failures);
    expect_true(b_clone_count == 1, "registry cloned source B", failures);

    EphemerisBlockDescriptor descriptor_a;
    EphemerisBlockDescriptor descriptor_b;
    expect_true(make_custom_ephemeris_descriptor(source_id_a, &descriptor_a), "make custom descriptor A", failures);
    expect_true(make_custom_ephemeris_descriptor(source_id_b, &descriptor_b), "make custom descriptor B", failures);
    expect_true(descriptor_a.format == EphemerisBlockFormat::Custom, "custom descriptor format", failures);
    expect_true(descriptor_a.path.empty(), "custom descriptor has no path", failures);

    EphemerisBlockCatalog catalog;
    expect_true(catalog.add(descriptor_a), "add custom descriptor A", failures);
    expect_true(catalog.add(descriptor_b), "add custom descriptor B", failures);

    EphemerisBlockCache cache(1);
    EphemerisService service;
    service.set_catalog(&catalog);
    service.set_cache(&cache);

    EphemerisRequest request_a;
    request_a.target_id = 900002;
    request_a.center_id = 399;
    request_a.frame = EphemerisFrame::IcrfJ2000Equatorial;
    request_a.jd_tdb = JD0;

    EphemerisResult result_a;
    expect_true(service.eval_state(request_a, &result_a), "eval custom source A", failures);
    expect_false(result_a.cache_hit, "first custom source A eval is cache miss", failures);
    expect_true(cache.entry_count() == 1, "cache has source A entry", failures);
    expect_true(cache.contains(descriptor_a.route_key), "cache contains source A route", failures);
    expect_near(result_a.state.position_au.x, 1.0, 1e-14, "source A position x", failures);
    expect_near(result_a.state.position_au.y, 0.0, 1e-14, "source A position y", failures);
    expect_true(a_clone_count == 2, "cache cloned source A", failures);

    EphemerisResult result_a_hit;
    expect_true(service.eval_state(request_a, &result_a_hit), "eval custom source A cache hit", failures);
    expect_true(result_a_hit.cache_hit, "second custom source A eval is cache hit", failures);
    expect_true(a_clone_count == 2, "cache hit does not clone source A", failures);

    EphemerisRequest request_b = request_a;
    request_b.target_id = 900003;
    EphemerisResult result_b;
    expect_true(service.eval_state(request_b, &result_b), "eval custom source B", failures);
    expect_false(result_b.cache_hit, "first custom source B eval is cache miss", failures);
    expect_true(cache.entry_count() == 1, "tiny cache keeps one custom entry after source B", failures);
    expect_false(cache.contains(descriptor_a.route_key), "source A evicted after source B", failures);
    expect_true(cache.contains(descriptor_b.route_key), "cache contains source B route", failures);
    expect_true(a_destroy_count == 1, "eviction destroyed source A cache clone", failures);
    expect_true(b_clone_count == 2, "cache cloned source B", failures);
    expect_near(result_b.state.position_au.x, 0.0, 1e-14, "source B position x", failures);
    expect_near(result_b.state.position_au.y, 2.0, 1e-14, "source B position y", failures);

    request_a.jd_tdb = JD0 + 0.25 * source_a.period_days;
    EphemerisResult result_a_reload;
    expect_true(service.eval_state(request_a, &result_a_reload), "reload evicted custom source A", failures);
    expect_false(result_a_reload.cache_hit, "reloaded custom source A is cache miss", failures);
    expect_true(cache.entry_count() == 1, "tiny cache keeps one custom entry after reload", failures);
    expect_true(cache.contains(descriptor_a.route_key), "cache contains reloaded source A route", failures);
    expect_false(cache.contains(descriptor_b.route_key), "source B evicted by source A reload", failures);
    expect_true(a_clone_count == 3, "reload cloned source A again", failures);
    expect_true(b_destroy_count == 1, "eviction destroyed source B cache clone", failures);
    expect_near(result_a_reload.state.position_au.x, 0.0, 1e-12, "reloaded source A quarter x", failures);
    expect_near(result_a_reload.state.position_au.y, 1.0, 1e-12, "reloaded source A quarter y", failures);

    cache.clear();
    expect_true(a_destroy_count == 2, "cache clear destroyed reloaded source A clone", failures);
    expect_true(b_destroy_count == 1, "cache clear did not destroy already-evicted source B clone again", failures);

    clear_custom_ephemeris_sources();
    expect_true(a_destroy_count == 3, "registry clear destroyed source A registry clone", failures);
    expect_true(b_destroy_count == 2, "registry clear destroyed source B registry clone", failures);

    EphemerisResult missing_result;
    expect_false(service.eval_state(request_a, &missing_result), "missing registry source cannot reload descriptor", failures);
}

void test_file_backed_custom_source_reload(int* failures) {
    using namespace taiyin::internal;
    using namespace taiyin::runtime;

    clear_custom_ephemeris_sources();
    g_file_load_count = 0;
    g_file_destroy_count = 0;

    const std::string path = make_temp_file_path();
    write_ziqi_file(path, JD0, 0.0, 365.25, 3.0);

    CustomEphemerisFileSourceDefinition file_definition;
    file_definition.target_id = 900005;
    file_definition.center_id = 399;
    file_definition.method_id = CUSTOM_METHOD_ID;
    file_definition.frame = EphemerisFrame::IcrfJ2000Equatorial;
    file_definition.jd_tdb_start = JD0 - 12000.0;
    file_definition.jd_tdb_end = JD0 + 12000.0;
    file_definition.path = path.c_str();
    file_definition.load = &load_ziqi_file;
    file_definition.position = &ziqi_position;
    file_definition.velocity = &ziqi_velocity;
    file_definition.acceleration = &ziqi_acceleration;
    file_definition.destroy = &ziqi_destroy;

    uint64_t file_source_id = 0;
    expect_true(register_custom_ephemeris_file_source(file_definition, &file_source_id), "register file-backed custom source", failures);

    EphemerisBlockDescriptor file_descriptor;
    expect_true(make_custom_ephemeris_descriptor(file_source_id, &file_descriptor), "make file-backed custom descriptor", failures);
    expect_true(file_descriptor.format == EphemerisBlockFormat::Custom, "file-backed custom descriptor format", failures);
    expect_true(file_descriptor.path == path, "file-backed descriptor stores path", failures);

    ZiQiData evicting_source;
    evicting_source.epoch_jd_tdb = JD0;
    evicting_source.longitude_at_epoch_rad = taiyin::TAIYIN_PI / 2.0;
    evicting_source.period_days = 365.25;
    evicting_source.radius_au = 5.0;
    evicting_source.clone_count = 0;
    evicting_source.destroy_count = 0;
    uint64_t evicting_source_id = 0;
    expect_true(register_custom_ephemeris_source(make_definition(900006, &evicting_source), &evicting_source_id), "register file eviction source", failures);
    EphemerisBlockDescriptor evicting_descriptor;
    expect_true(make_custom_ephemeris_descriptor(evicting_source_id, &evicting_descriptor), "make file eviction descriptor", failures);

    EphemerisBlockCatalog catalog;
    expect_true(catalog.add(file_descriptor), "add file-backed custom descriptor", failures);
    expect_true(catalog.add(evicting_descriptor), "add file eviction descriptor", failures);

    EphemerisBlockCache cache(1);
    EphemerisService service;
    service.set_catalog(&catalog);
    service.set_cache(&cache);

    EphemerisRequest request;
    request.target_id = 900005;
    request.center_id = 399;
    request.frame = EphemerisFrame::IcrfJ2000Equatorial;
    request.jd_tdb = JD0;

    EphemerisResult first;
    expect_true(service.eval_state(request, &first), "eval file-backed custom source", failures);
    expect_false(first.cache_hit, "first file-backed eval is cache miss", failures);
    expect_true(g_file_load_count == 1, "file-backed source loaded once", failures);
    expect_true(cache.contains(file_descriptor.route_key), "cache contains file-backed route", failures);
    expect_near(first.state.position_au.x, 3.0, 1e-14, "file-backed first radius", failures);

    EphemerisResult hit;
    expect_true(service.eval_state(request, &hit), "eval file-backed custom cache hit", failures);
    expect_true(hit.cache_hit, "second file-backed eval is cache hit", failures);
    expect_true(g_file_load_count == 1, "cache hit does not reload file", failures);

    EphemerisRequest evicting_request = request;
    evicting_request.target_id = 900006;
    EphemerisResult evicting;
    expect_true(service.eval_state(evicting_request, &evicting), "evict file-backed custom source", failures);
    expect_false(cache.contains(file_descriptor.route_key), "file-backed source evicted", failures);
    expect_true(g_file_destroy_count == 1, "file-backed cache clone destroyed on eviction", failures);

    write_ziqi_file(path, JD0, 0.0, 365.25, 4.0);
    EphemerisResult reloaded;
    expect_true(service.eval_state(request, &reloaded), "reload file-backed custom source from descriptor path", failures);
    expect_false(reloaded.cache_hit, "file-backed reload is cache miss", failures);
    expect_true(g_file_load_count == 2, "file-backed reload reads file again", failures);
    expect_true(cache.contains(file_descriptor.route_key), "cache contains reloaded file-backed route", failures);
    expect_near(reloaded.state.position_au.x, 4.0, 1e-14, "file-backed reloaded radius", failures);

    cache.clear();
    expect_true(g_file_destroy_count == 2, "file-backed cache clear destroys reloaded clone", failures);
    clear_custom_ephemeris_sources();
    expect_true(g_file_destroy_count == 2, "registry clear does not destroy file-backed parsed data", failures);
    std::remove(path.c_str());
}

void test_default_byte_clone_source(int* failures) {
    using namespace taiyin::internal;

    clear_custom_ephemeris_sources();
    ZiQiData source;
    source.epoch_jd_tdb = JD0;
    source.longitude_at_epoch_rad = 0.0;
    source.period_days = 365.25;
    source.radius_au = 1.0;
    source.clone_count = 0;
    source.destroy_count = 0;

    CustomEphemerisSourceDefinition definition = make_definition(900004, &source);
    definition.clone = 0;
    definition.destroy = 0;

    uint64_t source_id = 0;
    expect_true(register_custom_ephemeris_source(definition, &source_id), "register default byte-cloned source", failures);
    EphemerisBlockDescriptor descriptor;
    expect_true(make_custom_ephemeris_descriptor(source_id, &descriptor), "make default byte-cloned descriptor", failures);

    EphemerisBlockCache cache(1024 * 1024);
    taiyin::CartesianState state;
    expect_true(eval_descriptor_state(descriptor, &cache, JD0, &state), "eval default byte-cloned descriptor", failures);
    expect_near(state.position_au.x, 1.0, 1e-14, "default byte clone position x", failures);

    cache.clear();
    clear_custom_ephemeris_sources();
}

}  // namespace

int main() {
    int failures = 0;
    test_custom_source_cache_eviction_reload(&failures);
    test_file_backed_custom_source_reload(&failures);
    test_default_byte_clone_source(&failures);

    if (failures == 0) {
        std::cout << "test_custom_ephemeris_source_registry: ALL TESTS PASSED\n";
        return 0;
    }
    std::cerr << failures << " test_custom_ephemeris_source_registry failure(s)\n";
    return 1;
}
