#pragma once

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <utility>

#include "lob/bitmap.hpp"
#include "lob/compiler.hpp"
#include "lob/intrusive_list.hpp"
#include "lob/order.hpp"
#include "lob/order_id_map.hpp"
#include "lob/price_level.hpp"
#include "lob/slab_allocator.hpp"
#include "lob/types.hpp"

namespace lob {

// Single-symbol price-time-priority limit order book.
//
// All prices are in *engine ticks* -- a non-negative integer. The boundary
// layer (the ITCH parser in this project) is responsible for dividing the wire
// price by the symbol's tick size before submitting.
//
// Each side is a flat array of `num_levels` PriceLevel slots, indexed by
// `price - base_price`. A companion bitmap tracks which levels are occupied,
// so finding the next best price after the current best is consumed is
// constant-time in the typical case (the next-occupied word is hot in L1)
// and worst-case proportional to the number of words spanning the gap.
class OrderBook {
public:
    struct Config {
        // First level represents this price (in ticks).
        Price       base_price = 0;
        // Number of price levels covered per side. Engine accepts prices in
        // [base_price, base_price + num_levels). Default 131072 = 16 KB bitmap
        // per side, which fits cleanly in L1D on modern x86.
        std::size_t num_levels = std::size_t{1} << 17;
        // Maximum number of simultaneously resting orders. The slab pre-
        // allocates this many Order slots. Sized for the expected peak.
        std::size_t max_orders = std::size_t{1} << 20;
    };

    static constexpr std::size_t kNoBest = static_cast<std::size_t>(-1);

    explicit OrderBook(Config cfg)
        : cfg_(cfg),
          slab_(cfg.max_orders),
          id_map_(next_pow2(cfg.max_orders * 2)),
          bid_levels_(std::make_unique<PriceLevel[]>(cfg.num_levels)),
          ask_levels_(std::make_unique<PriceLevel[]>(cfg.num_levels)),
          bid_bitmap_(cfg.num_levels),
          ask_bitmap_(cfg.num_levels) {}

    // ----- queries -------------------------------------------------------

    [[nodiscard]] LOB_ALWAYS_INLINE Price best_bid() const noexcept {
        return best_bid_idx_ == kNoBest ? kInvalidPrice : idx_to_price(best_bid_idx_);
    }
    [[nodiscard]] LOB_ALWAYS_INLINE Price best_ask() const noexcept {
        return best_ask_idx_ == kNoBest ? kInvalidPrice : idx_to_price(best_ask_idx_);
    }
    [[nodiscard]] LOB_ALWAYS_INLINE Quantity total_at(Side side, Price price) const noexcept {
        if (!in_range(price)) return 0;
        auto idx = price_to_idx(price);
        return side == Side::Buy ? bid_levels_[idx].total_qty
                                 : ask_levels_[idx].total_qty;
    }
    [[nodiscard]] std::size_t open_orders() const noexcept { return slab_.in_use(); }

    struct LevelSnapshot {
        Price         price;
        Quantity      qty;
        std::uint32_t count;
    };

    // Fills `out` with up to `n` levels best-first. Returns the count written.
    std::size_t top_n(Side side, std::size_t n, LevelSnapshot* out) const noexcept {
        std::size_t written = 0;
        if (side == Side::Buy) {
            std::size_t i = best_bid_idx_;
            while (written < n && i != kNoBest) {
                const PriceLevel& lvl = bid_levels_[i];
                out[written++] = {idx_to_price(i), lvl.total_qty, lvl.count};
                i = (i == 0) ? Bitmap::kNotFound : bid_bitmap_.find_prev_set(i - 1);
                if (i == Bitmap::kNotFound) break;
            }
        } else {
            std::size_t i = best_ask_idx_;
            while (written < n && i != kNoBest) {
                const PriceLevel& lvl = ask_levels_[i];
                out[written++] = {idx_to_price(i), lvl.total_qty, lvl.count};
                i = ask_bitmap_.find_next_set(i + 1);
                if (i == Bitmap::kNotFound) break;
            }
        }
        return written;
    }

    // ----- mutations -----------------------------------------------------

