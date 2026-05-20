#pragma once

#include <cstddef>
#include <cstdint>

#include "lob/compiler.hpp"

namespace lob {

// ITCH 5.0 prices are 32-bit unsigned with four implied decimal places
// (i.e. $1.2345 is 12345). We use a signed integer internally so unset levels
// or sentinel deltas can be represented cleanly.
using Price     = std::int32_t;
using Quantity  = std::uint32_t;
using OrderId   = std::uint64_t;
using Timestamp = std::uint64_t;  // nanoseconds since session midnight

inline constexpr Price kInvalidPrice = -1;
inline constexpr OrderId kInvalidOrderId = 0;

enum class Side : std::uint8_t { Buy = 0, Sell = 1 };

LOB_ALWAYS_INLINE constexpr Side opposite(Side s) noexcept {
    return static_cast<Side>(static_cast<std::uint8_t>(s) ^ 1u);
}

enum class OrderType : std::uint8_t {
    Limit  = 0,
    Market = 1,
};

// Time-in-force. The semantics implemented match the IEX/NASDAQ wire conventions:
//   GTC       - rests on the book until filled or cancelled
//   DAY       - same as GTC for our session-scoped engine
//   IOC       - match what we can, cancel the remainder
//   FOK       - match the full quantity or none of it; never rests
//   POST_ONLY - reject if it would cross; otherwise rests as a limit
enum class TimeInForce : std::uint8_t {
    GTC      = 0,
    Day      = 1,
    IOC      = 2,
    FOK      = 3,
    PostOnly = 4,
};

struct Trade {
    OrderId   resting_id;
    OrderId   aggressor_id;
    Price     price;
    Quantity  quantity;
    Timestamp ts;
    Side      aggressor_side;
};

// Status of a single submit_order() call.
enum class OrderStatus : std::uint8_t {
    Accepted       = 0,  // resting on the book (in whole or in part)
    FullyFilled    = 1,
    PartiallyFilled = 2,  // returned for IOC with partial fill
    Cancelled      = 3,  // IOC unfilled, FOK rejected, POST_ONLY rejected
    Rejected       = 4,  // out-of-window price, duplicate id, etc.
};

struct SubmitResult {
    OrderStatus status;
    Quantity    filled_qty;
    Quantity    remaining_qty;
};

}  // namespace lob
