#pragma once

#include <cstddef>
#include <cstdint>

namespace transaction_engine {

using WideInt = __int128_t;

class TransactionEngine final {
public:
    TransactionEngine() noexcept;
    ~TransactionEngine();

    TransactionEngine(const TransactionEngine&) = delete;
    TransactionEngine& operator=(const TransactionEngine&) = delete;
    TransactionEngine(TransactionEngine&&) = delete;
    TransactionEngine& operator=(TransactionEngine&&) = delete;

    // Insert before position; position == size() appends.
    void insert_transaction(std::size_t position, std::int64_t value);

    // Add delta to each value in inclusive [left, right].
    void bulk_adjust_balance(std::size_t left, std::size_t right,
                             std::int64_t delta);

    // Maximum sum of a non-empty contiguous subarray in inclusive [left, right].
    // WideInt avoids overflow when summing several valid int64_t values.
    WideInt get_max_profit_period(std::size_t left, std::size_t right);

    [[nodiscard]] std::size_t size() const noexcept;

private:
    struct Node;
    Node* root_;
    std::uint64_t rng_state_;

    std::uint64_t next_priority() noexcept;
    static std::size_t node_size(const Node* node) noexcept;
    static void destroy(Node* node) noexcept;
    static void split(Node* root, std::size_t left_count, Node*& left,
                      Node*& right) noexcept;
    static Node* merge(Node* left, Node* right) noexcept;
    static void pull(Node* node) noexcept;
    static void validate_add(const Node* node, std::int64_t delta);
    static void add_all(Node* node, std::int64_t delta);
};

}  // namespace transaction_engine
