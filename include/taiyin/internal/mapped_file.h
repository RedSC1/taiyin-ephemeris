#ifndef TAIYIN_INTERNAL_MAPPED_FILE_H
#define TAIYIN_INTERNAL_MAPPED_FILE_H

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace taiyin {
namespace internal {

class MappedFile {
public:
    MappedFile() noexcept;
    ~MappedFile() noexcept;

    MappedFile(const MappedFile&) = delete;
    MappedFile& operator=(const MappedFile&) = delete;

    bool open_readonly(const std::string& path) noexcept;
    void close() noexcept;

    const uint8_t* data() const noexcept;
    size_t size() const noexcept;
    bool is_open() const noexcept;
    bool is_mapped() const noexcept;

private:
    bool open_readonly_mapped(const std::string& path) noexcept;
    bool open_readonly_owned(const std::string& path) noexcept;

    const uint8_t* data_;
    size_t size_;
    bool mapped_;
    std::vector<uint8_t> owned_buffer_;

    void* file_handle_;
    void* mapping_handle_;
    int fd_;
};

}  // namespace internal
}  // namespace taiyin

#endif  // TAIYIN_INTERNAL_MAPPED_FILE_H
