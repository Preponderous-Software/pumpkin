// Pumpkin in a browser: Strata's WebGL 2 backend (Emscripten), driven by the
// page (web/shell.html). The page owns the buttons and the fingers; it hands
// pointer events here in drawable pixels, and everything else is a call.
//
// Nothing is stored here: the page keeps the porch in localStorage and the
// carving in the link (#p=<code>).
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include <emscripten/emscripten.h>
#include <emscripten/html5.h>

#include "carve/editor.h"
#include "carve/shapes.h"
#include "strata/strata.hpp"
#include "view/view.h"

using namespace pumpkin;

namespace {

enum class Gesture { None, Cut, Move, Pinch, PorchTap };

struct Pointer {
    float x = 0, y = 0, startX = 0, startY = 0;
};

struct App {
    std::unique_ptr<strata::Engine> engine;
    std::unique_ptr<View> view;
    Editor editor;
    double last = 0;
    std::map<int, Pointer> pointers;
    Gesture gesture = Gesture::None;
    std::vector<Vec2> stroke;
    float pinchDist = 1, pinchAngle = 0, pinchScale = 0.2f, pinchStampAngle = 0;
    int cuts = 0;  // finished freehand cuts, for the page's first-time hint
    std::string text;
    std::vector<uint8_t> shot;
    int shotW = 0, shotH = 0;
};

App* app = nullptr;

void updateOutline() {
    std::vector<std::vector<Vec2>> outline;
    if (const Stamp* s = app->editor.selectedStamp()) {
        for (const Polygon& p : shapeOutline(s->shape)) {
            std::vector<Vec2> o;
            for (const Vec2& q : p) o.push_back(fromShape(*s, q));
            outline.push_back(std::move(o));
        }
    }
    app->view->setOutline(outline);
}

void frame(void*) {
    StrataEvent e;
    while (app->engine->pollEvent(e)) {
    }
    const double now = emscripten_get_now() / 1000.0;
    try {
        app->view->showCarving(app->editor.carving(), app->editor.revision());
        updateOutline();
        app->view->setStroke(app->stroke);
        app->view->frame(std::min(0.25, now - app->last));
    } catch (const std::exception& ex) {
        // Say it once; the page keeps running.
        static int said = 0;
        if (said++ < 3) std::fprintf(stderr, "pumpkin: a frame failed: %s\n", ex.what());
    }
    app->last = now;
    app->engine->present();
}

void twoFingers(float& dist, float& angle) {
    auto it = app->pointers.begin();
    const Pointer a = it->second;
    const Pointer b = (++it)->second;
    dist = std::max(1.0f, std::hypot(b.x - a.x, b.y - a.y));
    angle = std::atan2(-(b.y - a.y), b.x - a.x);
}

void startPinch() {
    const Stamp* s = app->editor.selectedStamp();
    if (!s || app->pointers.size() < 2) return;
    if (app->gesture == Gesture::Cut) app->stroke.clear();
    if (app->gesture != Gesture::Move) app->editor.beginGesture();
    app->gesture = Gesture::Pinch;
    twoFingers(app->pinchDist, app->pinchAngle);
    app->pinchScale = s->scale;
    app->pinchStampAngle = s->angle;
}

void pointerDown(int id, float x, float y) {
    Pointer p;
    p.x = p.startX = x;
    p.y = p.startY = y;
    app->pointers[id] = p;
    if (app->view->mode() == View::Mode::Porch) {
        app->gesture = app->pointers.size() == 1 ? Gesture::PorchTap : Gesture::None;
        return;
    }
    if (app->pointers.size() == 2) {
        if (app->editor.selectedStamp()) startPinch();
        else if (app->gesture == Gesture::Cut) {
            app->stroke.clear();
            app->gesture = Gesture::None;
        }
        return;
    }
    if (app->pointers.size() > 2) return;
    Vec2 c;
    const bool hit = app->view->carvePointAt(x, y, c);
    const int under = hit ? app->editor.stampAt(c) : -1;
    if (under >= 0) {
        // On a face piece: choose it, and drag it.
        if (under != app->editor.selected()) app->editor.select(under);
        app->editor.beginGesture();
        app->gesture = Gesture::Move;
    } else if (app->editor.selectedStamp()) {
        app->editor.edit(Edit::Done);  // a tap away puts the piece down
        app->gesture = Gesture::None;
    } else if (hit) {
        app->gesture = Gesture::Cut;
        app->stroke.assign(1, c);
    } else {
        app->gesture = Gesture::None;
    }
}

void pointerMove(int id, float x, float y) {
    auto it = app->pointers.find(id);
    if (it == app->pointers.end()) return;
    const float dx = x - it->second.x, dy = y - it->second.y;
    it->second.x = x;
    it->second.y = y;
    switch (app->gesture) {
    case Gesture::Cut: {
        Vec2 c;
        if (app->view->carvePointAt(x, y, c)) {
            const Vec2& b = app->stroke.back();
            if (std::hypot(c.x - b.x, c.y - b.y) > 0.008f && app->stroke.size() < 2000) app->stroke.push_back(c);
        }
        break;
    }
    case Gesture::Move: {
        const float k = app->view->unitsPerPixel();
        app->editor.moveSelected(dx * k, -dy * k);
        break;
    }
    case Gesture::Pinch: {
        if (app->pointers.size() < 2) break;
        float d, a;
        twoFingers(d, a);
        app->editor.setSelectedScaleAngle(app->pinchScale * d / app->pinchDist, app->pinchStampAngle + (a - app->pinchAngle));
        break;
    }
    default: break;
    }
}

void pointerUp(int id, bool cancel) {
    auto it = app->pointers.find(id);
    if (it == app->pointers.end()) return;
    const Pointer p = it->second;
    app->pointers.erase(it);
    switch (app->gesture) {
    case Gesture::Cut:
        if (!cancel && app->editor.cut(app->stroke)) ++app->cuts;
        app->stroke.clear();
        app->gesture = Gesture::None;
        break;
    case Gesture::Move:
    case Gesture::Pinch:
        if (app->pointers.empty()) {
            app->editor.endGesture();
            app->gesture = Gesture::None;
        }
        break;
    case Gesture::PorchTap:
        app->gesture = Gesture::None;
        if (!cancel && std::hypot(p.x - p.startX, p.y - p.startY) < 24) {
            const int i = app->view->porchPumpkinAt(p.x, p.y);
            app->view->setPorchChosen(i);
            char js[96];
            std::snprintf(js, sizeof js, "if (Module.onPorchTap) Module.onPorchTap(%d)", i);
            emscripten_run_script(js);
        }
        break;
    default:
        if (app->pointers.empty()) app->gesture = Gesture::None;
        break;
    }
}

}  // namespace

