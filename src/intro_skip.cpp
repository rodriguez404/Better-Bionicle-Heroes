// Skips the startup movies.
//
// Movies play through Bink (binkw32.dll, BinkOpen). The game copes with a movie that does not open (as
// when the Movies folder is renamed), so the mod returns "not opened" for the movies chosen in the ini:
// the Eidos/TT logos, the story intro (only the first time it is opened, i.e. at startup), and the
// LEGO Star Wars advert shown when the title menu is left idle.

#include "intro_skip.h"

#include "hooks.h"
#include "log.h"

#include <windows.h>

#include <cstring>

namespace
{
using BinkOpen_t = void*(__stdcall*)(const char* name, unsigned flags);
BinkOpen_t o_BinkOpen = nullptr;

IntroSkipConfig g_cfg;
bool g_introSeen = false;

bool Contains(const char* text, const char* part)
{
    const size_t n = std::strlen(part);
    for (; text && *text; ++text)
        if (_strnicmp(text, part, n) == 0)
            return true;
    return false;
}

void* __stdcall hk_BinkOpen(const char* name, unsigned flags)
{
    bool skip = false;
    if (Contains(name, "Eidos_TT_Logo"))
        skip = g_cfg.logos;
    else if (Contains(name, "Bionicle_Intro"))
    {
        skip = g_cfg.intro && !g_introSeen;
        g_introSeen = true;
    }
    else if (Contains(name, "LSW_Attract"))
        skip = g_cfg.attract;
    if (skip)
    {
        Log("IntroSkip: skipped %s", name);
        return nullptr;
    }
    return o_BinkOpen(name, flags);
}
} // namespace

bool InstallIntroSkip(const IntroSkipConfig& cfg)
{
    g_cfg = cfg;
    HMODULE bink = GetModuleHandleA("binkw32.dll");
    void* binkOpen = bink ? reinterpret_cast<void*>(GetProcAddress(bink, "_BinkOpen@8")) : nullptr;
    if (!binkOpen)
    {
        Log("IntroSkip: binkw32.dll BinkOpen not found; feature disabled");
        return false;
    }
    const HookSpec hook = {"BinkOpen", reinterpret_cast<uintptr_t>(binkOpen), reinterpret_cast<void*>(&hk_BinkOpen),
                           reinterpret_cast<void**>(&o_BinkOpen)};
    const bool ok = HooksInit() && InstallHook(hook);
    Log("IntroSkip: %s (logos %d, intro %d, attract %d)", ok ? "installed" : "FAILED", cfg.logos, cfg.intro, cfg.attract);
    return ok;
}
