#include "viewer/ValueEditor.h"

#include <QEvent>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QRegularExpression>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>

ValueEditor::ValueEditor(QWidget* parent)
    : QFrame(parent)
    , m_label(new QLabel(this))
    , m_edit(new QLineEdit(this))
    , m_keypad(new QWidget(this))
    , m_choices(new QWidget(this))
{
    setObjectName(QStringLiteral("valueEditor")); // styled by the window's style sheet
    setFrameShape(QFrame::StyledPanel);
    setAutoFillBackground(true);

    auto* ok = new QToolButton(this);
    ok->setText(QStringLiteral("✓"));
    ok->setObjectName(QStringLiteral("valueAccept"));
    auto* no = new QToolButton(this);
    no->setText(QStringLiteral("✕"));
    no->setObjectName(QStringLiteral("valueCancel"));
    retranslate();
    connect(ok, &QToolButton::clicked, this, &ValueEditor::accept);
    connect(no, &QToolButton::clicked, this, &ValueEditor::cancel);

    m_edit->setAlignment(Qt::AlignRight);
    // Live preview: report every valid value as it is typed (not values set from outside).
    connect(m_edit, &QLineEdit::textEdited, this, &ValueEditor::edited);
    m_edit->setMinimumWidth(90);
    m_edit->installEventFilter(this);

    auto* row = new QHBoxLayout;
    row->setSpacing(4);
    row->addWidget(m_label);
    row->addWidget(m_edit);
    row->addWidget(ok);
    row->addWidget(no);

    // Keys big enough for a fingertip; they never take the focus from the field.
    auto* keys = new QGridLayout(m_keypad);
    keys->setContentsMargins(0, 2, 0, 0);
    keys->setSpacing(4);
    const char* layout[4][4] = {{"7", "8", "9", "⌫"}, {"4", "5", "6", "C"}, {"1", "2", "3", "±"}, {"0", nullptr, ",", "✓"}};
    for (int r = 0; r < 4; ++r) {
        for (int c = 0; c < 4; ++c) {
            if (!layout[r][c])
                continue;
            const QString text = QString::fromUtf8(layout[r][c]);
            auto* button = new QToolButton(m_keypad);
            button->setText(text);
            button->setObjectName(QStringLiteral("key_") + text);
            button->setFocusPolicy(Qt::NoFocus);
            button->setMinimumSize(48, 40);
            button->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
            connect(button, &QToolButton::clicked, this, [this, text] { key(text); });
            keys->addWidget(button, r, c, 1, text == QStringLiteral("0") ? 2 : 1); // a wide zero
        }
    }
    m_keypad->hide();
    auto* choiceRow = new QHBoxLayout(m_choices);
    choiceRow->setContentsMargins(0, 0, 0, 0);
    choiceRow->setSpacing(2);
    m_choices->hide();

    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(6, 4, 4, 4);
    outer->setSpacing(4);
    outer->addLayout(row);
    outer->addWidget(m_choices);
    outer->addWidget(m_keypad);
    hide();
}

void ValueEditor::open(std::function<QString()> label, double value, double min, double max, const QPointF& anchor,
                       std::function<void(double)> accept, std::function<void()> cancel,
                       std::function<void(double)> changed)
{
    if (isOpen())
        this->cancel();
    clearChoices();
    m_labelText = std::move(label);
    m_label->setText(m_labelText());
    m_min = min;
    m_max = max;
    m_anchor = anchor;
    m_accept = std::move(accept);
    m_cancel = std::move(cancel);
    m_changed = std::move(changed);
    m_edit->setText(format(value));
    m_edit->selectAll(); // right away: the first key or keypad tap replaces the value
    m_edit->setStyleSheet(QString());

    // Sized once shown: only then do the style sheet's fonts apply, sized before the label clips.
    show();
    adjustSize();
    moveClamped(anchor.x() - width() / 2.0, anchor.y() - height() / 2.0);
    raise();
    refocus();
}

void ValueEditor::moveClamped(double x, double y)
{
    const QWidget* area = parentWidget();
    move(std::clamp(qRound(x), 4, std::max(4, area->width() - width() - 4)),
         std::clamp(qRound(y), 4, std::max(4, area->height() - height() - 4)));
}

void ValueEditor::placeBeside(const QPointF& point)
{
    m_anchor = point;
    // To the right of the point; on the left when it wouldn't fit there (narrow windows).
    const bool fitsRight = point.x() + 20.0 + width() <= parentWidget()->width() - 4;
    moveClamped(fitsRight ? point.x() + 20.0 : point.x() - 20.0 - width(), point.y() - height() / 2.0);
}

