#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <random>
#include <vector>

namespace {

using Wide = __int128_t;
constexpr Wide kNegInf = -(static_cast<Wide>(1) << 120);

struct Point {
    std::int64_t x = 0;
    Wide y = 0;
};

Wide cross(const Point& a, const Point& b, const Point& c) {
    const Wide dx1 = static_cast<Wide>(b.x) - a.x;
    const Wide dx2 = static_cast<Wide>(c.x) - b.x;
    return dx1 * (c.y - b.y) - (b.y - a.y) * dx2;
}

std::vector<Point> upper_hull(std::vector<Point> points) {
    std::sort(points.begin(), points.end(), [](const Point& a, const Point& b) {
        if (a.x != b.x) return a.x < b.x;
        return a.y > b.y;
    });

    std::vector<Point> unique;
    unique.reserve(points.size());
    for (const Point& point : points) {
        if (!unique.empty() && unique.back().x == point.x) {
            if (point.y > unique.back().y) unique.back() = point;
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

Wide line_value(const Point& point, Wide slope) {
    return point.y + slope * static_cast<Wide>(point.x);
}

Wide support_base(const std::vector<Point>& hull, Wide slope) {
    if (hull.empty()) return kNegInf;
    std::size_t low = 0U;
    std::size_t high = hull.size() - 1U;
    while (low < high) {
        const std::size_t middle = low + (high - low) / 2U;
        if (line_value(hull[middle], slope) <=
            line_value(hull[middle + 1U], slope)) {
            low = middle + 1U;
        } else {
            high = middle;
        }
    }
    return line_value(hull[low], slope);
}

Wide support_brute(const std::vector<Point>& points, Wide slope) {
    Wide best = kNegInf;
    for (const Point& point : points) {
        best = std::max(best, line_value(point, slope));
    }
    return best;
}

bool equal_hulls(const std::vector<Point>& a, const std::vector<Point>& b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (a[i].x != b[i].x || a[i].y != b[i].y) return false;
    }
    return true;
}

// Lazy map:
//   x' = x + shift_x
//   y' = y + shear*x + shift_y
// Its determinant is 1, so it preserves convexity and the order of hull
// vertices. Support can be answered from the original hull with no rebuilding.
class AffineHull {
public:
    explicit AffineHull(std::vector<Point> canonical_hull)
        : hull_(std::move(canonical_hull)) {}

    // Apply the map (x,y) -> (x + dx, y + new_shear*x + dy) to the
    // currently transformed points, composing it into one stored lazy tag.
    void apply(Wide new_shear, Wide dx, Wide dy) {
        const Wide old_shift_x = shift_x_;
        shift_y_ += new_shear * old_shift_x + dy;
        shear_ += new_shear;
        shift_x_ += dx;
    }

    Wide support(Wide slope) const {
        return support_base(hull_, slope + shear_) +
               shift_y_ + slope * shift_x_;
    }

    std::vector<Point> materialize_hull() const {
        std::vector<Point> result;
        result.reserve(hull_.size());
        for (const Point& point : hull_) {
            result.push_back(Point{
                static_cast<std::int64_t>(
                    static_cast<Wide>(point.x) + shift_x_),
                point.y + shear_ * static_cast<Wide>(point.x) + shift_y_});
        }
        return result;
    }

    std::size_t stored_vertex_count() const noexcept { return hull_.size(); }

private:
    std::vector<Point> hull_;
    Wide shift_x_ = 0;
    Wide shear_ = 0;
    Wide shift_y_ = 0;
};

void fail(int step, const char* message) {
    std::cerr << "affine hull test failed at step " << step
              << ": " << message << '\n';
}

bool test_lazy_affine_hull() {
    std::mt19937_64 random(0xA771F1EULL);
    std::vector<Point> points;
    points.reserve(1201U);
    Wide y = 0;
    points.push_back(Point{0, 0});
    for (std::int64_t x = 1; x <= 1200; ++x) {
        y += static_cast<std::int64_t>(random() % 101U) - 50;
        points.push_back(Point{x, y});
    }

    const std::vector<Point> initial_hull = upper_hull(points);
    AffineHull lazy_hull(initial_hull);
    const std::size_t stored_vertices = lazy_hull.stored_vertex_count();

    constexpr int transformations = 1500;
    for (int step = 0; step < transformations; ++step) {
        const Wide shear = static_cast<std::int64_t>(random() % 11U) - 5;
        const Wide dx = (random() % 3U == 0U) ? 1 : 0;
        const Wide dy = static_cast<std::int64_t>(random() % 41U) - 20;
        lazy_hull.apply(shear, dx, dy);

        for (Point& point : points) {
            const Wide old_x = static_cast<Wide>(point.x);
            point.y += shear * old_x + dy;
            point.x = static_cast<std::int64_t>(old_x + dx);
        }

        const std::vector<Point> expected_hull = upper_hull(points);
        const std::vector<Point> stored_hull = lazy_hull.materialize_hull();
        if (!equal_hulls(stored_hull, expected_hull)) {
            fail(step, "lazy affine transform changes hull topology or coordinates");
            return false;
        }

        for (Wide slope = -25; slope <= 25; slope += 5) {
            if (lazy_hull.support(slope) != support_brute(points, slope)) {
                fail(step, "lazy support differs from brute-force point scan");
                return false;
            }
        }
    }

    std::cout << "Lazy affine hull passed " << transformations
              << " transforms and support checks; stored hull vertices="
              << stored_vertices << " of " << points.size() << " input points.\n";
    return true;
}

}  // namespace

int main() {
    return test_lazy_affine_hull() ? 0 : 1;
}
