#include "app/AssemblyPanel.h"

#include "sketch/SketchEditor.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QEvent>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QScopedValueRollback>
#include <QSignalBlocker>
#include <QSlider>
#include <QTimer>
#include <QToolButton>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <cmath>

namespace {

enum PartColumn { PartName, PartGrounded, PartColumns };

constexpr JointKind kKinds[] = {JointKind::Rigid,       JointKind::Revolute, JointKind::Slider,
                                JointKind::Cylindrical, JointKind::Ball,     JointKind::Planar};

// A slider's steps: tenths of a degree or of a millimetre.
constexpr double kSliderStep = 0.1;

QDoubleSpinBox* spinBox(QWidget* parent)
{
    auto* box = new QDoubleSpinBox(parent);
    box->setRange(-1e5, 1e5);
    box->setDecimals(2);
    box->setKeyboardTracking(false); // a value is taken when typing ends
    return box;
}

} // namespace

AssemblyPanel::AssemblyPanel(SketchEditor* editor, QWidget* parent)
    : QWidget(parent)
    , m_editor(editor)
{
    setObjectName(QStringLiteral("assemblyPanel"));
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 6, 8, 8);
    layout->setSpacing(6);

    // Parts: shown (the check), grounded, picked with the view.
    m_partsTitle = new QLabel(this);
    layout->addWidget(m_partsTitle);
    m_parts = new QTreeWidget(this);
    m_parts->setColumnCount(PartColumns);
    m_parts->setRootIsDecorated(false);
    m_parts->header()->setSectionResizeMode(PartName, QHeaderView::Stretch);
    m_parts->header()->setSectionResizeMode(PartGrounded, QHeaderView::ResizeToContents);
    connect(m_parts, &QTreeWidget::itemChanged, this, [this](QTreeWidgetItem* item, int column) {
        if (m_syncing)
            return;
        const int index = m_parts->indexOfTopLevelItem(item);
        if (column == PartName)
            m_editor->setComponentHidden(index, item->checkState(PartName) != Qt::Checked);
        else if (column == PartGrounded)
            m_editor->setComponentGrounded(index, item->checkState(PartGrounded) == Qt::Checked);
    });
    connect(m_parts, &QTreeWidget::itemSelectionChanged, this, [this] {
        if (m_syncing)
            return;
        const auto items = m_parts->selectedItems();
        m_editor->pickComponent(items.isEmpty() ? -1 : m_parts->indexOfTopLevelItem(items.front()));
    });
    layout->addWidget(m_parts, 1);
    auto* partButtons = new QHBoxLayout;
    m_removePart = new QToolButton(this);
    m_removePart->setText(QStringLiteral("−"));
    connect(m_removePart, &QToolButton::clicked, this, [this] { m_editor->removeComponent(m_editor->pickedComponent()); });
    partButtons->addWidget(m_removePart);
    partButtons->addStretch();
    layout->addLayout(partButtons);

    // Joints, and the one picked in the list.
    m_jointsTitle = new QLabel(this);
    layout->addWidget(m_jointsTitle);
    m_joints = new QTreeWidget(this);
    m_joints->setColumnCount(2);
    m_joints->setRootIsDecorated(false);
    m_joints->setHeaderHidden(true);
    m_joints->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_joints->header()->setSectionResizeMode(1, QHeaderView::Stretch);
    connect(m_joints, &QTreeWidget::itemSelectionChanged, this, &AssemblyPanel::syncJoint);
    layout->addWidget(m_joints, 1);

    m_jointBox = new QWidget(this);
    auto* grid = new QGridLayout(m_jointBox);
    grid->setContentsMargins(0, 0, 0, 0);
    grid->setHorizontalSpacing(4);
    m_kind = new QComboBox(m_jointBox);
    connect(m_kind, &QComboBox::activated, this, [this](int index) {
        if (!m_syncing && currentJoint() >= 0)
            m_editor->setJointKind(currentJoint(), kKinds[index]);
    });
    m_flip = new QCheckBox(m_jointBox);
    connect(m_flip, &QCheckBox::clicked, this, [this] {
        if (!m_syncing && currentJoint() >= 0)
            m_editor->flipJoint(currentJoint());
    });
    grid->addWidget(m_kind, 0, 0, 1, 3);
    grid->addWidget(m_flip, 0, 3, 1, 3);
    for (int v = 0; v < 3; ++v) {
        ValueRow& row = m_values[v];
        row.label = new QLabel(m_jointBox);
        row.slider = new QSlider(Qt::Horizontal, m_jointBox);
        row.value = spinBox(m_jointBox);
        row.limited = new QCheckBox(m_jointBox);
        row.min = spinBox(m_jointBox);
        row.max = spinBox(m_jointBox);
        row.check = new QToolButton(m_jointBox);
        // The slider moves the model live; letting go records the step.
        connect(row.slider, &QSlider::valueChanged, this, [this, v](int position) {
            if (m_syncing || currentJoint() < 0)
                return;
            m_editor->setJointValue(currentJoint(), v, position * kSliderStep, !m_values[v].slider->isSliderDown());
        });
        connect(row.slider, &QSlider::sliderReleased, this, [this, v] {
            if (currentJoint() >= 0)
                m_editor->setJointValue(currentJoint(), v, m_values[v].slider->value() * kSliderStep, true);
        });
        connect(row.value, &QDoubleSpinBox::valueChanged, this, [this, v](double value) {
            if (!m_syncing && currentJoint() >= 0)
                m_editor->setJointValue(currentJoint(), v, value, true);
        });
        auto limits = [this, v] {
            if (m_syncing || currentJoint() < 0)
                return;
            const ValueRow& r = m_values[v];
            m_editor->setJointLimits(currentJoint(), v, r.limited->isChecked(), r.min->value(), r.max->value());
        };
        connect(row.limited, &QCheckBox::clicked, this, limits);
        connect(row.min, &QDoubleSpinBox::valueChanged, this, limits);
        connect(row.max, &QDoubleSpinBox::valueChanged, this, limits);
        connect(row.check, &QToolButton::clicked, this, [this, v] {
            if (currentJoint() >= 0)
                m_editor->checkJointRange(currentJoint(), v);
        });
        const int line = 1 + 2 * v;
        grid->addWidget(row.label, line, 0, 1, 2);
        grid->addWidget(row.slider, line, 2, 1, 3);
        grid->addWidget(row.value, line, 5);
        grid->addWidget(row.limited, line + 1, 0, 1, 2);
        grid->addWidget(row.min, line + 1, 2);
        grid->addWidget(row.max, line + 1, 3);
        grid->addWidget(row.check, line + 1, 4, 1, 2);
    }
    m_jointError = new QLabel(m_jointBox);
    m_jointError->setObjectName(QStringLiteral("jointError"));
    m_jointError->setWordWrap(true);
    m_jointError->setStyleSheet(QStringLiteral("color: #e5484d;"));
    grid->addWidget(m_jointError, 7, 0, 1, 6);
    m_removeJoint = new QToolButton(m_jointBox);
    connect(m_removeJoint, &QToolButton::clicked, this, [this] {
        if (currentJoint() >= 0)
            m_editor->removeJoint(currentJoint());
    });
    grid->addWidget(m_removeJoint, 8, 0, 1, 3);
    layout->addWidget(m_jointBox);

    m_help = new QLabel(this);
    m_help->setWordWrap(true);
    m_help->setObjectName(QStringLiteral("parametersHelp"));
    layout->addWidget(m_help);

    auto later = [this] { QTimer::singleShot(0, this, &AssemblyPanel::sync); };
    connect(m_editor, &SketchEditor::assemblyChanged, this, later);
    connect(m_editor, &SketchEditor::historyChanged, this, later);
    connect(m_editor, &SketchEditor::bodiesChanged, this, later);
    retranslate();
    sync();
}

