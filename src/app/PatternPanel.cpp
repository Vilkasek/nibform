#include "app/PatternPanel.h"

#include "sketch/SketchEditor.h"
#include "viewer/UnitText.h"

#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QEvent>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLocale>
#include <QPushButton>
#include <QScopedValueRollback>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QVBoxLayout>

#include <cmath>

namespace {

constexpr double kFullTurn = 359.999; // as the model reads it: a full turn

} // namespace

PatternPanel::PatternPanel(SketchEditor* editor, QWidget* parent)
    : QWidget(parent)
    , m_editor(editor)
{
    auto* layout = new QVBoxLayout(this);
    auto* form = new QFormLayout;
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    m_sourceTitle = new QLabel(this);
    m_source = new QLabel(this);
    m_source->setWordWrap(true);
    form->addRow(m_sourceTitle, m_source);
    m_axisTitle = new QLabel(this);
    m_axis = new QLabel(this);
    m_axis->setWordWrap(true);
    form->addRow(m_axisTitle, m_axis);

    m_countLabel = new QLabel(this);
    m_count = new QSpinBox(this);
    m_count->setRange(2, 500);
    m_countLabel->setBuddy(m_count);
    form->addRow(m_countLabel, m_count);

    m_valueLabel = new QLabel(this);
    m_value = new QDoubleSpinBox(this);
    m_value->setDecimals(3);
    m_valueLabel->setBuddy(m_value);
    form->addRow(m_valueLabel, m_value);

    m_full = new QCheckBox(this);
    form->addRow(QString(), m_full);
    m_reverse = new QCheckBox(this);
    form->addRow(QString(), m_reverse);
    layout->addLayout(form);

    m_step = new QLabel(this);
    m_step->setEnabled(false); // dimmed: for information
    layout->addWidget(m_step);
    m_error = new QLabel(this);
    m_error->setWordWrap(true);
    m_error->setStyleSheet(QStringLiteral("color: #d64545;"));
    layout->addWidget(m_error);

    auto* buttons = new QHBoxLayout;
    m_accept = new QPushButton(this);
    m_cancel = new QPushButton(this);
    for (QPushButton* b : {m_accept, m_cancel})
        b->setFocusPolicy(Qt::NoFocus); // Enter and Esc keep going to the window
    buttons->addWidget(m_accept);
    buttons->addWidget(m_cancel);
    layout->addLayout(buttons);
    layout->addStretch(1);

    connect(m_count, &QSpinBox::valueChanged, this, &PatternPanel::send);
    connect(m_value, &QDoubleSpinBox::valueChanged, this, &PatternPanel::send);
    connect(m_reverse, &QCheckBox::toggled, this, &PatternPanel::send);
    connect(m_full, &QCheckBox::toggled, this, [this](bool full) {
        if (m_syncing)
            return;
        {
            const QSignalBlocker block(m_value);
            m_value->setRange(full ? 360.0 : 0.1, full ? 360.0 : 359.9);
            if (!full && m_value->value() >= 359.9)
                m_value->setValue(180.0);
            m_value->setEnabled(!full);
        }
        send();
    });
    connect(m_accept, &QPushButton::clicked, m_editor, &SketchEditor::acceptPattern);
    connect(m_cancel, &QPushButton::clicked, m_editor, &SketchEditor::cancelPattern);
    retranslate();
}

QString PatternPanel::title() const
{
    return m_circular ? tr("Circular Pattern") : tr("Linear Pattern");
}

void PatternPanel::changeEvent(QEvent* event)
{
    if (event->type() == QEvent::LanguageChange)
        retranslate();
    QWidget::changeEvent(event);
}

