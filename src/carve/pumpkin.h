// The pumpkin itself: its ribbed shape, and the two pictures painted on it
// from a carving's mask: the skin (orange, with the holes dark and their cut
// edges pale) and the glow (the candlelight showing through the holes, with
// alpha where light comes out). No engine: the view (src/view) uploads these.
//
// The pumpkin is centred on the origin, its face towards +z, y up. Texture
// coordinates go round it: u = 0.5 is the middle of the face, v = 0 the top.
#pragma once

#include <cstdint>
#include <vector>

#include "carve/carving.h"
#include "carve/mask.h"

namespace pumpkin {

struct Vec3 {
    float x = 0, y = 0, z = 0;
};

struct Look {
    float radius;  // half its width
    float height;  // half its height
    float ribs;    // how deep the creases between ribs are
    float skin[3];
    float crease[3];
    float stem[3];
};
const Look& lookOf(Variety v);

Vec3 surfacePoint(Variety v, float u, float vv);
// The lowest point of the pumpkin (it stands on the porch there) and the
// highest, where the stem grows.
float bottomOf(Variety v);
float topOf(Variety v);

// The front of the pumpkin behind carve point (x, y): false if the point is
// off the pumpkin (seen from the front).
bool frontPoint(Variety v, float x, float y, Vec3& out);

struct MeshData {
    std::vector<float> positions;  // xyz
    std::vector<float> uvs;        // uv
    std::vector<float> colors;     // rgb, or empty
    std::vector<uint32_t> indices;
};

// The body as a latitude-longitude grid, `grow` times its true size (the glow
// layer sits just outside the skin).
MeshData buildBody(Variety v, int around = 96, int down = 48, float grow = 1.0f);
MeshData buildStem(Variety v);

struct Textures {
    int width = 0, height = 0;
    std::vector<uint8_t> skin;  // RGBA
    std::vector<uint8_t> glow;  // RGBA, alpha = light coming out
};
Textures paint(Variety v, const Mask& m, int width = 1024, int height = 512);

}  // namespace pumpkin
