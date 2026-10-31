// Carving: face pieces and freehand cuts make holes where they should, undo
// and start over put things back, and the pumpkin's pictures show the holes.
#include <cmath>
#include <vector>

#include "carve/editor.h"
#include "carve/mask.h"
#include "carve/pumpkin.h"
#include "carve/shapes.h"
#include "check.h"

using namespace pumpkin;

namespace {
std::vector<Vec2> circle(float cx, float cy, float r, int n = 32) {
    std::vector<Vec2> p;
    for (int i = 0; i <= n; ++i) {
        const float a = 6.2831853f * static_cast<float>(i) / static_cast<float>(n);
        p.push_back({cx + r * std::cos(a), cy + r * std::sin(a)});
    }
    return p;
}
}  // namespace

int main() {
    // Every shape is a real outline with its centre region mostly inside its box.
    for (int s = 0; s < ShapeCount; ++s) {
        const auto& polys = shapeOutline(static_cast<Shape>(s));
        CHECK(!polys.empty());
        int inside = 0;
        for (int y = 0; y < 40; ++y)
            for (int x = 0; x < 40; ++x) {
                Vec2 q{-1 + (static_cast<float>(x) + 0.5f) / 20, -1 + (static_cast<float>(y) + 0.5f) / 20};
                for (const auto& p : polys) if (insidePolygon(p, q)) { ++inside; break; }
            }
        CHECK(inside > 40);    // a real hole
        CHECK(inside < 1500);  // not the whole box
        CHECK(!shapeSvgPath(static_cast<Shape>(s)).empty());
    }

    Editor e;
    CHECK(!e.canUndo());
    CHECK(buildMask(e.carving()).cutCount() == 0);

    // A round eye cuts a hole at its centre and nowhere far off.
    e.addStamp(RoundEye);
    CHECK(e.selected() == 0);
    const Stamp eye = *e.selectedStamp();
    CHECK(eye.at.x < 0);
    Mask m = buildMask(e.carving());
    CHECK(m.sample(eye.at.x, eye.at.y) > 0.9f);
    CHECK(m.sample(-eye.at.x, eye.at.y) < 0.1f);
    CHECK(m.sample(0, -0.5f) < 0.1f);

    // The second eye mirrors the first.
    e.addStamp(RoundEye);
    const Stamp eye2 = *e.selectedStamp();
    CHECK(std::fabs(eye2.at.x + eye.at.x) < 0.02f);
    CHECK(eye2.flip != eye.flip);
    m = buildMask(e.carving());
    CHECK(m.sample(-eye.at.x, eye.at.y) > 0.9f);

    // Moving the chosen piece moves its hole; one undo puts it back.
    e.beginGesture();
    e.moveSelected(0, -0.3f);
    e.endGesture();
    m = buildMask(e.carving());
    CHECK(m.sample(eye2.at.x, eye2.at.y - 0.3f) > 0.9f);
    CHECK(m.sample(eye2.at.x, eye2.at.y) < 0.1f);
    e.undo();
    CHECK(std::fabs(e.selectedStamp()->at.y - eye2.at.y) < 1e-4f);
    // A gesture that changes nothing leaves nothing to undo.
    const size_t before = e.carving().ops.size();
    e.beginGesture();
    e.endGesture();
    e.undo();  // undoes the second eye
    CHECK(e.carving().ops.size() == before - 1);

    // Bigger, turn, flip, twin, delete.
    e.select(0);
    const float s0 = e.selectedStamp()->scale;
    e.edit(Edit::Bigger);
    CHECK(e.selectedStamp()->scale > s0);
    e.edit(Edit::TurnLeft);
    CHECK(e.selectedStamp()->angle > 0.2f);
    e.edit(Edit::Twin);
    CHECK(e.carving().ops.size() == 2);
    CHECK(e.selected() == 1);
    e.edit(Edit::Delete);
    CHECK(e.carving().ops.size() == 1);
    CHECK(e.selected() == -1);
    e.edit(Edit::Done);

    // Tapping a piece finds it.
    CHECK(e.stampAt(e.carving().ops[0].stamp.at) == 0);
    CHECK(e.stampAt({0.8f, -0.8f}) == -1);

    // A freehand loop is a hole; an open stroke is a thin slit; a tap is nothing.
    CHECK(e.cut(circle(0.3f, -0.4f, 0.15f)));
    m = buildMask(e.carving());
    CHECK(m.sample(0.3f, -0.4f) > 0.9f);
    CHECK(e.cut({{-0.6f, -0.7f}, {-0.3f, -0.72f}, {0.0f, -0.7f}}));
    m = buildMask(e.carving());
    CHECK(m.sample(-0.3f, -0.71f) > 0.5f);
    CHECK(m.sample(-0.3f, -0.6f) < 0.1f);
    const size_t ops = e.carving().ops.size();
    CHECK(!e.cut({{0.1f, 0.1f}, {0.11f, 0.1f}}));
    CHECK(e.carving().ops.size() == ops);

    // Start over clears it, and is undone like anything else.
    e.startOver();
    CHECK(e.carving().ops.empty());
    e.undo();
    CHECK(e.carving().ops.size() == ops);

    // The pumpkin: its front is where carve space says, and the pictures
    // show a hole there (dark skin, glowing light) and none elsewhere.
    Vec3 p;
    CHECK(frontPoint(OrangeRound, 0, 0, p));
    CHECK(p.z > 0.9f);
    CHECK(frontPoint(OrangeRound, 0.5f, 0.3f, p));
    CHECK(std::fabs(p.x - 0.5f) < 0.01f && std::fabs(p.y - 0.3f) < 0.01f && p.z > 0.3f);
    CHECK(!frontPoint(OrangeRound, 1.3f, 0, p));
    CHECK(bottomOf(OrangeRound) < -0.6f);
    CHECK(topOf(OrangeRound) > 0.5f);
    {
        Editor f;
        f.addStamp(OhMouth);  // at (0, -0.38)
        const Textures t = paint(OrangeRound, buildMask(f.carving()), 256, 128);
        // The texel on the face at (0, -0.38): u = 0.5, v from the latitude.
        Vec3 q;
        CHECK(frontPoint(OrangeRound, 0, -0.38f, q));
        int best = -1;
        float bestErr = 1e9f;
        for (int j = 0; j < 128; ++j) {
            const Vec3 r = surfacePoint(OrangeRound, 0.5f, (static_cast<float>(j) + 0.5f) / 128);
            const float err = std::fabs(r.y - q.y);
            if (err < bestErr) { bestErr = err; best = j; }
        }
        const size_t hole = (static_cast<size_t>(best) * 256 + 128) * 4;
        const size_t skin = (static_cast<size_t>(64) * 256 + 0) * 4;  // the back
        CHECK(t.glow[hole + 3] > 200);
        CHECK(t.glow[skin + 3] == 0);
        CHECK(t.skin[hole] < t.skin[skin]);
    }
    // On a squat pumpkin the mouth goes higher, and is still on its face.
    {
        Editor f;
        f.setVariety(GreenSquat);
        f.addStamp(GrinMouth);
        const Stamp grin = *f.selectedStamp();
        CHECK(grin.at.y > -0.38f);
        Vec3 q;
        CHECK(frontPoint(GreenSquat, 0, grin.at.y - 0.6f * grin.scale, q));
    }
    // Meshes are well formed.
    for (int v = 0; v < VarietyCount; ++v) {
        for (const MeshData& md : {buildBody(static_cast<Variety>(v), 96, 48), buildStem(static_cast<Variety>(v))}) {
            CHECK(md.indices.size() % 3 == 0);
            const size_t n = md.positions.size() / 3;
            for (uint32_t i : md.indices) CHECK(i < n);
            CHECK(md.uvs.size() == n * 2);
            bool finite = true;
            for (float f : md.positions) finite = finite && std::isfinite(f);
            CHECK(finite);  // the poles too
        }
    }
    TEST_MAIN_END();
}
