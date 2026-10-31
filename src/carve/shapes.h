// The face pieces, as outlines. Each shape is one or more closed polygons in
// its own units: about 1 from its centre to its edge, y up. A point is inside
// the piece when it is inside any of its polygons (even-odd within each).
#pragma once

#include <string>
#include <vector>

#include "carve/carving.h"

namespace pumpkin {

using Polygon = std::vector<Vec2>;

const std::vector<Polygon>& shapeOutline(Shape s);

// How big a new piece of this kind starts (carve units).
float defaultScale(Shape s);

// The piece as an SVG path in a 100 x 100 box (y down), for the page's buttons.
std::string shapeSvgPath(Shape s);

bool insidePolygon(const Polygon& poly, Vec2 p);

// A stamp's point in carve space to the shape's own units, and back.
Vec2 toShape(const Stamp& s, Vec2 p);
Vec2 fromShape(const Stamp& s, Vec2 q);

// Is carve point p inside the stamp?
bool insideStamp(const Stamp& s, Vec2 p);

}  // namespace pumpkin
