// Parameters: the table kept with the project, and the values typed as expressions
// following it.

#include "sketch/SketchEditor.h"

#include "model/Profiles.h"
#include "model/Tr.h"
#include "viewer/OcctViewerWidget.h"

#include <QCoreApplication>

#include <cmath>

QString SketchEditor::expressionText(const ExpressionError& error)
{
    if (error.message.empty())
        return {};
    QString text = QCoreApplication::translate("Model", error.message.c_str());
    return text.contains(QStringLiteral("%1")) ? text.arg(QString::fromStdString(error.detail)) : text;
}

void SketchEditor::setParameters(ParameterTable table)
{
    if (table == m_parameters)
        return;
    pushUndo();
    m_parameters = std::move(table);
    followParameters();
    emit parametersChanged();
    emit historyChanged();
}

bool SketchEditor::renameParameter(const std::string& from, const std::string& to)
{
    if (from == to || m_parameters.indexOf(from) < 0)
        return from == to;
    ExpressionError error;
    if (!isValidName(to, &error)) {
        emit message(expressionText(error));
        return false;
    }
    if (m_parameters.indexOf(to) >= 0) {
        emit message(expressionText({QT_TRANSLATE_NOOP("Model", "%1 is already taken"), to}));
        return false;
    }
    pushUndo();
    std::vector<Sketch*> sketches;
    for (Slot& slot : m_slots)
        sketches.push_back(&slot.sketch);
    ::renameParameter(m_parameters, m_features, sketches, from, to);
    // Values stay as they are; only the names in the expressions changed. Uses of the new
    // name that were broken before now work, though.
    followParameters();
    emit parametersChanged();
    emit historyChanged();
    return true;
}

ParameterUses SketchEditor::parameterUses(const std::string& name) const
{
    std::vector<const Sketch*> sketches;
    for (const Slot& slot : m_slots)
        sketches.push_back(&slot.sketch);
    return usesOf(name, m_parameters, m_features, sketches);
}

void SketchEditor::followParameters()
{
    std::vector<std::pair<int, Sketch*>> sketches;
    for (Slot& slot : m_slots)
        sketches.emplace_back(slot.id, &slot.sketch);
    const ApplyResult result = applyParameters(m_parameters, m_features, sketches);
    m_parameterIssues = result.issues;
    for (int id : result.sketches) {
        for (Slot& slot : m_slots) {
            if (slot.id == id)
                slot.regions = findRegions(slot.sketch);
        }
    }
    if (!result.sketches.empty()) {
        sketchChanged(); // the open one's degrees of freedom and regions
        refreshSketches();
        showRegions();
    }
    // A feature value, or a sketch it is built from: the model again (from the first
    // feature that changed, the rest comes from the cache).
    rebuildModel();
    for (const ParameterIssue& issue : m_parameterIssues) {
        if (issue.feature >= 0 || issue.sketch >= 0) {
            emit message(tr("Some values can't follow the parameters: see the history"));
            break;
        }
    }
    m_viewer->update();
}

EvalResult SketchEditor::evaluateTyped(const std::string& text, ValueKind kind, LengthUnit unit, bool definitions) const
{
    const ParameterScope scope(m_parameters);
    const auto definition = definitionIn(text);
    if (!definition)
        return scope.evaluate({text, unit}, kind);
    if (!definitions)
        return {std::nullopt, {QT_TRANSLATE_NOOP("Model", "Parameters are named in sketch dimensions or the Parameters panel"), {}}};
    EvalResult r = scope.evaluate({definition->expression, unit}, kind);
    ParameterTable probe = m_parameters;
    if (r.value && !setParameterValue(probe, definition->name, kind, *r.value, unit, &r.error))
        r.value.reset(); // a name that can't be one, or a parameter of another kind
    return r;
}

Expression SketchEditor::dimensionBinding(const Expression& typed)
{
    if (const auto definition = definitionIn(typed.text))
        return {definition->name, typed.unit};
    return typed;
}

void SketchEditor::defineFromDimension(const Expression& typed, const SketchConstraint& dimension)
{
    const auto definition = definitionIn(typed.text);
    if (!definition)
        return;
    const ValueKind kind = dimensionKind(dimension.kind);
    const double value = kind == ValueKind::Angle ? std::abs(dimension.value) * 180.0 / M_PI : std::abs(dimension.value);
    ParameterTable table = m_parameters;
    if (!setParameterValue(table, definition->name, kind, value, typed.unit) || table == m_parameters)
        return;
    m_parameters = std::move(table);
    followParameters(); // other values using it
    emit parametersChanged();
    emit message(tr("Parameter %1 = %2").arg(QString::fromStdString(definition->name),
                                            QString::fromStdString(m_parameters.parameters[m_parameters.indexOf(definition->name)]
                                                                       .expression.text)));
}