void AssemblyPanel::changeEvent(QEvent* event)
{
    if (event->type() == QEvent::LanguageChange) {
        retranslate();
        sync();
    }
    QWidget::changeEvent(event);
}

void AssemblyPanel::retranslate()
{
    m_partsTitle->setText(tr("Parts"));
    m_parts->setHeaderLabels({tr("Part"), tr("Grounded")});
    m_removePart->setToolTip(tr("Remove the picked part (and its joints)"));
    m_jointsTitle->setText(tr("Joints"));
    {
        const QSignalBlocker block(m_kind);
        m_kind->clear();
        for (JointKind k : kKinds)
            m_kind->addItem(SketchEditor::jointKindName(k));
    }
    m_flip->setText(tr("Facing the same way"));
    m_flip->setToolTip(tr("The two frames point the same way (by default they face each other, like touching faces)"));
    for (ValueRow& row : m_values) {
        row.limited->setText(tr("Limits"));
        row.min->setToolTip(tr("Lowest value"));
        row.max->setToolTip(tr("Highest value"));
        row.check->setText(tr("Check Range"));
        row.check->setToolTip(tr("Moves the joint through its range looking for parts that collide"));
    }
    m_removeJoint->setText(tr("Remove Joint"));
    m_help->setText(tr("Insert adds parts; Joint joins two (a face, round edge or corner of each); drag a part in "
                       "the view to move it as its joints allow."));
}

