#include "transaction_engine.hpp"

#include <algorithm>
#include <limits>
#include <stdexcept>
#include <utility>
#include <vector>

namespace transaction_engine {
namespace {

constexpr WideInt kNegativeInfinity = -(static_cast<WideInt>(1) << 120);
constexpr std::size_t kMaxBlockSize = 32;

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

bool fits_int64(WideInt value) noexcept {
    return value >= static_cast<WideInt>(std::numeric_limits<std::int64_t>::min()) &&
           value <= static_cast<WideInt>(std::numeric_limits<std::int64_t>::max());
}

std::int64_t checked_value(WideInt value) {
    if (!fits_int64(value)) {
        throw std::overflow_error("transaction value would overflow int64_t");
    }
    return static_cast<std::int64_t>(value);
}

// For each length x, a hull stores the largest base sum y of that length.
// Adding delta to every value transforms y into y + delta*x, a shear that
// leaves the convex-hull vertex order unchanged. Support queries are O(log B).
struct Hull {
    std::vector<std::uint16_t> x;
    std::vector<WideInt> y;

    static WideInt cross(std::uint16_t ax, WideInt ay,
                         std::uint16_t bx, WideInt by,
                         std::uint16_t cx, WideInt cy) noexcept {
        const WideInt abx = static_cast<WideInt>(bx - ax);
        const WideInt bcx = static_cast<WideInt>(cx - bx);
        return abx * (cy - by) - (by - ay) * bcx;
    }

    void build(const std::vector<WideInt>& sums_by_length) {
        x.clear();
        y.clear();
        x.reserve(sums_by_length.size());
        y.reserve(sums_by_length.size());

        for (std::size_t i = 0; i < sums_by_length.size(); ++i) {
            const auto new_x = static_cast<std::uint16_t>(i + 1U);
            const WideInt new_y = sums_by_length[i];

            while (x.size() >= 2U) {
                const std::size_t n = x.size();
                if (cross(x[n - 2U], y[n - 2U], x[n - 1U], y[n - 1U],
                          new_x, new_y) < 0) {
                    break;
                }
                x.pop_back();
                y.pop_back();
            }
            x.push_back(new_x);
            y.push_back(new_y);
        }
    }

    WideInt max_plus(WideInt delta) const noexcept {
        if (x.empty()) return kNegativeInfinity;

        const auto value_at = [&](std::size_t index) noexcept {
            return y[index] + delta * static_cast<WideInt>(x[index]);
        };

        std::size_t low = 0;
        std::size_t high = x.size() - 1U;
        while (low < high) {
            const std::size_t middle = low + (high - low) / 2U;
            if (value_at(middle) <= value_at(middle + 1U)) {
                low = middle + 1U;
            } else {
                high = middle;
            }
        }
        return value_at(low);
    }
};

struct Block {
    explicit Block(std::vector<std::int64_t> initial)
        : values(std::move(initial)) {
        rebuild();
    }

    std::vector<std::int64_t> values;  // Base values; actual = base + lazy.
    WideInt lazy = 0;
    WideInt base_sum = 0;
    std::int64_t min_value = 0;
    std::int64_t max_value = 0;
    Hull prefix_hull;
    Hull suffix_hull;
    Hull best_hull;

    std::size_t size() const noexcept { return values.size(); }

    std::int64_t actual_value(std::size_t index) const noexcept {
        return static_cast<std::int64_t>(
            static_cast<WideInt>(values[index]) + lazy);
    }

    Stats stats() const noexcept {
        Stats result;
        result.length = values.size();
        result.sum = base_sum + lazy * static_cast<WideInt>(values.size());
        result.prefix = prefix_hull.max_plus(lazy);
        result.suffix = suffix_hull.max_plus(lazy);
        result.best = best_hull.max_plus(lazy);
        return result;
    }

    void validate_full_add(std::int64_t delta) const {
        const WideInt shift = lazy + static_cast<WideInt>(delta);
        if (!fits_int64(static_cast<WideInt>(min_value) + shift) ||
            !fits_int64(static_cast<WideInt>(max_value) + shift)) {
            throw std::overflow_error("transaction value would overflow int64_t");
        }
    }

    void materialize_without_rebuild() {
        if (lazy == 0) return;
        for (std::int64_t& value : values) {
            value = checked_value(static_cast<WideInt>(value) + lazy);
        }
        lazy = 0;
    }

    void insert(std::size_t position, std::int64_t value) {
        materialize_without_rebuild();
        values.insert(values.begin() + static_cast<std::ptrdiff_t>(position), value);
        rebuild();
    }

    void add_partial(std::size_t left, std::size_t right,
                     std::int64_t delta) {
        materialize_without_rebuild();
        for (std::size_t i = left; i <= right; ++i) {
            values[i] = checked_value(static_cast<WideInt>(values[i]) +
                                      static_cast<WideInt>(delta));
        }
        rebuild();
    }

