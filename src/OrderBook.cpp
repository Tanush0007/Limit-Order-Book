#include "OrderBook.h"
#include <sstream>
#include <iomanip>
#include <cassert>

namespace lob {

std::string Trade::toString() const {
    std::ostringstream oss;
    oss << "TRADE #" << sequence << "  taker=" << taker_order_id
        << " maker=" << maker_order_id << " price=" << price
        << " qty=" << quantity;
    return oss.str();
}

OrderBook::OrderBook(std::string symbol) : symbol_(std::move(symbol)) {}

bool OrderBook::crosses(Side incoming_side, double incoming_price, double resting_price) {
    // A buy crosses (matches) any ask at or below its limit price.
    // A sell crosses (matches) any bid at or above its limit price.
    return incoming_side == Side::Buy ? incoming_price >= resting_price
                                       : incoming_price <= resting_price;
}

void OrderBook::eraseFromIndexAndLevel(uint64_t order_id) {
    auto idx_it = order_index_.find(order_id);
    if (idx_it == order_index_.end()) return;

    const OrderHandle& h = idx_it->second;
    if (h.side == Side::Buy) {
        auto level_it = bids_.find(h.price);
        assert(level_it != bids_.end());
        level_it->second.erase(h.it);
        if (level_it->second.empty()) bids_.erase(level_it);
    } else {
        auto level_it = asks_.find(h.price);
        assert(level_it != asks_.end());
        level_it->second.erase(h.it);
        if (level_it->second.empty()) asks_.erase(level_it);
    }
    order_index_.erase(idx_it);
}

void OrderBook::insertResting(Order order) {
    const uint64_t id = order.id;
    const Side side = order.side;
    const double price = order.price;

    if (side == Side::Buy) {
        auto& level = bids_[price];               // O(log P) if new level
        level.push_back(std::move(order));        // O(1)
        auto it = std::prev(level.end());
        order_index_[id] = OrderHandle{side, price, it};
    } else {
        auto& level = asks_[price];
        level.push_back(std::move(order));
        auto it = std::prev(level.end());
        order_index_[id] = OrderHandle{side, price, it};
    }
}

void OrderBook::match(Order& incoming, std::vector<Trade>& out_trades) {
    // Incoming Buy matches against asks_ (lowest price first);
    // incoming Sell matches against bids_ (highest price first).
    if (incoming.side == Side::Buy) {
        while (incoming.remaining > 0 && !asks_.empty()) {
            auto level_it = asks_.begin();               // best ask
            double level_price = level_it->first;
            if (!crosses(incoming.side, incoming.price, level_price)) break;

            auto& level_orders = level_it->second;
            while (incoming.remaining > 0 && !level_orders.empty()) {
                Order& resting = level_orders.front();    // oldest at this price = time priority
                uint32_t traded_qty = std::min(incoming.remaining, resting.remaining);

                incoming.remaining -= traded_qty;
                resting.remaining  -= traded_qty;

                Trade t{incoming.id, resting.id, level_price, traded_qty, ++trade_sequence_};
                out_trades.push_back(t);
                trade_log_.push_back(t);

                if (resting.remaining == 0) {
                    resting.status = OrderStatus::Filled;
                    order_index_.erase(resting.id);
                    level_orders.pop_front();              // O(1)
                } else {
                    resting.status = OrderStatus::PartiallyFilled;
                }
            }
            if (level_orders.empty()) asks_.erase(level_it); // O(log P)
        }
    } else { // incoming.side == Side::Sell
        while (incoming.remaining > 0 && !bids_.empty()) {
            auto level_it = bids_.begin();               // best bid
            double level_price = level_it->first;
            if (!crosses(incoming.side, incoming.price, level_price)) break;

            auto& level_orders = level_it->second;
            while (incoming.remaining > 0 && !level_orders.empty()) {
                Order& resting = level_orders.front();
                uint32_t traded_qty = std::min(incoming.remaining, resting.remaining);

                incoming.remaining -= traded_qty;
                resting.remaining  -= traded_qty;

                Trade t{incoming.id, resting.id, level_price, traded_qty, ++trade_sequence_};
                out_trades.push_back(t);
                trade_log_.push_back(t);

                if (resting.remaining == 0) {
                    resting.status = OrderStatus::Filled;
                    order_index_.erase(resting.id);
                    level_orders.pop_front();
                } else {
                    resting.status = OrderStatus::PartiallyFilled;
                }
            }
            if (level_orders.empty()) bids_.erase(level_it);
        }
    }
}

AddOrderResult OrderBook::addLimitOrder(Side side, double price, uint32_t quantity) {
    Order incoming(next_order_id_++, side, price, quantity, ++sequence_counter_);

    AddOrderResult result;
    result.order_id = incoming.id;

    match(incoming, result.trades);

    result.filled_quantity = quantity - incoming.remaining;
    result.remaining_quantity = incoming.remaining;

    if (incoming.remaining == 0) {
        result.final_status = OrderStatus::Filled;
    } else {
        result.final_status = (result.filled_quantity > 0) ? OrderStatus::PartiallyFilled
                                                             : OrderStatus::New;
        incoming.status = result.final_status;
        insertResting(std::move(incoming));   // rest the remainder on the book
    }
    return result;
}

bool OrderBook::cancelOrder(uint64_t order_id) {
    if (order_index_.find(order_id) == order_index_.end()) return false;
    eraseFromIndexAndLevel(order_id);
    return true;
}

std::optional<AddOrderResult> OrderBook::modifyOrder(uint64_t order_id, double new_price, uint32_t new_quantity) {
    auto idx_it = order_index_.find(order_id);
    if (idx_it == order_index_.end()) return std::nullopt;

    OrderHandle handle = idx_it->second;
    Order& existing = *handle.it;

    // In-place quantity decrease at the same price: keeps time priority.
    if (new_price == existing.price && new_quantity <= existing.remaining && new_quantity > 0) {
        existing.quantity = new_quantity;   // treat as reducing the resting size
        existing.remaining = new_quantity;
        AddOrderResult r;
        r.order_id = order_id;
        r.remaining_quantity = new_quantity;
        r.final_status = OrderStatus::New;
        return r;
    }

    // Otherwise: price change, or a quantity increase -> loses time priority.
    // Cancel and re-submit as a brand-new order (may trade immediately).
    Side side = existing.side;
    eraseFromIndexAndLevel(order_id);
    if (new_quantity == 0) {
        AddOrderResult r;
        r.order_id = order_id;
        r.final_status = OrderStatus::Cancelled;
        return r;
    }
    return addLimitOrder(side, new_price, new_quantity);
}

bool OrderBook::hasOrder(uint64_t order_id) const {
    return order_index_.find(order_id) != order_index_.end();
}

std::optional<double> OrderBook::bestBid() const {
    if (bids_.empty()) return std::nullopt;
    return bids_.begin()->first;
}

std::optional<double> OrderBook::bestAsk() const {
    if (asks_.empty()) return std::nullopt;
    return asks_.begin()->first;
}

std::optional<double> OrderBook::spread() const {
    auto bb = bestBid();
    auto ba = bestAsk();
    if (!bb || !ba) return std::nullopt;
    return *ba - *bb;
}

void OrderBook::printBook(std::ostream& os, size_t depth) const {
    os << "===== " << symbol_ << " order book =====\n";
    os << std::fixed << std::setprecision(2);

    os << "ASKS (best -> worst):\n";
    size_t count = 0;
    std::vector<std::pair<double, uint32_t>> ask_rows;
    for (auto it = asks_.begin(); it != asks_.end() && count < depth; ++it, ++count) {
        uint32_t total = 0;
        for (const auto& o : it->second) total += o.remaining;
        ask_rows.push_back({it->first, total});
    }
    for (auto rit = ask_rows.rbegin(); rit != ask_rows.rend(); ++rit) {
        os << "  " << std::setw(10) << rit->first << " x " << rit->second << "\n";
    }

    os << "  ------ spread: ";
    auto sp = spread();
    os << (sp ? std::to_string(*sp) : std::string("n/a")) << " ------\n";

    os << "BIDS (best -> worst):\n";
    count = 0;
    for (auto it = bids_.begin(); it != bids_.end() && count < depth; ++it, ++count) {
        uint32_t total = 0;
        for (const auto& o : it->second) total += o.remaining;
        os << "  " << std::setw(10) << it->first << " x " << total << "\n";
    }
    os << "=================================\n";
}

} // namespace lob
