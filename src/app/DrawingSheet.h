#pragma once

#include "model/Drawing.h"

#include <QColor>
#include <QRectF>
#include <QString>

#include <optional>
#include <vector>

class QPainter;

// A drawing as it is painted: the views' projected lines, plus what the painter needs to
// know about the model (where section planes lie).
struct SheetContent
{
    const Drawing* drawing = nullptr;
    std::vector<ViewGeometry> views; // one per drawing view
    gp_Pnt modelMiddle;              // the bodies' middle (section planes are offset from it)
    QString units;                   // "mm" / "in", for the title block
};

// What to show in the accent color while editing (none on paper).
struct SheetHighlight
{
    enum class Kind { None, View, Dimension, Note };
    Kind kind = Kind::None;
    int index = -1;
    std::optional<gp_Pnt2d> snap; // a point the pointer would pick (sheet mm)
};

// Paints the sheet with the painter set up so that one unit is one millimetre of paper,
// origin at the sheet's top left, Y down (as QPainter is): views, section lines,
// dimensions, notes, the frame and the title block.
void paintSheet(QPainter& painter, const SheetContent& content, const SheetHighlight& highlight = {},
                const QColor& accent = QColor(37, 99, 235));

// Where things are, in sheet mm (Y up), for picking: a view's extent, a dimension's label,
// a note's text.
QRectF viewRect(const SheetContent& content, int view);
std::optional<QRectF> dimensionLabelRect(const SheetContent& content, int dimension);
QRectF noteRect(const SheetNote& note);

// A sheet point (Y up) in the painter's coordinates (Y down), and back.
QPointF toPaint(const Drawing& drawing, const gp_Pnt2d& p);
gp_Pnt2d fromPaint(const Drawing& drawing, const QPointF& p);

// The text a dimension shows, in the current length unit ("12.5", "R4", "⌀10").
QString dimensionLabel(const SheetDimension& dimension, double value);
