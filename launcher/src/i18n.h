// Athelas - the launcher in other languages (1.7, as COGBox does it). Athelas' text is written in English; the chosen
// language's file, lang\<code>.txt beside Athelas.exe, gives each English text its translation, and a missing line
// stays English - a half-done translation never breaks anything. The file: UTF-8, one "English = translation" per
// line, "#" comments, "\n" for a line break inside a text. The English text is the key, so nothing is numbered.
//
// The text is swapped where Dear ImGui measures and draws it (GImTextTranslate in imgui.cpp) and where it formats
// it (GImFormatTranslate): no call site needs changing. A translated format string must have the same % fields in
// the same order as the English, or it isn't used (a wrong one could crash).
#pragma once
#include <string>
#include <vector>

struct Language { std::string code, name; };   // "fr", "Français"
const std::vector<Language> &languages();      // English first
// "auto" (or ""): Windows' display language when Athelas has it, else English; or a code from languages()
void i18n_load(const std::string &choice);
std::string i18n_current();                    // the code in use ("en": English)
std::string i18n_auto_code();
// After ImGui::Render(): the caches of run-time messages are emptied here when they grow large (not mid-frame)
void i18n_frame_end();
// For text drawn straight onto a draw list (not through ImGui's widgets)
const char *tr(const char *english);
std::string tr(const std::string &english);
// The font file this language needs from Windows (Japanese, Chinese, Korean), or "" when the built-in fonts do
std::string i18n_system_font(const std::string &code);
