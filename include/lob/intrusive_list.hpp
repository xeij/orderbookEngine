#pragma once

#include <cassert>

#include "lob/compiler.hpp"

namespace lob {

// Intrusive doubly-linked list. The links live inside T, so adding or removing
// a node never touches the allocator. T must expose two member pointers named
// `prev` and `next` (any pointer-to-T). The list does not own the nodes.
//
// Used as the FIFO at each price level: head is the oldest resting order and
// is the next to fill when an aggressive opposite-side order crosses.
template <class T>
class IntrusiveList {
public:
    IntrusiveList() noexcept = default;

    [[nodiscard]] LOB_ALWAYS_INLINE T* head() const noexcept { return head_; }
    [[nodiscard]] LOB_ALWAYS_INLINE T* tail() const noexcept { return tail_; }
    [[nodiscard]] LOB_ALWAYS_INLINE bool empty() const noexcept { return head_ == nullptr; }

    LOB_ALWAYS_INLINE void push_back(T* node) noexcept {
        node->prev = tail_;
        node->next = nullptr;
        if (tail_) {
            tail_->next = node;
        } else {
            head_ = node;
        }
        tail_ = node;
    }

    // Removes `node` from this list. The caller must guarantee that the node
    // is in fact a member of this list -- we do not search.
    LOB_ALWAYS_INLINE void unlink(T* node) noexcept {
        T* p = node->prev;
        T* n = node->next;
        if (p) p->next = n; else head_ = n;
        if (n) n->prev = p; else tail_ = p;
        // Leaving the node's own prev/next dangling is fine because the
        // caller is about to either re-insert it or return it to the slab.
    }

    LOB_ALWAYS_INLINE void clear() noexcept {
        head_ = nullptr;
        tail_ = nullptr;
    }

private:
    T* head_{nullptr};
    T* tail_{nullptr};
};

}  // namespace lob
