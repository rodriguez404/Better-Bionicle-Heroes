// Keeps two Piraka (the zone boss and Vezon) loaded at once so slot 6 can switch between them
// instantly instead of reloading a model (~38 ms) on every switch.
//
// The second model is loaded into a buffer of our own, as large as the game's Piraka buffer
// (PIRAKAMEM_PC, 1.5 MB; boss + Vezon do not always fit into one): the active Piraka's handles are
// hidden from LoadPirakaModel so it frees nothing, and the toy's buffer start/cursor/end point at our
// buffer during that load. A memory diff around
// that load gives every dword a load sets — all of them live in the character manager (0x4100
// bytes) or the global material table (shared Piraka body materials get the textures of the
// loaded Piraka). Switching writes the recorded values of the other Piraka, which is exactly what a
// fresh load would leave behind, minus the loading. Verified by a full .data diff: everything else
// a load touches is allocator/IO bookkeeping.

#include "piraka_cache.h"

#include "game.h"
#include "log.h"

#include <windows.h>

#include <cstring>
#include <vector>

using namespace game;

namespace
{
constexpr uintptr_t kMgrSize = 0x4100;              // allocated at 0x4ac35a
const uintptr_t& kMaterialTable = addr::MaterialTable;        // 0x33a2be0: 0x708 entries of 0x214 bytes
const uintptr_t& kMaterialTableEnd = addr::MaterialTableEnd;  // 0x348c880   (allocator 0x5417d0)
constexpr uintptr_t kMaterialSize = 0x214;          // dword 0 bit 0 = entry in use

// Manager fields LoadPirakaModel frees before loading (hidden so the active Piraka survives).
constexpr uintptr_t kToyStart = 0x14;               // toy[1]: buffer start
constexpr uintptr_t kToyCursor = 0x18;              // toy[2]
constexpr uintptr_t kToyEnd = 0x1c;                 // toy[3]
// Character model record of slot 6 (mgr+0x3e20 + 4 + 6*0x2c): [0] own base (0 = use the toy
// buffer), [1] cursor, [2] end — its own copy of the buffer end, used by 0x423ef0.
constexpr uintptr_t kCharRecEnd = 0x3f34;
constexpr uintptr_t kHeldHandles[] = {
    0x4cc,   // toy+0x4bc material scene
    0x3cf8,  // spine (mgr+0x3bd0) +0x128 instance
    0x3d30,  // spine +0x160 loaded descriptor
    0x3f38,  // character model record (mgr+0x3e20) +0x118 instance
    0x3f40,  // character model record +0x120 loaded descriptor
    0x3074,  // face (mgr+0x3070) +4 instance
    0x307c,  // face +0xc loaded descriptor
};

using LoadPirakaModel_t = int(__thiscall*)(void* toy, int charId);
using ReleaseScene_t = void(__cdecl*)(void* sceneHandleSlot);
using SubsystemFree_t = int(__thiscall*)(void* subsystem);
const GameFn<LoadPirakaModel_t> LoadPirakaModel{kLoadPirakaModel};
const GameFn<ReleaseScene_t> ReleaseScene{addr::ReleaseScene};                // 0x4a16b0
const GameFn<SubsystemFree_t> FreeSpine{addr::FreeSpine};                      // 0x424540
const GameFn<SubsystemFree_t> FreeCharacterModel{addr::FreeCharacterModel};    // 0x424200
const GameFn<SubsystemFree_t> FreeFace{addr::FreeFace};                        // 0x4249d0

struct Cache
{
    bool ready = false;
    int ids[2] = {-1, -1};                   // [0] loaded first (boss), [1] preloaded (Vezon)
    std::vector<uint32_t*> addresses;        // dwords a load sets
    std::vector<uint32_t> values[2];         // their values with ids[i] loaded
} g_cache;

struct Region
{
    uintptr_t base;
    size_t size;
};

std::vector<uint32_t> Snapshot(const Region* regions, int count)
{
    std::vector<uint32_t> out;
    for (int r = 0; r < count; ++r)
    {
        const auto* p = reinterpret_cast<const uint32_t*>(regions[r].base);
        out.insert(out.end(), p, p + regions[r].size / 4);
    }
    return out;
}

void Write(const std::vector<uint32_t>& values)
{
    for (size_t i = 0; i < g_cache.addresses.size(); ++i)
        *g_cache.addresses[i] = values[i];
}

std::vector<uint32_t> ReadCurrent()
{
    std::vector<uint32_t> out(g_cache.addresses.size());
    for (size_t i = 0; i < out.size(); ++i)
        out[i] = *g_cache.addresses[i];
    return out;
}

// What LoadPirakaModel does before loading: release everything of the loaded Piraka.
void FreeLoadedPiraka(uintptr_t mgr)
{
    ReleaseScene(reinterpret_cast<void*>(mgr + 0x4cc));
    const int loaded = At<int>(mgr, kMgrLoadedPiraka);
    if (loaded >= 0 && loaded < kCharNone)
        At<uint32_t>(mgr, 0x1ed4 + loaded * 4) = 0;  // toy+0x1ec4[id]: material instance
    FreeSpine(reinterpret_cast<void*>(mgr + 0x3bd0));
    FreeCharacterModel(reinterpret_cast<void*>(mgr + 0x3e20));
    FreeFace(reinterpret_cast<void*>(mgr + 0x3070));
}
} // namespace

