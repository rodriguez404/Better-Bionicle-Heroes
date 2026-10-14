// Esc on the top level of the pause menu resumes the game (like "Resume").
//
// Menu "back" is pad bit 0x80 (pad 1 "pressed", read by the GUI input 0x4b9ad0 as action 5); what it
// does is up to the page's OnBackout handler in the menu data. Submenus go back to their parent;
// the pause menu's own page ("PauseMenu", or "PauseMenuFrontEnd" in the hub) has no handler, so
// there Esc did nothing. Each GUI update (0x4bd040) the mod checks: game paused, the pause page is
// the page on screen (+0x58 bit 0; it is hidden while a submenu is open) and back (or Esc) was just
// pressed -> run the game's own ResumeGame GUI command (0x455890: unpause + close the menu).
// The Esc press that opens the pause menu must not close it, and the one that closes it must not
// reopen it, hence two short guards.

#include "pause_menu.h"

#include "addresses.h"
#include "hooks.h"
#include "log.h"

#include <windows.h>

#include <cstring>

namespace
{
// Bionicle.exe addresses: GuiUpdate 0x4bd040 (void __thiscall(Gui*, float dt), every frame),
// OpenPauseMenu 0x4862a0 (void __cdecl()), ResumeGame 0x455890 (GUI command, void __cdecl(element)),
// FindGuiObject 0x4bd2a0 (void* __thiscall(Gui*, const char* name, int any)),
// PauseState 0x64e0d8 (+8 = paused, set by 0x44bc40), Pad1Ptr 0x651e20 (+0x78 = buttons pressed).
constexpr uintptr_t kPageFlags = 0x58;          // bit 0 = page shown
constexpr uintptr_t kPageBackPage = 0x54;       // page "back" returns to, 0 on a top level page
const uintptr_t& kPauseState = addr::PauseState;
const uintptr_t& kPad1Ptr = addr::Pad1Ptr;
constexpr uint32_t kPadBack = 0x80;
constexpr DWORD kGuardMs = 300;

using GuiUpdate_t = void(__thiscall*)(void* gui, float dt);
using OpenPauseMenu_t = void(__cdecl*)();
using ResumeGame_t = void(__cdecl*)(void* element);
using FindGuiObject_t = void*(__thiscall*)(void* gui, const char* name, int any);
GuiUpdate_t o_GuiUpdate = nullptr;
OpenPauseMenu_t o_OpenPauseMenu = nullptr;
const GameFn<ResumeGame_t> ResumeGame{addr::ResumeGame};
const GameFn<FindGuiObject_t> FindGuiObject{addr::FindGuiObject};

DWORD g_openedAt = 0;
DWORD g_resumedAt = 0;
bool g_escWasDown = true;

template <typename T>
T& At(uintptr_t base, uintptr_t offset)
{
    return *reinterpret_cast<T*>(base + offset);
}

bool GameHasFocus()
{
    DWORD pid = 0;
    GetWindowThreadProcessId(GetForegroundWindow(), &pid);
    return pid == GetCurrentProcessId();
}

// The pause menu page, if it is the page currently on screen.
uintptr_t ShownPausePage(void* gui)
{
    static const char* const kPages[] = {"PauseMenu", "PauseMenuFrontEnd"};
    for (const char* name : kPages)
    {
        const auto page = reinterpret_cast<uintptr_t>(FindGuiObject(gui, name, 1));
        if (page && (At<uint32_t>(page, kPageFlags) & 1) && !At<uintptr_t>(page, kPageBackPage))
            return page;
    }
    return 0;
}

void __fastcall hk_GuiUpdate(void* gui, void*, float dt)
{
    if (At<int>(kPauseState, 8) == 0)
    {
        g_escWasDown = true;  // Esc held when the pause menu opens is not a fresh press
        return o_GuiUpdate(gui, dt);
    }
    const bool escDown = (GetAsyncKeyState(VK_ESCAPE) & 0x8000) != 0;
    const bool escPressed = escDown && !g_escWasDown && GameHasFocus();
    g_escWasDown = escDown;
    const uintptr_t pad = At<uintptr_t>(kPad1Ptr, 0);
    const bool backPressed = escPressed || (pad && (At<uint32_t>(pad, 0x78) & kPadBack));

    if (backPressed && GetTickCount() - g_openedAt > kGuardMs)
    {
        if (const uintptr_t page = ShownPausePage(gui))
        {
            ResumeGame(reinterpret_cast<void*>(page));
            g_resumedAt = GetTickCount();
        }
    }
    o_GuiUpdate(gui, dt);
}

void __cdecl hk_OpenPauseMenu()
{
    if (g_resumedAt && GetTickCount() - g_resumedAt < kGuardMs)
        return;  // the Esc press that just closed the menu
    g_openedAt = GetTickCount();
    o_OpenPauseMenu();
}
} // namespace

bool InstallPauseMenuEsc()
{
    if (!AddressesFound("PauseMenuEsc", {&addr::GuiUpdate, &addr::OpenPauseMenu, &addr::ResumeGame,
                                         &addr::FindGuiObject, &addr::PauseState, &addr::Pad1Ptr}))
        return false;
    const HookSpec hooks[] = {
        {"GuiUpdate", addr::GuiUpdate, reinterpret_cast<void*>(&hk_GuiUpdate),
         reinterpret_cast<void**>(&o_GuiUpdate)},
        {"OpenPauseMenu", addr::OpenPauseMenu, reinterpret_cast<void*>(&hk_OpenPauseMenu),
         reinterpret_cast<void**>(&o_OpenPauseMenu)},
    };
    bool ok = HooksInit();
    for (const auto& h : hooks)
        ok = ok && InstallHook(h);
    Log("PauseMenuEsc: %s", ok ? "installed" : "PARTIALLY installed");
    return ok;
}
