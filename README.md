# Limit Order Book Matching Engine

A price-time-priority limit order book and matching engine written in modern
**C++17**, built around STL containers for O(1) order lookups and O(log N)
insertion/cancellation, with **zero manual memory management** and a
**48,000+ assertion automated test suite** (including a 20,000-event
randomized order-flow simulation) verified leak-free with AddressSanitizer.

This project is a systems-and-data-structures sandbox for exploring how real
exchanges enforce price-time priority, handle partial fills across price
levels, and keep order cancellation/modification fast under load.

---

## Architecture & Core Features

- **O(1) Order Lookups:** An `unordered_map<order_id, iterator>` points
  directly at each order's node, so any order can be found, filled, or
  cancelled without scanning the book.
- **O(log N) Insertion & Cancellation:** Price levels are held in two
  `std::map` red-black trees (bids descending, asks ascending). A new price
  level costs `O(log P)` to insert; an emptied level costs `O(log P)` to
  erase. Everything else at a level is `O(1)` list operations.
- **Strict Price-Time Priority:** Within a price level, orders are matched
  FIFO — oldest order first — exactly like a real exchange, preventing
  crossed books and unfair fills.
- **Partial Fills Across Price Levels:** An aggressive order can sweep
  multiple resting price levels in a single call, partially filling some
  and fully consuming others, with the correct remainder resting back on
  the book.
- **Zero Manual Memory Management:** No raw `new`/`delete` anywhere —
  ownership lives entirely in `std::map`, `std::list`, and `std::vector`,
  so there is nothing to leak by construction.
- **Dynamic Order Modification:** A quantity *decrease* at an unchanged
  price is applied in place and keeps time priority; a price change or
  quantity *increase* is handled as cancel + re-submit, correctly losing
  priority and triggering an immediate match if the new price crosses.

---

## Compilation

No CMake or external dependencies — just a C++17 compiler (developed and
tested against g++ 13):

```
make demo    # builds the interactive walkthrough
make test    # builds with -fsanitize=address,undefined and runs the test suite
make clean   # removes built binaries
```

## Execution Modes

**1. Demo Mode** — builds up a two-sided book, sends an aggressive order
that sweeps two price levels, cancels a resting order, and prints the book
before/after each step.

```
./demo
```

**2. Test Suite Mode** — runs 10 targeted unit tests (partial fills, FIFO
priority, cancel/modify semantics) plus a 20,000-event randomized
high-frequency simulation, all under AddressSanitizer + UndefinedBehavior
Sanitizer to catch leaks or undefined behavior.

```
make test
```

Expected output ends with:

```
[simulation] 20000 events, 1435 resting orders, 11315 trades executed, 455 active price levels

48001 checks run, 48001 passed, 0 failed.
```

---

## Project Structure

```
include/Order.h        Order, Trade, and result data structures
include/OrderBook.h    OrderBook class + complexity design notes
src/OrderBook.cpp      Matching engine implementation
src/main.cpp           Interactive demo
tests/test_orderbook.cpp   Automated test suite
Makefile
```

---

## Development Notes

**1. Day 1 Progress**
- 1. Define Order (id, side, price, quantity, remaining) in Order.h.
- 2. Design OrderBook's storage: two std::map<price, std::list<Order>> trees (bids descending, asks ascending) for price-time priority, plus an unordered_map<order_id, iterator> for O(1) lookup.
- 3. Implement addLimitOrder: match against the opposite side first, rest any remainder.
- 4. Get exact matches and single-maker partial fills working; sanity-check with a few manual scenarios in main.cpp.

**2. Day 2 Progress**
- 1. Implement cancelOrder: O(1) average lookup + unlink, erasing the price level only if it empties out.
  2. Implement modifyOrder: quantity-only decrease at the same price updates in place (keeps time priority); a price change or quantity increase does cancel + re-submit (loses priority, may trade immediately).
  3. Add targeted tests for FIFO ordering within a level and for both modify paths.

**3. Day 3 Progress**
- 1. Write test_orderbook.cpp: unit tests for partial fills across multiple price levels, cancellation edge cases, modify semantics.
  2. Add a large randomized simulation (thousands of mixed add/cancel/modify events) asserting the book never ends up crossed and the order index never drifts from actual book contents.
  3. Build with AddressSanitizer + UBSan to confirm zero leaks — straightforward here since ownership lives entirely in STL containers, no raw new/delete.


---

## Possible Updates

- **Market Orders & Time-in-Force:** Add IOC (immediate-or-cancel) and FOK
  (fill-or-kill) order types alongside the current limit-only model.
- **Multi-Symbol Support:** Key a map of `OrderBook` instances by ticker to
  run several instruments in one process.
- **Throughput Benchmarking:** Add a `--benchmark` mode measuring real
  orders/sec and tick-to-trade latency, rather than only correctness tests.
- **Networked Order Entry:** A minimal TCP or Unix-socket front end so
  orders can be submitted from an external client instead of only via the
  in-process API.
