#include "taiyin/internal/descriptor_loader.h"
#include "taiyin/internal/ephemeris_discovery.h"
#include "taiyin/internal/spk_catalog_discovery.h"
#include "taiyin/internal/spk.h"
#include "test_env.h"

#include <cassert>
#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

namespace {

const char* kDe441Path = taiyin_test::getenv_path("TAIYIN_DE441_PATH");
const char* kMainBeltAsteroidsPath = taiyin_test::getenv_path("TAIYIN_MAIN_BELT_ASTEROIDS_SPK_PATH");
const char* kNearEarthAsteroidsPath = taiyin_test::getenv_path("TAIYIN_NEAR_EARTH_ASTEROIDS_SPK_PATH");
const char* kJupiterSatellitesPath = taiyin_test::getenv_path("TAIYIN_JUPITER_SATELLITES_SPK_PATH");
const char* kSaturnSatellitesPath = taiyin_test::getenv_path("TAIYIN_SATURN_SATELLITES_SPK_PATH");

bool file_exists(const char* path) {
    if (!path) {
        return false;
    }
    std::ifstream file(path, std::ios::binary);
    return static_cast<bool>(file);
}

void expect_true(bool value, const char* label) {
    if (!value) {
        std::fprintf(stderr, "expected true: %s\n", label);
        assert(false);
    }
}

const taiyin::internal::EphemerisBlockDescriptor* find_descriptor(
    const std::vector<taiyin::internal::EphemerisBlockDescriptor>& descriptors,
    int target_id,
    int center_id,
    double jd_tdb
) {
    for (size_t i = 0; i < descriptors.size(); ++i) {
        const taiyin::internal::EphemerisBlockDescriptor& descriptor = descriptors[i];
        if (descriptor.target_id == target_id
            && descriptor.center_id == center_id
            && descriptor.jd_tdb_start <= jd_tdb
            && jd_tdb < descriptor.jd_tdb_end) {
            return &descriptor;
        }
    }
    return 0;
}

void check_spk_file(
    const char* label,
    const char* path,
    int direct_target,
    int direct_center,
    int derived_target,
    int derived_center,
    double jd_tdb
) {
    using taiyin::CartesianState;
    using taiyin::internal::EphemerisBlockDescriptor;
    using taiyin::internal::EphemerisBlockFormat;
    using taiyin::internal::EphemerisDiscoveryOptions;
    using taiyin::internal::StorageEphemerisBlock;
    using taiyin::internal::compile_spk_ephemeris_block_from_file;
    using taiyin::internal::destroy_storage_ephemeris_block;
    using taiyin::internal::discover_spk_file;
    using taiyin::internal::eval_descriptor_state;

    if (!file_exists(path)) {
        std::fprintf(stderr, "skip %s: missing %s\n", label, path);
        return;
    }

    std::vector<EphemerisBlockDescriptor> descriptors;
    EphemerisDiscoveryOptions options;
    expect_true(discover_spk_file(path, options, &descriptors) == taiyin::internal::DiscoveryOk, label);
    expect_true(!descriptors.empty(), "spk descriptors not empty");

    for (size_t i = 0; i < descriptors.size(); ++i) {
        expect_true(descriptors[i].jd_tdb_end > descriptors[i].jd_tdb_start, "spk source descriptor non-empty coverage");
    }

    const EphemerisBlockDescriptor* direct = find_descriptor(descriptors, direct_target, direct_center, jd_tdb);
    expect_true(direct != 0, "direct target-center descriptor found");
    expect_true(direct->format == EphemerisBlockFormat::Spk, "direct descriptor format spk");
    expect_true(direct->method_id == 2, "direct descriptor method spk");
    expect_true(direct->path == path, "direct descriptor path");

    EphemerisBlockDescriptor direct_bucket;
    expect_true(taiyin::internal::make_cache_bucket_descriptor_for_jd(*direct, jd_tdb, &direct_bucket), "direct lazy ten-year bucket");
    const double direct_bucket_start = taiyin::internal::EPHEMERIS_DISCOVERY_J2000_JD
        + static_cast<double>(direct_bucket.route_key.bucket_id)
            * taiyin::internal::EPHEMERIS_DISCOVERY_TEN_JULIAN_YEARS_DAYS;
    const double direct_bucket_end = direct_bucket_start + taiyin::internal::EPHEMERIS_DISCOVERY_TEN_JULIAN_YEARS_DAYS;
    expect_true(direct_bucket.jd_tdb_end - direct_bucket.jd_tdb_start <= taiyin::internal::EPHEMERIS_DISCOVERY_TEN_JULIAN_YEARS_DAYS + 1e-9, "direct lazy bucket no larger than ten years");
    expect_true(direct_bucket.jd_tdb_start >= direct_bucket_start - 1e-9, "direct lazy bucket clipped start inside bucket");
    expect_true(direct_bucket.jd_tdb_end <= direct_bucket_end + 1e-9, "direct lazy bucket clipped end inside bucket");

    StorageEphemerisBlock storage;
    expect_true(
        compile_spk_ephemeris_block_from_file(
            path,
            direct->target_id,
            direct->center_id,
            jd_tdb - 0.01,
            jd_tdb + 0.01,
            &storage),
        "compile direct descriptor range");
    destroy_storage_ephemeris_block(&storage);

    if (derived_target != 0 || derived_center != 0) {
        const EphemerisBlockDescriptor* derived = find_descriptor(descriptors, derived_target, derived_center, jd_tdb);
        expect_true(derived != 0, "derived target-center descriptor found");
        expect_true(eval_descriptor_state(*derived, 0, jd_tdb, static_cast<CartesianState*>(0)) == false, "null cache rejected");
        expect_true(
            compile_spk_ephemeris_block_from_file(
                path,
                derived->target_id,
                derived->center_id,
                jd_tdb - 0.01,
                jd_tdb + 0.01,
                &storage),
            "compile derived descriptor range");
        destroy_storage_ephemeris_block(&storage);
    }

    std::fprintf(stderr, "%s descriptors=%zu\n", label, descriptors.size());
}

}  // namespace

int main() {
    check_spk_file("planetary-de441", kDe441Path, 4, 0, 4, 10, 2451545.0);
    check_spk_file("main-belt-asteroids", kMainBeltAsteroidsPath, 2000001, 10, 0, 0, 2451545.0);
    check_spk_file("near-earth-asteroids", kNearEarthAsteroidsPath, 2000433, 10, 0, 0, 2451545.0);
    check_spk_file("jupiter-satellites", kJupiterSatellitesPath, 599, 5, 501, 599, 2451545.0);
    check_spk_file("saturn-satellites", kSaturnSatellitesPath, 699, 6, 601, 699, 2451545.0);
    return 0;
}
