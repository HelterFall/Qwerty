#include "transaction_engine.hpp"

#include <cstdint>
#include <iostream>
#include <limits>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>

using transaction_engine::TransactionEngine;
using transaction_engine::WideInt;

namespace {
int failures = 0;

void check(bool condition, const std::string& description) {
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << description << '\n';
    }
}

WideInt brute_max_subarray(const std::vector<std::int64_t>& values,
                           std::size_t left, std::size_t right) {
    WideInt best = -(static_cast<WideInt>(1) << 120);
    for (std::size_t i = left; i <= right; ++i) {
        WideInt sum = 0;
        for (std::size_t j = i; j <= right; ++j) {
            sum += static_cast<WideInt>(values[j]);
            if (sum > best) best = sum;
        }
    }
    return best;
}

WideInt linear_max_subarray(const std::vector<std::int64_t>& values,
                            std::size_t left, std::size_t right) {
    WideInt ending_here = static_cast<WideInt>(values[left]);
    WideInt best = ending_here;
    for (std::size_t i = left + 1U; i <= right; ++i) {
        const WideInt value = static_cast<WideInt>(values[i]);
        ending_here = std::max(value, ending_here + value);
        best = std::max(best, ending_here);
    }
    return best;
}

template <typename Callable>
void expect_out_of_range(const Callable& callable, const std::string& description) {
    bool thrown = false;
    try {
        callable();
    } catch (const std::out_of_range&) {
        thrown = true;
    }
    check(thrown, description);
}

void test_examples() {
    TransactionEngine engine;
    engine.insert_transaction(0, 10);
    engine.insert_transaction(1, -5);
    engine.insert_transaction(2, 20);
    engine.bulk_adjust_balance(0, 1, 5);
    check(engine.get_max_profit_period(0, 2) == 35,
          "provided CLI example returns 35 (not 30; see README)");

    TransactionEngine second;
    second.insert_transaction(0, 10);
    second.insert_transaction(1, -5);
    second.insert_transaction(2, 20);
    second.insert_transaction(1, 99);
    second.bulk_adjust_balance(1, 2, -10);
    check(second.get_max_profit_period(0, 3) == 104,
          "insert/update example returns 104 (not 124; see README)");
    check(second.get_max_profit_period(2, 3) == 20,
          "query chooses the non-empty best subarray");
}

void test_negative_and_boundaries() {
    TransactionEngine engine;
    engine.insert_transaction(0, -8);
    engine.insert_transaction(1, -3);
    engine.insert_transaction(2, -11);
    check(engine.get_max_profit_period(0, 2) == -3,
          "all-negative range returns its largest element");
    check(engine.get_max_profit_period(0, 0) == -8, "single element query");
    check(engine.size() == 3, "size reflects inserts");
    expect_out_of_range([&] { engine.insert_transaction(4, 1); },
                        "insert out-of-range is rejected");
    expect_out_of_range([&] { engine.get_max_profit_period(1, 3); },
                        "query out-of-range is rejected");
    expect_out_of_range([&] { engine.bulk_adjust_balance(2, 1, 2); },
                        "reversed update range is rejected");
}

void test_wide_aggregate_and_transactional_overflow() {
    TransactionEngine engine;
    constexpr std::int64_t max_value = std::numeric_limits<std::int64_t>::max();
    engine.insert_transaction(0, max_value);
    engine.insert_transaction(1, max_value);
    engine.insert_transaction(2, max_value);
    const WideInt expected = static_cast<WideInt>(max_value) * 3;
    check(engine.get_max_profit_period(0, 2) == expected,
          "aggregate sum does not overflow int64_t");

    bool thrown = false;
    try {
        engine.bulk_adjust_balance(0, 0, 1);
    } catch (const std::overflow_error&) {
        thrown = true;
    }
    check(thrown, "element overflow is reported");
    check(engine.get_max_profit_period(0, 2) == expected,
          "overflowing update leaves the sequence unchanged");
}

