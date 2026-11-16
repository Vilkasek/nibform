#pragma once

#include <QWidget>

#include <array>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QGridLayout;
class QLabel;
class QPushButton;
class QSlider;
class QToolButton;
class QTreeWidget;
class SketchEditor;

// The assembly's parts and joints (the assembly mode's panel): parts shown or hidden,
// grounded, removed; a joint's kind, which way it faces, its values (sliders the model
// follows live), limits, and a check of its range for collisions. Every change is a step
// of the editor's undo.
class AssemblyPanel : public QWidget
{
    Q_OBJECT

public:
    explicit AssemblyPanel(SketchEditor* editor, QWidget* parent = nullptr);
    void sync();

protected:
    void changeEvent(QEvent* event) override;

private:
    void retranslate();
    void syncJoint();
    int currentJoint() const;

    struct ValueRow
    {
        QLabel* label = nullptr;
        QSlider* slider = nullptr;
        QDoubleSpinBox* value = nullptr;
        QCheckBox* limited = nullptr;
        QDoubleSpinBox* min = nullptr;
        QDoubleSpinBox* max = nullptr;
        QToolButton* check = nullptr;
    };

    SketchEditor* m_editor;
    bool m_syncing = false;
    QLabel* m_partsTitle = nullptr;
    QTreeWidget* m_parts = nullptr;
    QToolButton* m_removePart = nullptr;
    QLabel* m_jointsTitle = nullptr;
    QTreeWidget* m_joints = nullptr;
    QWidget* m_jointBox = nullptr;
    QComboBox* m_kind = nullptr;
    QCheckBox* m_flip = nullptr;
    QLabel* m_jointError = nullptr;
    std::array<ValueRow, 3> m_values;
    QToolButton* m_removeJoint = nullptr;
    QLabel* m_help = nullptr;
};
