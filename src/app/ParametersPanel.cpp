#include "app/ParametersPanel.h"

#include "model/Parameters.h"
#include "sketch/SketchEditor.h"
#include "viewer/UnitText.h"

#include <QComboBox>
#include <QEvent>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QMenu>
#include <QMessageBox>
#include <QScopedValueRollback>
#include <QTableWidget>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

#include <cmath>
#include <set>

namespace {

enum Column { Name, Unit, Value, Comment, Columns };

// The unit choices, in the order of the combo box.
int unitIndex(const Parameter& p)
{
    switch (p.kind) {
    case ValueKind::Length:
        return p.expression.unit == LengthUnit::Inch ? 1 : 0;
    case ValueKind::Angle:
        return 2;
    case ValueKind::Number:
        return 3;
    }
    return 0;
}

// The value in the parameter's own unit (the Unit column names it), as it is typed.
QString valueText(double value, const Parameter& p)
{
    const bool inches = p.kind == ValueKind::Length && p.expression.unit == LengthUnit::Inch;
    return QString::fromStdString(numberText(inches ? value / 25.4 : value));
}

QString qs(const std::string& s)
{
    return QString::fromStdString(s);
}

} // namespace

ParametersPanel::ParametersPanel(SketchEditor* editor, QWidget* parent)
    : QWidget(parent)
    , m_editor(editor)
{
    setObjectName(QStringLiteral("parametersPanel"));
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 6, 8, 8);
    layout->setSpacing(6);

    // Configurations: one tap switches the model to one.
    m_configTitle = new QLabel(this);
    layout->addWidget(m_configTitle);
    auto* configLine = new QHBoxLayout;
    configLine->setSpacing(4);
    m_configs = new QWidget(this);
    m_configRow = new QHBoxLayout(m_configs);
    m_configRow->setContentsMargins(0, 0, 0, 0);
    m_configRow->setSpacing(4);
    configLine->addWidget(m_configs);
    m_saveConfig = new QToolButton(this);
    connect(m_saveConfig, &QToolButton::clicked, this, &ParametersPanel::saveConfiguration);
    configLine->addWidget(m_saveConfig);
    configLine->addStretch();
    layout->addLayout(configLine);
    m_modified = new QLabel(this);
    m_modified->setObjectName(QStringLiteral("parametersModified"));
    m_modified->setWordWrap(true);
    layout->addWidget(m_modified);

    m_table = new QTableWidget(0, Columns, this);
    m_table->verticalHeader()->hide();
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setEditTriggers(QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed
                             | QAbstractItemView::AnyKeyPressed);
    m_table->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(Value, QHeaderView::Stretch);
    m_table->horizontalHeader()->setSectionResizeMode(Comment, QHeaderView::Stretch);
    connect(m_table, &QTableWidget::itemChanged, this, [this](QTableWidgetItem* item) {
        cellEdited(item->row(), item->column());
    });
    connect(m_table, &QTableWidget::itemSelectionChanged, this,
            [this] { m_remove->setEnabled(!m_table->selectionModel()->selectedRows().isEmpty()); });
    layout->addWidget(m_table, 1);

    auto* buttons = new QHBoxLayout;
    buttons->setSpacing(4);
    m_add = new QToolButton(this);
    m_add->setText(QStringLiteral("+"));
    connect(m_add, &QToolButton::clicked, this, &ParametersPanel::addParameter);
    m_remove = new QToolButton(this);
    m_remove->setText(QStringLiteral("−"));
    m_remove->setEnabled(false);
    connect(m_remove, &QToolButton::clicked, this, &ParametersPanel::removeSelected);
    m_csv = new QToolButton(this);
    m_csv->setPopupMode(QToolButton::InstantPopup);
    m_csv->setMenu(new QMenu(m_csv));
    buttons->addWidget(m_add);
    buttons->addWidget(m_remove);
    buttons->addStretch();
    buttons->addWidget(m_csv);
    layout->addLayout(buttons);
    m_help = new QLabel(this);
    m_help->setObjectName(QStringLiteral("parametersHelp"));
    m_help->setWordWrap(true);
    layout->addWidget(m_help);

    // After the change that caused it is done (an edit in this table among them).
    auto later = [this] { QTimer::singleShot(0, this, &ParametersPanel::sync); };
    connect(m_editor, &SketchEditor::parametersChanged, this, later);
    connect(m_editor, &SketchEditor::historyChanged, this, later); // undo, a project opened, units
    retranslate();
    sync();
}

