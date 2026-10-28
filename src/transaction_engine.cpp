#include "transaction_engine.hpp"

#include <algorithm>
#include <limits>
#include <stdexcept>
#include <utility>
#include <vector>

namespace transaction_engine {
namespace {

constexpr WideInt kNegativeInfinity = -(static_cast<WideInt>(1) << 120);
constexpr WideInt kPositiveInfinity = -kNegativeInfinity;
#ifndef TRANSACTION_ENGINE_BLOCK_SIZE
#define TRANSACTION_ENGINE_BLOCK_SIZE 512
#endif

constexpr std::size_t kMaxBlockSize = TRANSACTION_ENGINE_BLOCK_SIZE;
static_assert(kMaxBlockSize >= 2U && kMaxBlockSize <= 65535U,
              "block size must fit the uint16_t hull coordinate type");

struct ShiftBounds {
    WideInt low = kNegativeInfinity;
    WideInt high = kPositiveInfinity;
};

WideInt floor_div(WideInt numerator, WideInt denominator) noexcept {
    WideInt quotient = numerator / denominator;
    if (numerator % denominator < 0) --quotient;
    return quotient;
}

WideInt ceil_div(WideInt numerator, WideInt denominator) noexcept {
    WideInt quotient = numerator / denominator;
    if (numerator % denominator > 0) ++quotient;
    return quotient;
}

ShiftBounds intersect_bounds(ShiftBounds left, ShiftBounds right) noexcept {
    return ShiftBounds{std::max(left.low, right.low),
                       std::min(left.high, right.high)};
}

// Integer shifts for which the selected line stays above a competitor.
// A strict comparison is needed when the competitor wins ties by priority.
ShiftBounds line_dominance_bounds(WideInt winner_value,
                                  std::size_t winner_length,
                                  WideInt other_value,
                                  std::size_t other_length,
                                  bool strict) noexcept {
    const WideInt value_difference = winner_value - other_value;
    const WideInt slope_difference =
        static_cast<WideInt>(winner_length) -
        static_cast<WideInt>(other_length);

    if (slope_difference > 0) {
        const WideInt low = strict
            ? floor_div(-value_difference, slope_difference) + 1
            : ceil_div(-value_difference, slope_difference);
        return ShiftBounds{low, kPositiveInfinity};
    }
    if (slope_difference < 0) {
        const WideInt positive_slope = -slope_difference;
        const WideInt high = strict
            ? ceil_div(value_difference, positive_slope) - 1
            : floor_div(value_difference, positive_slope);
        return ShiftBounds{kNegativeInfinity, high};
    }

    const bool wins_at_all_shifts =
        strict ? value_difference > 0 : value_difference >= 0;
    return wins_at_all_shifts
        ? ShiftBounds{}
        : ShiftBounds{kPositiveInfinity, kNegativeInfinity};
}

struct Stats {
    std::size_t length = 0U;
    WideInt sum = 0;
    WideInt prefix = kNegativeInfinity;
    std::size_t prefix_length = 0U;
    WideInt suffix = kNegativeInfinity;
    std::size_t suffix_length = 0U;
    WideInt best = kNegativeInfinity;
    std::size_t best_length = 0U;
};

struct Stability {
    ShiftBounds prefix;
    ShiftBounds suffix;
    ShiftBounds best;
};

Stats singleton(std::int64_t value) noexcept {
    const WideInt v = static_cast<WideInt>(value);
    Stats result;
    result.length = 1U;
    result.sum = v;
    result.prefix = v;
    result.prefix_length = 1U;
    result.suffix = v;
    result.suffix_length = 1U;
    result.best = v;
    result.best_length = 1U;
    return result;
}

Stats combine(const Stats& left, const Stats& right) noexcept {
    if (left.length == 0U) return right;
    if (right.length == 0U) return left;

    Stats result;
    result.length = left.length + right.length;
    result.sum = left.sum + right.sum;

    const WideInt prefix_cross = left.sum + right.prefix;
    const std::size_t prefix_cross_length = left.length + right.prefix_length;
    if (left.prefix >= prefix_cross) {
        result.prefix = left.prefix;
        result.prefix_length = left.prefix_length;
    } else {
        result.prefix = prefix_cross;
        result.prefix_length = prefix_cross_length;
    }

    const WideInt suffix_cross = left.suffix + right.sum;
    const std::size_t suffix_cross_length = left.suffix_length + right.length;
    if (right.suffix >= suffix_cross) {
        result.suffix = right.suffix;
        result.suffix_length = right.suffix_length;
    } else {
        result.suffix = suffix_cross;
        result.suffix_length = suffix_cross_length;
    }

    const WideInt candidates[3] = {
        left.best, right.best, left.suffix + right.prefix};
    const std::size_t lengths[3] = {
        left.best_length, right.best_length,
        left.suffix_length + right.prefix_length};
    std::size_t winner = 0U;
    for (std::size_t i = 1U; i < 3U; ++i) {
        if (candidates[i] > candidates[winner]) winner = i;
    }
    result.best = candidates[winner];
    result.best_length = lengths[winner];
    return result;
}

Stability combine_stability(const Stats& left, const Stability& left_stability,
                            const Stats& right, const Stability& right_stability,
                            const Stats& result) noexcept {
    if (left.length == 0U) return right_stability;
    if (right.length == 0U) return left_stability;

    Stability stable;
    const WideInt prefix_cross = left.sum + right.prefix;
    const std::size_t prefix_cross_length = left.length + right.prefix_length;
    const bool prefix_left_wins = left.prefix >= prefix_cross;
    stable.prefix = intersect_bounds(left_stability.prefix,
                                     right_stability.prefix);
    stable.prefix = intersect_bounds(
        stable.prefix,
        line_dominance_bounds(
            result.prefix, result.prefix_length,
            prefix_left_wins ? prefix_cross : left.prefix,
            prefix_left_wins ? prefix_cross_length : left.prefix_length,
            !prefix_left_wins));

    const WideInt suffix_cross = left.suffix + right.sum;
    const std::size_t suffix_cross_length = left.suffix_length + right.length;
    const bool suffix_right_wins = right.suffix >= suffix_cross;
    stable.suffix = intersect_bounds(left_stability.suffix,
                                     right_stability.suffix);
    stable.suffix = intersect_bounds(
        stable.suffix,
        line_dominance_bounds(
            result.suffix, result.suffix_length,
            suffix_right_wins ? suffix_cross : right.suffix,
            suffix_right_wins ? suffix_cross_length : right.suffix_length,
            !suffix_right_wins));

    const WideInt candidates[3] = {
        left.best, right.best, left.suffix + right.prefix};
    const std::size_t lengths[3] = {
        left.best_length, right.best_length,
        left.suffix_length + right.prefix_length};
    std::size_t winner = 0U;
    for (std::size_t i = 1U; i < 3U; ++i) {
        if (candidates[i] > candidates[winner]) winner = i;
    }

    stable.best = intersect_bounds(left_stability.best,
                                   right_stability.best);
    stable.best = intersect_bounds(
        stable.best,
        intersect_bounds(left_stability.suffix, right_stability.prefix));
    for (std::size_t i = 0U; i < 3U; ++i) {
        if (i == winner) continue;
        stable.best = intersect_bounds(
            stable.best,
            line_dominance_bounds(
                result.best, result.best_length, candidates[i], lengths[i],
                winner > i));
    }
    return stable;
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
    mutable bool cache_valid = false;
    mutable WideInt cached_delta = 0;
    mutable std::size_t cached_index = 0;
    // A uniform vertical shift does not change the hull geometry or support
    // point. Keeping it separately makes suffix-hull appends O(1).
    WideInt y_offset = 0;

    static WideInt cross(std::uint16_t ax, WideInt ay,
                         std::uint16_t bx, WideInt by,
                         std::uint16_t cx, WideInt cy) noexcept {
        const WideInt abx = static_cast<WideInt>(bx - ax);
        const WideInt bcx = static_cast<WideInt>(cx - bx);
        return abx * (cy - by) - (by - ay) * bcx;
    }

    void append_point(std::uint16_t new_x, WideInt actual_y) {
        const WideInt new_y = actual_y - y_offset;
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
        cache_valid = false;
    }

    void add_vertical(WideInt amount) noexcept {
        y_offset += amount;
    }

    void reserve_points(std::size_t count) {
        x.reserve(count);
        y.reserve(count);
    }

    void build(const std::vector<WideInt>& sums_by_length) {
        x.clear();
        y.clear();
        y_offset = 0;
        cache_valid = false;
        cached_delta = 0;
        cached_index = 0;
        x.reserve(sums_by_length.size());
        y.reserve(sums_by_length.size());

        for (std::size_t i = 0; i < sums_by_length.size(); ++i) {
            append_point(static_cast<std::uint16_t>(i + 1U), sums_by_length[i]);
        }
    }

    // Suffix candidates are indexed by their start position, not their length.
    // This makes appending a value translate all existing y coordinates and
    // add a single rightmost point, without rebuilding the entire hull.
    void build_suffix(const std::vector<WideInt>& sums_by_length) {
        x.clear();
        y.clear();
        y_offset = 0;
        cache_valid = false;
        cached_delta = 0;
        cached_index = 0;
        x.reserve(sums_by_length.size());
        y.reserve(sums_by_length.size());

        const std::size_t count = sums_by_length.size();
        for (std::size_t length = count; length > 0U; --length) {
            append_point(static_cast<std::uint16_t>(count - length + 1U),
                         sums_by_length[length - 1U]);
        }
    }

    WideInt max_plus(WideInt delta) const noexcept {
        if (x.empty()) return kNegativeInfinity;

        const auto value_at = [&](std::size_t index) noexcept {
            return y[index] + y_offset +
                   delta * static_cast<WideInt>(x[index]);
        };
        const auto binary_search = [&]() noexcept {
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
            return low;
        };

        std::size_t best = 0;
        if (!cache_valid || cached_index >= x.size()) {
            best = binary_search();
        } else if (delta == cached_delta) {
            best = cached_index;
        } else if (delta > cached_delta) {
            // The support point moves only to the right as delta increases.
            // Most small shifts move it a few places; cap the walk and fall
            // back to binary search when it is not locally small.
            best = cached_index;
            std::size_t steps = 0;
            while (best + 1U < x.size() &&
                   value_at(best + 1U) >= value_at(best) && steps < 8U) {
                ++best;
                ++steps;
            }
            if (best + 1U < x.size() &&
                value_at(best + 1U) >= value_at(best)) {
                best = binary_search();
            }
        } else {
            // For a decreasing delta the support point moves to the left.
            // Strict comparison keeps the rightmost point on a tie, matching
            // the binary-search path.
            best = cached_index;
            std::size_t steps = 0;
            while (best > 0U && value_at(best - 1U) > value_at(best) &&
                   steps < 8U) {
                --best;
                ++steps;
            }
            if (best > 0U && value_at(best - 1U) > value_at(best)) {
                best = binary_search();
            }
        }

        cache_valid = true;
        cached_delta = delta;
        cached_index = best;
        return value_at(best);
    }

    // Integer perturbations of delta for which the selected hull vertex stays
    // optimal. On equal support values the rightmost vertex wins.
    ShiftBounds support_shift_bounds(WideInt delta) const noexcept {
        if (x.empty()) return ShiftBounds{};
        static_cast<void>(max_plus(delta));
        const std::size_t index = cached_index;
        ShiftBounds bounds;
        if (index > 0U) {
            const WideInt denominator =
                static_cast<WideInt>(x[index] - x[index - 1U]);
            const WideInt numerator = y[index - 1U] - y[index];
            bounds.low = ceil_div(numerator, denominator) - delta;
        }
        if (index + 1U < x.size()) {
            const WideInt denominator =
                static_cast<WideInt>(x[index + 1U] - x[index]);
            const WideInt numerator = y[index] - y[index + 1U];
            bounds.high = ceil_div(numerator, denominator) - 1 - delta;
        }
        return bounds;
    }
};

struct Block {
    explicit Block(std::vector<std::int64_t> initial)
        : values(std::move(initial)) {
        // stats() is used by noexcept treap pulls. Reserve the full block
        // capacity now so lazy best-hull materialization never allocates.
        best_hull.reserve_points(kMaxBlockSize);
        rebuild();
    }

