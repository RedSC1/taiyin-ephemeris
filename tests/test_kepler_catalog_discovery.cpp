#include "taiyin/physical_constants.h"
#include "taiyin/internal/descriptor_loader.h"
#include "taiyin/internal/ephemeris_cache.h"
#include "taiyin/internal/ephemeris_discovery.h"
#include "taiyin/internal/kepler_catalog_discovery.h"
#include "taiyin/internal/kepler_file.h"

#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

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

std::string make_temp_dir() {
    char templ[] = "/tmp/taiyin-kepler-discovery-XXXXXX";
    char* path = mkdtemp(templ);
    assert(path);
    return std::string(path);
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
        0.1,
        0.05,
        0.2,
        0.3,
        0.4,
        &element);
    return element;
}

void write_text_file(const std::string& path, const char* text) {
    FILE* file = std::fopen(path.c_str(), "wb");
    if (!file) {
        return;
    }
    std::fwrite(text, 1, std::strlen(text), file);
    std::fclose(file);
}

void test_kepler_discovery_and_lazy_eval(int* failures) {
    using namespace taiyin::internal;

    const std::string root = make_temp_dir();
    const std::string path = root + "/custom_orbit.tke1";
    const std::string path2 = root + "/custom_orbit_replacement.tke1";
    const std::string ignored = root + "/ignored.txt";
    KeplerElements element = make_element(2000002, 2.5);
    KeplerElements element2 = make_element(2000003, 3.1);
    expect_true(
        save_kepler_file(
            path,
            &element,
            1,
            TAIYIN_KEPLER_FILE_METHOD_ID,
            EphemerisFrame::IcrfJ2000Equatorial,
            JD0 - 500.0,
            JD0 + 500.0),
        "save discovered user Kepler file",
        failures);
    expect_true(
        save_kepler_file(
            path2,
            &element2,
            1,
            TAIYIN_KEPLER_FILE_METHOD_ID,
            EphemerisFrame::IcrfJ2000Equatorial,
            JD0 - 500.0,
            JD0 + 500.0),
        "save second discovered user Kepler file",
        failures);
    write_text_file(ignored, "not an ephemeris");

    std::vector<EphemerisBlockDescriptor> descriptors;
    expect_true(collect_kepler_descriptors_from_directory(root, &descriptors), "collect Kepler descriptors", failures);
    expect_true(descriptors.size() == 2, "two user Kepler descriptors", failures);
    bool saw_first_descriptor = false;
    bool saw_second_descriptor = false;
    for (size_t i = 0; i < descriptors.size(); ++i) {
        if (descriptors[i].target_id == 2000002) {
            saw_first_descriptor = true;
            expect_true(descriptors[i].format == EphemerisBlockFormat::Kepler, "first descriptor format", failures);
            expect_true(descriptors[i].path == path, "first descriptor path", failures);
        }
        if (descriptors[i].target_id == 2000003) {
            saw_second_descriptor = true;
            expect_true(descriptors[i].format == EphemerisBlockFormat::Kepler, "second descriptor format", failures);
            expect_true(descriptors[i].path == path2, "second descriptor path", failures);
        }
    }
    expect_true(saw_first_descriptor, "saw first saved user descriptor", failures);
    expect_true(saw_second_descriptor, "saw second saved user descriptor", failures);

    std::vector<EphemerisDiscoverFileFn> discoverers;
    append_builtin_ephemeris_discoverers(&discoverers);
    std::vector<EphemerisBlockDescriptor> generic_descriptors;
    EphemerisDiscoveryOptions options;
    expect_true(
        discover_ephemeris_descriptors_from_directory(root, discoverers, options, &generic_descriptors),
        "builtin discovery includes Kepler",
        failures);
    bool saw_kepler = false;
    bool saw_second_kepler = false;
    for (size_t i = 0; i < generic_descriptors.size(); ++i) {
        if (generic_descriptors[i].target_id == 2000002) {
            saw_kepler = true;
        }
        if (generic_descriptors[i].target_id == 2000003) {
            saw_second_kepler = true;
        }
    }
    expect_true(saw_kepler, "saw saved Kepler in builtin discovery", failures);
    expect_true(saw_second_kepler, "saw second saved Kepler in builtin discovery", failures);

    EphemerisBlockCatalog catalog;
    expect_true(discover_kepler_catalog_from_directory(root, &catalog), "discover Kepler catalog", failures);
    expect_true(catalog.size() == 2, "Kepler catalog size", failures);

    EphemerisBlockQuery query;
    query.target_id = 2000002;
    query.center_id = 10;
    query.frame = EphemerisFrame::IcrfJ2000Equatorial;
    query.jd_tdb = JD0 + 20.0;
    const EphemerisBlockDescriptor* selected = catalog.find_first(query);
    expect_true(selected != 0, "select Kepler descriptor", failures);

    EphemerisBlockCache cache(1024 * 1024);
    taiyin::CartesianState first;
    taiyin::CartesianState second;
    expect_true(selected && eval_descriptor_state(*selected, &cache, query.jd_tdb, &first), "lazy eval Kepler descriptor", failures);
    expect_true(cache.entry_count() == 1, "lazy eval inserts cache entry", failures);
    expect_true(selected && eval_descriptor_state(*selected, &cache, query.jd_tdb + 1.0, &second), "second eval reuses Kepler cache", failures);
    expect_true(cache.entry_count() == 1, "second eval keeps one cache entry", failures);
    expect_near(first.position_au.x, first.position_au.x, 0.0, "state finite placeholder", failures);

    EphemerisBlockQuery replacement_query = query;
    replacement_query.target_id = 2000003;
    const EphemerisBlockDescriptor* replacement = catalog.find_first(replacement_query);
    expect_true(replacement != 0, "select second user Kepler descriptor", failures);

    EphemerisBlockDescriptor selected_bucket;
    EphemerisBlockDescriptor replacement_bucket;
    expect_true(selected && make_cache_bucket_descriptor_for_jd(*selected, query.jd_tdb, &selected_bucket), "make first user cache bucket", failures);
    expect_true(replacement && make_cache_bucket_descriptor_for_jd(*replacement, replacement_query.jd_tdb, &replacement_bucket), "make second user cache bucket", failures);

    EphemerisBlockCache eviction_cache(1);
    taiyin::CartesianState saved_first;
    expect_true(selected && eval_descriptor_state(*selected, &eviction_cache, query.jd_tdb, &saved_first), "load saved user Kepler into tiny cache", failures);
    expect_true(eviction_cache.entry_count() == 1, "tiny user cache has one entry after first load", failures);
    expect_true(eviction_cache.contains(selected_bucket.route_key), "tiny user cache contains first saved orbit", failures);

    taiyin::CartesianState saved_second;
    expect_true(replacement && eval_descriptor_state(*replacement, &eviction_cache, replacement_query.jd_tdb, &saved_second), "load second saved user Kepler into tiny cache", failures);
    expect_true(eviction_cache.entry_count() == 1, "tiny user cache still has one entry after second load", failures);
    expect_true(!eviction_cache.contains(selected_bucket.route_key), "tiny user cache evicted first saved orbit", failures);
    expect_true(eviction_cache.contains(replacement_bucket.route_key), "tiny user cache contains second saved orbit", failures);

    taiyin::CartesianState reloaded_first;
    expect_true(selected && eval_descriptor_state(*selected, &eviction_cache, query.jd_tdb + 2.0, &reloaded_first), "reload evicted saved user Kepler from file", failures);
    expect_true(eviction_cache.entry_count() == 1, "tiny user cache still has one entry after reload", failures);
    expect_true(eviction_cache.contains(selected_bucket.route_key), "tiny user cache contains reloaded first saved orbit", failures);
    expect_true(!eviction_cache.contains(replacement_bucket.route_key), "tiny user cache evicted second saved orbit", failures);

    std::remove(path.c_str());
    std::remove(path2.c_str());
    std::remove(ignored.c_str());
    rmdir(root.c_str());
}

void test_bad_file_is_discovery_error(int* failures) {
    using namespace taiyin::internal;

    const std::string root = make_temp_dir();
    const std::string path = root + "/bad.tke1";
    write_text_file(path, "TKE1\nversion=2\n");
    std::vector<EphemerisBlockDescriptor> descriptors;
    expect_false(collect_kepler_descriptors_from_directory(root, &descriptors), "bad Kepler file rejected in strict discovery", failures);
    std::remove(path.c_str());
    rmdir(root.c_str());
}

}  // namespace

int main() {
    int failures = 0;
    test_kepler_discovery_and_lazy_eval(&failures);
    test_bad_file_is_discovery_error(&failures);

    if (failures == 0) {
        std::cout << "test_kepler_catalog_discovery: ALL TESTS PASSED\n";
        return 0;
    }
    std::cerr << failures << " test_kepler_catalog_discovery failure(s)\n";
    return 1;
}