void ParametersPanel::retranslate()
{
    m_configTitle->setText(tr("Configurations"));
    m_saveConfig->setText(tr("Save As…"));
    m_saveConfig->setToolTip(tr("Saves the current values as a configuration to switch back to"));
    m_table->setHorizontalHeaderLabels({tr("Name"), tr("Unit"), tr("Value"), tr("Comment")});
    m_add->setToolTip(tr("Add a parameter"));
    m_remove->setToolTip(tr("Delete the selected parameters"));
    m_csv->setText(tr("CSV"));
    QMenu* menu = m_csv->menu();
    menu->clear();
    menu->addAction(tr("Import from CSV…"), this, &ParametersPanel::importCsv);
    menu->addAction(tr("Export to CSV…"), this, &ParametersPanel::exportCsv);
    m_help->setText(tr("Type the values here. In a sketch dimension, use the names (width / 2 + 3) or name the "
                       "dimension (width = 40 makes the parameter); dimensions so are marked ƒ and follow the values."));
}

void ParametersPanel::changeEvent(QEvent* event)
{
    if (event->type() == QEvent::LanguageChange) {
        retranslate();
        sync();
    }
    QWidget::changeEvent(event);
}

void ParametersPanel::sync()
{
    const QScopedValueRollback guard(m_syncing, true);
    const ParameterTable& table = m_editor->parameters();
    const ParameterScope scope(table);
    const int current = m_table->currentRow();
    const int count = static_cast<int>(table.parameters.size());
    m_table->setRowCount(count);
    for (int row = 0; row < count; ++row) {
        const Parameter& p = table.parameters[row];
        auto cell = [this, row](int column, const QString& text, bool editable) {
            QTableWidgetItem* item = m_table->item(row, column);
            if (!item) {
                item = new QTableWidgetItem;
                m_table->setItem(row, column, item);
            }
            item->setText(text);
            item->setFlags(editable ? item->flags() | Qt::ItemIsEditable : item->flags() & ~Qt::ItemIsEditable);
            item->setToolTip(QString());
            item->setForeground(QBrush());
            return item;
        };
        cell(Name, qs(p.name), true);
        cell(Comment, qs(p.comment), true);
        const ParameterScope::Value& v = scope.values()[row];
        if (!v.value) {
            // Only from a file or CSV that holds an expression: typing a value replaces it.
            const QString error = SketchEditor::expressionText(v.error);
            QTableWidgetItem* item = cell(Value, qs(p.expression.text), true);
            item->setForeground(QColor(0xe5, 0x48, 0x4d));
            item->setToolTip(error);
        } else {
            QTableWidgetItem* item = cell(Value, valueText(*v.value, p), true);
            if (!isPlainNumber(p.expression.text))
                item->setToolTip(tr("From %1; typing a value replaces it").arg(qs(p.expression.text)));
        }
        m_table->item(row, Value)->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
        auto* unit = qobject_cast<QComboBox*>(m_table->cellWidget(row, Unit));
        if (!unit) {
            unit = new QComboBox(m_table);
            unit->addItems({QStringLiteral("mm"), QStringLiteral("in"), QStringLiteral("°"), QStringLiteral("–")});
            connect(unit, &QComboBox::activated, this, [this, unit](int index) {
                // The row it is in now (rows come and go).
                for (int r = 0; r < m_table->rowCount(); ++r) {
                    if (m_table->cellWidget(r, Unit) == unit)
                        unitChosen(r, index);
                }
            });
            m_table->setCellWidget(row, Unit, unit);
        }
        unit->setCurrentIndex(unitIndex(p));
        unit->setToolTip(tr("mm or in: a length (plain numbers in it); °: an angle; –: a plain number"));
    }
    if (current >= 0 && current < count)
        m_table->setCurrentCell(current, m_table->currentColumn());
    m_remove->setEnabled(!m_table->selectionModel()->selectedRows().isEmpty());
    syncConfigurations();
}

void ParametersPanel::syncConfigurations()
{
    const ParameterTable& table = m_editor->parameters();
    qDeleteAll(m_configs->findChildren<QToolButton*>(Qt::FindDirectChildrenOnly));
    for (const Configuration& c : table.configurations) {
        auto* button = new QToolButton(m_configs);
        button->setObjectName(QStringLiteral("configuration"));
        button->setText(qs(c.name));
        button->setCheckable(true);
        button->setChecked(c.name == table.active);
        button->setPopupMode(QToolButton::MenuButtonPopup); // its arrow: update, rename, delete
        button->setMenu(new QMenu(button));
        button->setToolTip(tr("Switches the parameters to these values"));
        const std::string name = c.name;
        connect(button, &QToolButton::clicked, this, [this, name, button] {
            ParameterTable t = m_editor->parameters();
            const bool already = t.active == name && !configurationModified(t);
            if (!already && applyConfiguration(t, name))
                m_editor->setParameters(std::move(t));
            button->setChecked(true);
        });
        configurationMenu(name, button);
        m_configRow->addWidget(button);
    }
    m_configs->setVisible(!table.configurations.empty());
    const bool modified = configurationModified(table);
    m_modified->setVisible(modified);
    if (modified)
        m_modified->setText(tr("Changed since %1 was chosen; its menu updates it").arg(qs(table.active)));
}

