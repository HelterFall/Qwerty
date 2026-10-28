#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <limits>
#include <random>
#include <string>
#include <stdexcept>
#include <utility>
#include <vector>

namespace {

using Wide = __int128_t;
constexpr Wide kNegInf = -(static_cast<Wide>(1) << 120);

struct Point {
    std::int64_t length = 0;
    Wide sum = 0;
};

Wide cross(const Point& a, const Point& b, const Point& c) {
    const Wide dx1 = static_cast<Wide>(b.length) - a.length;
    const Wide dx2 = static_cast<Wide>(c.length) - b.length;
    return dx1 * (c.sum - b.sum) - (b.sum - a.sum) * dx2;
}

// Input is sorted by length. Duplicate lengths are coalesced by keeping
// their largest sum, then the exposed upper chain is built in linear time.
std::vector<Point> upper_hull_sorted(std::vector<Point> points) {
    std::vector<Point> unique;
    unique.reserve(points.size());
    for (const Point& point : points) {
        if (!unique.empty() && unique.back().length == point.length) {
            if (point.sum > unique.back().sum) unique.back() = point;
        } else {
            unique.push_back(point);
        }
    }

    std::vector<Point> hull;
    hull.reserve(unique.size());
    for (const Point& point : unique) {
        while (hull.size() >= 2U &&
               cross(hull[hull.size() - 2U], hull.back(), point) >= 0) {
            hull.pop_back();
        }
        hull.push_back(point);
    }
    return hull;
}

std::vector<Point> merge_three_hulls(const std::vector<Point>& a,
                                     const std::vector<Point>& b,
                                     const std::vector<Point>& c) {
    std::vector<Point> candidates;
    candidates.reserve(a.size() + b.size() + c.size());
    std::size_t i = 0;
    std::size_t j = 0;
    std::size_t k = 0;
    while (i < a.size() || j < b.size() || k < c.size()) {
        std::int64_t next_length = std::numeric_limits<std::int64_t>::max();
        if (i < a.size()) next_length = std::min(next_length, a[i].length);
        if (j < b.size()) next_length = std::min(next_length, b[j].length);
        if (k < c.size()) next_length = std::min(next_length, c[k].length);

        Wide max_sum = kNegInf;
        if (i < a.size() && a[i].length == next_length) {
            max_sum = std::max(max_sum, a[i].sum);
            ++i;
        }
        if (j < b.size() && b[j].length == next_length) {
            max_sum = std::max(max_sum, b[j].sum);
            ++j;
        }
        if (k < c.size() && c[k].length == next_length) {
            max_sum = std::max(max_sum, c[k].sum);
            ++k;
        }
        candidates.push_back(Point{next_length, max_sum});
    }
    return upper_hull_sorted(std::move(candidates));
}

Wide line_value(const Point& point, std::int64_t offset) {
    return point.sum + static_cast<Wide>(offset) * point.length;
}

// Upper-hull edge slopes are non-increasing. Therefore line_value is unimodal
// along the chain, and its maximum can be found by binary search in O(log h).
Wide support(const std::vector<Point>& hull, std::int64_t offset) {
    if (hull.empty()) return kNegInf;
    std::size_t low = 0;
    std::size_t high = hull.size() - 1U;
    while (low < high) {
        const std::size_t middle = low + (high - low) / 2U;
        if (line_value(hull[middle], offset) <=
            line_value(hull[middle + 1U], offset)) {
            low = middle + 1U;
        } else {
            high = middle;
        }
    }
    return line_value(hull[low], offset);
}

// Deliberately slow oracle used to validate binary-search support queries.
Wide support_linear(const std::vector<Point>& hull, std::int64_t offset) {
    Wide answer = kNegInf;
    for (const Point& point : hull) {
        answer = std::max(answer, line_value(point, offset));
    }
    return answer;
}

struct Slope {
    Wide dy = 0;
    Wide dx = 1;  // Always positive.
};

int compare_slope(const Slope& a, const Slope& b) {
    const Wide difference = a.dy * b.dx - b.dy * a.dx;
    return (difference > 0) - (difference < 0);
}

Slope edge_slope(const std::vector<Point>& hull, std::size_t edge) {
    return Slope{
        hull[edge + 1U].sum - hull[edge].sum,
        static_cast<Wide>(hull[edge + 1U].length) - hull[edge].length};
}

// Both edge lists are sorted by descending slope. Select their k-th combined
// edge without materializing or sorting the union.
Slope kth_slope_desc(const std::vector<Slope>& a,
                     const std::vector<Slope>& b,
                     std::size_t k) {
    const std::vector<Slope>* first = &a;
    const std::vector<Slope>* second = &b;
    if (first->size() > second->size()) std::swap(first, second);

    std::size_t low = k + 1U > second->size() ? k + 1U - second->size() : 0U;
    std::size_t high = std::min(k + 1U, first->size());
    while (low <= high) {
        const std::size_t take_first = low + (high - low) / 2U;
        const std::size_t take_second = k + 1U - take_first;

        if (take_first > 0U && take_second < second->size() &&
            compare_slope((*first)[take_first - 1U],
                          (*second)[take_second]) < 0) {
            high = take_first - 1U;
            continue;
        }
        if (take_second > 0U && take_first < first->size() &&
            compare_slope((*second)[take_second - 1U],
                          (*first)[take_first]) < 0) {
            low = take_first + 1U;
            continue;
        }

        if (take_first == 0U) return (*second)[take_second - 1U];
        if (take_second == 0U) return (*first)[take_first - 1U];
        const Slope& last_first = (*first)[take_first - 1U];
        const Slope& last_second = (*second)[take_second - 1U];
        return compare_slope(last_first, last_second) < 0
            ? last_first : last_second;
    }
    return Slope{};  // Unreachable for valid k.
}

Wide support_slope_scaled(const std::vector<Point>& hull,
                          const Slope& slope,
                          std::size_t* selected_index = nullptr) {
    if (hull.empty()) {
        if (selected_index != nullptr) *selected_index = 0U;
        return kNegInf;
    }
    const auto value = [&](std::size_t i) {
        return hull[i].sum * slope.dx -
               slope.dy * static_cast<Wide>(hull[i].length);
    };
    std::size_t low = 0U;
    std::size_t high = hull.size() - 1U;
    while (low < high) {
        const std::size_t middle = low + (high - low) / 2U;
        if (value(middle) <= value(middle + 1U)) {
            low = middle + 1U;
        } else {
            high = middle;
        }
    }
    if (selected_index != nullptr) *selected_index = low;
    return value(low);
}

Wide bridge_gap(const std::vector<Point>& left,
                const std::vector<Point>& right,
                const Slope& slope) {
    return support_slope_scaled(left, slope) -
           support_slope_scaled(right, slope);
}

// Find the upper common tangent for two upper hulls whose x/length domains
// are disjoint (left.back.x < right.front.x). The gap between their support
// lines is strictly increasing with slope because every right x exceeds every
// left x. Binary-search the combined, descending edge-slope sequence to find
// the interval containing the unique bridge slope.
std::pair<std::size_t, std::size_t> find_disjoint_upper_bridge(
    const std::vector<Point>& left,
    const std::vector<Point>& right) {
    std::vector<Slope> left_edges;
    std::vector<Slope> right_edges;
    left_edges.reserve(left.size() > 0U ? left.size() - 1U : 0U);
    right_edges.reserve(right.size() > 0U ? right.size() - 1U : 0U);
    for (std::size_t i = 0; i + 1U < left.size(); ++i) {
        left_edges.push_back(edge_slope(left, i));
    }
    for (std::size_t i = 0; i + 1U < right.size(); ++i) {
        right_edges.push_back(edge_slope(right, i));
    }

    const std::size_t edge_count = left_edges.size() + right_edges.size();
    if (edge_count == 0U) return {0U, 0U};

    std::size_t low = 0U;
    std::size_t high = edge_count;
    while (low < high) {
        const std::size_t middle = low + (high - low) / 2U;
        const Slope candidate = kth_slope_desc(left_edges, right_edges, middle);
        const Wide gap = bridge_gap(left, right, candidate);
        if (gap > 0) {
            // The common tangent slope is smaller; continue to later edges.
            low = middle + 1U;
        } else {
            high = middle;
        }
    }

    Slope probe;
    if (low == edge_count) {
        probe = kth_slope_desc(left_edges, right_edges, edge_count - 1U);
        probe.dy = probe.dy - probe.dx;  // Strictly below every edge slope.
    } else {
        const Slope current = kth_slope_desc(left_edges, right_edges, low);
        if (bridge_gap(left, right, current) == 0) {
            std::size_t left_index = 0U;
            std::size_t right_index = 0U;
            support_slope_scaled(left, current, &left_index);
            support_slope_scaled(right, current, &right_index);
            return {left_index, right_index};
        }
        if (low == 0U) {
            probe = current;
            probe.dy = probe.dy + probe.dx;  // Strictly above every edge slope.
        } else {
            const Slope previous =
                kth_slope_desc(left_edges, right_edges, low - 1U);
            probe = Slope{
                previous.dy * current.dx + current.dy * previous.dx,
                static_cast<Wide>(2) * previous.dx * current.dx};
        }
    }

    std::size_t left_index = 0U;
    std::size_t right_index = 0U;
    support_slope_scaled(left, probe, &left_index);
    support_slope_scaled(right, probe, &right_index);
    return {left_index, right_index};
}

// Given two already-convex upper chains with disjoint x domains, their union's
// upper hull is a prefix of the left chain and a suffix of the right chain.
// This vector version validates bridge selection; a persistent rope is needed
// to turn the slice/concatenate steps themselves into logarithmic-time work.
std::vector<Point> merge_disjoint_upper_chains(
    const std::vector<Point>& left,
    const std::vector<Point>& right) {
    if (left.empty()) return right;
    if (right.empty()) return left;
    if (left.back().length >= right.front().length) {
        throw std::invalid_argument("upper chains must have disjoint ordered x domains");
    }

    const auto [left_bridge, right_bridge] =
        find_disjoint_upper_bridge(left, right);
    std::vector<Point> result;
    result.reserve(left_bridge + 1U + right.size() - right_bridge);
    result.insert(result.end(), left.begin(),
                  left.begin() + static_cast<std::ptrdiff_t>(left_bridge + 1U));
    result.insert(result.end(),
                  right.begin() + static_cast<std::ptrdiff_t>(right_bridge),
                  right.end());

    // The bridge may land on a slope shared by two consecutive edges. In that
    // case a tangent endpoint can be collinear with its neighbor. Normalize
    // the joined chain to the same canonical representation as upper_hull_sorted.
    return upper_hull_sorted(std::move(result));
}

// Minkowski sum of two upper chains. The upper-chain edge slopes are
// non-increasing, so their edge lists can be merged in linear time.
std::vector<Point> minkowski_upper(const std::vector<Point>& a,
                                   const std::vector<Point>& b) {
    if (a.empty()) return b;
    if (b.empty()) return a;

    std::vector<Point> result;
    result.reserve(a.size() + b.size());
    result.push_back(Point{a.front().length + b.front().length,
                           a.front().sum + b.front().sum});

    std::size_t i = 0;
    std::size_t j = 0;
    while (i + 1U < a.size() || j + 1U < b.size()) {
        bool take_a = false;
        if (j + 1U == b.size()) {
            take_a = true;
        } else if (i + 1U == a.size()) {
            take_a = false;
        } else {
            const Wide dx_a = static_cast<Wide>(a[i + 1U].length) - a[i].length;
            const Wide dy_a = a[i + 1U].sum - a[i].sum;
            const Wide dx_b = static_cast<Wide>(b[j + 1U].length) - b[j].length;
            const Wide dy_b = b[j + 1U].sum - b[j].sum;
            take_a = dy_a * dx_b >= dy_b * dx_a;
        }

        Wide dx = 0;
        Wide dy = 0;
        if (take_a) {
            dx = static_cast<Wide>(a[i + 1U].length) - a[i].length;
            dy = a[i + 1U].sum - a[i].sum;
            ++i;
        } else {
            dx = static_cast<Wide>(b[j + 1U].length) - b[j].length;
            dy = b[j + 1U].sum - b[j].sum;
            ++j;
        }
        result.push_back(Point{
            static_cast<std::int64_t>(static_cast<Wide>(result.back().length) + dx),
            result.back().sum + dy});
    }
    return upper_hull_sorted(std::move(result));
}

struct Profile {
    std::size_t length = 0;
    Wide total = 0;
    std::vector<Point> prefix;
    std::vector<Point> suffix;
    std::vector<Point> best;
};

Profile make_profile(const std::vector<std::int64_t>& values) {
    Profile profile;
    profile.length = values.size();
    if (values.empty()) return profile;

    std::vector<Point> prefix_points;
    std::vector<Point> suffix_points;
    std::vector<Point> best_points;
    prefix_points.reserve(values.size());
    suffix_points.reserve(values.size());
    best_points.reserve(values.size());

    Wide running = 0;
    for (std::size_t length = 1; length <= values.size(); ++length) {
        running += static_cast<Wide>(values[length - 1U]);
        prefix_points.push_back(Point{static_cast<std::int64_t>(length), running});
    }
    profile.total = running;

    running = 0;
    for (std::size_t length = 1; length <= values.size(); ++length) {
        running += static_cast<Wide>(values[values.size() - length]);
        suffix_points.push_back(Point{static_cast<std::int64_t>(length), running});
    }

    std::vector<Wide> best_by_length(values.size(), kNegInf);
    for (std::size_t start = 0; start < values.size(); ++start) {
        Wide window = 0;
        for (std::size_t end = start; end < values.size(); ++end) {
            window += static_cast<Wide>(values[end]);
            const std::size_t length = end - start + 1U;
            best_by_length[length - 1U] =
                std::max(best_by_length[length - 1U], window);
        }
    }
    for (std::size_t i = 0; i < best_by_length.size(); ++i) {
        best_points.push_back(Point{static_cast<std::int64_t>(i + 1U),
                                    best_by_length[i]});
    }

    profile.prefix = upper_hull_sorted(std::move(prefix_points));
    profile.suffix = upper_hull_sorted(std::move(suffix_points));
    profile.best = upper_hull_sorted(std::move(best_points));
    return profile;
}

// Exact profile composition. A uniform add is represented by changing the
// offset supplied to support(); the profile geometry itself remains unchanged.
// This prototype is intentionally separate from production: its explicit hull
// composition is not logarithmic and is not claimed as the final algorithm.
Profile combine_profiles(const Profile& a, const Profile& b) {
    if (a.length == 0U) return b;
    if (b.length == 0U) return a;

    Profile result;
    result.length = a.length + b.length;
    result.total = a.total + b.total;

    std::vector<Point> shifted_b_prefix;
    shifted_b_prefix.reserve(b.prefix.size());
    for (const Point& point : b.prefix) {
        shifted_b_prefix.push_back(Point{
            static_cast<std::int64_t>(a.length) + point.length,
            a.total + point.sum});
    }
    result.prefix = merge_disjoint_upper_chains(a.prefix, shifted_b_prefix);

    std::vector<Point> shifted_a_suffix;
    shifted_a_suffix.reserve(a.suffix.size());
    for (const Point& point : a.suffix) {
        shifted_a_suffix.push_back(Point{
            static_cast<std::int64_t>(b.length) + point.length,
            b.total + point.sum});
    }
    result.suffix = merge_disjoint_upper_chains(b.suffix, shifted_a_suffix);

    const std::vector<Point> crossing = minkowski_upper(a.suffix, b.prefix);
    result.best = merge_three_hulls(a.best, b.best, crossing);
    return result;
}

void check(bool condition, const std::string& message, int& failures) {
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}

bool same_hull(const std::vector<Point>& a, const std::vector<Point>& b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (a[i].length != b[i].length || a[i].sum != b[i].sum) return false;
    }
    return true;
}

