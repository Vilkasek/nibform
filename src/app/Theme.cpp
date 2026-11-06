#include "app/Theme.h"

#include <QApplication>
#include <QGuiApplication>
#include <QStyle>
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
    if (!dark)
        return QApplication::style()->standardPalette();

    const QColor window(45, 47, 51);
    const QColor base(32, 34, 37);
    const QColor text(225, 228, 233);
    const QColor disabled(125, 129, 136);
    const QColor accent(66, 133, 244);

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
    p.setColor(QPalette::BrightText, Qt::white);
    p.setColor(QPalette::Light, window.lighter(130));
    p.setColor(QPalette::Midlight, window.lighter(115));
    p.setColor(QPalette::Mid, window.darker(120));
    p.setColor(QPalette::Dark, window.darker(150));
    p.setColor(QPalette::Shadow, Qt::black);
    p.setColor(QPalette::Highlight, accent);
    p.setColor(QPalette::HighlightedText, Qt::white);
    p.setColor(QPalette::Link, accent.lighter(120));
    for (QPalette::ColorRole role : {QPalette::WindowText, QPalette::Text, QPalette::ButtonText})
        p.setColor(QPalette::Disabled, role, disabled);
    p.setColor(QPalette::Disabled, QPalette::Highlight, window.lighter(140));
    return p;
}
