// Skippable in-level cutscenes.
//
// The game already has a cutscene skip: the per-frame input handler (0x4862e0) ends the playing
// cutscene through CutsceneManager_Skip (0x41e950: stops it and tells the level script it finished)
// when a skip pad button (mask 0x1810; on the keyboard that is Esc = Start) is pressed — but only if
// the developer flag "AllowSkip" (flags byte 0x4e, set by the dev command-line options LOCAL /
// Level / DBUGDRAW) is on. Retail never sets it. The mod drops just that flag check, and also skips
// on extra keyboard keys (Space by default) read directly, independent of the game's key bindings.

#include "cutscene_skip.h"

#include "addresses.h"
#include "hooks.h"
#include "log.h"
#include "mem.h"

#include <windows.h>

#include <cctype>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace
{
// CutsceneSkipButtons (0x486454 in Bionicle.exe):
//   +0x00 test dword [edx+0x78], 0x1810 / jz   (pad 1 "pressed" buttons: skip buttons)
//   +0x09 mov edi, [flags]
//   +0x0f cmp byte [edi+0x4e], bl     / jz     (AllowSkip flag) <- this jz (+0x12) is removed
// CutsceneSkip 0x41e950: void __thiscall(CutsceneManager*, Cutscene*); PadInput 0x4862e0: per frame;
// CutsceneManagerPtr 0x648c50.
constexpr uintptr_t kAllowSkipJumpOffset = 0x12;
constexpr uintptr_t kCutscenePlaying = 0xa4;        // CutsceneManager: playing cutscene

using CutsceneSkip_t = void(__thiscall*)(void* manager, void* cutscene);
using PadInput_t = void(__cdecl*)();
const GameFn<CutsceneSkip_t> CutsceneSkip{addr::CutsceneSkip};
PadInput_t o_PadInput = nullptr;

std::vector<int> g_extraKeys;  // virtual-key codes
bool g_keyWasDown = true;      // a key held when a cutscene starts does not skip it

bool GameHasFocus()
{
    DWORD pid = 0;
    GetWindowThreadProcessId(GetForegroundWindow(), &pid);
    return pid == GetCurrentProcessId();
}

void __cdecl hk_PadInput()
{
    bool down = false;
    for (int key : g_extraKeys)
        down = down || (GetAsyncKeyState(key) & 0x8000) != 0;

    const uintptr_t manager = *reinterpret_cast<uintptr_t*>(addr::CutsceneManagerPtr);
    void* cutscene = manager ? *reinterpret_cast<void**>(manager + kCutscenePlaying) : nullptr;
    if (cutscene && down && !g_keyWasDown && GameHasFocus())
        CutsceneSkip(reinterpret_cast<void*>(manager), cutscene);
    g_keyWasDown = down;

    o_PadInput();
}

// "Space, Enter, 0x41" -> virtual-key codes.
std::vector<int> ParseKeys(const char* text)
{
    static const struct
    {
        const char* name;
        int vk;
    } kNames[] = {{"Space", VK_SPACE},   {"Enter", VK_RETURN},  {"Backspace", VK_BACK}, {"Tab", VK_TAB},
                  {"LMB", VK_LBUTTON},   {"RMB", VK_RBUTTON},   {"MMB", VK_MBUTTON},    {"Shift", VK_SHIFT},
                  {"Ctrl", VK_CONTROL},  {"Alt", VK_MENU}};
    std::vector<int> keys;
    std::string token;
    for (const char* p = text;; ++p)
    {
        const bool end = !*p || *p == ';';  // ";" starts an inline comment
        if (!end && *p != ',' && *p != ' ' && *p != '\t')
        {
            token += *p;
            continue;
        }
        if (!token.empty())
        {
            int vk = 0;
            for (const auto& n : kNames)
                if (_stricmp(n.name, token.c_str()) == 0)
                    vk = n.vk;
            if (!vk && token.size() == 1 && std::isalnum(static_cast<unsigned char>(token[0])))
                vk = std::toupper(static_cast<unsigned char>(token[0]));
            if (!vk)
                vk = static_cast<int>(std::strtol(token.c_str(), nullptr, 0));
            if (vk > 0 && vk < 0x100)
                keys.push_back(vk);
            else
                Log("CutsceneSkip: unknown key \"%s\" ignored", token.c_str());
            token.clear();
        }
        if (end)
            break;
    }
    return keys;
}

bool PatchBytes(uintptr_t address, const uint8_t* bytes, size_t size)
{
    DWORD oldProtect;
    if (!VirtualProtect(reinterpret_cast<void*>(address), size, PAGE_EXECUTE_READWRITE, &oldProtect))
        return false;
    std::memcpy(reinterpret_cast<void*>(address), bytes, size);
    VirtualProtect(reinterpret_cast<void*>(address), size, oldProtect, &oldProtect);
    FlushInstructionCache(GetCurrentProcess(), reinterpret_cast<void*>(address), size);
    return true;
}
} // namespace

bool InstallCutsceneSkip(const char* extraKeys)
{
    if (!AddressesFound("CutsceneSkip",
                        {&addr::CutsceneSkipButtons, &addr::CutsceneSkip, &addr::PadInput, &addr::CutsceneManagerPtr}))
        return false;
    const uintptr_t jump = addr::CutsceneSkipButtons + kAllowSkipJumpOffset;
    if (*reinterpret_cast<const uint8_t*>(jump) != 0x74)  // jz rel8
    {
        Log("CutsceneSkip: unexpected code at the AllowSkip check; feature disabled");
        return false;
    }

    g_extraKeys = ParseKeys(extraKeys);
    static const uint8_t kNops[] = {0x90, 0x90};
    bool ok = PatchBytes(jump, kNops, sizeof(kNops));
    const HookSpec padHook = {"PadInput", addr::PadInput, reinterpret_cast<void*>(&hk_PadInput),
                              reinterpret_cast<void**>(&o_PadInput)};
    if (!g_extraKeys.empty())
        ok = HooksInit() && InstallHook(padHook) && ok;
    Log("CutsceneSkip: %s (Esc + %u extra key(s))", ok ? "installed" : "PARTIALLY installed",
        static_cast<unsigned>(g_extraKeys.size()));
    return ok;
}
