// Better Bionicle Heroes: loaded by Ultimate ASI Loader (dinput8.dll) as BetterBionicleHeroes.asi.

#include <windows.h>

#include <cstdio>
#include <string>

#include "addresses.h"
#include "beam_fix.h"
#include "confirm_yes.h"
#include "cutscene_skip.h"
#include "dig_grass.h"
#include "display.h"
#include "intro_skip.h"
#include "log.h"
#include "pause_menu.h"
#include "vezon_extra.h"
#include "watchdog.h"

namespace
{
HMODULE g_module = nullptr;

// Path of a sibling file of this .asi with the given extension (".ini", ".log").
std::string SiblingPath(const char* extension)
{
    char path[MAX_PATH] = {};
    GetModuleFileNameA(g_module, path, MAX_PATH);
    std::string result(path);
    const size_t dot = result.find_last_of('.');
    if (dot != std::string::npos)
        result.resize(dot);
    return result + extension;
}

void Init()
{
    static bool done = false;
    if (done)
        return;
    done = true;

    LogOpen(SiblingPath(".log").c_str());
    const std::string ini = SiblingPath(".ini");

    if (GetPrivateProfileIntA("Debug", "HangWatchdog", 1, ini.c_str()) != 0)
        StartHangWatchdog();

    ResolveAddresses();

    VezonExtraConfig cfg;
    cfg.extraCharacter = GetPrivateProfileIntA("VezonExtra", "ExtraCharacter", 1, ini.c_str()) != 0;
    cfg.restrictVezon = GetPrivateProfileIntA("VezonExtra", "RestrictVezonPowers", 1, ini.c_str()) != 0;
    cfg.instantSwap = GetPrivateProfileIntA("VezonExtra", "InstantSwap", 1, ini.c_str()) != 0;
    if (cfg.extraCharacter || cfg.restrictVezon)
        InstallVezonExtra(cfg);

    if (GetPrivateProfileIntA("CutsceneSkip", "Enabled", 1, ini.c_str()) != 0)
    {
        char keys[128] = {};
        GetPrivateProfileStringA("CutsceneSkip", "ExtraKeys", "Space", keys, sizeof(keys), ini.c_str());
        InstallCutsceneSkip(keys);
    }

    if (GetPrivateProfileIntA("PauseMenu", "EscResumes", 1, ini.c_str()) != 0)
        InstallPauseMenuEsc();

    if (GetPrivateProfileIntA("Menus", "ConfirmDefaultYes", 1, ini.c_str()) != 0)
        InstallConfirmYes();

    DisplayConfig display;
    display.resolutionList = GetPrivateProfileIntA("Display", "ResolutionList", 1, ini.c_str()) != 0;
    display.aboveDesktop = GetPrivateProfileIntA("Display", "AboveDesktop", 0, ini.c_str()) != 0;
    display.matchRefresh = GetPrivateProfileIntA("Display", "MatchRefreshRate", 1, ini.c_str()) != 0;
    display.refreshRate = static_cast<int>(GetPrivateProfileIntA("Display", "RefreshRate", 0, ini.c_str()));
    if (display.resolutionList || display.matchRefresh)
        InstallDisplay(display);

    if (GetPrivateProfileIntA("Fixes", "ThokBeamDamage", 1, ini.c_str()) != 0)
        InstallBeamFix();

    if (GetPrivateProfileIntA("Fixes", "DigSpotGrass", 1, ini.c_str()) != 0)
        InstallDigSpotGrass();

    IntroSkipConfig intro;
    intro.logos = GetPrivateProfileIntA("Intro", "SkipLogos", 1, ini.c_str()) != 0;
    intro.intro = GetPrivateProfileIntA("Intro", "SkipIntro", 1, ini.c_str()) != 0;
    intro.attract = GetPrivateProfileIntA("Intro", "SkipAttract", 1, ini.c_str()) != 0;
    if (intro.logos || intro.intro || intro.attract)
        InstallIntroSkip(intro);
}
} // namespace

extern "C" __declspec(dllexport) void InitializeASI()
{
    Init();
}

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH)
    {
        g_module = module;
        DisableThreadLibraryCalls(module);
    }
    return TRUE;
}
