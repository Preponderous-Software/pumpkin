// Where the pumpkin is cut through, as a grid over carve space: 1 for a hole,
// 0 for skin. The pumpkin's textures are made from it (src/carve/pumpkin.h).
#pragma once

#include <cstdint>
#include <vector>

#include "carve/carving.h"

namespace pumpkin {

struct Mask {
    int size = 0;                // cells along each side
    float lo = -1.25f, hi = 1.25f;  // the carve-space square it covers
    std::vector<uint8_t> cells;  // size * size, row 0 at the top (largest y), 255 = cut
    std::vector<uint8_t> glow;   // the cut blurred: how deep inside a hole a cell is

    float cellSize() const { return (hi - lo) / static_cast<float>(size); }
    bool cut(int x, int y) const { return cells[static_cast<size_t>(y) * size + x] != 0; }
    // 0..1, bilinear: soft edges once on the pumpkin.
    float sample(float x, float y) const;
    float sampleGlow(float x, float y) const;
    int cutCount() const;
};

constexpr float kSlitWidth = 0.022f;  // half the width of an open freehand cut

Mask buildMask(const Carving& c, int size = 384);

}  // namespace pumpkin
