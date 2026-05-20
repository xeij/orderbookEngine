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

static_assert(sizeof(Order) == 40, "Order layout unexpected -- adjust pad_");

}  // namespace lob
