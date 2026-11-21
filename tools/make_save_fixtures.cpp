// Writes the current code format's save fixture: a porch of pumpkins exactly
// as the page keeps it in localStorage ('pumpkin-porch-v1', a JSON list of
// carving codes), as tests/fixtures/v<kCodeVersion>-porch.save.
//
//   cmake --build build --target make_save_fixtures
//   ./build/make_save_fixtures tests/fixtures
//
// It never overwrites a file: an older format's fixture stands for porches and
// links people already have (tests/fixtures/README.md).
#include <cmath>
#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

#include "carve/carving.h"
#include "carve/editor.h"

using namespace pumpkin;

namespace {

std::vector<Vec2> loop(float cx, float cy, float r, int n) {
    std::vector<Vec2> out;
    for (int i = 0; i <= n; ++i) {
        const float a = 6.2831853f * static_cast<float>(i) / static_cast<float>(n);
        out.push_back({cx + r * std::cos(a), cy + r * std::sin(a)});
    }
    return out;
}

std::vector<Carving> porch() {
    std::vector<Carving> out;
    // 1. A pumpkin put on the porch uncarved.
    out.push_back(Carving{});
    // 2. The classic face: two eyes (the second mirrored), a nose, a grin.
    {
        Editor e;
        e.setVariety(WhiteRound);
        e.addStamp(TriangleEye);
        e.addStamp(TriangleEye);
        e.addStamp(TriangleNose);
        e.addStamp(GrinMouth);
        e.edit(Edit::TurnLeft);
        e.edit(Edit::Bigger);
        out.push_back(e.carving());
    }
    // 3. Freehand only: a round hole and an open slit.
    {
        Editor e;
        e.setVariety(GreenSquat);
        e.cut(loop(0.5f, 0.55f, 0.1f, 40));
        e.cut({{-0.6f, -0.6f}, {-0.4f, -0.65f}, {-0.2f, -0.6f}});
        out.push_back(e.carving());
    }
    // 4. Every face piece once, each changed a different way.
    {
        Editor e;
        e.setVariety(OrangeTall);
        const Edit edits[] = {Edit::Bigger, Edit::Smaller, Edit::TurnLeft, Edit::TurnRight, Edit::Flip};
        for (int s = 0; s < ShapeCount; ++s) {
            e.addStamp(static_cast<Shape>(s));
            e.edit(edits[s % 5]);
            e.edit(Edit::Done);
        }
        out.push_back(e.carving());
    }
    // 5. A long wavy cut with many points, mixed with stamps, on a squat orange.
    {
        Editor e;
        e.setVariety(OrangeSquat);
        std::vector<Vec2> wave;
        for (int i = 0; i < 200; ++i) {
            const float t = static_cast<float>(i) / 199.0f;
            wave.push_back({-0.9f + 1.8f * t, -0.3f + 0.25f * std::sin(t * 18.0f) + 0.05f * std::sin(t * 97.0f)});
        }
        e.cut(wave);
        e.addStamp(StarEye);
        e.addStamp(Moon);
        e.addStamp(Bat);
        e.cut(loop(-0.3f, 0.4f, 0.15f, 60));
        out.push_back(e.carving());
    }
    return out;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc != 2) {
        std::fprintf(stderr, "usage: make_save_fixtures <fixtures dir>\n");
        return 2;
    }
    const std::string path = std::string(argv[1]) + "/v" + std::to_string(kCodeVersion) + "-porch.save";
    if (std::ifstream(path).good()) {
        std::fprintf(stderr, "%s exists: a released fixture is never rewritten\n", path.c_str());
        return 1;
    }
    std::string json = "[";
    for (const Carving& c : porch()) {
        if (json.size() > 1) json += ",";
        json += "\"" + encode(c) + "\"";
    }
    json += "]";
    std::ofstream f(path, std::ios::binary);
    f << json;
    std::printf("wrote %s (%zu bytes)\n", path.c_str(), json.size());
    return f ? 0 : 1;
}
