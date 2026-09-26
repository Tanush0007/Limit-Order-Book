CXX := g++
CXXFLAGS := -std=c++17 -O2 -Wall -Wextra -Iinclude
SANFLAGS := -std=c++17 -O1 -g -Wall -Wextra -Iinclude -fsanitize=address,undefined -fno-omit-frame-pointer

SRC := src/OrderBook.cpp
DEMO_SRC := src/main.cpp
TEST_SRC := tests/test_orderbook.cpp

.PHONY: all demo test clean

all: demo

demo: $(SRC) $(DEMO_SRC)
	$(CXX) $(CXXFLAGS) $(SRC) $(DEMO_SRC) -o demo
	./demo

# Builds with AddressSanitizer + UndefinedBehaviorSanitizer (LeakSanitizer is
# bundled with ASan on Linux) so a leak or UB would fail the run loudly --
# this is the "zero memory leaks" verification since valgrind isn't
# available in every environment.
test: $(SRC) $(TEST_SRC)
	$(CXX) $(SANFLAGS) $(SRC) $(TEST_SRC) -o test_orderbook
	ASAN_OPTIONS=detect_leaks=1 ./test_orderbook

clean:
	rm -f demo test_orderbook