bool PirakaCacheReady()
{
    return g_cache.ready;
}

bool PirakaCacheHas(int charId)
{
    return g_cache.ready && (g_cache.ids[0] == charId || g_cache.ids[1] == charId);
}

bool PirakaCachePreload(int charId)
{
    PirakaCacheRelease();
    const uintptr_t mgr = CharMgr();
    if (!mgr || At<int>(mgr, kMgrToyBusy) != 0)
        return false;
    const int active = At<int>(mgr, kMgrLoadedPiraka);
    if (active < kCharHakann || active > kCharVezon || active == charId)
        return false;

    const uint32_t start = At<uint32_t>(mgr, kToyStart);
    const uint32_t end = At<uint32_t>(mgr, kToyEnd);
    static uint8_t* second = nullptr;
    static uint32_t secondSize = 0;
    if (!second)
    {
        secondSize = end - start;
        second = static_cast<uint8_t*>(VirtualAlloc(nullptr, secondSize, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
        if (!second)
        {
            Log("cache: cannot allocate a second Piraka buffer (%X bytes)", secondSize);
            return false;
        }
    }
    const auto secondStart = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(second));

    const Region regions[] = {{mgr, kMgrSize}, {kMaterialTable, kMaterialTableEnd - kMaterialTable}};
    const std::vector<uint32_t> before = Snapshot(regions, 2);

    for (uintptr_t field : kHeldHandles)
        At<uint32_t>(mgr, field) = 0;
    const uint32_t recEnd = At<uint32_t>(mgr, kCharRecEnd);
    At<uint32_t>(mgr, kToyStart) = secondStart;
    At<uint32_t>(mgr, kToyEnd) = secondStart + secondSize;
    At<uint32_t>(mgr, kCharRecEnd) = secondStart + secondSize;
    const DWORD t0 = GetTickCount();
    const int ok = LoadPirakaModel(reinterpret_cast<void*>(mgr + kMgrToy), charId);
    const DWORD loadMs = GetTickCount() - t0;
    const uint32_t secondUsed = At<uint32_t>(mgr, kToyCursor) - secondStart;
    At<uint32_t>(mgr, kToyStart) = start;
    At<uint32_t>(mgr, kToyEnd) = end;
    At<uint32_t>(mgr, kCharRecEnd) = recEnd;
    const std::vector<uint32_t> after = Snapshot(regions, 2);

    // Collect the changed dwords (buffer start/end were ours only for the load and stay real).
    // Material entries the second model allocated stay allocated (its scene keeps using them); only
    // fields of materials in use before and after (the shared Piraka body) take part in switching.
    g_cache.addresses.clear();
    g_cache.values[0].clear();
    g_cache.values[1].clear();
    const size_t mgrDwords = kMgrSize / 4;
    unsigned newMaterials = 0;
    size_t at = 0;
    for (const Region& r : regions)
    {
        for (size_t i = 0; i < r.size / 4; ++i, ++at)
        {
            const uintptr_t address = r.base + i * 4;
            if (before[at] == after[at] || address == mgr + kToyStart || address == mgr + kToyEnd ||
                address == mgr + kCharRecEnd)
                continue;
            if (r.base == kMaterialTable)
            {
                const size_t entryStart = mgrDwords + (i / (kMaterialSize / 4)) * (kMaterialSize / 4);
                if ((before[entryStart] & 1) == 0 && (after[entryStart] & 1) != 0)
                {
                    if (i % (kMaterialSize / 4) == 0)
                        ++newMaterials;
                    continue;
                }
            }
            g_cache.addresses.push_back(reinterpret_cast<uint32_t*>(address));
            g_cache.values[0].push_back(before[at]);
            g_cache.values[1].push_back(after[at]);
        }
    }
    Write(g_cache.values[0]);  // back to the first Piraka, second one stays resident

    if (ok != 1 || At<int>(mgr, kMgrLoadedPiraka) != active)
    {
        Log("cache: preload of char %d failed (ret %d)", charId, ok);
        g_cache = Cache{};
        return false;
    }
    g_cache.ids[0] = active;
    g_cache.ids[1] = charId;
    g_cache.ready = true;
    Log("cache: char %d preloaded next to char %d in %lu ms (%u dwords tracked, %u new materials kept; second "
        "buffer %X of %X used)",
        charId, active, loadMs, static_cast<unsigned>(g_cache.addresses.size()), newMaterials, secondUsed, secondSize);
    return true;
}

bool PirakaCacheSwitch(int charId)
{
    if (!PirakaCacheHas(charId))
        return false;
    const uintptr_t mgr = CharMgr();
    if (At<int>(mgr, kMgrLoadedPiraka) == charId)
        return true;
    Write(g_cache.values[g_cache.ids[0] == charId ? 0 : 1]);
    return At<int>(mgr, kMgrLoadedPiraka) == charId;
}

void PirakaCacheRelease()
{
    if (!g_cache.ready)
        return;
    const uintptr_t mgr = CharMgr();
    if (mgr)
    {
        const int loaded = At<int>(mgr, kMgrLoadedPiraka);
        const int inactive = g_cache.ids[0] == loaded ? 1 : 0;
        const std::vector<uint32_t> current = ReadCurrent();
        Write(g_cache.values[inactive]);
        FreeLoadedPiraka(mgr);
        Write(current);
        Log("cache: released char %d (kept char %d)", g_cache.ids[inactive], loaded);
    }
    g_cache = Cache{};
}