    std::vector<std::int64_t> values;  // Base values; actual = base + lazy.
    WideInt lazy = 0;
    WideInt base_sum = 0;
    std::int64_t min_value = 0;
    std::int64_t max_value = 0;
    Hull prefix_hull;
    Hull suffix_hull;
    mutable Hull best_hull;
    mutable bool best_hull_dirty = false;
    WideInt best_zero = kNegativeInfinity;
    std::size_t best_zero_length = 0U;
    mutable bool best_zero_bounds_valid = false;
    mutable ShiftBounds best_zero_bounds;
    // Reuse scratch buffers across block rebuilds to avoid three heap
    // allocations on every insertion/update of a block.
    std::vector<WideInt> prefix_sums;
    std::vector<WideInt> suffix_sums;
    std::vector<WideInt> best_sums;

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
        result.prefix_length =
            static_cast<std::size_t>(prefix_hull.x[prefix_hull.cached_index]);

        const WideInt suffix_delta = -lazy;
        result.suffix = suffix_hull.max_plus(suffix_delta) +
                        lazy * static_cast<WideInt>(values.size() + 1U);
        result.suffix_length = values.size() + 1U -
            static_cast<std::size_t>(suffix_hull.x[suffix_hull.cached_index]);

        if (lazy == 0) {
            result.best = best_zero;
            result.best_length = best_zero_length;
        } else {
            ensure_best_hull();
            result.best = best_hull.max_plus(lazy);
            result.best_length =
                static_cast<std::size_t>(best_hull.x[best_hull.cached_index]);
        }
        return result;
    }

