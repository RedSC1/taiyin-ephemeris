#include "taiyin/physical_constants.h"
#include "taiyin/internal/ephemeris_block.h"
#include "taiyin/internal/kepler_file.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
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
    char templ[] = "/tmp/taiyin-kepler-file-XXXXXX";
    int fd = mkstemp(templ);
    if (fd >= 0) {
        close(fd);
        std::remove(templ);
    }
    return std::string(templ) + suffix;
}

void write_text_file(const std::string& path, const char* text) {
    FILE* file = std::fopen(path.c_str(), "wb");
    if (!file) {
        return;
    }
    std::fwrite(text, 1, std::strlen(text), file);
    std::fclose(file);
}

taiyin::internal::KeplerElements make_element() {
    taiyin::internal::KeplerElements element;
    taiyin::internal::make_elliptic_kepler_elements(
        2000001,
        10,
        JD0 - 1000.0,
        JD0 + 1000.0,
        JD0,
        taiyin::TAIYIN_SOLAR_MU_AU3_DAY2,
        2.0,
        0.25,
        0.1,
        0.2,
        0.3,
        0.4,
        &element);
    return element;
}

bool eval_storage(
    taiyin::internal::StorageEphemerisBlock* storage,
    double jd_tdb,
    taiyin::CartesianState* out
) {
    taiyin::internal::CompiledEphemerisBlock block;
    if (!taiyin::internal::get_compiled_block_from_storage(storage, 2000001, &block)) {
        return false;
    }
    return taiyin::internal::eval_compiled_ephemeris_block(jd_tdb, &block, out);
}

void test_save_load_compile_roundtrip(int* failures) {
    using namespace taiyin::internal;

    const std::string path = temp_path(".tke1");
    KeplerElements element = make_element();
    expect_true(
        save_kepler_file(
            path,
            &element,
            1,
            TAIYIN_KEPLER_FILE_METHOD_ID,
            EphemerisFrame::IcrfJ2000Equatorial,
            JD0 - 500.0,
            JD0 + 500.0),
        "save Kepler file",
        failures);

    std::vector<KeplerElements> loaded;
    EphemerisBlockDescriptor descriptor;
    expect_true(load_kepler_file(path, &loaded, &descriptor), "load Kepler file", failures);
    expect_true(loaded.size() == 1, "loaded one element", failures);
    expect_true(descriptor.target_id == 2000001, "descriptor target", failures);
    expect_true(descriptor.center_id == 10, "descriptor center", failures);
    expect_true(descriptor.method_id == TAIYIN_KEPLER_FILE_METHOD_ID, "descriptor method", failures);
    expect_true(descriptor.format == EphemerisBlockFormat::Kepler, "descriptor format", failures);
    expect_true(descriptor.path == path, "descriptor path", failures);
    expect_near(loaded[0].semi_major_axis_au, 2.0, 0.0, "loaded semi-major axis", failures);

    StorageEphemerisBlock direct;
    StorageEphemerisBlock from_file;
    expect_true(compile_kepler_ephemeris_block(&element, 1, JD0 - 500.0, JD0 + 500.0, &direct), "compile direct", failures);
    expect_true(compile_kepler_file(path, JD0 - 500.0, JD0 + 500.0, &from_file), "compile file", failures);

    taiyin::CartesianState direct_state;
    taiyin::CartesianState file_state;
    expect_true(eval_storage(&direct, JD0 + 10.0, &direct_state), "eval direct", failures);
    expect_true(eval_storage(&from_file, JD0 + 10.0, &file_state), "eval file", failures);
    expect_near(file_state.position_au.x, direct_state.position_au.x, 1e-14, "position x roundtrip", failures);
    expect_near(file_state.position_au.y, direct_state.position_au.y, 1e-14, "position y roundtrip", failures);
    expect_near(file_state.velocity_au_per_day.z, direct_state.velocity_au_per_day.z, 1e-14, "velocity z roundtrip", failures);

    destroy_storage_ephemeris_block(&direct);
    destroy_storage_ephemeris_block(&from_file);
    std::remove(path.c_str());
}

void test_save_user_kepler_default_directory(int* failures) {
    using namespace taiyin::internal;

    const std::string data_root = temp_path("");
    const std::string expected_dir = data_root + "/kepler/user";
    const std::string expected_path = expected_dir + "/2000001_user_custom_orbit.tke1";
    KeplerElements element = make_element();
    std::string saved_path;
    expect_true(
        save_user_kepler_file(
            data_root,
            "User Custom Orbit",
            &element,
            1,
            EphemerisFrame::IcrfJ2000Equatorial,
            JD0 - 500.0,
            JD0 + 500.0,
            &saved_path),
        "save user Kepler file to default directory",
        failures);
    expect_true(make_user_kepler_directory(data_root) == expected_dir, "user Kepler default directory", failures);
    expect_true(saved_path == expected_path, "user Kepler default file path", failures);

    std::vector<KeplerElements> loaded;
    EphemerisBlockDescriptor descriptor;
    expect_true(load_kepler_file(saved_path, &loaded, &descriptor), "load saved user Kepler default file", failures);
    expect_true(loaded.size() == 1, "loaded saved user element", failures);
    expect_true(descriptor.path == saved_path, "saved user descriptor path", failures);

    std::remove(saved_path.c_str());
    rmdir(expected_dir.c_str());
    rmdir((data_root + "/kepler").c_str());
    rmdir(data_root.c_str());
}

void test_rejects_bad_files(int* failures) {
    using namespace taiyin::internal;

    const std::string bad_magic = temp_path(".tke1");
    write_text_file(bad_magic, "NOPE\nversion=1\n");
    std::vector<KeplerElements> loaded;
    EphemerisBlockDescriptor descriptor;
    expect_false(load_kepler_file(bad_magic, &loaded, &descriptor), "reject bad magic", failures);
    std::remove(bad_magic.c_str());

    const std::string missing = temp_path(".tke1");
    write_text_file(
        missing,
        "TKE1\nversion=1\ntarget_id=1\ncenter_id=10\nmethod_id=3001\nframe_id=1\njd_tdb_start=1\njd_tdb_end=2\nelement_count=1\n");
    expect_false(load_kepler_file(missing, &loaded, &descriptor), "reject missing element fields", failures);
    std::remove(missing.c_str());
}

}  // namespace

int main() {
    int failures = 0;
    test_save_load_compile_roundtrip(&failures);
    test_save_user_kepler_default_directory(&failures);
    test_rejects_bad_files(&failures);

    if (failures == 0) {
        std::cout << "test_kepler_file: ALL TESTS PASSED\n";
        return 0;
    }
    std::cerr << failures << " test_kepler_file failure(s)\n";
    return 1;
}
