#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <random>
#include <vector>

namespace {

using Wide = __int128_t;

struct PrefixPoint {
    std::size_t index = 0;  // Prefix length k; point is (k, sum(A[0..k)).
    Wide sum = 0;
};

std::vector<PrefixPoint> compute_prefix_points(
    const std::vector<std::int64_t>& values) {
    std::vector<PrefixPoint> points;
    points.reserve(values.size() + 1U);
    Wide sum = 0;
    points.push_back(PrefixPoint{0U, 0});
    for (std::size_t k = 1U; k <= values.size(); ++k) {
        sum += static_cast<Wide>(values[k - 1U]);
        points.push_back(PrefixPoint{k, sum});
    }
    return points;
}

// If A[left..right] += delta, a prefix point P_k transforms as:
//   k <= left:       unchanged
//   left < k <= r+1: y += delta*(k-left)       (affine shear)
//   k > right+1:     y += delta*(right-left+1) (vertical shift)
void transform_prefix_points_for_range_add(
    std::vector<PrefixPoint>& points,
    std::size_t left,
    std::size_t right,
    std::int64_t delta) {
    const Wide d = static_cast<Wide>(delta);
    const std::size_t end_prefix = right + 1U;
    const Wide full_shift =
        d * static_cast<Wide>(right - left + 1U);

    for (PrefixPoint& point : points) {
        if (point.index <= left) continue;
        if (point.index <= end_prefix) {
            point.sum += d * static_cast<Wide>(point.index - left);
        } else {
            point.sum += full_shift;
        }
    }
}

// Insert value v before array position p. Prefix points P_0..P_p remain as
// points of the new prefix sequence. Every old point P_k for k >= p also
// creates a translated successor (k+1, P_k+v). The point at k=p is duplicated:
// it is both the last unchanged prefix and the predecessor of the inserted
// value. This is a geometric way to express insertion without rescanning sums.
void transform_prefix_points_for_insert(
    std::vector<PrefixPoint>& points,
    std::size_t position,
    std::int64_t value) {
    std::vector<PrefixPoint> transformed;
    transformed.reserve(points.size() + 1U);
    const Wide v = static_cast<Wide>(value);
    for (const PrefixPoint& point : points) {
        if (point.index <= position) transformed.push_back(point);
        if (point.index >= position) {
            transformed.push_back(PrefixPoint{point.index + 1U, point.sum + v});
        }
    }
    points = std::move(transformed);
}

bool equal_points(const std::vector<PrefixPoint>& a,
                  const std::vector<PrefixPoint>& b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (a[i].index != b[i].index || a[i].sum != b[i].sum) return false;
    }
    return true;
}

void fail(int step, const char* operation) {
    std::cerr << "prefix affine model mismatch at step " << step
              << " after " << operation << '\n';
}

bool test_deterministic_examples() {
    std::vector<std::int64_t> values{10, -5, 20, 7};
    auto model = compute_prefix_points(values);

    values.insert(values.begin() + 2, 99);
    transform_prefix_points_for_insert(model, 2U, 99);
    if (!equal_points(model, compute_prefix_points(values))) {
        fail(-1, "middle insertion");
        return false;
    }

    for (std::size_t i = 1U; i <= 4U; ++i) values[i] += 6;
    transform_prefix_points_for_range_add(model, 1U, 4U, 6);
    if (!equal_points(model, compute_prefix_points(values))) {
        fail(-2, "cross-range addition");
        return false;
    }
    return true;
}

bool test_randomized_affine_model() {
    std::mt19937_64 random(0xAFF1AE5EEDULL);
    std::vector<std::int64_t> values;
    for (std::size_t i = 0; i < 37U; ++i) {
        values.push_back(static_cast<std::int64_t>(random() % 101U) - 50);
    }
    auto model = compute_prefix_points(values);

    constexpr int operations = 2500;
    for (int step = 0; step < operations; ++step) {
        const unsigned choice = static_cast<unsigned>(random() % 100U);
        if ((choice < 22U && values.size() < 180U) || values.empty()) {
            const std::size_t position = static_cast<std::size_t>(
                random() % static_cast<std::uint64_t>(values.size() + 1U));
            const auto value =
                static_cast<std::int64_t>(random() % 101U) - 50;
            values.insert(values.begin() + static_cast<std::ptrdiff_t>(position), value);
            transform_prefix_points_for_insert(model, position, value);

            if (!equal_points(model, compute_prefix_points(values))) {
                fail(step, "random insert");
                return false;
            }
        } else {
            std::size_t left = static_cast<std::size_t>(
                random() % static_cast<std::uint64_t>(values.size()));
            std::size_t right = static_cast<std::size_t>(
                random() % static_cast<std::uint64_t>(values.size()));
            if (left > right) std::swap(left, right);
            const auto delta =
                static_cast<std::int64_t>(random() % 21U) - 10;

            for (std::size_t i = left; i <= right; ++i) values[i] += delta;
            transform_prefix_points_for_range_add(model, left, right, delta);

            if (!equal_points(model, compute_prefix_points(values))) {
                fail(step, "random range addition");
                return false;
            }
        }
    }
    std::cout << "Prefix-sum affine model passed " << operations
              << " randomized insert/update operations.\n";
    return true;
}

}  // namespace

int main() {
    if (!test_deterministic_examples()) return 1;
    if (!test_randomized_affine_model()) return 1;
    return 0;
}
