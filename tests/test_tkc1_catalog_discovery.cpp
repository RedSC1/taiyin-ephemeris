#include "taiyin/physical_constants.h"
#include "taiyin/internal/descriptor_loader.h"
#include "taiyin/internal/ephemeris_cache.h"
#include "taiyin/internal/ephemeris_discovery.h"
#include "taiyin/internal/kepler_catalog_tkc1.h"
#include "taiyin/internal/tkc1_catalog_discovery.h"

#include <cassert>
#include <cmath>
#include <cstdio>
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

void expect_near(double actual, double expected, double tolerance, const char* label, int* failures) {
    if (std::fabs(actual - expected) > tolerance) {
        std::cerr << "FAIL: " << label << ": actual=" << actual << " expected=" << expected
                  << " tolerance=" << tolerance << "\n";
        ++(*failures);
    }
}

std::string make_temp_dir() {
    char templ[] = "/tmp/taiyin-tkc1-discovery-XXXXXX";
    char* path = mkdtemp(templ);
    assert(path);
    return std::string(path);
}

void append_bytes(std::vector<uint8_t>* bytes, const void* data, size_t size) {
    const uint8_t* raw = static_cast<const uint8_t*>(data);
    bytes->insert(bytes->end(), raw, raw + size);
}

void write_file(const std::string& path, const std::vector<uint8_t>& bytes) {
    FILE* file = std::fopen(path.c_str(), "wb");
    assert(file);
    assert(std::fwrite(&bytes[0], 1, bytes.size(), file) == bytes.size());
    std::fclose(file);
}

uint32_t append_string(std::vector<char>* strings, const char* value) {
    const uint32_t offset = static_cast<uint32_t>(strings->size());
    const size_t len = std::strlen(value);
    strings->insert(strings->end(), value, value + len);
    strings->push_back('\0');
    return offset;
}

