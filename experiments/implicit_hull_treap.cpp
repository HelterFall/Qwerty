#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <memory>
#include <random>
#include <utility>
#include <vector>

namespace {
using Wide = __int128_t;
constexpr Wide kNegInf = -(static_cast<Wide>(1) << 120);
struct Point { std::int64_t x = 0; Wide y = 0; };
Wide value_at(const Point& p, Wide slope) { return p.y + slope * static_cast<Wide>(p.x); }
struct Node {
    Node(Point p, std::uint64_t rank) : point(p), priority(rank) {}
    Point point;
    std::uint64_t priority;
    std::size_t size = 1;
    std::unique_ptr<Node> left, right;
    Wide dx = 0, shear = 0, dy = 0;
};
using Tree = std::unique_ptr<Node>;
std::size_t count(const Tree& t) { return t ? t->size : 0; }
void pull(Node& n) { n.size = 1 + count(n.left) + count(n.right); }
void apply(Tree& t, Wide a, Wide dx, Wide dy) {
    if (!t) return;
    const Wide old_dx = t->dx;
    t->point.y += a * static_cast<Wide>(t->point.x) + dy;
    t->point.x = static_cast<std::int64_t>(static_cast<Wide>(t->point.x) + dx);
    t->dy += a * old_dx + dy;
    t->shear += a;
    t->dx += dx;
}
void push(Node& n) {
    if (n.dx == 0 && n.shear == 0 && n.dy == 0) return;
    apply(n.left, n.shear, n.dx, n.dy);
    apply(n.right, n.shear, n.dx, n.dy);
    n.dx = n.shear = n.dy = 0;
}
std::pair<Tree, Tree> split(Tree t, std::size_t k) {
    if (!t) return {nullptr, nullptr};
    push(*t);
    const auto left_count = count(t->left);
    if (k <= left_count) {
        auto p = split(std::move(t->left), k);
        t->left = std::move(p.second);
        pull(*t);
        return {std::move(p.first), std::move(t)};
    }
    auto p = split(std::move(t->right), k - left_count - 1);
    t->right = std::move(p.first);
    pull(*t);
    return {std::move(t), std::move(p.second)};
}
Tree merge(Tree a, Tree b) {
    if (!a) return b;
    if (!b) return a;
    if (a->priority >= b->priority) {
        push(*a);
        a->right = merge(std::move(a->right), std::move(b));
        pull(*a);
        return a;
    }
    push(*b);
    b->left = merge(std::move(a), std::move(b->left));
    pull(*b);
    return b;
}
const Point& kth(Tree& t, std::size_t k) {
    push(*t);
    const auto left_count = count(t->left);
    if (k < left_count) return kth(t->left, k);
    if (k == left_count) return t->point;
    return kth(t->right, k - left_count - 1);
}
void visit(Tree& t, std::vector<Point>& out) {
    if (!t) return;
    push(*t);
    visit(t->left, out);
    out.push_back(t->point);
    visit(t->right, out);
}
std::vector<Point> materialize(Tree& t) {
    std::vector<Point> out;
    out.reserve(count(t));
    visit(t, out);
    return out;
}
Tree build(const std::vector<Point>& points, std::mt19937_64& rng) {
    Tree t;
    for (const auto& p : points) t = merge(std::move(t), std::make_unique<Node>(p, rng()));
    return t;
}
Wide support(Tree& t, Wide slope) {
    if (!t) return kNegInf;
    std::size_t lo = 0, hi = count(t) - 1;
    while (lo < hi) {
        const auto mid = lo + (hi - lo) / 2;
        if (value_at(kth(t, mid), slope) <= value_at(kth(t, mid + 1), slope)) lo = mid + 1;
        else hi = mid;
    }
    return value_at(kth(t, lo), slope);
}
Wide brute(const std::vector<Point>& points, Wide slope) {
    Wide best = kNegInf;
    for (const auto& p : points) best = std::max(best, value_at(p, slope));
    return best;
}
bool equal(const std::vector<Point>& a, const std::vector<Point>& b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i)
        if (a[i].x != b[i].x || a[i].y != b[i].y) return false;
    return true;
}
bool check(Tree& t, const std::vector<Point>& expected, int step) {
    if (!equal(materialize(t), expected)) {
        std::cerr << "rope coordinates/order mismatch at step " << step << '\n';
        return false;
    }
    for (Wide slope = -30; slope <= 30; slope += 3) {
        if (support(t, slope) != brute(expected, slope)) {
            std::cerr << "support mismatch at step " << step << '\n';
            return false;
        }
    }
    return true;
}

