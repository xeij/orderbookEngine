#pragma once

#include <cstdint>

#include "lob/intrusive_list.hpp"
#include "lob/order.hpp"

namespace lob {

// One entry of the per-side flat array. The list is the time-ordered FIFO of
// orders at this price; total_qty/count are maintained incrementally so callers
// can answer aggregate questions without walking the list.
struct PriceLevel {
    IntrusiveList<Order> orders;
    Quantity             total_qty{0};
    std::uint32_t        count{0};

    [[nodiscard]] bool empty() const noexcept { return count == 0; }
};

}  // namespace lob
