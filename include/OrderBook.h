#pragma once
#include "Order.h"
#include <map>
#include <list>
#include <unordered_map>
#include <functional>
#include <optional>
#include <ostream>

namespace lob {

// ---------------------------------------------------------------------------
// Design notes
// ---------------------------------------------------------------------------
// Price levels:
//   bids_: std::map<price, std::list<Order>, std::greater<double>>   (best bid = begin())
//   asks_: std::map<price, std::list<Order>, std::less<double>>      (best ask = begin())
//
//   A std::map is a balanced tree, so:
//     - inserting a brand new price level costs O(log P) where P = number of
//       distinct price levels on that side.
//     - the list at each level preserves FIFO (time priority) order; pushing
//       a new order to the back of a level's list is O(1).
//   => order insertion is O(log P), i.e. O(log N) in the usual notation.
//
// Order index (the "hash map" for O(1) lookups):
//   unordered_map<order_id, OrderHandle> order_index_
//   where OrderHandle stores the side, the price level, and a
//   std::list<Order>::iterator pointing directly at the order's node.
//
//   Because std::list iterators are never invalidated by insertion/erasure
//   elsewhere in the list, this gives:
//     - O(1) average lookup of any order by id
//     - O(1) removal of the order from its list once located
//     - the only non-O(1) part of a cancel is erasing an *empty* price level
//       from the map, which is O(log P) -> overall cancel is O(log N).
// ---------------------------------------------------------------------------

struct OrderHandle {
    Side side;
    double price;
    std::list<Order>::iterator it;
};

class OrderBook {
public:
    explicit OrderBook(std::string symbol = "SYMBOL");

    // Submits a new limit order. It first attempts to match against the
    // opposite side of the book (price-time priority); any unfilled
    // remainder rests on the book. Returns fills + final order state.
    AddOrderResult addLimitOrder(Side side, double price, uint32_t quantity);

    // Cancels a resting order by id. Returns true if it was found & cancelled.
    bool cancelOrder(uint64_t order_id);

    // Modifies a resting order's quantity and/or price.
    //   - Quantity-only decrease with unchanged price keeps time priority
    //     (matches real exchange behavior).
    //   - Any price change, or a quantity *increase*, loses time priority:
    //     implemented as cancel + re-add (re-enters the matching engine, so
    //     it may trade immediately against the book).
    // Returns the AddOrderResult of the effective re-add when priority is
    // lost, or std::nullopt if the order id was not found.
    std::optional<AddOrderResult> modifyOrder(uint64_t order_id, double new_price, uint32_t new_quantity);

    // O(1) average lookups / stats.
    bool hasOrder(uint64_t order_id) const;
    size_t orderCount() const { return order_index_.size(); }
    size_t priceLevelCount() const { return bids_.size() + asks_.size(); }

    std::optional<double> bestBid() const;
    std::optional<double> bestAsk() const;
    std::optional<double> spread() const;

    uint64_t totalTrades() const { return trade_sequence_; }

    // Debug / demo helper: prints the top `depth` levels of each side.
    void printBook(std::ostream& os, size_t depth = 5) const;

    const std::vector<Trade>& tradeLog() const { return trade_log_; }

private:
    template <typename Compare>
    using PriceLevels = std::map<double, std::list<Order>, Compare>;

    std::string symbol_;
    PriceLevels<std::greater<double>> bids_;   // best bid first
    PriceLevels<std::less<double>>    asks_;   // best ask first

    std::unordered_map<uint64_t, OrderHandle> order_index_;

    uint64_t next_order_id_ = 1;
    uint64_t sequence_counter_ = 0;
    uint64_t trade_sequence_ = 0;
    std::vector<Trade> trade_log_;

    // Matches `incoming` against the resting side (bids_ if incoming is Sell,
    // asks_ if incoming is Buy), consuming resting liquidity while prices
    // cross. Appends any resulting Trades to `out_trades` and to trade_log_.
    void match(Order& incoming, std::vector<Trade>& out_trades);

    void insertResting(Order order);
    void eraseFromIndexAndLevel(uint64_t order_id);

    static bool crosses(Side incoming_side, double incoming_price, double resting_price);

public:
    uint64_t nextId() { return next_order_id_++; }
};

} // namespace lob
