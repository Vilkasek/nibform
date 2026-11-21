#pragma once

#include "model/Expression.h"
#include "sketch/Constraint.h"

#include <map>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

struct Feature;
class Sketch;

// Named values that drive the model. A parameter is an expression (`40`, `width / 2`)
// of a kind: a length (its plain numbers in its own unit), an angle or a plain number.
// Feature values and sketch dimensions can be typed as expressions using them; they
// keep the expression and follow when a parameter changes.
struct Parameter
{
    std::string name;
    Expression expression;
    ValueKind kind = ValueKind::Length;
    std::string comment;
    bool operator==(const Parameter&) const = default;
};

// A named set of parameter values to switch the model between (sizes of one part): the
// expression of each parameter it covers.
struct Configuration
{
    std::string name;
    std::map<std::string, std::string> values;
    bool operator==(const Configuration&) const = default;
};

struct ParameterTable
{
    std::vector<Parameter> parameters;
    std::vector<Configuration> configurations;
    std::string active; // the configuration last switched to; empty: none

    bool empty() const { return parameters.empty() && configurations.empty(); }
    int indexOf(const std::string& name) const; // -1: none
    const Configuration* configuration(const std::string& name) const;
    bool operator==(const ParameterTable&) const = default;
};

// Every parameter evaluated once (in any order, each after the ones it uses), to
// evaluate expressions against.
class ParameterScope
{
public:
    explicit ParameterScope(const ParameterTable& table);

    struct Value
    {
        std::optional<double> value; // mm, degrees or a plain number, by the parameter's kind
        ExpressionError error;       // when there is no value: a bad expression, a cycle, ...
    };
    const std::vector<Value>& values() const { return m_values; } // in table order
    EvalResult evaluate(const Expression& expression, ValueKind kind) const;
    NameLookup lookup() const;

private:
    int indexOf(const std::string& name) const; // -1: none
    const ParameterTable& m_table;
    std::vector<Value> m_values;
    std::unordered_map<std::string, int> m_index; // name → first parameter of that name
};

// --- Changing the table --------------------------------------------------------------------

// Renames a parameter and every use of it: in other parameters, configurations, feature
// values and sketch dimensions. False (with `error`) if the new name isn't valid or taken.
bool renameParameter(ParameterTable& table, std::vector<Feature>& features, std::vector<Sketch*> sketches,
                     const std::string& from, const std::string& to, ExpressionError* error = nullptr);
// Where a parameter is used: names of other parameters using it, and how many feature
// values and dimensions do.
struct ParameterUses
{
    std::vector<std::string> parameters;
    int values = 0;
};
ParameterUses usesOf(const std::string& name, const ParameterTable& table, const std::vector<Feature>& features,
                     const std::vector<const Sketch*>& sketches);

// Sets parameter `name` to a plain value (mm, degrees or a number), adding it when there
// is none (a new length in `unit`). False (with `error`) when the name isn't valid or
// names a parameter of another kind.
bool setParameterValue(ParameterTable& table, const std::string& name, ValueKind kind, double value, LengthUnit unit,
                       ExpressionError* error = nullptr);

// Configurations: the current values saved under `name` (replacing one of that name),
// and switching to one (its values into the parameters). False if there is none so named.
void saveConfiguration(ParameterTable& table, const std::string& name);
bool applyConfiguration(ParameterTable& table, const std::string& name);
// Whether the parameters still hold the active configuration's values.
bool configurationModified(const ParameterTable& table);

// --- Driving the model ---------------------------------------------------------------------

// The values of a feature that can be expressions, by key: "value", "count", "endValue",
// "setback", "startOffset", "hole.diameter", "hole.depth", "hole.headDiameter", "hole.headDepth",
// "hole.thread.length", "thread.diameter", "thread.length", "sheet.thickness", "sheet.radius",
// "sheet.kFactor", "flange.angle", "flange.inset", "flange.gap", "values.<name>" (a script's).
// A thread's diameter (a
// tapped hole's too) snaps to the nearest standard size.
struct FeatureField
{
    ValueKind kind = ValueKind::Length;
    double min = -1e5, max = 1e5; // what the feature can take
    bool whole = false;           // a count
};
std::optional<FeatureField> featureField(const Feature& feature, const std::string& key);
std::optional<double> featureValue(const Feature& feature, const std::string& key);
void setFeatureValue(Feature& feature, const std::string& key, double value);
// A dimension as an expression: its kind, and its value from an expression's (keeping
// the side / turning direction `current` has; radians for angles).
ValueKind dimensionKind(ConstraintKind kind);
double dimensionValue(ConstraintKind kind, double expressed, double current);

// A value that couldn't follow its expression (it keeps its last value).
struct ParameterIssue
{
    int feature = -1;          // index into the features, or
    int sketch = -1;           // a sketch (its id) and
    ConstraintId constraint = kNoId; // dimension
    std::string key;           // the feature's value
    ExpressionError error;
};
struct ApplyResult
{
    std::vector<ParameterIssue> issues;
    bool features = false;     // some feature value changed
    std::vector<int> sketches; // ids of the sketches whose dimensions changed (and were solved again)
};
// Sets every value that has an expression from the parameters; sketches with changed
// dimensions are solved again (a sketch that can't take its new values stays as it was).
ApplyResult applyParameters(const ParameterTable& table, std::vector<Feature>& features,
                            const std::vector<std::pair<int, Sketch*>>& sketches);

// --- CSV -------------------------------------------------------------------------------------

// name, unit (mm, in, deg or empty for a number), expression, comment, then a column per
// configuration.
std::string parametersToCsv(const ParameterTable& table);
// Reads what parametersToCsv() writes (also with `;` between columns, as spreadsheets in
// some languages save); empty with `error` (detail: the line) if it can't.
std::optional<ParameterTable> parametersFromCsv(const std::string& csv, ExpressionError* error = nullptr);
