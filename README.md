# Limit Order Book Matching Engine

A C++17 price-time-priority limit order book, matching the design implied by
the resume bullets: O(1) order lookups, O(log N) insertion/cancellation,
partial fills, and a leak-checked automated test suite.

## Design

**Price levels** — two `std::map<price, std::list<Order>>` trees, one for
bids (best price first, i.e. highest) and one for asks (best price first,
i.e. lowest). The list at each price level preserves FIFO time priority.
Inserting a new price level is `O(log P)` where `P` is the number of
distinct price levels on that side; pushing onto an existing level's list is
`O(1)`.

**Order index (the hash map)** — `std::unordered_map<order_id, OrderHandle>`,
where `OrderHandle` stores the side, price, and a `std::list<Order>::iterator`
pointing straight at the order's node. `std::list` iterators are stable
under insertion/erasure elsewhere in the list, so this gives:
- `O(1)` average lookup of any order by id
- `O(1)` removal from its list once located
- cancel is `O(log N)` overall only because erasing an *emptied* price level
  from the tree is `O(log P)`

**Matching** — an incoming buy walks `asks_` from `begin()` (lowest ask)
while its price crosses; an incoming sell walks `bids_` from `begin()`
(highest bid) while its price crosses. Within a level, the front of the
list (oldest order) is matched first. Partial fills are handled naturally:
whichever side has less remaining quantity is fully consumed and the
other's `remaining` is decremented; the loop continues until either the
incoming order is filled or the book no longer crosses.

**Modification** — a quantity *decrease* at an unchanged price is applied
in place and keeps time priority (matches real exchange semantics). A price
change or a quantity *increase* is implemented as cancel + re-submit, which
correctly loses priority and can trigger an immediate match if the new
price crosses the book.

## Files

```
include/Order.h        Order, Trade, AddOrderResult data structures
include/OrderBook.h    OrderBook class + design notes on complexity
src/OrderBook.cpp      Matching engine implementation
src/main.cpp           Small interactive demo
tests/test_orderbook.cpp  Automated test suite (see below)
Makefile
```

## Building & running

```bash
make demo    # builds and runs the demo program
make test    # builds with -fsanitize=address,undefined and runs the test suite
make clean
```

No external dependencies — just a C++17 compiler (tested with g++ 13).

## Test suite

`tests/test_orderbook.cpp` covers:
- resting orders that don't cross
- exact-match and partial fills, both single-level and swept across
  multiple price levels
- FIFO price-time priority within a level
- cancellation, including double-cancel and empty-level cleanup
- modification: in-place quantity decrease (priority kept) vs. price change /
  quantity increase (priority lost, may trade immediately)
- a **20,000-event randomized high-frequency simulation** (mixed adds,
  cancels, modifies) asserting structural invariants after every event —
  quantities never go negative, the book is never left crossed, and the
  order index never drifts from the book's actual contents

The `make test` target links with `-fsanitize=address,undefined`, so
LeakSanitizer runs automatically at exit. Since `OrderBook` never uses raw
`new`/`delete` (all ownership lives in `std::map`/`std::list`/`std::vector`),
the suite exits clean with zero leaks reported — confirmed by running it
during development:

```
[simulation] 20000 events, 1435 resting orders, 11315 trades executed, 455 active price levels
48001 checks run, 48001 passed, 0 failed.
```

If `valgrind` is available in your environment, `valgrind --leak-check=full
./test_orderbook` (built via `make demo`-style flags, without the sanitizer)
works as an additional check.

## Possible extensions

- Market orders (no limit price) and IOC/FOK time-in-force
- Multiple symbols (one `OrderBook` per instrument, keyed in a map)
- Persistent trade log / replay
- Lock-free or sharded design for true multi-threaded throughput
