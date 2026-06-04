#include "taiyin/star_catalog_tsc1.h"
#include "taiyin/angle.h"
#include "taiyin/internal/ephemeris_cache.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iostream>
#include <limits>
#include <map>
#include <string>
#include <vector>

namespace {

using namespace taiyin;

bool near(double actual, double expected, double tolerance) {
    return std::fabs(actual - expected) <= tolerance;
}

void expect_true(bool value, const char* label, int* failures) {
    if (!value) {
        std::cerr << "FAIL: expected true: " << label << "\n";
        ++(*failures);
    }
}

void expect_equal_u32(uint32_t actual, uint32_t expected, const char* label, int* failures) {
    if (actual != expected) {
        std::cerr << "FAIL: " << label << ": actual=" << actual << " expected=" << expected << "\n";
        ++(*failures);
    }
}

void expect_equal_u64(uint64_t actual, uint64_t expected, const char* label, int* failures) {
    if (actual != expected) {
        std::cerr << "FAIL: " << label << ": actual=" << actual << " expected=" << expected << "\n";
        ++(*failures);
    }
}

void expect_near(double actual, double expected, double tolerance, const char* label, int* failures) {
    if (!near(actual, expected, tolerance)) {
        std::cerr.precision(15);
        std::cerr << "FAIL: " << label << ": actual=" << actual
                  << " expected=" << expected << " tolerance=" << tolerance << "\n";
        ++(*failures);
    }
}

template <typename T>
void append_pod(std::vector<uint8_t>* bytes, const T& value) {
    const uint8_t* raw = reinterpret_cast<const uint8_t*>(&value);
    bytes->insert(bytes->end(), raw, raw + sizeof(T));
}

struct FixtureAlias {
    std::string alias;
    uint32_t star_index;
    uint64_t hash;
};

std::vector<uint8_t> make_fixture_catalog() {
    std::string strings;
    std::map<std::string, uint32_t> offsets;
    strings.push_back('\0');

    const auto add_string = [&](const std::string& value) -> uint32_t {
        std::map<std::string, uint32_t>::const_iterator found = offsets.find(value);
        if (found != offsets.end()) {
            return found->second;
        }
        const uint32_t offset = static_cast<uint32_t>(strings.size());
        strings.append(value);
        strings.push_back('\0');
        offsets[value] = offset;
        return offset;
    };

    const uint32_t spica_id = add_string("spica");
    const uint32_t spica_name = add_string("Spica");
    const uint32_t hip_alias = add_string("hip_65474");
    const uint32_t hr_alias = add_string("hr_5056");
    const uint32_t gc_id = add_string("galactic_center_j2000");
    const uint32_t gc_name = add_string("Galactic Center (J2000 direction)");
    const uint32_t gc_alias = add_string("galactic_center");

    std::vector<Tsc1StarRecord> stars(2);
    std::memset(&stars[0], 0, stars.size() * sizeof(Tsc1StarRecord));

    stars[0].canonical_id_offset = spica_id;
    stars[0].display_name_offset = spica_name;
    stars[0].gaia_dr3_source_id = 6193600049863267200ULL;
    stars[0].hip_id = 65474;
    stars[0].hr_id = 5056;
    stars[0].hd_id = 116658;
    stars[0].ra_deg = 201.298247375;
    stars[0].dec_deg = -11.161319472;
    stars[0].pm_ra_mas_yr = -42.35;
    stars[0].pm_dec_mas_yr = -31.73;
    stars[0].parallax_mas = 12.44;
    stars[0].radial_velocity_km_s = 1.0;
    stars[0].reference_epoch = 2016.0;
    stars[0].magnitude = 0.98f;
    stars[0].astrometry_source = TSC1_SOURCE_GAIA_DR3;
    stars[0].flags = TSC1_STAR_HAS_GAIA_ID | TSC1_STAR_HAS_HIP_ID | TSC1_STAR_HAS_HR_ID
        | TSC1_STAR_HAS_HD_ID | TSC1_STAR_HAS_RADIAL_VELOCITY | TSC1_STAR_HAS_PARALLAX;

    stars[1].canonical_id_offset = gc_id;
    stars[1].display_name_offset = gc_name;
    stars[1].ra_deg = 266.4168371;
    stars[1].dec_deg = -29.00781056;
    stars[1].reference_epoch = 2000.0;
    stars[1].magnitude = std::numeric_limits<float>::quiet_NaN();
    stars[1].astrometry_source = TSC1_SOURCE_MANUAL;
    stars[1].flags = TSC1_STAR_SPECIAL_DIRECTION;

    std::vector<FixtureAlias> fixture_aliases;
    const auto add_alias = [&](const std::string& alias, uint32_t star_index) {
        FixtureAlias entry;
        entry.alias = alias;
        entry.star_index = star_index;
        entry.hash = taiyin::tsc1_fnv1a_64(alias);
        fixture_aliases.push_back(entry);
    };
    add_alias("spica", 0);
    add_alias("hip_65474", 0);
    add_alias("hr_5056", 0);
    add_alias("galactic_center_j2000", 1);
    add_alias("galactic_center", 1);

    std::sort(fixture_aliases.begin(), fixture_aliases.end(), [](const FixtureAlias& lhs, const FixtureAlias& rhs) {
        if (lhs.hash != rhs.hash) {
            return lhs.hash < rhs.hash;
        }
        return lhs.alias < rhs.alias;
    });

    std::vector<Tsc1AliasEntry> aliases;
    for (size_t i = 0; i < fixture_aliases.size(); ++i) {
        Tsc1AliasEntry entry;
        entry.alias_offset = add_string(fixture_aliases[i].alias);
        entry.star_index = fixture_aliases[i].star_index;
        entry.alias_hash = fixture_aliases[i].hash;
        aliases.push_back(entry);
    }

    Tsc1Header header;
    std::memset(&header, 0, sizeof(header));
    header.magic[0] = 'T';
    header.magic[1] = 'S';
    header.magic[2] = 'C';
    header.magic[3] = '1';
    header.version = taiyin::TSC1_VERSION;
    header.star_count = static_cast<uint32_t>(stars.size());
    header.alias_count = static_cast<uint32_t>(aliases.size());
    header.star_records_offset = sizeof(Tsc1Header);
    header.alias_records_offset = header.star_records_offset + stars.size() * sizeof(Tsc1StarRecord);
    header.string_table_offset = header.alias_records_offset + aliases.size() * sizeof(Tsc1AliasEntry);
    header.string_table_size = strings.size();
    header.catalog_min_epoch = 2000.0;
    header.catalog_max_epoch = 2016.0;

    std::vector<uint8_t> bytes;
    append_pod(&bytes, header);
    for (size_t i = 0; i < stars.size(); ++i) {
        append_pod(&bytes, stars[i]);
    }
    for (size_t i = 0; i < aliases.size(); ++i) {
        append_pod(&bytes, aliases[i]);
    }
    bytes.insert(bytes.end(), strings.begin(), strings.end());
    return bytes;
}

void test_memory_catalog(int* failures) {
    std::vector<uint8_t> bytes = make_fixture_catalog();
    taiyin::Tsc1Catalog catalog;
    expect_true(taiyin::tsc1_catalog_load_from_memory(&catalog, &bytes[0], bytes.size()), "load fixture from memory", failures);
    expect_equal_u32(catalog.header ? catalog.header->star_count : 0, 2, "star count", failures);
    expect_equal_u32(catalog.header ? catalog.header->alias_count : 0, 5, "alias count", failures);

    const taiyin::Tsc1StarRecord* spica = taiyin::tsc1_catalog_find(&catalog, "HIP 65474");
    expect_true(spica != 0, "find Spica through normalized HIP alias", failures);
    if (spica) {
        expect_equal_u64(spica->gaia_dr3_source_id, 6193600049863267200ULL, "Spica Gaia source", failures);
        expect_equal_u32(spica->hip_id, 65474, "Spica HIP", failures);
        expect_near(spica->ra_deg, 201.298247375, 1e-12, "Spica RA", failures);
        expect_near(spica->reference_epoch, 2016.0, 0.0, "Spica reference epoch", failures);
        expect_true((spica->flags & taiyin::TSC1_STAR_HAS_GAIA_ID) != 0, "Spica Gaia flag", failures);
        expect_true(std::strcmp(taiyin::tsc1_catalog_string(&catalog, spica->canonical_id_offset), "spica") == 0,
                    "Spica canonical string", failures);
    }

    const taiyin::Tsc1StarRecord* gc = taiyin::tsc1_catalog_find(&catalog, "galactic-center");
    expect_true(gc != 0, "find Galactic Center through normalized alias", failures);
    if (gc) {
        expect_equal_u32(gc->astrometry_source, taiyin::TSC1_SOURCE_MANUAL, "GC source", failures);
        expect_true((gc->flags & taiyin::TSC1_STAR_SPECIAL_DIRECTION) != 0, "GC special flag", failures);
        expect_near(gc->ra_deg, 266.4168371, 1e-10, "GC RA", failures);
    }

    expect_true(taiyin::tsc1_catalog_find(&catalog, "does_not_exist") == 0, "missing alias lookup fails", failures);
    taiyin::tsc1_catalog_destroy(&catalog);
}

void test_file_catalog(int* failures) {
    const char* path = "test_star_catalog_tsc1_fixture.tsc1";
    std::vector<uint8_t> bytes = make_fixture_catalog();
    {
        std::ofstream file(path, std::ios::binary);
        file.write(reinterpret_cast<const char*>(&bytes[0]), static_cast<std::streamsize>(bytes.size()));
    }

    taiyin::Tsc1Catalog catalog;
    expect_true(taiyin::tsc1_catalog_load_from_file(&catalog, path), "load fixture from file", failures);
#if defined(__APPLE__) || defined(__linux__) || defined(_WIN32)
    expect_true(catalog.file.is_mapped(), "file loader uses platform mapping", failures);
#endif
    const taiyin::Tsc1StarRecord* spica = taiyin::tsc1_catalog_find(&catalog, "spica");
    expect_true(spica != 0, "file lookup Spica", failures);
    if (spica) {
        expect_near(spica->dec_deg, -11.161319472, 1e-12, "file Spica Dec", failures);
    }
    taiyin::tsc1_catalog_destroy(&catalog);
    std::remove(path);
}

void test_rejects_invalid_magic(int* failures) {
    std::vector<uint8_t> bytes = make_fixture_catalog();
    bytes[0] = 'B';
    taiyin::Tsc1Catalog catalog;
    expect_true(!taiyin::tsc1_catalog_load_from_memory(&catalog, &bytes[0], bytes.size()), "reject bad magic", failures);
}

bool file_exists(const char* path) {
    std::ifstream file(path, std::ios::binary);
    return static_cast<bool>(file);
}

void test_real_generated_catalog_if_available(int* failures) {
    const char* path = "data/stars/catalogs/stars-fixed-traditional.tsc1";
    if (!file_exists(path)) {
        std::cout << "test_star_catalog_tsc1: skipping real generated catalog smoke; " << path << " not found\n";
        return;
    }

    taiyin::Tsc1StarProvider provider(1024 * 1024, 101);
    expect_true(provider.load_from_file(path), "real catalog provider load", failures);

    taiyin::Tsc1ResolvedStar resolved;
    expect_true(provider.resolve("spica", &resolved), "real catalog resolves Spica", failures);
    if (resolved.record) {
        expect_true(resolved.record->reference_epoch > 1900.0, "real Spica has plausible reference epoch", failures);
        expect_true(resolved.record->reference_epoch < 2100.0, "real Spica has bounded reference epoch", failures);
    }

    taiyin::Vector3 position;
    expect_true(provider.eval_position("hip_65474", 2451545.0, &position), "real catalog evaluates Spica by HIP", failures);
    double ra_rad = 0.0;
    double dec_rad = 0.0;
    double radius = 0.0;
    taiyin::cartesian_to_spherical(position, &ra_rad, &dec_rad, &radius);
    ra_rad = taiyin::normalize_radians(ra_rad);
    const double ra_deg = ra_rad * taiyin::TAIYIN_RAD_TO_DEG;
    const double dec_deg = dec_rad * taiyin::TAIYIN_RAD_TO_DEG;
    expect_true(ra_deg > 200.0 && ra_deg < 203.0, "real Spica RA plausible", failures);
    expect_true(dec_deg > -12.0 && dec_deg < -10.0, "real Spica Dec plausible", failures);
    expect_true(provider.cache_entry_count() >= 1, "real catalog provider caches evaluated star", failures);
}

void test_provider_runtime_path(int* failures) {
    std::vector<uint8_t> bytes = make_fixture_catalog();
    taiyin::Tsc1StarProvider provider(1024 * 1024, 7);
    expect_true(provider.load_from_memory(&bytes[0], bytes.size()), "provider load from memory", failures);
    expect_equal_u32(provider.catalog()->header ? provider.catalog()->header->star_count : 0, 2,
                     "provider catalog star count", failures);

    taiyin::Tsc1ResolvedStar resolved;
    expect_true(provider.resolve("HIP 65474", &resolved), "provider resolve normalized HIP alias", failures);
    expect_equal_u32(resolved.star_index, 0, "provider resolved star index", failures);
    expect_true(resolved.star_id != 0, "provider assigned star id", failures);

    int queried_id = 0;
    expect_true(taiyin::internal::query_celestial_body_id("spica", &queried_id), "provider registers canonical id", failures);
    expect_equal_u32(static_cast<uint32_t>(queried_id), static_cast<uint32_t>(resolved.star_id),
                     "registered canonical id matches", failures);
    expect_true(taiyin::internal::query_celestial_body_id("hip_65474", &queried_id), "provider registers alias", failures);
    expect_equal_u32(static_cast<uint32_t>(queried_id), static_cast<uint32_t>(resolved.star_id),
                     "registered alias id matches", failures);

    expect_true(!provider.contains_cached_star("spica"), "provider cache starts cold", failures);
    expect_equal_u32(static_cast<uint32_t>(provider.cache_entry_count()), 0, "provider initial cache entries", failures);

    taiyin::Vector3 position;
    expect_true(provider.eval_position("spica", 2457389.0, &position), "provider eval position", failures);
    expect_equal_u32(static_cast<uint32_t>(provider.cache_entry_count()), 1, "provider caches after eval", failures);
    expect_true(provider.contains_cached_star("hr_5056"), "provider cache shared across aliases", failures);

    double ra_rad = 0.0;
    double dec_rad = 0.0;
    double radius = 0.0;
    taiyin::cartesian_to_spherical(position, &ra_rad, &dec_rad, &radius);
    ra_rad = taiyin::normalize_radians(ra_rad);
    expect_near(ra_rad * taiyin::TAIYIN_RAD_TO_DEG, 201.298247375, 1e-12, "provider RA", failures);
    expect_near(dec_rad * taiyin::TAIYIN_RAD_TO_DEG, -11.161319472, 1e-12, "provider Dec", failures);

    taiyin::CartesianState state;
    expect_true(provider.eval_state("hr_5056", 2457389.0 + 20.0, &state), "provider eval state via alias", failures);
    expect_near(state.acceleration_au_per_day2.x, 0.0, 0.0, "provider acceleration x", failures);
    expect_near(state.acceleration_au_per_day2.y, 0.0, 0.0, "provider acceleration y", failures);
    expect_near(state.acceleration_au_per_day2.z, 0.0, 0.0, "provider acceleration z", failures);
    expect_equal_u32(static_cast<uint32_t>(provider.cache_entry_count()), 1, "provider alias hit keeps one cache entry", failures);

    taiyin::Vector3 missing;
    expect_true(!provider.eval_position("missing_star", 2457389.0, &missing), "provider missing alias fails", failures);

    provider.close();
    expect_equal_u32(static_cast<uint32_t>(provider.cache_entry_count()), 0, "provider close clears cache", failures);
}

void test_star_evaluator_and_block_cache(int* failures) {
    std::vector<uint8_t> bytes = make_fixture_catalog();
    taiyin::Tsc1Catalog catalog;
    if (!taiyin::tsc1_catalog_load_from_memory(&catalog, &bytes[0], bytes.size())) {
        std::cerr << "FAIL: cannot load fixture for evaluator test\n";
        ++(*failures);
        return;
    }

    const taiyin::Tsc1StarRecord* spica = taiyin::tsc1_catalog_find(&catalog, "spica");
    expect_true(spica != 0, "Spica record for evaluator", failures);
    if (!spica) {
        taiyin::tsc1_catalog_destroy(&catalog);
        return;
    }

    taiyin::Tsc1StarEphemerisData* data = 0;
    expect_true(taiyin::tsc1_compile_star_ephemeris_data(spica, 0, &data), "compile Spica evaluator", failures);
    if (data) {
        expect_near(data->reference_jd_tdb, 2457389.0, 0.0, "Spica Gaia reference JD", failures);

        taiyin::Vector3 position;
        expect_true(taiyin::calc_tsc1_star_position(data->reference_jd_tdb, data, &position), "position at reference epoch", failures);
        double ra_rad = 0.0;
        double dec_rad = 0.0;
        double radius = 0.0;
        taiyin::cartesian_to_spherical(position, &ra_rad, &dec_rad, &radius);
        ra_rad = taiyin::normalize_radians(ra_rad);
        expect_near(ra_rad * taiyin::TAIYIN_RAD_TO_DEG, 201.298247375, 1e-12, "reference RA", failures);
        expect_near(dec_rad * taiyin::TAIYIN_RAD_TO_DEG, -11.161319472, 1e-12, "reference Dec", failures);

        taiyin::Vector3 velocity;
        taiyin::Vector3 acceleration;
        expect_true(taiyin::calc_tsc1_star_velocity(data->reference_jd_tdb + 10.0, data, &velocity), "constant velocity", failures);
        expect_true(taiyin::calc_tsc1_star_acceleration(data->reference_jd_tdb + 10.0, data, &acceleration), "zero acceleration", failures);
        expect_near(acceleration.x, 0.0, 0.0, "acceleration x", failures);
        expect_near(acceleration.y, 0.0, 0.0, "acceleration y", failures);
        expect_near(acceleration.z, 0.0, 0.0, "acceleration z", failures);
        taiyin::destroy_tsc1_star_ephemeris_data(data);
    }

    const int spica_id = 1000000000;
    taiyin::internal::StorageEphemerisBlock storage;
    expect_true(taiyin::tsc1_compile_star_storage_block(&catalog, 0, spica_id, &storage), "compile storage block", failures);

    taiyin::internal::CompiledEphemerisBlock compiled;
    expect_true(taiyin::internal::get_compiled_block_from_storage(&storage, spica_id, &compiled), "get compiled block", failures);
    if (compiled.position) {
        taiyin::Vector3 ref_position;
        expect_true(taiyin::internal::eval_compiled_ephemeris_block_position(2457389.0, &compiled, &ref_position),
                    "eval compiled block position", failures);
        double ra_rad = 0.0;
        double dec_rad = 0.0;
        double radius = 0.0;
        taiyin::cartesian_to_spherical(ref_position, &ra_rad, &dec_rad, &radius);
        ra_rad = taiyin::normalize_radians(ra_rad);
        expect_near(ra_rad * taiyin::TAIYIN_RAD_TO_DEG, 201.298247375, 1e-12, "compiled block RA", failures);
    }

    taiyin::internal::EphemerisBlockCache cache(1024 * 1024);
    taiyin::internal::EphemerisRouteKey key(spica_id, 0, 1001, 1);
    expect_true(cache.insert(key, 2000000.0, 3000000.0, &storage), "insert TSC1 block cache", failures);
    expect_true(storage.data_vector.empty(), "cache insert moves storage", failures);
    taiyin::Vector3 cached_position;
    expect_true(cache.eval_position(key, 2457389.0, &cached_position), "eval position from block cache", failures);
    double cached_ra_rad = 0.0;
    double cached_dec_rad = 0.0;
    double cached_radius = 0.0;
    taiyin::cartesian_to_spherical(cached_position, &cached_ra_rad, &cached_dec_rad, &cached_radius);
    cached_ra_rad = taiyin::normalize_radians(cached_ra_rad);
    expect_near(cached_ra_rad * taiyin::TAIYIN_RAD_TO_DEG, 201.298247375, 1e-12, "cached RA", failures);

    taiyin::tsc1_catalog_destroy(&catalog);
}

}  // namespace

int main() {
    int failures = 0;
    test_memory_catalog(&failures);
    test_file_catalog(&failures);
    test_rejects_invalid_magic(&failures);
    test_star_evaluator_and_block_cache(&failures);
    test_provider_runtime_path(&failures);
    test_real_generated_catalog_if_available(&failures);

    if (failures == 0) {
        std::cout << "test_star_catalog_tsc1: ALL TESTS PASSED\n";
        return 0;
    }
    std::cerr << failures << " test_star_catalog_tsc1 failure(s)\n";
    return 1;
}
