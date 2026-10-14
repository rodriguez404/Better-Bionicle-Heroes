// Skippable in-level cutscenes.
//
// The game already has a cutscene skip: the per-frame input handler (0x4862e0) ends the playing
// cutscene through CutsceneManager_Skip (0x41e950: stops it and tells the level script it finished)
// when a skip pad button (mask 0x1810; on the keyboard that is Esc = Start) is pressed — but only if
// the developer flag "AllowSkip" (flags byte 0x4e, set by the dev command-line options LOCAL /
// Level / DBUGDRAW) is on. Retail never sets it. The mod drops just that flag check, and also skips
// on extra keyboard keys (Space by default) read directly, independent of the game's key bindings.

#include "cutscene_skip.h"

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
// 0x486454: test dword [edx+0x78], 0x1810 / jz  (pad 1 "pressed" buttons: skip buttons)
// 0x486463: cmp byte [edi+0x4e], bl     / jz  (AllowSkip flag) <- the jz is removed
constexpr uintptr_t kSkipButtonsTest = 0x486454;
constexpr uintptr_t kAllowSkipCheck = 0x486463;
constexpr uintptr_t kAllowSkipJump = 0x486466;
constexpr uintptr_t kCutsceneSkip = 0x41e950;       // void __thiscall(CutsceneManager*, Cutscene*)
constexpr uintptr_t kPadInput = 0x4862e0;           // void __cdecl(), once per frame
constexpr uintptr_t kCutsceneManagerPtr = 0x648c50; // CutsceneManager*: +0xa4 = playing cutscene
constexpr uintptr_t kCutscenePlaying = 0xa4;

using CutsceneSkip_t = void(__thiscall*)(void* manager, void* cutscene);
using PadInput_t = void(__cdecl*)();
CutsceneSkip_t o_CutsceneSkip = nullptr;
PadInput_t o_PadInput = nullptr;

std::vector<int> g_extraKeys;  // virtual-key codes
bool g_keyWasDown = true;      // a key held when a cutscene starts does not skip it

bool GameHasFocus()
{
    DWORD pid = 0;
    GetWindowThreadProcessId(GetForegroundWindow(), &pid);
    return pid == GetCurrentProcessId();
}

void __fastcall hk_CutsceneSkip(void* manager, void*, void* cutscene)
{
    Log("cutscene skipped (%p)", cutscene);
    o_CutsceneSkip(manager, cutscene);
}

void __cdecl hk_PadInput()
{
    bool down = false;
    for (int key : g_extraKeys)
        down = down || (GetAsyncKeyState(key) & 0x8000) != 0;

    const uintptr_t manager = *reinterpret_cast<uintptr_t*>(kCutsceneManagerPtr);
    void* cutscene = manager ? *reinterpret_cast<void**>(manager + kCutscenePlaying) : nullptr;
    if (cutscene && down && !g_keyWasDown && GameHasFocus())
        hk_CutsceneSkip(reinterpret_cast<void*>(manager), nullptr, cutscene);
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
    static const uint8_t kButtons[] = {0xf7, 0x42, 0x78, 0x10, 0x18, 0x00, 0x00, 0x74, 0x1d};
    static const uint8_t kFlagCheck[] = {0x38, 0x5f, 0x4e, 0x74, 0x12};
    const HookSpec skipHook = {"CutsceneSkip", kCutsceneSkip, {0x56, 0x57, 0x8b, 0x7c, 0x24, 0x0c, 0x57, 0x8b},
                               reinterpret_cast<void*>(&hk_CutsceneSkip), reinterpret_cast<void**>(&o_CutsceneSkip)};
    const HookSpec padHook = {"PadInput", kPadInput, {0x83, 0xec, 0x08, 0xd9, 0x05, 0xbc, 0xeb, 0x64},
                              reinterpret_cast<void*>(&hk_PadInput), reinterpret_cast<void**>(&o_PadInput)};
    if (!BytesMatch(kSkipButtonsTest, kButtons, sizeof(kButtons)) ||
        !BytesMatch(kAllowSkipCheck, kFlagCheck, sizeof(kFlagCheck)) || !PrologueMatches(skipHook) ||
        !PrologueMatches(padHook))
    {
        Log("CutsceneSkip: this Bionicle.exe build is not the one the mod was made for; feature disabled");
        return false;
    }

    g_extraKeys = ParseKeys(extraKeys);
    static const uint8_t kNops[] = {0x90, 0x90};
    bool ok = PatchBytes(kAllowSkipJump, kNops, sizeof(kNops));
    ok = HooksInit() && InstallHook(skipHook) && ok;
    if (!g_extraKeys.empty())
        ok = InstallHook(padHook) && ok;
    Log("CutsceneSkip: %s (Esc + %u extra key(s))", ok ? "installed" : "PARTIALLY installed",
        static_cast<unsigned>(g_extraKeys.size()));
    return ok;
}
