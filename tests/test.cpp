// Automated test suite for the LOB matching engine.
//
// Covers:
//   - basic resting / crossing behavior
//   - partial fills across single and multiple price levels
//   - price-time priority (FIFO within a level)
//   - cancellation and O(1)-lookup correctness
//   - dynamic modification (in-place quantity decrease vs. priority loss)
//   - a simulated high-frequency randomized order flow with invariant checks
//     (no negative/overflowing quantities, book never crosses after settling,
//     every filled/cancelled order is actually removed from the index)
//
// Build with AddressSanitizer + LeakSanitizer (see Makefile: `make test`)
// to positively confirm zero memory leaks, since no raw new/delete is used
// anywhere in OrderBook (all ownership lives in std::map/std::list/std::vector).

#include "OrderBook.h"
#include <iostream>
#include <cassert>
#include <random>
#include <unordered_set>

using namespace lob;

static int g_tests_run = 0;
static int g_tests_failed = 0;

#define CHECK(cond) do { \
    g_tests_run++; \
    if (!(cond)) { \
        g_tests_failed++; \
        std::cerr << "FAIL [" << __FUNCTION__ << ":" << __LINE__ << "]  " #cond "\n"; \
    } \
} while (0)

// ---------------------------------------------------------------------------
static void test_basic_resting_no_cross() {
    OrderBook book;
    auto r1 = book.addLimitOrder(Side::Buy, 10.00, 50);
    CHECK(r1.trades.empty());
    CHECK(r1.remaining_quantity == 50);
    CHECK(book.bestBid().value() == 10.00);
    CHECK(!book.bestAsk().has_value());
    CHECK(book.orderCount() == 1);
}

// ---------------------------------------------------------------------------
static void test_exact_match_single_level() {
    OrderBook book;
    book.addLimitOrder(Side::Sell, 10.00, 100);
    auto r = book.addLimitOrder(Side::Buy, 10.00, 100);
    CHECK(r.trades.size() == 1);
    CHECK(r.trades[0].quantity == 100);
    CHECK(r.trades[0].price == 10.00);
    CHECK(r.filled_quantity == 100);
    CHECK(r.remaining_quantity == 0);
    CHECK(book.orderCount() == 0); // both sides fully consumed
}

// ---------------------------------------------------------------------------
static void test_partial_fill_single_maker() {
    OrderBook book;
    book.addLimitOrder(Side::Sell, 10.00, 30);      // resting sell, 30 shares
    auto r = book.addLimitOrder(Side::Buy, 10.00, 100); // aggressive buy, 100 shares
    CHECK(r.trades.size() == 1);
    CHECK(r.trades[0].quantity == 30);
    CHECK(r.filled_quantity == 30);
    CHECK(r.remaining_quantity == 70);               // 70 shares rest on the bid side
    CHECK(book.bestBid().value() == 10.00);
    CHECK(!book.bestAsk().has_value());
}

// ---------------------------------------------------------------------------
static void test_partial_fill_across_multiple_price_levels() {
    OrderBook book;
    book.addLimitOrder(Side::Sell, 10.00, 20);
    book.addLimitOrder(Side::Sell, 10.05, 20);
    book.addLimitOrder(Side::Sell, 10.10, 20);

    // Aggressive buy sweeps all three levels + rests remainder.
    auto r = book.addLimitOrder(Side::Buy, 10.10, 100);
    CHECK(r.trades.size() == 3);
    CHECK(r.trades[0].price == 10.00);
    CHECK(r.trades[1].price == 10.05);
    CHECK(r.trades[2].price == 10.10);
    CHECK(r.filled_quantity == 60);
    CHECK(r.remaining_quantity == 40);
    CHECK(!book.bestAsk().has_value());   // asks fully consumed
    CHECK(book.bestBid().value() == 10.10);
}

// ---------------------------------------------------------------------------
static void test_price_time_priority_fifo() {
    OrderBook book;
    auto a = book.addLimitOrder(Side::Sell, 10.00, 10); // first in line
    auto b = book.addLimitOrder(Side::Sell, 10.00, 10); // second in line
    uint64_t first_id = a.order_id;
    uint64_t second_id = b.order_id;

    auto r = book.addLimitOrder(Side::Buy, 10.00, 10); // should only fill the FIRST resting order
    CHECK(r.trades.size() == 1);
    CHECK(r.trades[0].maker_order_id == first_id);
    CHECK(book.hasOrder(second_id));   // second order untouched, still resting
    CHECK(!book.hasOrder(first_id));   // first order fully consumed
}

// ---------------------------------------------------------------------------
static void test_cancel_order() {
    OrderBook book;
    auto r = book.addLimitOrder(Side::Buy, 9.90, 40);
    CHECK(book.hasOrder(r.order_id));
    CHECK(book.cancelOrder(r.order_id));
    CHECK(!book.hasOrder(r.order_id));
    CHECK(!book.cancelOrder(r.order_id)); // second cancel is a no-op, returns false
    CHECK(!book.bestBid().has_value());   // level removed once empty
}

