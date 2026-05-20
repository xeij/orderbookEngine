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

// Status of a single submit() call. The (filled_qty, remaining_qty) pair gives
// the precise outcome; this enum is the convenient summary.
//   Accepted        - rested on the book with no part filled
//   FullyFilled     - matched in full, nothing rested
//   PartiallyFilled - some part filled. For GTC the remainder rested; for IOC
//                     the remainder was cancelled. Inspect remaining_qty +
//                     the original TIF to disambiguate.
//   Cancelled       - no fill at all, no rest. FOK reject, IOC vs empty book,
//                     POST_ONLY would-cross.
//   Rejected        - rejected before any side-effects (bad input).
enum class OrderStatus : std::uint8_t {
    Accepted        = 0,
    FullyFilled     = 1,
    PartiallyFilled = 2,
    Cancelled       = 3,
    Rejected        = 4,
};

struct SubmitResult {
    OrderStatus status;
    Quantity    filled_qty;
    Quantity    remaining_qty;
};

}  // namespace lob