    // Submit an order. `on_trade` is invoked once per fill in price-time order.
    // For LIMIT orders, `price` is the limit (and must be in range).
    // For MARKET orders, `price` is ignored; the order walks the book until
    // its quantity is exhausted or the opposite side is empty (any remainder
    // is dropped -- a market order never rests).
    template <typename Sink>
    LOB_HOT SubmitResult submit(OrderId id, Side side, OrderType type, TimeInForce tif,
                                Price price, Quantity qty, Timestamp ts,
                                Sink&& on_trade) {
        if (LOB_UNLIKELY(qty == 0 || id == kInvalidOrderId)) {
            return {OrderStatus::Rejected, 0, 0};
        }

        // Duplicate-id rejection: ITCH guarantees unique ids per session but
        // we defend the engine against bad inputs.
        if (LOB_UNLIKELY(id_map_.find(id) != nullptr)) {
            return {OrderStatus::Rejected, 0, 0};
        }

        const bool is_market = (type == OrderType::Market);

        if (!is_market && LOB_UNLIKELY(!in_range(price))) {
            return {OrderStatus::Rejected, 0, 0};
        }

        // FOK: do a non-destructive walk first; reject if the book can't
        // fully absorb the order. Slightly redundant work versus the matching
        // loop but FOK is rare enough that the simplicity is worth it.
        if (tif == TimeInForce::FOK) {
            if (!can_fully_fill(side, is_market ? kInvalidPrice : price, qty)) {
                return {OrderStatus::Cancelled, 0, qty};
            }
        }

        // POST_ONLY: only rests, never crosses. Reject if it would cross.
        if (tif == TimeInForce::PostOnly) {
            if (would_cross(side, price)) {
                return {OrderStatus::Cancelled, 0, qty};
            }
        }

        Quantity remaining = qty;

        // Aggressive phase: only if not POST_ONLY and there is something to
        // cross. We branch on side once to specialise the matching loop.
        if (tif != TimeInForce::PostOnly &&
            (is_market || would_cross(side, price))) {
            if (side == Side::Buy) {
                remaining = match_<Side::Buy>(is_market ? kNoBest : price_to_idx(price),
                                              remaining, id, ts, on_trade);
            } else {
                remaining = match_<Side::Sell>(is_market ? kNoBest : price_to_idx(price),
                                               remaining, id, ts, on_trade);
            }
        }

        if (remaining == 0) {
            return {OrderStatus::FullyFilled, qty, 0};
        }

        // Non-resting paths: IOC keeps what filled, drops the rest. Market
        // orders never rest. FOK never reaches here with remainder > 0
        // because the pre-check would have rejected.
        if (tif == TimeInForce::IOC || is_market) {
            const Quantity filled = qty - remaining;
            const auto status = filled > 0 ? OrderStatus::PartiallyFilled
                                           : OrderStatus::Cancelled;
            return {status, filled, remaining};
        }

        // Rest the remainder as a passive order.
        insert_resting_(id, side, price, remaining);
        return {qty == remaining ? OrderStatus::Accepted
                                 : OrderStatus::PartiallyFilled,
                qty - remaining, remaining};
    }

    // Cancel an order by id. Returns false if no such id is live.
    LOB_HOT bool cancel(OrderId id) noexcept {
        Order* o = id_map_.find(id);
        if (LOB_UNLIKELY(o == nullptr)) return false;
        remove_from_book_(o);
        id_map_.erase(id);
        slab_.deallocate(o);
        return true;
    }

    // Modify an existing order. NASDAQ semantics: a modify of price or an
    // upward quantity revision loses time priority; a same-or-lower quantity
    // at the same price retains priority (handled in place).
    template <typename Sink>
    LOB_HOT SubmitResult modify(OrderId id, Quantity new_qty, Price new_price,
                                Timestamp ts, Sink&& on_trade) {
        Order* o = id_map_.find(id);
        if (LOB_UNLIKELY(o == nullptr)) return {OrderStatus::Rejected, 0, 0};

        const Side  side      = o->side;
        const Price old_price = o->price;

        // In-place quantity-down at same price: preserve FIFO position.
        if (new_price == old_price && new_qty > 0 && new_qty <= o->quantity) {
            Quantity delta = o->quantity - new_qty;
            o->quantity    = new_qty;
            PriceLevel& lvl = level_(side, o->level_idx);
            lvl.total_qty -= delta;
            return {OrderStatus::Accepted, 0, new_qty};
        }

        // Otherwise cancel + new (which is what real exchanges do for any
        // change that increases priority risk for the modifier).
        cancel(id);
        return submit(id, side, OrderType::Limit, TimeInForce::GTC,
                      new_price, new_qty, ts, std::forward<Sink>(on_trade));
    }

private:
    // ----- helpers --------------------------------------------------------

