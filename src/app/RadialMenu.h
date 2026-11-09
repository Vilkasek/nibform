#pragma once

#include <QIcon>
#include <QPointF>
#include <QWidget>

#include <vector>

class QAction;

// The tools of the current mode in a ring around the pen (a long press, the barrel
// button or a right click opens it), so they are in reach without the toolbar.
// A tap on an item triggers it; a tap anywhere else, or Esc, closes the ring. Opened by
// a long press, it can also be used in one motion: slide onto an item and lift.
// Covers its parent so taps outside the ring reach it; deletes itself when closed.
class RadialMenu : public QWidget
{
    Q_OBJECT

public:
    // `center` in the parent's coordinates (moved in so the ring fits); `scale` grows it
    // for fingers (tablet mode). Disabled actions are shown faded and can't be picked.
    struct Item
    {
        QAction* action;
        QIcon icon; // drawn on the ring's background (the action's own may not be)
    };
    struct Colors
    {
        QColor panel, ink, accent;
    };
    RadialMenu(std::vector<Item> items, const QPointF& center, double scale, const Colors& colors, QWidget* parent);

    // The pen that opened the menu is still down (parent coordinates): the item under it
    // lights up; when it lifts over one, that one is picked.
    void track(const QPointF& pos, bool lifted);

protected:
    bool event(QEvent* e) override;
    void paintEvent(QPaintEvent* e) override;
    void mousePressEvent(QMouseEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void mouseReleaseEvent(QMouseEvent* e) override;
    void keyPressEvent(QKeyEvent* e) override;

private:
    QPointF itemCenter(int index) const;
    int itemAt(const QPointF& pos) const; // -1: none
    void setHot(int index);
    void pick(int index);

    std::vector<Item> m_items;
    QPointF m_center;
    double m_scale = 1.0;
    Colors m_colors;
    int m_hot = -1;     // under the pointer
    int m_pressed = -1; // where the current press began
    bool m_strayed = false; // the tracked pen left the middle (lifting there doesn't pick)
};
