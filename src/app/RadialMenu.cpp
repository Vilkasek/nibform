#include "app/RadialMenu.h"

#include <QAction>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>

#include <algorithm>
#include <cmath>

namespace {

constexpr double kRingRadius = 80.0; // logical pixels at scale 1
constexpr double kItemRadius = 23.0;
constexpr double kHubRadius = 16.0;
constexpr double kIconSize = 24.0;

QString plainText(const QAction* action)
{
    QString text = action->text();
    text.remove(QLatin1Char('&'));
    return text;
}

} // namespace

RadialMenu::RadialMenu(std::vector<Item> items, const QPointF& center, double scale, const Colors& colors,
                       QWidget* parent)
    : QWidget(parent)
    , m_items(std::move(items))
    , m_scale(scale)
    , m_colors(colors)
{
    setAttribute(Qt::WA_DeleteOnClose);
    setAttribute(Qt::WA_NoSystemBackground);
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
    setGeometry(parent->rect());

    // Keep the whole ring (and the label under it) on screen.
    const double reach = (kRingRadius + kItemRadius) * m_scale + 4.0;
    const double below = reach + 30.0 * m_scale;
    m_center = QPointF(std::clamp(center.x(), reach, std::max(reach, width() - reach)),
                       std::clamp(center.y(), reach, std::max(reach, height() - below)));
    m_hot = itemAt(mapFromParent(center.toPoint()));

    show();
    raise();
    setFocus(Qt::PopupFocusReason);
}

QPointF RadialMenu::itemCenter(int index) const
{
    // Clockwise from the top.
    const double angle = -M_PI / 2.0 + 2.0 * M_PI * index / static_cast<double>(m_items.size());
    return m_center + QPointF(std::cos(angle), std::sin(angle)) * kRingRadius * m_scale;
}

int RadialMenu::itemAt(const QPointF& pos) const
{
    // The whole slice around an item counts, from just outside the hub to past the ring.
    const QPointF d = pos - m_center;
    const double r = std::hypot(d.x(), d.y());
    if (m_items.empty() || r < (kHubRadius + 6.0) * m_scale || r > (kRingRadius + kItemRadius + 18.0) * m_scale)
        return -1;
    double angle = std::atan2(d.y(), d.x()) + M_PI / 2.0; // 0 at the top, clockwise
    const double slice = 2.0 * M_PI / static_cast<double>(m_items.size());
    angle = std::fmod(angle + slice / 2.0 + 4.0 * M_PI, 2.0 * M_PI);
    return std::min(static_cast<int>(angle / slice), static_cast<int>(m_items.size()) - 1);
}

void RadialMenu::setHot(int index)
{
    if (index == m_hot)
        return;
    m_hot = index;
    update();
}

void RadialMenu::pick(int index)
{
    if (index < 0 || index >= static_cast<int>(m_items.size()) || !m_items[index].action->isEnabled())
        return;
    QAction* action = m_items[index].action;
    close(); // first, so the tool gets the keyboard focus back
    action->trigger();
}

void RadialMenu::track(const QPointF& pos, bool lifted)
{
    const QPointF local = mapFromParent(pos.toPoint());
    const QPointF d = local - m_center;
    if (std::hypot(d.x(), d.y()) > (kHubRadius + 6.0) * m_scale)
        m_strayed = true;
    setHot(itemAt(local));
    if (lifted && m_strayed && m_hot >= 0)
        pick(m_hot);
    // Lifted in the middle: the ring stays open for a tap.
}

bool RadialMenu::event(QEvent* e)
{
    // Esc closes the ring before the window's shortcut (cancel) sees it.
    if (e->type() == QEvent::ShortcutOverride && static_cast<QKeyEvent*>(e)->key() == Qt::Key_Escape) {
        e->accept();
        return true;
    }
    return QWidget::event(e);
}

void RadialMenu::keyPressEvent(QKeyEvent* e)
{
    if (e->key() == Qt::Key_Escape) {
        close();
        return;
    }
    QWidget::keyPressEvent(e);
}