struct Slope { Wide dy = 0; Wide dx = 1; };
int compare_slope(const Slope& a, const Slope& b) {
    const Wide d = a.dy * b.dx - b.dy * a.dx;
    return (d > 0) - (d < 0);
}
std::size_t edge_count(const Tree& t) { return count(t) == 0 ? 0 : count(t) - 1; }
Slope edge_slope(Tree& t, std::size_t i) {
    const Point& p = kth(t, i);
    const Point& q = kth(t, i + 1);
    return Slope{q.y - p.y, static_cast<Wide>(q.x) - p.x};
}
Slope kth_slope_desc(Tree& a, Tree& b, std::size_t k) {
    Tree* first = &a;
    Tree* second = &b;
    if (edge_count(*first) > edge_count(*second)) std::swap(first, second);
    const std::size_t n1 = edge_count(*first);
    const std::size_t n2 = edge_count(*second);
    std::size_t lo = k + 1 > n2 ? k + 1 - n2 : 0;
    std::size_t hi = std::min(k + 1, n1);
    while (lo <= hi) {
        const std::size_t take1 = lo + (hi - lo) / 2;
        const std::size_t take2 = k + 1 - take1;
        if (take1 > 0 && take2 < n2 &&
            compare_slope(edge_slope(*first, take1 - 1), edge_slope(*second, take2)) < 0) {
            hi = take1 - 1;
            continue;
        }
        if (take2 > 0 && take1 < n1 &&
            compare_slope(edge_slope(*second, take2 - 1), edge_slope(*first, take1)) < 0) {
            lo = take1 + 1;
            continue;
        }
        if (take1 == 0) return edge_slope(*second, take2 - 1);
        if (take2 == 0) return edge_slope(*first, take1 - 1);
        const Slope s1 = edge_slope(*first, take1 - 1);
        const Slope s2 = edge_slope(*second, take2 - 1);
        return compare_slope(s1, s2) < 0 ? s1 : s2;
    }
    return Slope{};
}
Wide support_scaled(Tree& t, const Slope& slope, std::size_t* selected = nullptr,
                    bool rightmost = true) {
    if (!t) {
        if (selected) *selected = 0;
        return kNegInf;
    }
    const auto eval = [&](std::size_t i) {
        const Point& p = kth(t, i);
        return p.y * slope.dx - slope.dy * static_cast<Wide>(p.x);
    };
    std::size_t lo = 0, hi = count(t) - 1;
    while (lo < hi) {
        const auto mid = lo + (hi - lo) / 2;
        const Wide here = eval(mid);
        const Wide next = eval(mid + 1);
        if (rightmost ? here <= next : here < next) lo = mid + 1;
        else hi = mid;
    }
    if (selected) *selected = lo;
    return eval(lo);
}
Wide bridge_gap(Tree& left, Tree& right, const Slope& slope) {
    return support_scaled(left, slope) - support_scaled(right, slope);
}

