#include "carve/editor.h"

#include <algorithm>
#include <cmath>

#include "carve/shapes.h"

namespace pumpkin {

namespace {
constexpr size_t kHistory = 200;
constexpr float kTurn = 3.14159265359f / 12;  // 15 degrees a press
}  // namespace

const Stamp* Editor::selectedStamp() const {
    if (selected_ < 0 || selected_ >= static_cast<int>(carving_.ops.size())) return nullptr;
    const Op& op = carving_.ops[static_cast<size_t>(selected_)];
    return op.isStamp ? &op.stamp : nullptr;
}

void Editor::remember() {
    history_.push_back(carving_);
    historySelected_.push_back(selected_);
    if (history_.size() > kHistory) {
        history_.erase(history_.begin());
        historySelected_.erase(historySelected_.begin());
    }
}

void Editor::addStamp(Shape shape) {
    if (carving_.ops.size() >= kMaxOps) return;
    Stamp s;
    s.shape = shape;
    s.scale = defaultScale(shape);
    switch (partOf(shape)) {
    case Part::Eye: {
        // The first eye goes on the left; the next one mirrors the last eye.
        const Stamp* other = nullptr;
        for (const Op& op : carving_.ops)
            if (op.isStamp && partOf(op.stamp.shape) == Part::Eye) other = &op.stamp;
        if (other && other->at.x < 0) {
            s.at = {-other->at.x, other->at.y};
            s.scale = other->scale;
            s.angle = -other->angle;
            s.flip = !other->flip;
        } else {
            s.at = {-0.36f, 0.22f};
        }
        break;
    }
    case Part::Nose: s.at = {0, -0.02f}; break;
    case Part::Mouth: s.at = {0, -0.38f}; break;
    default: s.at = {0, 0.5f}; break;
    }
    // Not exactly on top of a piece already there.
    for (int tries = 0; tries < 6; ++tries) {
        bool clash = false;
        for (const Op& op : carving_.ops)
            if (op.isStamp && std::fabs(op.stamp.at.x - s.at.x) < 0.02f && std::fabs(op.stamp.at.y - s.at.y) < 0.02f) clash = true;
        if (!clash) break;
        s.at.y -= 0.12f;
    }
    quantiseStamp(s);
    remember();
    Op op;
    op.isStamp = true;
    op.stamp = s;
    carving_.ops.push_back(op);
    selected_ = static_cast<int>(carving_.ops.size()) - 1;
    changed();
}

void Editor::edit(Edit e) {
    if (e == Edit::Done) {
        selected_ = -1;
        changed();
        return;
    }
    const Stamp* cur = selectedStamp();
    if (!cur) return;
    Stamp s = *cur;
    remember();
    switch (e) {
    case Edit::Bigger: s.scale *= 1.15f; break;
    case Edit::Smaller: s.scale /= 1.15f; break;
    case Edit::TurnLeft: s.angle += kTurn; break;
    case Edit::TurnRight: s.angle -= kTurn; break;
    case Edit::Flip: s.flip = !s.flip; break;
    case Edit::Twin: {
        if (carving_.ops.size() >= kMaxOps) { history_.pop_back(); historySelected_.pop_back(); return; }
        Stamp t = s;
        t.at.x = std::fabs(s.at.x) < 0.05f ? s.at.x + 0.3f : -s.at.x;
        t.flip = !s.flip;
        t.angle = -s.angle;
        quantiseStamp(t);
        Op op;
        op.stamp = t;
        carving_.ops.push_back(op);
        selected_ = static_cast<int>(carving_.ops.size()) - 1;
        changed();
        return;
    }
    case Edit::Delete:
        carving_.ops.erase(carving_.ops.begin() + selected_);
        selected_ = -1;
        changed();
        return;
    default: break;
    }
    quantiseStamp(s);
    carving_.ops[static_cast<size_t>(selected_)].stamp = s;
    changed();
}

void Editor::select(int op) {
    if (op >= 0 && op < static_cast<int>(carving_.ops.size()) && carving_.ops[static_cast<size_t>(op)].isStamp) selected_ = op;
    else selected_ = -1;
    changed();
}

int Editor::stampAt(Vec2 p) const {
    for (int i = static_cast<int>(carving_.ops.size()) - 1; i >= 0; --i) {
        const Op& op = carving_.ops[static_cast<size_t>(i)];
        if (!op.isStamp) continue;
        // A little bigger than the piece, so small ones are easy to grab.
        Stamp s = op.stamp;
        const float dx = p.x - s.at.x, dy = p.y - s.at.y;
        const float grab = std::max(0.09f, s.scale * 0.55f);
        if (dx * dx + dy * dy < grab * grab || insideStamp(s, p)) return i;
    }
    return -1;
}

void Editor::beginGesture() {
    if (!selectedStamp() || inGesture_) return;
    inGesture_ = true;
    before_ = carving_;
}

void Editor::moveSelected(float dx, float dy) {
    if (!inGesture_ || !selectedStamp()) return;
    Stamp& s = carving_.ops[static_cast<size_t>(selected_)].stamp;
    s.at.x += dx;
    s.at.y += dy;
    clampStamp(s);
    changed();
}

void Editor::setSelectedScaleAngle(float scale, float angle) {
    if (!inGesture_ || !selectedStamp()) return;
    Stamp& s = carving_.ops[static_cast<size_t>(selected_)].stamp;
    s.scale = scale;
    s.angle = angle;
    clampStamp(s);
    changed();
}

void Editor::endGesture() {
    if (!inGesture_) return;
    inGesture_ = false;
    if (selectedStamp()) quantiseStamp(carving_.ops[static_cast<size_t>(selected_)].stamp);
    if (carving_ != before_) {
        history_.push_back(before_);
        historySelected_.push_back(selected_);
        if (history_.size() > kHistory) {
            history_.erase(history_.begin());
            historySelected_.erase(historySelected_.begin());
        }
    }
    changed();
}

bool Editor::cut(const std::vector<Vec2>& stroke) {
    if (carving_.ops.size() >= kMaxOps) return false;
    Op op;
    op.isStamp = false;
    if (!strokeToCut(stroke, op.cut)) return false;
    remember();
    carving_.ops.push_back(std::move(op));
    selected_ = -1;
    changed();
    return true;
}

void Editor::setVariety(Variety v) {
    if (v >= VarietyCount || v == carving_.variety) return;
    remember();
    carving_.variety = v;
    changed();
}

void Editor::undo() {
    if (history_.empty()) return;
    inGesture_ = false;
    carving_ = history_.back();
    selected_ = historySelected_.back();
    history_.pop_back();
    historySelected_.pop_back();
    if (!selectedStamp()) selected_ = -1;
    changed();
}

void Editor::startOver() {
    if (carving_.ops.empty()) return;
    remember();
    carving_.ops.clear();
    selected_ = -1;
    changed();
}

void Editor::load(const Carving& c) {
    carving_ = c;
    history_.clear();
    historySelected_.clear();
    selected_ = -1;
    inGesture_ = false;
    changed();
}

}  // namespace pumpkin
