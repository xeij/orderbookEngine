#pragma once

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <new>
#include <type_traits>

#include "lob/compiler.hpp"

namespace lob {

// Fixed-capacity slab allocator. The storage is allocated once at construction
// and never grown, so allocate()/deallocate() touch only the embedded free list
// and never call into the system allocator on the hot path.
//
// We do not run T's destructor on deallocate -- the caller is expected to
// invoke it before returning the slot (the matching engine does this when an
// order leaves the book). T must be trivially destructible OR the caller must
// handle destruction explicitly; in this engine Order is trivially destructible.
template <class T>
class SlabAllocator {
    static_assert(sizeof(T) >= sizeof(void*),
                  "T must be at least pointer-sized so a free node fits in-place");

public:
    explicit SlabAllocator(std::size_t capacity)
        : capacity_(capacity),
          storage_(static_cast<Slot*>(::operator new[](capacity * sizeof(Slot),
                                                      std::align_val_t{alignof(T)}))),
          free_head_(nullptr),
          in_use_(0) {
        // Thread every slot onto the free list, last-to-first so allocation
        // returns slots in ascending address order -- friendlier to the
        // prefetcher when orders are added in bursts.
        for (std::size_t i = capacity_; i-- > 0;) {
            Slot* s = storage_ + i;
            s->next = free_head_;
            free_head_ = s;
        }
    }

    SlabAllocator(const SlabAllocator&)            = delete;
    SlabAllocator& operator=(const SlabAllocator&) = delete;
    SlabAllocator(SlabAllocator&&)                 = delete;
    SlabAllocator& operator=(SlabAllocator&&)      = delete;

    ~SlabAllocator() {
        ::operator delete[](storage_, std::align_val_t{alignof(T)});
    }

    // Returns nullptr if the pool is exhausted. The matching engine treats
    // exhaustion as a fatal configuration error -- size the pool for the
    // expected peak open-order count.
    LOB_ALWAYS_INLINE T* allocate() noexcept {
        Slot* s = free_head_;
        if (LOB_UNLIKELY(s == nullptr)) return nullptr;
        free_head_ = s->next;
        ++in_use_;
        return reinterpret_cast<T*>(s);
    }

    LOB_ALWAYS_INLINE void deallocate(T* p) noexcept {
        assert(p != nullptr);
        Slot* s = reinterpret_cast<Slot*>(p);
        s->next = free_head_;
        free_head_ = s;
        --in_use_;
    }

    [[nodiscard]] std::size_t capacity() const noexcept { return capacity_; }
    [[nodiscard]] std::size_t in_use() const noexcept   { return in_use_; }

private:
    // A slot is either a live T or a free-list node. The union here is purely
    // a sizing trick: while a slot is free we treat the leading pointer-sized
    // bytes as a "next" pointer; while live, the bytes are a T.
    union Slot {
        T     value;
        Slot* next;
        Slot() {}
        ~Slot() {}
    };

    std::size_t capacity_;
    Slot*       storage_;
    Slot*       free_head_;
    std::size_t in_use_;
};

}  // namespace lob