// Both chains have strictly disjoint, ordered x domains and non-increasing
// edge slopes. The gap between their support lines is monotone in the slope,
// so binary-search the merged descending edge-slope sequence.
std::pair<std::size_t, std::size_t> find_disjoint_bridge(Tree& left, Tree& right) {
    const std::size_t total = edge_count(left) + edge_count(right);
    if (total == 0) return {0, 0};
    std::size_t lo = 0, hi = total;
    while (lo < hi) {
        const auto mid = lo + (hi - lo) / 2;
        const Slope candidate = kth_slope_desc(left, right, mid);
        if (bridge_gap(left, right, candidate) > 0) lo = mid + 1;
        else hi = mid;
    }
    Slope probe;
    if (lo == total) {
        probe = kth_slope_desc(left, right, total - 1);
        probe.dy -= probe.dx;
    } else {
        const Slope current = kth_slope_desc(left, right, lo);
        if (bridge_gap(left, right, current) == 0) {
            std::size_t li = 0, ri = 0;
            support_scaled(left, current, &li, false);
            support_scaled(right, current, &ri, true);
            return {li, ri};
        }
        if (lo == 0) {
            probe = current;
            probe.dy += probe.dx;
        } else {
            const Slope previous = kth_slope_desc(left, right, lo - 1);
            probe = Slope{
                previous.dy * current.dx + current.dy * previous.dx,
                static_cast<Wide>(2) * previous.dx * current.dx};
        }
    }
    std::size_t li = 0, ri = 0;
    support_scaled(left, probe, &li, false);
    support_scaled(right, probe, &ri, true);
    return {li, ri};
}

Tree join_disjoint_upper_chains(Tree left, Tree right) {
    if (!left) return right;
    if (!right) return left;
    if (kth(left, count(left) - 1).x >= kth(right, 0).x)
        throw std::invalid_argument("upper chains must have disjoint ordered x domains");
    const auto bridge = find_disjoint_bridge(left, right);
    auto left_parts = split(std::move(left), bridge.first + 1);
    auto right_parts = split(std::move(right), bridge.second);
    // Keep only the prefix ending at the left tangent and suffix starting at
    // the right tangent. The implicit treap stores the bridge as adjacency;
    // no hull vertices are copied or scanned during the join.
    return merge(std::move(left_parts.first), std::move(right_parts.second));
}

