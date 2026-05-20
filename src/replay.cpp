// ITCH replay harness.
//
// Memory-maps a NASDAQ TotalView-ITCH 5.0 file, filters to a single symbol,
// drives the matching engine with the order-life-cycle messages, and emits:
//   * a CSV of every per-message engine latency (or histogram summary), and
//   * the end-of-day top-N book snapshot for cross-validation against
//     LOBSTER / the published exchange snapshot.
//
// Each ITCH order message expresses a state change that the exchange has
// already executed, so we use the book's *passive* primitives (submit-with-
// GTC, reduce, cancel, replace) rather than running our own matching. The
// "lock-free, sub-microsecond" claim is about those primitives -- their
// timings are what the histogram captures.
//
// CLI:
//   lob_replay [--symbol SYM] [--base PRICE_TICKS] [--num-levels N]
//              [--max-orders N] [--tick TICK_IN_1e-4_DOLLARS]
//              [--top-n N] [--out-dir DIR] [--snapshot-only] FILE.itch

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <string>

#include "lob/clock.hpp"
#include "lob/histogram.hpp"
#include "lob/itch.hpp"
#include "lob/mapped_file.hpp"
#include "lob/order_book.hpp"

namespace {

struct Args {
    std::string file;
    std::string symbol      = "AAPL";
    std::string out_dir     = ".";
    lob::Price  base_price  = 0;
    std::size_t num_levels  = std::size_t{1} << 17;
    std::size_t max_orders  = std::size_t{1} << 20;
    std::int32_t itch_tick  = 100;  // 1/10000 dollars per tick (penny default)
    std::size_t top_n       = 10;
    bool        snapshot_only = false;
};

Args parse_args(int argc, char** argv) {
    Args a;
    auto need = [&](int& i, const char* flag) {
        if (i + 1 >= argc) {
            std::fprintf(stderr, "missing value for %s\n", flag);
            std::exit(2);
        }
        return argv[++i];
    };
    for (int i = 1; i < argc; ++i) {
        std::string s = argv[i];
        if (s == "--symbol")        a.symbol      = need(i, "--symbol");
        else if (s == "--out-dir")  a.out_dir     = need(i, "--out-dir");
        else if (s == "--base")     a.base_price  = std::atoi(need(i, "--base"));
        else if (s == "--num-levels") a.num_levels = static_cast<std::size_t>(std::atoll(need(i, "--num-levels")));
        else if (s == "--max-orders") a.max_orders = static_cast<std::size_t>(std::atoll(need(i, "--max-orders")));
        else if (s == "--tick")     a.itch_tick   = std::atoi(need(i, "--tick"));
        else if (s == "--top-n")    a.top_n       = static_cast<std::size_t>(std::atoll(need(i, "--top-n")));
        else if (s == "--snapshot-only") a.snapshot_only = true;
        else if (s == "-h" || s == "--help") {
            std::puts(
                "Usage: lob_replay [options] FILE.itch\n"
                "  --symbol SYM        ticker to reconstruct (default AAPL)\n"
                "  --base N            base engine-tick (default 0)\n"
                "  --num-levels N      price levels per side (default 131072)\n"
                "  --max-orders N      slab capacity (default 1048576)\n"
                "  --tick T            ITCH-ticks per engine-tick (default 100 = penny)\n"
                "  --top-n N           levels in snapshot output (default 10)\n"
                "  --out-dir DIR       output directory (default .)\n"
                "  --snapshot-only     skip latency capture, just reconstruct\n");
            std::exit(0);
        }
        else if (!s.empty() && s[0] != '-') a.file = s;
        else { std::fprintf(stderr, "unknown arg: %s\n", s.c_str()); std::exit(2); }
    }
    if (a.file.empty()) { std::fprintf(stderr, "missing input file\n"); std::exit(2); }
    return a;
}

bool symbol_matches(const std::array<char, 8>& wire, const std::string& want) {
    // ITCH symbols are 8 ASCII bytes, space-padded on the right.
    for (std::size_t i = 0; i < 8; ++i) {
        char w = i < want.size() ? want[i] : ' ';
        if (wire[i] != w) return false;
    }
    return true;
}

class ReplayHandler final : public lob::itch::ItchHandler {
public:
    ReplayHandler(lob::OrderBook& book, std::string symbol, std::int32_t itch_tick,
                  lob::LatencyHistogram* hist_submit,
                  lob::LatencyHistogram* hist_cancel,
                  lob::LatencyHistogram* hist_reduce)
        : book_(book),
          symbol_(std::move(symbol)),
          itch_tick_(itch_tick),
          hist_submit_(hist_submit),
          hist_cancel_(hist_cancel),
          hist_reduce_(hist_reduce) {}

