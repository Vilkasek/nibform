#include "app/Theme.h"

#include <QApplication>
#include <QGuiApplication>
#include <QStyleHints>

bool resolvesToDark(ThemeMode mode)
{
    switch (mode) {
    case ThemeMode::Light:
        return false;
    case ThemeMode::Dark:
        return true;
    case ThemeMode::System:
        break;
    }
    // Qt guesses the scheme from the application palette when the platform can't tell,
    // so drop any palette we set ourselves before asking.
    qApp->setPalette(QPalette());
    return QGuiApplication::styleHints()->colorScheme() == Qt::ColorScheme::Dark;
}

QPalette themePalette(bool dark)
{
    // Both spelled out: Fusion's own "standard" palette follows the desktop's scheme,
    // so it would turn dark under a dark desktop even when light was asked for.
    const QColor window = dark ? QColor(45, 47, 51) : QColor(246, 247, 249);
    const QColor base = dark ? QColor(32, 34, 37) : QColor(255, 255, 255);
    const QColor text = dark ? QColor(225, 228, 233) : QColor(31, 35, 41);
    const QColor disabled = dark ? QColor(125, 129, 136) : QColor(160, 166, 174);
    const QColor accent = dark ? QColor(66, 133, 244) : QColor(37, 99, 235);

    QPalette p;
    p.setColor(QPalette::Window, window);
    p.setColor(QPalette::WindowText, text);
    p.setColor(QPalette::Base, base);
    p.setColor(QPalette::AlternateBase, window);
    p.setColor(QPalette::ToolTipBase, base);
    p.setColor(QPalette::ToolTipText, text);
    p.setColor(QPalette::PlaceholderText, disabled);
    p.setColor(QPalette::Text, text);
    p.setColor(QPalette::Button, window);
    p.setColor(QPalette::ButtonText, text);
    p.setColor(QPalette::BrightText, dark ? Qt::white : Qt::black);
    p.setColor(QPalette::Light, dark ? window.lighter(130) : QColor(255, 255, 255));
    p.setColor(QPalette::Midlight, dark ? window.lighter(115) : window.darker(104));
    p.setColor(QPalette::Mid, window.darker(dark ? 120 : 125));
    p.setColor(QPalette::Dark, window.darker(dark ? 150 : 160));
    p.setColor(QPalette::Shadow, Qt::black);
    p.setColor(QPalette::Highlight, accent);
    p.setColor(QPalette::HighlightedText, Qt::white);
    p.setColor(QPalette::Link, accent.lighter(120));
    for (QPalette::ColorRole role : {QPalette::WindowText, QPalette::Text, QPalette::ButtonText})
        p.setColor(QPalette::Disabled, role, disabled);
    p.setColor(QPalette::Disabled, QPalette::Highlight, dark ? window.lighter(140) : window.darker(115));
    return p;
}
