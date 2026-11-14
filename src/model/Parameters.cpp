#include "model/Parameters.h"

#include "model/Features.h"
#include "model/Tr.h"
#include "sketch/Sketch.h"
#include "sketch/Solver.h"

#include <algorithm>
#include <cmath>

namespace {

Quantity quantityOf(double value, ValueKind kind)
{
    return {value, kind == ValueKind::Length ? 1 : 0, kind == ValueKind::Angle ? 1 : 0};
}

} // namespace

int ParameterTable::indexOf(const std::string& name) const
{
    for (std::size_t i = 0; i < parameters.size(); ++i) {
        if (parameters[i].name == name)
            return static_cast<int>(i);
    }
    return -1;
}

const Configuration* ParameterTable::configuration(const std::string& name) const
{
    for (const Configuration& c : configurations) {
        if (c.name == name)
            return &c;
    }
    return nullptr;
}

// --- Evaluating -------------------------------------------------------------------------------

ParameterScope::ParameterScope(const ParameterTable& table)
    : m_table(table)
    , m_values(table.parameters.size())
{
    for (std::size_t i = 0; i < table.parameters.size(); ++i)
        m_index.emplace(table.parameters[i].name, static_cast<int>(i));
    // Each parameter after the ones it uses (Kahn's algorithm, no recursion, so a long
    // chain is fine); what is left over sits on a cycle or depends on one.
    const std::size_t n = table.parameters.size();
    std::vector<std::vector<int>> deps(n), users(n);
    std::vector<int> waiting(n, 0);
    for (std::size_t i = 0; i < n; ++i) {
        for (const std::string& name : namesIn(table.parameters[i].expression.text)) {
            const int j = indexOf(name);
            if (j < 0)
                continue; // reported when evaluated
            deps[i].push_back(j);
            users[j].push_back(static_cast<int>(i));
            ++waiting[i];
        }
    }
    std::vector<int> ready, done(n, 0);
    for (std::size_t i = 0; i < n; ++i) {
        if (waiting[i] == 0)
            ready.push_back(static_cast<int>(i));
    }
    const NameLookup names = lookup();
    while (!ready.empty()) {
        const int i = ready.back();
        ready.pop_back();
        const Parameter& p = table.parameters[i];
        Value& v = m_values[i];
        if (const auto q = evaluateQuantity(p.expression.text, p.expression.unit, names, v.error))
            v.value = asKind(*q, p.kind, p.expression.unit, v.error);
        done[i] = 1;
        for (int u : users[i]) {
            if (--waiting[u] == 0)
                ready.push_back(u);
        }
    }
    for (std::size_t i = 0; i < n; ++i) {
        if (done[i])
            continue;
        const auto open = std::find_if(deps[i].begin(), deps[i].end(), [&done](int j) { return !done[j]; });
        m_values[i].error = {QT_TRANSLATE_NOOP("Model", "Circular reference through %1"),
                             open != deps[i].end() ? table.parameters[*open].name : table.parameters[i].name};
    }
}

int ParameterScope::indexOf(const std::string& name) const
{
    const auto it = m_index.find(name);
    return it == m_index.end() ? -1 : it->second;
}

NameLookup ParameterScope::lookup() const
{
    return [this](const std::string& name, ExpressionError& error) -> std::optional<Quantity> {
        const int j = indexOf(name);
        if (j < 0)
            return std::nullopt;
        if (!m_values[j].value) {
            error = {QT_TRANSLATE_NOOP("Model", "%1 has an error"), name};
            return std::nullopt;
        }
        return quantityOf(*m_values[j].value, m_table.parameters[j].kind);
    };
}

EvalResult ParameterScope::evaluate(const Expression& expression, ValueKind kind) const
{
    return ::evaluate(expression.text, kind, expression.unit, lookup());
}

// --- Changing the table -----------------------------------------------------------------------