void test_randomized_against_vector() {
    TransactionEngine engine;
    std::vector<std::int64_t> reference;
    std::mt19937_64 random(0xC0FFEEULL);

    for (int step = 0; step < 4000; ++step) {
        const unsigned choice = static_cast<unsigned>(random() % 3U);
        if (reference.empty() || (choice == 0U && reference.size() < 100U)) {
            const std::size_t position = static_cast<std::size_t>(
                random() % static_cast<std::uint64_t>(reference.size() + 1U));
            const auto value = static_cast<std::int64_t>(random() % 2001U) - 1000;
            reference.insert(reference.begin() + static_cast<std::ptrdiff_t>(position), value);
            engine.insert_transaction(position, value);
        } else if (choice == 1U) {
            std::size_t left = static_cast<std::size_t>(random() % reference.size());
            std::size_t right = static_cast<std::size_t>(random() % reference.size());
            if (left > right) {
                const std::size_t temp = left;
                left = right;
                right = temp;
            }
            const auto delta = static_cast<std::int64_t>(random() % 41U) - 20;
            for (std::size_t i = left; i <= right; ++i) reference[i] += delta;
            engine.bulk_adjust_balance(left, right, delta);
        } else {
            std::size_t left = static_cast<std::size_t>(random() % reference.size());
            std::size_t right = static_cast<std::size_t>(random() % reference.size());
            if (left > right) {
                const std::size_t temp = left;
                left = right;
                right = temp;
            }
            check(engine.get_max_profit_period(left, right) ==
                      brute_max_subarray(reference, left, right),
                  "randomized operation agrees with brute-force reference");
        }
        check(engine.size() == reference.size(), "randomized size invariant");
    }

    if (!reference.empty()) {
        check(engine.get_max_profit_period(0, reference.size() - 1U) ==
                  brute_max_subarray(reference, 0, reference.size() - 1U),
              "final randomized full-range query agrees with reference");
    }
}


void test_cross_block_ranges_and_transactional_overflow() {
    TransactionEngine engine;
    std::vector<std::int64_t> reference;

    for (std::size_t i = 0; i < 257U; ++i) {
        const auto value = static_cast<std::int64_t>(i % 17U) - 8;
        engine.insert_transaction(i, value);
        reference.push_back(value);
    }
    check(engine.get_max_profit_period(0, 256) ==
              brute_max_subarray(reference, 0, 256),
          "initial query spans several blocks");

    engine.bulk_adjust_balance(7, 250, 11);
    for (std::size_t i = 7; i <= 250; ++i) reference[i] += 11;
    check(engine.get_max_profit_period(0, 99) ==
              brute_max_subarray(reference, 0, 99),
          "range update across block boundaries");

    engine.insert_transaction(32, -500);
    reference.insert(reference.begin() + 32, -500);
    engine.bulk_adjust_balance(20, 75, -3);
    for (std::size_t i = 20; i <= 75; ++i) reference[i] -= 3;
    check(engine.get_max_profit_period(16, 88) ==
              brute_max_subarray(reference, 16, 88),
          "insert after lazy block updates");

    TransactionEngine overflow;
    for (std::size_t i = 0; i < 100U; ++i) {
        overflow.insert_transaction(i, 0);
    }
    overflow.bulk_adjust_balance(99, 99,
        std::numeric_limits<std::int64_t>::max());

    bool thrown = false;
    try {
        overflow.bulk_adjust_balance(
            0, 99, 1);
    } catch (const std::overflow_error&) {
        thrown = true;
    }
    check(thrown, "overflow in a later block rejects a wide range update");
    check(overflow.get_max_profit_period(0, 0) == 0,
          "overflow rejection leaves earlier blocks unchanged");
    check(overflow.get_max_profit_period(99, 99) ==
              static_cast<WideInt>(std::numeric_limits<std::int64_t>::max()),
          "overflow rejection leaves the boundary element unchanged");
}