    void on_stock_directory(const lob::itch::StockDirectory& d) override {
        if (target_locate_ == 0 && symbol_matches(d.stock, symbol_)) {
            target_locate_ = d.stock_locate;
            std::fprintf(stderr, "[replay] target symbol %s -> locate=%u\n",
                         symbol_.c_str(), target_locate_);
        }
    }

    void on_add_order(const lob::itch::AddOrder& a) override {
        if (a.stock_locate != target_locate_) return;
        const lob::Price engine_price =
            static_cast<lob::Price>(a.price / static_cast<std::uint32_t>(itch_tick_));
        time_(*hist_submit_, [&] {
            book_.submit(a.order_ref, a.side, lob::OrderType::Limit,
                         lob::TimeInForce::GTC, engine_price, a.shares,
                         a.ts_ns, sink_);
        });
        ++adds_;
    }

    void on_order_executed(const lob::itch::OrderExecuted& e) override {
        if (e.stock_locate != target_locate_) return;
        time_(*hist_reduce_, [&] { book_.reduce(e.order_ref, e.executed_shares); });
        ++execs_;
    }

    void on_order_executed_with_price(const lob::itch::OrderExecutedWithPrice& e) override {
        if (e.stock_locate != target_locate_) return;
        time_(*hist_reduce_, [&] { book_.reduce(e.order_ref, e.executed_shares); });
        ++execs_;
    }

    void on_order_cancel(const lob::itch::OrderCancel& c) override {
        if (c.stock_locate != target_locate_) return;
        time_(*hist_reduce_, [&] { book_.reduce(c.order_ref, c.cancelled_shares); });
        ++cancels_;
    }

    void on_order_delete(const lob::itch::OrderDelete& d) override {
        if (d.stock_locate != target_locate_) return;
        time_(*hist_cancel_, [&] { book_.cancel(d.order_ref); });
        ++deletes_;
    }

    void on_order_replace(const lob::itch::OrderReplace& r) override {
        if (r.stock_locate != target_locate_) return;
        // Replace is implemented atomically by exchanges; our engine emulates
        // it as cancel + new with a fresh order_ref (NASDAQ assigns a new ref
        // on replace). The 'U' wire message does not carry the side -- we
        // read it from the still-resting original order before cancelling.
        const lob::Order* original = book_.find(r.original_order_ref);
        if (original == nullptr) { ++replaces_skipped_; return; }
        const lob::Side side = original->side;
        const lob::Price engine_price =
            static_cast<lob::Price>(r.price / static_cast<std::uint32_t>(itch_tick_));
        time_(*hist_cancel_, [&] { book_.cancel(r.original_order_ref); });
        time_(*hist_submit_, [&] {
            book_.submit(r.new_order_ref, side, lob::OrderType::Limit,
                         lob::TimeInForce::GTC, engine_price, r.shares,
                         r.ts_ns, sink_);
        });
        ++replaces_;
    }

    std::uint64_t adds()    const noexcept { return adds_; }
    std::uint64_t execs()   const noexcept { return execs_; }
    std::uint64_t cancels() const noexcept { return cancels_; }
    std::uint64_t deletes() const noexcept { return deletes_; }
    std::uint64_t replaces() const noexcept { return replaces_; }
    std::uint64_t replaces_skipped() const noexcept { return replaces_skipped_; }

private:
    template <typename Fn>
    LOB_ALWAYS_INLINE void time_(lob::LatencyHistogram& hist, Fn&& fn) {
        const std::uint64_t t0 = lob::now_ns();
        std::forward<Fn>(fn)();
        const std::uint64_t t1 = lob::now_ns();
        hist.record(t1 - t0);
    }