    void ensure_best_hull() const {
        if (!best_hull_dirty) return;
        best_hull.build(best_sums);
        best_hull_dirty = false;
    }

    ShiftBounds best_zero_shift_bounds() const noexcept {
        if (best_zero_bounds_valid) return best_zero_bounds;
        ShiftBounds bounds;
        if (best_zero_length != 0U) {
            for (std::size_t i = 0U; i < best_sums.size(); ++i) {
                const std::size_t length = i + 1U;
                if (length == best_zero_length) continue;
                bounds = intersect_bounds(
                    bounds,
                    line_dominance_bounds(
                        best_zero, best_zero_length, best_sums[i], length,
                        length < best_zero_length));
            }
        }
        best_zero_bounds = bounds;
        best_zero_bounds_valid = true;
        return bounds;
    }

    Stability stability() const noexcept {
        Stability stable;
        stable.prefix = prefix_hull.support_shift_bounds(lazy);
        const ShiftBounds suffix_shift =
            suffix_hull.support_shift_bounds(-lazy);
        stable.suffix = ShiftBounds{-suffix_shift.high, -suffix_shift.low};
        if (lazy == 0) {
            stable.best = best_zero_shift_bounds();
        } else {
            ensure_best_hull();
            stable.best = best_hull.support_shift_bounds(lazy);
        }
        return stable;
    }

