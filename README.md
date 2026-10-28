# TransactionEngine (C++17)

An in-memory sequence engine with a C++ API and a stdin/stdout CLI for the command format in the specification.

> **Status: correctness-focused, performance-improved MVP; not yet SLA-qualified.** The sequence is stored in an implicit treap of small blocks. Each block keeps convex hulls of prefix, suffix and best-subarray sums by length, allowing a whole-block range addition to be represented by a lazy tag. This avoids visiting every transaction in a long update, but an update still visits the affected blocks. This implementation therefore does **not** claim strict (O(\log M)) range updates or the 1.5-second acceptance target without a workload benchmark.

## Build and test

Requires GCC or Clang with C++17 support and CMake 3.16+.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

The GitHub Actions workflow builds the project, runs unit tests and executes the sample CLI test on pushes and pull requests.

## CLI

The executable is `build/transaction_engine_cli`. It reads `Q` and then `Q` commands from stdin:

- `1 p v` — insert `v` before position `p`; `p == size` appends.
- `2 l r d` — add `d` to every element in inclusive range `[l, r]`.
- `3 l r` — print the maximum sum of a **non-empty** contiguous subarray within `[l, r]`.

Example:

```text
5
1 0 10
1 1 -5
1 2 20
2 0 1 5
3 0 2
```

Output:

```text
35
```

**Arithmetic correction:** the specification says `30`, but the described array is `[15, 0, 20]`, whose maximum non-empty subarray sum is `35`. Similarly, `[10, 89, -15, 20]` sums to `104`, not `124`. Tests follow the stated operations and exact arithmetic.

Invalid indices and per-element `int64_t` overflow are reported on stderr with exit code 1. Range updates validate all affected values before changing the sequence, so an arithmetic-overflow rejection leaves the array unchanged. Query sums use a 128-bit result type so sums of multiple valid 64-bit elements are not silently wrapped. `WideInt` is a GCC/Clang `__int128_t` extension.

## API

Header: `src/transaction_engine.hpp`, namespace `transaction_engine`.

```cpp
TransactionEngine engine;
engine.insert_transaction(position, value);
engine.bulk_adjust_balance(left, right, delta);
WideInt profit = engine.get_max_profit_period(left, right);
std::size_t count = engine.size();
```

Indices are zero-based and ranges are inclusive. Empty ranges are not accepted. If every value in a query range is negative, the query returns the least-negative element, never zero.

## Data structure and complexity

The implicit treap stores small blocks (up to 32 values per block). The treap maintains the total element count and the standard maximum-subarray summary for each subtree. Each block stores:
- the base values and a lazy uniform-add tag;
- the base sum and element minimum/maximum for overflow validation;
- upper hulls of prefix sums, suffix sums and best subarray sums indexed by length.

Adding `delta` to every value in a block changes a candidate sum of length `k` by `delta * k`. The block's hull vertices stay in the same order under this shear, so a block's maximum prefix, suffix and non-empty subarray sum can be recomputed with logarithmic hull support queries. A partially covered block is materialized and rebuilt.

Let `B` be the maximum block size (currently 32), `M` the number of elements, and `K` the number of elements in an update range. Expected bounds are approximately:
- insertion: (O(\log(M/B) + B^2)), because the affected block's subarray hull is rebuilt;
- range query: (O(B + \log(M/B)\log B));
- range addition: (O((K/B)\log B + B^2)), plus the same order for validating the affected blocks before applying an update.

These bounds are a practical improvement over visiting every element of every update range, but they are **not** strict logarithmic bounds. The randomized differential tests and CI are correctness checks, not a substitute for benchmarking the target input limits and 1.5-second requirement.

## Tests

The unit tests cover the supplied examples, insertion and block splitting, updates and queries spanning block boundaries, all-negative ranges, invalid indices, overflow safety and transactional overflow rejection, sums wider than `int64_t`, and randomized operations compared with a brute-force vector model.
