#pragma once

#include "sketch/Constraint.h"

#include <QColor>
#include <QIcon>

enum class Icon
{
    Select, Line, Rectangle, Circle, Arc, Trim, Offset,
    NewSketch, EditSketch, CloseSketch, Extrude, Fillet, Chamfer,
    Fit, Iso, Top, Front, Right, Dimension, Coincident,
};

// Toolbar icons drawn in code, so they follow the theme: lines in `ink`, the part that
// matters (what a tool adds or changes) in `accent`. Disabled versions fade `ink`.
QIcon makeIcon(Icon icon, const QColor& ink, const QColor& accent);
QIcon makeConstraintIcon(ConstraintKind kind, const QColor& ink);