int AssemblyPanel::currentJoint() const
{
    const auto items = m_joints->selectedItems();
    return items.isEmpty() ? -1 : m_joints->indexOfTopLevelItem(items.front());
}

void AssemblyPanel::sync()
{
    const QScopedValueRollback guard(m_syncing, true);
    const auto parts = m_editor->componentInfos();
    m_parts->clear();
    for (const SketchEditor::ComponentInfo& info : parts) {
        auto* item = new QTreeWidgetItem(m_parts);
        item->setText(PartName, info.missing ? tr("%1 (missing)").arg(info.name) : info.name);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(PartName, info.hidden ? Qt::Unchecked : Qt::Checked);
        item->setCheckState(PartGrounded, info.grounded ? Qt::Checked : Qt::Unchecked);
        QString tip = info.source;
        if (!info.problem.isEmpty())
            tip += QStringLiteral("\n") + tr("Using the copy kept in the project: %1").arg(info.problem);
        item->setToolTip(PartName, tip);
        if (info.missing || !info.problem.isEmpty())
            item->setForeground(PartName, QColor(0xe5, 0x48, 0x4d));
    }
    const int picked = m_editor->pickedComponent();
    if (picked >= 0 && picked < m_parts->topLevelItemCount())
        m_parts->topLevelItem(picked)->setSelected(true);
    m_removePart->setEnabled(picked >= 0);

    const int current = currentJoint();
    const auto joints = m_editor->jointInfos();
    m_joints->clear();
    for (const SketchEditor::JointInfo& info : joints) {
        auto* item = new QTreeWidgetItem(m_joints, {info.name, info.between});
        if (!info.error.isEmpty()) {
            item->setForeground(0, QColor(0xe5, 0x48, 0x4d));
            item->setToolTip(0, info.error);
        }
    }
    if (m_joints->topLevelItemCount() > 0)
        m_joints->topLevelItem(std::clamp(current, 0, m_joints->topLevelItemCount() - 1))->setSelected(true);
    m_jointsTitle->setVisible(!joints.empty());
    m_joints->setVisible(!joints.empty());
    syncJoint();
}

void AssemblyPanel::syncJoint()
{
    const QScopedValueRollback guard(m_syncing, true);
    const int index = currentJoint();
    const auto joints = m_editor->jointInfos();
    m_jointBox->setVisible(index >= 0 && index < static_cast<int>(joints.size()));
    if (!m_jointBox->isVisible())
        return;
    const SketchEditor::JointInfo& joint = joints[index];
    m_kind->setCurrentIndex(static_cast<int>(std::find(std::begin(kKinds), std::end(kKinds), joint.kind) - std::begin(kKinds)));
    m_flip->setChecked(joint.flip);
    m_jointError->setText(joint.error);
    m_jointError->setVisible(!joint.error.isEmpty());
    const int count = degreesOfFreedom(joint.kind);
    for (int v = 0; v < 3; ++v) {
        ValueRow& row = m_values[v];
        const bool shown = v < count;
        for (QWidget* w : std::initializer_list<QWidget*>{row.label, row.slider, row.value, row.limited, row.min, row.max, row.check})
            w->setVisible(shown);
        if (!shown)
            continue;
        const bool angle = isAngle(joint.kind, v);
        row.label->setText(SketchEditor::jointValueLabel(joint.kind, v));
        // The slider spans the limits, or a full turn / ±100 mm.
        const double low = joint.limited[v] ? joint.min[v] : angle ? -180.0 : -100.0;
        const double high = joint.limited[v] ? joint.max[v] : angle ? 180.0 : 100.0;
        if (!row.slider->isSliderDown()) {
            row.slider->setRange(static_cast<int>(std::lround(low / kSliderStep)), static_cast<int>(std::lround(high / kSliderStep)));
            row.slider->setValue(static_cast<int>(std::lround(joint.values[v] / kSliderStep)));
        }
        row.value->setSuffix(angle ? QStringLiteral(" °") : QStringLiteral(" mm"));
        row.value->setValue(joint.values[v]);
        row.limited->setChecked(joint.limited[v]);
        row.min->setEnabled(joint.limited[v]);
        row.max->setEnabled(joint.limited[v]);
        row.min->setValue(joint.min[v]);
        row.max->setValue(joint.max[v]);
    }
}
