// The pumpkin (or the porch of them) on Strata: meshes and textures from
// src/carve, the candle, the lights on or off, picking with the engine's ray
// cast, and the overlays for a stroke being drawn and the chosen face piece.
#pragma once

#include <array>
#include <cstdint>
#include <vector>

#include "carve/carving.h"
#include "carve/pumpkin.h"
#include "strata/strata.hpp"

namespace pumpkin {

class View {
public:
    explicit View(strata::Engine& engine);

    enum class Mode { Carve, Porch };
    void setMode(Mode m) { mode_ = m; placeAll(time_); }
    Mode mode() const { return mode_; }
    void setDark(bool dark) { dark_ = dark; }
    bool dark() const { return dark_; }

    // The drawable size, and how many pixels the page's bars cover at the top
    // and bottom: the pumpkin is framed in what is left.
    void setLayout(int width, int height, float coverTop, float coverBottom);

    // The carving being made; repainted when `revision` changes.
    void showCarving(const Carving& c, unsigned revision);
    void setPorch(const std::vector<Carving>& pumpkins);
    int porchCount() const { return static_cast<int>(porch_.size()); }
    void setPorchChosen(int i) { porchChosen_ = i; }

    // Overlays for this frame: the stroke being cut (carve points) and the
    // outline of the chosen face piece (empty for none).
    void setStroke(const std::vector<Vec2>& stroke) { stroke_ = stroke; }
    void setOutline(const std::vector<std::vector<Vec2>>& outline) { outline_ = outline; }

    void frame(double seconds);
    // Draws one frame with the lights out and no overlays, and reads it back.
    void snapshot(std::vector<uint8_t>& rgba, int& width, int& height);

    // The carve point under a pixel (on the carving's pumpkin), if any.
    bool carvePointAt(float px, float py, Vec2& out);
    // The porch pumpkin under a pixel, or -1.
    int porchPumpkinAt(float px, float py);
    // Where carve point c (on the carving's pumpkin) is on screen, in pixels;
    // false when it is off the pumpkin. For tests and the page's hints.
    bool screenOf(Vec2 c, float& px, float& py) const;
    // Carve units per pixel at the face of the pumpkin (for dragging).
    float unitsPerPixel() const;

private:
    struct Gfx {
        bool live = false;
        Variety variety = OrangeRound;
        uint32_t skinTex = 0, glowTex = 0;
        uint32_t skin = 0, stem = 0;
        std::array<uint32_t, 5> glow{};  // four candle levels, then the daylight glow
        float pos[3] = {0, 0, 0};
        float scale = 1;
        int texW = 0, texH = 0;
    };
    void build(Gfx& g, const Carving& c, int texW, int texH);
    void destroy(Gfx& g);
    void place(Gfx& g, bool shown, int level, bool dayGlow);
    // Puts every pumpkin where this mode shows it (or out of sight), so a
    // ray cast is right before the next frame is drawn.
    void placeAll(double t);
    void camera(float& tanHalf, float eye[3], float target[3]) const;
    void drawScene(double t, bool overlays);
    void ray(float px, float py, float origin[3], float dir[3]) const;

    strata::Engine& engine_;
    Mode mode_ = Mode::Carve;
    bool dark_ = false;
    int width_ = 1280, height_ = 720;
    float coverTop_ = 0, coverBottom_ = 0;
    Gfx carve_;
    unsigned carveRevision_ = 0;
    std::vector<Gfx> porch_;
    int porchChosen_ = -1;
    std::vector<Vec2> stroke_;
    std::vector<std::vector<Vec2>> outline_;
    uint32_t window_ = 0;
    double time_ = 0;
    std::vector<float> stars_;
};

}  // namespace pumpkin
