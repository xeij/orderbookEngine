#include <catch2/catch_test_macros.hpp>

#include <unordered_set>

#include "lob/slab_allocator.hpp"

namespace {
struct Widget {
    std::uint64_t a;
    std::uint64_t b;
};
}  // namespace

TEST_CASE("SlabAllocator hands out unique slots up to capacity", "[slab]") {
    lob::SlabAllocator<Widget> slab(8);
    std::unordered_set<Widget*> seen;
    for (int i = 0; i < 8; ++i) {
        Widget* w = slab.allocate();
        REQUIRE(w != nullptr);
        REQUIRE(seen.insert(w).second);
    }
    REQUIRE(slab.allocate() == nullptr);  // exhausted
    REQUIRE(slab.in_use() == 8);
}

TEST_CASE("SlabAllocator recycles freed slots", "[slab]") {
    lob::SlabAllocator<Widget> slab(4);
    Widget* a = slab.allocate();
    Widget* b = slab.allocate();
    Widget* c = slab.allocate();
    Widget* d = slab.allocate();
    REQUIRE(slab.allocate() == nullptr);
    slab.deallocate(b);
    REQUIRE(slab.in_use() == 3);
    Widget* e = slab.allocate();
    REQUIRE(e == b);  // LIFO free list
    (void)a; (void)c; (void)d;
}
