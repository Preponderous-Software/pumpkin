// Carving a pumpkin, step by step: placing and changing face pieces, cutting
// freehand, undo and start over. The page and the view drive it; it knows
// nothing of screens or fingers, only carve space.
#pragma once

#include <vector>

#include "carve/carving.h"

namespace pumpkin {

enum class Edit { Bigger, Smaller, TurnLeft, TurnRight, Flip, Twin, Delete, Done };

class Editor {
public:
    const Carving& carving() const { return carving_; }
    // Which op is the face piece being changed, or -1.
    int selected() const { return selected_; }
    const Stamp* selectedStamp() const;
    bool canUndo() const { return !history_.empty(); }
    // Bumped on every change to the carving (the view repaints on it).
    unsigned revision() const { return revision_; }

    // A new face piece, chosen: placed where that kind of piece goes, the
    // second eye mirrored opposite the first.
    void addStamp(Shape s);
    void edit(Edit e);
    void select(int op);
    // The topmost face piece at carve point p, or -1.
    int stampAt(Vec2 p) const;

    // Dragging or pinching the chosen piece: begin, any number of changes, end.
    void beginGesture();
    void moveSelected(float dx, float dy);
    void setSelectedScaleAngle(float scale, float angle);
    void endGesture();

    // A finished freehand stroke; false when it was too small to cut.
    bool cut(const std::vector<Vec2>& stroke);

    void setVariety(Variety v);
    void undo();
    void startOver();
    // Replaces the carving (from a link or the porch), with nothing to undo.
    void load(const Carving& c);

private:
    void remember();
    void changed() { ++revision_; }

    Carving carving_;
    std::vector<Carving> history_;
    std::vector<int> historySelected_;
    int selected_ = -1;
    unsigned revision_ = 1;
    bool inGesture_ = false;
    Carving before_;
};

}  // namespace pumpkin
