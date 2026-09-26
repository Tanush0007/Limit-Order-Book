#include "OrderBook.h"
#include <iostream>

using namespace lob;

static void printResult(const char* label, const AddOrderResult& r) {
    std::cout << label << " order #" << r.order_id
              << " | filled=" << r.filled_quantity
              << " remaining=" << r.remaining_quantity
              << " trades=" << r.trades.size() << "\n";
    for (const auto& t : r.trades) std::cout << "    " << t.toString() << "\n";
}

int main() {
    OrderBook book("DEMO");

    // Build up resting liquidity on both sides.
    printResult("BUY ", book.addLimitOrder(Side::Buy, 99.50, 100));
    printResult("BUY ", book.addLimitOrder(Side::Buy, 99.75, 50));
    printResult("SELL", book.addLimitOrder(Side::Sell, 100.25, 75));
    printResult("SELL", book.addLimitOrder(Side::Sell, 100.50, 200));

    book.printBook(std::cout);

    // Aggressive buy that crosses the spread and partially fills two levels.
    auto r = book.addLimitOrder(Side::Buy, 100.50, 220);
    printResult("BUY (aggressive)", r);

    book.printBook(std::cout);

    std::cout << "Best bid: " << book.bestBid().value_or(-1)
              << "  Best ask: " << book.bestAsk().value_or(-1) << "\n";

    // Cancel one of the remaining resting orders.
    bool cancelled = book.cancelOrder(2);
    std::cout << "Cancel order #2: " << (cancelled ? "ok" : "not found") << "\n";

    book.printBook(std::cout);

    std::cout << "Total trades executed: " << book.totalTrades() << "\n";
    return 0;
}
