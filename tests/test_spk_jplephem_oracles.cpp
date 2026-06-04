#include "taiyin/internal/descriptor_loader.h"
#include "taiyin/internal/ephemeris_discovery.h"
#include "taiyin/internal/spk.h"
#include "taiyin/internal/spk_catalog_discovery.h"
#include "test_env.h"

#include <cassert>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <vector>

namespace {

const char* kDe441Path = taiyin_test::getenv_path("TAIYIN_DE441_PATH");
const char* kMainBeltAsteroidsPath = taiyin_test::getenv_path("TAIYIN_MAIN_BELT_ASTEROIDS_SPK_PATH");
const char* kNearEarthAsteroidsPath = taiyin_test::getenv_path("TAIYIN_NEAR_EARTH_ASTEROIDS_SPK_PATH");
const char* kJupiterSatellitesPath = taiyin_test::getenv_path("TAIYIN_JUPITER_SATELLITES_SPK_PATH");
const char* kSaturnSatellitesPath = taiyin_test::getenv_path("TAIYIN_SATURN_SATELLITES_SPK_PATH");

struct SpkJplephemOracle {
    const char* label;
    const char* path;
    int target_id;
    int center_id;
    double jd_tdb;
    double expected_position_au[3];
    double expected_velocity_au_per_day[3];
    double position_tolerance_au;
    double velocity_tolerance_au_per_day;
};

// Baked with jplephem 2.24 from local NASA BSP files.
// Re-run tools/generate_spk_jplephem_oracles.py to refresh.
const SpkJplephemOracle kOracles[] = {
    // de441-mars-barycenter-sun: jplephem derived via common center 0
    {
        "de441-mars-barycenter-sun",
        kDe441Path,
        4, 10, 2.45154500000000000e+06,
        { 1.39071592174628722e+00, 1.40121762681456896e-03, -3.69601671960114175e-02 },
        { 6.71499521033585081e-04, 1.38140375156143615e-02, 6.31790043331084683e-03 },
        2.0e-13, 2.0e-13
    },
    // main-belt-ceres-sun: jplephem direct
    {
        "main-belt-ceres-sun",
        kMainBeltAsteroidsPath,
        2000001, 10, 2.45154500000000000e+06,
        { -2.37932771276317467e+00, 5.45671084190370137e-01, 7.41225457026469137e-01 },
        { -3.58422807467806458e-03, -9.84521741445162739e-03, -3.90454387460957087e-03 },
        2.0e-13, 2.0e-13
    },
    // near-earth-eros-sun: jplephem direct
    {
        "near-earth-eros-sun",
        kNearEarthAsteroidsPath,
        2000433, 10, 2.45154500000000000e+06,
        { -1.19701215807893258e+00, -4.15696371554325206e-01, -4.52231237348988957e-01 },
        { 3.59489914451817860e-03, -1.32829710642919912e-02, -6.89998738298877763e-03 },
        2.0e-13, 2.0e-13
    },
    // jupiter-cob-barycenter: jplephem direct
    {
        "jupiter-cob-barycenter",
        kJupiterSatellitesPath,
        599, 5, 2.45154500000000000e+06,
        { 2.74463368666632054e-07, -2.95006823088859694e-07, -1.35332396420828882e-07 },
        { -1.30546740961134543e-08, 4.40478718106401572e-08, 2.09884067170561347e-08 },
        2.0e-13, 2.0e-13
    },
    // jupiter-io-cob: jplephem derived via common center 5
    {
        "jupiter-io-cob",
        kJupiterSatellitesPath,
        501, 599, 2.45154500000000000e+06,
        { 2.67192463675602985e-03, 7.64437576941230982e-04, 4.09114559281444372e-04 },
        { -3.11707551759944331e-03, 8.64531243545795317e-03, 4.06636910564108119e-03 },
        2.0e-13, 2.0e-13
    },
    // saturn-cob-barycenter: jplephem direct
    {
        "saturn-cob-barycenter",
        kSaturnSatellitesPath,
        699, 6, 2.45154500000000000e+06,
        { 1.56884584725223846e-06, -1.24894501243548127e-06, -4.75576704499893192e-08 },
        { 4.67259223096904967e-07, 5.63690432577963951e-07, -7.75733916068960939e-08 },
        2.0e-13, 2.0e-13
    },
    // saturn-mimas-cob: jplephem derived via common center 6
    {
        "saturn-mimas-cob",
        kSaturnSatellitesPath,
        601, 699, 2.45154500000000000e+06,
        { 9.42446216256323716e-04, -7.78227819492697376e-04, -1.39236494984477143e-05 },
        { 5.40260407981995629e-03, 6.37820865100706749e-03, -7.12512914636447199e-04 },
        2.0e-13, 2.0e-13
    },
};

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

double abs_max3(double x, double y, double z) {
    return std::fmax(std::fabs(x), std::fmax(std::fabs(y), std::fabs(z)));
}

void expect_oracle_state(
    const SpkJplephemOracle& oracle,
    const taiyin::CartesianState& actual,
    const char* source_label
) {
    const double position_diff = abs_max3(
        actual.position_au.x - oracle.expected_position_au[0],
        actual.position_au.y - oracle.expected_position_au[1],
        actual.position_au.z - oracle.expected_position_au[2]);
    const double velocity_diff = abs_max3(
        actual.velocity_au_per_day.x - oracle.expected_velocity_au_per_day[0],
        actual.velocity_au_per_day.y - oracle.expected_velocity_au_per_day[1],
        actual.velocity_au_per_day.z - oracle.expected_velocity_au_per_day[2]);

    if (!(position_diff <= oracle.position_tolerance_au)) {
        std::fprintf(
            stderr,
            "%s %s position mismatch: diff %.17g limit %.17g\n",
            oracle.label,
            source_label,
            position_diff,
            oracle.position_tolerance_au);
        assert(false);
    }
    if (!(velocity_diff <= oracle.velocity_tolerance_au_per_day)) {
        std::fprintf(
            stderr,
            "%s %s velocity mismatch: diff %.17g limit %.17g\n",
            oracle.label,
            source_label,
            velocity_diff,
            oracle.velocity_tolerance_au_per_day);
        assert(false);
    }

    std::fprintf(
        stderr,
        "%s %s position_diff=%.3e velocity_diff=%.3e\n",
        oracle.label,
        source_label,
        position_diff,
        velocity_diff);
}

const taiyin::internal::EphemerisBlockDescriptor* find_oracle_descriptor(
    const std::vector<taiyin::internal::EphemerisBlockDescriptor>& descriptors,
    const SpkJplephemOracle& oracle
) {
    for (size_t i = 0; i < descriptors.size(); ++i) {
        const taiyin::internal::EphemerisBlockDescriptor& descriptor = descriptors[i];
        if (descriptor.target_id == oracle.target_id
            && descriptor.center_id == oracle.center_id
            && descriptor.jd_tdb_start <= oracle.jd_tdb
            && oracle.jd_tdb < descriptor.jd_tdb_end) {
            return &descriptor;
        }
    }
    return 0;
}

bool discover_oracle_source_descriptor(
    const SpkJplephemOracle& oracle,
    taiyin::internal::EphemerisBlockDescriptor* out
) {
    if (!out || !file_exists(oracle.path)) {
        return false;
    }

    std::vector<taiyin::internal::EphemerisBlockDescriptor> descriptors;
    taiyin::internal::EphemerisDiscoveryOptions options;
    if (taiyin::internal::discover_spk_file(oracle.path, options, &descriptors) != taiyin::internal::DiscoveryOk) {
        return false;
    }
    const taiyin::internal::EphemerisBlockDescriptor* source = find_oracle_descriptor(descriptors, oracle);
    if (!source) {
        return false;
    }
    *out = *source;
    return true;
}

void check_descriptor_cache_eviction() {
    using taiyin::CartesianState;
    using taiyin::internal::EphemerisBlockCache;
    using taiyin::internal::EphemerisBlockDescriptor;
    using taiyin::internal::eval_descriptor_state;
    using taiyin::internal::make_cache_bucket_descriptor_for_jd;

    const SpkJplephemOracle& first_oracle = kOracles[0];
    const SpkJplephemOracle& second_oracle = kOracles[1];
    EphemerisBlockDescriptor first_source;
    EphemerisBlockDescriptor second_source;
    if (!discover_oracle_source_descriptor(first_oracle, &first_source)
        || !discover_oracle_source_descriptor(second_oracle, &second_source)) {
        std::printf("skipping descriptor cache eviction oracle; local BSP files are absent\n");
        return;
    }

    EphemerisBlockDescriptor first_bucket;
    EphemerisBlockDescriptor second_bucket;
    expect_true(make_cache_bucket_descriptor_for_jd(first_source, first_oracle.jd_tdb, &first_bucket), "first eviction bucket");
    expect_true(make_cache_bucket_descriptor_for_jd(second_source, second_oracle.jd_tdb, &second_bucket), "second eviction bucket");

    EphemerisBlockCache cache(1);
    CartesianState actual;
    expect_true(eval_descriptor_state(first_source, &cache, first_oracle.jd_tdb, &actual), "eval first oversized cached oracle");
    expect_oracle_state(first_oracle, actual, "lru-first");
    expect_true(cache.entry_count() == 1, "first oversized cache entry count");
    expect_true(cache.contains(first_bucket.route_key), "first oversized cache route");

    expect_true(eval_descriptor_state(second_source, &cache, second_oracle.jd_tdb, &actual), "eval second oversized cached oracle");
    expect_oracle_state(second_oracle, actual, "lru-second");
    expect_true(cache.entry_count() == 1, "second oversized cache entry count");
    expect_true(!cache.contains(first_bucket.route_key), "first oversized route evicted");
    expect_true(cache.contains(second_bucket.route_key), "second oversized cache route");
}

void check_oracle(const SpkJplephemOracle& oracle) {
    using taiyin::CartesianState;
    using taiyin::internal::CompiledEphemerisBlock;
    using taiyin::internal::EphemerisBlockCache;
    using taiyin::internal::EphemerisBlockDescriptor;
    using taiyin::internal::EphemerisBlockFormat;
    using taiyin::internal::EphemerisDiscoveryOptions;
    using taiyin::internal::StorageEphemerisBlock;
    using taiyin::internal::compile_spk_ephemeris_block_from_file;
    using taiyin::internal::destroy_storage_ephemeris_block;
    using taiyin::internal::discover_spk_file;
    using taiyin::internal::eval_compiled_ephemeris_block;
    using taiyin::internal::eval_descriptor_state;
    using taiyin::internal::get_compiled_block_from_storage;
    using taiyin::internal::make_cache_bucket_descriptor_for_jd;

    if (!file_exists(oracle.path)) {
        std::printf("skipping %s; local BSP file is absent\n", oracle.label);
        return;
    }

    StorageEphemerisBlock storage;
    expect_true(
        compile_spk_ephemeris_block_from_file(
            oracle.path,
            oracle.target_id,
            oracle.center_id,
            oracle.jd_tdb - 0.01,
            oracle.jd_tdb + 0.01,
            &storage),
        oracle.label);
    expect_true(storage.format == EphemerisBlockFormat::Spk, "compiled block is SPK");

    CompiledEphemerisBlock block;
    expect_true(get_compiled_block_from_storage(&storage, 0, &block), "get compiled SPK block");

    CartesianState actual;
    expect_true(eval_compiled_ephemeris_block(oracle.jd_tdb, &block, &actual), "eval SPK oracle state");
    expect_oracle_state(oracle, actual, "direct");
    destroy_storage_ephemeris_block(&storage);

    EphemerisBlockDescriptor source_descriptor;
    expect_true(discover_oracle_source_descriptor(oracle, &source_descriptor), "discover oracle source descriptor");

    EphemerisBlockDescriptor bucket_descriptor;
    expect_true(make_cache_bucket_descriptor_for_jd(source_descriptor, oracle.jd_tdb, &bucket_descriptor), "make oracle cache bucket");

    EphemerisBlockCache cache(1024 * 1024);
    expect_true(eval_descriptor_state(source_descriptor, &cache, oracle.jd_tdb, &actual), "eval oracle through descriptor cache");
    expect_oracle_state(oracle, actual, "descriptor-cache");
    expect_true(cache.entry_count() == 1, "one lazy bucket cache entry");
    expect_true(cache.contains(bucket_descriptor.route_key), "cache contains lazy bucket route");

    CartesianState cached_actual;
    expect_true(eval_descriptor_state(source_descriptor, &cache, oracle.jd_tdb, &cached_actual), "eval oracle cache hit");
    expect_oracle_state(oracle, cached_actual, "descriptor-cache-hit");
    expect_true(cache.entry_count() == 1, "cache hit does not add entries");
}

}  // namespace

int main() {
    for (size_t i = 0; i < sizeof(kOracles) / sizeof(kOracles[0]); ++i) {
        check_oracle(kOracles[i]);
    }
    check_descriptor_cache_eviction();
    return 0;
}
