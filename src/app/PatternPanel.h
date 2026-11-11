#pragma once

#include <QWidget>

class QCheckBox;
class QDoubleSpinBox;
class QLabel;
class QPushButton;
class QSpinBox;
class SketchEditor;

// The values of a linear or circular pattern, typed: how many, how far apart (or over
// what angle), which way. Follows the editor's pattern being set up (see
// SketchEditor::patternState()); changes are previewed in the model as they are typed.
class PatternPanel : public QWidget
{
    Q_OBJECT

public:
    explicit PatternPanel(SketchEditor* editor, QWidget* parent = nullptr);
    // Takes the editor's state; false when there is no pattern to show.
    bool sync();
    QString title() const;

signals:
    void titleChanged(const QString& title);

protected:
    void changeEvent(QEvent* event) override;

private:
    void retranslate();
    void send(); // the fields to the editor
    void updateStep();

    SketchEditor* m_editor;
    bool m_circular = false;
    bool m_syncing = false;
    QLabel* m_sourceTitle = nullptr;
    QLabel* m_source = nullptr;
    QLabel* m_axisTitle = nullptr;
    QLabel* m_axis = nullptr;
    QLabel* m_countLabel = nullptr;
    QSpinBox* m_count = nullptr;
    QLabel* m_valueLabel = nullptr;
    QDoubleSpinBox* m_value = nullptr;
    QCheckBox* m_full = nullptr;
    QCheckBox* m_reverse = nullptr;
    QLabel* m_step = nullptr;
    QLabel* m_error = nullptr;
    QPushButton* m_accept = nullptr;
    QPushButton* m_cancel = nullptr;
};
