#include "taiyin/internal/ephemeris_discovery.h"
#include "taiyin/internal/star_file.h"
#include "taiyin/star_catalog_tsc1.h"
#include "taiyin/star_provider_tsf1.h"

#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iostream>
#include <limits>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

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

void expect_near(double actual, double expected, double tolerance, const char* label, int* failures) {
    if (std::fabs(actual - expected) > tolerance) {
        std::cerr << "FAIL: " << label << ": actual=" << actual << " expected=" << expected
                  << " tolerance=" << tolerance << "\n";
        ++(*failures);
    }
}

std::string make_temp_dir() {
    char templ[] = "/tmp/taiyin-star-file-XXXXXX";
    char* path = mkdtemp(templ);
    assert(path);
    return std::string(path);
}

void write_text_file(const std::string& path, const char* text) {
    std::ofstream file(path.c_str(), std::ios::out | std::ios::trunc);
    file << text;
}

taiyin::internal::Tsf1StarEntry make_entry(
    const std::string& id,
    const std::string& name,
    double ra_deg,
    double dec_deg
) {
    taiyin::internal::Tsf1StarEntry entry;
    entry.id = id;
    entry.name = name;
    entry.aliases.push_back(name + " Alias");
    entry.ra_deg = ra_deg;
    entry.dec_deg = dec_deg;
    entry.pm_ra_mas_yr = 1.0;
    entry.pm_dec_mas_yr = -2.0;
    entry.parallax_mas = 0.0;
    entry.radial_velocity_km_s = 0.0;
    entry.reference_epoch = 2000.0;
    entry.magnitude = 5.5;
    return entry;
}

void test_save_load_roundtrip(int* failures) {
    using namespace taiyin::internal;

    const std::string root = make_temp_dir();
    const std::string path = root + "/custom_stars.tsf1";
    Tsf1StarEntry entries[2];
    entries[0] = make_entry("my_custom_star", "My Custom Star", 180.0, 45.0);
    entries[0].aliases.push_back("custom-one");
    entries[1] = make_entry("another_star", "Another Star", 90.0, -30.0);
    entries[1].magnitude = std::numeric_limits<double>::quiet_NaN();

    expect_true(save_star_file(path, entries, 2), "save TSF1 file", failures);

    std::vector<Tsf1StarEntry> loaded;
    expect_true(load_star_file(path, &loaded), "load TSF1 file", failures);
    expect_true(loaded.size() == 2, "loaded two stars", failures);
    if (loaded.size() == 2) {
        expect_true(loaded[0].id == "my_custom_star", "first canonical id", failures);
        expect_true(loaded[0].name == "My Custom Star", "first display name", failures);
        expect_near(loaded[0].ra_deg, 180.0, 0.0, "first RA", failures);
        expect_near(loaded[0].dec_deg, 45.0, 0.0, "first Dec", failures);
        expect_true(loaded[0].aliases.size() == 2, "first aliases", failures);
        expect_true(loaded[1].id == "another_star", "second canonical id", failures);
    }

    std::remove(path.c_str());
    rmdir(root.c_str());
}

void test_default_user_directory(int* failures) {
    using namespace taiyin::internal;

    const std::string root = make_temp_dir();
    const std::string expected_dir = root + "/stars/user";
    const std::string expected_path = expected_dir + "/user_custom_stars.tsf1";
    Tsf1StarEntry entry = make_entry("default_dir_star", "Default Dir Star", 10.0, 20.0);
    std::string saved_path;
    expect_true(
        save_user_star_file(root, "User Custom Stars", &entry, 1, &saved_path),
        "save user TSF1 default path",
        failures);
    expect_true(make_user_star_directory(root) == expected_dir, "make user star directory", failures);
    expect_true(saved_path == expected_path, "make user star file path", failures);

    std::vector<Tsf1StarEntry> loaded;
    expect_true(load_star_file(saved_path, &loaded), "load saved user TSF1 file", failures);
    expect_true(loaded.size() == 1, "loaded one user star", failures);
    expect_true(default_user_star_directory() == "data/stars/user", "default user star directory", failures);

    std::remove(saved_path.c_str());
    rmdir(expected_dir.c_str());
    rmdir((root + "/stars").c_str());
    rmdir(root.c_str());
}

void test_invalid_files(int* failures) {
    using namespace taiyin::internal;

    const std::string root = make_temp_dir();
    std::vector<Tsf1StarEntry> loaded;

    const std::string bad_magic = root + "/bad_magic.tsf1";
    write_text_file(bad_magic, "NOPE\nversion=1\nstar_count=1\n");
    expect_false(load_star_file(bad_magic, &loaded), "reject bad TSF1 magic", failures);

    const std::string bad_version = root + "/bad_version.tsf1";
    write_text_file(bad_version, "TSF1\nversion=2\nstar_count=1\nstar.0.id=x\nstar.0.ra_deg=0\nstar.0.dec_deg=0\n");
    expect_false(load_star_file(bad_version, &loaded), "reject bad TSF1 version", failures);

    const std::string missing_required = root + "/missing_required.tsf1";
    write_text_file(missing_required, "TSF1\nversion=1\nstar_count=1\nstar.0.id=x\nstar.0.ra_deg=0\n");
    expect_false(load_star_file(missing_required, &loaded), "reject missing required TSF1 field", failures);

    std::remove(bad_magic.c_str());
    std::remove(bad_version.c_str());
    std::remove(missing_required.c_str());
    rmdir(root.c_str());
}