extern "C" {

// kind: 0 down, 1 move, 2 up, 3 cancel. x, y in drawable pixels.
EMSCRIPTEN_KEEPALIVE void pumpkin_pointer(int kind, int id, double x, double y) {
    if (!app) return;
    const float fx = static_cast<float>(x), fy = static_cast<float>(y);
    if (kind == 0) pointerDown(id, fx, fy);
    else if (kind == 1) pointerMove(id, fx, fy);
    else pointerUp(id, kind == 3);
}
EMSCRIPTEN_KEEPALIVE void pumpkin_stamp(int shape) {
    if (app && shape >= 0 && shape < ShapeCount) app->editor.addStamp(static_cast<Shape>(shape));
}
EMSCRIPTEN_KEEPALIVE void pumpkin_edit(int e) {
    if (app && e >= 0 && e <= static_cast<int>(Edit::Done)) app->editor.edit(static_cast<Edit>(e));
}
EMSCRIPTEN_KEEPALIVE void pumpkin_undo() {
    if (app) app->editor.undo();
}
EMSCRIPTEN_KEEPALIVE void pumpkin_start_over() {
    if (app) app->editor.startOver();
}
EMSCRIPTEN_KEEPALIVE void pumpkin_variety(int v) {
    if (app && v >= 0 && v < VarietyCount) app->editor.setVariety(static_cast<Variety>(v));
}
EMSCRIPTEN_KEEPALIVE void pumpkin_dark(int on) {
    if (app) app->view->setDark(on != 0);
}
EMSCRIPTEN_KEEPALIVE void pumpkin_layout(double w, double h, double top, double bottom) {
    if (!app || w < 1 || h < 1) return;
    app->engine->resize(static_cast<int32_t>(w), static_cast<int32_t>(h));
    app->view->setLayout(static_cast<int>(w), static_cast<int>(h), static_cast<float>(top), static_cast<float>(bottom));
}
EMSCRIPTEN_KEEPALIVE void pumpkin_mode(int porch) {
    if (!app) return;
    app->pointers.clear();
    app->stroke.clear();
    app->gesture = Gesture::None;
    app->view->setMode(porch ? View::Mode::Porch : View::Mode::Carve);
}
// Loads a carving from a link's code; 1 if it was a carving.
EMSCRIPTEN_KEEPALIVE int pumpkin_load(const char* code) {
    if (!app || !code) return 0;
    Carving c;
    if (!decode(code, c)) return 0;
    app->editor.load(c);
    return 1;
}
EMSCRIPTEN_KEEPALIVE const char* pumpkin_code() {
    if (!app) return "";
    app->text = encode(app->editor.carving());
    return app->text.c_str();
}
// The porch: codes separated by commas; returns how many are standing there.
EMSCRIPTEN_KEEPALIVE int pumpkin_porch(const char* codes) {
    if (!app || !codes) return 0;
    std::vector<Carving> list;
    std::stringstream in(codes);
    std::string one;
    while (std::getline(in, one, ',')) {
        Carving c;
        if (decode(one, c)) list.push_back(c);
    }
    app->view->setPorch(list);
    return app->view->porchCount();
}
EMSCRIPTEN_KEEPALIVE void pumpkin_porch_choose(int i) {
    if (app) app->view->setPorchChosen(i);
}
// A picture of the pumpkin, lit: RGBA, top row first. Returns the pixels'
// address in the heap (read with pumpkin_snapshot_width/height).
EMSCRIPTEN_KEEPALIVE int pumpkin_snapshot() {
    if (!app) return 0;
    app->view->showCarving(app->editor.carving(), app->editor.revision());
    app->view->setOutline({});
    app->view->setStroke({});
    app->view->snapshot(app->shot, app->shotW, app->shotH);
    return app->shot.empty() ? 0 : static_cast<int>(reinterpret_cast<uintptr_t>(app->shot.data()));
}
EMSCRIPTEN_KEEPALIVE int pumpkin_snapshot_width() { return app ? app->shotW : 0; }
EMSCRIPTEN_KEEPALIVE int pumpkin_snapshot_height() { return app ? app->shotH : 0; }
// Where carve point (x, y) is on screen, as "px,py" in drawable pixels, or "".
EMSCRIPTEN_KEEPALIVE const char* pumpkin_screen_of(double x, double y) {
    float px = 0, py = 0;
    if (!app || !app->view->screenOf({static_cast<float>(x), static_cast<float>(y)}, px, py)) return "";
    char buf[64];
    std::snprintf(buf, sizeof buf, "%.1f,%.1f", static_cast<double>(px), static_cast<double>(py));
    app->text = buf;
    return app->text.c_str();
}
EMSCRIPTEN_KEEPALIVE int pumpkin_shape_count() { return ShapeCount; }
EMSCRIPTEN_KEEPALIVE int pumpkin_shape_part(int s) {
    return s >= 0 && s < ShapeCount ? static_cast<int>(partOf(static_cast<Shape>(s))) : -1;
}
EMSCRIPTEN_KEEPALIVE const char* pumpkin_shape_svg(int s) {
    if (!app || s < 0 || s >= ShapeCount) return "";
    app->text = shapeSvgPath(static_cast<Shape>(s));
    return app->text.c_str();
}
EMSCRIPTEN_KEEPALIVE const char* pumpkin_status() {
    if (!app) return "{}";
    const Carving& c = app->editor.carving();
    int stamps = 0;
    for (const Op& op : c.ops) stamps += op.isStamp ? 1 : 0;
    char buf[320];
    std::snprintf(buf, sizeof buf,
                  "{\"mode\":\"%s\",\"dark\":%s,\"ops\":%d,\"stamps\":%d,\"cuts\":%d,\"made\":%d,\"selected\":%d,"
                  "\"canUndo\":%s,\"variety\":%d,\"revision\":%u,\"cutting\":%s,\"porch\":%d,\"gesture\":%d}",
                  app->view->mode() == View::Mode::Porch ? "porch" : "carve", app->view->dark() ? "true" : "false",
                  static_cast<int>(c.ops.size()), stamps, static_cast<int>(c.ops.size()) - stamps, app->cuts,
                  app->editor.selected(), app->editor.canUndo() ? "true" : "false", static_cast<int>(c.variety),
                  app->editor.revision(), app->gesture == Gesture::Cut ? "true" : "false", app->view->porchCount(),
                  static_cast<int>(app->gesture));
    app->text = buf;
    return app->text.c_str();
}

}  // extern "C"

int main() {
    static App a;
    app = &a;
    double cw = 0, ch = 0;
    emscripten_get_element_css_size("#canvas", &cw, &ch);
    if (cw < 1 || ch < 1) {
        cw = 1280;
        ch = 720;
    }
    strata::EngineDesc desc;
    desc.surface = STRATA_SURFACE_WINDOW;
    desc.width = static_cast<int32_t>(cw);
    desc.height = static_cast<int32_t>(ch);
    desc.title = "Pumpkin";
    try {
        app->engine = strata::Engine::create(desc);
    } catch (const std::exception& ex) {
        std::fprintf(stderr, "pumpkin: cannot start the renderer: %s\n", ex.what());
        emscripten_run_script("if (Module.onRendererFailed) Module.onRendererFailed('no WebGL 2')");
        return 1;
    }
    app->view = std::make_unique<View>(*app->engine);
    app->view->setLayout(static_cast<int>(cw), static_cast<int>(ch), 0, 0);
    app->last = emscripten_get_now() / 1000.0;
    emscripten_run_script("if (Module.onGameReady) Module.onGameReady()");
    emscripten_set_main_loop_arg(frame, nullptr, 0, false);
    return 0;
}
