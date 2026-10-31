#include "carve/shapes.h"

#include <cmath>
#include <cstdio>

namespace pumpkin {

namespace {

constexpr float kPi = 3.14159265359f;

Polygon ellipse(float cx, float cy, float rx, float ry, int n = 40) {
    Polygon p;
    for (int i = 0; i < n; ++i) {
        const float a = 2 * kPi * static_cast<float>(i) / static_cast<float>(n);
        p.push_back({cx + rx * std::cos(a), cy + ry * std::sin(a)});
    }
    return p;
}

// A crescent: the band between two arcs from x = -w to x = w. The outer arc
// bulges by `outer` below the line, the inner one by `inner` (both negative
// for a smile that opens upwards).
Polygon band(float w, float yEnds, float outer, float inner, int n = 24) {
    Polygon p;
    for (int i = 0; i <= n; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(n);
        const float x = -w + 2 * w * t;
        const float k = 1 - (2 * t - 1) * (2 * t - 1);
        p.push_back({x, yEnds - outer * k});
    }
    for (int i = n - 1; i >= 1; --i) {
        const float t = static_cast<float>(i) / static_cast<float>(n);
        const float x = -w + 2 * w * t;
        const float k = 1 - (2 * t - 1) * (2 * t - 1);
        p.push_back({x, yEnds - inner * k});
    }
    return p;
}

std::vector<std::vector<Polygon>> build() {
    std::vector<std::vector<Polygon>> s(ShapeCount);
    s[TriangleEye] = {{{-1, -0.8f}, {1, -0.8f}, {0, 0.95f}}};
    s[RoundEye] = {ellipse(0, 0, 0.9f, 0.9f)};
    // A happy eye: an upside-down crescent (^).
    s[HappyEye] = {band(1, -0.35f, -1.05f, -0.45f)};
    // An angry eye: a wedge sloping down towards the nose (the right side).
    s[AngryEye] = {{{-1, 0.55f}, {1, -0.1f}, {0.75f, -0.7f}, {-0.85f, -0.7f}}};
    {
        Polygon star;
        for (int i = 0; i < 10; ++i) {
            const float a = kPi / 2 + kPi * static_cast<float>(i) / 5;
            const float r = i % 2 ? 0.42f : 1.0f;
            star.push_back({r * std::cos(a), r * std::sin(a) - 0.05f});
        }
        s[StarEye] = {star};
    }
    s[TriangleNose] = {{{-0.8f, -0.75f}, {0.8f, -0.75f}, {0, 0.85f}}};
    {
        Polygon heart;
        for (int i = 0; i < 48; ++i) {
            const float t = 2 * kPi * static_cast<float>(i) / 48;
            const float x = 16 * std::pow(std::sin(t), 3);
            const float y = 13 * std::cos(t) - 5 * std::cos(2 * t) - 2 * std::cos(3 * t) - std::cos(4 * t);
            heart.push_back({x / 17, y / 17 + 0.1f});
        }
        s[HeartNose] = {heart};
    }
    {
        // A grin: a wide smile with two square teeth hanging from the top.
        Polygon g;
        const int n = 28;
        auto top = [](float x) { return 0.25f - 0.25f * (1 - x * x); };
        auto bottom = [](float x) { return 0.25f - 0.85f * (1 - x * x); };
        for (int i = 0; i <= n; ++i) {
            const float x = -1 + 2 * static_cast<float>(i) / n;
            g.push_back({x, bottom(x)});
        }
        // Back along the top, right to left, with a tooth at x = 0.3 and -0.3.
        const float teeth[][2] = {{0.42f, 0.18f}, {-0.18f, -0.42f}};
        float x = 1;
        const float step = 2.0f / n;
        int tooth = 0;
        while (x > -1 + 1e-4f) {
            if (tooth < 2 && x <= teeth[tooth][0] + 1e-4f) {
                const float a = teeth[tooth][0], b = teeth[tooth][1];
                g.push_back({a, top(a)});
                g.push_back({a, top(a) - 0.28f});
                g.push_back({b, top(b) - 0.28f});
                g.push_back({b, top(b)});
                x = b - step;
                ++tooth;
                continue;
            }
            g.push_back({x, top(x)});
            x -= step;
        }
        g.erase(g.begin());  // the right corner is already there
        s[GrinMouth] = {g};
    }
    s[SmileMouth] = {band(1, 0.3f, 0.95f, 0.45f)};
    {
        // A zigzag mouth: a band whose top and bottom edges are saw teeth.
        Polygon z;
        const int teeth = 6;
        for (int i = 0; i <= teeth * 2; ++i) {
            const float x = -1 + static_cast<float>(i) / teeth;
            const float curve = -0.3f * (1 - x * x);
            z.push_back({x, curve - (i % 2 ? 0.42f : 0.18f)});
        }
        for (int i = teeth * 2; i >= 0; --i) {
            const float x = -1 + static_cast<float>(i) / teeth;
            const float curve = -0.3f * (1 - x * x);
            z.push_back({x, curve + (i % 2 ? 0.05f : 0.3f)});
        }
        s[ZigzagMouth] = {z};
    }
    s[OhMouth] = {ellipse(0, 0, 0.55f, 0.8f)};
    {
        // A frown with two fangs pointing down from the top.
        Polygon f;
        const int n = 24;
        for (int i = 0; i <= n; ++i) {
            const float x = -1 + 2 * static_cast<float>(i) / n;
            f.push_back({x, -0.35f + 0.55f * (1 - x * x) - 0.45f});
        }
        for (int i = n; i >= 0; --i) {
            const float x = -1 + 2 * static_cast<float>(i) / n;
            const float yTop = -0.35f + 0.55f * (1 - x * x) + 0.05f;
            if (i == 15 || i == 9) {
                f.push_back({x + 0.08f, yTop});
                f.push_back({x, yTop - 0.42f});
                f.push_back({x - 0.08f, yTop});
            } else {
                f.push_back({x, yTop});
            }
        }
        s[FangMouth] = {f};
    }
    s[Bat] = {{{0, 0.25f}, {0.12f, 0.45f}, {0.2f, 0.2f}, {0.5f, 0.45f}, {1, 0.35f}, {0.85f, 0.05f}, {0.7f, -0.05f},
               {0.55f, -0.3f}, {0.35f, -0.1f}, {0.15f, -0.35f}, {0, -0.15f}, {-0.15f, -0.35f}, {-0.35f, -0.1f},
               {-0.55f, -0.3f}, {-0.7f, -0.05f}, {-0.85f, 0.05f}, {-1, 0.35f}, {-0.5f, 0.45f}, {-0.2f, 0.2f},
               {-0.12f, 0.45f}}};
    {
        Polygon m;
        for (int i = 0; i <= 24; ++i) {
            const float a = kPi / 2 + kPi * static_cast<float>(i) / 24;
            m.push_back({0.9f * std::cos(a) + 0.2f, 0.9f * std::sin(a)});
        }
        for (int i = 23; i >= 1; --i) {
            const float a = kPi / 2 + kPi * static_cast<float>(i) / 24;
            const float r = 0.9f;
            const float k = std::sin(a);
            m.push_back({0.2f + r * std::cos(a) * 0.35f, r * k});
        }
        s[Moon] = {m};
    }
    return s;
}

}  // namespace

const std::vector<Polygon>& shapeOutline(Shape s) {
    static const std::vector<std::vector<Polygon>> all = build();
    return all[s < ShapeCount ? s : 0];
}

float defaultScale(Shape s) {
    switch (partOf(s)) {
    case Part::Eye: return 0.17f;
    case Part::Nose: return 0.12f;
    case Part::Mouth: return 0.45f;
    default: return 0.22f;
    }
}

std::string shapeSvgPath(Shape s) {
    std::string out;
    char buf[48];
    for (const Polygon& p : shapeOutline(s)) {
        for (size_t i = 0; i < p.size(); ++i) {
            std::snprintf(buf, sizeof buf, "%c%.1f %.1f", i ? 'L' : 'M', static_cast<double>(50 + 46 * p[i].x),
                          static_cast<double>(50 - 46 * p[i].y));
            out += buf;
        }
        out += 'Z';
    }
    return out;
}

bool insidePolygon(const Polygon& poly, Vec2 p) {
    bool in = false;
    const size_t n = poly.size();
    for (size_t i = 0, j = n - 1; i < n; j = i++) {
        const Vec2 &a = poly[i], &b = poly[j];
        if ((a.y > p.y) != (b.y > p.y)) {
            const float x = a.x + (p.y - a.y) * (b.x - a.x) / (b.y - a.y);
            if (p.x < x) in = !in;
        }
    }
    return in;
}

Vec2 toShape(const Stamp& s, Vec2 p) {
    const float dx = p.x - s.at.x, dy = p.y - s.at.y;
    const float c = std::cos(-s.angle), sn = std::sin(-s.angle);
    Vec2 q{(dx * c - dy * sn) / s.scale, (dx * sn + dy * c) / s.scale};
    if (s.flip) q.x = -q.x;
    return q;
}

Vec2 fromShape(const Stamp& s, Vec2 q) {
    if (s.flip) q.x = -q.x;
    const float c = std::cos(s.angle), sn = std::sin(s.angle);
    return {s.at.x + s.scale * (q.x * c - q.y * sn), s.at.y + s.scale * (q.x * sn + q.y * c)};
}

bool insideStamp(const Stamp& s, Vec2 p) {
    const Vec2 q = toShape(s, p);
    if (q.x < -1.1f || q.x > 1.1f || q.y < -1.1f || q.y > 1.1f) return false;
    for (const Polygon& poly : shapeOutline(s.shape))
        if (insidePolygon(poly, q)) return true;
    return false;
}

}  // namespace pumpkin