    std::int64_t min_actual() const noexcept {
        return static_cast<std::int64_t>(static_cast<WideInt>(min_value) + lazy);
    }

    std::int64_t max_actual() const noexcept {
        return static_cast<std::int64_t>(static_cast<WideInt>(max_value) + lazy);
    }

    void validate_full_add(WideInt delta) const {
        const WideInt shift = lazy + delta;
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
        // Appending is a common hot path while the initial sequence is built.
        // When there is no lazy offset, all old windows remain valid; the only
        // new candidate for each length is the suffix ending at the new value.
        // Updating the three per-length summaries therefore takes O(B), not
        // O(B^2). Middle insertions still rebuild the block from its values.
        if (lazy == 0 && position == values.size()) {
            const std::size_t old_size = values.size();
            const WideInt v = static_cast<WideInt>(value);
            values.push_back(value);

            base_sum += v;
            min_value = std::min(min_value, value);
            max_value = std::max(max_value, value);
            prefix_sums.push_back(base_sum);
            prefix_hull.append_point(
                static_cast<std::uint16_t>(old_size + 1U), base_sum);

            suffix_sums.resize(old_size + 1U);
            for (std::size_t i = old_size; i > 0U; --i) {
                suffix_sums[i] = suffix_sums[i - 1U] + v;
            }
            suffix_sums[0] = v;
            suffix_hull.add_vertical(v);
            suffix_hull.append_point(
                static_cast<std::uint16_t>(old_size + 1U), v);

            best_sums.resize(old_size + 1U);
            for (std::size_t i = 0; i < old_size; ++i) {
                if (suffix_sums[i] > best_sums[i]) {
                    best_sums[i] = suffix_sums[i];
                }
                const std::size_t candidate_length = i + 1U;
                if (best_sums[i] > best_zero ||
                    (best_sums[i] == best_zero &&
                     (best_zero_length == 0U ||
                      candidate_length < best_zero_length))) {
                    best_zero = best_sums[i];
                    best_zero_length = candidate_length;
                }
            }
            best_sums[old_size] = suffix_sums[old_size];
            if (best_sums[old_size] > best_zero ||
                (best_sums[old_size] == best_zero &&
                 (best_zero_length == 0U ||
                  old_size + 1U < best_zero_length))) {
                best_zero = best_sums[old_size];
                best_zero_length = old_size + 1U;
            }
            // The exact zero-shift maximum is maintained incrementally.
            // Build the full support hull only if a nonzero lazy shift later
            // needs it; this avoids rebuilding one hull per append.
            best_hull_dirty = true;
            best_zero_bounds_valid = false;
            return;
        }

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
            best_hull_dirty = false;
            best_zero = kNegativeInfinity;
            best_zero_length = 0U;
            best_zero_bounds_valid = false;
            lazy = 0;
            return;
        }

