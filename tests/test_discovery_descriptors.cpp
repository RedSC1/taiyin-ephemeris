#include "taiyin/body_id.h"
#include "taiyin/internal/descriptor_loader.h"
#include "taiyin/internal/ephemeris_block.h"
#include "taiyin/internal/ephemeris_catalog.h"
#include "taiyin/internal/ephemeris_discovery.h"
#include "taiyin/internal/kepler.h"
#include "taiyin/internal/kepler_catalog_discovery.h"
#include "taiyin/internal/kepler_catalog_tkc1.h"
#include "taiyin/internal/kepler_file.h"
#include "taiyin/internal/tkc1_catalog_discovery.h"
#include "taiyin/physical_constants.h"
#include "taiyin/state.h"
#include "taiyin/time.h"

#include <cmath>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <unistd.h>
#include <vector>

namespace {

const int USER_TARGET_A = 930001;
const int USER_TARGET_B = 930002;

void expect_true(bool value, const char* label, int* failures) {
    if (!value) {
        std::cerr << "FAIL: expected true: " << label << "\n";
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

std::string repo_path(const std::string& suffix) {
    const char* root = std::getenv("TAIYIN_REPO_ROOT");
    if (root && root[0] != '\0') {
        return std::string(root) + "/" + suffix;
    }
    return std::string("../") + suffix;
}

std::string make_temp_directory() {
    char pattern[] = "/tmp/taiyin-discovery-XXXXXX";
    char* root = mkdtemp(pattern);
    return root ? std::string(root) : std::string();
}

taiyin::internal::KeplerElements make_element(int target_id, double semi_major_axis_au) {
    taiyin::internal::KeplerElements element;
    taiyin::internal::make_elliptic_kepler_elements(
        target_id,
        taiyin::TAIYIN_BODY_SUN,
        taiyin::JD_J2000 - 500.0,
        taiyin::JD_J2000 + 500.0,
        taiyin::JD_J2000,
        taiyin::TAIYIN_SOLAR_MU_AU3_DAY2,
        semi_major_axis_au,
        0.05,
        0.02,
        0.03,
        0.04,
        0.05,
        &element);
    return element;
}

const taiyin::internal::EphemerisBlockDescriptor* find_descriptor(
    const std::vector<taiyin::internal::EphemerisBlockDescriptor>& descriptors,
    int target_id
) {
    for (size_t i = 0; i < descriptors.size(); ++i) {
        if (descriptors[i].target_id == target_id) {
            return &descriptors[i];
        }
    }
    return 0;
}

bool descriptor_loads_and_evals(
    const taiyin::internal::EphemerisBlockDescriptor& descriptor,
    int* failures,
    const char* label
) {
    taiyin::internal::StorageEphemerisBlock storage;
    if (!taiyin::internal::load_descriptor_ephemeris_block(descriptor, &storage)) {
        std::cerr << "FAIL: load descriptor: " << label << "\n";
        ++(*failures);
        return false;
    }

    taiyin::internal::CompiledEphemerisBlock block;
    if (!taiyin::internal::get_compiled_block_from_storage(&storage, descriptor.target_id, &block)) {
        std::cerr << "FAIL: get compiled block: " << label << "\n";
        ++(*failures);
        taiyin::internal::destroy_storage_ephemeris_block(&storage);
        return false;
    }

    const double jd = 0.5 * (descriptor.jd_tdb_start + descriptor.jd_tdb_end);
    taiyin::SplitJulianDate split_jd;
    taiyin::split_julian_date_from_double(jd, &split_jd);
    taiyin::CartesianState state;
    const bool ok = taiyin::internal::eval_compiled_ephemeris_block(split_jd, &block, &state)
        && std::isfinite(state.position_au.x)
        && std::isfinite(state.position_au.y)
        && std::isfinite(state.position_au.z)
        && std::isfinite(state.velocity_au_per_day.x)
        && std::isfinite(state.velocity_au_per_day.y)
        && std::isfinite(state.velocity_au_per_day.z);
    if (!ok) {
        std::cerr << "FAIL: eval descriptor: " << label << "\n";
        ++(*failures);
    }
    taiyin::internal::destroy_storage_ephemeris_block(&storage);
    return ok;
}

void test_kepler_file_discovery(int* failures) {
    using namespace taiyin::internal;

    const std::string temp_root = make_temp_directory();
    expect_true(!temp_root.empty(), "make temp discovery directory", failures);
    if (temp_root.empty()) {
        return;
    }

    const std::string path_a = temp_root + "/custom-a.tke1";
    const std::string path_b = temp_root + "/custom-b.tke1";
    const KeplerElements element_a = make_element(USER_TARGET_A, 1.2);
    const KeplerElements element_b = make_element(USER_TARGET_B, 1.5);
    expect_true(save_kepler_file(
        path_a,
        &element_a,
        1,
        TAIYIN_KEPLER_FILE_METHOD_ID,
        EphemerisFrame::IcrfJ2000Equatorial,
        taiyin::JD_J2000 - 500.0,
        taiyin::JD_J2000 + 500.0), "save kepler file A", failures);
    expect_true(save_kepler_file(
        path_b,
        &element_b,
        1,
        TAIYIN_KEPLER_FILE_METHOD_ID,
        EphemerisFrame::IcrfJ2000Equatorial,
        taiyin::JD_J2000 - 500.0,
        taiyin::JD_J2000 + 500.0), "save kepler file B", failures);

    std::vector<EphemerisBlockDescriptor> descriptors;
    expect_true(collect_kepler_descriptors_from_directory(temp_root, &descriptors), "collect kepler descriptors", failures);
    expect_equal_size(descriptors.size(), 2u, "kepler descriptor count", failures);

    const EphemerisBlockDescriptor* descriptor_a = find_descriptor(descriptors, USER_TARGET_A);
    expect_true(descriptor_a != 0, "find kepler descriptor A", failures);
    if (descriptor_a) {
        expect_equal_int(descriptor_a->format, EphemerisBlockFormat::Kepler, "kepler descriptor format", failures);
        expect_equal_int(descriptor_a->method_id, TAIYIN_KEPLER_FILE_METHOD_ID, "kepler descriptor method", failures);
        expect_equal_int(descriptor_a->frame, EphemerisFrame::IcrfJ2000Equatorial, "kepler descriptor frame", failures);
        expect_equal_int(descriptor_a->center_id, taiyin::TAIYIN_BODY_SUN, "kepler descriptor center", failures);
        expect_true(descriptor_a->path == path_a, "kepler descriptor path", failures);
        expect_true(descriptor_a->source_key.source_id == TAIYIN_KEPLER_FILE_SOURCE_ID, "kepler source id", failures);
        expect_equal_int(descriptor_a->cache_policy.kind, CacheWholeEntry, "kepler cache policy", failures);
        expect_true(descriptor_a->jd_tdb_start <= taiyin::JD_J2000, "kepler descriptor start", failures);
        expect_true(descriptor_a->jd_tdb_end >= taiyin::JD_J2000, "kepler descriptor end", failures);
        descriptor_loads_and_evals(*descriptor_a, failures, "kepler file A");
    }

    std::vector<EphemerisDiscoverFileFn> discoverers;
    append_builtin_ephemeris_discoverers(&discoverers);
    std::vector<EphemerisBlockDescriptor> generic_descriptors;
    EphemerisDiscoveryOptions options;
    expect_true(
        discover_ephemeris_descriptors_from_directory(temp_root, discoverers, options, &generic_descriptors),
        "generic discovery sees kepler files",
        failures);
    expect_true(find_descriptor(generic_descriptors, USER_TARGET_A) != 0, "generic descriptor A", failures);
    expect_true(find_descriptor(generic_descriptors, USER_TARGET_B) != 0, "generic descriptor B", failures);

    unlink(path_a.c_str());
    unlink(path_b.c_str());
    rmdir(temp_root.c_str());
}

void test_tkc1_packaged_discovery(int* failures) {
    using namespace taiyin::internal;

    const std::string root = repo_path("data/kepler/sbdb");
    std::vector<EphemerisBlockDescriptor> descriptors;
    expect_true(collect_tkc1_descriptors_from_directory(root, &descriptors), "collect packaged TKC1 descriptors", failures);
    expect_true(!descriptors.empty(), "packaged TKC1 descriptor count", failures);
    if (descriptors.empty()) {
        return;
    }

    const EphemerisBlockDescriptor& descriptor = descriptors[0];
    expect_equal_int(descriptor.format, EphemerisBlockFormat::Tkc1, "TKC1 descriptor format", failures);
    expect_equal_int(descriptor.method_id, TKC1_KEPLER_METHOD_ID, "TKC1 method", failures);
    expect_equal_int(descriptor.frame, EphemerisFrame::IcrfJ2000Equatorial, "TKC1 frame", failures);
    expect_equal_int(descriptor.cache_policy.kind, CacheWholeEntry, "TKC1 cache policy", failures);
    expect_true(descriptor.target_id != 0, "TKC1 target id", failures);
    expect_true(descriptor.center_id != 0, "TKC1 center id", failures);
    expect_true(!descriptor.path.empty(), "TKC1 descriptor path", failures);
    expect_true(descriptor.source_key.generation == TKC1_GENERATION, "TKC1 generation", failures);
    expect_true(descriptor.source_key.purpose == TKC1_PURPOSE, "TKC1 purpose", failures);
    expect_true(descriptor.jd_tdb_start < descriptor.jd_tdb_end, "TKC1 coverage", failures);
    descriptor_loads_and_evals(descriptor, failures, "packaged TKC1");

    std::vector<EphemerisDiscoverFileFn> discoverers;
    append_builtin_ephemeris_discoverers(&discoverers);
    std::vector<EphemerisBlockDescriptor> generic_descriptors;
    EphemerisDiscoveryOptions options;
    expect_true(
        discover_ephemeris_descriptors_from_directory(root, discoverers, options, &generic_descriptors),
        "generic discovery sees TKC1 files",
        failures);
    expect_true(!generic_descriptors.empty(), "generic TKC1 descriptor count", failures);
}

void test_cache_bucket_ids(int* failures) {
    using namespace taiyin::internal;

    EphemerisBlockDescriptor descriptor;
    descriptor.jd_tdb_start = 100.0;
    descriptor.jd_tdb_end = 200.0;
    int bucket_id = -1;
    taiyin::SplitJulianDate bucket_jd;
    taiyin::split_julian_date_from_double(150.0, &bucket_jd);
    expect_true(
        cache_bucket_id_for_jd(descriptor, bucket_jd, &bucket_id),
        "whole-entry bucket id resolves",
        failures);
    expect_equal_int(bucket_id, 0, "whole-entry bucket id", failures);

    descriptor.cache_policy.kind = CacheFixedSpan;
    descriptor.cache_policy.origin_jd = 100.0;
    descriptor.cache_policy.span_days = 10.0;
    descriptor.cache_policy.first_index = 0;
    descriptor.cache_policy.count = 10;
    taiyin::split_julian_date_from_double(139.999, &bucket_jd);
    expect_true(
        cache_bucket_id_for_jd(descriptor, bucket_jd, &bucket_id),
        "fixed-span bucket id resolves",
        failures);
    expect_equal_int(bucket_id, 3, "fixed-span bucket id", failures);
    EphemerisBlockDescriptor bucket;
    expect_true(
        make_cache_bucket_descriptor_for_jd(descriptor, bucket_jd, &bucket),
        "fixed-span bucket descriptor resolves",
        failures);
    expect_true(
        bucket.jd_tdb_start_split == taiyin::SplitJulianDate(130, 0.0)
            && bucket.jd_tdb_end_split == taiyin::SplitJulianDate(140, 0.0),
        "bucket descriptor keeps split coverage fields synchronized",
        failures);
    taiyin::split_julian_date_from_double(200.0, &bucket_jd);
    expect_true(
        !cache_bucket_id_for_jd(descriptor, bucket_jd, &bucket_id),
        "coverage end excludes cache bucket",
        failures);
}

}  // namespace

int main() {
    int failures = 0;
    test_cache_bucket_ids(&failures);
    test_kepler_file_discovery(&failures);
    test_tkc1_packaged_discovery(&failures);
    return failures == 0 ? 0 : 1;
}
