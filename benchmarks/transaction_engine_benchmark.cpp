#include "transaction_engine.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <random>
#include <string>

namespace {
using Clock = std::chrono::steady_clock;

double seconds(Clock::duration duration) {
    return std::chrono::duration<double>(duration).count();
}

std::size_t parse_or_default(char* value, std::size_t fallback) {
    if (value == nullptr) return fallback;
    try {
        const auto parsed = std::stoull(value);
        return static_cast<std::size_t>(parsed);
    } catch (...) {
        return fallback;
    }
}
}  // namespace

int main(int argc, char** argv) {
    const std::size_t count = argc > 1 ? parse_or_default(argv[1], 20000U) : 20000U;
    const std::size_t operations = argc > 2 ? parse_or_default(argv[2], 2000U) : 2000U;
    if (count == 0U) {
        std::cerr << "element count must be positive\n";
        return 2;
    }

    std::mt19937_64 random(0xBADC0FFEEULL);
    transaction_engine::TransactionEngine engine;

    const auto build_start = Clock::now();
    for (std::size_t i = 0; i < count; ++i) {
        const auto value = static_cast<std::int64_t>(random() % 2001U) - 1000;
        engine.insert_transaction(engine.size(), value);
    }
    const auto build_end = Clock::now();

    std::uint64_t checksum = 0;
    const auto operations_start = Clock::now();
    for (std::size_t i = 0; i < operations; ++i) {
        std::size_t left = static_cast<std::size_t>(random() % count);
        std::size_t right = static_cast<std::size_t>(random() % count);
        if (left > right) std::swap(left, right);

        switch (i % 4U) {
        case 0U: {
            // Full-range update: stress the worst case for walking all blocks.
            engine.bulk_adjust_balance(0U, count - 1U,
                static_cast<std::int64_t>(random() % 3U) - 1);
            break;
        }
        case 1U: {
            // A wide update, typically covering about half the sequence.
            const std::size_t start = static_cast<std::size_t>(random() % (count / 4U + 1U));
            const std::size_t end = count - 1U -
                static_cast<std::size_t>(random() % (count / 4U + 1U));
            engine.bulk_adjust_balance(start, std::max(start, end),
                static_cast<std::int64_t>(random() % 3U) - 1);
            break;
        }
        default: {
            const auto answer = engine.get_max_profit_period(left, right);
            checksum ^= static_cast<std::uint64_t>(answer) +
                        static_cast<std::uint64_t>(i);
            break;
        }
        }
    }
    const auto operations_end = Clock::now();

    std::cout << "elements=" << count
              << " operations=" << operations
              << " build_seconds=" << seconds(build_end - build_start)
              << " mixed_operations_seconds=" << seconds(operations_end - operations_start)
              << " mixed_ops_per_second="
              << static_cast<double>(operations) /
                     std::max(seconds(operations_end - operations_start), 1e-9)
              << " checksum=" << checksum << '\n';
    return 0;
}
