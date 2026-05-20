#pragma once

#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>

#include "lob/compiler.hpp"

namespace lob {

// Dense bitmap over a fixed-size index space. Backed by a contiguous array of
// 64-bit words so we can use ctzll/clzll to find the next set bit in a word in
// one instruction, and scan multiple words by looping.
//
// The book uses one of these per side. The find_next_* operations are the
// difference between O(1) and O(price-range) when, e.g., the order that was
// the best bid gets cancelled and we need to recover the new best.
class Bitmap {
public:
    explicit Bitmap(std::size_t num_bits)
        : num_words_((num_bits + 63) / 64),
          words_(std::make_unique<std::uint64_t[]>(num_words_)) {
        std::memset(words_.get(), 0, num_words_ * sizeof(std::uint64_t));
    }

    LOB_ALWAYS_INLINE void set(std::size_t idx) noexcept {
        words_[idx >> 6] |= (std::uint64_t{1} << (idx & 63));
    }

    LOB_ALWAYS_INLINE void clear(std::size_t idx) noexcept {
        words_[idx >> 6] &= ~(std::uint64_t{1} << (idx & 63));
    }

    LOB_ALWAYS_INLINE bool test(std::size_t idx) const noexcept {
        return (words_[idx >> 6] >> (idx & 63)) & 1u;
    }

    // Returns the smallest index >= from that is set, or kNotFound if none.
    LOB_ALWAYS_INLINE std::size_t find_next_set(std::size_t from) const noexcept {
        std::size_t w = from >> 6;
        if (w >= num_words_) return kNotFound;
        std::uint64_t bits = words_[w] & (~std::uint64_t{0} << (from & 63));
        while (bits == 0) {
            if (++w >= num_words_) return kNotFound;
            bits = words_[w];
        }
        return (w << 6) + static_cast<std::size_t>(std::countr_zero(bits));
    }

    // Returns the largest index <= from that is set, or kNotFound if none.
    LOB_ALWAYS_INLINE std::size_t find_prev_set(std::size_t from) const noexcept {
        std::size_t w = from >> 6;
        if (w >= num_words_) {
            if (num_words_ == 0) return kNotFound;
            w = num_words_ - 1;
            std::uint64_t bits = words_[w];
            while (bits == 0) {
                if (w == 0) return kNotFound;
                bits = words_[--w];
            }
            return (w << 6) + 63u - static_cast<std::size_t>(std::countl_zero(bits));
        }
        // Mask to bits at position <= (from & 63).
        std::uint64_t mask = (from & 63) == 63
                                 ? ~std::uint64_t{0}
                                 : ((std::uint64_t{1} << ((from & 63) + 1)) - 1);
        std::uint64_t bits = words_[w] & mask;
        while (bits == 0) {
            if (w == 0) return kNotFound;
            bits = words_[--w];
        }
        return (w << 6) + 63u - static_cast<std::size_t>(std::countl_zero(bits));
    }

    static constexpr std::size_t kNotFound = static_cast<std::size_t>(-1);

private:
    std::size_t                       num_words_;
    std::unique_ptr<std::uint64_t[]>  words_;
};

}  // namespace lob