void ValueEditor::retranslate()
{
    findChild<QToolButton*>(QStringLiteral("valueAccept"))->setToolTip(tr("Accept (Enter)"));
    findChild<QToolButton*>(QStringLiteral("valueCancel"))->setToolTip(tr("Cancel (Esc)"));
}

void ValueEditor::changeEvent(QEvent* event)
{
    if (event->type() == QEvent::LanguageChange) {
        retranslate();
        if (m_labelText) {
            m_label->setText(m_labelText());
            refreshChoices();
            adjustSize();
        }
    }
    QFrame::changeEvent(event);
}

void ValueEditor::setChoices(std::function<QStringList()> labels, int current, const std::function<void(int)>& chosen)
{
    clearChoices();
    m_choiceLabels = std::move(labels);
    const QStringList texts = m_choiceLabels();
    for (int i = 0; i < texts.size(); ++i) {
        auto* button = new QToolButton(m_choices);
        button->setObjectName(QStringLiteral("valueChoice"));
        button->setText(texts[i]);
        button->setCheckable(true);
        button->setAutoExclusive(true);
        button->setChecked(i == current);
        button->setFocusPolicy(Qt::NoFocus); // typing goes on in the field
        button->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        connect(button, &QToolButton::clicked, this, [chosen, i] {
            if (chosen)
                chosen(i);
        });
        m_choices->layout()->addWidget(button);
    }
    m_choices->setVisible(!texts.isEmpty());
    if (isOpen())
        adjustSize();
}

void ValueEditor::refreshChoices()
{
    if (!m_choiceLabels)
        return;
    const QStringList texts = m_choiceLabels();
    const QList<QToolButton*> buttons = m_choices->findChildren<QToolButton*>();
    for (int i = 0; i < std::min<int>(texts.size(), buttons.size()); ++i)
        buttons[i]->setText(texts[i]);
    if (isOpen())
        adjustSize();
}

void ValueEditor::clearChoices()
{
    m_choiceLabels = nullptr;
    qDeleteAll(m_choices->findChildren<QToolButton*>());
    m_choices->hide();
}

void ValueEditor::setKeypad(bool on)
{
    m_keypad->setVisible(on);
    adjustSize();
}

void ValueEditor::edited()
{
    m_edit->setStyleSheet(QString());
    if (const auto value = parsed(); value && m_changed)
        m_changed(*value);
}

void ValueEditor::key(const QString& key)
{
    if (key == QStringLiteral("✓")) {
        accept();
        return;
    }
    if (key == QStringLiteral("⌫")) {
        m_edit->backspace();
    } else if (key == QStringLiteral("C")) {
        m_edit->clear();
    } else if (key == QStringLiteral("±")) {
        const QString text = m_edit->text();
        m_edit->setText(text.startsWith(QLatin1Char('-')) ? text.mid(1) : QLatin1Char('-') + text);
    } else {
        m_edit->insert(key); // replaces the selection, like typing would
    }
    edited();
}

QString ValueEditor::format(double value)
{
    // Three decimals at most, without trailing zeros.
    return QLocale().toString(value, 'f', 3).remove(QRegularExpression(QStringLiteral("[.,]?0+$")));
}

void ValueEditor::setValue(double value)
{
    m_edit->setText(format(value));
    m_edit->setStyleSheet(QString());
    if (m_edit->hasFocus())
        m_edit->selectAll();
}

std::optional<double> ValueEditor::parsed() const
{
    // Accept both decimal separators, whatever the locale says.
    QString text = m_edit->text().trimmed();
    text.replace(QLatin1Char(','), QLatin1Char('.'));
    bool ok = false;
    const double value = QLocale::c().toDouble(text, &ok);
    if (!ok || value < m_min || value > m_max)
        return std::nullopt;
    return value;
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
    m_changed = nullptr;
    if (done)
        done();
}

void ValueEditor::accept()
{
    if (!isOpen())
        return;
    const auto value = parsed();
    if (!value) {
        m_edit->setStyleSheet(QStringLiteral("color: #e5484d;"));
        m_edit->selectAll();
        return;
    }
    hide();
    auto done = std::move(m_accept);
    m_accept = nullptr;
    m_cancel = nullptr;
    m_changed = nullptr;
    if (done)
        done(*value);
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
