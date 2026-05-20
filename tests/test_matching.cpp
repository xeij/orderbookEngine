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

TEST_CASE("Aggressive buy crosses and fills the best ask", "[match]") {
    auto book = make_book();
    std::vector<lob::Trade> trades;
    VecSink sink{&trades};
    book.submit(1, lob::Side::Sell, lob::OrderType::Limit, lob::TimeInForce::GTC, 105, 10, 1, sink);
    auto r = book.submit(2, lob::Side::Buy,  lob::OrderType::Limit, lob::TimeInForce::GTC, 110,  4, 2, sink);
    REQUIRE(r.status == lob::OrderStatus::FullyFilled);
    REQUIRE(r.filled_qty == 4);
    REQUIRE(trades.size() == 1);
    REQUIRE(trades[0].resting_id == 1);
    REQUIRE(trades[0].aggressor_id == 2);
    REQUIRE(trades[0].price == 105);
    REQUIRE(trades[0].quantity == 4);
    REQUIRE(book.total_at(lob::Side::Sell, 105) == 6);  // resting reduced
    REQUIRE(book.best_ask() == 105);
}

TEST_CASE("Aggressive buy that exceeds top-of-book walks to next level", "[match]") {
    auto book = make_book();
    std::vector<lob::Trade> trades;
    VecSink sink{&trades};
    book.submit(1, lob::Side::Sell, lob::OrderType::Limit, lob::TimeInForce::GTC, 105, 3, 1, sink);
    book.submit(2, lob::Side::Sell, lob::OrderType::Limit, lob::TimeInForce::GTC, 106, 5, 2, sink);
    book.submit(3, lob::Side::Sell, lob::OrderType::Limit, lob::TimeInForce::GTC, 107, 5, 3, sink);

    auto r = book.submit(99, lob::Side::Buy, lob::OrderType::Limit,
                         lob::TimeInForce::GTC, 106, 7, 4, sink);
    REQUIRE(r.status == lob::OrderStatus::FullyFilled);
    REQUIRE(trades.size() == 2);
    REQUIRE(trades[0].price == 105);
    REQUIRE(trades[0].quantity == 3);
    REQUIRE(trades[1].price == 106);
    REQUIRE(trades[1].quantity == 4);
    REQUIRE(book.best_ask() == 106);
    REQUIRE(book.total_at(lob::Side::Sell, 106) == 1);
    REQUIRE(book.total_at(lob::Side::Sell, 107) == 5);
}

TEST_CASE("Price-time priority across two orders at the same level", "[match]") {
    auto book = make_book();
    std::vector<lob::Trade> trades;
    VecSink sink{&trades};
    book.submit(1, lob::Side::Sell, lob::OrderType::Limit, lob::TimeInForce::GTC, 105, 3, 1, sink);
    book.submit(2, lob::Side::Sell, lob::OrderType::Limit, lob::TimeInForce::GTC, 105, 5, 2, sink);
    auto r = book.submit(99, lob::Side::Buy, lob::OrderType::Limit,
                         lob::TimeInForce::GTC, 105, 6, 3, sink);
    REQUIRE(r.status == lob::OrderStatus::FullyFilled);
    REQUIRE(trades.size() == 2);
    REQUIRE(trades[0].resting_id == 1);  // older filled first
    REQUIRE(trades[0].quantity == 3);
    REQUIRE(trades[1].resting_id == 2);
    REQUIRE(trades[1].quantity == 3);
    REQUIRE(book.total_at(lob::Side::Sell, 105) == 2);
}

TEST_CASE("Aggressive buy with insufficient liquidity rests the remainder", "[match]") {
    auto book = make_book();
    std::vector<lob::Trade> trades;
    VecSink sink{&trades};
    book.submit(1, lob::Side::Sell, lob::OrderType::Limit, lob::TimeInForce::GTC, 105, 3, 1, sink);
    auto r = book.submit(99, lob::Side::Buy, lob::OrderType::Limit,
                         lob::TimeInForce::GTC, 110, 7, 2, sink);
    REQUIRE(r.status == lob::OrderStatus::PartiallyFilled);
    REQUIRE(r.filled_qty == 3);
    REQUIRE(r.remaining_qty == 4);
    REQUIRE(book.best_bid() == 110);
    REQUIRE(book.total_at(lob::Side::Buy, 110) == 4);
    REQUIRE(book.best_ask() == lob::kInvalidPrice);
}

TEST_CASE("Symmetric: aggressive sell crosses best bid", "[match]") {
    auto book = make_book();
    std::vector<lob::Trade> trades;
    VecSink sink{&trades};
    book.submit(1, lob::Side::Buy, lob::OrderType::Limit, lob::TimeInForce::GTC, 100, 3, 1, sink);
    book.submit(2, lob::Side::Buy, lob::OrderType::Limit, lob::TimeInForce::GTC, 99,  5, 2, sink);
    auto r = book.submit(99, lob::Side::Sell, lob::OrderType::Limit,
                         lob::TimeInForce::GTC, 99, 5, 3, sink);
    REQUIRE(r.status == lob::OrderStatus::FullyFilled);
    REQUIRE(trades.size() == 2);
    REQUIRE(trades[0].price == 100);  // best bid filled first
    REQUIRE(trades[0].quantity == 3);
    REQUIRE(trades[1].price == 99);
    REQUIRE(trades[1].quantity == 2);
    REQUIRE(book.best_bid() == 99);
    REQUIRE(book.total_at(lob::Side::Buy, 99) == 3);
}

TEST_CASE("Market order walks the book and drops any remainder", "[match]") {
    auto book = make_book();
    std::vector<lob::Trade> trades;
    VecSink sink{&trades};
    book.submit(1, lob::Side::Sell, lob::OrderType::Limit, lob::TimeInForce::GTC, 105, 2, 1, sink);
    book.submit(2, lob::Side::Sell, lob::OrderType::Limit, lob::TimeInForce::GTC, 106, 2, 2, sink);
    auto r = book.submit(99, lob::Side::Buy, lob::OrderType::Market,
                         lob::TimeInForce::IOC, 0, 10, 3, sink);
    REQUIRE(r.filled_qty == 4);
    REQUIRE(r.remaining_qty == 6);
    REQUIRE(r.status == lob::OrderStatus::PartiallyFilled);
    REQUIRE(book.best_ask() == lob::kInvalidPrice);
}
