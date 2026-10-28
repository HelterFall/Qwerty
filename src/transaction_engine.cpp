#include "transaction_engine.hpp"

#include <algorithm>
#include <limits>
#include <stdexcept>

namespace transaction_engine {
namespace {

constexpr WideInt kNegativeInfinity = -(static_cast<WideInt>(1) << 120);

struct Stats {
    std::size_t length = 0;
    WideInt sum = 0;
    WideInt prefix = kNegativeInfinity;
    WideInt suffix = kNegativeInfinity;
    WideInt best = kNegativeInfinity;
};

Stats singleton(std::int64_t value) noexcept {
    const WideInt v = static_cast<WideInt>(value);
    return Stats{1, v, v, v, v};
}

Stats combine(const Stats& left, const Stats& right) noexcept {
    if (left.length == 0) return right;
    if (right.length == 0) return left;

    Stats result;
    result.length = left.length + right.length;
    result.sum = left.sum + right.sum;
    result.prefix = std::max(left.prefix, left.sum + right.prefix);
    result.suffix = std::max(right.suffix, right.sum + left.suffix);
    result.best = std::max(std::max(left.best, right.best),
                           left.suffix + right.prefix);
    return result;
}

std::int64_t checked_add(std::int64_t value, std::int64_t delta) {
    const WideInt sum = static_cast<WideInt>(value) +
                        static_cast<WideInt>(delta);
    if (sum < static_cast<WideInt>(std::numeric_limits<std::int64_t>::min()) ||
        sum > static_cast<WideInt>(std::numeric_limits<std::int64_t>::max())) {
        throw std::overflow_error("transaction value would overflow int64_t");
    }
    return static_cast<std::int64_t>(sum);
}

}  // namespace

struct TransactionEngine::Node {
    explicit Node(std::int64_t input, std::uint64_t priority_value) noexcept
        : value(input), priority(priority_value), aggregate(singleton(input)) {}

    std::int64_t value;
    std::uint64_t priority;
    Node* left = nullptr;
    Node* right = nullptr;
    Stats aggregate;
};

TransactionEngine::TransactionEngine() noexcept
    : root_(nullptr), rng_state_(0x9E3779B97F4A7C15ULL) {}

TransactionEngine::~TransactionEngine() { destroy(root_); }

std::uint64_t TransactionEngine::next_priority() noexcept {
    std::uint64_t z = (rng_state_ += 0x9E3779B97F4A7C15ULL);
    z = (z ^ (z >> 30U)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27U)) * 0x94D049BB133111EBULL;
    return z ^ (z >> 31U);
}

std::size_t TransactionEngine::node_size(const Node* node) noexcept {
    return node == nullptr ? 0U : node->aggregate.length;
}

void TransactionEngine::destroy(Node* node) noexcept {
    if (node == nullptr) return;
    destroy(node->left);
    destroy(node->right);
    delete node;
}

void TransactionEngine::pull(Node* node) noexcept {
    if (node == nullptr) return;
    Stats result;
    if (node->left != nullptr) result = node->left->aggregate;
    result = combine(result, singleton(node->value));
    if (node->right != nullptr) result = combine(result, node->right->aggregate);
    node->aggregate = result;
}

void TransactionEngine::split(Node* root, std::size_t left_count, Node*& left,
                              Node*& right) noexcept {
    if (root == nullptr) {
        left = nullptr;
        right = nullptr;
        return;
    }
    if (node_size(root->left) >= left_count) {
        split(root->left, left_count, left, root->left);
        right = root;
        pull(right);
    } else {
        split(root->right, left_count - node_size(root->left) - 1U,
              root->right, right);
        left = root;
        pull(left);
    }
}

TransactionEngine::Node* TransactionEngine::merge(Node* left,
                                                   Node* right) noexcept {
    if (left == nullptr) return right;
    if (right == nullptr) return left;

    if (left->priority > right->priority) {
        left->right = merge(left->right, right);
        pull(left);
        return left;
    }
    right->left = merge(left, right->left);
    pull(right);
    return right;
}

void TransactionEngine::validate_add(const Node* node, std::int64_t delta) {
    if (node == nullptr) return;
    (void)checked_add(node->value, delta);
    validate_add(node->left, delta);
    validate_add(node->right, delta);
}

void TransactionEngine::add_all(Node* node, std::int64_t delta) {
    if (node == nullptr) return;
    // validate_add() ran first, so individual values cannot overflow here.
    node->value = static_cast<std::int64_t>(
        static_cast<WideInt>(node->value) + static_cast<WideInt>(delta));
    add_all(node->left, delta);
    add_all(node->right, delta);
    pull(node);
}

void TransactionEngine::insert_transaction(std::size_t position,
                                           std::int64_t value) {
    if (position > size()) {
        throw std::out_of_range("insert position must be in [0, size]");
    }
    Node* inserted = new Node(value, next_priority());
    Node* left = nullptr;
    Node* right = nullptr;
    split(root_, position, left, right);
    root_ = merge(merge(left, inserted), right);
}

void TransactionEngine::bulk_adjust_balance(std::size_t left_index,
                                            std::size_t right_index,
                                            std::int64_t delta) {
    if (left_index > right_index || right_index >= size()) {
        throw std::out_of_range("update range must be a valid inclusive range");
    }

    Node* left = nullptr;
    Node* remainder = nullptr;
    Node* middle = nullptr;
    Node* right = nullptr;
    split(root_, left_index, left, remainder);
    split(remainder, right_index - left_index + 1U, middle, right);

    try {
        validate_add(middle, delta);
        add_all(middle, delta);
    } catch (...) {
        root_ = merge(left, merge(middle, right));
        throw;
    }
    root_ = merge(left, merge(middle, right));
}

WideInt TransactionEngine::get_max_profit_period(std::size_t left_index,
                                                  std::size_t right_index) {
    if (left_index > right_index || right_index >= size()) {
        throw std::out_of_range("query range must be a valid inclusive range");
    }

    Node* left = nullptr;
    Node* remainder = nullptr;
    Node* middle = nullptr;
    Node* right = nullptr;
    split(root_, left_index, left, remainder);
    split(remainder, right_index - left_index + 1U, middle, right);
    const WideInt answer = middle->aggregate.best;
    root_ = merge(left, merge(middle, right));
    return answer;
}

std::size_t TransactionEngine::size() const noexcept {
    return node_size(root_);
}

}  // namespace transaction_engine
