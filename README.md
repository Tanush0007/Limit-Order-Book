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

*(Add your own notes here on what you built, learned, or changed while
working with this — e.g. what you'd explain in an interview about a design
decision, or a bug you hit and how you fixed it. Left blank intentionally
rather than a fabricated day-by-day log.)*

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