void print_hull(const char* label, const std::vector<Point>& hull) {
    std::cerr << label << ":";
    for (const Point& point : hull) {
        std::cerr << " (" << point.length << ","
                  << static_cast<long long>(point.sum) << ")";
    }
    std::cerr << '\n';
}

Wide brute_prefix(const std::vector<std::int64_t>& values,
                  std::int64_t offset) {
    Wide sum = 0;
    Wide best = kNegInf;
    for (const auto value : values) {
        sum += static_cast<Wide>(value) + offset;
        best = std::max(best, sum);
    }
    return best;
}

Wide brute_suffix(const std::vector<std::int64_t>& values,
                  std::int64_t offset) {
    Wide sum = 0;
    Wide best = kNegInf;
    for (std::size_t length = 1; length <= values.size(); ++length) {
        sum += static_cast<Wide>(values[values.size() - length]) + offset;
        best = std::max(best, sum);
    }
    return best;
}

Wide brute_best(const std::vector<std::int64_t>& values,
                std::int64_t offset) {
    Wide best = kNegInf;
    for (std::size_t start = 0; start < values.size(); ++start) {
        Wide sum = 0;
        for (std::size_t end = start; end < values.size(); ++end) {
            sum += static_cast<Wide>(values[end]) + offset;
            best = std::max(best, sum);
        }
    }
    return best;
}

