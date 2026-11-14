#pragma once

#include "model/Units.h"

#include <functional>
#include <optional>
#include <string>
#include <vector>

// Values typed as expressions: `width / 2 + 3 mm`, `2 * pi * r`, `max(t; 1.5)`.
//
// Numbers may carry a unit (mm, cm, m, in, ft, deg or °, rad); lengths are millimetres
// and angles degrees inside. A plain number takes the unit it is added to (`width + 3`)
// or the one the field asks for (`3` in a length field), in the length unit the
// expression was typed in. Functions: sqrt, abs, round, floor, ceil, min, max, sin,
// cos, tan (of degrees), asin, acos, atan (in degrees); the constant pi. Arguments are
// separated by `;` or `,`; a comma between digits outside a function's parentheses is a
// decimal comma (`2,5`).

// What a value is: what its plain numbers mean, and what its result must be.
enum class ValueKind { Number, Length, Angle };

// An expression kept in place of a value, with the length unit its plain numbers were
// typed in (the one shown at the time), so it means the same when the interface
// switches units.
struct Expression
{
    std::string text;
    LengthUnit unit = LengthUnit::Millimetre;
    bool empty() const { return text.empty(); }
    bool operator==(const Expression&) const = default;
};

// Why an expression has no value: an English message marked for translation
// (QT_TRANSLATE_NOOP("Model", ...)), where %1 stands for `detail` (a name, a position).
struct ExpressionError
{
    std::string message;
    std::string detail;
    bool operator==(const ExpressionError&) const = default;
};

// A value with its dimension: lengths (mm) to the power `length`, angles (degrees) to
// the power `angle`. Both zero: a plain number.
struct Quantity
{
    double value = 0.0;
    int length = 0;
    int angle = 0;
    bool plain() const { return length == 0 && angle == 0; }
};

// The value of a name used in an expression; empty with `error` set when there is none.
using NameLookup = std::function<std::optional<Quantity>(const std::string& name, ExpressionError& error)>;

struct EvalResult
{
    std::optional<double> value; // mm, degrees or a plain number, as `kind` asks
    ExpressionError error;       // when there is no value
};

// Evaluates `text` as a value of `kind`. Plain numbers are in `unit` when they turn into
// lengths.
EvalResult evaluate(const std::string& text, ValueKind kind, LengthUnit unit, const NameLookup& names);
// The same without fixing the dimension: what a parameter's expression comes to.
std::optional<Quantity> evaluateQuantity(const std::string& text, LengthUnit unit, const NameLookup& names,
                                         ExpressionError& error);
// `quantity` as a value of `kind` (plain numbers in `unit`); empty with `error` if it is
// another dimension.
std::optional<double> asKind(const Quantity& quantity, ValueKind kind, LengthUnit unit, ExpressionError& error);

// `name = expression` typed in a value field: the value names a parameter (a new one, or
// one to set). Empty for anything else.
struct Definition
{
    std::string name;
    std::string expression;
};
std::optional<Definition> definitionIn(const std::string& text);
// A value as a parameter keeps it: up to six decimals, no trailing zeros, a decimal point.
std::string numberText(double value);

// A number as typed in a value field ("12.5", "-3", "12,5"), nothing else.
bool isPlainNumber(const std::string& text);
// The names an expression uses (parameters; not units, functions or constants), in the
// order they appear, each once. Empty for text that can't be read.
std::vector<std::string> namesIn(const std::string& text);
// `text` with every use of the name `from` replaced by `to` (as written otherwise).
std::string renameInExpression(const std::string& text, const std::string& from, const std::string& to);
// Whether `name` can name a parameter: a letter or `_`, then letters, digits and `_`,
// and not a unit, function or constant. `error` says why not.
bool isValidName(const std::string& name, ExpressionError* error = nullptr);
