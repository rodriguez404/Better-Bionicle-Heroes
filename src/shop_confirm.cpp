// Shop purchase confirmations default to YES instead of NO.
//
// The shop's confirm pages ("ConfirmBuy" for hints/bonuses, "ConfirmBuyUpgrade" and
// "Confirm{Weapon,Ability,Health}Upgrade" for Toa upgrades) run the GUI command HighlightBottom
// when they open (menu data), which highlights their bottom item: NO (children: [0] YES, [1] NO).
// On those pages only, the mod runs HighlightTop instead. Other YES/NO pages (e.g. "QuitToWinSure")
// keep defaulting to NO.

#include "shop_confirm.h"

#include "hooks.h"
#include "log.h"

#include <cstring>

namespace
{
constexpr uintptr_t kHighlightTop = 0x4c1670;     // GUI commands: void __cdecl(element, arg)
constexpr uintptr_t kHighlightBottom = 0x4c16b0;
constexpr uintptr_t kElementParent = 0x4c;

using GuiCommand_t = void(__cdecl*)(void* element, void* arg);
using ElementName_t = const char*(__thiscall*)(void* element);
GuiCommand_t o_HighlightBottom = nullptr;
const auto HighlightTop = reinterpret_cast<GuiCommand_t>(kHighlightTop);

bool IsShopConfirmPage(uintptr_t element)
{
    while (element && *reinterpret_cast<uintptr_t*>(element + kElementParent))
        element = *reinterpret_cast<uintptr_t*>(element + kElementParent);
    if (!element)
        return false;
    const auto vtable = *reinterpret_cast<uintptr_t*>(element);
    const char* name =
        reinterpret_cast<ElementName_t>(*reinterpret_cast<uintptr_t*>(vtable + 0xc))(reinterpret_cast<void*>(element));
    if (!name)
        return false;
    static const char* const kPages[] = {"ConfirmBuy", "ConfirmBuyUpgrade", "ConfirmWeaponUpgrade",
                                         "ConfirmAbilityUpgrade", "ConfirmHealthUpgrade"};
    for (const char* page : kPages)
        if (_stricmp(name, page) == 0)
            return true;
    return false;
}

void __cdecl hk_HighlightBottom(void* element, void* arg)
{
    if (IsShopConfirmPage(reinterpret_cast<uintptr_t>(element)))
        return HighlightTop(element, arg);
    o_HighlightBottom(element, arg);
}
} // namespace

bool InstallShopConfirmYes()
{
    static const uint8_t kTopBytes[] = {0x8b, 0x44, 0x24, 0x04, 0x8b, 0x88, 0x8c, 0x00, 0x00, 0x00, 0x85, 0xc9,
                                        0x74, 0x09, 0x8b, 0x40, 0x3c};
    const HookSpec hook = {"HighlightBottom", kHighlightBottom, {0x8b, 0x44, 0x24, 0x04, 0x8b, 0x88, 0x8c, 0x00},
                           reinterpret_cast<void*>(&hk_HighlightBottom), reinterpret_cast<void**>(&o_HighlightBottom)};
    if (std::memcmp(reinterpret_cast<const void*>(kHighlightTop), kTopBytes, sizeof(kTopBytes)) != 0 ||
        !PrologueMatches(hook))
    {
        Log("ShopConfirmYes: this Bionicle.exe build is not the one the mod was made for; feature disabled");
        return false;
    }
    const bool ok = HooksInit() && InstallHook(hook);
    Log("ShopConfirmYes: %s", ok ? "installed" : "FAILED");
    return ok;
}