void PatternPanel::retranslate()
{
    m_sourceTitle->setText(tr("Repeats:"));
    m_axisTitle->setText(m_circular ? tr("Axis:") : tr("Direction:"));
    m_countLabel->setText(tr("Count:"));
    m_count->setToolTip(tr("How many in all, the original included"));
    m_valueLabel->setText(m_circular ? tr("Angle:") : tr("Spacing:"));
    m_value->setToolTip(m_circular ? tr("The angle from the first instance to the last")
                                   : tr("The distance from one instance to the next"));
    m_full->setText(tr("Full circle (spread evenly)"));
    m_reverse->setText(tr("Reverse direction"));
    m_accept->setText(tr("Accept"));
    m_accept->setToolTip(tr("Applies the pattern (Enter)"));
    m_cancel->setText(tr("Cancel"));
    m_cancel->setToolTip(tr("Drops the pattern (a changed one goes back as it was)"));
    sync();
}

bool PatternPanel::sync()
{
    const std::optional<SketchEditor::PatternState> state = m_editor->patternState();
    if (!state)
        return false;
    const bool kindChanged = m_circular != state->circular;
    m_circular = state->circular;
    if (kindChanged) {
        retranslate(); // labels for the other kind (calls back in here)
        emit titleChanged(title());
        return true;
    }

    m_source->setText(state->source.isEmpty() ? tr("tap a face of a cut or boss, or bodies") : state->source);
    m_source->setEnabled(!state->source.isEmpty());
    m_axis->setText(!state->axis.isEmpty() ? state->axis
                    : m_circular           ? tr("tap a round or straight edge, an axis or a sketch line")
                                           : tr("tap a straight edge, an axis or a sketch line"));
    m_axis->setEnabled(!state->axis.isEmpty());

    // The fields show the editor's values unless they already do (no fight while typing).
    const QScopedValueRollback guard(m_syncing, true);
    const QSignalBlocker b1(m_count), b2(m_value), b3(m_full), b4(m_reverse);
    if (m_count->value() != state->count)
        m_count->setValue(state->count);
    const bool reversed = state->value < 0.0;
    m_reverse->setChecked(reversed);
    if (m_circular) {
        const bool full = std::abs(state->value) >= kFullTurn;
        m_full->setVisible(true);
        m_full->setChecked(full);
        m_value->setSuffix(QStringLiteral(" °"));
        m_value->setDecimals(2);
        m_value->setRange(full ? 360.0 : 0.1, full ? 360.0 : 359.9);
        m_value->setEnabled(!full);
        m_reverse->setEnabled(!full);
        const double shown = full ? 360.0 : std::abs(state->value);
        if (std::abs(m_value->value() - shown) > 1e-9)
            m_value->setValue(shown);
    } else {
        m_full->setVisible(false);
        m_reverse->setEnabled(true);
        m_value->setEnabled(true);
        m_value->setSuffix(QLatin1Char(' ') + QString::fromLatin1(lengthSymbol()));
        m_value->setDecimals(lengthDecimals() + 1);
        m_value->setRange(toDisplayLength(0.001), toDisplayLength(1e5));
        const double shown = toDisplayLength(std::abs(state->value));
        if (std::abs(m_value->value() - shown) > 1e-9)
            m_value->setValue(shown);
    }
    m_error->setText(state->error);
    m_error->setVisible(!state->error.isEmpty());
    m_accept->setEnabled(state->ready);
    updateStep();
    return true;
}

void PatternPanel::send()
{
    if (m_syncing)
        return;
    const double sign = m_reverse->isChecked() && m_reverse->isEnabled() ? -1.0 : 1.0;
    double value = 0.0;
    if (m_circular)
        value = m_full->isChecked() ? 360.0 : sign * m_value->value();
    else
        value = sign * fromDisplayLength(m_value->value());
    m_editor->setPatternValues(m_count->value(), value);
    updateStep();
}

void PatternPanel::updateStep()
{
    const int count = m_count->value();
    if (m_circular) {
        const double step = m_full->isChecked() ? 360.0 / count : m_value->value() / (count - 1);
        m_step->setText(tr("%1° between instances").arg(QLocale().toString(step, 'g', 6)));
    } else {
        const double total = fromDisplayLength(m_value->value()) * (count - 1);
        m_step->setText(tr("%1 from the first to the last").arg(lengthShort(total)));
    }
}
