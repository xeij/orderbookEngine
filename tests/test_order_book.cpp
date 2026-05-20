#include <catch2/catch_test_macros.hpp>

#include <vector>

#include "lob/order_book.hpp"

namespace {

struct VecSink {
    std::vector<lob::Trade>* out;
    void operator()(const lob::Trade& t) const { out->push_back(t); }
};

lob::OrderBook make_book() {
    return lob::OrderBook{lob::OrderBook::Config{
        /*base_price=*/0, /*num_levels=*/4096, /*max_orders=*/1024}};
}

}  // namespace

TEST_CASE("Empty book has invalid best prices", "[book]") {
    auto book = make_book();
    REQUIRE(book.best_bid() == lob::kInvalidPrice);
    REQUIRE(book.best_ask() == lob::kInvalidPrice);
    REQUIRE(book.open_orders() == 0);
}

TEST_CASE("Resting limit orders set best bid and ask", "[book]") {
    auto book = make_book();
    std::vector<lob::Trade> trades;
    VecSink sink{&trades};

    auto r1 = book.submit(1, lob::Side::Buy, lob::OrderType::Limit,
                          lob::TimeInForce::GTC, 100, 10, 1, sink);
    REQUIRE(r1.status == lob::OrderStatus::Accepted);
    REQUIRE(book.best_bid() == 100);
    REQUIRE(book.best_ask() == lob::kInvalidPrice);

    auto r2 = book.submit(2, lob::Side::Sell, lob::OrderType::Limit,
                          lob::TimeInForce::GTC, 105, 7, 2, sink);
    REQUIRE(r2.status == lob::OrderStatus::Accepted);
    REQUIRE(book.best_ask() == 105);

    REQUIRE(trades.empty());
    REQUIRE(book.total_at(lob::Side::Buy, 100) == 10);
    REQUIRE(book.total_at(lob::Side::Sell, 105) == 7);
}

TEST_CASE("New best bid replaces inferior bid", "[book]") {
    auto book = make_book();
    std::vector<lob::Trade> trades;
    VecSink sink{&trades};
    book.submit(1, lob::Side::Buy, lob::OrderType::Limit, lob::TimeInForce::GTC, 100, 5, 1, sink);
    book.submit(2, lob::Side::Buy, lob::OrderType::Limit, lob::TimeInForce::GTC, 101, 5, 2, sink);
    REQUIRE(book.best_bid() == 101);
    book.submit(3, lob::Side::Buy, lob::OrderType::Limit, lob::TimeInForce::GTC, 99, 5, 3, sink);
    REQUIRE(book.best_bid() == 101);
}

TEST_CASE("Cancel of best bid recovers next-best", "[book]") {
    auto book = make_book();
    std::vector<lob::Trade> trades;
    VecSink sink{&trades};
    book.submit(1, lob::Side::Buy, lob::OrderType::Limit, lob::TimeInForce::GTC, 100, 5, 1, sink);
    book.submit(2, lob::Side::Buy, lob::OrderType::Limit, lob::TimeInForce::GTC, 101, 5, 2, sink);
    book.submit(3, lob::Side::Buy, lob::OrderType::Limit, lob::TimeInForce::GTC, 99,  5, 3, sink);
    REQUIRE(book.best_bid() == 101);
    REQUIRE(book.cancel(2));
    REQUIRE(book.best_bid() == 100);
    REQUIRE(book.cancel(1));
    REQUIRE(book.best_bid() == 99);
    REQUIRE(book.cancel(3));
    REQUIRE(book.best_bid() == lob::kInvalidPrice);
}

TEST_CASE("Cancel of non-best leaves best unchanged", "[book]") {
    auto book = make_book();
    std::vector<lob::Trade> trades;
    VecSink sink{&trades};
    book.submit(1, lob::Side::Buy, lob::OrderType::Limit, lob::TimeInForce::GTC, 100, 5, 1, sink);
    book.submit(2, lob::Side::Buy, lob::OrderType::Limit, lob::TimeInForce::GTC, 99,  5, 2, sink);
    REQUIRE(book.best_bid() == 100);
    REQUIRE(book.cancel(2));
    REQUIRE(book.best_bid() == 100);
}

TEST_CASE("Same-level cancel preserves bitmap when other orders remain", "[book]") {
    auto book = make_book();
    std::vector<lob::Trade> trades;
    VecSink sink{&trades};
    book.submit(1, lob::Side::Buy, lob::OrderType::Limit, lob::TimeInForce::GTC, 100, 5, 1, sink);
    book.submit(2, lob::Side::Buy, lob::OrderType::Limit, lob::TimeInForce::GTC, 100, 5, 2, sink);
    REQUIRE(book.cancel(1));
    REQUIRE(book.best_bid() == 100);  // still at 100 via order 2
    REQUIRE(book.total_at(lob::Side::Buy, 100) == 5);
}

TEST_CASE("Modify down quantity at same price preserves time priority", "[book]") {
    auto book = make_book();
    std::vector<lob::Trade> trades;
    VecSink sink{&trades};
    book.submit(1, lob::Side::Buy, lob::OrderType::Limit, lob::TimeInForce::GTC, 100, 10, 1, sink);
    book.submit(2, lob::Side::Buy, lob::OrderType::Limit, lob::TimeInForce::GTC, 100, 10, 2, sink);
    book.modify(1, 3, 100, 3, sink);  // shrink order 1
    REQUIRE(book.total_at(lob::Side::Buy, 100) == 13);

    // An aggressive sell of 5 should hit order 1 first (3 filled), then order 2 (2 filled).
    book.submit(99, lob::Side::Sell, lob::OrderType::Limit, lob::TimeInForce::IOC, 100, 5, 4, sink);
    REQUIRE(trades.size() == 2);
    REQUIRE(trades[0].resting_id == 1);
    REQUIRE(trades[0].quantity == 3);
    REQUIRE(trades[1].resting_id == 2);
    REQUIRE(trades[1].quantity == 2);
}

TEST_CASE("Modify price moves order to new level", "[book]") {
    auto book = make_book();
    std::vector<lob::Trade> trades;
    VecSink sink{&trades};
    book.submit(1, lob::Side::Buy, lob::OrderType::Limit, lob::TimeInForce::GTC, 100, 5, 1, sink);
    book.modify(1, 5, 102, 2, sink);
    REQUIRE(book.best_bid() == 102);
    REQUIRE(book.total_at(lob::Side::Buy, 100) == 0);
}

TEST_CASE("Duplicate order id is rejected", "[book]") {
    auto book = make_book();
    std::vector<lob::Trade> trades;
    VecSink sink{&trades};
    book.submit(1, lob::Side::Buy, lob::OrderType::Limit, lob::TimeInForce::GTC, 100, 5, 1, sink);
    auto r = book.submit(1, lob::Side::Buy, lob::OrderType::Limit, lob::TimeInForce::GTC, 100, 5, 1, sink);
    REQUIRE(r.status == lob::OrderStatus::Rejected);
}

TEST_CASE("Out-of-range price is rejected", "[book]") {
    auto book = make_book();
    std::vector<lob::Trade> trades;
    VecSink sink{&trades};
    auto r = book.submit(1, lob::Side::Buy, lob::OrderType::Limit, lob::TimeInForce::GTC, 99999, 5, 1, sink);
    REQUIRE(r.status == lob::OrderStatus::Rejected);
}
