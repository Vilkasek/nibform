// The floating value field: Enter / Esc must reach it even though the main
// window also binds them as shortcuts. Runs on the offscreen platform.

#include "viewer/ValueEditor.h"

#include <QAction>
#include <QApplication>
#include <QLineEdit>
#include <QMainWindow>
#include <QTest>
#include <QToolButton>

#include <optional>

class ValueEditorTest : public QObject
{
    Q_OBJECT

private slots:
    void init()
    {
        m_window = new QMainWindow;
        auto* area = new QWidget;
        area->resize(600, 400);
        m_window->setCentralWidget(area);
        m_editor = new ValueEditor(area);
        // The same window-wide shortcuts MainWindow has.
        m_escapes = m_returns = 0;
        auto* esc = new QAction(m_window);
        esc->setShortcut(Qt::Key_Escape);
        connect(esc, &QAction::triggered, this, [this] { ++m_escapes; });
        auto* ret = new QAction(m_window);
        ret->setShortcut(Qt::Key_Return);
        connect(ret, &QAction::triggered, this, [this] { ++m_returns; });
        m_window->addActions({esc, ret});
        m_window->show();
        QVERIFY(QTest::qWaitForWindowActive(m_window));
        m_accepted.reset();
        m_cancelled = false;
    }

    void cleanup() { delete m_window; }

    void enterAcceptsBothSeparators()
    {
        open(10.0);
        type("12,5");
        QTest::keyClick(field(), Qt::Key_Return);
        QCOMPARE(m_accepted, std::optional<double>(12.5));
        QVERIFY(!m_editor->isOpen());
        QCOMPARE(m_returns, 0);

        open(10.0);
        type("7.25");
        QTest::keyClick(field(), Qt::Key_Enter);
        QCOMPARE(m_accepted, std::optional<double>(7.25));
    }

    void escapeCancels()
    {
        open(10.0);
        type("99");
        QTest::keyClick(field(), Qt::Key_Escape);
        QVERIFY(m_cancelled);
        QVERIFY(!m_accepted);
        QVERIFY(!m_editor->isOpen());
        QCOMPARE(m_escapes, 0);
    }

    void invalidValuesStayOpen()
    {
        open(10.0);
        type("abc");
        QTest::keyClick(field(), Qt::Key_Return);
        QVERIFY(m_editor->isOpen() && !m_accepted);
        type("5000"); // above max
        QTest::keyClick(field(), Qt::Key_Return);
        QVERIFY(m_editor->isOpen() && !m_accepted);
        type("50");
        QTest::keyClick(field(), Qt::Key_Return);
        QCOMPARE(m_accepted, std::optional<double>(50.0));
    }

    void startsWithTheCurrentValueSelected()
    {
        open(57.14);
        QCOMPARE(field()->text().replace(',', '.'), QStringLiteral("57.14"));
        QTest::keyClick(field(), Qt::Key_3); // replaces the selection
        QCOMPARE(field()->text(), QStringLiteral("3"));
    }

    void typingReportsLiveValues()
    {
        std::vector<double> seen;
        m_editor->open(QStringLiteral("Odległość [mm]"), 10.0, -100.0, 100.0, {300, 200},
                       [](double) {}, {}, [&seen](double v) { seen.push_back(v); });
        QTRY_VERIFY(field()->hasFocus());
        type("-2,5");
        QVERIFY(!seen.empty());
        QCOMPARE(seen.back(), -2.5);
        // Values set from outside (an arrow being dragged) don't echo back.
        const std::size_t before = seen.size();
        m_editor->setValue(7.0);
        QCOMPARE(field()->text(), QStringLiteral("7"));
        QCOMPARE(seen.size(), before);
        m_editor->cancel();
    }

    void keypadWithoutAKeyboard()
    {
        std::vector<double> seen;
        m_editor->setKeypad(true);
        m_editor->open(QStringLiteral("Odległość [mm]"), 10.0, -100.0, 100.0, {300, 200},
                       [this](double v) { m_accepted = v; }, {}, [&seen](double v) { seen.push_back(v); });
        auto press = [this](const QString& key) {
            auto* button = m_editor->findChild<QToolButton*>(QStringLiteral("key_") + key);
            QVERIFY2(button, qPrintable(key));
            button->click();
        };
        press("1"); // replaces the selected 10
        press("2");
        press(",");
        press("5");
        QCOMPARE(field()->text(), QStringLiteral("12,5"));
        QCOMPARE(seen.back(), 12.5); // the preview follows the keys
        press("±");
        QCOMPARE(field()->text(), QStringLiteral("-12,5"));
        press("⌫");
        QCOMPARE(field()->text(), QStringLiteral("-12,"));
        press("C");
        press("7");
        press("✓");
        QCOMPARE(m_accepted, std::optional<double>(7.0));
        QVERIFY(!m_editor->isOpen());
        m_editor->setKeypad(false);
    }

private:
    void open(double value)
    {
        m_editor->open(QStringLiteral("Długość [mm]"), value, 1e-3, 1000.0, {300, 200},
                       [this](double v) { m_accepted = v; }, [this] { m_cancelled = true; });
        QApplication::processEvents(); // the field takes focus on the next event loop pass
        QTRY_VERIFY(field()->hasFocus());
    }

    QLineEdit* field() const { return m_editor->findChild<QLineEdit*>(); }

    void type(const QString& text)
    {
        field()->selectAll();
        QTest::keyClicks(field(), text);
    }

    QMainWindow* m_window = nullptr;
    ValueEditor* m_editor = nullptr;
    int m_escapes = 0;
    int m_returns = 0;
    std::optional<double> m_accepted;
    bool m_cancelled = false;
};

QTEST_MAIN(ValueEditorTest)
#include "test_value_editor.moc"
