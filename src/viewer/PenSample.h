#pragma once

#include <QPointF>

// One stylus event, already translated into viewer coordinates.
struct PenSample
{
    enum class Phase { Hover, Down, Move, Up };

    Phase phase = Phase::Hover;
    QPointF pos;            // logical widget coordinates
    QPointF pixel;          // device pixels (what OCCT works with)
    double pressure = 0.0;  // 0..1
    bool eraser = false;
    bool barrelButton = false;
};
