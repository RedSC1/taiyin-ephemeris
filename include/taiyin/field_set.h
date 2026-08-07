#ifndef TAIYIN_FIELD_SET_H
#define TAIYIN_FIELD_SET_H

#include "taiyin/status.h"

#include <stddef.h>
#include <stdint.h>
#include <vector>

namespace taiyin {

class FieldSet {
public:
    FieldSet() noexcept;

    bool empty() const noexcept;
    size_t byte_size() const noexcept;

    bool has(size_t field) const noexcept;
    Status set(size_t field) noexcept;
    void clear(size_t field) noexcept;
    void clear_all() noexcept;

    bool contains(const FieldSet& required) const noexcept;
    FieldSet missing(const FieldSet& required) const;
    bool first_set(size_t* field) const noexcept;

    FieldSet& operator|=(const FieldSet& other);
    FieldSet& operator&=(const FieldSet& other) noexcept;

private:
    std::vector<uint8_t> bytes_;
};

FieldSet operator|(FieldSet lhs, const FieldSet& rhs);
FieldSet operator&(FieldSet lhs, const FieldSet& rhs) noexcept;
bool operator==(const FieldSet& lhs, const FieldSet& rhs) noexcept;
bool operator!=(const FieldSet& lhs, const FieldSet& rhs) noexcept;

}  // namespace taiyin

#endif  // TAIYIN_FIELD_SET_H
