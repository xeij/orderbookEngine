#include <catch2/catch_test_macros.hpp>

#include <vector>

#include "lob/order_book.hpp"

namespace {

struct VecSink {
    std::vector<lob::Trade>* out;
    void operator()(const lob::Trade& t) const { out->push_back(t); }
};

lob::OrderBook make_book() {
    return lob::OrderBook{lob::OrderBook::Config{0, 4096, 1024}};
}

}  // namespace

TEST_CASE("IOC fully fills against sufficient liquidity", "[tif][ioc]") {
    auto book = make_book();
    std::vector<lob::Trade> trades;
    VecSink sink{&trades};
    book.submit(1, lob::Side::Sell, lob::OrderType::Limit, lob::TimeInForce::GTC, 105, 10, 1, sink);
    auto r = book.submit(2, lob::Side::Buy, lob::OrderType::Limit, lob::TimeInForce::IOC, 110, 5, 2, sink);
    REQUIRE(r.status == lob::OrderStatus::FullyFilled);
    REQUIRE(r.filled_qty == 5);
}

TEST_CASE("IOC partial fill cancels the remainder", "[tif][ioc]") {
    auto book = make_book();
    std::vector<lob::Trade> trades;
    VecSink sink{&trades};
    book.submit(1, lob::Side::Sell, lob::OrderType::Limit, lob::TimeInForce::GTC, 105, 3, 1, sink);
    auto r = book.submit(2, lob::Side::Buy, lob::OrderType::Limit, lob::TimeInForce::IOC, 110, 5, 2, sink);
    REQUIRE(r.status == lob::OrderStatus::PartiallyFilled);
    REQUIRE(r.filled_qty == 3);
    REQUIRE(r.remaining_qty == 2);
    REQUIRE(book.best_bid() == lob::kInvalidPrice);  // remainder not rested
}

TEST_CASE("IOC with no available liquidity cancels with no trades", "[tif][ioc]") {
    auto book = make_book();
    std::vector<lob::Trade> trades;
    VecSink sink{&trades};
    auto r = book.submit(2, lob::Side::Buy, lob::OrderType::Limit, lob::TimeInForce::IOC, 110, 5, 1, sink);
    REQUIRE(r.status == lob::OrderStatus::Cancelled);
    REQUIRE(trades.empty());
}

TEST_CASE("FOK fully fills when liquidity is sufficient", "[tif][fok]") {
    auto book = make_book();
    std::vector<lob::Trade> trades;
    VecSink sink{&trades};
    book.submit(1, lob::Side::Sell, lob::OrderType::Limit, lob::TimeInForce::GTC, 105, 3, 1, sink);
    book.submit(2, lob::Side::Sell, lob::OrderType::Limit, lob::TimeInForce::GTC, 106, 5, 2, sink);
    auto r = book.submit(3, lob::Side::Buy, lob::OrderType::Limit, lob::TimeInForce::FOK, 106, 7, 3, sink);
    REQUIRE(r.status == lob::OrderStatus::FullyFilled);
    REQUIRE(trades.size() == 2);
}

TEST_CASE("FOK rejects without any fills if liquidity is short", "[tif][fok]") {
    auto book = make_book();
    std::vector<lob::Trade> trades;
    VecSink sink{&trades};
    book.submit(1, lob::Side::Sell, lob::OrderType::Limit, lob::TimeInForce::GTC, 105, 3, 1, sink);
    auto r = book.submit(2, lob::Side::Buy, lob::OrderType::Limit, lob::TimeInForce::FOK, 110, 5, 2, sink);
    REQUIRE(r.status == lob::OrderStatus::Cancelled);
    REQUIRE(trades.empty());                // crucially: no partial fill
    REQUIRE(book.total_at(lob::Side::Sell, 105) == 3);  // resting unchanged
}

TEST_CASE("POST_ONLY rests when it would not cross", "[tif][post]") {
    auto book = make_book();
    std::vector<lob::Trade> trades;
    VecSink sink{&trades};
    book.submit(1, lob::Side::Sell, lob::OrderType::Limit, lob::TimeInForce::GTC, 105, 3, 1, sink);
    auto r = book.submit(2, lob::Side::Buy, lob::OrderType::Limit, lob::TimeInForce::PostOnly, 104, 5, 2, sink);
    REQUIRE(r.status == lob::OrderStatus::Accepted);
    REQUIRE(book.best_bid() == 104);
}

TEST_CASE("POST_ONLY is rejected if it would cross", "[tif][post]") {
    auto book = make_book();
    std::vector<lob::Trade> trades;
    VecSink sink{&trades};
    book.submit(1, lob::Side::Sell, lob::OrderType::Limit, lob::TimeInForce::GTC, 105, 3, 1, sink);
    auto r = book.submit(2, lob::Side::Buy, lob::OrderType::Limit, lob::TimeInForce::PostOnly, 106, 5, 2, sink);
    REQUIRE(r.status == lob::OrderStatus::Cancelled);
    REQUIRE(trades.empty());
    REQUIRE(book.total_at(lob::Side::Sell, 105) == 3);
}
