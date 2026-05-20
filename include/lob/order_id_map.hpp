#pragma once

#include <bit>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>

#include "lob/compiler.hpp"
#include "lob/types.hpp"

namespace lob {

// Open-addressing hash table with linear probing and backshift deletion,
// specialised for OrderId -> T* lookups. Capacity is a power of two; the
// table grows by reallocation only if the user-specified initial capacity
// is exceeded -- in normal use we size for the expected peak open-order
// count and never resize on the hot path.
//
// Empty slots are marked by id == 0 (matching kInvalidOrderId). The matching
// engine guarantees real order IDs are non-zero (zero is reserved by the ITCH
// spec and synthesised by the replay harness to start at 1).
template <class T>
class OrderIdMap {
public:
    explicit OrderIdMap(std::size_t initial_capacity = 1u << 20)
        : mask_(round_up_pow2(initial_capacity) - 1),
          slots_(std::make_unique<Slot[]>(mask_ + 1)) {
        // make_unique<Slot[]> value-initialises, which zeroes both fields.
    }

    LOB_ALWAYS_INLINE bool insert(OrderId id, T* value) noexcept {
        assert(id != kInvalidOrderId);
        std::size_t i = hash(id) & mask_;
        for (;;) {
            Slot& s = slots_[i];
            if (s.id == kInvalidOrderId) {
                s.id    = id;
                s.value = value;
                ++size_;
                return true;
            }
            if (LOB_UNLIKELY(s.id == id)) return false;  // duplicate id
            i = (i + 1) & mask_;
        }
    }

    LOB_ALWAYS_INLINE T* find(OrderId id) const noexcept {
        std::size_t i = hash(id) & mask_;
        for (;;) {
            const Slot& s = slots_[i];
            if (s.id == id) return s.value;
            if (s.id == kInvalidOrderId) return nullptr;
            i = (i + 1) & mask_;
        }
    }

    // Backshift deletion preserves the linear-probing invariant without
    // tombstones, so find() probe lengths don't degrade over time.
    LOB_ALWAYS_INLINE bool erase(OrderId id) noexcept {
        std::size_t i = hash(id) & mask_;
        for (;;) {
            Slot& s = slots_[i];
            if (s.id == id) break;
            if (s.id == kInvalidOrderId) return false;
            i = (i + 1) & mask_;
        }
        // Walk forward shifting entries whose ideal slot is at-or-before `i`
        // back into the hole at `i`.
        std::size_t hole = i;
        for (;;) {
            std::size_t next = (hole + 1) & mask_;
            Slot&       n    = slots_[next];
            if (n.id == kInvalidOrderId) {
                slots_[hole].id = kInvalidOrderId;
                --size_;
                return true;
            }
            std::size_t ideal = hash(n.id) & mask_;
            // distance from ideal slot to `next`; if it would be no worse to
            // sit at `hole` instead, shift it back.
            std::size_t dist_next = (next - ideal) & mask_;
            if (dist_next == 0) {
                // n is in its ideal slot; we must not move it.
                slots_[hole].id = kInvalidOrderId;
                --size_;
                return true;
            }
            slots_[hole] = n;
            hole = next;
        }
    }

    [[nodiscard]] std::size_t size() const noexcept { return size_; }
    [[nodiscard]] std::size_t capacity() const noexcept { return mask_ + 1; }

private:
    struct Slot {
        OrderId id{kInvalidOrderId};
        T*      value{nullptr};
    };

    // splitmix64 -- avalanching, ~2 cycles. We do not mix the high bits back
    // into the low via xorshift on the result because we want speed; with
    // power-of-two mask the bottom bits are what index the table, and
    // splitmix64 already distributes evenly across all 64 bits.
    LOB_ALWAYS_INLINE static std::uint64_t hash(OrderId x) noexcept {
        x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ULL;
        x = (x ^ (x >> 27)) * 0x94d049bb133111ebULL;
        x = (x ^ (x >> 31));
        return x;
    }

    static std::size_t round_up_pow2(std::size_t n) noexcept {
        if (n < 2) return 2;
        return std::size_t{1} << (64 - std::countl_zero(n - 1));
    }

    std::size_t              mask_;
    std::unique_ptr<Slot[]>  slots_;
    std::size_t              size_{0};
};

}  // namespace lob