Wide cross_product(const Point& a, const Point& b, const Point& c) {
    const Wide dx1 = static_cast<Wide>(b.x) - a.x;
    const Wide dx2 = static_cast<Wide>(c.x) - b.x;
    return dx1 * (c.y - b.y) - (b.y - a.y) * dx2;
}
std::vector<Point> upper_hull_sorted(std::vector<Point> points) {
    std::vector<Point> unique;
    unique.reserve(points.size());
    for (const auto& p : points) {
        if (!unique.empty() && unique.back().x == p.x) {
            if (p.y > unique.back().y) unique.back() = p;
        } else {
            unique.push_back(p);
        }
    }
    std::vector<Point> hull;
    hull.reserve(unique.size());
    for (const auto& p : unique) {
        while (hull.size() >= 2 &&
               cross_product(hull[hull.size() - 2], hull.back(), p) >= 0)
            hull.pop_back();
        hull.push_back(p);
    }
    return hull;
}
std::vector<Point> prefix_points(const std::vector<std::int64_t>& values) {
    std::vector<Point> points;
    points.reserve(values.size());
    Wide sum = 0;
    for (std::size_t i = 0; i < values.size(); ++i) {
        sum += values[i];
        points.push_back(Point{static_cast<std::int64_t>(i + 1), sum});
    }
    return points;
}
std::vector<Point> suffix_points(const std::vector<std::int64_t>& values) {
    std::vector<Point> points;
    points.reserve(values.size());
    Wide sum = 0;
    for (std::size_t length = 1; length <= values.size(); ++length) {
        sum += values[values.size() - length];
        points.push_back(Point{static_cast<std::int64_t>(length), sum});
    }
    return points;
}
bool test_implicit_profile_joins() {
    std::mt19937_64 rng(0xB12D6EULL);
    for (int trial = 0; trial < 700; ++trial) {
        const std::size_t n = 1 + static_cast<std::size_t>(rng() % 70U);
        const std::size_t cut = static_cast<std::size_t>(rng() % (n + 1));
        std::vector<std::int64_t> values;
        values.reserve(n);
        for (std::size_t i = 0; i < n; ++i)
            values.push_back(static_cast<std::int64_t>(rng() % 201U) - 100);
        const std::vector<std::int64_t> a(values.begin(),
            values.begin() + static_cast<std::ptrdiff_t>(cut));
        const std::vector<std::int64_t> b(values.begin() + static_cast<std::ptrdiff_t>(cut),
            values.end());

        // Prefix profile of A + B: hull(prefix(A)) joined with the shifted
        // hull(total(A) + prefix(B), whose x domain begins after |A|.
        auto left_prefix = upper_hull_sorted(prefix_points(a));
        auto right_prefix = upper_hull_sorted(prefix_points(b));
        Tree lp = build(left_prefix, rng);
        Tree rp = build(right_prefix, rng);
        Wide total_a = 0;
        for (auto value : a) total_a += value;
        apply(rp, 0, static_cast<Wide>(a.size()), total_a);
        Tree joined_prefix = join_disjoint_upper_chains(std::move(lp), std::move(rp));
        const auto expected_prefix = upper_hull_sorted(prefix_points(values));
        if (!equal(materialize(joined_prefix), expected_prefix)) {
            std::cerr << "implicit prefix-profile join failed at trial " << trial << '\n';
            return false;
        }

        // Suffix profile of A + B: hull(suffix(B)) followed by shifted
        // hull(total(B) + suffix(A)).
        auto left_suffix = upper_hull_sorted(suffix_points(b));
        auto right_suffix = upper_hull_sorted(suffix_points(a));
        Tree ls = build(left_suffix, rng);
        Tree rs = build(right_suffix, rng);
        Wide total_b = 0;
        for (auto value : b) total_b += value;
        apply(rs, 0, static_cast<Wide>(b.size()), total_b);
        const auto suffix_bridge = find_disjoint_bridge(ls, rs);
        Tree joined_suffix = join_disjoint_upper_chains(std::move(ls), std::move(rs));
        const auto expected_suffix = upper_hull_sorted(suffix_points(values));
        const auto actual_suffix = materialize(joined_suffix);
        if (!equal(actual_suffix, expected_suffix)) {
            std::cerr << "implicit suffix-profile join failed at trial " << trial
                      << " n=" << n << " cut=" << cut
                      << " bridge=(" << suffix_bridge.first << ","
                      << suffix_bridge.second << ")\nvalues:";
            for (auto value : values) std::cerr << " " << value;
            std::cerr << "\nleft suffix hull:";
            for (const auto& p : left_suffix)
                std::cerr << " (" << p.x << "," << static_cast<long long>(p.y) << ")";
            std::cerr << "\nshifted right suffix hull:";
            for (const auto& p : right_suffix)
                std::cerr << " (" << p.x + static_cast<std::int64_t>(b.size()) << ","
                          << static_cast<long long>(p.y + total_b) << ")";
            std::cerr << "\nactual:";
            for (const auto& p : actual_suffix)
                std::cerr << " (" << p.x << "," << static_cast<long long>(p.y) << ")";
            std::cerr << "\nexpected:";
            for (const auto& p : expected_suffix)
                std::cerr << " (" << p.x << "," << static_cast<long long>(p.y) << ")";
            std::cerr << '\n';
            return false;
        }

        for (Wide slope = -110; slope <= 110; slope += 11) {
            if (support(joined_prefix, slope) != brute(expected_prefix, slope) ||
                support(joined_suffix, slope) != brute(expected_suffix, slope)) {
                std::cerr << "implicit profile support failed at trial " << trial << '\n';
                return false;
            }
        }
    }
    std::cout << "Implicit prefix/suffix profile joins passed 700 random splits; "
              << "output chains were never materialized during the joins.\n";
    return true;
}

