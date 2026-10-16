// Two-option confirmations start on the first option (YES / QUIT / EXIT LEVEL) instead of the second.
//
// The menu scripts (gui/frontend.guic, gui/game_global.guic) run the GUI command HighlightBottom only on
// two-option confirmation pages: the shop's ConfirmBuy, ConfirmBuyUpgrade and
// Confirm{Weapon,Ability,Health}Upgrade, QuitToWinSure, QuitSure, QuitGameSure and ExitLevel (11 uses in
// all). It highlights their second option (NO / CANCEL / RESUME). The mod runs HighlightTop instead.
//
// The save/load screens are not scripted: SaveMenuPush (0x522180) opens a screen by id with a starting
// selection, and the Yes/No ones (confirm save or overwrite, confirm load, cancel save, cancel load) are
// opened on 1 = No. The mod opens them on 0 = Yes.

#include "confirm_yes.h"

#include "addresses.h"
#include "hooks.h"
#include "log.h"

namespace
{
// GUI commands HighlightTop / HighlightBottom (0x4c1670 / 0x4c16b0): void __cdecl(element, arg).
using GuiCommand_t = void(__cdecl*)(void* element, void* arg);
GuiCommand_t o_HighlightBottom = nullptr;
const GameFn<GuiCommand_t> HighlightTop{addr::HighlightTop};

void __cdecl hk_HighlightBottom(void* element, void* arg)
{
    HighlightTop(element, arg);
}

// Save/load screen ids (table at 0x6093c0) with Yes / No buttons.
constexpr int kScreenConfirmSave = 0x3f0;  // "Confirm Save?" / "Do you wish to overwrite previous Save?"
constexpr int kScreenConfirmLoad = 0x3f5;  // "Confirm Load?"
constexpr int kScreenCancelSave = 0x3f8;   // "Do you want to cancel the Save?"
constexpr int kScreenCancelLoad = 0x3f9;   // "Do you want to cancel the Load?"

using SaveMenuPush_t = void*(__cdecl*)(int screen, int selection, int extra);
SaveMenuPush_t o_SaveMenuPush = nullptr;

void* __cdecl hk_SaveMenuPush(int screen, int selection, int extra)
{
    const bool yesNo = screen == kScreenConfirmSave || screen == kScreenConfirmLoad || screen == kScreenCancelSave ||
                       screen == kScreenCancelLoad;
    if (yesNo && selection == 1)
        selection = 0;
    return o_SaveMenuPush(screen, selection, extra);
}
} // namespace

bool InstallConfirmYes()
{
    if (!AddressesFound("ConfirmYes", {&addr::HighlightTop, &addr::HighlightBottom, &addr::SaveMenuPush}))
        return false;
    const HookSpec hooks[] = {
        {"HighlightBottom", addr::HighlightBottom, reinterpret_cast<void*>(&hk_HighlightBottom),
         reinterpret_cast<void**>(&o_HighlightBottom)},
        {"SaveMenuPush", addr::SaveMenuPush, reinterpret_cast<void*>(&hk_SaveMenuPush),
         reinterpret_cast<void**>(&o_SaveMenuPush)},
    };
    bool ok = HooksInit();
    for (const auto& h : hooks)
        ok = ok && InstallHook(h);
    Log("ConfirmYes: %s", ok ? "installed" : "FAILED");
    return ok;
}
