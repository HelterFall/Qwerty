#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <iostream>
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
        tree = merge(merge(std::move(p1.first), std::move(p2.first)), std::move(p2.second));
        if (!equal(materialize(tree), original)) {
            std::cerr << "split/merge changed untouched sequence at step " << step << '\n';
            return false;
        }
        tree = build(original, rng);
    }
    std::cout << "Implicit hull treap passed 300 split/merge/lazy-affine rounds on "
              << n << " vertices; support matched brute force.\n";
    return true;
}
} // namespace
int main() { return run_test() ? 0 : 1; }
