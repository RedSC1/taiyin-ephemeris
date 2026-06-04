#include "taiyin/internal/mapped_file.h"

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

    expect_false(file.open_readonly("missing_mapped_file_fixture.bin"), "missing file open fails", &failures);
    expect_false(file.is_open(), "missing file leaves closed", &failures);

    std::remove(path);

    if (failures == 0) {
        std::cout << "test_mapped_file: ALL TESTS PASSED\n";
        return 0;
    }
    std::cerr << failures << " test_mapped_file failure(s)\n";
    return 1;
}
