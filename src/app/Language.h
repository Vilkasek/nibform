#pragma once

#include <QString>

// The UI language: "system" (Polish when the desktop is Polish, otherwise English),
// "pl" or "en". Installs the app's and Qt's own translations and the number format;
// Qt then sends every widget a LanguageChange event.
void applyLanguage(const QString& code);
QString savedLanguage();          // from the settings, "system" when unset
void saveLanguage(const QString& code);