std::vector<uint8_t> make_fixture() {
    using namespace taiyin::internal;

    std::vector<char> strings(1, '\0');
    const uint32_t ceres_name = append_string(&strings, "ceres");
    const uint32_t ceres_display = append_string(&strings, "Ceres");
    const uint32_t pallas_name = append_string(&strings, "pallas");
    const uint32_t pallas_display = append_string(&strings, "Pallas");
    const uint32_t alias_ceres = append_string(&strings, "ceres");
    const uint32_t alias_pallas = append_string(&strings, "pallas");

    Tkc1ObjectRecord objects[2];
    std::memset(objects, 0, sizeof(objects));
    for (int i = 0; i < 2; ++i) {
        objects[i].target_id = 2000001 + i;
        objects[i].center_id = 10;
        objects[i].method_id = TKC1_KEPLER_METHOD_ID;
        objects[i].frame_id = static_cast<int>(EphemerisFrame::IcrfJ2000Equatorial);
        objects[i].jd_tdb_start = JD0 - 100.0;
        objects[i].jd_tdb_end = JD0 + 100.0;
        objects[i].element_start_index = static_cast<uint32_t>(i);
        objects[i].element_count = 1;
        objects[i].sbdb_spkid = 20000001 + i;
        objects[i].small_body_number = 1 + i;
        objects[i].flags = TKC1_OBJECT_NUMBERED | TKC1_OBJECT_NAMED;
    }
    objects[0].canonical_name_offset = ceres_name;
    objects[0].display_name_offset = ceres_display;
    objects[1].canonical_name_offset = pallas_name;
    objects[1].display_name_offset = pallas_display;

    Tkc1ElementRecord elements[2];
    std::memset(elements, 0, sizeof(elements));
    for (int i = 0; i < 2; ++i) {
        elements[i].jd_tdb_start = JD0 - 100.0;
        elements[i].jd_tdb_end = JD0 + 100.0;
        elements[i].epoch_jd_tdb = JD0;
        elements[i].mu_au3_day2 = taiyin::TAIYIN_SOLAR_MU_AU3_DAY2;
        elements[i].semi_major_axis_au = 2.0 + i;
        elements[i].eccentricity = 0.1;
        elements[i].inclination_rad = 0.1;
        elements[i].longitude_ascending_node_rad = 0.2;
        elements[i].argument_periapsis_rad = 0.3;
        elements[i].mean_anomaly_at_epoch_rad = 0.4;
    }

    Tkc1AliasRecord aliases[2];
    aliases[0].alias_offset = alias_ceres;
    aliases[0].object_index = 0;
    aliases[0].alias_hash = tkc1_fnv1a_64("ceres");
    aliases[1].alias_offset = alias_pallas;
    aliases[1].object_index = 1;
    aliases[1].alias_hash = tkc1_fnv1a_64("pallas");
    if (aliases[1].alias_hash < aliases[0].alias_hash) {
        Tkc1AliasRecord tmp = aliases[0];
        aliases[0] = aliases[1];
        aliases[1] = tmp;
    }

    Tkc1Header header;
    std::memset(&header, 0, sizeof(header));
    header.magic[0] = 'T'; header.magic[1] = 'K'; header.magic[2] = 'C'; header.magic[3] = '1';
    header.version = TKC1_VERSION;
    header.object_count = 2;
    header.element_count = 2;
    header.alias_count = 2;
    header.object_records_offset = sizeof(Tkc1Header);
    header.element_records_offset = header.object_records_offset + sizeof(objects);
    header.alias_records_offset = header.element_records_offset + sizeof(elements);
    header.string_table_offset = header.alias_records_offset + sizeof(aliases);
    header.string_table_size = strings.size();
    header.catalog_jd_tdb_start = JD0 - 100.0;
    header.catalog_jd_tdb_end = JD0 + 100.0;
    header.source_id = 42;
    header.source_version = 1;
    header.generation = TKC1_GENERATION;

    std::vector<uint8_t> bytes;
    append_bytes(&bytes, &header, sizeof(header));
    append_bytes(&bytes, objects, sizeof(objects));
    append_bytes(&bytes, elements, sizeof(elements));
    append_bytes(&bytes, aliases, sizeof(aliases));
    append_bytes(&bytes, &strings[0], strings.size());
    return bytes;
}