void ParametersPanel::configurationMenu(const std::string& name, QToolButton* button)
{
    QMenu* menu = button->menu();
    menu->addAction(tr("Update with Current Values"), this, [this, name] {
        ParameterTable t = m_editor->parameters();
        ::saveConfiguration(t, name);
        m_editor->setParameters(std::move(t));
    });
    menu->addAction(tr("Rename…"), this, [this, name] {
        bool ok = false;
        const QString to = QInputDialog::getText(this, tr("Rename Configuration"), tr("Name:"), QLineEdit::Normal,
                                                 qs(name), &ok).trimmed();
        if (!ok || to.isEmpty() || to.toStdString() == name)
            return;
        ParameterTable t = m_editor->parameters();
        if (t.configuration(to.toStdString())) {
            QMessageBox::information(this, tr("Rename Configuration"), tr("There is a configuration %1 already.").arg(to));
            return;
        }
        for (Configuration& c : t.configurations) {
            if (c.name == name)
                c.name = to.toStdString();
        }
        if (t.active == name)
            t.active = to.toStdString();
        m_editor->setParameters(std::move(t));
    });
    menu->addAction(tr("Delete"), this, [this, name] {
        ParameterTable t = m_editor->parameters();
        std::erase_if(t.configurations, [&](const Configuration& c) { return c.name == name; });
        if (t.active == name)
            t.active.clear();
        m_editor->setParameters(std::move(t)); // the values stay as they are
    });
}

void ParametersPanel::cellEdited(int row, int column)
{
    if (m_syncing)
        return;
    ParameterTable table = m_editor->parameters();
    if (row < 0 || row >= static_cast<int>(table.parameters.size()))
        return;
    Parameter& p = table.parameters[row];
    const std::string text = m_table->item(row, column)->text().trimmed().toStdString();
    switch (column) {
    case Name:
        if (!m_editor->renameParameter(p.name, text))
            QTimer::singleShot(0, this, &ParametersPanel::sync); // back to the old name
        return;
    case Value: {
        // A number, as in any value field (a decimal comma too).
        QString number = QString::fromStdString(text);
        number.replace(QLatin1Char(','), QLatin1Char('.'));
        bool ok = false;
        const double value = number.toDouble(&ok);
        if (!ok || !std::isfinite(value)) {
            QMessageBox::information(this, tr("Parameters"), tr("Type a number: %1").arg(QString::fromStdString(text)));
            QTimer::singleShot(0, this, &ParametersPanel::sync);
            return;
        }
        p.expression.text = numberText(value);
        break;
    }
    case Comment:
        p.comment = text;
        break;
    default:
        return;
    }
    m_editor->setParameters(std::move(table));
}

void ParametersPanel::unitChosen(int row, int index)
{
    ParameterTable table = m_editor->parameters();
    if (row < 0 || row >= static_cast<int>(table.parameters.size()))
        return;
    Parameter& p = table.parameters[row];
    const ValueKind kind = index <= 1 ? ValueKind::Length : index == 2 ? ValueKind::Angle : ValueKind::Number;
    const LengthUnit unit = index == 1 ? LengthUnit::Inch : LengthUnit::Millimetre;
    // Between mm and in the length stays the same; the number changes.
    if (p.kind == ValueKind::Length && kind == ValueKind::Length && p.expression.unit != unit
        && isPlainNumber(p.expression.text)) {
        const double value = QString::fromStdString(p.expression.text).replace(QLatin1Char(','), QLatin1Char('.')).toDouble();
        p.expression.text = numberText(unit == LengthUnit::Inch ? value / 25.4 : value * 25.4);
    }
    p.kind = kind;
    p.expression.unit = unit;
    m_editor->setParameters(std::move(table));
}

