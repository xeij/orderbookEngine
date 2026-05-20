// Single translation unit that forces the compiler to type-check the templated
// matching engine even when no consumer in the library happens to instantiate
// it. The NullSink instantiation is also picked up by the benchmark harness so
// it isn't strictly speculative work.
#include "lob/order_book.hpp"

namespace lob::detail {

// Reference the template with a concrete sink so the compiler emits and
// type-checks every code path. Static-storage so it isn't optimised away.
[[maybe_unused]] inline void touch_template() {
    OrderBook book{OrderBook::Config{}};
    NullSink sink;
    (void)book.submit(1, Side::Buy, OrderType::Limit, TimeInForce::GTC,
                      100, 1, 0, sink);
}

}  // namespace lob::detail
