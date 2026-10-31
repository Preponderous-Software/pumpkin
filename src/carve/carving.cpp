#include "carve/carving.h"

#include <algorithm>
#include <cmath>

namespace pumpkin {

Part partOf(Shape s) {
    switch (s) {
    case TriangleEye: case RoundEye: case HappyEye: case AngryEye: case StarEye: return Part::Eye;
    case TriangleNose: case HeartNose: return Part::Nose;
    case GrinMouth: case SmileMouth: case ZigzagMouth: case OhMouth: case FangMouth: return Part::Mouth;
    default: return Part::Extra;
    }
}

namespace {

bool same(const Vec2& a, const Vec2& b) { return a.x == b.x && a.y == b.y; }

bool sameOp(const Op& a, const Op& b) {
    if (a.isStamp != b.isStamp) return false;
    if (a.isStamp) {
        const Stamp &s = a.stamp, &t = b.stamp;
        return s.shape == t.shape && same(s.at, t.at) && s.scale == t.scale && s.angle == t.angle && s.flip == t.flip;
    }
    if (a.cut.closed != b.cut.closed || a.cut.points.size() != b.cut.points.size()) return false;
    for (size_t i = 0; i < a.cut.points.size(); ++i)
        if (!same(a.cut.points[i], b.cut.points[i])) return false;
    return true;
}

float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

float segDist(const Vec2& p, const Vec2& a, const Vec2& b) {
    const float dx = b.x - a.x, dy = b.y - a.y;
    const float len2 = dx * dx + dy * dy;
    float t = len2 > 0 ? ((p.x - a.x) * dx + (p.y - a.y) * dy) / len2 : 0;
    t = clampf(t, 0, 1);
    const float ex = a.x + t * dx - p.x, ey = a.y + t * dy - p.y;
    return std::sqrt(ex * ex + ey * ey);
}

void rdp(const std::vector<Vec2>& p, size_t lo, size_t hi, float tol, std::vector<bool>& keep) {
    if (hi <= lo + 1) return;
    float worst = -1;
    size_t at = lo;
    for (size_t i = lo + 1; i < hi; ++i) {
        const float d = segDist(p[i], p[lo], p[hi]);
        if (d > worst) { worst = d; at = i; }
    }
    if (worst > tol) {
        keep[at] = true;
        rdp(p, lo, at, tol, keep);
        rdp(p, at, hi, tol, keep);
    }
}

// --- bytes <-> numbers
uint8_t qpos(float v) {
    const float t = (clampf(v, kCarveMin, kCarveMax) - kCarveMin) / (kCarveMax - kCarveMin);
    return static_cast<uint8_t>(std::lround(t * 255.0f));
}
float dpos(uint8_t b) { return kCarveMin + (kCarveMax - kCarveMin) * (static_cast<float>(b) / 255.0f); }
uint8_t qscale(float v) {
    const float t = (clampf(v, kScaleMin, kScaleMax) - kScaleMin) / (kScaleMax - kScaleMin);
    return static_cast<uint8_t>(std::lround(t * 255.0f));
}
float dscale(uint8_t b) { return kScaleMin + (kScaleMax - kScaleMin) * (static_cast<float>(b) / 255.0f); }
constexpr float kTau = 6.28318530718f;
uint8_t qangle(float a) {
    float t = std::fmod(a, kTau);
    if (t < 0) t += kTau;
    return static_cast<uint8_t>(static_cast<int>(std::lround(t / kTau * 256.0f)) & 255);
}
float dangle(uint8_t b) { return static_cast<float>(b) * kTau / 256.0f; }

const char kB64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";

int b64val(char c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '-') return 62;
    if (c == '_') return 63;
    return -1;
}

constexpr uint8_t kVersion = 1;

}  // namespace

bool Carving::operator==(const Carving& o) const {
    if (variety != o.variety || ops.size() != o.ops.size()) return false;
    for (size_t i = 0; i < ops.size(); ++i)
        if (!sameOp(ops[i], o.ops[i])) return false;
    return true;
}

void clampStamp(Stamp& s) {
    s.at.x = clampf(s.at.x, kCarveMin, kCarveMax);
    s.at.y = clampf(s.at.y, kCarveMin, kCarveMax);
    s.scale = clampf(s.scale, kScaleMin, kScaleMax);
    s.angle = std::fmod(s.angle, kTau);
    if (s.angle < 0) s.angle += kTau;
}

std::vector<Vec2> simplify(const std::vector<Vec2>& points, float tolerance) {
    if (points.size() <= 2) return points;
    for (float tol = tolerance;; tol *= 1.5f) {
        std::vector<bool> keep(points.size(), false);
        keep.front() = keep.back() = true;
        rdp(points, 0, points.size() - 1, tol, keep);
        std::vector<Vec2> out;
        for (size_t i = 0; i < points.size(); ++i)
            if (keep[i]) out.push_back(points[i]);
        if (out.size() <= kMaxCutPoints) return out;
    }
}