bool renameParameter(ParameterTable& table, std::vector<Feature>& features, std::vector<Sketch*> sketches,
                     const std::string& from, const std::string& to, ExpressionError* error)
{
    const int index = table.indexOf(from);
    if (index < 0)
        return false;
    if (from == to)
        return true;
    if (!isValidName(to, error))
        return false;
    if (table.indexOf(to) >= 0) {
        if (error)
            *error = {QT_TRANSLATE_NOOP("Model", "%1 is already taken"), to};
        return false;
    }
    table.parameters[index].name = to;
    for (Parameter& p : table.parameters)
        p.expression.text = renameInExpression(p.expression.text, from, to);
    for (Configuration& c : table.configurations) {
        std::map<std::string, std::string> values;
        for (auto& [name, text] : c.values)
            values[name == from ? to : name] = renameInExpression(text, from, to);
        c.values = std::move(values);
    }
    for (Feature& f : features) {
        for (auto& [key, e] : f.expressions)
            e.text = renameInExpression(e.text, from, to);
    }
    for (Sketch* s : sketches) {
        for (const SketchConstraint& c : s->constraints()) {
            if (!c.expression.empty()) {
                SketchConstraint* m = s->constraint(c.id);
                m->expression.text = renameInExpression(m->expression.text, from, to);
            }
        }
    }
    return true;
}

ParameterUses usesOf(const std::string& name, const ParameterTable& table, const std::vector<Feature>& features,
                     const std::vector<const Sketch*>& sketches)
{
    auto uses = [&name](const std::string& text) {
        const std::vector<std::string> names = namesIn(text);
        return std::find(names.begin(), names.end(), name) != names.end();
    };
    ParameterUses result;
    for (const Parameter& p : table.parameters) {
        if (p.name != name && uses(p.expression.text))
            result.parameters.push_back(p.name);
    }
    for (const Feature& f : features) {
        for (const auto& [key, e] : f.expressions)
            result.values += uses(e.text) ? 1 : 0;
    }
    for (const Sketch* s : sketches) {
        for (const SketchConstraint& c : s->constraints())
            result.values += !c.expression.empty() && uses(c.expression.text) ? 1 : 0;
    }
    return result;
}

bool setParameterValue(ParameterTable& table, const std::string& name, ValueKind kind, double value, LengthUnit unit,
                       ExpressionError* error)
{
    if (!isValidName(name, error))
        return false;
    int i = table.indexOf(name);
    if (i < 0) {
        Parameter p;
        p.name = name;
        p.kind = kind;
        p.expression.unit = unit;
        table.parameters.push_back(p);
        i = static_cast<int>(table.parameters.size()) - 1;
    } else if (table.parameters[i].kind != kind) {
        if (error) {
            *error = {table.parameters[i].kind == ValueKind::Length  ? QT_TRANSLATE_NOOP("Model", "%1 is a length")
                    : table.parameters[i].kind == ValueKind::Angle   ? QT_TRANSLATE_NOOP("Model", "%1 is an angle")
                                                                      : QT_TRANSLATE_NOOP("Model", "%1 is a plain number"),
                      name};
        }
        return false;
    }
    Parameter& p = table.parameters[i];
    const bool inches = p.kind == ValueKind::Length && p.expression.unit == LengthUnit::Inch;
    p.expression.text = numberText(inches ? value / 25.4 : value);
    return true;
}

void saveConfiguration(ParameterTable& table, const std::string& name)
{
    Configuration saved{name, {}};
    for (const Parameter& p : table.parameters)
        saved.values[p.name] = p.expression.text;
    auto it = std::find_if(table.configurations.begin(), table.configurations.end(),
                           [&](const Configuration& c) { return c.name == name; });
    if (it != table.configurations.end())
        *it = std::move(saved);
    else
        table.configurations.push_back(std::move(saved));
    table.active = name;
}

bool applyConfiguration(ParameterTable& table, const std::string& name)
{
    const Configuration* c = table.configuration(name);
    if (!c)
        return false;
    for (const auto& [parameter, text] : c->values) {
        const int i = table.indexOf(parameter);
        if (i >= 0)
            table.parameters[i].expression.text = text;
    }
    table.active = name;
    return true;
}

bool configurationModified(const ParameterTable& table)
{
    const Configuration* c = table.configuration(table.active);
    if (!c)
        return false;
    for (const auto& [parameter, text] : c->values) {
        const int i = table.indexOf(parameter);
        if (i >= 0 && table.parameters[i].expression.text != text)
            return true;
    }
    return false;
}

