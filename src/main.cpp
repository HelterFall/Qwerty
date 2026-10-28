#include "transaction_engine.hpp"

#include <cstdint>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

void print_wide_int(transaction_engine::WideInt value) {
    using transaction_engine::WideInt;
    if (value == 0) {
        std::cout << '0';
        return;
    }
    if (value < 0) {
        std::cout << '-';
        value = -value;
    }
    char digits[48];
    int count = 0;
    while (value > 0) {
        digits[count++] = static_cast<char>('0' + value % 10);
        value /= 10;
    }
    while (count > 0) std::cout << digits[--count];
}

std::size_t parse_index(long long value) {
    if (value < 0) throw std::out_of_range("indices must be non-negative");
    return static_cast<std::size_t>(value);
}

}  // namespace

int main() {
    std::ios::sync_with_stdio(false);
    std::cin.tie(nullptr);

    long long query_count = 0;
    if (!(std::cin >> query_count)) return 0;
    if (query_count < 0) {
        std::cerr << "error: query count must be non-negative\n";
        return 1;
    }

    transaction_engine::TransactionEngine engine;
    try {
        for (long long i = 0; i < query_count; ++i) {
            int operation = 0;
            if (!(std::cin >> operation)) {
                throw std::runtime_error("unexpected end of input while reading operation");
            }

            if (operation == 1) {
                long long position = 0;
                std::int64_t value = 0;
                if (!(std::cin >> position >> value)) {
                    throw std::runtime_error("invalid insert command");
                }
                engine.insert_transaction(parse_index(position), value);
            } else if (operation == 2) {
                long long left = 0;
                long long right = 0;
                std::int64_t delta = 0;
                if (!(std::cin >> left >> right >> delta)) {
                    throw std::runtime_error("invalid update command");
                }
                engine.bulk_adjust_balance(parse_index(left), parse_index(right), delta);
            } else if (operation == 3) {
                long long left = 0;
                long long right = 0;
                if (!(std::cin >> left >> right)) {
                    throw std::runtime_error("invalid query command");
                }
                print_wide_int(engine.get_max_profit_period(parse_index(left),
                                                            parse_index(right)));
                std::cout << '\n';
            } else {
                throw std::runtime_error("unknown operation type: " +
                                         std::to_string(operation));
            }
        }
    } catch (const std::exception& error) {
        std::cerr << "error: " << error.what() << '\n';
        return 1;
    }
    return 0;
}
