#ifndef TAIYIN_ZIWEI_DYNAMIC_BITSET_H
#define TAIYIN_ZIWEI_DYNAMIC_BITSET_H

#include <cstddef>
#include <cstdint>
#include <vector>

namespace taiyin {
namespace ziwei {

// A deliberately small C++11 dynamic bitset for StarId collections. It is an
// original Taiyin implementation rather than a vendored third-party bitset.
// The class keeps its unused tail bits clear so equality and bitwise results
// have one deterministic representation.
class DynamicBitset {
public:
    typedef uint64_t block_type;

    DynamicBitset() noexcept;
    explicit DynamicBitset(std::size_t bit_count, bool value = false);

    std::size_t size() const noexcept;
    bool empty() const noexcept;

    void resize(std::size_t bit_count, bool value = false);
    void clear() noexcept;

    DynamicBitset& set(std::size_t position);
    DynamicBitset& reset(std::size_t position);
    DynamicBitset& reset() noexcept;
    bool test(std::size_t position) const;

    bool any() const noexcept;
    bool none() const noexcept;
    std::size_t count() const noexcept;

    DynamicBitset& operator&=(const DynamicBitset& other);
    DynamicBitset& operator|=(const DynamicBitset& other);
    DynamicBitset& operator^=(const DynamicBitset& other);

    friend bool operator==(
        const DynamicBitset& lhs,
        const DynamicBitset& rhs
    ) noexcept;
    friend bool operator!=(
        const DynamicBitset& lhs,
        const DynamicBitset& rhs
    ) noexcept;

private:
    static const std::size_t kBitsPerBlock = 64u;

    std::size_t bit_count_;
    std::vector<block_type> blocks_;

    static std::size_t block_count_for(std::size_t bit_count) noexcept;
    static block_type low_bits_mask(std::size_t bit_count) noexcept;
    void require_position(std::size_t position) const;
    void require_same_size(const DynamicBitset& other) const;
    void mask_unused_tail_bits() noexcept;
};

DynamicBitset operator&(DynamicBitset lhs, const DynamicBitset& rhs);
DynamicBitset operator|(DynamicBitset lhs, const DynamicBitset& rhs);
DynamicBitset operator^(DynamicBitset lhs, const DynamicBitset& rhs);

}  // namespace ziwei
}  // namespace taiyin

#endif  // TAIYIN_ZIWEI_DYNAMIC_BITSET_H
