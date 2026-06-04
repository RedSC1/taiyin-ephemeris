#ifndef TAIYIN_RUNTIME_HANDLE_TYPES_H
#define TAIYIN_RUNTIME_HANDLE_TYPES_H

#include <cstdint>
#include <cstddef>

namespace taiyin {
namespace runtime {

struct MethodId {
    uint32_t value;

    MethodId() noexcept : value(0) {}
    explicit MethodId(uint32_t id) noexcept : value(id) {}

    bool is_valid() const noexcept { return value != 0; }
    bool operator==(const MethodId& other) const noexcept { return value == other.value; }
    bool operator!=(const MethodId& other) const noexcept { return value != other.value; }
};

struct ServiceId {
    uint32_t value;

    ServiceId() noexcept : value(0) {}
    explicit ServiceId(uint32_t id) noexcept : value(id) {}

    bool is_valid() const noexcept { return value != 0; }
    bool operator==(const ServiceId& other) const noexcept { return value == other.value; }
    bool operator!=(const ServiceId& other) const noexcept { return value != other.value; }
};

}  // namespace runtime
}  // namespace taiyin

#endif  // TAIYIN_RUNTIME_HANDLE_TYPES_H
