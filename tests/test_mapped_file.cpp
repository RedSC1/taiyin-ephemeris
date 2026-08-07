#include "taiyin/internal/mapped_file.h"
#include "taiyin/internal/ephemeris_file_loader.h"

#include <cstdio>
#include <cstring>
#include <fstream>
#include <iostream>

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

void expect_size(size_t actual, size_t expected, const char* label, int* failures) {
    if (actual != expected) {
        std::cerr << "FAIL: " << label << ": actual=" << actual << " expected=" << expected << "\n";
        ++(*failures);
    }
}

}  // namespace

int main() {
    int failures = 0;
    const char* path = "test_mapped_file_fixture.bin";
    const char* contents = "taiyin mapped file fixture";
    const size_t contents_size = std::strlen(contents);

    {
        std::ofstream file(path, std::ios::binary);
        file.write(contents, static_cast<std::streamsize>(contents_size));
    }

    taiyin::internal::MappedFile file;
    expect_false(file.is_open(), "new mapped file starts closed", &failures);
    expect_false(file.is_mapped(), "new mapped file starts unmapped", &failures);
    expect_true(file.open_readonly(path), "open fixture readonly", &failures);
    expect_true(file.is_open(), "fixture is open", &failures);
    expect_size(file.size(), contents_size, "fixture size", &failures);
    expect_true(file.data() != 0, "fixture data pointer", &failures);
    if (file.data()) {
        expect_true(std::memcmp(file.data(), contents, contents_size) == 0, "fixture bytes match", &failures);
    }
#if defined(__APPLE__) || defined(__linux__) || defined(_WIN32)
    expect_true(file.is_mapped(), "fixture uses platform mapping", &failures);
#endif

    file.close();
    expect_false(file.is_open(), "close resets open state", &failures);
    expect_false(file.is_mapped(), "close resets mapped state", &failures);
    expect_size(file.size(), 0, "close resets size", &failures);
    expect_true(file.data() == 0, "close resets data pointer", &failures);

    taiyin::internal::EphemerisFileView view;
    expect_true(view.open_readonly(path), "open fixture as transient ephemeris view", &failures);
    expect_true(view.is_open(), "ephemeris view is open", &failures);
    expect_false(view.is_decompressed(), "plain fixture is not decompressed", &failures);
    expect_size(view.size(), contents_size, "ephemeris view size", &failures);
    if (view.data()) {
        expect_true(std::memcmp(view.data(), contents, contents_size) == 0, "ephemeris view bytes match", &failures);
    }
#if defined(__APPLE__) || defined(__linux__) || defined(_WIN32)
    expect_true(view.is_mapped(), "plain ephemeris view uses platform mapping", &failures);
#endif
    view.close();
    expect_false(view.is_open(), "closed ephemeris view is not open", &failures);
    expect_false(view.is_mapped(), "closed ephemeris view is not mapped", &failures);
    expect_false(view.is_decompressed(), "closed ephemeris view is not decompressed", &failures);
    expect_size(view.size(), 0, "closed ephemeris view size", &failures);
    expect_true(view.data() == 0, "closed ephemeris view data pointer", &failures);

    const char* gzip_path = "test_mapped_file_fixture.bin.gz";
    const unsigned char gzip_contents[] = {
        0x1f, 0x8b, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x03, 0x2b,
        0x49, 0xcc, 0xac, 0xcc, 0xcc, 0x53, 0xc8, 0x4d, 0x2c, 0x28, 0x48,
        0x4d, 0x51, 0x48, 0xcb, 0xcc, 0x49, 0x05, 0x12, 0x15, 0x25, 0xa5,
        0x45, 0xa9, 0x00, 0xca, 0x44, 0xc4, 0x72, 0x1a, 0x00, 0x00, 0x00
    };
    {
        std::ofstream gzip_file(gzip_path, std::ios::binary);
        gzip_file.write(
            reinterpret_cast<const char*>(gzip_contents),
            static_cast<std::streamsize>(sizeof(gzip_contents)));
    }
    expect_true(view.open_readonly(gzip_path), "open gzip ephemeris view", &failures);
    expect_true(view.is_open(), "gzip ephemeris view is open", &failures);
    expect_false(view.is_mapped(), "gzip ephemeris view releases compressed mapping", &failures);
    expect_true(view.is_decompressed(), "gzip ephemeris view owns decompressed bytes", &failures);
    expect_size(view.size(), contents_size, "gzip ephemeris view size", &failures);
    if (view.data()) {
        expect_true(std::memcmp(view.data(), contents, contents_size) == 0, "gzip ephemeris bytes match", &failures);
    }
    view.close();
    expect_false(view.is_open(), "closed gzip view is not open", &failures);
    expect_false(view.is_decompressed(), "closed gzip view releases decompressed bytes", &failures);

    expect_false(file.open_readonly("missing_mapped_file_fixture.bin"), "missing file open fails", &failures);
    expect_false(file.is_open(), "missing file leaves closed", &failures);

    std::remove(path);
    std::remove(gzip_path);

    if (failures == 0) {
        std::cout << "test_mapped_file: ALL TESTS PASSED\n";
        return 0;
    }
    std::cerr << failures << " test_mapped_file failure(s)\n";
    return 1;
}