    lob::OrderBook&        book_;
    lob::NullSink          sink_{};
    std::string            symbol_;
    std::int32_t           itch_tick_;
    std::uint16_t          target_locate_{0};
    lob::LatencyHistogram* hist_submit_;
    lob::LatencyHistogram* hist_cancel_;
    lob::LatencyHistogram* hist_reduce_;
    std::uint64_t          adds_{0};
    std::uint64_t          execs_{0};
    std::uint64_t          cancels_{0};
    std::uint64_t          deletes_{0};
    std::uint64_t          replaces_{0};
    std::uint64_t          replaces_skipped_{0};
};

void write_snapshot(const lob::OrderBook& book, std::size_t top_n,
                    const std::string& out_path) {
    std::vector<lob::OrderBook::LevelSnapshot> bids(top_n);
    std::vector<lob::OrderBook::LevelSnapshot> asks(top_n);
    const std::size_t nb = book.top_n(lob::Side::Buy, top_n, bids.data());
    const std::size_t na = book.top_n(lob::Side::Sell, top_n, asks.data());

    std::ofstream os(out_path);
    os << "side,level,price,quantity,order_count\n";
    for (std::size_t i = 0; i < nb; ++i)
        os << "B," << i << ',' << bids[i].price << ',' << bids[i].qty
           << ',' << bids[i].count << '\n';
    for (std::size_t i = 0; i < na; ++i)
        os << "A," << i << ',' << asks[i].price << ',' << asks[i].qty
           << ',' << asks[i].count << '\n';
}

}  // namespace

int main(int argc, char** argv) {
    const Args args = parse_args(argc, argv);

    lob::MappedFile file(args.file);
    std::fprintf(stderr, "[replay] mapped %s (%.2f MB)\n",
                 args.file.c_str(), static_cast<double>(file.size()) / (1024.0 * 1024.0));

    lob::OrderBook book(lob::OrderBook::Config{
        args.base_price, args.num_levels, args.max_orders});

    lob::LatencyHistogram h_submit, h_cancel, h_reduce;
    ReplayHandler handler(book, args.symbol, args.itch_tick,
                          &h_submit, &h_cancel, &h_reduce);

    const auto t0 = std::chrono::steady_clock::now();
    const std::size_t n_msgs = lob::itch::parse(file.data(), file.size(), handler);
    const auto t1 = std::chrono::steady_clock::now();
    const auto elapsed_ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();

    std::fprintf(stderr,
        "[replay] parsed %zu ITCH messages in %lld ms (%.2f Mmsg/s)\n",
        n_msgs, static_cast<long long>(elapsed_ms),
        static_cast<double>(n_msgs) / std::max<double>(1.0, elapsed_ms) / 1000.0);
    std::fprintf(stderr,
        "[replay] %s adds=%llu execs=%llu cancels=%llu deletes=%llu "
        "replaces=%llu (replace_skipped=%llu)\n",
        args.symbol.c_str(),
        static_cast<unsigned long long>(handler.adds()),
        static_cast<unsigned long long>(handler.execs()),
        static_cast<unsigned long long>(handler.cancels()),
        static_cast<unsigned long long>(handler.deletes()),
        static_cast<unsigned long long>(handler.replaces()),
        static_cast<unsigned long long>(handler.replaces_skipped()));
    std::fprintf(stderr, "[replay] book best_bid=%d best_ask=%d open=%zu\n",
                 book.best_bid(), book.best_ask(), book.open_orders());

    if (!args.snapshot_only) {
        std::ofstream hist_csv_submit(args.out_dir + "/latency_submit.csv");
        std::ofstream hist_csv_cancel(args.out_dir + "/latency_cancel.csv");
        std::ofstream hist_csv_reduce(args.out_dir + "/latency_reduce.csv");
        h_submit.write_csv(hist_csv_submit);
        h_cancel.write_csv(hist_csv_cancel);
        h_reduce.write_csv(hist_csv_reduce);

        std::ofstream summary(args.out_dir + "/latency_summary.txt");
        summary << "submit: "; h_submit.write_summary(summary);
        summary << "cancel: "; h_cancel.write_summary(summary);
        summary << "reduce: "; h_reduce.write_summary(summary);

        std::fprintf(stderr, "[replay] wrote latency CSVs and summary to %s/\n",
                     args.out_dir.c_str());
        std::fputs("submit: ", stderr); h_submit.write_summary(std::cerr);
        std::fputs("cancel: ", stderr); h_cancel.write_summary(std::cerr);
        std::fputs("reduce: ", stderr); h_reduce.write_summary(std::cerr);
    }

    write_snapshot(book, args.top_n, args.out_dir + "/snapshot.csv");
    std::fprintf(stderr, "[replay] wrote EOD snapshot to %s/snapshot.csv\n",
                 args.out_dir.c_str());
    return 0;
}
