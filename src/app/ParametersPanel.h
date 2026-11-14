#pragma once

#include <QWidget>

#include <string>

class QHBoxLayout;
class QLabel;
class QTableWidget;
class QToolButton;
class SketchEditor;

// The project's parameters: a table of names, units, expressions, values and comments,
// and the configurations above it (a tap switches the model to one). Every change is a
// step of the editor's undo; values typed as expressions follow.
class ParametersPanel : public QWidget
{
    Q_OBJECT

public:
    explicit ParametersPanel(SketchEditor* editor, QWidget* parent = nullptr);
    // Shows the editor's table (after undo, opening a project, a change from here).
    void sync();
    // Adds a parameter and starts typing its name.
    void addParameter();

protected:
    void changeEvent(QEvent* event) override;

private:
    void retranslate();
    void cellEdited(int row, int column);
    void unitChosen(int row, int index);
    void removeSelected();
    void saveConfiguration();
    void configurationMenu(const std::string& name, QToolButton* button);
    void importCsv();
    void exportCsv();
    void syncConfigurations();

    SketchEditor* m_editor;
    bool m_syncing = false;
    QLabel* m_configTitle = nullptr;
    QWidget* m_configs = nullptr; // a button per configuration
    QHBoxLayout* m_configRow = nullptr;
    QToolButton* m_saveConfig = nullptr;
    QLabel* m_modified = nullptr;
    QTableWidget* m_table = nullptr;
    QToolButton* m_add = nullptr;
    QToolButton* m_remove = nullptr;
    QToolButton* m_csv = nullptr;
    QLabel* m_help = nullptr;
};