    void rebuild() {
        if (values.empty()) {
            base_sum = 0;
            min_value = max_value = 0;
            prefix_hull.build({});
            suffix_hull.build({});
            best_hull.build({});
            lazy = 0;
            return;
        }

        // Rebuild is called after materialization for every mutation.
        lazy = 0;
        min_value = max_value = values.front();
        base_sum = 0;
        std::vector<WideInt> prefix_sums;
        std::vector<WideInt> suffix_sums;
        std::vector<WideInt> best_sums;
        prefix_sums.reserve(values.size());
        suffix_sums.reserve(values.size());
        best_sums.reserve(values.size());

        WideInt running = 0;
        for (const std::int64_t value : values) {
            running += static_cast<WideInt>(value);
            prefix_sums.push_back(running);
            base_sum += static_cast<WideInt>(value);
            min_value = std::min(min_value, value);
            max_value = std::max(max_value, value);
        }

        running = 0;
        for (std::size_t length = 1; length <= values.size(); ++length) {
            running += static_cast<WideInt>(values[values.size() - length]);
            suffix_sums.push_back(running);
        }

        for (std::size_t length = 1; length <= values.size(); ++length) {
            WideInt window = 0;
            for (std::size_t i = 0; i < length; ++i) {
                window += static_cast<WideInt>(values[i]);
            }
            WideInt best = window;
            for (std::size_t start = 1; start + length <= values.size(); ++start) {
                window += static_cast<WideInt>(values[start + length - 1U]);
                window -= static_cast<WideInt>(values[start - 1U]);
                best = std::max(best, window);
            }
            best_sums.push_back(best);
        }

        prefix_hull.build(prefix_sums);
        suffix_hull.build(suffix_sums);
        best_hull.build(best_sums);
    }
};

}  // namespace

struct TransactionEngine::Node {
    Node(std::vector<std::int64_t> initial_values,
         std::uint64_t priority_value)
        : priority(priority_value),
          block(std::move(initial_values)),
          aggregate(block.stats()),
          subtree_length(block.size()) {}

    Node* left = nullptr;
    Node* right = nullptr;
    std::uint64_t priority;
    Block block;
    Stats aggregate;
    std::size_t subtree_length;
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
    return node == nullptr ? 0U : node->subtree_length;
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
    result = combine(result, node->block.stats());
    if (node->right != nullptr) result = combine(result, node->right->aggregate);

