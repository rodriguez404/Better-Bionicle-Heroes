#pragma once

// Additions to the game's GUI scripts (gui/*.guic: plain text in bionicle.dat), made while the game
// reads them, and the mod's own GUI commands and text callbacks. Everything is registered before
// InstallGuiScript().

#include <initializer_list>

// In a script whose path contains `file` (lines compared without surrounding spaces):
// once the lines `after` have been read in this order, the game reads `lines` next;
void GuiScriptInsertAfter(const char* file, std::initializer_list<const char*> after,
                          std::initializer_list<const char*> lines);
// the game reads `lines` right before the block whose first lines are `blockStart`. A page must be
// defined before anything that opens it: the game resolves names while it reads the script.
void GuiScriptInsertBefore(const char* file, std::initializer_list<const char*> blockStart,
                           std::initializer_list<const char*> lines);

// "Action <name>" runs fn(element).
using GuiCommandFn = void(__cdecl*)(void* element);
void GuiAddCommand(const char* name, GuiCommandFn fn);

// "Text ? <name>" shows the string fn stores in *text (kept alive by the mod).
using GuiTextFn = void(__cdecl*)(void* element, const wchar_t** text);
void GuiAddText(const char* name, GuiTextFn fn);

// The game's language: 1 English, 2 French, 3 Spanish, 4 German, 5 Italian, 8 Danish, 11 Polish,
// 12 Russian, 18 American English, 0 Japanese.
int GuiLanguage();

bool InstallGuiScript();
