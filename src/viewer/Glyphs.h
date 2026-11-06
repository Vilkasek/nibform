#pragma once

#include "sketch/Constraint.h"

#include <QColor>
#include <QRectF>

class QPainter;

// Small vector drawings of the constraint kinds, so glyphs and buttons don't depend on
// which fonts are installed. Drawn in `ink`, filling the square `r`.
void drawConstraintGlyph(QPainter& p, ConstraintKind kind, const QRectF& r, const QColor& ink);
