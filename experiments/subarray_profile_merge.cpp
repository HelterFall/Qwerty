#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <random>
#include <string>
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

// Keeps candidates that can maximize sum + offset * length for some offset.
std::vector<Point> upper_hull(std::vector<Point> points) {
    std::sort(points.begin(), points.end(), [](const Point& a, const Point& b) {
        if (a.length != b.length) return a.length < b.length;
        return a.sum > b.sum;
    });

    std::vector<Point> unique;
    unique.reserve(points.size());
    for (const Point& point : points) {
        if (!unique.empty() && unique.back().length == point.length) continue;
        unique.push_back(point);  // Highest sum for this length comes first.
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

Wide support(const std::vector<Point>& hull, std::int64_t offset) {
    Wide answer = kNegInf;
    for (const Point& point : hull) {
        answer = std::max(answer,
            point.sum + static_cast<Wide>(offset) * point.length);
    }
    return answer;
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
    return upper_hull(std::move(result));
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

    profile.prefix = upper_hull(std::move(prefix_points));
    profile.suffix = upper_hull(std::move(suffix_points));
    profile.best = upper_hull(std::move(best_points));
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

    std::vector<Point> prefix_candidates = a.prefix;
    prefix_candidates.reserve(a.prefix.size() + b.prefix.size());
    for (const Point& point : b.prefix) {
        prefix_candidates.push_back(Point{
            static_cast<std::int64_t>(a.length) + point.length,
            a.total + point.sum});
    }
    result.prefix = upper_hull(std::move(prefix_candidates));

    std::vector<Point> suffix_candidates = b.suffix;
    suffix_candidates.reserve(a.suffix.size() + b.suffix.size());
    for (const Point& point : a.suffix) {
        suffix_candidates.push_back(Point{
            static_cast<std::int64_t>(b.length) + point.length,
            b.total + point.sum});
    }
    result.suffix = upper_hull(std::move(suffix_candidates));

    std::vector<Point> best_candidates = a.best;
    best_candidates.insert(best_candidates.end(), b.best.begin(), b.best.end());
    const std::vector<Point> crossing = minkowski_upper(a.suffix, b.prefix);
    best_candidates.insert(best_candidates.end(), crossing.begin(), crossing.end());
    result.best = upper_hull(std::move(best_candidates));
    return result;
}

void check(bool condition, const std::string& message, int& failures) {
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
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
    for (const std::int64_t offset : {-100, -17, -1, 0, 1, 23, 100}) {
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
        for (const std::int64_t offset : {-100, -19, -1, 0, 1, 11, 100}) {
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
