#include "viewer/ValueEditor.h"

#include <QEvent>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QRegularExpression>
#include <QTimer>
#include <QToolButton>

#include <algorithm>

ValueEditor::ValueEditor(QWidget* parent)
    : QFrame(parent)
    , m_label(new QLabel(this))
    , m_edit(new QLineEdit(this))
{
    setFrameShape(QFrame::StyledPanel);
    setAutoFillBackground(true);

    auto* ok = new QToolButton(this);
    ok->setText(QStringLiteral("✓"));
    ok->setToolTip(tr("Zatwierdź (Enter)"));
    auto* no = new QToolButton(this);
    no->setText(QStringLiteral("✕"));
    no->setToolTip(tr("Anuluj (Esc)"));
    connect(ok, &QToolButton::clicked, this, &ValueEditor::accept);
    connect(no, &QToolButton::clicked, this, &ValueEditor::cancel);

    m_edit->setAlignment(Qt::AlignRight);
    m_edit->setMinimumWidth(90);
    m_edit->installEventFilter(this);

    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(6, 4, 4, 4);
    layout->setSpacing(4);
    layout->addWidget(m_label);
    layout->addWidget(m_edit);
    layout->addWidget(ok);
    layout->addWidget(no);
    hide();
}

void ValueEditor::open(const QString& label, double value, double min, double max, const QPointF& anchor,
                       std::function<void(double)> accept, std::function<void()> cancel)
{
    if (isOpen())
        this->cancel();
    m_label->setText(label);
    m_min = min;
    m_max = max;
    m_anchor = anchor;
    m_accept = std::move(accept);
    m_cancel = std::move(cancel);
    m_edit->setText(QLocale().toString(value, 'f', 3).remove(QRegularExpression(QStringLiteral("[.,]?0+$"))));
    m_edit->setStyleSheet(QString());

    adjustSize();
    const QWidget* area = parentWidget();
    const int x = std::clamp(qRound(anchor.x() - width() / 2.0), 4, std::max(4, area->width() - width() - 4));
    const int y = std::clamp(qRound(anchor.y() - height() / 2.0), 4, std::max(4, area->height() - height() - 4));
    move(x, y);
    show();
    raise();
    refocus();
}

void ValueEditor::refocus()
{
    // Deferred: a click that is still being delivered would otherwise take the focus back.
    QTimer::singleShot(0, m_edit, [edit = m_edit] {
        edit->setFocus(Qt::OtherFocusReason);
        edit->selectAll();
    });
}

void ValueEditor::cancel()
{
    if (!isOpen())
        return;
    hide();
    auto done = std::move(m_cancel);
    m_accept = nullptr;
    m_cancel = nullptr;
    if (done)
        done();
}

void ValueEditor::accept()
{
    if (!isOpen())
        return;
    // Accept both decimal separators, whatever the locale says.
    QString text = m_edit->text().trimmed();
    text.replace(QLatin1Char(','), QLatin1Char('.'));
    bool ok = false;
    const double value = QLocale::c().toDouble(text, &ok);
    if (!ok || value < m_min || value > m_max) {
        m_edit->setStyleSheet(QStringLiteral("color: #e5484d;"));
        m_edit->selectAll();
        return;
    }
    hide();
    auto done = std::move(m_accept);
    m_accept = nullptr;
    m_cancel = nullptr;
    if (done)
        done(value);
}

bool ValueEditor::eventFilter(QObject* watched, QEvent* event)
{
    if (watched != m_edit)
        return false;
    if (event->type() == QEvent::ShortcutOverride || event->type() == QEvent::KeyPress) {
        // Enter and Esc are also window-wide shortcuts; claim them while editing.
        const int key = static_cast<QKeyEvent*>(event)->key();
        const bool ours = key == Qt::Key_Return || key == Qt::Key_Enter || key == Qt::Key_Escape;
        if (!ours)
            return false;
        event->accept();
        if (event->type() == QEvent::KeyPress) {
            if (key == Qt::Key_Escape)
                cancel();
            else
                accept();
        }
        return true;
    }
    if (event->type() == QEvent::KeyRelease)
        m_edit->setStyleSheet(QString()); // typing clears the "invalid" mark
    return false;
}
