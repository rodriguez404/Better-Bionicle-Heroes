// Video options list the monitor's own resolutions, and fullscreen runs at the Windows refresh rate.
//
// Resolutions. The video options take their resolutions from a fixed table in the exe (0x603df8:
// 9 x {w, h}, 640x480 .. 1600x1200); the option's range 0..8 comes from the menu data. The GUI
// command GetCurrentRes (0x45baf0) looks the current mode up in that table and falls back to entry 0,
// so a resolution set in nuconfig.txt (e.g. 1920x1080) shows as "640 x 480" and Accept switches to it.
// The mod builds its own table from the adapter's modes (the D3D list the game keeps at device+0x1c4)
// up to the desktop resolution, points the table reads of AdjustRes (0x45b9f0, Accept) and
// GetResolution (0x45fe50, the "W x H" label) at it, and replaces GetCurrentRes to select the current
// mode and set the option's range.
//
// Refresh rate. The game never applies NU_PCREFRESH: the first device gets the default rate, every
// later create/reset (Accept, Alt+Tab) the highest rate below 76 Hz (FindModeRefresh 0x582f10), and
// Accept asks for the last rate below 96 Hz (0x583110 + 0x57c830). In practice: 60 Hz. Before the
// device is created or reset (0x582040), the mod sets the requested rate (device+0x1e8) to the
// Windows desktop rate (or [Display] RefreshRate) if the mode has it, else the closest lower one.

#include "display.h"

#include "addresses.h"
#include "hooks.h"
#include "log.h"
#include "mem.h"

#include <windows.h>

#include <algorithm>
#include <cstring>
#include <utility>
#include <vector>