void ParametersPanel::addParameter()
{
    ParameterTable table = m_editor->parameters();
    std::string name;
    for (int n = 1; name.empty() || table.indexOf(name) >= 0; ++n)
        name = "p" + std::to_string(n);
    Parameter p;
    p.name = name;
    p.kind = ValueKind::Length;
    p.expression = {lengthUnit() == LengthUnit::Inch ? "1" : "10", lengthUnit()};
    table.parameters.push_back(p);
    const int row = static_cast<int>(table.parameters.size()) - 1;
    m_editor->setParameters(std::move(table));
    // Once the table shows it (it syncs on the next pass), its name is typed first.
    QTimer::singleShot(0, this, [this, row] {
        sync();
        if (QTableWidgetItem* item = m_table->item(row, Name)) { // then its value
            m_table->setCurrentItem(item);
            m_table->editItem(item);
        }
    });
}

void ParametersPanel::removeSelected()
{
    ParameterTable table = m_editor->parameters();
    std::set<std::string> names;
    for (const QModelIndex& index : m_table->selectionModel()->selectedRows()) {
        if (index.row() < static_cast<int>(table.parameters.size()))
            names.insert(table.parameters[index.row()].name);
    }
    if (names.empty())
        return;
    QStringList used;
    for (const std::string& name : names) {
        const ParameterUses uses = m_editor->parameterUses(name);
        QStringList by;
        for (const std::string& p : uses.parameters) {
            if (!names.count(p))
                by << qs(p);
        }
        if (uses.values > 0)
            by << tr("values: %1").arg(uses.values);
        if (!by.isEmpty())
            used << QStringLiteral("%1 (%2)").arg(qs(name), by.join(QStringLiteral(", ")));
    }
    if (!used.isEmpty()) {
        const auto answer = QMessageBox::question(
            this, tr("Delete Parameters"),
            tr("Still used: %1.\n\nWhat uses them keeps its last value and shows an error until it is changed. "
               "Delete anyway?")
                .arg(used.join(QStringLiteral("; "))));
        if (answer != QMessageBox::Yes)
            return;
    }
    std::erase_if(table.parameters, [&](const Parameter& p) { return names.count(p.name) > 0; });
    for (Configuration& c : table.configurations) {
        for (const std::string& name : names)
            c.values.erase(name);
    }
    m_editor->setParameters(std::move(table));
}

void ParametersPanel::saveConfiguration()
{
    const ParameterTable& current = m_editor->parameters();
    QString suggestion = qs(current.active);
    if (suggestion.isEmpty())
        suggestion = tr("Configuration %1").arg(current.configurations.size() + 1);
    bool ok = false;
    const QString name = QInputDialog::getText(this, tr("Save Configuration"),
                                               tr("Name of this set of values (e.g. Small, Large):"),
                                               QLineEdit::Normal, suggestion, &ok)
                             .trimmed();
    if (!ok || name.isEmpty())
        return;
    if (current.configuration(name.toStdString()) && name.toStdString() != current.active) {
        const auto answer = QMessageBox::question(this, tr("Save Configuration"),
                                                  tr("Replace the values of %1 with the current ones?").arg(name));
        if (answer != QMessageBox::Yes)
            return;
    }
    ParameterTable table = current;
    ::saveConfiguration(table, name.toStdString());
    m_editor->setParameters(std::move(table));
}

void ParametersPanel::importCsv()
{
    const QString path = QFileDialog::getOpenFileName(this, tr("Import Parameters"), QString(),
                                                      tr("CSV files (*.csv);;All files (*)"));
    if (path.isEmpty())
        return;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        QMessageBox::warning(this, tr("Import Parameters"), tr("Can't read %1: %2").arg(path, file.errorString()));
        return;
    }
    ExpressionError error;
    const auto table = parametersFromCsv(file.readAll().toStdString(), &error);
    if (!table) {
        QMessageBox::warning(this, tr("Import Parameters"),
                             tr("Can't import %1: %2").arg(QFileInfo(path).fileName(), SketchEditor::expressionText(error)));
        return;
    }
    if (!m_editor->parameters().empty()) {
        const auto answer = QMessageBox::question(
            this, tr("Import Parameters"),
            tr("Replace the parameters and configurations with those from the file?"));
        if (answer != QMessageBox::Yes)
            return;
    }
    m_editor->setParameters(*table);
}

void ParametersPanel::exportCsv()
{
    const QString path = QFileDialog::getSaveFileName(this, tr("Export Parameters"), tr("parameters.csv"),
                                                      tr("CSV files (*.csv)"));
    if (path.isEmpty())
        return;
    QFile file(path);
    const QByteArray csv = QByteArray::fromStdString(parametersToCsv(m_editor->parameters()));
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate) || file.write(csv) != csv.size())
        QMessageBox::warning(this, tr("Export Parameters"), tr("Can't save %1: %2").arg(path, file.errorString()));
}