        // Rebuild is called after materialization for every mutation.
        lazy = 0;
        min_value = max_value = values.front();
        base_sum = 0;
        prefix_sums.clear();
        suffix_sums.clear();
        best_sums.assign(values.size(), kNegativeInfinity);
        prefix_sums.reserve(values.size());
        suffix_sums.reserve(values.size());

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

        // Enumerate each contiguous window once, updating the best sum for
        // its length. This halves the inner-loop work versus sliding once
        // per target length.
        for (std::size_t start = 0; start < values.size(); ++start) {
            WideInt window = 0;
            for (std::size_t end = start; end < values.size(); ++end) {
                window += static_cast<WideInt>(values[end]);
                const std::size_t length = end - start;
                best_sums[length] = std::max(best_sums[length], window);
            }
        }

        prefix_hull.build(prefix_sums);
        suffix_hull.build_suffix(suffix_sums);
        best_hull.build(best_sums);
        best_hull_dirty = false;
        const auto best_it =
            std::max_element(best_sums.begin(), best_sums.end());
        best_zero = *best_it;
        best_zero_length =
            static_cast<std::size_t>(best_it - best_sums.begin()) + 1U;
        best_zero_bounds_valid = false;
    }
};

}  // namespace

struct TransactionEngine::Node {
    Node(std::vector<std::int64_t> initial_values,
         std::uint64_t priority_value)
        : priority(priority_value),
          block(std::move(initial_values)),
          block_aggregate(block.stats()),
          aggregate(block_aggregate),
          subtree_min(block.min_actual()),
          subtree_max(block.max_actual()),
          subtree_length(block.size()) {}

