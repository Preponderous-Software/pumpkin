// What a carving is: a pumpkin (its colour and shape) and the cuts made in it,
// in order. No engine and no GPU: everything here builds and tests in seconds.
//
// Carve space is the pumpkin's face seen from the front: x to the right, y up,
// in the pumpkin's own units (a round pumpkin is 1 wide each side of its
// centre). A cut at (x, y) goes through the skin at the front of the pumpkin
// straight behind that point.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace pumpkin {

struct Vec2 {
    float x = 0, y = 0;
};

// The face pieces a stamp can be (src/carve/shapes.cpp draws them).
enum Shape : uint8_t {
    TriangleEye = 0,
    RoundEye,
    HappyEye,
    AngryEye,
    StarEye,
    TriangleNose,
    HeartNose,
    GrinMouth,
    SmileMouth,
    ZigzagMouth,
    OhMouth,
    FangMouth,
    Bat,
    Moon,
    ShapeCount
};

enum class Part : uint8_t { Eye, Nose, Mouth, Extra };
Part partOf(Shape s);

// A face piece placed on the pumpkin: where, how big, turned how far, and
// whether it is mirrored (a left eye made into a right one).
struct Stamp {
    Shape shape = TriangleEye;
    Vec2 at;
    float scale = 0.2f;  // carve units from the piece's centre to its edge
    float angle = 0;     // radians, anticlockwise
    bool flip = false;
};

// A freehand cut: a closed stroke pops out as a hole; an open one is a slit.
struct Cut {
    std::vector<Vec2> points;
    bool closed = false;
};

struct Op {
    bool isStamp = true;
    Stamp stamp;
    Cut cut;
};

// The pumpkins to choose from (colour and shape).
enum Variety : uint8_t { OrangeRound = 0, OrangeTall, WhiteRound, GreenSquat, OrangeSquat, VarietyCount };

struct Carving {
    Variety variety = OrangeRound;
    std::vector<Op> ops;
    bool operator==(const Carving& o) const;
    bool operator!=(const Carving& o) const { return !(*this == o); }
};

// Limits a carving keeps to (so that a link stays short).
constexpr size_t kMaxOps = 64;
constexpr size_t kMaxCutPoints = 96;
constexpr float kCarveMin = -1.3f, kCarveMax = 1.3f;
constexpr float kScaleMin = 0.05f, kScaleMax = 0.9f;

// Clamps a stamp to what a carving can hold (and what a link can say).
void clampStamp(Stamp& s);
// Clamps a stamp and snaps it to what a link can say, so a shared carving
// looks exactly like the one on screen.
void quantiseStamp(Stamp& s);

// Fewer points along a stroke, keeping its shape (Ramer-Douglas-Peucker),
// then at most kMaxCutPoints of them.
std::vector<Vec2> simplify(const std::vector<Vec2>& points, float tolerance);

// The stroke a finger drew, made into a cut: closed when its end comes back
// near its start, an open slit otherwise. False when it is too small to cut.
bool strokeToCut(const std::vector<Vec2>& stroke, Cut& out);

// ---------------------------------------------------------------- links
// A carving as a short URL-safe code (base64url, no padding), and back.
// decode() refuses anything malformed and leaves `out` untouched then.
std::string encode(const Carving& c);
bool decode(const std::string& code, Carving& out);

}  // namespace pumpkin
