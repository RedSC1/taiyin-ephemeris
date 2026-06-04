#include "taiyin/internal/ephemeris_discovery.h"
#include "taiyin/internal/tsc1_catalog_discovery.h"
#include "taiyin/star_catalog_tsc1.h"

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

#if defined(_WIN32)
#include <direct.h>
#else
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace {

using namespace taiyin;
using namespace taiyin::internal;

bool near(double actual, double expected, double tolerance) {
    return std::fabs(actual - expected) <= tolerance;
}

void expect_true(bool value, const char* label, int* failures) {
    if (!value) {
        std::cerr << "FAIL: expected true: " << label << "\n";
        ++(*failures);
    }
}

void expect_equal_size(size_t actual, size_t expected, const char* label, int* failures) {
    if (actual != expected) {
        std::cerr << "FAIL: " << label << ": actual=" << actual << " expected=" << expected << "\n";
        ++(*failures);
    }
}

void expect_equal_int(int actual, int expected, const char* label, int* failures) {
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
    (void)hip_alias;
    (void)hr_alias;
    (void)gc_alias;

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

std::string join_path(const std::string& lhs, const std::string& rhs) {
    if (lhs.empty()) {
        return rhs;
    }
    if (lhs[lhs.size() - 1] == '/') {
        return lhs + rhs;
    }
    return lhs + "/" + rhs;
}

bool make_dir(const std::string& path) {
#if defined(_WIN32)
    return _mkdir(path.c_str()) == 0;
#else
    return mkdir(path.c_str(), 0755) == 0;
#endif
}

void remove_dir(const std::string& path) {
#if defined(_WIN32)
    _rmdir(path.c_str());
#else
    rmdir(path.c_str());
#endif
}

void write_bytes(const std::string& path, const std::vector<uint8_t>& bytes) {
    std::ofstream file(path.c_str(), std::ios::binary);
    file.write(reinterpret_cast<const char*>(&bytes[0]), static_cast<std::streamsize>(bytes.size()));
}

void write_text(const std::string& path, const char* value) {
    std::ofstream file(path.c_str(), std::ios::binary);
    file << value;
}

void cleanup_file(const std::string& path) {
    std::remove(path.c_str());
}

void expect_tsc1_descriptor(const EphemerisBlockDescriptor& descriptor, const std::string& path, int* failures) {
    expect_equal_int(descriptor.target_id, 0, "TSC1 descriptor target sentinel", failures);
    expect_equal_int(descriptor.center_id, TSC1_DEFAULT_CENTER_ID, "TSC1 descriptor center", failures);
    expect_equal_int(descriptor.method_id, TSC1_STAR_METHOD_ID, "TSC1 descriptor method", failures);
    expect_equal_int(static_cast<int>(descriptor.frame), static_cast<int>(EphemerisFrame::IcrfJ2000Equatorial),
                     "TSC1 descriptor frame", failures);
    expect_equal_int(static_cast<int>(descriptor.format), static_cast<int>(EphemerisBlockFormat::Tsc1),
                     "TSC1 descriptor format", failures);
    expect_true(descriptor.path == path, "TSC1 descriptor path", failures);
    expect_near(descriptor.jd_tdb_start, 2451545.0, 1e-9, "TSC1 descriptor epoch start", failures);
    expect_near(descriptor.jd_tdb_end, 2457389.0, 1e-9, "TSC1 descriptor epoch end", failures);
    expect_equal_int(descriptor.route_key.target_id, 0, "TSC1 route target", failures);
    expect_equal_int(descriptor.route_key.center_id, TSC1_DEFAULT_CENTER_ID, "TSC1 route center", failures);
    expect_equal_int(descriptor.route_key.method_id, TSC1_STAR_METHOD_ID, "TSC1 route method", failures);
    expect_equal_u64(descriptor.source_key.source_id, 3, "TSC1 source id", failures);
}

void test_discover_tsc1_file(int* failures) {
    const std::string path = "test_tsc1_catalog_discovery_fixture.tsc1";
    std::vector<uint8_t> bytes = make_fixture_catalog();
    write_bytes(path, bytes);

    std::vector<EphemerisBlockDescriptor> descriptors;
    EphemerisDiscoveryOptions options;
    const EphemerisDiscoveryStatus status = discover_tsc1_file(path, options, &descriptors);
    expect_equal_int(static_cast<int>(status), static_cast<int>(DiscoveryOk), "discover TSC1 file status", failures);
    expect_equal_size(descriptors.size(), 1, "discover TSC1 descriptor count", failures);
    if (!descriptors.empty()) {
        expect_tsc1_descriptor(descriptors[0], path, failures);
    }

    cleanup_file(path);
}

void test_non_applicable_and_invalid_files(int* failures) {
    std::vector<EphemerisBlockDescriptor> descriptors;
    EphemerisDiscoveryOptions options;
    expect_equal_int(static_cast<int>(discover_tsc1_file("not_a_catalog.txt", options, &descriptors)),
                     static_cast<int>(DiscoveryNotApplicable), "TSC1 ignores txt", failures);
    expect_equal_int(static_cast<int>(discover_tsc1_file("not_a_catalog.opm4", options, &descriptors)),
                     static_cast<int>(DiscoveryNotApplicable), "TSC1 ignores opm4", failures);

    const std::string path = "test_tsc1_catalog_discovery_invalid.tsc1";
    write_text(path, "not a tsc1 catalog");
    expect_equal_int(static_cast<int>(discover_tsc1_file(path, options, &descriptors)),
                     static_cast<int>(DiscoveryError), "TSC1 rejects invalid catalog", failures);
    cleanup_file(path);
}

void test_collect_tsc1_descriptors_from_directory(int* failures) {
    const std::string root = "test_tsc1_catalog_discovery_dir";
    const std::string subdir = join_path(root, "nested");
    make_dir(root);
    make_dir(subdir);

    std::vector<uint8_t> bytes = make_fixture_catalog();
    const std::string first = join_path(root, "first.tsc1");
    const std::string second = join_path(subdir, "second.tsc1");
    const std::string ignored = join_path(root, "ignored.txt");
    write_bytes(first, bytes);
    write_bytes(second, bytes);
    write_text(ignored, "ignore me");

    std::vector<EphemerisBlockDescriptor> descriptors;
    expect_true(collect_tsc1_descriptors_from_directory(root, &descriptors), "collect TSC1 descriptors", failures);
    expect_equal_size(descriptors.size(), 2, "collect TSC1 descriptor count", failures);
    for (size_t i = 0; i < descriptors.size(); ++i) {
        expect_equal_int(static_cast<int>(descriptors[i].format), static_cast<int>(EphemerisBlockFormat::Tsc1),
                         "collected descriptor format", failures);
    }

    cleanup_file(first);
    cleanup_file(second);
    cleanup_file(ignored);
    remove_dir(subdir);
    remove_dir(root);
}

void test_builtin_discovery_and_bucket_copy(int* failures) {
    const std::string root = "test_tsc1_catalog_builtin_dir";
    make_dir(root);

    std::vector<uint8_t> bytes = make_fixture_catalog();
    const std::string path = join_path(root, "builtin.tsc1");
    write_bytes(path, bytes);

    std::vector<EphemerisDiscoverFileFn> discoverers;
    append_builtin_ephemeris_discoverers(&discoverers);
    EphemerisDiscoveryOptions options;
    options.strict = true;

    std::vector<EphemerisBlockDescriptor> descriptors;
    expect_true(discover_ephemeris_descriptors_from_directory(root, discoverers, options, &descriptors),
                "builtin discovery finds TSC1", failures);
    expect_equal_size(descriptors.size(), 1, "builtin TSC1 descriptor count", failures);

    if (!descriptors.empty()) {
        expect_tsc1_descriptor(descriptors[0], path, failures);

        EphemerisBlockDescriptor bucket;
        expect_true(make_cache_bucket_descriptor_for_jd(descriptors[0], descriptors[0].jd_tdb_start, &bucket),
                    "TSC1 bucket descriptor copy", failures);
        expect_equal_int(static_cast<int>(bucket.format), static_cast<int>(descriptors[0].format),
                         "TSC1 bucket format unchanged", failures);
        expect_equal_int(bucket.route_key.bucket_id, descriptors[0].route_key.bucket_id,
                         "TSC1 bucket route unchanged", failures);
        expect_near(bucket.jd_tdb_start, descriptors[0].jd_tdb_start, 0.0,
                    "TSC1 bucket start unchanged", failures);
        expect_near(bucket.jd_tdb_end, descriptors[0].jd_tdb_end, 0.0,
                    "TSC1 bucket end unchanged", failures);
    }

    cleanup_file(path);
    remove_dir(root);
}

}  // namespace

int main() {
    int failures = 0;
    test_discover_tsc1_file(&failures);
    test_non_applicable_and_invalid_files(&failures);
    test_collect_tsc1_descriptors_from_directory(&failures);
    test_builtin_discovery_and_bucket_copy(&failures);

    if (failures == 0) {
        std::cout << "test_tsc1_catalog_discovery: ALL TESTS PASSED\n";
        return 0;
    }
    std::cerr << failures << " test_tsc1_catalog_discovery failure(s)\n";
    return 1;
}
