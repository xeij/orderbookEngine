#include "lob/itch.hpp"

#include <cstring>

#include "lob/compiler.hpp"

namespace lob::itch {

namespace {

LOB_ALWAYS_INLINE std::uint16_t be16(const std::uint8_t* p) noexcept {
    return static_cast<std::uint16_t>((std::uint16_t{p[0]} << 8) | p[1]);
}

LOB_ALWAYS_INLINE std::uint32_t be32(const std::uint8_t* p) noexcept {
    return (std::uint32_t{p[0]} << 24) | (std::uint32_t{p[1]} << 16) |
           (std::uint32_t{p[2]} << 8)  | std::uint32_t{p[3]};
}

// ITCH timestamps are 48-bit nanoseconds since session midnight.
LOB_ALWAYS_INLINE std::uint64_t be48(const std::uint8_t* p) noexcept {
    return (std::uint64_t{p[0]} << 40) | (std::uint64_t{p[1]} << 32) |
           (std::uint64_t{p[2]} << 24) | (std::uint64_t{p[3]} << 16) |
           (std::uint64_t{p[4]} << 8)  |  std::uint64_t{p[5]};
}

LOB_ALWAYS_INLINE std::uint64_t be64(const std::uint8_t* p) noexcept {
    std::uint64_t hi = be32(p);
    std::uint64_t lo = be32(p + 4);
    return (hi << 32) | lo;
}

LOB_ALWAYS_INLINE std::array<char, 8> read_symbol(const std::uint8_t* p) noexcept {
    std::array<char, 8> s{};
    std::memcpy(s.data(), p, 8);
    return s;
}

}  // namespace

std::size_t parse(const std::uint8_t* data, std::size_t len, ItchHandler& h) {
    std::size_t pos = 0;
    std::size_t count = 0;

    while (pos + kFrameLengthBytes <= len) {
        const std::uint16_t msg_len = be16(data + pos);
        pos += kFrameLengthBytes;
        if (LOB_UNLIKELY(pos + msg_len > len)) break;  // truncated tail

        const std::uint8_t* m = data + pos;
        const char type = static_cast<char>(m[0]);

        // Field offsets follow Section 4 of the TotalView-ITCH 5.0 spec.
        // The wire layout is type(1) stock_locate(2) tracking(2) ts(6) ...
        switch (type) {
            case static_cast<char>(MessageType::SystemEvent): {
                SystemEvent ev;
                ev.ts_ns      = be48(m + 5);
                ev.event_code = static_cast<char>(m[11]);
                h.on_system_event(ev);
                break;
            }
            case static_cast<char>(MessageType::StockDirectory): {
                StockDirectory sd;
                sd.stock_locate    = be16(m + 1);
                sd.ts_ns           = be48(m + 5);
                sd.stock           = read_symbol(m + 11);
                sd.market_category = static_cast<char>(m[19]);
                h.on_stock_directory(sd);
                break;
            }
            case static_cast<char>(MessageType::AddOrder):
            case static_cast<char>(MessageType::AddOrderMpid): {
                AddOrder a;
                a.stock_locate    = be16(m + 1);
                a.tracking_number = be16(m + 3);
                a.ts_ns           = be48(m + 5);
                a.order_ref       = be64(m + 11);
                a.side            = (m[19] == 'B') ? Side::Buy : Side::Sell;
                a.shares          = be32(m + 20);
                a.stock           = read_symbol(m + 24);
                a.price           = be32(m + 32);
                h.on_add_order(a);
                // 'F' adds MPID at offset 36 (4 bytes); we ignore it.
                break;
            }
            case static_cast<char>(MessageType::OrderExecuted): {
                OrderExecuted e;
                e.stock_locate    = be16(m + 1);
                e.ts_ns           = be48(m + 5);
                e.order_ref       = be64(m + 11);
                e.executed_shares = be32(m + 19);
                e.match_number    = be64(m + 23);
                h.on_order_executed(e);
                break;
            }
            case static_cast<char>(MessageType::OrderExecutedWithPrice): {
                OrderExecutedWithPrice e;
                e.stock_locate    = be16(m + 1);
                e.ts_ns           = be48(m + 5);
                e.order_ref       = be64(m + 11);
                e.executed_shares = be32(m + 19);
                e.match_number    = be64(m + 23);
                e.printable       = (m[31] == 'Y');
                e.execution_price = be32(m + 32);
                h.on_order_executed_with_price(e);
                break;
            }
            case static_cast<char>(MessageType::OrderCancel): {
                OrderCancel c;
                c.stock_locate     = be16(m + 1);
                c.ts_ns            = be48(m + 5);
                c.order_ref        = be64(m + 11);
                c.cancelled_shares = be32(m + 19);
                h.on_order_cancel(c);
                break;
            }
            case static_cast<char>(MessageType::OrderDelete): {
                OrderDelete d;
                d.stock_locate = be16(m + 1);
                d.ts_ns        = be48(m + 5);
                d.order_ref    = be64(m + 11);
                h.on_order_delete(d);
                break;
            }
            case static_cast<char>(MessageType::OrderReplace): {
                OrderReplace r;
                r.stock_locate       = be16(m + 1);
                r.ts_ns              = be48(m + 5);
                r.original_order_ref = be64(m + 11);
                r.new_order_ref      = be64(m + 19);
                r.shares             = be32(m + 27);
                r.price              = be32(m + 31);
                h.on_order_replace(r);
                break;
            }
            default:
                h.on_unhandled(type, msg_len);
                break;
        }

        pos += msg_len;
        ++count;
    }

    return count;
}

}  // namespace lob::itch
