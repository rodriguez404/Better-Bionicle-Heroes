#pragma once

// In-level cutscenes skippable with Esc (the game's own, normally developer-only, skip) and with
// extra keyboard keys, e.g. "Space, Enter" ([CutsceneSkip] Enabled / ExtraKeys).
bool InstallCutsceneSkip(const char* extraKeys);