// --- Driving the model --------------------------------------------------------------------------

std::optional<FeatureField> featureField(const Feature& f, const std::string& key)
{
    using K = Feature::Kind;
    const FeatureField length{ValueKind::Length, -1e5, 1e5, false};
    const FeatureField positive{ValueKind::Length, 1e-3, 1e5, false};
    const FeatureField turn{ValueKind::Angle, -360.0, 360.0, false};
    if (key == "value") {
        switch (f.kind) {
        case K::Extrude:
            return length;
        case K::Fillet:
        case K::Chamfer:
        case K::Shell:
            return positive;
        case K::Revolve:
            return turn;
        case K::Pattern:
            return f.pattern == Feature::PatternKind::Circular ? FeatureField{ValueKind::Angle, 1.0, 360.0, false} : length;
        case K::Datum:
            if (f.datum == Feature::DatumKind::OffsetPlane)
                return length;
            if (f.datum == Feature::DatumKind::AnglePlane)
                return turn;
            return std::nullopt;
        default:
            return std::nullopt;
        }
    }
    if (key == "count" && f.kind == K::Pattern)
        return FeatureField{ValueKind::Number, 2, 500, true};
    if (f.kind == K::Hole && (key == "hole.diameter" || key == "hole.depth" || key == "hole.headDiameter"
                              || key == "hole.headDepth"))
        return FeatureField{ValueKind::Length, 0.01, 1e5, false};
    return std::nullopt;
}

std::optional<double> featureValue(const Feature& f, const std::string& key)
{
    if (!featureField(f, key))
        return std::nullopt;
    if (key == "value")
        return f.value;
    if (key == "count")
        return f.count;
    if (key == "hole.diameter")
        return f.hole.diameter;
    if (key == "hole.depth")
        return f.hole.depth;
    if (key == "hole.headDiameter")
        return f.hole.headDiameter;
    return f.hole.headDepth;
}

void setFeatureValue(Feature& f, const std::string& key, double value)
{
    if (key == "value")
        f.value = value;
    else if (key == "count")
        f.count = static_cast<int>(std::lround(value));
    else if (key == "hole.diameter")
        f.hole.diameter = value;
    else if (key == "hole.depth")
        f.hole.depth = value;
    else if (key == "hole.headDiameter")
        f.hole.headDiameter = value;
    else if (key == "hole.headDepth")
        f.hole.headDepth = value;
}

ValueKind dimensionKind(ConstraintKind kind)
{
    return kind == ConstraintKind::Angle ? ValueKind::Angle : ValueKind::Length;
}

double dimensionValue(ConstraintKind kind, double expressed, double current)
{
    const double sign = current < 0.0 ? -1.0 : 1.0;
    return sign * (kind == ConstraintKind::Angle ? expressed * M_PI / 180.0 : expressed);
}

namespace {

std::string range(double min, double max)
{
    return numberText(min) + " … " + numberText(max);
}

} // namespace

