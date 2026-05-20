#pragma once

#include <atomic>
#include <bit>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <new>
#include <type_traits>

#include "lob/compiler.hpp"

namespace lob {

// Single-producer / single-consumer lock-free ring buffer.
//
// Layout follows the Disruptor pattern: the producer cursor and the consumer
// cursor each sit on their own cache line, so the false-sharing penalty that
// would otherwise pingpong the line between cores is eliminated. Each side
// also caches the other's last-observed cursor to avoid hitting the
// other-line atomic on every push/pop -- a refresh is only needed when the
// cached value indicates the ring is full (push) or empty (pop).
//
// Capacity is fixed at construction and must be a power of two so the index
// wrap is a cheap mask. T must be trivially copyable so an element transfer
// is just a memcpy / scalar copy.
template <class T>
class SpscRing {
    static_assert(std::is_trivially_copyable_v<T>, "T must be trivially copyable");

public:
    explicit SpscRing(std::size_t capacity)
        : mask_(capacity - 1),
          buf_(std::make_unique<T[]>(capacity)) {
        assert(capacity >= 2 && std::has_single_bit(capacity));
    }

    // Producer side. Returns false if the ring is full.
    LOB_ALWAYS_INLINE bool try_push(const T& v) noexcept {
        const std::size_t head = head_.load(std::memory_order_relaxed);
        const std::size_t next = head + 1;
        // Fast path: cached tail says there is room.
        if (next - cached_tail_ > mask_ + 1) {
            cached_tail_ = tail_.load(std::memory_order_acquire);
            if (next - cached_tail_ > mask_ + 1) return false;
        }
        buf_[head & mask_] = v;
        head_.store(next, std::memory_order_release);
        return true;
    }

    // Consumer side. Returns false if the ring is empty.
    LOB_ALWAYS_INLINE bool try_pop(T& out) noexcept {
        const std::size_t tail = tail_.load(std::memory_order_relaxed);
        if (tail == cached_head_) {
            cached_head_ = head_.load(std::memory_order_acquire);
            if (tail == cached_head_) return false;
        }
        out = buf_[tail & mask_];
        tail_.store(tail + 1, std::memory_order_release);
        return true;
    }

    [[nodiscard]] std::size_t capacity() const noexcept { return mask_ + 1; }
    [[nodiscard]] std::size_t size_approx() const noexcept {
        return head_.load(std::memory_order_relaxed) -
               tail_.load(std::memory_order_relaxed);
    }

private:
    std::size_t          mask_;
    std::unique_ptr<T[]> buf_;

    alignas(kCacheLine) std::atomic<std::size_t> head_{0};
    std::size_t cached_tail_{0};
    char        pad0_[kCacheLine - sizeof(std::atomic<std::size_t>) - sizeof(std::size_t)]{};

    alignas(kCacheLine) std::atomic<std::size_t> tail_{0};
    std::size_t cached_head_{0};
    char        pad1_[kCacheLine - sizeof(std::atomic<std::size_t>) - sizeof(std::size_t)]{};
};

}  // namespace lob
