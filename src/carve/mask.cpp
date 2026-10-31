#include "carve/mask.h"

#include <algorithm>
#include <cmath>

#include "carve/shapes.h"

namespace pumpkin {

namespace {

float bilinear(const std::vector<uint8_t>& a, int n, float lo, float hi, float x, float y) {
    if (n <= 0) return 0;
    const float cell = (hi - lo) / static_cast<float>(n);
    const float fx = (x - lo) / cell - 0.5f, fy = (hi - y) / cell - 0.5f;
    const int x0 = static_cast<int>(std::floor(fx)), y0 = static_cast<int>(std::floor(fy));
    const float tx = fx - static_cast<float>(x0), ty = fy - static_cast<float>(y0);
    auto at = [&](int cx, int cy) -> float {
        if (cx < 0 || cy < 0 || cx >= n || cy >= n) return 0;
        return a[static_cast<size_t>(cy) * n + cx] / 255.0f;
    };
    const float top = at(x0, y0) * (1 - tx) + at(x0 + 1, y0) * tx;
    const float bot = at(x0, y0 + 1) * (1 - tx) + at(x0 + 1, y0 + 1) * tx;
    return top * (1 - ty) + bot * ty;
}

float segDist2(Vec2 p, Vec2 a, Vec2 b) {
    const float dx = b.x - a.x, dy = b.y - a.y;
    const float len2 = dx * dx + dy * dy;
    float t = len2 > 0 ? ((p.x - a.x) * dx + (p.y - a.y) * dy) / len2 : 0;
    t = std::max(0.0f, std::min(1.0f, t));
    const float ex = a.x + t * dx - p.x, ey = a.y + t * dy - p.y;
    return ex * ex + ey * ey;
}

struct Box {
    float x0, y0, x1, y1;
};

template <typename Inside>
void fill(Mask& m, Box b, Inside inside) {
    const float cell = m.cellSize();
    const int cx0 = std::max(0, static_cast<int>((b.x0 - m.lo) / cell) - 1);
    const int cx1 = std::min(m.size - 1, static_cast<int>((b.x1 - m.lo) / cell) + 1);
    const int cy0 = std::max(0, static_cast<int>((m.hi - b.y1) / cell) - 1);
    const int cy1 = std::min(m.size - 1, static_cast<int>((m.hi - b.y0) / cell) + 1);
    for (int cy = cy0; cy <= cy1; ++cy) {
        const float y = m.hi - (static_cast<float>(cy) + 0.5f) * cell;
        for (int cx = cx0; cx <= cx1; ++cx) {
            const float x = m.lo + (static_cast<float>(cx) + 0.5f) * cell;
            if (inside(Vec2{x, y})) m.cells[static_cast<size_t>(cy) * m.size + cx] = 255;
        }
    }
}

// Box blur, one axis at a time, `passes` times: how far into a hole each cell is.
std::vector<uint8_t> blur(const std::vector<uint8_t>& in, int n, int radius, int passes) {
    std::vector<float> a(in.begin(), in.end()), b(a.size());
    const float k = 1.0f / static_cast<float>(2 * radius + 1);
    for (int p = 0; p < passes; ++p) {
        for (int y = 0; y < n; ++y) {
            float sum = 0;
            for (int x = -radius; x <= radius; ++x) sum += x >= 0 && x < n ? a[static_cast<size_t>(y) * n + x] : 0;
            for (int x = 0; x < n; ++x) {
                b[static_cast<size_t>(y) * n + x] = sum * k;
                const int add = x + radius + 1, sub = x - radius;
                if (add < n) sum += a[static_cast<size_t>(y) * n + add];
                if (sub >= 0) sum -= a[static_cast<size_t>(y) * n + sub];
            }
        }
        for (int x = 0; x < n; ++x) {
            float sum = 0;
            for (int y = -radius; y <= radius; ++y) sum += y >= 0 && y < n ? b[static_cast<size_t>(y) * n + x] : 0;
            for (int y = 0; y < n; ++y) {
                a[static_cast<size_t>(y) * n + x] = sum * k;
                const int add = y + radius + 1, sub = y - radius;
                if (add < n) sum += b[static_cast<size_t>(add) * n + x];
                if (sub >= 0) sum -= b[static_cast<size_t>(sub) * n + x];
            }
        }
    }
    std::vector<uint8_t> out(in.size());
    for (size_t i = 0; i < a.size(); ++i) out[i] = static_cast<uint8_t>(std::min(255.0f, a[i] + 0.5f));
    return out;
}

}  // namespace

float Mask::sample(float x, float y) const { return bilinear(cells, size, lo, hi, x, y); }
float Mask::sampleGlow(float x, float y) const { return bilinear(glow, size, lo, hi, x, y); }

int Mask::cutCount() const {
    int n = 0;
    for (uint8_t c : cells) n += c ? 1 : 0;
    return n;
}

Mask buildMask(const Carving& c, int size) {
    Mask m;
    m.size = size;
    m.cells.assign(static_cast<size_t>(size) * size, 0);
    for (const Op& op : c.ops) {
        if (op.isStamp) {
            const Stamp& s = op.stamp;
            const float r = s.scale * 1.45f;
            fill(m, {s.at.x - r, s.at.y - r, s.at.x + r, s.at.y + r}, [&](Vec2 p) { return insideStamp(s, p); });
            continue;
        }
        const std::vector<Vec2>& pts = op.cut.points;
        if (pts.size() < 2) continue;
        Box b{pts[0].x, pts[0].y, pts[0].x, pts[0].y};
        for (const Vec2& p : pts) {
            b.x0 = std::min(b.x0, p.x); b.x1 = std::max(b.x1, p.x);
            b.y0 = std::min(b.y0, p.y); b.y1 = std::max(b.y1, p.y);
        }
        b.x0 -= kSlitWidth; b.y0 -= kSlitWidth; b.x1 += kSlitWidth; b.y1 += kSlitWidth;
        const bool closed = op.cut.closed && pts.size() >= 3;
        const float w2 = kSlitWidth * kSlitWidth;
        fill(m, b, [&](Vec2 p) {
            if (closed && insidePolygon(pts, p)) return true;
            for (size_t i = 1; i < pts.size(); ++i)
                if (segDist2(p, pts[i - 1], pts[i]) < w2) return true;
            return false;
        });
    }
    m.glow = blur(m.cells, size, std::max(2, size / 64), 2);
    return m;
}

}  // namespace pumpkin