ApplyResult applyParameters(const ParameterTable& table, std::vector<Feature>& features,
                            const std::vector<std::pair<int, Sketch*>>& sketches)
{
    ApplyResult result;
    const ParameterScope scope(table);
    for (std::size_t i = 0; i < features.size(); ++i) {
        Feature& f = features[i];
        for (const auto& [key, e] : f.expressions) {
            ParameterIssue issue;
            issue.feature = static_cast<int>(i);
            issue.key = key;
            const auto field = featureField(f, key);
            if (!field) {
                issue.error = {QT_TRANSLATE_NOOP("Model", "This value can't be an expression"), {}};
                result.issues.push_back(issue);
                continue;
            }
            const EvalResult r = scope.evaluate(e, field->kind);
            if (!r.value) {
                issue.error = r.error;
                result.issues.push_back(issue);
                continue;
            }
            double v = *r.value;
            if (field->whole) {
                if (std::abs(v - std::round(v)) > 1e-9) {
                    issue.error = {QT_TRANSLATE_NOOP("Model", "Expected a whole number"), {}};
                    result.issues.push_back(issue);
                    continue;
                }
                v = std::round(v);
            }
            if (v < field->min - 1e-9 || v > field->max + 1e-9) {
                issue.error = {QT_TRANSLATE_NOOP("Model", "Out of range: %1"), range(field->min, field->max)};
                result.issues.push_back(issue);
                continue;
            }
            if (featureValue(f, key) != v) {
                setFeatureValue(f, key, v);
                result.features = true;
            }
        }
    }
    for (const auto& [id, sketch] : sketches) {
        Sketch candidate = *sketch;
        std::vector<ConstraintId> changed;
        for (const SketchConstraint& c : sketch->constraints()) {
            if (c.expression.empty() || !isDimension(c.kind))
                continue;
            ParameterIssue issue;
            issue.sketch = id;
            issue.constraint = c.id;
            const ValueKind kind = dimensionKind(c.kind);
            const EvalResult r = scope.evaluate(c.expression, kind);
            if (!r.value) {
                issue.error = r.error;
                result.issues.push_back(issue);
                continue;
            }
            const double max = kind == ValueKind::Angle ? 180.0 : 1e6;
            if (*r.value < 1e-3 || *r.value > max) {
                issue.error = {QT_TRANSLATE_NOOP("Model", "Out of range: %1"), range(1e-3, max)};
                result.issues.push_back(issue);
                continue;
            }
            const double v = dimensionValue(c.kind, *r.value, c.value);
            if (std::abs(v - c.value) > 1e-12) {
                candidate.constraint(c.id)->value = v;
                changed.push_back(c.id);
            }
        }
        if (changed.empty())
            continue;
        if (!solve(candidate, {}, false).ok) {
            for (ConstraintId c : changed) {
                ParameterIssue issue;
                issue.sketch = id;
                issue.constraint = c;
                issue.error = {QT_TRANSLATE_NOOP("Model", "The sketch can't take this value"), {}};
                result.issues.push_back(issue);
            }
            continue;
        }
        *sketch = std::move(candidate);
        result.sketches.push_back(id);
    }
    return result;
}

// --- CSV ---------------------------------------------------------------------------------------