    node->aggregate = result;
    node->subtree_length = node_size(node->left) + node->block.size() +
                           node_size(node->right);
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

TransactionEngine::Node* TransactionEngine::insert_at(
    Node* root, std::size_t position, std::int64_t value) {
    if (root == nullptr) {
        std::vector<std::int64_t> initial{value};
        return new Node(std::move(initial), next_priority());
    }

    const std::size_t left_count = node_size(root->left);
    const std::size_t block_count = root->block.size();

    if (position < left_count) {
        root->left = insert_at(root->left, position, value);
        pull(root);
        return root;
    }
    if (position > left_count + block_count) {
        root->right = insert_at(root->right,
                                position - left_count - block_count, value);
        pull(root);
        return root;
    }

    const std::size_t inside_position = position - left_count;
    root->block.insert(inside_position, value);
    if (root->block.size() <= kMaxBlockSize) {
        pull(root);
        return root;
    }

    const std::size_t split_position = root->block.size() / 2U;
    std::vector<std::int64_t> left_values(
        root->block.values.begin(),
        root->block.values.begin() + static_cast<std::ptrdiff_t>(split_position));
    std::vector<std::int64_t> right_values(
        root->block.values.begin() + static_cast<std::ptrdiff_t>(split_position),
        root->block.values.end());

    Node* old_left = root->left;
    Node* old_right = root->right;
    root->left = nullptr;
    root->right = nullptr;
    delete root;

    Node* left_block = new Node(std::move(left_values), next_priority());
    Node* right_block = new Node(std::move(right_values), next_priority());
    return merge(merge(old_left, left_block), merge(right_block, old_right));
}

void TransactionEngine::insert_transaction(std::size_t position,
                                           std::int64_t value) {
    if (position > size()) {
        throw std::out_of_range("insert position must be in [0, size]");
    }
    root_ = insert_at(root_, position, value);
}

void TransactionEngine::validate_subtree_add(const Node* node,
                                              std::int64_t delta) const {
    if (node == nullptr) return;
    node->block.validate_full_add(delta);
    validate_subtree_add(node->left, delta);
    validate_subtree_add(node->right, delta);
}

void TransactionEngine::validate_add_range(const Node* node,
                                            std::size_t subtree_start,
                                            std::size_t left,
                                            std::size_t right,
                                            std::int64_t delta) const {
    if (node == nullptr) return;
    const std::size_t subtree_end = subtree_start + node_size(node) - 1U;
    if (right < subtree_start || left > subtree_end) return;
    if (left <= subtree_start && subtree_end <= right) {
        validate_subtree_add(node, delta);
        return;
    }

    const std::size_t left_count = node_size(node->left);
    const std::size_t block_start = subtree_start + left_count;
    const std::size_t block_end = block_start + node->block.size() - 1U;

    validate_add_range(node->left, subtree_start, left, right, delta);

    if (!(right < block_start || left > block_end)) {
        const std::size_t local_left =
            std::max(left, block_start) - block_start;
        const std::size_t local_right =
            std::min(right, block_end) - block_start;

        if (local_left == 0U && local_right + 1U == node->block.size()) {
            node->block.validate_full_add(delta);
        } else {
            for (std::size_t i = local_left; i <= local_right; ++i) {
                const WideInt updated =
                    static_cast<WideInt>(node->block.values[i]) +
                    node->block.lazy + static_cast<WideInt>(delta);
                if (!fits_int64(updated)) {
                    throw std::overflow_error(
                        "transaction value would overflow int64_t");
                }
            }
        }
    }

    validate_add_range(node->right, block_end + 1U, left, right, delta);
}

void TransactionEngine::apply_add_subtree(Node* node, std::int64_t delta) {
    if (node == nullptr) return;
    node->block.lazy += static_cast<WideInt>(delta);
    apply_add_subtree(node->left, delta);
    apply_add_subtree(node->right, delta);
    pull(node);
}

void TransactionEngine::apply_add_range(Node* node,
                                         std::size_t subtree_start,
                                         std::size_t left,
                                         std::size_t right,
                                         std::int64_t delta) {
    if (node == nullptr) return;
    const std::size_t subtree_end = subtree_start + node_size(node) - 1U;
    if (right < subtree_start || left > subtree_end) return;
    if (left <= subtree_start && subtree_end <= right) {
        apply_add_subtree(node, delta);
        return;
    }

    const std::size_t left_count = node_size(node->left);
    const std::size_t block_start = subtree_start + left_count;
    const std::size_t block_end = block_start + node->block.size() - 1U;

    apply_add_range(node->left, subtree_start, left, right, delta);

    if (!(right < block_start || left > block_end)) {
        const std::size_t local_left =
            std::max(left, block_start) - block_start;
        const std::size_t local_right =
            std::min(right, block_end) - block_start;

        if (local_left == 0U && local_right + 1U == node->block.size()) {
            node->block.lazy += static_cast<WideInt>(delta);
        } else {
            node->block.add_partial(local_left, local_right, delta);
        }
    }

    apply_add_range(node->right, block_end + 1U, left, right, delta);
    pull(node);
}

void TransactionEngine::bulk_adjust_balance(std::size_t left_index,
                                            std::size_t right_index,
                                            std::int64_t delta) {
    if (left_index > right_index || right_index >= size()) {
        throw std::out_of_range("update range must be a valid inclusive range");
    }
    if (delta == 0) return;

    // Validate the entire operation before mutating anything.
    validate_add_range(root_, 0U, left_index, right_index, delta);
    apply_add_range(root_, 0U, left_index, right_index, delta);
}

WideInt TransactionEngine::get_max_profit_period(std::size_t left_index,
                                                  std::size_t right_index) {
    if (left_index > right_index || right_index >= size()) {
        throw std::out_of_range("query range must be a valid inclusive range");
    }

    const auto query = [&](const auto& self, const Node* node,
                           std::size_t subtree_start) -> Stats {
        if (node == nullptr) return Stats{};

        const std::size_t subtree_end = subtree_start + node_size(node) - 1U;
        if (right_index < subtree_start || left_index > subtree_end) {
            return Stats{};
        }
        if (left_index <= subtree_start && subtree_end <= right_index) {
            return node->aggregate;
        }

        const std::size_t left_count = node_size(node->left);
        const std::size_t block_start = subtree_start + left_count;
        const std::size_t block_end = block_start + node->block.size() - 1U;

        Stats result = self(self, node->left, subtree_start);

        if (!(right_index < block_start || left_index > block_end)) {
            const std::size_t local_left =
                std::max(left_index, block_start) - block_start;
            const std::size_t local_right =
                std::min(right_index, block_end) - block_start;

            if (local_left == 0U &&
                local_right + 1U == node->block.size()) {
                result = combine(result, node->block.stats());
            } else {
                for (std::size_t i = local_left; i <= local_right; ++i) {
                    result = combine(result,
                                     singleton(node->block.actual_value(i)));
                }
            }
        }

        result = combine(result,
                         self(self, node->right, block_end + 1U));
        return result;
    };

    return query(query, root_, 0U).best;
}

std::size_t TransactionEngine::size() const noexcept {
    return node_size(root_);
}

}  // namespace transaction_engine
