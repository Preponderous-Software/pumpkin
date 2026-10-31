#include "view/view.h"

#include <algorithm>
#include <cmath>

#include "carve/mask.h"

namespace pumpkin {

namespace {

constexpr float kPi = 3.14159265359f;
constexpr float kAway = -1000.0f;  // where hidden meshes wait (below the world)
constexpr float kPorchScale = 0.55f;
constexpr int kPorchRow = 5;

// The candle: a few sines, never quite repeating; 0.68 .. 1.02.
float candle(double t, float phase) {
    const double a = t + phase;
    return static_cast<float>(0.85 + 0.08 * std::sin(7.3 * a) + 0.05 * std::sin(13.1 * a + 1.0) + 0.04 * std::sin(23.7 * a + 2.0));
}
int candleLevel(float f) { return std::max(0, std::min(3, static_cast<int>((f - 0.68f) / 0.34f * 4))); }

void model(float out[16], const float pos[3], float scale) {
    for (int i = 0; i < 16; ++i) out[i] = 0;
    out[0] = out[5] = out[10] = scale;
    out[12] = pos[0];
    out[13] = pos[1];
    out[14] = pos[2];
    out[15] = 1;
}

StrataRenderItem cube(float x, float y, float z, float sx, float sy, float sz, float r, float g, float b) {
    StrataRenderItem it{};
    it.kind = STRATA_PRIMITIVE_CUBE;
    it.position[0] = x; it.position[1] = y; it.position[2] = z;
    it.scale[0] = sx; it.scale[1] = sy; it.scale[2] = sz;
    it.color[0] = r; it.color[1] = g; it.color[2] = b; it.color[3] = 1;
    return it;
}

uint32_t addMesh(strata::Engine& e, const MeshData& m, uint32_t texture, uint32_t flags, const float color[4]) {
    StrataTexturedMesh tm{};
    tm.mesh.positions = m.positions.data();
    tm.mesh.normals = nullptr;
    tm.mesh.colors = m.colors.empty() ? nullptr : m.colors.data();
    tm.mesh.indices = m.indices.data();
    tm.mesh.vertex_count = static_cast<int32_t>(m.positions.size() / 3);
    tm.mesh.index_count = static_cast<int32_t>(m.indices.size());
    for (int i = 0; i < 4; ++i) tm.mesh.color[i] = color[i];
    tm.uvs = m.uvs.data();
    tm.texture = texture;
    tm.flags = flags;
    return e.addTexturedMesh(tm);
}

void porchSpot(int i, int n, Variety v, float pos[3]) {
    const bool twoRows = n > kPorchRow;
    const int row = i < kPorchRow ? 0 : 1;
    const int inRow = row == 0 ? std::min(n, kPorchRow) : n - kPorchRow;
    const int k = row == 0 ? i : i - kPorchRow;
    const float spacing = 1.3f;
    pos[0] = (static_cast<float>(k) - 0.5f * static_cast<float>(inRow - 1)) * spacing;
    const float ground = twoRows && row == 1 ? -0.5f : 0.0f;
    pos[1] = ground - bottomOf(v) * kPorchScale;
    pos[2] = twoRows ? (row == 0 ? -0.35f : 1.25f) : 0.2f;
}

}  // namespace

View::View(strata::Engine& engine) : engine_(engine) {
    // A window in the house wall behind: dark glass by day, warm by night.
    MeshData q;
    q.positions = {-0.6f, -0.5f, 0, 0.6f, -0.5f, 0, 0.6f, 0.5f, 0, -0.6f, 0.5f, 0};
    q.uvs = {0, 1, 1, 1, 1, 0, 0, 0};
    q.indices = {0, 1, 2, 0, 2, 3};
    const float warm[4] = {1.0f, 0.72f, 0.36f, 1};
    window_ = addMesh(engine_, q, 0, STRATA_MESH_UNLIT, warm);
    // Stars, fixed in the sky.
    uint32_t s = 12345;
    auto rnd = [&s]() { s = s * 1664525u + 1013904223u; return static_cast<float>(s >> 8) / 16777216.0f; };
    for (int i = 0; i < 90; ++i) {
        stars_.push_back(-30 + 60 * rnd());
        stars_.push_back(4 + 18 * rnd());
        stars_.push_back(-20 - 5 * rnd());
        stars_.push_back(1.5f + 2.5f * rnd());
    }
}

void View::setLayout(int width, int height, float coverTop, float coverBottom) {
    width_ = std::max(1, width);
    height_ = std::max(1, height);
    coverTop_ = std::max(0.0f, coverTop);
    coverBottom_ = std::max(0.0f, coverBottom);
}

void View::destroy(Gfx& g) {
    if (!g.live) return;
    engine_.removeMesh(g.skin);
    engine_.removeMesh(g.stem);
    for (uint32_t id : g.glow) engine_.removeMesh(id);
    engine_.destroyTexture(g.skinTex);
    engine_.destroyTexture(g.glowTex);
    g.live = false;
}

void View::build(Gfx& g, const Carving& c, int texW, int texH) {
    const Textures t = paint(c.variety, buildMask(c, texW >= 1024 ? 384 : 256), texW, texH);
    if (g.live && g.variety == c.variety && g.texW == texW) {
        engine_.updateTextureRgba8(g.skinTex, 0, 0, texW, texH, t.skin.data());
        engine_.updateTextureRgba8(g.glowTex, 0, 0, texW, texH, t.glow.data());
        return;
    }
    destroy(g);
    g.variety = c.variety;
    g.texW = texW;
    g.texH = texH;
    g.skinTex = engine_.createTextureRgba8(texW, texH, t.skin.data(), STRATA_TEXTURE_CLAMP_V);
    g.glowTex = engine_.createTextureRgba8(texW, texH, t.glow.data(), STRATA_TEXTURE_CLAMP_V);
    const float white[4] = {1, 1, 1, 1};
    g.skin = addMesh(engine_, buildBody(c.variety, 96, 48), g.skinTex, 0, white);
    g.stem = addMesh(engine_, buildStem(c.variety), 0, 0, white);
    const MeshData shell = buildBody(c.variety, 96, 48, 1.004f);
    const float levels[4] = {0.72f, 0.82f, 0.92f, 1.0f};
    for (int i = 0; i < 4; ++i) {
        const float col[4] = {levels[i], levels[i], levels[i], 1};
        g.glow[static_cast<size_t>(i)] = addMesh(engine_, shell, g.glowTex, STRATA_MESH_UNLIT | STRATA_MESH_TRANSLUCENT, col);
    }
    const float day[4] = {1, 0.85f, 0.6f, 0.22f};
    g.glow[4] = addMesh(engine_, shell, g.glowTex, STRATA_MESH_UNLIT | STRATA_MESH_TRANSLUCENT, day);
    g.live = true;
}

void View::place(Gfx& g, bool shown, int level, bool dayGlow) {
    if (!g.live) return;
    float m[16];
    const float away[3] = {0, kAway, 0};
    model(m, shown ? g.pos : away, g.scale);
    engine_.setMeshTransform(g.skin, m);
    engine_.setMeshTransform(g.stem, m);
    float hidden[16];
    model(hidden, away, g.scale);
    for (int i = 0; i < 5; ++i) {
        const bool on = shown && (dayGlow ? i == 4 : i == level);
        engine_.setMeshTransform(g.glow[static_cast<size_t>(i)], on ? m : hidden);
    }
}

void View::showCarving(const Carving& c, unsigned revision) {
    if (carve_.live && revision == carveRevision_) return;
    carveRevision_ = revision;
    build(carve_, c, 1024, 512);
}

void View::setPorch(const std::vector<Carving>& pumpkins) {
    for (Gfx& g : porch_) destroy(g);
    porch_.clear();
    const int n = std::min(static_cast<int>(pumpkins.size()), 2 * kPorchRow);
    porch_.resize(static_cast<size_t>(n));
    for (int i = 0; i < n; ++i) {
        Gfx& g = porch_[static_cast<size_t>(i)];
        build(g, pumpkins[static_cast<size_t>(i)], 512, 256);
        porchSpot(i, n, g.variety, g.pos);
        g.scale = kPorchScale;
    }
    porchChosen_ = -1;
}

void View::camera(float& tanHalf, float eye[3], float target[3]) const {
    // The box to frame: half its width, half its height, its centre, and how far back to stand.
    float halfW, halfH, cy, dist;
    if (mode_ == Mode::Carve) {
        halfW = 1.3f; halfH = 1.08f; cy = 0.12f; dist = 4.6f;
    } else {
        const bool twoRows = porch_.size() > static_cast<size_t>(kPorchRow);
        const int across = std::max(3, std::min(static_cast<int>(porch_.size()), kPorchRow));
        halfW = 0.65f * static_cast<float>(across) + 0.25f; halfH = twoRows ? 1.25f : 0.9f; cy = twoRows ? 0.0f : 0.3f; dist = 8.5f;
    }
    const float aspect = static_cast<float>(width_) / static_cast<float>(height_);
    const float free = std::max(0.3f, (static_cast<float>(height_) - coverTop_ - coverBottom_) / static_cast<float>(height_));
    tanHalf = std::max(halfH / dist / free, halfW / dist / aspect);
    tanHalf = std::max(tanHalf, std::tan(15.0f * kPi / 180));
    // Raise the camera so the box sits in the middle of what the bars leave.
    const float shiftNdc = (coverBottom_ - coverTop_) / static_cast<float>(height_);
    const float lift = -shiftNdc * dist * tanHalf;
    const float rise = mode_ == Mode::Carve ? 0.35f : 2.2f;
    eye[0] = 0; eye[1] = cy + rise + lift; eye[2] = dist + (mode_ == Mode::Carve ? 0.0f : 0.4f);
    target[0] = 0; target[1] = cy + lift; target[2] = mode_ == Mode::Carve ? 0.0f : 0.4f;
}

void View::ray(float px, float py, float origin[3], float dir[3]) const {
    float tanHalf, eye[3], target[3];
    camera(tanHalf, eye, target);
    const float aspect = static_cast<float>(width_) / static_cast<float>(height_);
    float f[3] = {target[0] - eye[0], target[1] - eye[1], target[2] - eye[2]};
    const float fl = std::sqrt(f[0] * f[0] + f[1] * f[1] + f[2] * f[2]);
    for (float& v : f) v /= fl;
    // right = f x up(0,1,0); up' = right x f
    float r[3] = {-f[2], 0, f[0]};
    const float rl = std::sqrt(r[0] * r[0] + r[2] * r[2]);
    r[0] /= rl; r[2] /= rl;
    const float u[3] = {r[1] * f[2] - r[2] * f[1], r[2] * f[0] - r[0] * f[2], r[0] * f[1] - r[1] * f[0]};
    const float nx = 2 * px / static_cast<float>(width_) - 1, ny = 1 - 2 * py / static_cast<float>(height_);
    for (int i = 0; i < 3; ++i) {
        origin[i] = eye[i];
        dir[i] = f[i] + r[i] * nx * tanHalf * aspect + u[i] * ny * tanHalf;
    }
}

bool View::carvePointAt(float px, float py, Vec2& out) {
    if (mode_ != Mode::Carve || !carve_.live) return false;
    float o[3], d[3];
    ray(px, py, o, d);
    const StrataRayHit hit = engine_.raycastMeshes(o, d, 100.0f);
    bool ours = hit.mesh_id == carve_.skin;
    for (uint32_t id : carve_.glow) ours = ours || hit.mesh_id == id;
    if (!ours || hit.point[2] < 0) return false;
    out = {hit.point[0], hit.point[1]};
    return true;
}

int View::porchPumpkinAt(float px, float py) {
    if (mode_ != Mode::Porch) return -1;
    float o[3], d[3];
    ray(px, py, o, d);
    const StrataRayHit hit = engine_.raycastMeshes(o, d, 100.0f);
    if (!hit.mesh_id) return -1;
    for (size_t i = 0; i < porch_.size(); ++i) {
        const Gfx& g = porch_[i];
        if (hit.mesh_id == g.skin || hit.mesh_id == g.stem) return static_cast<int>(i);
        for (uint32_t id : g.glow)
            if (hit.mesh_id == id) return static_cast<int>(i);
    }
    return -1;
}

bool View::screenOf(Vec2 c, float& px, float& py) const {
    Vec3 p;
    if (!frontPoint(carve_.live ? carve_.variety : OrangeRound, c.x, c.y, p)) return false;
    float tanHalf, eye[3], target[3];
    camera(tanHalf, eye, target);
    const float aspect = static_cast<float>(width_) / static_cast<float>(height_);
    float f[3] = {target[0] - eye[0], target[1] - eye[1], target[2] - eye[2]};
    const float fl = std::sqrt(f[0] * f[0] + f[1] * f[1] + f[2] * f[2]);
    for (float& v : f) v /= fl;
    float r[3] = {-f[2], 0, f[0]};
    const float rl = std::sqrt(r[0] * r[0] + r[2] * r[2]);
    r[0] /= rl; r[2] /= rl;
    const float u[3] = {r[1] * f[2] - r[2] * f[1], r[2] * f[0] - r[0] * f[2], r[0] * f[1] - r[1] * f[0]};
    const float rel[3] = {p.x - eye[0], p.y - eye[1], p.z - eye[2]};
    const float depth = rel[0] * f[0] + rel[1] * f[1] + rel[2] * f[2];
    if (depth <= 0) return false;
    const float nx = (rel[0] * r[0] + rel[1] * r[1] + rel[2] * r[2]) / (depth * tanHalf * aspect);
    const float ny = (rel[0] * u[0] + rel[1] * u[1] + rel[2] * u[2]) / (depth * tanHalf);
    px = (nx + 1) * 0.5f * static_cast<float>(width_);
    py = (1 - ny) * 0.5f * static_cast<float>(height_);
    return true;
}

float View::unitsPerPixel() const {
    float tanHalf, eye[3], target[3];
    camera(tanHalf, eye, target);
    return 2 * (eye[2] - 1.0f) * tanHalf / static_cast<float>(height_);
}

void View::drawScene(double t, bool overlays) {
    float tanHalf, eye[3], target[3];
    camera(tanHalf, eye, target);
    const float up[3] = {0, 1, 0};
    const float aspect = static_cast<float>(width_) / static_cast<float>(height_);
    const float fovDeg = 2 * std::atan(tanHalf) * 180 / kPi;  // Strata takes degrees
    engine_.setCamera(strata::cameraLookAt(eye, target, up, fovDeg, aspect, 0.1f, 80.0f));

    StrataLighting light = strata::lightingDefaults(STRATA_LIGHTING_SPACE);
    light.legacy_ambient = dark_ ? 0.05f : 0.42f;
    engine_.setLighting(light);

    const bool carve = mode_ == Mode::Carve;
    const float ground = carve ? (carve_.live ? bottomOf(carve_.variety) : -0.8f) : 0.0f;

    std::vector<StrataPointLight> lights;
    auto pointLight = [&](float x, float y, float z, float range, float r, float g, float b, float k) {
        StrataPointLight p{};
        p.position[0] = x; p.position[1] = y; p.position[2] = z;
        p.range = range;
        p.color[0] = r; p.color[1] = g; p.color[2] = b;
        p.intensity = k;
        lights.push_back(p);
    };
    if (dark_) {
        pointLight(-6, 9, 8, 30, 0.55f, 0.62f, 1.0f, 0.22f);  // the moon
    } else {
        pointLight(-4, 5, 7, 30, 1.0f, 0.95f, 0.88f, 1.1f);
        pointLight(5, 2, 5, 20, 0.7f, 0.75f, 1.0f, 0.35f);
    }
    // A candle in every pumpkin: light on the boards round it.
    const float f0 = candle(t, 0);
    if (carve && carve_.live) {
        pointLight(0, ground + 0.35f, 0.85f, dark_ ? 3.4f : 2.0f, 1.0f, 0.55f, 0.18f, (dark_ ? 1.3f : 0.3f) * f0);
    } else if (!carve) {
        for (size_t i = 0; i < porch_.size() && lights.size() < 30; ++i) {
            const Gfx& g = porch_[i];
            const float fi = candle(t, 1.7f * static_cast<float>(i));
            pointLight(g.pos[0], g.pos[1] + bottomOf(g.variety) * g.scale + 0.2f, g.pos[2] + 0.55f, dark_ ? 2.2f : 1.2f, 1.0f, 0.55f,
                       0.18f, (dark_ ? 1.1f : 0.25f) * fi);
        }
    }
    engine_.setPointLights(lights.data(), lights.size());

    // Pumpkins: the carving's in carve mode, the porch's otherwise.
    carve_.scale = 1;
    place(carve_, carve, candleLevel(f0), !dark_);
    for (size_t i = 0; i < porch_.size(); ++i)
        place(porch_[i], !carve, candleLevel(candle(t, 1.7f * static_cast<float>(i))), !dark_);
    {
        float m[16];
        const float at[3] = {-2.6f, ground + 2.0f, -3.335f};
        const float away[3] = {0, kAway, 0};
        model(m, dark_ ? at : away, 1);
        engine_.setMeshTransform(window_, m);
    }

    StrataFrameDesc fd{};
    fd.clear_flags = STRATA_CLEAR_COLOR | STRATA_CLEAR_DEPTH;
    const float skyDay[3] = {0.30f, 0.22f, 0.36f}, skyNight[3] = {0.012f, 0.01f, 0.03f};
    for (int i = 0; i < 3; ++i) fd.clear_color[i] = dark_ ? skyNight[i] : skyDay[i];
    fd.clear_color[3] = 1;
    engine_.beginFrame(fd);

    // The porch: boards, the house wall with its siding, and (porch) a step.
    std::vector<StrataRenderItem> items;
    for (int b = 0; b < 22; ++b) {
        const float z = -3.3f + 0.3f * static_cast<float>(b);
        const float shade = (b % 3 == 0) ? 0.9f : (b % 3 == 1 ? 1.0f : 0.95f);
        const float y = carve || z < 0.55f ? ground : ground - 0.5f;
        if (!carve && z > 0.55f && z < 0.7f) continue;
        items.push_back(cube(0, y - 0.05f, z, 18, 0.1f, 0.27f, 0.36f * shade, 0.22f * shade, 0.12f * shade));
    }
    if (!carve) items.push_back(cube(0, -0.25f, 0.62f, 18, 0.5f, 0.1f, 0.28f, 0.17f, 0.09f));  // the step's face
    items.push_back(cube(0, ground - 0.3f, -0.1f, 18, 0.4f, 7.0f, 0.2f, 0.12f, 0.07f));  // under the boards
    for (int s = 0; s < 16; ++s) {
        const float y = ground + 0.18f + 0.36f * static_cast<float>(s);
        const float k = s % 2 ? 1.0f : 0.92f;
        items.push_back(cube(0, y, -3.5f, 18, 0.36f, 0.2f, 0.30f * k, 0.32f * k, 0.38f * k));
    }
    // The window frame (dark by day, round the warm glass by night).
    const float wx = -2.6f, wy = ground + 2.0f, wz = -3.36f;
    items.push_back(cube(wx, wy, wz - 0.01f, 1.3f, 1.1f, 0.04f, 0.10f, 0.12f, 0.16f));
    items.push_back(cube(wx, wy, wz + 0.03f, 0.06f, 1.1f, 0.04f, 0.85f, 0.82f, 0.75f));
    items.push_back(cube(wx, wy, wz + 0.03f, 1.3f, 0.06f, 0.04f, 0.85f, 0.82f, 0.75f));
    for (float sx : {-1.0f, 1.0f}) items.push_back(cube(wx + sx * 0.68f, wy, wz + 0.03f, 0.08f, 1.24f, 0.05f, 0.85f, 0.82f, 0.75f));
    for (float sy : {-1.0f, 1.0f}) items.push_back(cube(wx, wy + sy * 0.58f, wz + 0.03f, 1.44f, 0.08f, 0.05f, 0.85f, 0.82f, 0.75f));
    // A porch post on the right.
    items.push_back(cube(3.6f, ground + 2.5f, -1.2f, 0.25f, 5.0f, 0.25f, 0.80f, 0.77f, 0.70f));
    engine_.submitPrimitives(items.data(), items.size());

    if (dark_) {
        std::vector<StrataPoint> pts;
        for (size_t i = 0; i + 3 < stars_.size(); i += 4) {
            StrataPoint p{};
            p.position[0] = stars_[i]; p.position[1] = stars_[i + 1]; p.position[2] = stars_[i + 2];
            p.size_pixels = stars_[i + 3];
            p.color[0] = 0.9f; p.color[1] = 0.9f; p.color[2] = 1.0f; p.color[3] = 0.8f;
            p.flags = STRATA_POINT_SOFT;
            pts.push_back(p);
        }
        StrataPoint moon{};
        moon.position[0] = 9; moon.position[1] = 13; moon.position[2] = -24;
        moon.world_diameter = 3.2f;
        moon.color[0] = 1.0f; moon.color[1] = 0.96f; moon.color[2] = 0.82f; moon.color[3] = 1;
        pts.push_back(moon);
        StrataPoint halo = moon;
        halo.world_diameter = 9;
        halo.color[3] = 0.25f;
        halo.flags = STRATA_POINT_SOFT;
        pts.push_back(halo);
        engine_.submitPoints(pts.data(), pts.size());
    }

    if (overlays && carve && carve_.live) {
        std::vector<StrataLineVertex> lines;
        std::vector<StrataPoint> dots;
        auto onSkin = [&](Vec2 c, float out[3]) {
            Vec3 p;
            if (!frontPoint(carve_.variety, c.x, c.y, p)) return false;
            out[0] = p.x * 1.012f; out[1] = p.y * 1.012f; out[2] = p.z * 1.012f + 0.01f;
            return true;
        };
        auto path = [&](const std::vector<Vec2>& pts, bool loop, const float col[4], float dot) {
            float prev[3];
            bool havePrev = false;
            const size_t n = pts.size();
            for (size_t i = 0; i < n + (loop ? 1 : 0); ++i) {
                float p[3];
                if (!onSkin(pts[i % n], p)) { havePrev = false; continue; }
                if (havePrev) {
                    StrataLineVertex a{}, b{};
                    for (int k = 0; k < 3; ++k) { a.position[k] = prev[k]; b.position[k] = p[k]; }
                    for (int k = 0; k < 4; ++k) { a.color[k] = col[k]; b.color[k] = col[k]; }
                    lines.push_back(a);
                    lines.push_back(b);
                }
                if (dot > 0) {
                    StrataPoint d{};
                    for (int k = 0; k < 3; ++k) d.position[k] = p[k];
                    d.size_pixels = dot;
                    for (int k = 0; k < 4; ++k) d.color[k] = col[k];
                    d.flags = STRATA_POINT_ON_TOP;
                    dots.push_back(d);
                }
                for (int k = 0; k < 3; ++k) prev[k] = p[k];
                havePrev = true;
            }
        };
        const float px = std::max(2.0f, static_cast<float>(height_) / 220.0f);
        if (stroke_.size() >= 2) {
            // Fill the stroke in densely so it reads as a thick line.
            std::vector<Vec2> dense;
            for (size_t i = 1; i < stroke_.size(); ++i) {
                const Vec2 a = stroke_[i - 1], b = stroke_[i];
                const float len = std::hypot(b.x - a.x, b.y - a.y);
                const int steps = std::max(1, static_cast<int>(len / 0.01f));
                for (int s = 0; s < steps; ++s) {
                    const float k = static_cast<float>(s) / static_cast<float>(steps);
                    dense.push_back({a.x + (b.x - a.x) * k, a.y + (b.y - a.y) * k});
                }
            }
            dense.push_back(stroke_.back());
            const float col[4] = {1.0f, 0.93f, 0.55f, 0.95f};
            path(dense, false, col, px * 2.2f);
        }
        const float sel[4] = {0.55f, 0.95f, 1.0f, 0.95f};
        for (const auto& o : outline_) path(o, true, sel, px * 1.2f);
        if (!lines.empty()) engine_.submitLines(lines.data(), lines.size(), STRATA_LINES_ON_TOP);
        if (!dots.empty()) engine_.submitPoints(dots.data(), dots.size());
    }
    // The chosen porch pumpkin: a ring of light on the boards in front of it.
    if (overlays && !carve && porchChosen_ >= 0 && porchChosen_ < static_cast<int>(porch_.size())) {
        const Gfx& g = porch_[static_cast<size_t>(porchChosen_)];
        std::vector<StrataLineVertex> ring;
        const float y = g.pos[1] + bottomOf(g.variety) * g.scale + 0.02f;
        for (int i = 0; i < 48; ++i) {
            for (int e = 0; e < 2; ++e) {
                const float a = 2 * kPi * static_cast<float>(i + e) / 48;
                StrataLineVertex v{};
                v.position[0] = g.pos[0] + 0.72f * std::cos(a);
                v.position[1] = y;
                v.position[2] = g.pos[2] + 0.6f * std::sin(a);
                v.color[0] = 0.55f; v.color[1] = 0.95f; v.color[2] = 1.0f; v.color[3] = 1;
                ring.push_back(v);
            }
        }
        engine_.submitLines(ring.data(), ring.size(), STRATA_LINES_ON_TOP);
    }
    engine_.endFrame();
}

void View::frame(double seconds) {
    time_ += seconds;
    drawScene(time_, true);
}

void View::snapshot(std::vector<uint8_t>& rgba, int& width, int& height) {
    const bool wasDark = dark_;
    dark_ = true;
    drawScene(time_, false);
    int32_t w = 0, h = 0;
    engine_.readPixels(rgba, w, h);
    width = w;
    height = h;
    dark_ = wasDark;
}

}  // namespace pumpkin