void RadialMenu::mousePressEvent(QMouseEvent* e)
{
    m_pressed = itemAt(e->position());
    if (m_pressed < 0)
        close(); // a tap outside the ring
}

void RadialMenu::mouseMoveEvent(QMouseEvent* e)
{
    setHot(itemAt(e->position()));
}

void RadialMenu::mouseReleaseEvent(QMouseEvent* e)
{
    const int at = itemAt(e->position());
    if (at >= 0 && at == m_pressed)
        pick(at);
    m_pressed = -1;
}

void RadialMenu::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QColor& panel = m_colors.panel;
    const QColor& ink = m_colors.ink;
    const double s = m_scale;

    // A soft disc behind the ring, so it reads over any model.
    QColor back = panel;
    back.setAlpha(225);
    QColor edge = ink;
    edge.setAlpha(40);
    p.setPen(QPen(edge, 1.0));
    p.setBrush(back);
    p.drawEllipse(m_center, (kRingRadius + kItemRadius + 10.0) * s, (kRingRadius + kItemRadius + 10.0) * s);

    // The hub: a cross that closes the ring.
    p.setPen(QPen(ink, 1.6 * s, Qt::SolidLine, Qt::RoundCap));
    p.setBrush(Qt::NoBrush);
    p.drawEllipse(m_center, kHubRadius * s, kHubRadius * s);
    const double x = 5.0 * s;
    p.drawLine(m_center + QPointF(-x, -x), m_center + QPointF(x, x));
    p.drawLine(m_center + QPointF(-x, x), m_center + QPointF(x, -x));

    const double iconSize = kIconSize * s;
    for (int i = 0; i < static_cast<int>(m_items.size()); ++i) {
        const QAction* action = m_items[i].action;
        const QPointF c = itemCenter(i);
        const bool hot = i == m_hot && action->isEnabled();
        const bool current = action->isCheckable() && action->isChecked();
        QColor fill = hot ? m_colors.accent : panel;
        if (current && !hot)
            fill = m_colors.accent.lighter(panel.lightness() > 128 ? 175 : 60);
        p.setPen(QPen(current || hot ? m_colors.accent : edge, current ? 2.0 : 1.0));
        p.setBrush(fill);
        p.drawEllipse(c, kItemRadius * s, kItemRadius * s);
        const QIcon::Mode mode = !action->isEnabled() ? QIcon::Disabled : hot ? QIcon::Selected : QIcon::Normal;
        const QPixmap pixmap = m_items[i].icon.pixmap(QSize(qRound(iconSize), qRound(iconSize)), devicePixelRatioF(), mode);
        p.drawPixmap(QRectF(c - QPointF(iconSize / 2.0, iconSize / 2.0), QSizeF(iconSize, iconSize)), pixmap,
                     QRectF(pixmap.rect()));
    }

    // What the lit item does, under the ring.
    const int shown = m_hot >= 0 ? m_hot : -1;
    if (shown >= 0) {
        QFont font = p.font();
        font.setBold(true);
        font.setPointSizeF(font.pointSizeF() * std::max(1.0, s * 0.9));
        p.setFont(font);
        const QString text = plainText(m_items[shown].action);
        const QFontMetricsF metrics(font);
        const QSizeF size = metrics.size(Qt::TextSingleLine, text) + QSizeF(16, 6);
        const QPointF at = m_center + QPointF(0.0, (kRingRadius + kItemRadius + 12.0) * s + size.height() / 2.0);
        const QRectF rect(at - QPointF(size.width() / 2.0, size.height() / 2.0), size);
        p.setPen(Qt::NoPen);
        p.setBrush(m_colors.accent);
        p.drawRoundedRect(rect, size.height() / 2.0, size.height() / 2.0);
        p.setPen(Qt::white);
        p.drawText(rect, Qt::AlignCenter, text);
    }
}
