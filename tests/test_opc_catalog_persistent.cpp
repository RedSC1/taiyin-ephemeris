#include "taiyin/body_id.h"
#include "taiyin/internal/descriptor_loader.h"
#include "taiyin/internal/ephemeris_catalog.h"
#include "taiyin/internal/ephemeris_discovery.h"
#include "taiyin/internal/ephemeris_source_identity.h"
#include "taiyin/internal/opm2_catalog_discovery.h"
#include "taiyin/internal/opc_catalog_persistent.h"
#include "taiyin/internal/path_utils.h"
#include "taiyin/internal/spk_catalog_discovery.h"
#include "taiyin/state.h"
#include "taiyin/time.h"

#include <cassert>
#include <cstddef>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <unistd.h>
#include <vector>

namespace {

const size_t PACKAGED_OPM2_DESCRIPTOR_COUNT = 36u;

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

void expect_equal_int(int actual, int expected, const char* label, int* failures) {
    if (actual != expected) {
        std::cerr << "FAIL: " << label << ": actual=" << actual << " expected=" << expected << "\n";
        ++(*failures);
    }
}

void expect_equal_size(size_t actual, size_t expected, const char* label, int* failures) {
    if (actual != expected) {
        std::cerr << "FAIL: " << label << ": actual=" << actual << " expected=" << expected << "\n";
        ++(*failures);
    }
}

void expect_equal_u64(uint64_t actual, uint64_t expected, const char* label, int* failures) {
    if (actual != expected) {
        std::cerr << "FAIL: " << label << ": actual=" << actual
                  << " expected=" << expected << "\n";
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
    char templ[] = "/tmp/taiyin-opc-XXXXXX";
    char* path = mkdtemp(templ);
    assert(path);
    return std::string(path);
}

bool write_file(const std::string& path, const char* contents) {
    FILE* file = std::fopen(path.c_str(), "wb");
    if (!file) {
        return false;
    }
    const size_t len = std::strlen(contents);
    const size_t written = std::fwrite(contents, 1, len, file);
    const int close_status = std::fclose(file);
    return written == len && close_status == 0;
}

std::string repo_data_path(const std::string& suffix) {
    const char* root = std::getenv("TAIYIN_REPO_ROOT");
    if (root && root[0] != '\0') {
        return std::string(root) + "/data/" + suffix;
    }
    return std::string("../data/") + suffix;
}

const taiyin::internal::EphemerisBlockDescriptor* find_descriptor(
    const std::vector<taiyin::internal::EphemerisBlockDescriptor>& descriptors,
    int target_id,
    int center_id
) {
    for (size_t i = 0; i < descriptors.size(); ++i) {
        if (descriptors[i].target_id == target_id && descriptors[i].center_id == center_id) {
            return &descriptors[i];
        }
    }
    return 0;
}

const taiyin::internal::EphemerisBlockDescriptor* find_route_descriptor(
    const std::vector<taiyin::internal::EphemerisBlockDescriptor>& descriptors,
    const taiyin::internal::EphemerisBlockDescriptor& needle
) {
    for (size_t i = 0; i < descriptors.size(); ++i) {
        if (descriptors[i].target_id == needle.target_id
            && descriptors[i].center_id == needle.center_id
            && descriptors[i].method_id == needle.method_id
            && descriptors[i].frame == needle.frame
            && descriptors[i].format == needle.format
            && descriptors[i].route_key.bucket_id == needle.route_key.bucket_id
            && descriptors[i].source_key.source_id == needle.source_key.source_id
            && descriptors[i].source_key.block_id == needle.source_key.block_id) {
            return &descriptors[i];
        }
    }
    return 0;
}

bool discover_all_ephemeris_descriptors(
    const std::string& root,
    std::vector<taiyin::internal::EphemerisBlockDescriptor>* out
) {
    std::vector<taiyin::internal::EphemerisDiscoverFileFn> discoverers;
    taiyin::internal::append_builtin_ephemeris_discoverers(&discoverers);
    taiyin::internal::EphemerisDiscoveryOptions options;
    options.strict = false;
    return taiyin::internal::discover_ephemeris_descriptors_from_directory(root, discoverers, options, out);
}

size_t count_format(
    const std::vector<taiyin::internal::EphemerisBlockDescriptor>& descriptors,
    taiyin::internal::EphemerisBlockFormat format
) {
    size_t count = 0;
    for (size_t i = 0; i < descriptors.size(); ++i) {
        if (descriptors[i].format == format) {
            ++count;
        }
    }
    return count;
}

void expect_descriptor_equal(
    const taiyin::internal::EphemerisBlockDescriptor& actual,
    const taiyin::internal::EphemerisBlockDescriptor& expected,
    const char* label,
    int* failures
) {
    expect_equal_int(actual.target_id, expected.target_id, label, failures);
    expect_equal_int(actual.center_id, expected.center_id, label, failures);
    expect_equal_int(actual.method_id, expected.method_id, label, failures);
    expect_equal_int(actual.frame, expected.frame, label, failures);
    expect_equal_int(actual.format, expected.format, label, failures);
    expect_equal_int(actual.route_key.bucket_id, expected.route_key.bucket_id, label, failures);
    expect_near(actual.jd_tdb_start, expected.jd_tdb_start, 0.0, label, failures);
    expect_near(actual.jd_tdb_end, expected.jd_tdb_end, 0.0, label, failures);
    expect_true(!actual.path.empty(), label, failures);
}

void expect_descriptor_loads(
    const taiyin::internal::EphemerisBlockDescriptor& descriptor,
    const char* label,
    int* failures
) {
    taiyin::internal::StorageEphemerisBlock storage;
    if (!taiyin::internal::load_descriptor_ephemeris_block(descriptor, &storage)) {
        std::cerr << "FAIL: load descriptor: " << label << "\n";
        ++(*failures);
        return;
    }

    taiyin::internal::CompiledEphemerisBlock block;
    if (!taiyin::internal::get_compiled_block_from_storage(&storage, descriptor.target_id, &block)) {
        std::cerr << "FAIL: get compiled descriptor block: " << label << "\n";
        ++(*failures);
        taiyin::internal::destroy_storage_ephemeris_block(&storage);
        return;
    }

    const double jd_value = 0.5 * (descriptor.jd_tdb_start + descriptor.jd_tdb_end);
    taiyin::SplitJulianDate jd;
    if (!taiyin::split_julian_date_from_double(jd_value, &jd)) {
        std::cerr << "FAIL: split descriptor epoch: " << label << "\n";
        ++(*failures);
        taiyin::internal::destroy_storage_ephemeris_block(&storage);
        return;
    }
    taiyin::CartesianState state;
    if (!taiyin::internal::eval_compiled_ephemeris_block(jd, &block, &state)
        || !std::isfinite(state.position_au.x)
        || !std::isfinite(state.position_au.y)
        || !std::isfinite(state.position_au.z)) {
        std::cerr << "FAIL: eval loaded descriptor: " << label << "\n";
        ++(*failures);
    }
    taiyin::internal::destroy_storage_ephemeris_block(&storage);
}

void test_empty_fingerprint(int* failures) {
    using namespace taiyin::internal;

    const std::string root = make_temp_dir();
    uint64_t fingerprint = 0;
    expect_true(compute_opc_catalog_fingerprint(root, &fingerprint), "empty fingerprint computes", failures);
    expect_true(fingerprint == OPC_FINGERPRINT_EMPTY, "empty fingerprint offset basis", failures);
}

void test_stale_catalog_rejected(int* failures) {
    using namespace taiyin::internal;

    const std::string root = repo_data_path("ephemerides/opm2/major-bodies/600y");
    const std::string temp = make_temp_dir();
    const std::string catalog_path = temp + "/catalog.opc";

    std::vector<EphemerisBlockDescriptor> descriptors;
    expect_true(collect_opm2_descriptors_from_directory(root, &descriptors), "collect major body descriptors", failures);
    expect_true(write_opc_persistent_catalog(catalog_path, root, descriptors), "write staged OPC", failures);

    expect_true(write_file(join_path(temp, "dummy.opm2"), "not really opm2"), "write dummy OPM2 file", failures);
    std::vector<EphemerisBlockDescriptor> loaded;
    expect_false(load_opc_persistent_catalog(catalog_path, temp, &loaded), "reject catalog for different root", failures);
}

void test_staged_catalog_roundtrip(int* failures) {
    using namespace taiyin::internal;

    const std::string root = repo_data_path("");
    const std::string temp = make_temp_dir();
    const std::string catalog_path = temp + "/catalog.opc";

    std::vector<EphemerisBlockDescriptor> discovered;
    expect_true(discover_all_ephemeris_descriptors(root, &discovered), "collect staged ephemeris descriptors", failures);
    expect_equal_size(
        count_format(discovered, EphemerisBlockFormat::Opm2),
        PACKAGED_OPM2_DESCRIPTOR_COUNT,
        "staged OPM2 descriptor count",
        failures);
    expect_true(
        count_format(discovered, EphemerisBlockFormat::Tkc1) > 0,
        "staged OPC includes TKC1 descriptors",
        failures);
    expect_equal_size(
        count_format(discovered, EphemerisBlockFormat::Tsc1),
        0u,
        "staged ephemeris discovery keeps TSC1 out",
        failures);
    expect_equal_size(
        count_format(discovered, EphemerisBlockFormat::FixedStar),
        0u,
        "staged ephemeris discovery keeps fixed stars out",
        failures);
    expect_true(
        find_descriptor(discovered, taiyin::TAIYIN_BODY_URANUS, taiyin::TAIYIN_BODY_URANUS_BARYCENTER) != 0,
        "top-level staged OPM2 includes Uranus COB slice",
        failures);
    expect_true(
        find_descriptor(discovered, taiyin::TAIYIN_BODY_JUPITER, taiyin::TAIYIN_BODY_JUPITER_BARYCENTER) != 0,
        "top-level staged OPM2 includes Jupiter COB",
        failures);
    expect_true(
        find_descriptor(discovered, taiyin::TAIYIN_BODY_SATURN, taiyin::TAIYIN_BODY_SATURN_BARYCENTER) != 0,
        "top-level staged OPM2 includes Saturn COB",
        failures);
    expect_true(
        find_descriptor(discovered, taiyin::TAIYIN_BODY_NEPTUNE, taiyin::TAIYIN_BODY_NEPTUNE_BARYCENTER) != 0,
        "top-level staged OPM2 includes Neptune COB",
        failures);
    expect_true(
        find_descriptor(discovered, taiyin::TAIYIN_BODY_PLUTO, taiyin::TAIYIN_BODY_PLUTO_BARYCENTER) != 0,
        "top-level staged OPM2 includes Pluto COB",
        failures);
    expect_true(write_opc_persistent_catalog(catalog_path, root, discovered), "write staged top-level OPC", failures);

    std::vector<EphemerisBlockDescriptor> loaded;
    expect_true(load_opc_persistent_catalog(catalog_path, root, &loaded), "load staged top-level OPC", failures);
    expect_equal_size(loaded.size(), discovered.size(), "loaded OPC descriptor count", failures);
    expect_equal_size(count_format(loaded, EphemerisBlockFormat::Tsc1), 0u, "OPC load keeps TSC1 out", failures);
    expect_equal_size(count_format(loaded, EphemerisBlockFormat::FixedStar), 0u, "OPC load keeps fixed stars out", failures);

    for (size_t i = 0; i < loaded.size(); ++i) {
        const EphemerisBlockDescriptor* expected = find_route_descriptor(discovered, loaded[i]);
        expect_true(expected != 0, "loaded descriptor exists in discovery", failures);
        if (expected) {
            expect_descriptor_equal(loaded[i], *expected, "loaded descriptor matches discovery", failures);
        }
    }

    std::vector<EphemerisBlockDescriptor> via_api;
    std::vector<EphemerisDiscoverFileFn> discoverers;
    append_builtin_ephemeris_discoverers(&discoverers);
    EphemerisDiscoveryOptions options;
    options.strict = false;
    expect_true(
        collect_ephemeris_descriptors_from_catalog_or_directory(root, catalog_path, discoverers, options, &via_api),
        "collect via catalog-or-directory",
        failures);
    expect_equal_size(via_api.size(), discovered.size(), "catalog-or-directory descriptor count", failures);

    EphemerisBlockCatalog catalog;
    expect_true(
        discover_ephemeris_catalog_from_catalog_or_directory(root, catalog_path, discoverers, options, &catalog),
        "discover catalog via persistent catalog",
        failures);
    expect_equal_size(catalog.size(), discovered.size(), "persistent EphemerisBlockCatalog size", failures);

    bool loaded_tkc1 = false;
    for (size_t i = 0; i < loaded.size(); ++i) {
        if (loaded[i].target_id == taiyin::TAIYIN_BODY_SUN || loaded[i].target_id == taiyin::TAIYIN_BODY_CERES) {
            expect_descriptor_loads(loaded[i], "loaded persistent descriptor", failures);
        }
        if (!loaded_tkc1 && loaded[i].format == EphemerisBlockFormat::Tkc1) {
            expect_descriptor_loads(loaded[i], "loaded persistent TKC1 descriptor", failures);
            loaded_tkc1 = true;
        }
    }
    expect_true(loaded_tkc1, "loaded one TKC1 descriptor", failures);
}

void test_fallback_writes_catalog(int* failures) {
    using namespace taiyin::internal;

    const std::string root = repo_data_path("ephemerides/opm2/asteroids/600y");
    const std::string temp = make_temp_dir();
    const std::string catalog_path = temp + "/catalog.opc";

    std::vector<EphemerisBlockDescriptor> descriptors;
    std::vector<EphemerisDiscoverFileFn> discoverers;
    append_builtin_ephemeris_discoverers(&discoverers);
    EphemerisDiscoveryOptions options;
    options.strict = false;
    expect_true(
        collect_ephemeris_descriptors_from_catalog_or_directory(root, catalog_path, discoverers, options, &descriptors),
        "fallback collect writes catalog",
        failures);
    expect_equal_size(descriptors.size(), 9u, "fallback asteroid descriptor count", failures);

    std::vector<EphemerisBlockDescriptor> loaded;
    expect_true(load_opc_persistent_catalog(catalog_path, root, &loaded), "fallback-written catalog loads", failures);
    expect_equal_size(loaded.size(), 9u, "fallback-written catalog count", failures);
}

void test_cached_spk_source_identity_is_reclassified(int* failures) {
    using namespace taiyin::internal;

    const std::string root = make_temp_dir();
    const std::string spk_path = join_path(root, "de442-test.bsp");
    const std::string catalog_path = join_path(root, "catalog.opc");
    expect_true(write_file(spk_path, "legacy cached SPK fixture"), "write cached SPK fixture", failures);

    EphemerisBlockDescriptor descriptor;
    descriptor.route_key = EphemerisRouteKey(5, 0, SPK_METHOD_ID, 0);
    descriptor.source_key = EphemerisBlockKey(SPK_SOURCE_EXTERNAL, 1, 1, 0);
    descriptor.target_id = 5;
    descriptor.center_id = 0;
    descriptor.method_id = SPK_METHOD_ID;
    descriptor.frame = EphemerisFrame::IcrfJ2000Equatorial;
    descriptor.format = EphemerisBlockFormat::Spk;
    descriptor.jd_tdb_start = 2400000.5;
    descriptor.jd_tdb_end = 2500000.5;
    descriptor.path = spk_path;
    descriptor.cache_policy.kind = CacheFixedSpan;
    descriptor.cache_policy.span_days = 32.0;

    std::vector<EphemerisBlockDescriptor> legacy_descriptors(1, descriptor);
    expect_true(
        write_opc_persistent_catalog(catalog_path, root, legacy_descriptors),
        "write legacy generic-SPK OPC fixture",
        failures);

    std::vector<EphemerisBlockDescriptor> loaded;
    expect_true(
        load_opc_persistent_catalog(catalog_path, root, &loaded),
        "load legacy generic-SPK OPC fixture",
        failures);
    expect_equal_size(loaded.size(), 1u, "reclassified OPC descriptor count", failures);
    if (loaded.size() == 1u) {
        expect_equal_u64(
            loaded[0].source_key.source_id,
            SPK_SOURCE_JPL_DE442,
            "cached SPK source is reclassified from its current path",
            failures);
        expect_equal_u64(
            loaded[0].source_key.block_id,
            descriptor.source_key.block_id,
            "cached SPK reclassification preserves block identity",
            failures);
    }

    FILE* catalog = std::fopen(catalog_path.c_str(), "r+b");
    expect_true(catalog != 0, "open OPC to simulate old discovery generation", failures);
    if (catalog) {
        const uint32_t old_discovery_version = OPC_DISCOVERY_VERSION - 1;
        const bool patched =
            std::fseek(
                catalog,
                static_cast<long>(offsetof(OpcHeader, source_version)),
                SEEK_SET) == 0
            && std::fwrite(
                &old_discovery_version,
                sizeof(old_discovery_version),
                1,
                catalog) == 1;
        expect_true(patched, "patch old OPC discovery generation", failures);
        std::fclose(catalog);
    }
    loaded.clear();
    expect_false(
        load_opc_persistent_catalog(catalog_path, root, &loaded),
        "OPC from old discovery generation is invalidated",
        failures);
}

}  // namespace

int main() {
    int failures = 0;
    test_empty_fingerprint(&failures);
    test_stale_catalog_rejected(&failures);
    test_staged_catalog_roundtrip(&failures);
    test_fallback_writes_catalog(&failures);
    test_cached_spk_source_identity_is_reclassified(&failures);

    if (failures == 0) {
        std::cout << "test_opc_catalog_persistent: ALL TESTS PASSED\n";
        return 0;
    }
    std::cerr << failures << " test_opc_catalog_persistent failure(s)\n";
    return 1;
}