bool strokeToCut(const std::vector<Vec2>& stroke, Cut& out) {
    if (stroke.size() < 2) return false;
    float length = 0, minx = stroke[0].x, maxx = minx, miny = stroke[0].y, maxy = miny;
    for (size_t i = 1; i < stroke.size(); ++i) {
        const float dx = stroke[i].x - stroke[i - 1].x, dy = stroke[i].y - stroke[i - 1].y;
        length += std::sqrt(dx * dx + dy * dy);
        minx = std::min(minx, stroke[i].x); maxx = std::max(maxx, stroke[i].x);
        miny = std::min(miny, stroke[i].y); maxy = std::max(maxy, stroke[i].y);
    }
    if (length < 0.06f) return false;  // a tap, not a cut
    const float diag = std::sqrt((maxx - minx) * (maxx - minx) + (maxy - miny) * (maxy - miny));
    const float gx = stroke.back().x - stroke.front().x, gy = stroke.back().y - stroke.front().y;
    const float gap = std::sqrt(gx * gx + gy * gy);
    // Closed: the end comes back near the start, and the loop has room inside.
    const bool closed = stroke.size() >= 4 && gap < std::max(0.12f, 0.3f * diag) && diag > 0.08f && length > 2.2f * gap + 0.1f;
    std::vector<Vec2> pts = simplify(stroke, 0.006f);
    if (closed) {
        if (pts.size() > 3 && same(pts.front(), pts.back())) pts.pop_back();
        if (pts.size() < 3) return false;
    }
    out.points = std::move(pts);
    out.closed = closed;
    // Quantise now, so a carving is the same before and after a link.
    for (Vec2& p : out.points) { p.x = dpos(qpos(p.x)); p.y = dpos(qpos(p.y)); }
    return true;
}

std::string encode(const Carving& c) {
    std::vector<uint8_t> b;
    b.push_back(kVersion);
    b.push_back(static_cast<uint8_t>(c.variety));
    size_t n = 0;
    for (const Op& op : c.ops) {
        if (n++ >= kMaxOps) break;
        if (op.isStamp) {
            const Stamp& s = op.stamp;
            b.push_back(static_cast<uint8_t>((s.shape & 0x3F) | (s.flip ? 0x40 : 0)));
            b.push_back(qpos(s.at.x));
            b.push_back(qpos(s.at.y));
            b.push_back(qscale(s.scale));
            b.push_back(qangle(s.angle));
        } else {
            const size_t count = std::min(op.cut.points.size(), kMaxCutPoints);
            b.push_back(static_cast<uint8_t>(0x80 | (op.cut.closed ? 0x40 : 0)));
            b.push_back(static_cast<uint8_t>(count));
            for (size_t i = 0; i < count; ++i) {
                b.push_back(qpos(op.cut.points[i].x));
                b.push_back(qpos(op.cut.points[i].y));
            }
        }
    }
    std::string out;
    size_t i = 0;
    for (; i + 3 <= b.size(); i += 3) {
        const uint32_t v = (uint32_t(b[i]) << 16) | (uint32_t(b[i + 1]) << 8) | b[i + 2];
        out += kB64[(v >> 18) & 63]; out += kB64[(v >> 12) & 63]; out += kB64[(v >> 6) & 63]; out += kB64[v & 63];
    }
    if (b.size() - i == 1) {
        const uint32_t v = uint32_t(b[i]) << 16;
        out += kB64[(v >> 18) & 63]; out += kB64[(v >> 12) & 63];
    } else if (b.size() - i == 2) {
        const uint32_t v = (uint32_t(b[i]) << 16) | (uint32_t(b[i + 1]) << 8);
        out += kB64[(v >> 18) & 63]; out += kB64[(v >> 12) & 63]; out += kB64[(v >> 6) & 63];
    }
    return out;
}

bool decode(const std::string& code, Carving& out) {
    if (code.size() > 4096 || code.size() % 4 == 1) return false;
    std::vector<uint8_t> b;
    uint32_t acc = 0;
    int bits = 0;
    for (char ch : code) {
        const int v = b64val(ch);
        if (v < 0) return false;
        acc = (acc << 6) | static_cast<uint32_t>(v);
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            b.push_back(static_cast<uint8_t>((acc >> bits) & 0xFF));
        }
    }
    if (b.size() < 2 || b[0] != kVersion || b[1] >= VarietyCount) return false;
    Carving c;
    c.variety = static_cast<Variety>(b[1]);
    size_t i = 2;
    while (i < b.size()) {
        if (c.ops.size() >= kMaxOps) return false;
        const uint8_t tag = b[i++];
        Op op;
        if (!(tag & 0x80)) {
            if (i + 4 > b.size() || (tag & 0x3F) >= ShapeCount) return false;
            op.isStamp = true;
            op.stamp.shape = static_cast<Shape>(tag & 0x3F);
            op.stamp.flip = (tag & 0x40) != 0;
            op.stamp.at = {dpos(b[i]), dpos(b[i + 1])};
            op.stamp.scale = dscale(b[i + 2]);
            op.stamp.angle = dangle(b[i + 3]);
            i += 4;
        } else {
            if (tag & 0x3F) return false;
            if (i >= b.size()) return false;
            const size_t count = b[i++];
            if (count < 2 || count > kMaxCutPoints || i + 2 * count > b.size()) return false;
            op.isStamp = false;
            op.cut.closed = (tag & 0x40) != 0;
            if (op.cut.closed && count < 3) return false;
            for (size_t k = 0; k < count; ++k, i += 2) op.cut.points.push_back({dpos(b[i]), dpos(b[i + 1])});
        }
        c.ops.push_back(std::move(op));
    }
    out = std::move(c);
    return true;
}

void quantiseStamp(Stamp& s) {
    clampStamp(s);
    s.at = {dpos(qpos(s.at.x)), dpos(qpos(s.at.y))};
    s.scale = dscale(qscale(s.scale));
    s.angle = dangle(qangle(s.angle));
}

}  // namespace pumpkin