bool run_test() {
    std::mt19937_64 rng(0x1A11C17ULL);
    constexpr std::size_t n = 1023;
    std::vector<Point> original;
    original.reserve(n);
    for (std::size_t i = 0; i < n; ++i)
        original.push_back(Point{static_cast<std::int64_t>(i), -static_cast<Wide>(i) * static_cast<Wide>(i)});
    Tree tree = build(original, rng);
    for (int step = 0; step < 300; ++step) {
        const auto cut = static_cast<std::size_t>(rng() % (n + 1));
        auto pieces = split(std::move(tree), cut);
        tree = merge(std::move(pieces.first), std::move(pieces.second));
        if (!check(tree, original, step)) return false;
        const auto left = static_cast<std::size_t>(rng() % n);
        const auto right = left + static_cast<std::size_t>(rng() % (n - left));
        auto p1 = split(std::move(tree), left);
        auto p2 = split(std::move(p1.second), right - left + 1);
        const Wide a = static_cast<std::int64_t>(rng() % 9) - 4;
        const Wide dx = static_cast<std::int64_t>(rng() % 3) - 1;
        const Wide dy = static_cast<std::int64_t>(rng() % 41) - 20;
        apply(p2.first, a, dx, dy);
        std::vector<Point> expected(original.begin() + static_cast<std::ptrdiff_t>(left),
                                    original.begin() + static_cast<std::ptrdiff_t>(right + 1));
        for (auto& point : expected) {
            const Wide old_x = static_cast<Wide>(point.x);
            point.y += a * old_x + dy;
            point.x = static_cast<std::int64_t>(old_x + dx);
        }
        if (!check(p2.first, expected, step)) return false;
        std::vector<Point> expected_full = original;
        std::copy(expected.begin(), expected.end(),
                  expected_full.begin() + static_cast<std::ptrdiff_t>(left));
        tree = merge(merge(std::move(p1.first), std::move(p2.first)), std::move(p2.second));
        if (!equal(materialize(tree), expected_full)) {
            std::cerr << "split/merge changed sequence coordinates/order at step " << step << '\n';
            return false;
        }
        tree = build(original, rng);
    }
    std::cout << "Implicit hull treap passed 300 split/merge/lazy-affine rounds on "
              << n << " vertices; support matched brute force.\n";
    return true;
}

bool test_composed_affine_tags() {
    std::mt19937_64 rng(0xA771F1EULL);
    constexpr std::size_t n = 1023;
    std::vector<Point> expected;
    expected.reserve(n);
    for (std::size_t i = 0; i < n; ++i) {
        expected.push_back(Point{static_cast<std::int64_t>(i),
                                 -static_cast<Wide>(i) * static_cast<Wide>(i)});
    }
    Tree tree = build(expected, rng);
    constexpr int operations = 1500;
    for (int step = 0; step < operations; ++step) {
        const Wide a = static_cast<std::int64_t>(rng() % 11U) - 5;
        const Wide dx = static_cast<std::int64_t>(rng() % 3U) - 1;
        const Wide dy = static_cast<std::int64_t>(rng() % 101U) - 50;
        apply(tree, a, dx, dy);
        for (auto& point : expected) {
            const Wide old_x = static_cast<Wide>(point.x);
            point.y += a * old_x + dy;
            point.x = static_cast<std::int64_t>(old_x + dx);
        }
        if (!equal(materialize(tree), expected)) {
            std::cerr << "composed lazy affine tags mismatch at step "
                      << step << '\n';
            return false;
        }
        if (step % 25 == 0) {
            for (Wide slope : {-17, -3, 0, 8, 21}) {
                if (support(tree, slope) != brute(expected, slope)) {
                    std::cerr << "composed affine support mismatch at step "
                              << step << '\n';
                    return false;
                }
            }
        }
    }
    std::cout << "Implicit hull treap passed " << operations
              << " consecutive composed lazy-affine transforms on " << n
              << " vertices.\\n";
    return true;
}

} // namespace
int main() { return run_test() && test_composed_affine_tags() && test_implicit_profile_joins() ? 0 : 1; }