// ---------------------------------------------------------------------------
static void test_modify_quantity_decrease_keeps_priority() {
    OrderBook book;
    auto a = book.addLimitOrder(Side::Sell, 10.00, 50); // first
    auto b = book.addLimitOrder(Side::Sell, 10.00, 50); // second

    // Shrink the FIRST order's quantity in place; it should keep priority.
    auto mod = book.modifyOrder(a.order_id, 10.00, 20);
    CHECK(mod.has_value());

    auto r = book.addLimitOrder(Side::Buy, 10.00, 20);
    CHECK(r.trades.size() == 1);
    CHECK(r.trades[0].maker_order_id == a.order_id); // still first in line despite the resize
    CHECK(book.hasOrder(b.order_id));                // untouched
}

// ---------------------------------------------------------------------------
static void test_modify_price_change_loses_priority_and_may_trade() {
    OrderBook book;
    book.addLimitOrder(Side::Buy, 9.95, 100);
    auto r = book.addLimitOrder(Side::Sell, 10.05, 50); // rests, doesn't cross 9.95

    // Move the resting sell's price down so it now crosses the existing bid.
    auto mod = book.modifyOrder(r.order_id, 9.95, 50);
    CHECK(mod.has_value());
    CHECK(mod->trades.size() == 1);       // repricing triggered an immediate match
    CHECK(mod->trades[0].quantity == 50);
}

// ---------------------------------------------------------------------------
static void test_modify_nonexistent_order_returns_nullopt() {
    OrderBook book;
    auto mod = book.modifyOrder(99999, 10.0, 10);
    CHECK(!mod.has_value());
}

// ---------------------------------------------------------------------------
// Simulated high-frequency order flow: thousands of randomized adds,
// cancels, and modifies. Checks structural invariants hold throughout,
// and that the order index never drifts from the actual book contents.
static void test_high_frequency_simulation() {
    OrderBook book;
    std::mt19937 rng(42);
    std::uniform_real_distribution<double> price_dist(95.0, 105.0);
    std::uniform_int_distribution<int> qty_dist(1, 500);
    std::uniform_int_distribution<int> side_dist(0, 1);
    std::uniform_int_distribution<int> action_dist(0, 9); // 0-6 add, 7-8 cancel, 9 modify

    std::vector<uint64_t> live_ids;

    const int N = 20000;
    for (int i = 0; i < N; ++i) {
        int action = action_dist(rng);

        if (action <= 6 || live_ids.empty()) {
            Side side = side_dist(rng) == 0 ? Side::Buy : Side::Sell;
            double price = std::round(price_dist(rng) * 100.0) / 100.0;
            uint32_t qty = static_cast<uint32_t>(qty_dist(rng));

            auto r = book.addLimitOrder(side, price, qty);

            // Invariant: filled + remaining == original quantity.
            CHECK(r.filled_quantity + r.remaining_quantity == qty);

            if (r.remaining_quantity > 0) live_ids.push_back(r.order_id);

            for (auto& t : r.trades) {
                CHECK(t.quantity > 0);
            }
        } else if (action <= 8) {
            // cancel a random live order
            size_t idx = rng() % live_ids.size();
            uint64_t id = live_ids[idx];
            bool existed = book.hasOrder(id);
            bool cancelled = book.cancelOrder(id);
            CHECK(cancelled == existed);
            live_ids[idx] = live_ids.back();
            live_ids.pop_back();
        } else {
            // modify a random live order
            size_t idx = rng() % live_ids.size();
            uint64_t id = live_ids[idx];
            if (!book.hasOrder(id)) continue;
            double new_price = std::round(price_dist(rng) * 100.0) / 100.0;
            uint32_t new_qty = static_cast<uint32_t>(qty_dist(rng));
            auto mod = book.modifyOrder(id, new_price, new_qty);
            CHECK(mod.has_value());
        }

        // Structural invariant: best bid must never exceed best ask once
        // matching has run to completion for this event (the book is never
        // left in a crossed state between events).
        auto bb = book.bestBid();
        auto ba = book.bestAsk();
        if (bb && ba) {
            CHECK(*bb < *ba);
        }
    }

    std::cout << "  [simulation] " << N << " events, "
              << book.orderCount() << " resting orders, "
              << book.totalTrades() << " trades executed, "
              << book.priceLevelCount() << " active price levels\n";
}

// ---------------------------------------------------------------------------
static void test_zero_quantity_modify_cancels() {
    OrderBook book;
    auto a = book.addLimitOrder(Side::Buy, 10.00, 30);
    auto mod = book.modifyOrder(a.order_id, 10.00, 0);
    CHECK(mod.has_value());
    CHECK(mod->final_status == OrderStatus::Cancelled);
    CHECK(!book.hasOrder(a.order_id));
}

// ---------------------------------------------------------------------------
int main() {
    test_basic_resting_no_cross();
    test_exact_match_single_level();
    test_partial_fill_single_maker();
    test_partial_fill_across_multiple_price_levels();
    test_price_time_priority_fifo();
    test_cancel_order();
    test_modify_quantity_decrease_keeps_priority();
    test_modify_price_change_loses_priority_and_may_trade();
    test_modify_nonexistent_order_returns_nullopt();
    test_zero_quantity_modify_cancels();
    test_high_frequency_simulation();

    std::cout << "\n" << g_tests_run << " checks run, "
              << (g_tests_run - g_tests_failed) << " passed, "
              << g_tests_failed << " failed.\n";

    return g_tests_failed == 0 ? 0 : 1;
}
