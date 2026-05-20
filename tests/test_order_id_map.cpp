#include <catch2/catch_test_macros.hpp>

#include <random>
#include <unordered_map>

#include "lob/order_id_map.hpp"

namespace {
struct Dummy { int x; };
}  // namespace

TEST_CASE("OrderIdMap basic insert/find/erase", "[id_map]") {
    lob::OrderIdMap<Dummy> map(32);
    Dummy a{1}, b{2}, c{3};
    REQUIRE(map.insert(10, &a));
    REQUIRE(map.insert(20, &b));
    REQUIRE(map.insert(30, &c));
    REQUIRE_FALSE(map.insert(20, &c));  // duplicate

    REQUIRE(map.find(10) == &a);
    REQUIRE(map.find(20) == &b);
    REQUIRE(map.find(40) == nullptr);

    REQUIRE(map.erase(20));
    REQUIRE_FALSE(map.erase(20));
    REQUIRE(map.find(20) == nullptr);
    REQUIRE(map.find(10) == &a);
    REQUIRE(map.find(30) == &c);
}

TEST_CASE("OrderIdMap stress vs std::unordered_map", "[id_map][stress]") {
    lob::OrderIdMap<Dummy> map(1u << 14);
    std::unordered_map<lob::OrderId, Dummy*> ref;
    std::mt19937_64 rng(42);

    std::vector<Dummy> arena(8192);
    for (int op = 0; op < 100'000; ++op) {
        lob::OrderId id = (rng() & 0xFFFFu) + 1u;  // never zero
        int action = static_cast<int>(rng() % 3);
        if (action == 0) {
            auto* slot = &arena[rng() % arena.size()];
            bool ok    = map.insert(id, slot);
            bool ref_ok = ref.emplace(id, slot).second;
            REQUIRE(ok == ref_ok);
        } else if (action == 1) {
            auto* got = map.find(id);
            auto  it  = ref.find(id);
            if (it == ref.end()) {
                REQUIRE(got == nullptr);
            } else {
                REQUIRE(got == it->second);
            }
        } else {
            bool ok     = map.erase(id);
            bool ref_ok = ref.erase(id) > 0;
            REQUIRE(ok == ref_ok);
        }
    }
}
