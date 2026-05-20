#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <thread>
#include <vector>

#include "lob/spsc_ring.hpp"

TEST_CASE("SpscRing single-threaded push/pop", "[spsc]") {
    lob::SpscRing<int> ring(4);
    int got = 0;
    REQUIRE_FALSE(ring.try_pop(got));
    REQUIRE(ring.try_push(1));
    REQUIRE(ring.try_push(2));
    REQUIRE(ring.try_push(3));
    REQUIRE(ring.try_push(4));        // capacity = 4, all four slots usable
    REQUIRE_FALSE(ring.try_push(5));  // full
    REQUIRE(ring.try_pop(got)); REQUIRE(got == 1);
    REQUIRE(ring.try_pop(got)); REQUIRE(got == 2);
    REQUIRE(ring.try_pop(got)); REQUIRE(got == 3);
    REQUIRE(ring.try_pop(got)); REQUIRE(got == 4);
    REQUIRE_FALSE(ring.try_pop(got));
}

TEST_CASE("SpscRing producer/consumer round-trip", "[spsc][threaded]") {
    constexpr int N = 100'000;
    lob::SpscRing<int> ring(1024);
    std::atomic<bool> done{false};
    std::vector<int> received;
    received.reserve(N);

    std::thread consumer([&] {
        int v;
        while (received.size() < static_cast<std::size_t>(N)) {
            if (ring.try_pop(v)) received.push_back(v);
        }
        done.store(true, std::memory_order_release);
    });

    for (int i = 0; i < N; ++i) {
        while (!ring.try_push(i)) { /* spin */ }
    }
    consumer.join();

    REQUIRE(received.size() == static_cast<std::size_t>(N));
    for (int i = 0; i < N; ++i) REQUIRE(received[i] == i);
}
