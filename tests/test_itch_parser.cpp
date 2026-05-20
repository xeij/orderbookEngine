#include <catch2/catch_test_macros.hpp>

#include <cstring>
#include <vector>

#include "lob/itch.hpp"

namespace {

// Helper: pack a value big-endian into a byte vector.
void push_be16(std::vector<std::uint8_t>& buf, std::uint16_t v) {
    buf.push_back(static_cast<std::uint8_t>(v >> 8));
    buf.push_back(static_cast<std::uint8_t>(v));
}
void push_be32(std::vector<std::uint8_t>& buf, std::uint32_t v) {
    buf.push_back(static_cast<std::uint8_t>(v >> 24));
    buf.push_back(static_cast<std::uint8_t>(v >> 16));
    buf.push_back(static_cast<std::uint8_t>(v >> 8));
    buf.push_back(static_cast<std::uint8_t>(v));
}
void push_be48(std::vector<std::uint8_t>& buf, std::uint64_t v) {
    buf.push_back(static_cast<std::uint8_t>(v >> 40));
    buf.push_back(static_cast<std::uint8_t>(v >> 32));
    buf.push_back(static_cast<std::uint8_t>(v >> 24));
    buf.push_back(static_cast<std::uint8_t>(v >> 16));
    buf.push_back(static_cast<std::uint8_t>(v >> 8));
    buf.push_back(static_cast<std::uint8_t>(v));
}
void push_be64(std::vector<std::uint8_t>& buf, std::uint64_t v) {
    push_be32(buf, static_cast<std::uint32_t>(v >> 32));
    push_be32(buf, static_cast<std::uint32_t>(v));
}

struct RecordingHandler : lob::itch::ItchHandler {
    std::vector<lob::itch::AddOrder>      adds;
    std::vector<lob::itch::OrderExecuted> execs;
    std::vector<lob::itch::OrderDelete>   deletes;
    std::vector<lob::itch::OrderReplace>  replaces;

    void on_add_order(const lob::itch::AddOrder& a) override            { adds.push_back(a); }
    void on_order_executed(const lob::itch::OrderExecuted& e) override  { execs.push_back(e); }
    void on_order_delete(const lob::itch::OrderDelete& d) override      { deletes.push_back(d); }
    void on_order_replace(const lob::itch::OrderReplace& r) override    { replaces.push_back(r); }
};

}  // namespace

TEST_CASE("ITCH parser dispatches AddOrder fields correctly", "[itch]") {
    std::vector<std::uint8_t> buf;
    push_be16(buf, 36);                  // frame length
    buf.push_back('A');                  // type
    push_be16(buf, 0x1234);              // stock_locate
    push_be16(buf, 0x0001);              // tracking
    push_be48(buf, 0x000102030405ULL);   // timestamp
    push_be64(buf, 0xDEADBEEFCAFEBABEULL);// order_ref
    buf.push_back('B');                  // side
    push_be32(buf, 100);                 // shares
    for (char c : std::string("AAPL    ")) buf.push_back(static_cast<std::uint8_t>(c));
    push_be32(buf, 1500000);             // $150.0000

    RecordingHandler h;
    auto n = lob::itch::parse(buf.data(), buf.size(), h);
    REQUIRE(n == 1);
    REQUIRE(h.adds.size() == 1);
    const auto& a = h.adds[0];
    REQUIRE(a.stock_locate == 0x1234);
    REQUIRE(a.ts_ns        == 0x000102030405ULL);
    REQUIRE(a.order_ref    == 0xDEADBEEFCAFEBABEULL);
    REQUIRE(a.side         == lob::Side::Buy);
    REQUIRE(a.shares       == 100);
    REQUIRE(a.price        == 1500000);
    REQUIRE(std::string(a.stock.data(), 4) == "AAPL");
}

TEST_CASE("ITCH parser handles multiple framed messages and stops on truncation", "[itch]") {
    std::vector<std::uint8_t> buf;

    // OrderDelete (19 bytes body)
    push_be16(buf, 19);
    buf.push_back('D');
    push_be16(buf, 1); push_be16(buf, 0);
    push_be48(buf, 42);
    push_be64(buf, 7);

    // Truncated AddOrder (announces 36, only provides 10)
    push_be16(buf, 36);
    buf.push_back('A');
    for (int i = 0; i < 9; ++i) buf.push_back(0);

    RecordingHandler h;
    auto n = lob::itch::parse(buf.data(), buf.size(), h);
    REQUIRE(n == 1);
    REQUIRE(h.deletes.size() == 1);
    REQUIRE(h.deletes[0].order_ref == 7);
    REQUIRE(h.adds.empty());
}
