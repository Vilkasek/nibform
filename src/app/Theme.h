#pragma once

#include <QColor>
#include <QPalette>
#include <QString>

enum class ThemeMode { System, Light, Dark };

// Whether `mode` means dark right now; System follows the desktop's color scheme.
bool resolvesToDark(ThemeMode mode);

// Application palette for the Fusion style.
QPalette themePalette(bool dark);

// The main window's style sheet, and the colors toolbar icons are drawn with.
struct ThemeStyle
{
    QString sheet;
    QColor ink;    // icon strokes, matching the text
    QColor accent; // what a tool adds or changes
};
ThemeStyle themeStyle(bool dark);
