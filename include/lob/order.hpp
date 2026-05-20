#pragma once

#include <cstdint>

#include "lob/types.hpp"

namespace lob {

// A resting order. Lives in the slab, embedded into the intrusive FIFO at its
// price level. We cache the level index so cancel can be O(1) without scanning
// or dividing.
struct Order {
    Order*       prev;       // intrusive list links
    Order*       next;
    OrderId      order_id;
    Quantity     quantity;   // remaining (decremented on partial fill)
    Price        price;
    std::uint32_t level_idx; // cached index into the side's level array
    Side         side;
    std::uint8_t pad_[3];
};

// Sanity check on layout: 2 pointers + 64-bit id + a few 32-bit + 1 byte side
// + 3 byte pad = 40 bytes on a 64-bit ABI. We accept up to 48 to tolerate
// implementations that round-up to 8-byte alignment of the trailing tail.
static_assert(sizeof(Order) <= 48, "Order is larger than expected; check fields");

}  // namespace lob
