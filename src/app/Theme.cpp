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

ThemeStyle themeStyle(bool dark)
{
    struct Tones
    {
        const char *bar, *border, *text, *hover, *pressed, *checkedText, *disabled, *panel, *base, *accent;
    };
    const Tones t = dark ? Tones{"#26282c", "#3a3e44", "#e3e6eb", "#33363b", "#23395f", "#8ab4ff", "#6b7079",
                                 "#2c2f34", "#1f2125", "#3b82f6"}
                         : Tones{"#f6f7f9", "#dde1e6", "#1f2329", "#e8ebef", "#dbe6fd", "#1d4ed8", "#a3a9b1",
                                 "#ffffff", "#ffffff", "#2563eb"};
    QString sheet = QStringLiteral(R"(
QToolBar { background: @bar; border: none; border-bottom: 1px solid @border; padding: 3px 6px; spacing: 2px; }
QToolBar::separator { background: @border; width: 1px; margin: 8px 6px; }
QToolButton { color: @text; border: none; border-radius: 8px; padding: 3px 7px; }
QToolButton:hover { background: @hover; }
QToolButton:pressed, QToolButton:checked { background: @pressed; color: @checkedText; }
QToolButton:disabled { color: @disabled; }
QToolButton::menu-button { border: none; width: 14px; }
QLabel#modeBadge { color: white; border-radius: 10px; padding: 3px 12px; margin: 0 8px 0 2px; font-weight: 600; }
QLabel#modeBadge[mode="model"] { background: #2563eb; }
QLabel#modeBadge[mode="sketch"] { background: #ea580c; }
QToolButton#closeSketch { background: #ea580c; color: white; font-weight: 600; padding: 3px 12px; }
QToolButton#closeSketch:hover { background: #f97316; }
QFrame#valueEditor, QFrame#contextBar { background: @panel; border: 1px solid @border; border-radius: 12px; }
QFrame#valueEditor QLabel { color: @text; padding-left: 4px; }
QFrame#valueEditor QLineEdit { border: 1px solid @accent; border-radius: 6px; padding: 3px 6px;
                               background: @base; color: @text; selection-background-color: @accent; }
QToolButton#valueAccept { background: @accent; color: white; }
QToolButton#valueChoice { border: 1px solid @border; border-radius: 6px; padding: 2px 8px; }
QToolButton#valueChoice:checked { border-color: @accent; }
QStatusBar { background: @bar; border-top: 1px solid @border; }
QLabel[message="true"] { color: @checkedText; font-weight: 600; }
QLabel#statusChip { background: @pressed; color: @checkedText; border-radius: 9px; padding: 2px 10px; }
QToolBar#timeline { border-bottom: none; border-top: 1px solid @border; padding: 4px 8px; spacing: 6px; }
QLabel#timelineTitle { color: @disabled; font-weight: 600; padding: 0 8px 0 2px; }
QToolButton#timelineChip { background: @panel; border: 1px solid @border; border-radius: 10px; padding: 3px 8px; }
QToolButton#timelineChip:hover { border-color: @accent; }
QToolButton#timelineChip:checked { border-color: @accent; background: @pressed; color: @checkedText; }
QToolButton#timelineChip[rolledBack="true"] { color: @disabled; border-style: dashed; }
QToolButton#timelineChip[failed="true"] { border-color: #e5484d; color: #e5484d; }
QMenu { background: @panel; border: 1px solid @border; border-radius: 8px; padding: 4px; }
QMenu::item { padding: 5px 22px 5px 22px; border-radius: 6px; color: @text; }
QMenu::item:selected { background: @hover; }
QMenu::item:disabled { color: @disabled; }
QMenu::separator { height: 1px; background: @border; margin: 4px 8px; }
)");
    const std::pair<const char*, const char*> tones[] = {
        {"@checkedText", t.checkedText}, {"@disabled", t.disabled}, {"@pressed", t.pressed}, {"@border", t.border},
        {"@accent", t.accent},           {"@hover", t.hover},       {"@panel", t.panel},     {"@text", t.text},
        {"@base", t.base},               {"@bar", t.bar},
    };
    for (const auto& [key, value] : tones)
        sheet.replace(QLatin1String(key), QLatin1String(value));
    return {sheet, QColor(QLatin1String(t.text)), QColor(dark ? "#5b9cff" : "#2563eb")};
}
