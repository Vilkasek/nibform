#pragma once

// Marks a user-facing message for translation without making the core depend on Qt:
// Qt's lupdate finds QT_TRANSLATE_NOOP by name, and the app translates the message in
// the "Model" context when it shows it (see modelText() in the editor).
#ifndef QT_TRANSLATE_NOOP
#define QT_TRANSLATE_NOOP(scope, text) text
#endif