void test_profile(const std::vector<std::int64_t>& values, int& failures) {
    const Profile profile = make_profile(values);
    for (std::int64_t offset = -250; offset <= 250; ++offset) {
        check(support(profile.prefix, offset) ==
                  support_linear(profile.prefix, offset),
              "binary prefix-hull search agrees with linear hull scan", failures);
        check(support(profile.suffix, offset) ==
                  support_linear(profile.suffix, offset),
              "binary suffix-hull search agrees with linear hull scan", failures);
        check(support(profile.best, offset) ==
                  support_linear(profile.best, offset),
              "binary best-hull search agrees with linear hull scan", failures);
        check(support(profile.prefix, offset) == brute_prefix(values, offset),
              "prefix profile support matches a direct scan", failures);
        check(support(profile.suffix, offset) == brute_suffix(values, offset),
              "suffix profile support matches a direct scan", failures);
        check(support(profile.best, offset) == brute_best(values, offset),
              "best profile support matches a direct scan", failures);
    }
}

void test_randomized_composition(int& failures) {
    std::mt19937_64 random(0xC0A7E5ULL);
    constexpr int trials = 700;
    for (int trial = 0; trial < trials; ++trial) {
        const std::size_t n = 1U + static_cast<std::size_t>(random() % 26U);
        std::vector<std::int64_t> values;
        values.reserve(n);
        for (std::size_t i = 0; i < n; ++i) {
            values.push_back(static_cast<std::int64_t>(random() % 201U) - 100);
        }
        const std::size_t split = static_cast<std::size_t>(
            random() % static_cast<std::uint64_t>(n + 1U));
        const std::vector<std::int64_t> left(
            values.begin(), values.begin() + static_cast<std::ptrdiff_t>(split));
        const std::vector<std::int64_t> right(
            values.begin() + static_cast<std::ptrdiff_t>(split), values.end());

        const Profile joined = combine_profiles(make_profile(left),
                                                 make_profile(right));
        const Profile direct = make_profile(values);
        check(joined.length == direct.length, "joined profile length is exact", failures);
        check(joined.total == direct.total, "joined total is exact", failures);
        check(same_hull(joined.prefix, direct.prefix),
              "disjoint bridge merge produces the exact prefix hull", failures);
        if (!same_hull(joined.suffix, direct.suffix)) {
            std::cerr << "suffix bridge mismatch trial=" << trial
                      << " n=" << n << " split=" << split << " values:";
            for (const auto value : values) std::cerr << ' ' << value;
            std::cerr << '\n';
            print_hull("joined suffix", joined.suffix);
            print_hull("direct suffix", direct.suffix);
        }
        check(same_hull(joined.suffix, direct.suffix),
              "disjoint bridge merge produces the exact suffix hull", failures);
        // Exhaust every integer offset that can expose a profile vertex for
        // these inputs (each source value is in [-100, 100]).
        for (std::int64_t offset = -250; offset <= 250; ++offset) {
            check(support(joined.prefix, offset) == support(direct.prefix, offset),
                  "joined prefix hull equals direct prefix hull", failures);
            check(support(joined.suffix, offset) == support(direct.suffix, offset),
                  "joined suffix hull equals direct suffix hull", failures);
            check(support(joined.best, offset) == support(direct.best, offset),
                  "joined best hull equals direct best hull", failures);
        }
        test_profile(values, failures);
    }
}

std::vector<std::int64_t> descending_positive(std::size_t n) {
    std::vector<std::int64_t> values;
    values.reserve(n);
    for (std::size_t i = n; i > 0U; --i) {
        values.push_back(static_cast<std::int64_t>(i));
    }
    return values;
}

void test_linear_profile_size(int& failures) {
    const auto values = descending_positive(1000U);
    const Profile profile = make_profile(values);
    check(profile.prefix.size() == values.size(),
          "decreasing values retain a linear-size prefix hull", failures);
    check(profile.best.size() == values.size(),
          "decreasing positive values retain a linear-size best hull", failures);
}

}  // namespace

int main() {
    int failures = 0;
    test_randomized_composition(failures);
    test_linear_profile_size(failures);
    if (failures != 0) {
        std::cerr << failures << " profile prototype test(s) failed\n";
        return 1;
    }
    const Profile large = make_profile(descending_positive(1000U));
    std::cout << "Profile shift/merge prototype passed (700 randomized compositions).\n";
    std::cout << "A monotone 1000-element input keeps " << large.prefix.size()
              << " prefix-hull vertices and " << large.best.size()
              << " best-hull vertices.\n";
    return 0;
}