void test_build_tsc1_bytes_and_provider(int* failures) {
    using namespace taiyin;
    using namespace taiyin::internal;

    const std::string root = make_temp_dir();
    const std::string path = root + "/provider_stars.tsf1";
    Tsf1StarEntry entries[2];
    entries[0] = make_entry("cache_star", "Cache Star", 120.0, 10.0);
    entries[0].aliases.push_back("cache-star-alias");
    entries[1] = make_entry("cold_star", "Cold Star", 240.0, -20.0);
    expect_true(save_star_file(path, entries, 2), "save provider TSF1 file", failures);

    std::vector<uint8_t> bytes;
    expect_true(build_tsc1_catalog_bytes_from_star_file(path, &bytes), "build TSC1 bytes from TSF1", failures);
    expect_true(!bytes.empty(), "nonempty TSC1 bytes", failures);

    Tsc1Catalog catalog;
    expect_true(tsc1_catalog_load_from_memory(&catalog, &bytes[0], bytes.size()), "load generated TSC1 bytes", failures);
    expect_true(catalog.header && catalog.header->star_count == 2, "generated TSC1 star count", failures);
    expect_true(tsc1_catalog_find(&catalog, "Cache Star Alias") != 0, "find generated TSC1 alias", failures);
    expect_true(tsc1_catalog_find(&catalog, "cold_star") != 0, "find generated TSC1 canonical", failures);
    tsc1_catalog_destroy(&catalog);

    Tsf1StarProvider provider(1024 * 1024, 77);
    expect_true(provider.load_from_file(path), "load TSF1 provider", failures);
    expect_true(provider.cache_entry_count() == 0, "provider cache starts empty", failures);
    Tsc1ResolvedStar resolved;
    expect_true(provider.resolve("cache star alias", &resolved), "resolve TSF1 provider alias", failures);
    expect_true(resolved.star_id != 0, "resolved provider star id", failures);

    Vector3 position;
    expect_true(provider.eval_position("cache-star-alias", 2451545.0, &position), "eval TSF1 provider position", failures);
    expect_true(std::isfinite(position.x) && std::isfinite(position.y) && std::isfinite(position.z), "provider position finite", failures);
    expect_true(provider.cache_entry_count() == 1, "provider cache has one entry", failures);
    expect_true(provider.contains_cached_star("Cache Star Alias"), "provider contains cached star", failures);

    CartesianState state;
    expect_true(provider.eval_state("cache_star", 2451546.0, &state), "eval TSF1 provider state", failures);
    expect_true(provider.cache_entry_count() == 1, "provider cache reuses alias entry", failures);

    provider.close();
    std::remove(path.c_str());
    rmdir(root.c_str());
}

void test_discovery(int* failures) {
    using namespace taiyin::internal;

    const std::string root = make_temp_dir();
    const std::string path = root + "/discoverable.tsf1";
    const std::string ignored = root + "/ignored.txt";
    Tsf1StarEntry entry = make_entry("discoverable_star", "Discoverable Star", 33.0, -44.0);
    expect_true(save_star_file(path, &entry, 1), "save discoverable TSF1", failures);
    write_text_file(ignored, "not a star file");

    std::vector<EphemerisBlockDescriptor> descriptors;
    expect_true(collect_star_file_descriptors_from_directory(root, &descriptors), "collect TSF1 descriptors", failures);
    expect_true(descriptors.size() == 1, "one TSF1 descriptor", failures);
    if (!descriptors.empty()) {
        expect_true(descriptors[0].format == EphemerisBlockFormat::Tsc1, "TSF1 descriptor format", failures);
        expect_true(descriptors[0].path == path, "TSF1 descriptor path", failures);
        expect_true(descriptors[0].target_id == 0, "TSF1 descriptor target sentinel", failures);
        expect_true(descriptors[0].method_id == taiyin::TSC1_STAR_METHOD_ID, "TSF1 descriptor method", failures);
    }

    std::vector<EphemerisDiscoverFileFn> discoverers;
    append_builtin_ephemeris_discoverers(&discoverers);
    std::vector<EphemerisBlockDescriptor> generic;
    EphemerisDiscoveryOptions options;
    expect_true(discover_ephemeris_descriptors_from_directory(root, discoverers, options, &generic), "builtin discovery sees TSF1", failures);
    bool saw_tsf1 = false;
    for (size_t i = 0; i < generic.size(); ++i) {
        if (generic[i].path == path && generic[i].format == EphemerisBlockFormat::Tsc1) {
            saw_tsf1 = true;
        }
    }
    expect_true(saw_tsf1, "saw TSF1 in builtin discovery", failures);

    EphemerisBlockCatalog catalog;
    expect_true(discover_star_file_catalog_from_directory(root, &catalog), "discover TSF1 catalog", failures);
    expect_true(catalog.size() == 1, "TSF1 catalog descriptor count", failures);

    std::remove(path.c_str());
    std::remove(ignored.c_str());
    rmdir(root.c_str());
}

}  // namespace

int main() {
    int failures = 0;
    test_save_load_roundtrip(&failures);
    test_default_user_directory(&failures);
    test_invalid_files(&failures);
    test_build_tsc1_bytes_and_provider(&failures);
    test_discovery(&failures);

    if (failures == 0) {
        std::cout << "test_star_file: ALL TESTS PASSED\n";
        return 0;
    }
    std::cerr << failures << " test_star_file failure(s)\n";
    return 1;
}
