#include "app/Language.h"

#include <QCoreApplication>
#include <QLibraryInfo>
#include <QLocale>
#include <QSettings>
#include <QTranslator>

namespace {

// Live for the whole run; swapped in and out as the language changes.
QTranslator& appTranslator()
{
    static QTranslator translator;
    return translator;
}

QTranslator& qtTranslator()
{
    static QTranslator translator;
    return translator;
}

} // namespace

void applyLanguage(const QString& code)
{
    QCoreApplication::removeTranslator(&appTranslator());
    QCoreApplication::removeTranslator(&qtTranslator());

    // The sources are English; Polish comes from translations/shapr_pl.ts.
    const bool polish = code == QLatin1String("pl")
                     || (code != QLatin1String("en") && QLocale::system().language() == QLocale::Polish);
    // Decimal comma or point in values, to match the language.
    QLocale::setDefault(polish ? QLocale(QLocale::Polish, QLocale::Poland) : QLocale(QLocale::English, QLocale::UnitedStates));
    if (!polish)
        return;
    if (appTranslator().load(QStringLiteral(":/i18n/shapr_pl.qm")))
        QCoreApplication::installTranslator(&appTranslator());
    // Qt's own texts: standard buttons, file dialogs.
    if (qtTranslator().load(QStringLiteral("qtbase_pl"), QLibraryInfo::path(QLibraryInfo::TranslationsPath)))
        QCoreApplication::installTranslator(&qtTranslator());
}

QString savedLanguage()
{
    return QSettings().value(QStringLiteral("language"), QStringLiteral("system")).toString();
}

void saveLanguage(const QString& code)
{
    QSettings().setValue(QStringLiteral("language"), code);
}
