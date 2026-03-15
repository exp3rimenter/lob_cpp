# Limit Order Book

A production-grade, high-performance limit order book engine written in C++23.

## Features

- **Order types**: Limit, Market, IOC (Immediate-or-Cancel), FOK (Fill-or-Kill)
- **Zero heap allocation in hot path** via lock-free pool allocator
- **Cache-friendly** intrusive linked list per price level (`alignas(64)` Order)
- **Price-time priority** matching (FIFO within each level)
- **Multi-symbol** matching engine with `shared_mutex` read-write locking
- **Event-driven** via `IEventHandler` interface (trade callbacks, fill callbacks, book updates)
- **Full order lifecycle**: submit, cancel, modify
- **FOK rollback** on insufficient depth
- **Google Test** suite (25+ test cases)
- **Google Benchmark** suite (6 benchmarks)
- **Interactive CLI** for manual testing
- **GitHub Actions** CI across Debug/Release/RelWithDebInfo

## Build

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
```

## Run Tests

```bash
ctest --test-dir build --output-on-failure
```

## Run Benchmarks

```bash
./build/bench/lob_bench
```

## Run CLI

```bash
./build/tools/lob_cli
> add_sym AAPL
> buy AAPL 15000 100
> sell AAPL 14900 50
> depth AAPL
> cancel AAPL <id>
> quit
```

## Architecture

```
include/lob/
  types.hpp           # Enums, aliases, to_string()
  clock.hpp           # Real + mock clock abstraction
  order.hpp           # Order, Trade, ExecutionReport, request types
  pool_allocator.hpp  # Lock-free slab allocator
  price_level.hpp     # Intrusive doubly-linked list per price level
  event_handler.hpp   # IEventHandler pure virtual interface
  order_book.hpp      # Single-symbol book
  matching_engine.hpp # Multi-symbol engine

src/
  order_book.cpp      # Match loop, FOK rollback, modify, cancel
  matching_engine.cpp # Symbol routing, shared_mutex

tests/                # Google Test suite
bench/                # Google Benchmark suite
tools/                # Interactive CLI
```

## Performance (indicative, Release -O3 -march=native)

| Benchmark                   | Throughput       |
|-----------------------------|-----------------|
| Limit order (no match)      | ~10M ops/sec     |
| Limit order (with match)    | ~4M ops/sec      |
| Cancel order                | ~10M ops/sec     |
| Market sweep (10 levels)    | ~2M ops/sec      |
| Depth snapshot (10 levels)  | ~8M ops/sec      |

## License

MIT