    LOB_ALWAYS_INLINE std::uint32_t price_to_idx(Price p) const noexcept {
        return static_cast<std::uint32_t>(p - cfg_.base_price);
    }
    LOB_ALWAYS_INLINE Price idx_to_price(std::size_t i) const noexcept {
        return cfg_.base_price + static_cast<Price>(i);
    }
    LOB_ALWAYS_INLINE bool in_range(Price p) const noexcept {
        return p >= cfg_.base_price &&
               static_cast<std::size_t>(p - cfg_.base_price) < cfg_.num_levels;
    }
    LOB_ALWAYS_INLINE PriceLevel& level_(Side s, std::size_t idx) noexcept {
        return s == Side::Buy ? bid_levels_[idx] : ask_levels_[idx];
    }

    LOB_ALWAYS_INLINE bool would_cross(Side side, Price p) const noexcept {
        if (side == Side::Buy) {
            return best_ask_idx_ != kNoBest &&
                   static_cast<std::size_t>(p - cfg_.base_price) >= best_ask_idx_;
        }
        return best_bid_idx_ != kNoBest &&
               static_cast<std::size_t>(p - cfg_.base_price) <= best_bid_idx_;
    }

    bool can_fully_fill(Side side, Price limit_price, Quantity qty) const noexcept {
        const bool is_market = (limit_price == kInvalidPrice);
        if (side == Side::Buy) {
            std::size_t i = best_ask_idx_;
            const std::size_t limit = is_market ? cfg_.num_levels - 1
                                                : price_to_idx(limit_price);
            Quantity acc = 0;
            while (i != kNoBest && i <= limit) {
                acc += ask_levels_[i].total_qty;
                if (acc >= qty) return true;
                i = ask_bitmap_.find_next_set(i + 1);
                if (i == Bitmap::kNotFound) break;
            }
            return false;
        } else {
            std::size_t i = best_bid_idx_;
            const std::size_t limit = is_market ? 0 : price_to_idx(limit_price);
            Quantity acc = 0;
            while (i != kNoBest && i >= limit) {
                acc += bid_levels_[i].total_qty;
                if (acc >= qty) return true;
                if (i == 0) break;
                i = bid_bitmap_.find_prev_set(i - 1);
                if (i == Bitmap::kNotFound) break;
            }
            return false;
        }
    }

    // Walk the opposite side filling against resting orders until either qty
    // is exhausted, the limit price is breached, or the opposite side empties.
    // Returns the unfilled remainder of the aggressor.
    template <Side AggressorSide, typename Sink>
    LOB_HOT LOB_FLATTEN Quantity match_(std::size_t limit_idx_or_no_limit,
                                        Quantity remaining,
                                        OrderId aggressor_id,
                                        Timestamp ts,
                                        Sink& on_trade) {
        constexpr bool kBuy = (AggressorSide == Side::Buy);

        std::size_t i = kBuy ? best_ask_idx_ : best_bid_idx_;
        const std::size_t limit = limit_idx_or_no_limit;

        while (i != kNoBest && remaining > 0) {
            // Price-limit check. For market orders limit==kNoBest (cast of -1
            // to size_t = SIZE_MAX) so the check is always false.
            if constexpr (kBuy) {
                if (limit != kNoBest && i > limit) break;
            } else {
                if (limit != kNoBest && i < limit) break;
            }

            PriceLevel& lvl = kBuy ? ask_levels_[i] : bid_levels_[i];

            while (remaining > 0 && !lvl.orders.empty()) {
                Order* head = lvl.orders.head();
                const Quantity fill = std::min(remaining, head->quantity);

                on_trade(Trade{head->order_id, aggressor_id, idx_to_price(i),
                               fill, ts, AggressorSide});

                head->quantity -= fill;
                lvl.total_qty  -= fill;
                remaining      -= fill;

                if (head->quantity == 0) {
                    lvl.orders.unlink(head);
                    --lvl.count;
                    id_map_.erase(head->order_id);
                    slab_.deallocate(head);
                }
            }

            if (lvl.empty()) {
                if constexpr (kBuy) {
                    ask_bitmap_.clear(i);
                    auto next = ask_bitmap_.find_next_set(i + 1);
                    best_ask_idx_ = (next == Bitmap::kNotFound) ? kNoBest : next;
                    i = best_ask_idx_;
                } else {
                    bid_bitmap_.clear(i);
                    if (i == 0) {
                        best_bid_idx_ = kNoBest;
                        i = kNoBest;
                    } else {
                        auto prev = bid_bitmap_.find_prev_set(i - 1);
                        best_bid_idx_ = (prev == Bitmap::kNotFound) ? kNoBest : prev;
                        i = best_bid_idx_;
                    }
                }
            } else {
                // Level still has resting volume; remaining must be 0 here.
                break;
            }
        }

        return remaining;
    }

