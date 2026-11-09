#include "app/ShortcutsDialog.h"

#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QKeySequenceEdit>
#include <QLineEdit>
#include <QPushButton>
#include <QToolButton>
#include <QTreeWidget>
#include <QVBoxLayout>

ShortcutsDialog::ShortcutsDialog(std::vector<Entry> entries, QWidget* parent)
    : QDialog(parent)
    , m_entries(std::move(entries))
{
    setWindowTitle(tr("Keyboard Shortcuts"));
    resize(640, 640);
    auto* layout = new QVBoxLayout(this);
    m_search = new QLineEdit(this);
    m_search->setPlaceholderText(tr("Search actions or keys…"));
    m_search->setClearButtonEnabled(true);
    layout->addWidget(m_search);

    m_tree = new QTreeWidget(this);
    m_tree->setColumnCount(3);
    m_tree->setHeaderLabels({tr("Action"), tr("Shortcut"), QString()});
    m_tree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_tree->header()->setSectionResizeMode(1, QHeaderView::Fixed);
    m_tree->header()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    m_tree->header()->resizeSection(1, 200);
    m_tree->setSelectionMode(QAbstractItemView::NoSelection);
    m_tree->setRootIsDecorated(false);
    layout->addWidget(m_tree, 1);

    std::map<QString, QTreeWidgetItem*> groups;
    for (const Entry& e : m_entries) {
        QTreeWidgetItem*& group = groups[e.group];
        if (!group) {
            group = new QTreeWidgetItem(m_tree, {e.group});
            QFont bold = group->font(0);
            bold.setBold(true);
            group->setFont(0, bold);
            group->setFirstColumnSpanned(true);
            group->setExpanded(true);
        }
        auto* item = new QTreeWidgetItem(group, {e.name});
        auto* edit = new QKeySequenceEdit(e.keys, m_tree);
        edit->setMaximumSequenceLength(1);
        edit->setClearButtonEnabled(true);
        auto* reset = new QToolButton(m_tree);
        reset->setText(tr("Default"));
        reset->setToolTip(tr("Back to %1").arg(e.defaultKeys.isEmpty() ? tr("none")
                                                                        : e.defaultKeys.toString(QKeySequence::NativeText)));
        connect(reset, &QToolButton::clicked, edit, [edit, keys = e.defaultKeys] { edit->setKeySequence(keys); });
        connect(edit, &QKeySequenceEdit::keySequenceChanged, this, &ShortcutsDialog::markConflicts);
        m_tree->setItemWidget(item, 1, edit);
        m_tree->setItemWidget(item, 2, reset);
        m_edits.push_back(edit);
        m_items.push_back(item);
    }
    connect(m_search, &QLineEdit::textChanged, this, &ShortcutsDialog::filter);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    QPushButton* all = buttons->addButton(tr("Reset All"), QDialogButtonBox::ResetRole);
    connect(all, &QPushButton::clicked, this, [this] {
        for (std::size_t i = 0; i < m_edits.size(); ++i)
            m_edits[i]->setKeySequence(m_entries[i].defaultKeys);
    });
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);
    markConflicts();
}

std::map<QString, QKeySequence> ShortcutsDialog::result() const
{
    std::map<QString, QKeySequence> out;
    for (std::size_t i = 0; i < m_entries.size(); ++i)
        out[m_entries[i].id] = m_edits[i]->keySequence();
    return out;
}

void ShortcutsDialog::filter(const QString& text)
{
    const QString needle = text.trimmed();
    for (std::size_t i = 0; i < m_items.size(); ++i) {
        const QString keys = m_edits[i]->keySequence().toString(QKeySequence::NativeText);
        const bool match = needle.isEmpty() || m_entries[i].name.contains(needle, Qt::CaseInsensitive)
                        || keys.contains(needle, Qt::CaseInsensitive)
                        || m_entries[i].group.contains(needle, Qt::CaseInsensitive);
        m_items[i]->setHidden(!match);
    }
    for (int g = 0; g < m_tree->topLevelItemCount(); ++g) {
        QTreeWidgetItem* group = m_tree->topLevelItem(g);
        bool any = false;
        for (int c = 0; c < group->childCount(); ++c)
            any |= !group->child(c)->isHidden();
        group->setHidden(!any);
    }
}

void ShortcutsDialog::markConflicts()
{
    // Keys that two actions would answer at the same time: then neither does. Actions of
    // different modes (a sketch tool, a model tool) may share keys.
    auto together = [](const QString& a, const QString& b) {
        return a == b || a == QLatin1String("global") || b == QLatin1String("global");
    };
    for (std::size_t i = 0; i < m_entries.size(); ++i) {
        const QKeySequence keys = m_edits[i]->keySequence();
        QString other;
        for (std::size_t j = 0; j < m_entries.size() && !keys.isEmpty(); ++j) {
            if (j != i && m_edits[j]->keySequence() == keys && together(m_entries[i].context, m_entries[j].context)) {
                other = m_entries[j].name;
                break;
            }
        }
        m_items[i]->setForeground(0, other.isEmpty() ? QBrush() : QBrush(QColor(220, 38, 38)));
        m_items[i]->setToolTip(0, other.isEmpty() ? QString() : tr("Also the keys of: %1").arg(other));
    }
}