namespace
{
// Video device object (0x3513508 in Bionicle.exe; `this` of the hooked functions).
constexpr uintptr_t kDevBackBufferWidth = 0x158;   // present parameters
constexpr uintptr_t kDevBackBufferHeight = 0x15c;
constexpr uintptr_t kDevPresentWindowed = 0x178;
constexpr uintptr_t kDevPresentRefresh = 0x188;
constexpr uintptr_t kDevModes = 0x1c4;             // D3DDISPLAYMODE[] for X8R8G8B8 (adapter 0)
constexpr uintptr_t kDevModeCount = 0x1cc;
constexpr uintptr_t kDevWindowed = 0x1dc;          // requested mode
constexpr uintptr_t kDevWidth = 0x1e0;
constexpr uintptr_t kDevHeight = 0x1e4;
constexpr uintptr_t kDevRefresh = 0x1e8;
constexpr uint32_t kFormatX8R8G8B8 = 22;

// GUI option element -> value object with int16 value / step / min / max.
constexpr uintptr_t kElementValue = 0x88;
constexpr uintptr_t kValue = 0x28;
constexpr uintptr_t kValueMin = 0x2c;
constexpr uintptr_t kValueMax = 0x2e;

constexpr int kGameTableSize = 9;
constexpr int kMaxModes = 64;
constexpr uint32_t kMinWidth = 640;
constexpr uint32_t kMinHeight = 480;

struct Mode  // D3DDISPLAYMODE
{
    uint32_t width, height, refresh, format;
};

using CreateOrResetDevice_t = void(__thiscall*)(uintptr_t dev);
using FindModeRefresh_t = Mode*(__thiscall*)(uintptr_t dev, uint32_t w, uint32_t h);
using ListRefreshRates_t = int(__thiscall*)(uintptr_t dev, uint32_t w, uint32_t h, uint32_t* out);
using GuiCommand_t = void(__cdecl*)(uintptr_t element);
CreateOrResetDevice_t o_CreateOrResetDevice = nullptr;
FindModeRefresh_t o_FindModeRefresh = nullptr;
ListRefreshRates_t o_ListRefreshRates = nullptr;
GuiCommand_t o_GetCurrentRes = nullptr;

DisplayConfig g_cfg;
uintptr_t g_dev = 0;
uint32_t g_desktopWidth = 0, g_desktopHeight = 0, g_desktopRefresh = 0;
Mode g_mode = {};

// Same layout as the game's table: {w, h} pairs. Read by the game's patched AdjustRes/GetResolution.
int32_t g_table[kMaxModes * 2] = {};
int g_count = 0;

template <typename T>
T& At(uintptr_t base, uintptr_t offset)
{
    return *reinterpret_cast<T*>(base + offset);
}

uint32_t TargetRefresh()
{
    if (!g_cfg.matchRefresh)
        return 75;  // the game's own preference (highest below 76 Hz)
    if (g_cfg.refreshRate > 0)
        return static_cast<uint32_t>(g_cfg.refreshRate);
    return g_desktopRefresh > 1 ? g_desktopRefresh : UINT32_MAX;  // 0/1 = "hardware default": the highest
}

// Refresh rate for fullscreen w x h: `target` if the adapter has it, else the closest lower one (or
// the lowest higher one). 0 if the adapter has no such mode.
uint32_t PickRefresh(uintptr_t dev, uint32_t w, uint32_t h, uint32_t target)
{
    const Mode* modes = At<const Mode*>(dev, kDevModes);
    const uint32_t count = At<uint32_t>(dev, kDevModeCount);
    uint32_t below = 0, above = 0;
    for (uint32_t i = 0; modes && i < count; ++i)
    {
        const Mode& m = modes[i];
        if (m.width != w || m.height != h || m.format != kFormatX8R8G8B8 || m.refresh == 0)
            continue;
        if (m.refresh <= target)
            below = std::max(below, m.refresh);
        else if (!above || m.refresh < above)
            above = m.refresh;
    }
    return below ? below : above;
}

void CurrentSize(uintptr_t dev, uint32_t& w, uint32_t& h)
{
    w = At<uint32_t>(dev, kDevBackBufferWidth);
    h = At<uint32_t>(dev, kDevBackBufferHeight);
    if (!w || !h)  // windowed: the back buffer follows the window
    {
        w = At<uint32_t>(dev, kDevWidth);
        h = At<uint32_t>(dev, kDevHeight);
    }
}

// The adapter's resolutions (>= 640x480, up to the desktop's unless AboveDesktop) plus the current one.
void BuildResolutionList()
{
    if (!g_dev)
        return;
    const Mode* modes = At<const Mode*>(g_dev, kDevModes);
    const uint32_t count = At<uint32_t>(g_dev, kDevModeCount);
    bool desktopListed = false;
    for (uint32_t i = 0; modes && i < count; ++i)
        desktopListed = desktopListed || (modes[i].width == g_desktopWidth && modes[i].height == g_desktopHeight);
    // No cap if the desktop size is unknown or not an adapter mode (it would cut off real modes).
    const bool cap = !g_cfg.aboveDesktop && desktopListed;

    std::vector<std::pair<uint32_t, uint32_t>> list;
    for (uint32_t i = 0; modes && i < count; ++i)
    {
        const Mode& m = modes[i];
        if (m.format != kFormatX8R8G8B8 || m.width < kMinWidth || m.height < kMinHeight)
            continue;
        if (cap && (m.width > g_desktopWidth || m.height > g_desktopHeight))
            continue;
        list.emplace_back(m.width, m.height);
    }
    uint32_t w, h;
    CurrentSize(g_dev, w, h);
    if (w && h)
        list.emplace_back(w, h);
    std::sort(list.begin(), list.end());
    list.erase(std::unique(list.begin(), list.end()), list.end());
    if (list.size() > kMaxModes)
        list.erase(list.begin(), list.end() - kMaxModes);
    if (list.empty())
        return;

    int32_t table[kMaxModes * 2] = {};
    for (size_t i = 0; i < list.size(); ++i)
    {
        table[i * 2] = static_cast<int32_t>(list[i].first);
        table[i * 2 + 1] = static_cast<int32_t>(list[i].second);
    }
    if (g_count == static_cast<int>(list.size()) && std::memcmp(table, g_table, sizeof(table)) == 0)
        return;
    std::memcpy(g_table, table, sizeof(table));
    g_count = static_cast<int>(list.size());
    Log("Display: %d resolutions in the video options, up to %ux%u", g_count, list.back().first,
        list.back().second);
}

// GUI command GetCurrentRes(element): selects the current mode in the Resolution option.
void __cdecl hk_GetCurrentRes(uintptr_t element)
{
    if (!g_dev)
        return o_GetCurrentRes(element);  // never happens after startup; the game's table is still ours
    BuildResolutionList();
    const uintptr_t value = element ? At<uintptr_t>(element, kElementValue) : 0;
    if (!value)
        return;
    uint32_t w, h;
    CurrentSize(g_dev, w, h);
    int index = 0;
    for (int i = 0; i < g_count; ++i)
        if (static_cast<uint32_t>(g_table[i * 2]) == w && static_cast<uint32_t>(g_table[i * 2 + 1]) == h)
            index = i;
    At<int16_t>(value, kValueMin) = 0;
    At<int16_t>(value, kValueMax) = static_cast<int16_t>(g_count - 1);
    At<int16_t>(value, kValue) = static_cast<int16_t>(index);
}

void LogModeIfChanged(uintptr_t dev)
{
    static uint32_t last[4] = {};
    const uint32_t now[4] = {At<uint32_t>(dev, kDevBackBufferWidth), At<uint32_t>(dev, kDevBackBufferHeight),
                             At<uint32_t>(dev, kDevPresentRefresh), At<uint32_t>(dev, kDevPresentWindowed)};
    if (std::memcmp(now, last, sizeof(now)) == 0)
        return;
    std::memcpy(last, now, sizeof(now));
    if (now[3])
        Log("Display: windowed");
    else
        Log("Display: fullscreen %ux%u @ %u Hz", now[0], now[1], now[2]);
}

void __fastcall hk_CreateOrResetDevice(uintptr_t dev)
{
    g_dev = dev;
    if (g_cfg.matchRefresh && !At<uint32_t>(dev, kDevWindowed))
    {
        const uint32_t hz = PickRefresh(dev, At<uint32_t>(dev, kDevWidth), At<uint32_t>(dev, kDevHeight), TargetRefresh());
        if (hz)
            At<uint32_t>(dev, kDevRefresh) = hz;
    }
    o_CreateOrResetDevice(dev);
    LogModeIfChanged(dev);
}

// Used when the device is reset: the caller reads the mode unchecked (null = crash if the adapter
// has no rate below 76 Hz for that resolution).
Mode* __fastcall hk_FindModeRefresh(uintptr_t dev, void*, uint32_t w, uint32_t h)
{
    Mode* mode = o_FindModeRefresh(dev, w, h);
    if (mode && !g_cfg.matchRefresh)
        return mode;
    const uint32_t hz = PickRefresh(dev, w, h, TargetRefresh());
    if (!hz)
        return mode;
    Mode* out = mode ? mode : &g_mode;
    *out = {w, h, hz, kFormatX8R8G8B8};
    return out;
}

// Accept refuses a resolution without any rate below 96 Hz ("UnsupportedRes").
int __fastcall hk_ListRefreshRates(uintptr_t dev, void*, uint32_t w, uint32_t h, uint32_t* out)
{
    int count = o_ListRefreshRates(dev, w, h, out);
    if (count == 0)
    {
        if (const uint32_t hz = PickRefresh(dev, w, h, TargetRefresh()))
        {
            out[0] = hz;
            count = 1;
        }
    }
    return count;
}

// Addresses of the operands that read the game's resolution table (AdjustRes: 4, GetResolution: 2).
bool FindTableReads(std::vector<uintptr_t>& operands)
{
    const struct
    {
        uintptr_t function;
        int expected;
    } readers[] = {{addr::GuiAdjustRes, 4}, {addr::GuiGetResolutionText, 2}};
    const uintptr_t table = addr::ResolutionTable;
    for (const auto& r : readers)
    {
        int found = 0;
        for (uintptr_t p = r.function; p < r.function + 0x100 && found < r.expected; ++p)
        {
            const uint32_t v = *reinterpret_cast<const uint32_t*>(p);
            if (v == table || v == table + 4)
            {
                operands.push_back(p);
                ++found;
                p += 3;
            }
        }
        if (found != r.expected)
        {
            Log("Display: resolution table reads not recognised (%d of %d at %08X)", found, r.expected,
                static_cast<unsigned>(r.function));
            return false;
        }
    }
    return true;
}

bool InstallResolutionList()
{
    std::vector<uintptr_t> operands;
    if (!FindTableReads(operands))
        return false;
    // Until the device is known: the game's own table.
    std::memcpy(g_table, reinterpret_cast<const void*>(addr::ResolutionTable), kGameTableSize * 2 * sizeof(int32_t));
    g_count = kGameTableSize;

    const HookSpec hook = {"GetCurrentRes", addr::GuiGetCurrentRes, reinterpret_cast<void*>(&hk_GetCurrentRes),
                           reinterpret_cast<void**>(&o_GetCurrentRes)};
    if (!InstallHook(hook))
        return false;
    const uintptr_t table = addr::ResolutionTable;
    bool ok = true;
    for (const uintptr_t p : operands)
    {
        const uint32_t ours = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(g_table)) +
                              (*reinterpret_cast<const uint32_t*>(p) - static_cast<uint32_t>(table));
        ok = WriteCode(p, &ours, sizeof(ours)) && ok;
    }
    return ok;
}
} // namespace

