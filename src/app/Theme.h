#pragma once

#include <QPalette>

enum class ThemeMode { System, Light, Dark };

// Whether `mode` means dark right now; System follows the desktop's color scheme.
bool resolvesToDark(ThemeMode mode);

// Application palette for the Fusion style.
QPalette themePalette(bool dark);
