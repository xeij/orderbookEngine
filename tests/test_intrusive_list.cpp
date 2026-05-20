#include <catch2/catch_test_macros.hpp>

#include "lob/intrusive_list.hpp"

namespace {

struct Node {
    Node* prev;
    Node* next;
    int   value;
};

}  // namespace

TEST_CASE("IntrusiveList push_back / unlink preserves FIFO order", "[intrusive_list]") {
    lob::IntrusiveList<Node> list;
    Node a{nullptr, nullptr, 1};
    Node b{nullptr, nullptr, 2};
    Node c{nullptr, nullptr, 3};

    REQUIRE(list.empty());
    list.push_back(&a);
    list.push_back(&b);
    list.push_back(&c);

    REQUIRE(list.head()->value == 1);
    REQUIRE(list.tail()->value == 3);

    list.unlink(&b);  // unlink middle
    REQUIRE(list.head()->value == 1);
    REQUIRE(list.head()->next->value == 3);
    REQUIRE(list.tail()->value == 3);

    list.unlink(&a);  // unlink head
    REQUIRE(list.head()->value == 3);
    REQUIRE(list.tail()->value == 3);

    list.unlink(&c);  // unlink last
    REQUIRE(list.empty());
}

TEST_CASE("IntrusiveList unlink tail then push_back", "[intrusive_list]") {
    lob::IntrusiveList<Node> list;
    Node a{nullptr, nullptr, 1};
    Node b{nullptr, nullptr, 2};

    list.push_back(&a);
    list.push_back(&b);
    list.unlink(&b);
    REQUIRE(list.head() == &a);
    REQUIRE(list.tail() == &a);

    Node c{nullptr, nullptr, 3};
    list.push_back(&c);
    REQUIRE(list.tail() == &c);
    REQUIRE(a.next == &c);
    REQUIRE(c.prev == &a);
}
