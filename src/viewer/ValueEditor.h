#pragma once

#include "model/Expression.h"

#include <QFrame>
#include <QPointF>

#include <functional>
#include <optional>

#include <QStringList>

class QLabel;
class QLineEdit;

// A small number field that floats over the 3D view, next to what it edits
// (as CAD apps do on tablets). Unlike a modal dialog it never blocks the event loop,
// so it is safe to open from pointer handlers. Enter or ✓ accepts, Esc or ✕ cancels.
class ValueEditor : public QFrame
{
    Q_OBJECT

public:
    explicit ValueEditor(QWidget* parent);

    // Shows the field centered near `anchor` (parent coordinates) with `value` selected.
    // `accept` gets a value within [min, max]; `cancel` runs when the edit is dropped;
    // `changed` follows every valid value while typing (for live previews).
    // Opening again drops the previous edit first. `label` is asked again when the
    // language changes.
    void open(std::function<QString()> label, double value, double min, double max, const QPointF& anchor,
              std::function<void(double)> accept, std::function<void()> cancel = {},
              std::function<void(double)> changed = {});
    // Moves the open field so it starts just right of `point` (parent coordinates),
    // instead of centered on its anchor: for things that must stay visible, like an arrow.
    void placeBeside(const QPointF& point);
    // Shows `value` as if typed (e.g. while an arrow is dragged), selected, without `changed`.
    void setValue(double value);
    // Shows the expression the value came from instead of the number (after open()).
    void setExpression(const Expression& expression);
    // What was accepted, when it was an expression (read it in the accept callback):
    // empty for a plain number. An expression accepted unchanged keeps its unit.
    const Expression& expression() const { return m_expression; }
    // Evaluates expressions: the text, what the field takes, the unit plain numbers are in,
    // and whether `name = value` may name a parameter here.
    using Evaluator =
        std::function<EvalResult(const std::string& text, ValueKind kind, LengthUnit unit, bool definitions)>;
    void setEvaluator(Evaluator evaluator) { m_evaluator = std::move(evaluator); }
    // This edit (until the field closes) takes `name = value` too (a sketch dimension).
    void allowDefinitions() { m_definitions = true; }
    void cancel();
    // Takes the typed value (what Enter does); an invalid one is marked and kept open.
    void accept();
    bool isOpen() const { return isVisible(); }
    QPointF anchor() const { return m_anchor; }
    // Gives the keyboard back to the field (after a click it chose to ignore).
    void refocus();
    // A row of exclusive choices under the field (e.g. what an extrusion does to bodies),
    // until the field closes. `labels` is asked again by refreshChoices() and when the
    // language changes; `chosen` gets the index tapped.
    void setChoices(std::function<QStringList()> labels, int current, const std::function<void(int)>& chosen);
    void refreshChoices();
    // A numeric keypad under the field, for typing values without a keyboard (tablet mode).
    void setKeypad(bool on);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
    void changeEvent(QEvent* event) override;

private:
    void retranslate();
    void edited(); // typed or keyed in: clear the error mark, report the value
    void key(const QString& key);
    void moveClamped(double x, double y);
    // The typed value in the unit shown; `error` says why there is none (for an expression).
    std::optional<double> parsed(QString* error = nullptr) const;
    bool typedExpression() const; // the text is more than a number
    void showResult();            // under the field, for an expression
    // A label with "[mm]" asks for a length: shown (and typed) in the current length unit,
    // always handed back in millimetres.
    QString shownLabel() const;
    double shown(double value) const;
    double taken(double value) const;
    bool m_length = false;
    ValueKind m_kind = ValueKind::Number;
    bool m_definitions = false;
    Evaluator m_evaluator;
    Expression m_expression; // accepted
    Expression m_opened;     // shown by setExpression()
    static QString format(double value);

    QLabel* m_label;
    std::function<QString()> m_labelText;
    QLineEdit* m_edit;
    QWidget* m_keypad;
    QWidget* m_choices;
    QLabel* m_result; // an expression's value, or why it has none
    std::function<QStringList()> m_choiceLabels;
    void clearChoices();
    double m_min = 0.0;
    double m_max = 0.0;
    QPointF m_anchor;
    std::function<void(double)> m_accept;
    std::function<void()> m_cancel;
    std::function<void(double)> m_changed;
};
