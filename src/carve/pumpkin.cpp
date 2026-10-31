#include "carve/pumpkin.h"

#include <algorithm>
#include <cmath>

namespace pumpkin {

namespace {

constexpr float kPi = 3.14159265359f;

float clamp01(float v) { return v < 0 ? 0 : (v > 1 ? 1 : v); }
float smooth(float a, float b, float x) {
    const float t = clamp01((x - a) / (b - a));
    return t * t * (3 - 2 * t);
}
float mix(float a, float b, float t) { return a + (b - a) * t; }
uint8_t byte(float v) { return static_cast<uint8_t>(std::lround(clamp01(v) * 255.0f)); }

const Look kLooks[VarietyCount] = {
    // radius height ribs   skin                  crease               stem
    {1.00f, 0.82f, 0.075f, {0.93f, 0.40f, 0.05f}, {0.62f, 0.20f, 0.02f}, {0.30f, 0.26f, 0.10f}},  // orange, round
    {0.86f, 1.00f, 0.065f, {0.95f, 0.46f, 0.07f}, {0.66f, 0.24f, 0.03f}, {0.28f, 0.30f, 0.10f}},  // orange, tall
    {1.00f, 0.80f, 0.07f, {0.90f, 0.88f, 0.80f}, {0.66f, 0.64f, 0.56f}, {0.36f, 0.40f, 0.20f}},   // white
    {1.10f, 0.66f, 0.085f, {0.40f, 0.52f, 0.20f}, {0.24f, 0.32f, 0.10f}, {0.34f, 0.28f, 0.14f}},  // green, squat
    {1.10f, 0.68f, 0.09f, {0.96f, 0.52f, 0.10f}, {0.68f, 0.28f, 0.04f}, {0.32f, 0.27f, 0.11f}},   // orange, squat
};

// The ribs: 1 on a rib's crown, down to 1 - ribs in the crease between two,
// fading out towards the top and bottom.
float rib(const Look& k, float phi, float theta) {
    const float c = std::fabs(std::cos(5 * phi));
    return 1 - k.ribs * (1 - std::sqrt(c)) * std::sqrt(std::max(0.0f, std::sin(theta)));
}

// Height of the latitude theta: flattened top with a dimple for the stem.
float heightAt(const Look& k, float theta) {
    const float c = std::cos(theta);
    const float dimple = 1 - 0.22f * std::exp(-(theta / 0.42f) * (theta / 0.42f));
    const float bottom = c < 0 ? 0.92f : 1.0f;
    return k.height * c * dimple * bottom;
}

float widthAt(float theta) {
    const float s = std::sin(theta);
    return s * (1 + 0.05f * s);  // fuller round the middle
}

}  // namespace

const Look& lookOf(Variety v) { return kLooks[v < VarietyCount ? v : 0]; }

Vec3 surfacePoint(Variety v, float u, float vv) {
    const Look& k = lookOf(v);
    const float theta = vv * kPi, phi = (u - 0.5f) * 2 * kPi;
    const float s = k.radius * widthAt(theta) * rib(k, phi, theta) / 1.05f;
    return {s * std::sin(phi), heightAt(k, theta), s * std::cos(phi)};
}

float bottomOf(Variety v) {
    float lo = 0;
    for (int i = 0; i <= 64; ++i) lo = std::min(lo, surfacePoint(v, 0.5f, 0.5f + 0.5f * static_cast<float>(i) / 64).y);
    return lo;
}

float topOf(Variety v) { return surfacePoint(v, 0.5f, 0).y; }

bool frontPoint(Variety v, float x, float y, Vec3& out) {
    const Look& k = lookOf(v);
    // Latitude from height: heightAt falls steadily between these.
    float a = 0.35f, b = kPi - 0.2f;
    if (y > heightAt(k, a) || y < heightAt(k, b)) return false;
    for (int i = 0; i < 28; ++i) {
        const float m = 0.5f * (a + b);
        if (heightAt(k, m) > y) a = m; else b = m;
    }
    const float theta = 0.5f * (a + b);
    // Longitude from x, on the front half.
    float lo = -kPi / 2, hi = kPi / 2;
    auto xAt = [&](float phi) { return k.radius * widthAt(theta) * rib(k, phi, theta) / 1.05f * std::sin(phi); };
    if (x < xAt(lo) || x > xAt(hi)) return false;
    for (int i = 0; i < 28; ++i) {
        const float m = 0.5f * (lo + hi);
        if (xAt(m) < x) lo = m; else hi = m;
    }
    const float phi = 0.5f * (lo + hi);
    out = surfacePoint(v, phi / (2 * kPi) + 0.5f, theta / kPi);
    return true;
}

MeshData buildBody(Variety v, int around, int down, float grow) {
    MeshData m;
    for (int j = 0; j <= down; ++j) {
        for (int i = 0; i <= around; ++i) {
            const float u = static_cast<float>(i) / around, vv = static_cast<float>(j) / down;
            const Vec3 p = surfacePoint(v, u, vv);
            m.positions.insert(m.positions.end(), {p.x * grow, p.y * grow, p.z * grow});
            m.uvs.insert(m.uvs.end(), {u, vv});
        }
    }
    const uint32_t row = static_cast<uint32_t>(around + 1);
    for (int j = 0; j < down; ++j) {
        for (int i = 0; i < around; ++i) {
            const uint32_t a = static_cast<uint32_t>(j) * row + static_cast<uint32_t>(i);
            // Counter-clockwise seen from outside: u runs to the viewer's right, v down.
            m.indices.insert(m.indices.end(), {a, a + row, a + 1, a + 1, a + row, a + row + 1});
        }
    }
    return m;
}

MeshData buildStem(Variety v) {
    const Look& k = lookOf(v);
    MeshData m;
    const int sides = 8, rings = 7;
    const float base = topOf(v) - 0.04f;
    const float length = 0.32f * std::min(1.0f, k.radius);
    for (int r = 0; r <= rings; ++r) {
        const float t = static_cast<float>(r) / rings;
        const float radius = mix(0.11f, 0.065f, t) * (r == rings ? 0.9f : 1.0f);
        const float cx = 0.10f * t * t, cy = base + length * t;  // it bends over a little
        for (int s = 0; s <= sides; ++s) {
            const float a = 2 * kPi * static_cast<float>(s) / sides;
            const float wobble = 1 + 0.12f * std::cos(4 * a);
            m.positions.insert(m.positions.end(), {cx + radius * wobble * std::cos(a), cy, radius * wobble * std::sin(a)});
            m.uvs.insert(m.uvs.end(), {0, 0});
            const float shade = 0.8f + 0.2f * std::cos(4 * a);
            m.colors.insert(m.colors.end(), {k.stem[0] * shade * (1.1f - 0.2f * t), k.stem[1] * shade, k.stem[2] * shade});
        }
    }
    const uint32_t row = sides + 1;
    for (uint32_t r = 0; r < static_cast<uint32_t>(rings); ++r) {
        for (uint32_t s = 0; s < static_cast<uint32_t>(sides); ++s) {
            const uint32_t a = r * row + s;
            // Seen from outside, s runs clockwise about +y (angle grows from +x to +z), r up.
            m.indices.insert(m.indices.end(), {a, a + row, a + 1, a + 1, a + row, a + row + 1});
        }
    }
    // A cut top.
    const uint32_t centre = static_cast<uint32_t>(m.positions.size() / 3);
    const float t = 1, cx = 0.10f * t * t, cy = base + length;
    m.positions.insert(m.positions.end(), {cx, cy, 0});
    m.uvs.insert(m.uvs.end(), {0, 0});
    m.colors.insert(m.colors.end(), {k.stem[0] * 1.5f, k.stem[1] * 1.4f, k.stem[2] * 1.2f});
    const uint32_t top = static_cast<uint32_t>(rings) * row;
    for (uint32_t s = 0; s < static_cast<uint32_t>(sides); ++s) m.indices.insert(m.indices.end(), {centre, top + s + 1, top + s});
    return m;
}

Textures paint(Variety v, const Mask& mask, int width, int height) {
    const Look& k = lookOf(v);
    Textures t;
    t.width = width;
    t.height = height;
    t.skin.resize(static_cast<size_t>(width) * height * 4);
    t.glow.resize(t.skin.size());
    std::vector<float> ribShade(static_cast<size_t>(width)), streak(static_cast<size_t>(width));
    for (int i = 0; i < width; ++i) {
        const float u = (static_cast<float>(i) + 0.5f) / static_cast<float>(width);
        const float phi = (u - 0.5f) * 2 * kPi;
        ribShade[static_cast<size_t>(i)] = std::sqrt(std::fabs(std::cos(5 * phi)));
        // Faint lengthwise streaks, the same on every pumpkin.
        streak[static_cast<size_t>(i)] = 0.04f * std::sin(phi * 37.0f) + 0.03f * std::sin(phi * 83.0f + 1.3f);
    }
    for (int j = 0; j < height; ++j) {
        const float vv = (static_cast<float>(j) + 0.5f) / static_cast<float>(height);
        const float theta = vv * kPi;
        const float pole = std::exp(-(theta / 0.5f) * (theta / 0.5f)) + 0.6f * std::exp(-((kPi - theta) / 0.5f) * ((kPi - theta) / 0.5f));
        for (int i = 0; i < width; ++i) {
            const float u = (static_cast<float>(i) + 0.5f) / static_cast<float>(width);
            const size_t o = (static_cast<size_t>(j) * width + i) * 4;
            const float r = ribShade[static_cast<size_t>(i)];
            float skin[3];
            for (int c = 0; c < 3; ++c) {
                skin[c] = mix(k.crease[c], k.skin[c], 0.35f + 0.65f * r) * (1 + streak[static_cast<size_t>(i)]);
                skin[c] = mix(skin[c], k.crease[c] * 0.8f, 0.5f * pole);
            }
            float cut = 0, deep = 0;
            if (u > 0.2f && u < 0.8f) {
                const Vec3 p = surfacePoint(v, u, vv);
                const float facing = smooth(0.02f, 0.3f, p.z / std::max(0.2f, std::sqrt(p.x * p.x + p.z * p.z)));
                if (facing > 0) {
                    cut = mask.sample(p.x, p.y) * facing;
                    deep = mask.sampleGlow(p.x, p.y) * facing;
                }
            }
            // The cut edge: pale flesh in a thin ring just outside each hole.
            const float rim = smooth(0.04f, 0.32f, deep) * (1 - smooth(0.35f, 0.6f, cut));
            const float flesh[3] = {0.96f, 0.78f, 0.38f};
            const float inside[3] = {0.16f, 0.07f, 0.02f};
            for (int c = 0; c < 3; ++c) {
                float s = mix(skin[c], flesh[c], rim);
                s = mix(s, inside[c], smooth(0.35f, 0.65f, cut));
                t.skin[o + c] = byte(s);
            }
            t.skin[o + 3] = 255;
            const float hot = smooth(0.35f, 0.95f, deep);
            const float g[3] = {1.0f, mix(0.42f, 0.86f, hot), mix(0.06f, 0.42f, hot)};
            for (int c = 0; c < 3; ++c) t.glow[o + c] = byte(g[c]);
            t.glow[o + 3] = byte(std::max(smooth(0.3f, 0.7f, cut), 0.55f * rim));
        }
    }
    return t;
}

}  // namespace pumpkin
