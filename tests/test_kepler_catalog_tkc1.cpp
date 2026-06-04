#include "taiyin/physical_constants.h"
#include "taiyin/internal/kepler_catalog_tkc1.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <string>
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

std::string temp_path(const char* suffix) {
    char templ[] = "/tmp/taiyin-tkc1-XXXXXX";
    int fd = mkstemp(templ);
    if (fd >= 0) {
        close(fd);
        std::remove(templ);
    }
    return std::string(templ) + suffix;
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

struct StringTableBuilder {
    std::vector<char> data;

    StringTableBuilder() : data(1, '\0') {}

    uint32_t add(const char* value) {
        if (!value || !value[0]) {
            return 0;
        }
        const uint32_t offset = static_cast<uint32_t>(data.size());
        const size_t len = std::strlen(value);
        data.insert(data.end(), value, value + len);
        data.push_back('\0');
        return offset;
    }
};

taiyin::internal::Tkc1ElementRecord make_element(double a_au, double e, double ma) {
    taiyin::internal::Tkc1ElementRecord element;
    element.jd_tdb_start = JD0 - 500.0;
    element.jd_tdb_end = JD0 + 500.0;
    element.epoch_jd_tdb = JD0;
    element.mu_au3_day2 = taiyin::TAIYIN_SOLAR_MU_AU3_DAY2;
    element.semi_major_axis_au = a_au;
    element.eccentricity = e;
    element.inclination_rad = 0.1;
    element.longitude_ascending_node_rad = 0.2;
    element.argument_periapsis_rad = 0.3;
    element.mean_anomaly_at_epoch_rad = ma;
    return element;
}

std::vector<uint8_t> make_fixture(bool unsorted_aliases = false, bool bad_magic = false) {
    using namespace taiyin::internal;

    StringTableBuilder strings;
    const uint32_t ceres_canonical = strings.add("ceres");
    const uint32_t ceres_display = strings.add("Ceres");
    const uint32_t pallas_canonical = strings.add("pallas");
    const uint32_t pallas_display = strings.add("Pallas");
    const uint32_t alias_ceres = strings.add("1_ceres");
    const uint32_t alias_pallas = strings.add("2_pallas");

    Tkc1ObjectRecord objects[2];
    std::memset(objects, 0, sizeof(objects));
    objects[0].target_id = 2000001;
    objects[0].center_id = 10;
    objects[0].method_id = TKC1_KEPLER_METHOD_ID;
    objects[0].frame_id = static_cast<int>(EphemerisFrame::IcrfJ2000Equatorial);
    objects[0].jd_tdb_start = JD0 - 500.0;
    objects[0].jd_tdb_end = JD0 + 500.0;
    objects[0].element_start_index = 0;
    objects[0].element_count = 1;
    objects[0].canonical_name_offset = ceres_canonical;
    objects[0].display_name_offset = ceres_display;
    objects[0].sbdb_spkid = 20000001;
    objects[0].small_body_number = 1;
    objects[0].flags = TKC1_OBJECT_NUMBERED | TKC1_OBJECT_NAMED;

    objects[1].target_id = 2000002;
    objects[1].center_id = 10;
    objects[1].method_id = TKC1_KEPLER_METHOD_ID;
    objects[1].frame_id = static_cast<int>(EphemerisFrame::IcrfJ2000Equatorial);
    objects[1].jd_tdb_start = JD0 - 500.0;
    objects[1].jd_tdb_end = JD0 + 500.0;
    objects[1].element_start_index = 1;
    objects[1].element_count = 1;
    objects[1].canonical_name_offset = pallas_canonical;
    objects[1].display_name_offset = pallas_display;
    objects[1].sbdb_spkid = 20000002;
    objects[1].small_body_number = 2;
    objects[1].flags = TKC1_OBJECT_NUMBERED | TKC1_OBJECT_NAMED;

    Tkc1ElementRecord elements[2];
    elements[0] = make_element(2.0, 0.1, 0.4);
    elements[1] = make_element(3.0, 0.2, 0.5);

    Tkc1AliasRecord aliases[2];
    aliases[0].alias_offset = alias_ceres;
    aliases[0].object_index = 0;
    aliases[0].alias_hash = tkc1_fnv1a_64("1_ceres");
    aliases[1].alias_offset = alias_pallas;
    aliases[1].object_index = 1;
    aliases[1].alias_hash = tkc1_fnv1a_64("2_pallas");
    std::sort(aliases, aliases + 2, [&strings](const Tkc1AliasRecord& lhs, const Tkc1AliasRecord& rhs) {
        if (lhs.alias_hash != rhs.alias_hash) {
            return lhs.alias_hash < rhs.alias_hash;
        }
        return std::strcmp(&strings.data[lhs.alias_offset], &strings.data[rhs.alias_offset]) < 0;
    });
    if (unsorted_aliases) {
        std::swap(aliases[0], aliases[1]);
    }

    Tkc1Header header;
    std::memset(&header, 0, sizeof(header));
    header.magic[0] = bad_magic ? 'B' : 'T';
    header.magic[1] = 'K';
    header.magic[2] = 'C';
    header.magic[3] = '1';
    header.version = TKC1_VERSION;
    header.object_count = 2;
    header.element_count = 2;
    header.alias_count = 2;
    header.object_records_offset = sizeof(Tkc1Header);
    header.element_records_offset = header.object_records_offset + sizeof(objects);
    header.alias_records_offset = header.element_records_offset + sizeof(elements);
    header.string_table_offset = header.alias_records_offset + sizeof(aliases);
    header.string_table_size = strings.data.size();
    header.catalog_jd_tdb_start = JD0 - 500.0;
    header.catalog_jd_tdb_end = JD0 + 500.0;
    header.source_id = 42;
    header.source_version = 1;
    header.generation = TKC1_GENERATION;

    std::vector<uint8_t> bytes;
    append_bytes(&bytes, &header, sizeof(header));
    append_bytes(&bytes, objects, sizeof(objects));
    append_bytes(&bytes, elements, sizeof(elements));
    append_bytes(&bytes, aliases, sizeof(aliases));
    append_bytes(&bytes, &strings.data[0], strings.data.size());
    return bytes;
}

void test_load_lookup_and_compile(int* failures) {
    using namespace taiyin::internal;

    const std::vector<uint8_t> bytes = make_fixture();
    Tkc1Catalog catalog;
    expect_true(tkc1_catalog_load_from_memory(&catalog, &bytes[0], bytes.size()), "load TKC1 memory fixture", failures);
    expect_true(catalog.header && catalog.header->object_count == 2, "object count", failures);

    uint32_t object_index = 999;
    expect_true(tkc1_catalog_find_object_index_by_target_id(&catalog, 2000002, &object_index), "find Pallas target", failures);
    expect_true(object_index == 1, "Pallas object index", failures);
    expect_true(tkc1_catalog_find_object_index_by_alias(&catalog, "(1) Ceres", &object_index), "find Ceres alias", failures);
    expect_true(object_index == 0, "Ceres alias index", failures);
    expect_false(tkc1_catalog_find_object_index_by_alias(&catalog, "missing", &object_index), "missing alias fails", failures);

    StorageEphemerisBlock from_catalog;
    expect_true(
        tkc1_compile_object_storage_block(&catalog, 0, JD0 - 100.0, JD0 + 100.0, &from_catalog),
        "compile Ceres from TKC1",
        failures);

    KeplerElements direct_element;
    expect_true(
        make_elliptic_kepler_elements(
            2000001,
            10,
            JD0 - 500.0,
            JD0 + 500.0,
            JD0,
            taiyin::TAIYIN_SOLAR_MU_AU3_DAY2,
            2.0,
            0.1,
            0.1,
            0.2,
            0.3,
            0.4,
            &direct_element),
        "make direct element",
        failures);
    StorageEphemerisBlock direct;
    expect_true(compile_kepler_ephemeris_block(&direct_element, 1, JD0 - 100.0, JD0 + 100.0, &direct), "compile direct", failures);

    CompiledEphemerisBlock catalog_block;
    CompiledEphemerisBlock direct_block;
    expect_true(get_compiled_block_from_storage(&from_catalog, 2000001, &catalog_block), "compiled catalog block", failures);
    expect_true(get_compiled_block_from_storage(&direct, 2000001, &direct_block), "compiled direct block", failures);
    taiyin::CartesianState catalog_state;
    taiyin::CartesianState direct_state;
    expect_true(eval_compiled_ephemeris_block(JD0 + 5.0, &catalog_block, &catalog_state), "eval catalog", failures);
    expect_true(eval_compiled_ephemeris_block(JD0 + 5.0, &direct_block, &direct_state), "eval direct", failures);
    expect_near(catalog_state.position_au.x, direct_state.position_au.x, 1e-14, "position x", failures);
    expect_near(catalog_state.velocity_au_per_day.y, direct_state.velocity_au_per_day.y, 1e-14, "velocity y", failures);

    destroy_storage_ephemeris_block(&from_catalog);
    destroy_storage_ephemeris_block(&direct);
    tkc1_catalog_destroy(&catalog);
}

void test_file_and_rejections(int* failures) {
    using namespace taiyin::internal;

    const std::string path = temp_path(".tkc1");
    const std::vector<uint8_t> bytes = make_fixture();
    write_file(path, bytes);
    Tkc1Catalog catalog;
    expect_true(tkc1_catalog_load_from_file(&catalog, path), "load TKC1 from file", failures);
    tkc1_catalog_destroy(&catalog);

    StorageEphemerisBlock storage;
    expect_true(
        tkc1_compile_object_storage_block_from_file(path, 1, JD0 - 50.0, JD0 + 50.0, &storage),
        "compile object from file",
        failures);
    destroy_storage_ephemeris_block(&storage);
    std::remove(path.c_str());

    const std::vector<uint8_t> bad_magic = make_fixture(false, true);
    expect_false(tkc1_catalog_load_from_memory(&catalog, &bad_magic[0], bad_magic.size()), "reject bad magic", failures);

    const std::vector<uint8_t> bad_aliases = make_fixture(true, false);
    expect_false(tkc1_catalog_load_from_memory(&catalog, &bad_aliases[0], bad_aliases.size()), "reject unsorted aliases", failures);
}

}  // namespace

int main() {
    int failures = 0;
    test_load_lookup_and_compile(&failures);
    test_file_and_rejections(&failures);

    if (failures == 0) {
        std::cout << "test_kepler_catalog_tkc1: ALL TESTS PASSED\n";
        return 0;
    }
    std::cerr << failures << " test_kepler_catalog_tkc1 failure(s)\n";
    return 1;
}