    LOB_HOT void insert_resting_(OrderId id, Side side, Price price, Quantity qty) noexcept {
        Order* o   = slab_.allocate();
        assert(o != nullptr);  // slab exhaustion = configuration bug
        const std::uint32_t idx = price_to_idx(price);
        o->prev      = nullptr;
        o->next      = nullptr;
        o->order_id  = id;
        o->quantity  = qty;
        o->price     = price;
        o->level_idx = idx;
        o->side      = side;

        PriceLevel& lvl = level_(side, idx);
        const bool was_empty = lvl.empty();
        lvl.orders.push_back(o);
        lvl.total_qty += qty;
        ++lvl.count;

        if (was_empty) {
            if (side == Side::Buy) {
                bid_bitmap_.set(idx);
                if (best_bid_idx_ == kNoBest || idx > best_bid_idx_) {
                    best_bid_idx_ = idx;
                }
            } else {
                ask_bitmap_.set(idx);
                if (best_ask_idx_ == kNoBest || idx < best_ask_idx_) {
                    best_ask_idx_ = idx;
                }
            }
        }

        id_map_.insert(id, o);
    }

    LOB_HOT void remove_from_book_(Order* o) noexcept {
        PriceLevel& lvl = level_(o->side, o->level_idx);
        lvl.orders.unlink(o);
        lvl.total_qty -= o->quantity;
        --lvl.count;

        if (lvl.empty()) {
            if (o->side == Side::Buy) {
                bid_bitmap_.clear(o->level_idx);
                if (best_bid_idx_ == o->level_idx) {
                    if (o->level_idx == 0) {
                        best_bid_idx_ = kNoBest;
                    } else {
                        auto p = bid_bitmap_.find_prev_set(o->level_idx - 1);
                        best_bid_idx_ = (p == Bitmap::kNotFound) ? kNoBest : p;
                    }
                }
            } else {
                ask_bitmap_.clear(o->level_idx);
                if (best_ask_idx_ == o->level_idx) {
                    auto n = ask_bitmap_.find_next_set(o->level_idx + 1);
                    best_ask_idx_ = (n == Bitmap::kNotFound) ? kNoBest : n;
                }
            }
        }
    }

    static std::size_t next_pow2(std::size_t n) noexcept {
        std::size_t p = 1;
        while (p < n) p <<= 1;
        return p;
    }

    Config                          cfg_;
    SlabAllocator<Order>            slab_;
    OrderIdMap<Order>               id_map_;
    std::unique_ptr<PriceLevel[]>   bid_levels_;
    std::unique_ptr<PriceLevel[]>   ask_levels_;
    Bitmap                          bid_bitmap_;
    Bitmap                          ask_bitmap_;
    std::size_t                     best_bid_idx_{kNoBest};
    std::size_t                     best_ask_idx_{kNoBest};
};

// A trade sink that discards everything -- useful in benchmarks where we want
// to isolate book/match cost from any I/O the real sink would do.
struct NullSink {
    LOB_ALWAYS_INLINE void operator()(const Trade&) const noexcept {}
};

}  // namespace lob