    Node* left = nullptr;
    Node* right = nullptr;
    std::uint64_t priority;
    Block block;
    Stats block_aggregate;
    Stats aggregate;
    std::int64_t subtree_min;
    std::int64_t subtree_max;
    std::size_t subtree_length;
    WideInt pending_add = 0;
    Stability aggregate_stability;
    bool aggregate_stability_valid = false;
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

void TransactionEngine::pull_aggregate(Node* node) noexcept {
    if (node == nullptr) return;
    node->aggregate_stability_valid = false;

    Stats result;
    if (node->left != nullptr) result = node->left->aggregate;
    result = combine(result, node->block_aggregate);
    if (node->right != nullptr) result = combine(result, node->right->aggregate);
    node->aggregate = result;
}

void TransactionEngine::pull(Node* node) noexcept {
    if (node == nullptr) return;
    pull_aggregate(node);

    node->subtree_min = node->block.min_actual();
    node->subtree_max = node->block.max_actual();
    if (node->left != nullptr) {
        node->subtree_min = std::min(node->subtree_min, node->left->subtree_min);
        node->subtree_max = std::max(node->subtree_max, node->left->subtree_max);
    }
    if (node->right != nullptr) {
        node->subtree_min = std::min(node->subtree_min, node->right->subtree_min);
        node->subtree_max = std::max(node->subtree_max, node->right->subtree_max);
    }
    node->subtree_length = node_size(node->left) + node->block.size() +
                           node_size(node->right);
}

void TransactionEngine::push_lazy(Node* node) noexcept {
    if (node == nullptr || node->pending_add == 0) return;
    const WideInt delta = node->pending_add;
    node->pending_add = 0;

    apply_add_subtree(node->left, delta);
    node->block.lazy += delta;
    node->block_aggregate = node->block.stats();
    apply_add_subtree(node->right, delta);
    pull(node);
}

TransactionEngine::Node* TransactionEngine::merge(Node* left,
                                                   Node* right) noexcept {
    if (left == nullptr) return right;
    if (right == nullptr) return left;

    if (left->priority > right->priority) {
        push_lazy(left);
        left->right = merge(left->right, right);
        pull(left);
        return left;
    }
    push_lazy(right);
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
    push_lazy(root);

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
    root->block_aggregate = root->block.stats();
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

void TransactionEngine::validate_add_range(const Node* node,
                                            std::size_t subtree_start,
                                            std::size_t left,
                                            std::size_t right,
                                            std::int64_t delta,
                                            WideInt inherited_add) const {
    if (node == nullptr) return;
    const std::size_t subtree_end = subtree_start + node_size(node) - 1U;
    if (right < subtree_start || left > subtree_end) return;
    if (left <= subtree_start && subtree_end <= right) {
        const WideInt new_min =
            static_cast<WideInt>(node->subtree_min) + inherited_add + delta;
        const WideInt new_max =
            static_cast<WideInt>(node->subtree_max) + inherited_add + delta;
        if (!fits_int64(new_min) || !fits_int64(new_max)) {
            throw std::overflow_error("transaction value would overflow int64_t");
        }
        return;
    }

    const std::size_t left_count = node_size(node->left);
    const std::size_t block_start = subtree_start + left_count;
    const std::size_t block_end = block_start + node->block.size() - 1U;
    const WideInt child_inherited = inherited_add + node->pending_add;

    validate_add_range(node->left, subtree_start, left, right, delta,
                       child_inherited);

    if (!(right < block_start || left > block_end)) {
        const std::size_t local_left =
            std::max(left, block_start) - block_start;
        const std::size_t local_right =
            std::min(right, block_end) - block_start;
        const WideInt effective_shift =
            inherited_add + node->pending_add + delta;

        if (local_left == 0U && local_right + 1U == node->block.size()) {
            node->block.validate_full_add(effective_shift);
        } else {
            for (std::size_t i = local_left; i <= local_right; ++i) {
                const WideInt updated =
                    static_cast<WideInt>(node->block.values[i]) +
                    node->block.lazy + effective_shift;
                if (!fits_int64(updated)) {
                    throw std::overflow_error(
                        "transaction value would overflow int64_t");
                }
            }
        }
    }

    validate_add_range(node->right, block_end + 1U, left, right, delta,
                       child_inherited);
}

void TransactionEngine::apply_add_subtree(Node* node,
                                          WideInt delta) noexcept {
    if (node == nullptr || delta == 0) return;

    const auto get_stability = [&](const auto& self, Node* current) -> Stability {
        if (current == nullptr) return Stability{};
        if (current->aggregate_stability_valid) {
            return current->aggregate_stability;
        }

        const Stats empty_stats{};
        const Stats left_stats = current->left != nullptr
            ? current->left->aggregate : empty_stats;
        const Stability left_stability =
            self(self, current->left);
        const Stats block_stats = current->block_aggregate;
        const Stability block_stability = current->block.stability();

        const Stats left_and_block = combine(left_stats, block_stats);
        const Stability left_and_block_stability = combine_stability(
            left_stats, left_stability, block_stats, block_stability,
            left_and_block);

        const Stats right_stats = current->right != nullptr
            ? current->right->aggregate : empty_stats;
        const Stability right_stability =
            self(self, current->right);
        current->aggregate_stability = combine_stability(
            left_and_block, left_and_block_stability,
            right_stats, right_stability, current->aggregate);
        current->aggregate_stability_valid = true;
        return current->aggregate_stability;
    };

    const Stability stability = get_stability(get_stability, node);
    const bool stable =
        delta >= stability.prefix.low && delta <= stability.prefix.high &&
        delta >= stability.suffix.low && delta <= stability.suffix.high &&
        delta >= stability.best.low && delta <= stability.best.high;

    if (stable) {
        Stats& aggregate = node->aggregate;
        aggregate.sum += delta * static_cast<WideInt>(aggregate.length);
        aggregate.prefix +=
            delta * static_cast<WideInt>(aggregate.prefix_length);
        aggregate.suffix +=
            delta * static_cast<WideInt>(aggregate.suffix_length);
        aggregate.best +=
            delta * static_cast<WideInt>(aggregate.best_length);
        node->aggregate_stability.prefix.low -= delta;
        node->aggregate_stability.prefix.high -= delta;
        node->aggregate_stability.suffix.low -= delta;
        node->aggregate_stability.suffix.high -= delta;
        node->aggregate_stability.best.low -= delta;
        node->aggregate_stability.best.high -= delta;

        node->subtree_min = static_cast<std::int64_t>(
            static_cast<WideInt>(node->subtree_min) + delta);
        node->subtree_max = static_cast<std::int64_t>(
            static_cast<WideInt>(node->subtree_max) + delta);
        node->pending_add += delta;
        return;
    }

    // If a support point would change, descend and repair exact summaries.
    push_lazy(node);
    apply_add_subtree(node->left, delta);
    node->block.lazy += delta;
    node->block_aggregate = node->block.stats();
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
        apply_add_subtree(node, static_cast<WideInt>(delta));
        return;
    }

    push_lazy(node);
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
        node->block_aggregate = node->block.stats();
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
    validate_add_range(root_, 0U, left_index, right_index, delta, 0);
    apply_add_range(root_, 0U, left_index, right_index, delta);
}

WideInt TransactionEngine::get_max_profit_period(std::size_t left_index,
                                                  std::size_t right_index) {
    if (left_index > right_index || right_index >= size()) {
        throw std::out_of_range("query range must be a valid inclusive range");
    }

    const auto query = [&](const auto& self, Node* node,
                           std::size_t subtree_start) -> Stats {
        if (node == nullptr) return Stats{};

        const std::size_t subtree_end = subtree_start + node_size(node) - 1U;
        if (right_index < subtree_start || left_index > subtree_end) {
            return Stats{};
        }
        if (left_index <= subtree_start && subtree_end <= right_index) {
            return node->aggregate;
        }

        push_lazy(node);
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
                result = combine(result, node->block_aggregate);
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