bool InstallDisplay(const DisplayConfig& cfg)
{
    g_cfg = cfg;
    if (!AddressesFound("Display", {&addr::CreateOrResetDevice, &addr::FindModeRefresh, &addr::ListRefreshRates}))
        return false;
    if (g_cfg.resolutionList &&
        !AddressesFound("Display (resolution list)", {&addr::GuiAdjustRes, &addr::GuiGetCurrentRes,
                                                      &addr::GuiGetResolutionText, &addr::ResolutionTable}))
        g_cfg.resolutionList = false;

    // Before the game sets its own mode: the desktop's.
    DEVMODEW dm = {};
    dm.dmSize = sizeof(dm);
    if (EnumDisplaySettingsW(nullptr, ENUM_CURRENT_SETTINGS, &dm) || EnumDisplaySettingsW(nullptr, ENUM_REGISTRY_SETTINGS, &dm))
    {
        g_desktopWidth = dm.dmPelsWidth;
        g_desktopHeight = dm.dmPelsHeight;
        g_desktopRefresh = dm.dmDisplayFrequency;
    }

    const HookSpec hooks[] = {
        {"CreateOrResetDevice", addr::CreateOrResetDevice, reinterpret_cast<void*>(&hk_CreateOrResetDevice),
         reinterpret_cast<void**>(&o_CreateOrResetDevice)},
        {"FindModeRefresh", addr::FindModeRefresh, reinterpret_cast<void*>(&hk_FindModeRefresh),
         reinterpret_cast<void**>(&o_FindModeRefresh)},
        {"ListRefreshRates", addr::ListRefreshRates, reinterpret_cast<void*>(&hk_ListRefreshRates),
         reinterpret_cast<void**>(&o_ListRefreshRates)},
    };
    bool ok = HooksInit();
    for (const auto& h : hooks)
        ok = ok && InstallHook(h);
    const bool list = ok && g_cfg.resolutionList && InstallResolutionList();

    char refresh[32] = "game default";
    if (g_cfg.matchRefresh && g_cfg.refreshRate > 0)
        wsprintfA(refresh, "%d Hz", g_cfg.refreshRate);
    else if (g_cfg.matchRefresh)
        wsprintfA(refresh, "as Windows (%u Hz)", g_desktopRefresh);
    Log("Display: %s (desktop %ux%u @ %u Hz; resolution list %s; refresh rate %s)",
        ok ? "installed" : "PARTIALLY installed", g_desktopWidth, g_desktopHeight, g_desktopRefresh,
        list ? "on" : "off", refresh);
    return ok;
}
