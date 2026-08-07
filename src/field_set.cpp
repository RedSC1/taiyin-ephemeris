#include "taiyin/field_set.h"

#include <algorithm>
#include <limits>

namespace taiyin {
namespace {

const size_t BITS_PER_BYTE = 8;

bool field_to_byte_bit(size_t field, size_t* byte_index, uint8_t* bit_mask) noexcept {
    if (!byte_index || !bit_mask) {
        return false;
    }
    if (field > (std::numeric_limits<size_t>::max() - (BITS_PER_BYTE - 1)) / BITS_PER_BYTE) {
        return false;
    }
    *byte_index = field / BITS_PER_BYTE;
    *bit_mask = static_cast<uint8_t>(1u << (field % BITS_PER_BYTE));
    return true;
}

void trim_trailing_zero_bytes(std::vector<uint8_t>* bytes) noexcept {
    if (!bytes) {
        return;
    }
    while (!bytes->empty() && bytes->back() == 0u) {
        bytes->pop_back();
    }
}

}  // namespace

FieldSet::FieldSet() noexcept : bytes_() {}

bool FieldSet::empty() const noexcept {
    for (size_t i = 0; i < bytes_.size(); ++i) {
        if (bytes_[i] != 0u) {
            return false;
        }
    }
    return true;
}

size_t FieldSet::byte_size() const noexcept {
    return bytes_.size();
}

bool FieldSet::has(size_t field) const noexcept {
    size_t byte_index = 0;
    uint8_t bit_mask = 0u;
    if (!field_to_byte_bit(field, &byte_index, &bit_mask) || byte_index >= bytes_.size()) {
        return false;
    }
    return (bytes_[byte_index] & bit_mask) != 0u;
}

Status FieldSet::set(size_t field) noexcept {
    size_t byte_index = 0;
    uint8_t bit_mask = 0u;
    if (!field_to_byte_bit(field, &byte_index, &bit_mask)) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    try {
        if (byte_index >= bytes_.size()) {
            bytes_.resize(byte_index + 1, 0u);
        }
    } catch (...) {
        return TAIYIN_ERROR_OUT_OF_MEMORY;
    }
    bytes_[byte_index] = static_cast<uint8_t>(bytes_[byte_index] | bit_mask);
    return TAIYIN_STATUS_OK;
}

void FieldSet::clear(size_t field) noexcept {
    size_t byte_index = 0;
    uint8_t bit_mask = 0u;
    if (!field_to_byte_bit(field, &byte_index, &bit_mask) || byte_index >= bytes_.size()) {
        return;
    }
    bytes_[byte_index] = static_cast<uint8_t>(bytes_[byte_index] & ~bit_mask);
    trim_trailing_zero_bytes(&bytes_);
}

void FieldSet::clear_all() noexcept {
    bytes_.clear();
}

bool FieldSet::contains(const FieldSet& required) const noexcept {
    for (size_t i = 0; i < required.bytes_.size(); ++i) {
        const uint8_t want = required.bytes_[i];
        const uint8_t got = i < bytes_.size() ? bytes_[i] : 0u;
        if ((got & want) != want) {
            return false;
        }
    }
    return true;
}

FieldSet FieldSet::missing(const FieldSet& required) const {
    FieldSet out;
    out.bytes_.resize(required.bytes_.size(), 0u);
    for (size_t i = 0; i < required.bytes_.size(); ++i) {
        const uint8_t want = required.bytes_[i];
        const uint8_t got = i < bytes_.size() ? bytes_[i] : 0u;
        out.bytes_[i] = static_cast<uint8_t>(want & ~got);
    }
    trim_trailing_zero_bytes(&out.bytes_);
    return out;
}

bool FieldSet::first_set(size_t* field) const noexcept {
    if (!field) {
        return false;
    }
    for (size_t byte_index = 0; byte_index < bytes_.size(); ++byte_index) {
        const uint8_t value = bytes_[byte_index];
        if (value == 0u) {
            continue;
        }
        for (size_t bit = 0; bit < BITS_PER_BYTE; ++bit) {
            if ((value & static_cast<uint8_t>(1u << bit)) != 0u) {
                *field = byte_index * BITS_PER_BYTE + bit;
                return true;
            }
        }
    }
    return false;
}

FieldSet& FieldSet::operator|=(const FieldSet& other) {
    try {
        if (other.bytes_.size() > bytes_.size()) {
            bytes_.resize(other.bytes_.size(), 0u);
        }
    } catch (...) {
        throw;
    }
    for (size_t i = 0; i < other.bytes_.size(); ++i) {
        bytes_[i] = static_cast<uint8_t>(bytes_[i] | other.bytes_[i]);
    }
    return *this;
}

FieldSet& FieldSet::operator&=(const FieldSet& other) noexcept {
    const size_t common_size = std::min(bytes_.size(), other.bytes_.size());
    for (size_t i = 0; i < common_size; ++i) {
        bytes_[i] = static_cast<uint8_t>(bytes_[i] & other.bytes_[i]);
    }
    for (size_t i = common_size; i < bytes_.size(); ++i) {
        bytes_[i] = 0u;
    }
    trim_trailing_zero_bytes(&bytes_);
    return *this;
}

FieldSet operator|(FieldSet lhs, const FieldSet& rhs) {
    lhs |= rhs;
    return lhs;
}

FieldSet operator&(FieldSet lhs, const FieldSet& rhs) noexcept {
    lhs &= rhs;
    return lhs;
}

bool operator==(const FieldSet& lhs, const FieldSet& rhs) noexcept {
    return lhs.contains(rhs) && rhs.contains(lhs);
}

bool operator!=(const FieldSet& lhs, const FieldSet& rhs) noexcept {
    return !(lhs == rhs);
}

}  // namespace taiyin
