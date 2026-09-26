#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace lob {

enum class Side : uint8_t { Buy, Sell };

enum class OrderStatus : uint8_t { New, PartiallyFilled, Filled, Cancelled };

// A single resting or incoming limit order.
struct Order {
    uint64_t id;
    Side side;
    double price;
    uint32_t quantity;          // original quantity
    uint32_t remaining;         // remaining (unfilled) quantity
    uint64_t sequence;          // monotonically increasing -> time priority within a price level
    OrderStatus status;

    Order(uint64_t id_, Side side_, double price_, uint32_t qty_, uint64_t seq_)
        : id(id_), side(side_), price(price_), quantity(qty_), remaining(qty_),
          sequence(seq_), status(OrderStatus::New) {}
};

// A single match between an incoming (aggressor/taker) order and a resting (maker) order.
struct Trade {
    uint64_t taker_order_id;
    uint64_t maker_order_id;
    double price;       // trades execute at the resting (maker) order's price
    uint32_t quantity;
    uint64_t sequence;   // trade sequence number, for ordering/printing

    std::string toString() const;
};

// Result of submitting a new order: any resulting trades, plus what happened
// to the order itself (fully filled / partially filled / resting / etc).
struct AddOrderResult {
    uint64_t order_id;
    uint32_t filled_quantity = 0;
    uint32_t remaining_quantity = 0;
    OrderStatus final_status = OrderStatus::New;
    std::vector<Trade> trades;
};

} // namespace lob