void test_large_multi_block_updates() {
    TransactionEngine engine;
    std::vector<std::int64_t> reference;
    constexpr std::size_t count = 1300U;

    // Cross the split threshold for both the 512- and 1024-element defaults.
    // Other CI variants also exercise multiple blocks with this same workload.
    for (std::size_t i = 0; i < count; ++i) {
        const auto value = static_cast<std::int64_t>((i * 37U) % 101U) - 50;
        engine.insert_transaction(engine.size(), value);
        reference.push_back(value);
    }
    check(engine.size() == count, "large append sequence has the expected size");
    check(engine.get_max_profit_period(0U, count - 1U) ==
              brute_max_subarray(reference, 0U, count - 1U),
          "large multi-block initial query agrees with brute force");

    engine.bulk_adjust_balance(17U, 1282U, 13);
    for (std::size_t i = 17U; i <= 1282U; ++i) reference[i] += 13;
    check(engine.get_max_profit_period(0U, count - 1U) ==
              brute_max_subarray(reference, 0U, count - 1U),
          "large cross-block update agrees with brute force");
    check(engine.get_max_profit_period(200U, 1000U) ==
              brute_max_subarray(reference, 200U, 1000U),
          "large interior query agrees with brute force");

    engine.bulk_adjust_balance(511U, 1025U, -29);
    for (std::size_t i = 511U; i <= 1025U; ++i) reference[i] -= 29;
    check(engine.get_max_profit_period(400U, 1100U) ==
              brute_max_subarray(reference, 400U, 1100U),
          "second cross-block update agrees with brute force");

    engine.insert_transaction(512U, -777);
    reference.insert(reference.begin() + 512, -777);
    check(engine.get_max_profit_period(0U, reference.size() - 1U) ==
              brute_max_subarray(reference, 0U, reference.size() - 1U),
          "middle insertion after large lazy updates preserves block aggregates");
}


void test_large_randomized_multiblock_stream() {
    TransactionEngine engine;
    std::vector<std::int64_t> reference;
    std::mt19937_64 random(0xB10C5EEDULL);

    for (std::size_t i = 0; i < 1300U; ++i) {
        const auto value = static_cast<std::int64_t>(random() % 2001U) - 1000;
        engine.insert_transaction(engine.size(), value);
        reference.push_back(value);
    }

    std::size_t inserted = 0;
    for (int step = 0; step < 600; ++step) {
        const unsigned choice = static_cast<unsigned>(random() % 10U);
        if (choice == 0U && inserted < 20U) {
            const std::size_t position = static_cast<std::size_t>(
                random() % static_cast<std::uint64_t>(reference.size() + 1U));
            const auto value = static_cast<std::int64_t>(random() % 2001U) - 1000;
            reference.insert(reference.begin() + static_cast<std::ptrdiff_t>(position),
                             value);
            engine.insert_transaction(position, value);
            ++inserted;
        } else if (choice < 6U) {
            std::size_t left = static_cast<std::size_t>(random() % reference.size());
            std::size_t right = static_cast<std::size_t>(random() % reference.size());
            if (left > right) std::swap(left, right);
            const auto delta = static_cast<std::int64_t>(random() % 51U) - 25;
            engine.bulk_adjust_balance(left, right, delta);
            for (std::size_t i = left; i <= right; ++i) reference[i] += delta;
        } else {
            std::size_t left = static_cast<std::size_t>(random() % reference.size());
            std::size_t right = static_cast<std::size_t>(random() % reference.size());
            if (left > right) std::swap(left, right);
            check(engine.get_max_profit_period(left, right) ==
                      linear_max_subarray(reference, left, right),
                  "large randomized multi-block stream agrees with linear reference");
        }

        check(engine.size() == reference.size(),
              "large randomized multi-block stream preserves size");
    }

    check(engine.get_max_profit_period(0U, reference.size() - 1U) ==
              linear_max_subarray(reference, 0U, reference.size() - 1U),
          "large randomized stream final full-range query agrees with reference");
}

}  // namespace

int main() {
    test_examples();
    test_negative_and_boundaries();
    test_wide_aggregate_and_transactional_overflow();
    test_cross_block_ranges_and_transactional_overflow();
    test_large_multi_block_updates();
    test_large_randomized_multiblock_stream();
    test_randomized_against_vector();

    if (failures != 0) {
        std::cerr << failures << " test(s) failed\n";
        return 1;
    }
    std::cout << "All TransactionEngine tests passed\n";
    return 0;
}
