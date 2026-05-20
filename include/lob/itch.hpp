#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "lob/types.hpp"

// Subset of the NASDAQ TotalView-ITCH 5.0 specification needed to
// reconstruct the limit order book. Every wire field is big-endian; the
// parser converts to host order before invoking the handler.
//
// Spec reference: TotalView-ITCH 5.0, Sections 4.3-4.5 (Add Order, Order
// Modification, Trade Messages).

namespace lob::itch {

// ITCH BinaryFILE format prefixes every message with a 2-byte big-endian
// length. The length covers the message body only (including the type byte
// at offset 0).
inline constexpr std::size_t kFrameLengthBytes = 2;

enum class MessageType : char {
    SystemEvent                 = 'S',
    StockDirectory              = 'R',
    StockTradingAction          = 'H',
    RegSho                      = 'Y',
    MarketParticipantPosition   = 'L',
    MwcbDeclineLevel            = 'V',
    MwcbStatus                  = 'W',
    IpoQuotingPeriod            = 'K',
    LuldAuctionCollar           = 'J',
    OperationalHalt             = 'h',
    AddOrder                    = 'A',
    AddOrderMpid                = 'F',
    OrderExecuted               = 'E',
    OrderExecutedWithPrice      = 'C',
    OrderCancel                 = 'X',
    OrderDelete                 = 'D',
    OrderReplace                = 'U',
    Trade                       = 'P',
    CrossTrade                  = 'Q',
    BrokenTrade                 = 'B',
    NoiiImbalance               = 'I',
    RetailPriceImprovement      = 'N',
};

struct AddOrder {
    std::uint16_t       stock_locate;
    std::uint16_t       tracking_number;
    std::uint64_t       ts_ns;
    std::uint64_t       order_ref;
    Side                side;
    std::uint32_t       shares;
    std::array<char, 8> stock;
    std::uint32_t       price;  // in 1/10000 dollars
};

struct OrderExecuted {
    std::uint16_t stock_locate;
    std::uint64_t ts_ns;
    std::uint64_t order_ref;
    std::uint32_t executed_shares;
    std::uint64_t match_number;
};

struct OrderExecutedWithPrice {
    std::uint16_t stock_locate;
    std::uint64_t ts_ns;
    std::uint64_t order_ref;
    std::uint32_t executed_shares;
    std::uint64_t match_number;
    bool          printable;       // 'Y' => true, 'N' => false
    std::uint32_t execution_price; // in 1/10000 dollars
};

struct OrderCancel {
    std::uint16_t stock_locate;
    std::uint64_t ts_ns;
    std::uint64_t order_ref;
    std::uint32_t cancelled_shares;
};

struct OrderDelete {
    std::uint16_t stock_locate;
    std::uint64_t ts_ns;
    std::uint64_t order_ref;
};

struct OrderReplace {
    std::uint16_t stock_locate;
    std::uint64_t ts_ns;
    std::uint64_t original_order_ref;
    std::uint64_t new_order_ref;
    std::uint32_t shares;
    std::uint32_t price;  // in 1/10000 dollars
};

struct SystemEvent {
    std::uint64_t ts_ns;
    char          event_code;  // 'O' start of messages, 'S' start of system hours,
                               // 'Q' start of market hours, 'M' end of market hours,
                               // 'E' end of system hours, 'C' end of messages
};

struct StockDirectory {
    std::uint16_t       stock_locate;
    std::uint64_t       ts_ns;
    std::array<char, 8> stock;
    char                market_category;
    // Other fields omitted -- we only need locate->symbol mapping for replay.
};

// Polymorphic handler interface. All methods default to no-op so a derived
// class only overrides what it cares about. The book-reconstruction handler
// only needs the order-life-cycle messages plus the locate->symbol map.
class ItchHandler {
public:
    virtual ~ItchHandler() = default;

    virtual void on_system_event(const SystemEvent&)                {}
    virtual void on_stock_directory(const StockDirectory&)          {}
    virtual void on_add_order(const AddOrder&)                      {}
    virtual void on_order_executed(const OrderExecuted&)            {}
    virtual void on_order_executed_with_price(const OrderExecutedWithPrice&) {}
    virtual void on_order_cancel(const OrderCancel&)                {}
    virtual void on_order_delete(const OrderDelete&)                {}
    virtual void on_order_replace(const OrderReplace&)              {}

    // Called for every message we don't unpack explicitly, so handlers can
    // skip-count them for diagnostics.
    virtual void on_unhandled(char type, std::size_t length)        {
        (void)type; (void)length;
    }
};

// Parse a contiguous ITCH stream. Returns the number of messages dispatched.
// Stops cleanly at the first truncated frame.
std::size_t parse(const std::uint8_t* data, std::size_t len, ItchHandler& h);

}  // namespace lob::itch
