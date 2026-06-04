#include "taiyin/internal/mapped_file.h"

#include <fstream>
#include <limits>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#if defined(__APPLE__) || defined(__linux__)
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace taiyin {
namespace internal {

MappedFile::MappedFile() noexcept
    : data_(0),
      size_(0),
      mapped_(false),
      owned_buffer_(),
      file_handle_(0),
      mapping_handle_(0),
      fd_(-1) {}

MappedFile::~MappedFile() noexcept {
    close();
}

bool MappedFile::open_readonly(const std::string& path) noexcept {
    close();
    if (open_readonly_mapped(path)) {
        return true;
    }
    close();
    return open_readonly_owned(path);
}

void MappedFile::close() noexcept {
#if defined(_WIN32)
    if (mapped_ && data_) {
        UnmapViewOfFile(data_);
    }
    if (mapping_handle_) {
        CloseHandle(static_cast<HANDLE>(mapping_handle_));
    }
    if (file_handle_) {
        CloseHandle(static_cast<HANDLE>(file_handle_));
    }
#elif defined(__APPLE__) || defined(__linux__)
    if (mapped_ && data_ && size_ > 0) {
        munmap(const_cast<uint8_t*>(data_), size_);
    }
    if (fd_ >= 0) {
        ::close(fd_);
    }
#endif
    data_ = 0;
    size_ = 0;
    mapped_ = false;
    owned_buffer_.clear();
    file_handle_ = 0;
    mapping_handle_ = 0;
    fd_ = -1;
}

const uint8_t* MappedFile::data() const noexcept {
    return data_;
}

size_t MappedFile::size() const noexcept {
    return size_;
}

bool MappedFile::is_open() const noexcept {
    return data_ != 0 && size_ > 0;
}

bool MappedFile::is_mapped() const noexcept {
    return mapped_;
}

bool MappedFile::open_readonly_mapped(const std::string& path) noexcept {
    if (path.empty()) {
        return false;
    }

#if defined(_WIN32)
    HANDLE file = CreateFileA(
        path.c_str(),
        GENERIC_READ,
        FILE_SHARE_READ,
        0,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        0);
    if (file == INVALID_HANDLE_VALUE) {
        return false;
    }

    LARGE_INTEGER file_size;
    if (!GetFileSizeEx(file, &file_size) || file_size.QuadPart <= 0
        || static_cast<unsigned long long>(file_size.QuadPart) > static_cast<unsigned long long>(std::numeric_limits<size_t>::max())) {
        CloseHandle(file);
        return false;
    }

    HANDLE mapping = CreateFileMappingA(file, 0, PAGE_READONLY, 0, 0, 0);
    if (!mapping) {
        CloseHandle(file);
        return false;
    }

    void* view = MapViewOfFile(mapping, FILE_MAP_READ, 0, 0, 0);
    if (!view) {
        CloseHandle(mapping);
        CloseHandle(file);
        return false;
    }

    data_ = static_cast<const uint8_t*>(view);
    size_ = static_cast<size_t>(file_size.QuadPart);
    mapped_ = true;
    file_handle_ = file;
    mapping_handle_ = mapping;
    fd_ = -1;
    return true;
#elif defined(__APPLE__) || defined(__linux__)
    const int fd = open(path.c_str(), O_RDONLY);
    if (fd < 0) {
        return false;
    }

    struct stat st;
    if (fstat(fd, &st) != 0 || st.st_size <= 0
        || static_cast<uint64_t>(st.st_size) > static_cast<uint64_t>(std::numeric_limits<size_t>::max())) {
        ::close(fd);
        return false;
    }

    void* mapped = mmap(0, static_cast<size_t>(st.st_size), PROT_READ, MAP_PRIVATE, fd, 0);
    if (mapped == MAP_FAILED) {
        ::close(fd);
        return false;
    }

    data_ = static_cast<const uint8_t*>(mapped);
    size_ = static_cast<size_t>(st.st_size);
    mapped_ = true;
    fd_ = fd;
    file_handle_ = 0;
    mapping_handle_ = 0;
    return true;
#else
    (void)path;
    return false;
#endif
}

bool MappedFile::open_readonly_owned(const std::string& path) noexcept {
    if (path.empty()) {
        return false;
    }
    std::ifstream file(path.c_str(), std::ios::binary | std::ios::ate);
    if (!file) {
        return false;
    }
    const std::ifstream::pos_type end_pos = file.tellg();
    if (end_pos <= 0
        || static_cast<uint64_t>(end_pos) > static_cast<uint64_t>(std::numeric_limits<size_t>::max())
        || static_cast<uint64_t>(end_pos) > static_cast<uint64_t>(std::numeric_limits<std::streamsize>::max())) {
        return false;
    }

    const size_t byte_count = static_cast<size_t>(end_pos);
    try {
        owned_buffer_.resize(byte_count);
    } catch (...) {
        owned_buffer_.clear();
        return false;
    }

    file.seekg(0, std::ios::beg);
    file.read(reinterpret_cast<char*>(&owned_buffer_[0]), static_cast<std::streamsize>(owned_buffer_.size()));
    if (!file) {
        owned_buffer_.clear();
        return false;
    }

    data_ = &owned_buffer_[0];
    size_ = owned_buffer_.size();
    mapped_ = false;
    file_handle_ = 0;
    mapping_handle_ = 0;
    fd_ = -1;
    return true;
}

}  // namespace internal
}  // namespace taiyin