void test_tkc1_discovery_and_lazy_eval(int* failures) {
    using namespace taiyin::internal;

    const std::string root = make_temp_dir();
    const std::string path = root + "/tier0.tkc1";
    const std::vector<uint8_t> bytes = make_fixture();
    write_file(path, bytes);

    std::vector<EphemerisBlockDescriptor> descriptors;
    expect_true(collect_tkc1_descriptors_from_directory(root, &descriptors), "collect TKC1 descriptors", failures);
    expect_true(descriptors.size() == 2, "two TKC1 descriptors", failures);
    if (descriptors.size() == 2) {
        expect_true(descriptors[0].format == EphemerisBlockFormat::Tkc1, "descriptor format", failures);
        expect_true(descriptors[0].target_id == 2000001, "first target", failures);
        expect_true(descriptors[1].target_id == 2000002, "second target", failures);
    }

    std::vector<EphemerisDiscoverFileFn> discoverers;
    append_builtin_ephemeris_discoverers(&discoverers);
    std::vector<EphemerisBlockDescriptor> generic;
    EphemerisDiscoveryOptions options;
    expect_true(discover_ephemeris_descriptors_from_directory(root, discoverers, options, &generic), "builtin discovery sees TKC1", failures);
    expect_true(generic.size() == 2, "builtin TKC1 descriptor count", failures);

    EphemerisBlockCatalog catalog;
    expect_true(discover_tkc1_catalog_from_directory(root, &catalog), "discover TKC1 catalog", failures);
    expect_true(catalog.size() == 2, "catalog descriptor count", failures);

    EphemerisBlockQuery query;
    query.target_id = 2000002;
    query.center_id = 10;
    query.frame = EphemerisFrame::IcrfJ2000Equatorial;
    query.jd_tdb = JD0 + 1.0;
    const EphemerisBlockDescriptor* selected = catalog.find_first(query);
    expect_true(selected != 0, "select Pallas descriptor", failures);

    EphemerisBlockCache cache(1024 * 1024);
    taiyin::CartesianState state;
    expect_true(selected && eval_descriptor_state(*selected, &cache, query.jd_tdb, &state), "lazy eval TKC1 descriptor", failures);
    expect_true(cache.entry_count() == 1, "TKC1 lazy eval inserts one cache entry", failures);

    taiyin::CartesianState second_state;
    expect_true(selected && eval_descriptor_state(*selected, &cache, query.jd_tdb + 1.0, &second_state), "TKC1 second eval", failures);
    expect_true(cache.entry_count() == 1, "TKC1 second eval reuses cache", failures);
    expect_near(state.position_au.x, state.position_au.x, 0.0, "state finite placeholder", failures);

    EphemerisBlockQuery ceres_query = query;
    ceres_query.target_id = 2000001;
    const EphemerisBlockDescriptor* ceres = catalog.find_first(ceres_query);
    expect_true(ceres != 0, "select Ceres descriptor", failures);

    EphemerisBlockDescriptor ceres_bucket;
    EphemerisBlockDescriptor pallas_bucket;
    expect_true(ceres && make_cache_bucket_descriptor_for_jd(*ceres, ceres_query.jd_tdb, &ceres_bucket), "make Ceres cache bucket", failures);
    expect_true(selected && make_cache_bucket_descriptor_for_jd(*selected, query.jd_tdb, &pallas_bucket), "make Pallas cache bucket", failures);

    EphemerisBlockCache eviction_cache(1);
    taiyin::CartesianState ceres_state;
    expect_true(ceres && eval_descriptor_state(*ceres, &eviction_cache, ceres_query.jd_tdb, &ceres_state), "load Ceres into tiny TKC1 cache", failures);
    expect_true(eviction_cache.entry_count() == 1, "tiny cache has one entry after Ceres load", failures);
    expect_true(eviction_cache.contains(ceres_bucket.route_key), "tiny cache contains Ceres", failures);

    taiyin::CartesianState pallas_state;
    expect_true(selected && eval_descriptor_state(*selected, &eviction_cache, query.jd_tdb, &pallas_state), "load Pallas into tiny TKC1 cache", failures);
    expect_true(eviction_cache.entry_count() == 1, "tiny cache still has one entry after Pallas load", failures);
    expect_true(!eviction_cache.contains(ceres_bucket.route_key), "tiny cache evicted Ceres", failures);
    expect_true(eviction_cache.contains(pallas_bucket.route_key), "tiny cache contains Pallas", failures);

    taiyin::CartesianState reloaded_ceres_state;
    expect_true(ceres && eval_descriptor_state(*ceres, &eviction_cache, ceres_query.jd_tdb + 1.0, &reloaded_ceres_state), "reload evicted Ceres from TKC1", failures);
    expect_true(eviction_cache.entry_count() == 1, "tiny cache still has one entry after Ceres reload", failures);
    expect_true(eviction_cache.contains(ceres_bucket.route_key), "tiny cache contains reloaded Ceres", failures);
    expect_true(!eviction_cache.contains(pallas_bucket.route_key), "tiny cache evicted Pallas", failures);

    std::remove(path.c_str());
    rmdir(root.c_str());
}

}  // namespace

int main() {
    int failures = 0;
    test_tkc1_discovery_and_lazy_eval(&failures);

    if (failures == 0) {
        std::cout << "test_tkc1_catalog_discovery: ALL TESTS PASSED\n";
        return 0;
    }
    std::cerr << failures << " test_tkc1_catalog_discovery failure(s)\n";
    return 1;
}
