# TransactionEngine (C++17)

An in-memory sequence engine with a C++ API and a stdin/stdout CLI for the command format in the specification.

> **Status: correctness-first MVP, not yet SLA-qualified.** Insert and range-query operations use an implicit treap and have expected `O(log M)` structural cost. The current range-adjustment implementation visits each element in the affected range (`O(K + log M)` for a range of length `K`). It is correct, but it does **not** meet the requested strict logarithmic update SLA or the 1.5-second acceptance target. Do not deploy this version as the high-load production engine until that gap is resolved and benchmarked.

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

Invalid indices and per-element `int64_t` overflow are reported on stderr with exit code 1. Query sums use a 128-bit intermediate/result type so the sum of multiple valid 64-bit elements is not silently wrapped. The public `WideInt` alias is a GCC/Clang `__int128_t` extension; the CLI prints it as a decimal integer.

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

## Complexity and engineering note

The implicit treap stores each subtree's size, sum, maximum prefix, maximum suffix and maximum non-empty subarray sum. These aggregates combine exactly in constant time, so a query can split out a range, read its aggregate, then merge the sequence back. An insertion is an expected-logarithmic split/merge.

A uniform range addition is the difficult operation: adding `d` changes a candidate subarray sum by `d * length`, so the stored maximum prefix/suffix/subarray cannot be updated by simply adding `d` to each aggregate. This MVP therefore validates the range and recomputes the affected subtree by visiting its elements. Any proposed optimization must preserve exact answers for arbitrary positive and negative values and must be validated against randomized differential tests before being described as SLA-compliant.

## Tests

The unit tests cover the supplied example, insertion in the middle, range updates, all-negative ranges, invalid indices, overflow safety, sums wider than `int64_t`, and 4,000 randomized operations checked against a brute-force vector model.
