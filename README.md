# TransactionEngine (C++17)

An in-memory sequence engine with a C++ API and a stdin/stdout CLI for the command format in the specification.

> **Status: correctness tests pass; one synthetic benchmark is below 1.5 seconds, but the full SLA is not proven.** The sequence is stored in an implicit treap of small blocks. Each block keeps convex profile hulls for prefixes, suffixes and best subarrays. Profiles are rebuilt by divide-and-conquer hull composition in O(B log B) time, while whole-block additions use lazy tags. Range addition still visits all affected blocks, so this implementation does **not** provide strict (O(\\log M)) range updates.

## Build and test

Requires GCC or Clang with C++17 support and CMake 3.16+.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

The GitHub Actions workflow builds the project, runs the randomized correctness suite at seven block sizes, checks the sample CLI, and benchmarks mixed and update-heavy synthetic workloads. In a completed comparison run, 100,000 append insertions followed by 20,000 mixed operations took about 1.18 s with 256-element blocks, 0.88 s with 512-element blocks, and 1.38 s with 1024-element blocks. For 20,000 full-range updates on 100,000 elements, the update phase took about 1.17 s, 0.50 s, and 0.24 s respectively. Runner timings vary, and these synthetic results do not guarantee the 1.5-second target for other operation mixes or the official hidden workload; 512 is the new default based on the target-sized comparison.

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

The implicit treap stores small blocks (up to 512 values per block by default). The treap maintains the total element count and the standard maximum-subarray summary for each subtree. Each block stores:
- the base values and a lazy uniform-add tag;
- the base sum and element minimum/maximum for overflow validation;
- upper hulls of prefix sums, suffix sums and best subarray sums indexed by length.

Adding `delta` to every value in a block changes a candidate sum of length `k` by `delta * k`. The block's hull vertices stay in the same order under this shear, so a block's maximum prefix, suffix and non-empty subarray sum can be recomputed with logarithmic hull support queries. A partially covered block is materialized and rebuilt. Rebuilding recursively composes the profiles, including a Minkowski sum for subarrays crossing a split, rather than enumerating every O(B²) window.

Let `B` be the maximum block size (currently 512), `M` the number of elements, and `K` the number of elements in an update range. Expected bounds are approximately:
- insertion at an arbitrary position: \(O(\log(M/B) + B\log B)\), because the affected block's profiles are rebuilt; appending to an unshifted block uses incremental per-length summaries and takes \(O(B)\), with occasional splits costing \(O(B\log B)\);
- range query: \(O(B + \log(M/B)\log B)\);
- range-add validation: \(O(\log(M/B) + B)\), using subtree minimum/maximum summaries to reject overflowing updates before mutation;
- applying a range addition: \(O((K/B)\log B + B\log B)\), because each affected block is visited and partial boundary blocks are rebuilt.

These bounds are a practical improvement over visiting every transaction in every update range, but range addition is still **not** strict logarithmic time. The randomized differential tests check correctness. CI compares block sizes 16, 32, 64, 128, 256, 512 and 1024 and runs larger 100,000-element mixed and full-range-update benchmarks. The 512-element default was selected from the target-sized comparison; on the smaller 20,000-element mixed workload, the 256-element blocks were faster. These tests do not prove compliance for an unspecified or different target workload.

## Tests

The unit tests cover the supplied examples, insertion and block splitting, updates and queries spanning block boundaries, all-negative ranges, invalid indices, overflow safety and transactional overflow rejection, sums wider than `int64_t`, and randomized operations compared with a brute-force vector model.
