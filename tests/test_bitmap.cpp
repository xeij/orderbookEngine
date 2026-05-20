#include <catch2/catch_test_macros.hpp>

#include "lob/bitmap.hpp"

TEST_CASE("Bitmap set/clear/test", "[bitmap]") {
    lob::Bitmap bm(200);
    REQUIRE_FALSE(bm.test(0));
    bm.set(0);
    bm.set(63);
    bm.set(64);
    bm.set(199);
    REQUIRE(bm.test(0));
    REQUIRE(bm.test(63));
    REQUIRE(bm.test(64));
    REQUIRE(bm.test(199));
    REQUIRE_FALSE(bm.test(65));

    bm.clear(63);
    REQUIRE_FALSE(bm.test(63));
}

TEST_CASE("Bitmap find_next_set across word boundaries", "[bitmap]") {
    lob::Bitmap bm(256);
    bm.set(70);
    bm.set(130);
    bm.set(255);

    REQUIRE(bm.find_next_set(0)   == 70);
    REQUIRE(bm.find_next_set(70)  == 70);
    REQUIRE(bm.find_next_set(71)  == 130);
    REQUIRE(bm.find_next_set(131) == 255);
    REQUIRE(bm.find_next_set(256) == lob::Bitmap::kNotFound);
}

TEST_CASE("Bitmap find_prev_set across word boundaries", "[bitmap]") {
    lob::Bitmap bm(256);
    bm.set(5);
    bm.set(70);
    bm.set(130);

    REQUIRE(bm.find_prev_set(255) == 130);
    REQUIRE(bm.find_prev_set(130) == 130);
    REQUIRE(bm.find_prev_set(129) == 70);
    REQUIRE(bm.find_prev_set(69)  == 5);
    REQUIRE(bm.find_prev_set(4)   == lob::Bitmap::kNotFound);
}