namespace {

std::string csvField(const std::string& s)
{
    const bool quote = s.find_first_of(",;\"\n\r") != std::string::npos || (!s.empty() && (s.front() == ' ' || s.back() == ' '));
    if (!quote)
        return s;
    std::string out = "\"";
    for (char c : s) {
        if (c == '"')
            out += '"';
        out += c;
    }
    return out + '"';
}

// Rows of fields; quoted fields may hold separators, quotes ("") and line breaks.
std::vector<std::vector<std::string>> csvRows(const std::string& text, char separator, bool& closed)
{
    std::vector<std::vector<std::string>> rows;
    std::vector<std::string> row;
    std::string field;
    bool quoted = false;
    for (std::size_t i = 0; i < text.size(); ++i) {
        const char c = text[i];
        if (quoted) {
            if (c == '"' && i + 1 < text.size() && text[i + 1] == '"') {
                field += '"';
                ++i;
            } else if (c == '"') {
                quoted = false;
            } else {
                field += c;
            }
        } else if (c == '"') {
            quoted = true;
        } else if (c == separator) {
            row.push_back(std::move(field));
            field.clear();
        } else if (c == '\n' || c == '\r') {
            if (c == '\r' && i + 1 < text.size() && text[i + 1] == '\n')
                ++i;
            row.push_back(std::move(field));
            field.clear();
            rows.push_back(std::move(row));
            row.clear();
        } else {
            field += c;
        }
    }
    if (!field.empty() || !row.empty()) {
        row.push_back(std::move(field));
        rows.push_back(std::move(row));
    }
    closed = !quoted;
    return rows;
}

std::string trimmed(const std::string& s)
{
    const auto a = s.find_first_not_of(" \t");
    if (a == std::string::npos)
        return {};
    return s.substr(a, s.find_last_not_of(" \t") - a + 1);
}

std::string lower(std::string s)
{
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

} // namespace

std::string parametersToCsv(const ParameterTable& table)
{
    std::string out = "name,unit,expression,comment";
    for (const Configuration& c : table.configurations)
        out += ',' + csvField(c.name);
    out += '\n';
    for (const Parameter& p : table.parameters) {
        const char* unit = p.kind == ValueKind::Angle    ? "deg"
                         : p.kind == ValueKind::Number   ? ""
                         : p.expression.unit == LengthUnit::Inch ? "in"
                                                                 : "mm";
        out += csvField(p.name) + ',' + unit + ',' + csvField(p.expression.text) + ',' + csvField(p.comment);
        for (const Configuration& c : table.configurations) {
            const auto it = c.values.find(p.name);
            out += ',' + csvField(it != c.values.end() ? it->second : std::string());
        }
        out += '\n';
    }
    return out;
}

std::optional<ParameterTable> parametersFromCsv(const std::string& csvText, ExpressionError* error)
{
    std::string csv = csvText;
    if (csv.rfind("\xEF\xBB\xBF", 0) == 0)
        csv.erase(0, 3); // a byte order mark, as spreadsheets write
    const std::string header = csv.substr(0, csv.find_first_of("\r\n"));
    const char separator = header.find(',') == std::string::npos && header.find(';') != std::string::npos ? ';' : ',';
    bool closed = true;
    const auto rows = csvRows(csv, separator, closed);
    auto fail = [error](const char* message, std::size_t line) -> std::optional<ParameterTable> {
        if (error)
            *error = {message, std::to_string(line)};
        return std::nullopt;
    };
    if (!closed)
        return fail(QT_TRANSLATE_NOOP("Model", "Line %1: a quote isn't closed"), rows.size());
    const char* const columns[] = {"name", "unit", "expression", "comment"};
    if (rows.empty() || rows[0].size() < 3)
        return fail(QT_TRANSLATE_NOOP("Model", "Line %1: expected the columns name, unit, expression, comment"), 1);
    for (std::size_t i = 0; i < std::min<std::size_t>(rows[0].size(), 4); ++i) {
        if (lower(trimmed(rows[0][i])) != columns[i])
            return fail(QT_TRANSLATE_NOOP("Model", "Line %1: expected the columns name, unit, expression, comment"), 1);
    }
    ParameterTable table;
    const std::size_t first = std::min<std::size_t>(rows[0].size(), 4);
    for (std::size_t i = first; i < rows[0].size(); ++i) {
        const std::string name = trimmed(rows[0][i]);
        if (name.empty() || table.configuration(name))
            return fail(QT_TRANSLATE_NOOP("Model", "Line %1: each configuration needs its own name"), 1);
        table.configurations.push_back({name, {}});
    }
    for (std::size_t r = 1; r < rows.size(); ++r) {
        const auto& row = rows[r];
        if (std::all_of(row.begin(), row.end(), [](const std::string& f) { return trimmed(f).empty(); }))
            continue;
        const std::size_t line = r + 1;
        auto cell = [&row](std::size_t i) { return i < row.size() ? trimmed(row[i]) : std::string(); };
        Parameter p;
        p.name = cell(0);
        if (!isValidName(p.name))
            return fail(QT_TRANSLATE_NOOP("Model", "Line %1: not a valid parameter name"), line);
        if (table.indexOf(p.name) >= 0)
            return fail(QT_TRANSLATE_NOOP("Model", "Line %1: the name is used twice"), line);
        const std::string unit = lower(cell(1));
        if (unit == "mm") {
            p.kind = ValueKind::Length;
        } else if (unit == "in" || unit == "inch") {
            p.kind = ValueKind::Length;
            p.expression.unit = LengthUnit::Inch;
        } else if (unit == "deg" || unit == "\xC2\xB0") {
            p.kind = ValueKind::Angle;
        } else if (unit.empty()) {
            p.kind = ValueKind::Number;
        } else {
            return fail(QT_TRANSLATE_NOOP("Model", "Line %1: unknown unit (mm, in, deg or none)"), line);
        }
        p.expression.text = cell(2);
        if (p.expression.text.empty())
            return fail(QT_TRANSLATE_NOOP("Model", "Line %1: the expression is missing"), line);
        p.comment = cell(3);
        for (std::size_t c = 0; c < table.configurations.size(); ++c) {
            const std::string value = cell(first + c);
            if (!value.empty())
                table.configurations[c].values[p.name] = value;
        }
        table.parameters.push_back(std::move(p));
    }
    return table;
}
