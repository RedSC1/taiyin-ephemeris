#include "taiyin/ziwei/dynamic_bitset.h"

#include <algorithm>
#include <stdexcept>

namespace taiyin {
namespace ziwei {

DynamicBitset::DynamicBitset() noexcept : bit_count_(0u), blocks_() {}

DynamicBitset::DynamicBitset(std::size_t bit_count, bool value)
    : bit_count_(bit_count),
      blocks_(block_count_for(bit_count), value ? ~block_type(0) : block_type(0)) {
    mask_unused_tail_bits();
}

std::size_t DynamicBitset::size() const noexcept {
    return bit_count_;
}

bool DynamicBitset::empty() const noexcept {
    return bit_count_ == 0u;
}

void DynamicBitset::resize(std::size_t bit_count, bool value) {
    const std::size_t old_bit_count = bit_count_;
    const std::size_t old_block_count = blocks_.size();
    const std::size_t new_block_count = block_count_for(bit_count);

    blocks_.resize(new_block_count, value ? ~block_type(0) : block_type(0));
    bit_count_ = bit_count;

    if (value && bit_count > old_bit_count && old_block_count != 0u) {
        const std::size_t first_new_bit = old_bit_count % kBitsPerBlock;
        if (first_new_bit != 0u) {
            blocks_[old_block_count - 1u] |= ~low_bits_mask(first_new_bit);
        }
    }

    mask_unused_tail_bits();
}

void DynamicBitset::clear() noexcept {
    bit_count_ = 0u;
    blocks_.clear();
}

DynamicBitset& DynamicBitset::set(std::size_t position) {
    require_position(position);
    blocks_[position / kBitsPerBlock] |= block_type(1) << (position % kBitsPerBlock);
    return *this;
}

DynamicBitset& DynamicBitset::reset(std::size_t position) {
    require_position(position);
    blocks_[position / kBitsPerBlock] &= ~(block_type(1) << (position % kBitsPerBlock));
    return *this;
}

DynamicBitset& DynamicBitset::reset() noexcept {
    std::fill(blocks_.begin(), blocks_.end(), block_type(0));
    return *this;
}

bool DynamicBitset::test(std::size_t position) const {
    require_position(position);
    return (blocks_[position / kBitsPerBlock]
        & (block_type(1) << (position % kBitsPerBlock))) != 0u;
}

bool DynamicBitset::any() const noexcept {
    for (std::size_t i = 0u; i < blocks_.size(); ++i) {
        if (blocks_[i] != 0u) return true;
    }
    return false;
}

bool DynamicBitset::none() const noexcept {
    return !any();
}

std::size_t DynamicBitset::count() const noexcept {
    std::size_t total = 0u;
    for (std::size_t i = 0u; i < blocks_.size(); ++i) {
        block_type value = blocks_[i];
        while (value != 0u) {
            value &= value - 1u;
            ++total;
        }
    }
    return total;
}

DynamicBitset& DynamicBitset::operator&=(const DynamicBitset& other) {
    require_same_size(other);
    for (std::size_t i = 0u; i < blocks_.size(); ++i) {
        blocks_[i] &= other.blocks_[i];
    }
    return *this;
}

DynamicBitset& DynamicBitset::operator|=(const DynamicBitset& other) {
    require_same_size(other);
    for (std::size_t i = 0u; i < blocks_.size(); ++i) {
        blocks_[i] |= other.blocks_[i];
    }
    mask_unused_tail_bits();
    return *this;
}

DynamicBitset& DynamicBitset::operator^=(const DynamicBitset& other) {
    require_same_size(other);
    for (std::size_t i = 0u; i < blocks_.size(); ++i) {
        blocks_[i] ^= other.blocks_[i];
    }
    mask_unused_tail_bits();
    return *this;
}

bool operator==(const DynamicBitset& lhs, const DynamicBitset& rhs) noexcept {
    return lhs.bit_count_ == rhs.bit_count_ && lhs.blocks_ == rhs.blocks_;
}

bool operator!=(const DynamicBitset& lhs, const DynamicBitset& rhs) noexcept {
    return !(lhs == rhs);
}

std::size_t DynamicBitset::block_count_for(std::size_t bit_count) noexcept {
    return bit_count == 0u ? 0u : 1u + (bit_count - 1u) / kBitsPerBlock;
}

DynamicBitset::block_type DynamicBitset::low_bits_mask(
    std::size_t bit_count
) noexcept {
    if (bit_count == 0u) return block_type(0);
    if (bit_count >= kBitsPerBlock) return ~block_type(0);
    return (block_type(1) << bit_count) - 1u;
}

void DynamicBitset::require_position(std::size_t position) const {
    if (position >= bit_count_) {
        throw std::out_of_range("DynamicBitset position is outside its bit count");
    }
}

void DynamicBitset::require_same_size(const DynamicBitset& other) const {
    if (bit_count_ != other.bit_count_) {
        throw std::invalid_argument("DynamicBitset operands have different sizes");
    }
}

void DynamicBitset::mask_unused_tail_bits() noexcept {
    if (blocks_.empty()) return;
    const std::size_t used_tail_bits = bit_count_ % kBitsPerBlock;
    if (used_tail_bits != 0u) {
        blocks_.back() &= low_bits_mask(used_tail_bits);
    }
}

DynamicBitset operator&(DynamicBitset lhs, const DynamicBitset& rhs) {
    lhs &= rhs;
    return lhs;
}

DynamicBitset operator|(DynamicBitset lhs, const DynamicBitset& rhs) {
    lhs |= rhs;
    return lhs;
}

DynamicBitset operator^(DynamicBitset lhs, const DynamicBitset& rhs) {
    lhs ^= rhs;
    return lhs;
}

}  // namespace ziwei
}  // namespace taiyin
