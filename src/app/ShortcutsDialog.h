#pragma once

#include <QDialog>
#include <QKeySequence>
#include <QList>
#include <QString>

#include <map>
#include <vector>

class QLineEdit;
class QTreeWidget;
class QTreeWidgetItem;
class QKeySequenceEdit;

// Keyboard shortcuts, grouped, with a search box: click a field and press the new keys,
// clear it to have none, or put one (or all) back to the default. Shortcuts of different
// modes may share keys (a sketch tool and a model tool); the same keys twice in one group
// are marked.
class ShortcutsDialog : public QDialog
{
    Q_OBJECT

public:
    struct Entry
    {
        QString id;
        QString name;
        QString group;
        QKeySequence defaultKeys;
        QKeySequence keys;
    };

    ShortcutsDialog(std::vector<Entry> entries, QWidget* parent = nullptr);
    // id → the keys chosen (an empty sequence: none), for every entry.
    std::map<QString, QKeySequence> result() const;

private:
    void filter(const QString& text);
    void markConflicts();

    std::vector<Entry> m_entries;
    std::vector<QKeySequenceEdit*> m_edits;
    std::vector<QTreeWidgetItem*> m_items;
    QTreeWidget* m_tree;
    QLineEdit* m_search;
};
