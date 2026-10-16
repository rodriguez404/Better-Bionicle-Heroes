// Vezon as an 8th character.
//
// Vanilla: after Vezon is beaten, every zone level puts Vezon in the single Piraka slot (slot 6)
// instead of that zone's boss, and Vezon passes every boss-only construction because those only
// check "player is in slot 6".
//
// Here: the zone boss keeps slot 6 and Vezon becomes an extra stop in the character ring right
// after the boss (... X -> boss -> Vezon -> Y ...), with his own icon in an 8-entry wheel. The game
// still has 7 slots; slot 6 changes identity between the two Piraka, which are both kept loaded
// (piraka_cache.cpp; reloading the model on demand is the fallback). While the boss is in the
// ring, Vezon is denied boss constructions (types 6 and 8-13) and keeps his own (type 14).
// The game's mask lock for slot 6 is shared too, so a death of one of them is tracked here: the dead
// one leaves the ring and the other one keeps slot 6.

#include "vezon_extra.h"

#include "game.h"
#include "hooks.h"
#include "log.h"
#include "mem.h"
#include "piraka_cache.h"

#include <windows.h>

#include <cmath>

using namespace game;

namespace
{
VezonExtraConfig g_cfg;

// Masks lost by the boss / Vezon (the game's lock of slot 6 stays 0 while one of them is left), and
// the Piraka whose death is running while the other one is still there (-1 none).
bool g_lostBoss = false;
bool g_lostVezon = false;
int g_dying = -1;

GetLevelPirakaCharId_t o_GetLevelPirakaCharId = nullptr;
PlayerSwitchInput_t o_PlayerSwitchInput = nullptr;
PlayerInitSlotChars_t o_PlayerInitSlotChars = nullptr;
ConstructionUpdate_t o_ConstructionInteract = nullptr;
ConstructionUpdate_t o_ConstructionBuilding = nullptr;
LevelLoadConfig_t o_LevelLoadConfig = nullptr;

const GameFn<ZoneToPirakaCharId_t> ZoneToPirakaCharId{kZoneToPirakaCharId};
const GameFn<LoadPirakaModel_t> LoadPirakaModel{kLoadPirakaModel};
const GameFn<VisualReset_t> VisualReset{kVisualReset};
const GameFn<VisualSetup_t> VisualSetup{kVisualSetup};
const GameFn<PlayerSetSlot_t> PlayerSetSlot{kPlayerSetSlot};
const GameFn<PlayerPartyStep_t> PartyNext{kPartyNext};
const GameFn<PlayerPartyStep_t> PartyPrev{kPartyPrev};
const GameFn<SaveUnlockCheck_t> SaveIsPirakaUnlocked{kSaveIsPirakaUnlocked};
const GameFn<SaveUnlockCheck_t> SaveIsPirakaUnlocked2{kSaveIsPirakaUnlocked2};
const GameFn<SwitchFx_t> SwitchFx{kSwitchFx};
const GameFn<PlaySound_t> PlaySound{kPlaySound};
const GameFn<IsSwitchBlocked_t> IsSwitchBlocked{kIsSwitchBlocked};

bool IsZoneBoss(int charId) { return charId >= kCharHakann && charId <= kCharReidak; }

// Zone boss of a level (zones 3..8), or -1. `level` = -1 means the current level.
int ZoneBossForLevel(int level)
{
    const uintptr_t levels = *reinterpret_cast<uintptr_t*>(kLevelsPtr);
    if (!levels)
        return -1;
    if (level == -1)
        level = At<int>(levels, 0xc);
    const int zone = At<int>(levels, 0xf38 + level * 4);
    if (zone < 3 || zone > 8)
        return -1;
    return ZoneToPirakaCharId(zone);
}

bool VezonUnlocked()
{
    return SaveIsPirakaUnlocked(reinterpret_cast<void*>(kSaveObject), kCharVezon - kCharHakann) != 0;
}

int LoadedPiraka()
{
    const uintptr_t mgr = CharMgr();
    return mgr ? At<int>(mgr, kMgrLoadedPiraka) : -1;
}

bool PirakaLost(int charId)
{
    return charId == kCharVezon ? g_lostVezon : g_lostBoss;
}

void ResetLostMasks(const char* why)
{
    if (g_lostBoss || g_lostVezon || g_dying >= 0)
        Log("VezonExtra: boss and Vezon masks back (%s)", why);
    g_lostBoss = g_lostVezon = false;
    g_dying = -1;
}

// Vezon is offered as the extra character in this level right now.
bool ExtraActive(uintptr_t player, int& bossId)
{
    bossId = ZoneBossForLevel(-1);
    if (!IsZoneBoss(bossId) || !VezonUnlocked() || !CharMgr())
        return false;
    if (At<float>(player, kPlayerSlotLock + kPirakaSlot * 4) != 0.0f)
        return false;
    for (int i = 0; i < kSlotCount; ++i)
        if (At<int>(player, kPlayerParty + i * 4) == kPirakaSlot)
            return true;
    return false;
}

void RebuildSlot6(uintptr_t player, int charId)
{
    At<int>(player, kPlayerSlotChar + kPirakaSlot * 4) = charId;
    if (void* visual = At<void*>(player, kPlayerSlotVisual + kPirakaSlot * 4))
    {
        VisualReset(visual);
        VisualSetup(visual, CharVisualDescriptor(charId));
    }
}

// Makes `charId` the Piraka of slot 6: instantly from the two-Piraka cache, otherwise by reloading
// the model (~40 ms). Only safe while the player is NOT currently in slot 6.
bool LoadSlot6Character(uintptr_t player, int charId)
{
    const uintptr_t mgr = CharMgr();
    if (At<int>(mgr, kMgrLoadedPiraka) == charId)
        return true;
    if (At<int>(mgr, kMgrToyBusy) != 0)
    {
        Log("slot6 load %d skipped: Piraka loader busy", charId);
        return false;
    }
    if (PirakaCacheHas(charId))
    {
        if (!PirakaCacheSwitch(charId))
        {
            Log("cache: switch to char %d failed", charId);
            return false;
        }
        RebuildSlot6(player, charId);
        return true;
    }
    // Not cached: a real reload resets the Piraka buffer, so drop the cached second model first.
    PirakaCacheRelease();
    const DWORD t0 = GetTickCount();
    if (LoadPirakaModel(reinterpret_cast<void*>(mgr + kMgrToy), charId) != 1)
    {
        Log("LoadPirakaModel(%d) failed", charId);
        return false;
    }
    RebuildSlot6(player, charId);
    Log("slot6 -> char %d reloaded (%lu ms)", charId, GetTickCount() - t0);
    return true;
}

// Boss <-> Vezon while standing in slot 6: step off to a neighbour while slot 6 changes, then come
// back, all within one frame.
void SwapInPlace(uintptr_t player, int charId)
{
    const int neighbour = At<int>(player, kPlayerParty + 4);
    const bool stepOff = neighbour >= 0 && neighbour != kPirakaSlot;
    if (stepOff)
        PlayerSetSlot(reinterpret_cast<void*>(player), neighbour);
    const bool loaded = LoadSlot6Character(player, charId);
    PlayerSetSlot(reinterpret_cast<void*>(player), kPirakaSlot);
    if (!loaded)
        return;
    if (void* visual = At<void*>(player, kPlayerSlotVisual + kPirakaSlot * 4))
        SwitchFx(visual);
    PlaySound(At<void*>(player, kPlayerSwitchSound), 0);
}

// Mirrors the gates at the top of PlayerSwitchInput (0x496220).
bool SwitchAllowed(uintptr_t player)
{
    if (IsSwitchBlocked() != 0)
        return false;
    if ((At<uint32_t>(player, kPlayerFlags690) & 0x1c) != 0)
        return false;
    if (At<int>(player, kPlayerSwitchCooldown) != 0)
        return false;
    const int state = At<int>(CharMgr(), kMgrSwitchState);
    return state < 0x16 || state > 0x19;
}

// Bit 0 doubles as "switch to the mask the HUD hint asks for" (HUD+0xb0) unless the hint was just used.
int HintSwitchSlot(uintptr_t player)
{
    const uintptr_t hud = At<uintptr_t>(0, kHudPtr);
    if (!hud || At<int16_t>(hud, kHudHintSlot) == -1 || At<float>(player, kPlayerHintCooldown) != 0.0f)
        return -1;
    return At<int16_t>(hud, kHudHintSlot);
}

// Which Piraka the HUD's "switch to the needed mask" prompt wants when it points at slot 6.
int HintWantedPiraka(int bossId)
{
    const uintptr_t hud = At<uintptr_t>(0, kHudPtr);
    if (hud && At<int16_t>(hud, kHudHintType) == kCharVezon + 2)
        return kCharVezon;
    return bossId;
}

// --- hooks -------------------------------------------------------------------------------------

int __cdecl hk_GetLevelPirakaCharId(int level)
{
    const int id = o_GetLevelPirakaCharId(level);
    if (id == kCharVezon && g_cfg.extraCharacter)
    {
        const int boss = ZoneBossForLevel(level);
        if (IsZoneBoss(boss))
            return boss;
    }
    return id;
}

// Replaces the two "Vezon unlocked?" calls inside Level_LoadConfig: in a zone level the zone boss
// is loaded, elsewhere vanilla behaviour stays.
int __fastcall hk_LoadCfgVezonCheck1(void* save, void*, int index)
{
    // First of the two calls during every Level_LoadConfig: a breadcrumb for load-time hangs.
    if (const uintptr_t levels = *reinterpret_cast<uintptr_t*>(kLevelsPtr))
        Log("level load: level %d, zone boss %d, loaded piraka %d", At<int>(levels, 0xc), ZoneBossForLevel(-1),
            LoadedPiraka());
    if (g_cfg.extraCharacter && IsZoneBoss(ZoneBossForLevel(-1)))
        return 0;
    return SaveIsPirakaUnlocked2(save, index);
}

int __fastcall hk_LoadCfgVezonCheck2(void* save, void*, int index)
{
    if (g_cfg.extraCharacter && IsZoneBoss(ZoneBossForLevel(-1)))
        return 0;
    return SaveIsPirakaUnlocked(save, index);
}

void WheelInvalidate();

// Level loads: drop the second Piraka before the game possibly reloads the buffer, and preload
// Vezon behind the zone boss afterwards.
int __cdecl hk_LevelLoadConfig(int level, void* arena, void* extra)
{
    PirakaCacheRelease();
    WheelInvalidate();  // a new level starts with a fresh wheel layout
    ResetLostMasks("level load");
    const int result = o_LevelLoadConfig(level, arena, extra);
    const int boss = ZoneBossForLevel(-1);
    if (g_cfg.extraCharacter && g_cfg.instantSwap && IsZoneBoss(boss) && VezonUnlocked() && LoadedPiraka() == boss)
        PirakaCachePreload(kCharVezon);
    return result;
}

// Keeps slot 6's char id in sync with whatever is actually in the Piraka buffer (respawns etc.).
void __fastcall hk_PlayerInitSlotChars(void* player, void*)
{
    o_PlayerInitSlotChars(player);
    const auto p = reinterpret_cast<uintptr_t>(player);
    int& slotChar = At<int>(p, kPlayerSlotChar + kPirakaSlot * 4);
    const int loaded = LoadedPiraka();
    if (g_cfg.extraCharacter && slotChar != loaded && IsZoneBoss(slotChar) && loaded == kCharVezon &&
        VezonUnlocked())
    {
        Log("InitSlotChars: slot6 %d -> %d (Vezon loaded)", slotChar, loaded);
        slotChar = loaded;
    }
}

void __fastcall hk_PlayerSwitchInput(void* playerPtr, void*, void* pad)
{
    const auto player = reinterpret_cast<uintptr_t>(playerPtr);
    int bossId = -1;
    if (!g_cfg.extraCharacter || !pad || g_dying >= 0 || !ExtraActive(player, bossId) || !SwitchAllowed(player))
        return o_PlayerSwitchInput(playerPtr, pad);

    auto& pressed = At<uint8_t>(reinterpret_cast<uintptr_t>(pad), kPadPressed);
    const int slot = At<int>(player, kPlayerSlot);
    const int loaded = LoadedPiraka();
    const int hintSlot = (pressed & 1) ? HintSwitchSlot(player) : -1;

    // "Switch to the needed mask" pointing at slot 6: pick the boss or Vezon by construction type.
    if (hintSlot == kPirakaSlot)
    {
        const int wanted = HintWantedPiraka(bossId);
        if (slot != kPirakaSlot)
        {
            if (loaded != wanted)
                LoadSlot6Character(player, wanted);
        }
        else if (loaded != wanted)
        {
            SwapInPlace(player, wanted);
            const uint8_t saved = pressed;
            pressed &= ~0x07;
            o_PlayerSwitchInput(playerPtr, pad);
            pressed = saved;
            return;
        }
        return o_PlayerSwitchInput(playerPtr, pad);
    }

    const bool next = (pressed & 2) || ((pressed & 1) && hintSlot == -1);
    const bool prev = (pressed & 4) != 0;
    if (next == prev)
        return o_PlayerSwitchInput(playerPtr, pad);

    if (slot == kPirakaSlot)
    {
        const bool toVezon = next && loaded != kCharVezon && !g_lostVezon;
        const bool toBoss = prev && loaded == kCharVezon && !g_lostBoss;
        if (toVezon || toBoss)
        {
            SwapInPlace(player, toVezon ? kCharVezon : bossId);
            const uint8_t saved = pressed;
            pressed &= ~0x07;
            o_PlayerSwitchInput(playerPtr, pad);
            pressed = saved;
            return;
        }
        return o_PlayerSwitchInput(playerPtr, pad);
    }

    // Entering slot 6 from the side: next lands on the boss, previous lands on Vezon (or on the one
    // whose mask is not lost).
    const int target = next ? PartyNext(playerPtr) : PartyPrev(playerPtr);
    if (target == kPirakaSlot)
    {
        int wanted = next ? bossId : kCharVezon;
        if (PirakaLost(wanted))
            wanted = wanted == kCharVezon ? bossId : kCharVezon;
        if (loaded != wanted)
            LoadSlot6Character(player, wanted);
    }
    o_PlayerSwitchInput(playerPtr, pad);
}

// Vezon may not use boss constructions (types 6 and 8-13) while the boss shares the ring with him.
bool DenyTypeForVezon(int type)
{
    const uintptr_t player = Player();
    // Without the extra character Vezon replaces the boss and must keep the boss powers (vanilla
    // design: otherwise boss constructions would be unreachable once Vezon is unlocked).
    if (!g_cfg.extraCharacter || !g_cfg.restrictVezon || !player || At<int>(player, kPlayerSlot) != kPirakaSlot)
        return false;
    if (LoadedPiraka() != kCharVezon)
        return false;
    if (type != kPirakaSlot && (type < kCharHakann + 2 || type > kCharReidak + 2))
        return false;
    // Only where the zone boss is actually offered next to Vezon (not in the hub or other levels).
    return IsZoneBoss(ZoneBossForLevel(-1));
}

bool DenyForVezon(const uint8_t* piece)
{
    return DenyTypeForVezon(static_cast<int8_t>(piece[kPieceType]));
}

// The interaction hint (gears = "you can use this") in the HUD update 0x466af0 compares the
// player's slot with the construction type at two places; both are redirected here.
int __cdecl HintSlotMatches(int type)
{
    const uintptr_t player = Player();
    const int slot = player ? At<int>(player, kPlayerSlot) : -1;
    const bool matches = type < 7 ? slot == type : slot == kPirakaSlot;
    return matches && !DenyTypeForVezon(type) ? 1 : 0;
}

// 0x466ca5 (13 bytes): mov ecx,[edx+0x12a4]; xor ebx,ebx; cmp ecx,ebp; sete bl   (type in ebp)
__declspec(naked) void HintCheckLowTypes()
{
    __asm {
        push eax
        push ecx
        push edx
        push ebp
        call HintSlotMatches
        add esp, 4
        mov ebx, eax
        pop edx
        pop ecx
        pop eax
        ret
    }
}

// 0x466d70 (14 bytes): mov eax,[ecx+0x12a4]; xor ebx,ebx; cmp eax,6; sete bl   (type at caller [esp+0x10])
__declspec(naked) void HintCheckBossTypes()
{
    __asm {
        push eax
        push ecx
        push edx
        mov eax, [esp + 0x20]  // caller's [esp+0x10]: +4 return address, +0xc saved registers
        push eax
        call HintSlotMatches
        add esp, 4
        mov ebx, eax
        pop edx
        pop ecx
        pop eax
        ret
    }
}

void __fastcall hk_ConstructionInteract(void* self, void*, void* group, uint8_t* piece, void* pad)
{
    if (!piece || !DenyForVezon(piece))
        return o_ConstructionInteract(self, group, piece, pad);
    const uint8_t type = piece[kPieceType];
    piece[kPieceType] = 0;  // a Toa colour that slot 6 can never match
    o_ConstructionInteract(self, group, piece, pad);
    piece[kPieceType] = type;
}

void __fastcall hk_ConstructionBuilding(void* self, void*, void* group, uint8_t* piece, void* pad)
{
    if (!piece || !DenyForVezon(piece))
        return o_ConstructionBuilding(self, group, piece, pad);
    const uint8_t type = piece[kPieceType];
    piece[kPieceType] = 0;
    o_ConstructionBuilding(self, group, piece, pad);
    piece[kPieceType] = type;
}

// --- 8-entry character wheel -------------------------------------------------------------------
//
// Logical entries: 0-5 Toa (= slot), 6 zone boss, 7 Vezon. Appear/disappear state and its timer are
// copied from the game's per-slot wheel data (boss and Vezon both follow slot 6); positions, the
// strip rotation and icon scales are kept here and follow the same rules as the game's
// FUN_00493080 (layout) and DrawMaskIcons (0x457550), just over up to 8 entries.

constexpr int kWheelEntries = 8;
constexpr int kEntryBoss = 6;
constexpr int kEntryVezon = 7;

struct WheelEntry
{
    int state = 0;      // 0 hidden, 1 shown, 2 appearing, -1 disappearing
    float t = 0.0f;     // appear/disappear timer
    float pos = 0.0f;
    float scale = 1.0f;
};

struct Wheel
{
    bool active = false;
    WheelEntry e[kWheelEntries];
    float rot = 0.0f;
    int ring[kWheelEntries] = {};
    int count = 0;
    int prevRing[kWheelEntries] = {};
    int prevCount = 0;
    int lastCurrent = -1;
    bool iconWarned = false;
} g_wheel;

void WheelInvalidate()
{
    g_wheel.active = false;
}

DrawMaskIcons_t o_DrawMaskIcons = nullptr;
const GameFn<DrawSprite_t> DrawSprite{kDrawSprite};
const GameFn<SlotSaveVariant_t> SlotSaveVariant{kSlotSaveVariant};

// Ring order starting at the current character: ... X -> boss -> Vezon -> Y ... (without a Piraka
// whose mask is lost).
int BuildRing(uintptr_t player, int* ring)
{
    const bool vezonCurrent = LoadedPiraka() == kCharVezon;
    int n = 0;
    int tailBoss = 0;
    for (int i = 0; i < kSlotCount; ++i)
    {
        const int slot = At<int>(player, kPlayerParty + i * 4);
        if (slot < 0)
            continue;
        if (slot != kPirakaSlot)
            ring[n++] = slot;
        else if (i == 0 && vezonCurrent)
        {
            ring[n++] = kEntryVezon;
            tailBoss = !g_lostBoss;  // the boss sits just before Vezon, i.e. at the end of the ring
        }
        else
        {
            if (!g_lostBoss)
                ring[n++] = kEntryBoss;
            if (!g_lostVezon)
                ring[n++] = kEntryVezon;
        }
    }
    if (tailBoss)
        ring[n++] = kEntryBoss;
    return n;
}

int CurrentEntry(uintptr_t player)
{
    const int slot = At<int>(player, kPlayerSlot);
    if (slot != kPirakaSlot)
        return slot;
    return LoadedPiraka() == kCharVezon ? kEntryVezon : kEntryBoss;
}

uintptr_t GameWheelSlot(uintptr_t player, int slot)
{
    return player + kPlayerWheel + slot * kPlayerWheelStride;
}

void WheelReset(uintptr_t player)
{
    Wheel& w = g_wheel;
    w = Wheel{};
    w.active = true;
    w.count = BuildRing(player, w.ring);
    for (int i = 0; i < kWheelEntries; ++i)
        w.e[i].scale = At<float>(GameWheelSlot(player, i < kPirakaSlot ? i : kPirakaSlot), 0x14);
    for (int i = 0; i < w.count; ++i)
        w.e[w.ring[i]].pos = static_cast<float>(i);
    for (int i = 0; i < w.count; ++i)
        w.prevRing[i] = w.ring[i];
    w.prevCount = w.count;
}

// FUN_00493080's position rules over the logical ring.
void WheelLayout(uintptr_t player, int bossId)
{
    Wheel& w = g_wheel;
    for (int i = 0; i < kWheelEntries; ++i)
    {
        const uintptr_t src = GameWheelSlot(player, i < kPirakaSlot ? i : kPirakaSlot);
        w.e[i].state = At<int>(src, 4);
        w.e[i].t = At<float>(src, 0xc);
    }
    // Boss and Vezon follow slot 6, except one whose mask is lost (gone) and, while one of them dies,
    // the other one (stays).
    for (int i = kEntryBoss; i <= kEntryVezon; ++i)
    {
        const int charId = i == kEntryVezon ? kCharVezon : bossId;
        if (PirakaLost(charId) || (g_dying >= 0 && g_dying != charId))
        {
            w.e[i].state = PirakaLost(charId) ? 0 : 1;
            w.e[i].t = 0.0f;
        }
    }
    w.count = BuildRing(player, w.ring);
    const int n = w.count;

    int first = -1;
    for (int i = 0; i < n; ++i)
        if (w.e[w.ring[i]].state != 0)
        {
            first = i;
            break;
        }
    int index = 0;
    if (first >= 0)
    {
        WheelEntry& f = w.e[w.ring[first]];
        if (n > w.prevCount && n > 1)
        {
            f.pos = w.e[w.ring[(first + 1) % n]].pos - 1.0f;
            w.rot -= 1.0f;
        }
        else if (n < w.prevCount)
        {
            f.pos = 0.0f;
            w.rot = 0.0f;
        }
        index = static_cast<int>(f.pos);
    }
    if (w.prevCount >= n)
    {
        const bool steppedBack = n > 2 && w.prevRing[n - 1] == w.ring[0];
        for (int i = 0; i < n; ++i, ++index)
            w.e[w.ring[i]].pos = static_cast<float>(steppedBack ? index - n : index);
    }
    if (n > 0)
        w.rot += (w.e[w.ring[0]].pos - w.rot) * At<float>(0, kSmoothRate);
    for (int i = 0; i < n; ++i)
        w.prevRing[i] = w.ring[i];
    w.prevCount = n;
}

void* SpriteTexture(uintptr_t sprite)
{
    const uintptr_t image = At<uintptr_t>(sprite, 0x1c);
    return image ? At<void*>(image, 0x5c) : nullptr;
}

// DrawMaskIcons (0x457550) over the logical entries.
void WheelDraw(uintptr_t player, int bossId)
{
    Wheel& w = g_wheel;
    void* gui = At<void*>(0, kGuiPtr);
    const float dt = At<float>(0, kFrameDt);
    const float rate = At<float>(0, kSmoothRate);
    float& pulse = At<float>(0, kWheelPulse);
    const float originX = At<float>(0, kWheelX);
    const float originY = At<float>(0, kWheelY);
    const float* sinTable = reinterpret_cast<const float*>(kSinTable);

    int forced = -1;
    if (const uintptr_t hud = At<uintptr_t>(0, kHudPtr))
        forced = At<int16_t>(hud, kHudHintSlot);
    if (forced == kPirakaSlot)
        forced = HintWantedPiraka(bossId) == kCharVezon ? kEntryVezon : kEntryBoss;

    pulse += dt * 1.5f;
    if (pulse > 1.0f)
        pulse = std::fmod(pulse, 1.0f);

    int shown = 0;
    for (int i = 0; i < kWheelEntries; ++i)
    {
        if (w.e[i].state != 0)
            ++shown;
        w.e[i].scale += ((i == forced ? 1.25f : 1.0f) - w.e[i].scale) * rate;
    }
    At<int>(0, kWheelCount) = shown;
    At<int>(0, kWheelCountCopy) = shown;

    struct Marker
    {
        float x, y, size;
        uint32_t color;
    } markers[kWheelEntries];
    int markerCount = 0;

    const int current = CurrentEntry(player);
    const float n = static_cast<float>(shown);
    const float edge = n - 0.5f;
    for (int i = 0; i < kWheelEntries; ++i)
    {
        WheelEntry& e = w.e[i];
        if (e.state == 0)
            continue;

        uintptr_t sprite = 0;
        int upgraded = 0;
        if (i < kPirakaSlot)
        {
            sprite = At<uintptr_t>(kToaIcons, i * 4);
            upgraded = SlotSaveVariant(reinterpret_cast<void*>(kSaveObject), i);
        }
        else
        {
            const int charId = i == kEntryVezon ? kCharVezon : bossId;
            sprite = At<uintptr_t>(kPirakaIcons, charId * 4);
            if (!sprite && !w.iconWarned)
            {
                Log("wheel: no icon sprite for char %d (loaded %d)", charId, LoadedPiraka());
                w.iconWarned = true;
            }
        }

        const float r = e.pos - w.rot;
        float alpha = 1.0f;
        if (r > edge || r <= -0.5f)
            alpha = 2.0f * (r - edge);
        else if (r > n - 0.95f)
            alpha = 1.0f - 2.0f * (r - (n - 1.0f));
        if (r < 0.0f && r >= -1.0f)
            alpha = r >= -0.5f ? 2.0f * r + 1.0f : 2.0f * (-r - 0.5f);
        if (r > n || r < -1.0f)
            alpha = 1.0f;
        if (e.t == 1.0f)
            alpha = 0.0f;

        float wrap = kWheelStep * 0.25f;
        if (r > edge)
            wrap += kWheelStep * n;
        else if (r <= -0.5f)
            wrap -= kWheelStep * n;

        if (i == current)
        {
            if (w.lastCurrent != i)
                pulse = 0.0f;
            const int index = (static_cast<int>(pulse * 65536.0f) >> 1) & 0x7fff;
            e.scale = sinTable[index] * 0.25f + 1.5f;
            w.lastCurrent = i;
            if (r <= -0.5f)
            {
                e.scale = 1.0f;
                pulse = 0.0f;
            }
        }

        float size = e.scale * kWheelIcon * alpha;
        if (e.state == -1)
            size *= 1.0f - e.t;
        if (e.state == 2)
            size *= e.t;
        const float x = kWheelStep * (n - 1.0f) + wrap - kWheelStep * e.pos + kWheelStep * w.rot +
                        (kWheelIcon - size) * 0.5f;
        const float y = (kWheelIcon - size) * 0.5f;
        const uint32_t color = (static_cast<uint32_t>(static_cast<int>(alpha * 128.0f)) << 24) | 0x808080;

        if (sprite)
            if (void* texture = SpriteTexture(sprite))
                DrawSprite(gui, originX + x, originY + y, size, size, 0.0f, 0.0f, 1.0f, 1.0f, 0, 0, 0, color,
                           texture);
        if (upgraded)
        {
            const float k = e.scale * alpha;
            markers[markerCount++] = {originX + x - 5.0f * k, originY + y - 5.0f * k, 20.0f * k, color};
        }
    }

    const uintptr_t marker = At<uintptr_t>(0, kUpgradeSprite);
    if (!marker)
        return;
    void* texture = SpriteTexture(marker);
    const float u0 = At<float>(marker, 0x24), v0 = At<float>(marker, 0x28);
    const float u1 = u0 + At<float>(marker, 0x2c), v1 = v0 + At<float>(marker, 0x30);
    for (int i = 0; i < markerCount; ++i)
        DrawSprite(gui, markers[i].x, markers[i].y, markers[i].size, markers[i].size, u0, v0, u1, v1, 0, 0, 0,
                   markers[i].color, texture);
}

void __cdecl hk_DrawMaskIcons(void* guiElement)
{
    const uintptr_t player = Player();
    int bossId = -1;
    if (!g_cfg.extraCharacter || !guiElement || !At<void*>(0, kGuiPtr) || !player || !ExtraActive(player, bossId))
    {
        g_wheel.active = false;
        return o_DrawMaskIcons(guiElement);
    }
    if (!g_wheel.active)
    {
        WheelReset(player);
        Log("wheel: 8-entry mode on (boss %d, icons boss=%p vezon=%p)", bossId,
            At<void*>(kPirakaIcons, bossId * 4), At<void*>(kPirakaIcons, kCharVezon * 4));
    }
    WheelLayout(player, bossId);
    WheelDraw(player, bossId);
}

// --- separate masks for the boss and Vezon -----------------------------------------------------
//
// A death of the slot-6 character would lock slot 6, and slot 6 never comes back in a level (there
// is no mask pickup for it), so both Piraka were gone. Now the dead one is marked lost and the other
// one is put back into slot 6. Both come back with the rest of the party (BuildParty: a death with
// nobody left, a new level).

PlayerDeathStart_t o_PlayerDeathStart = nullptr;
PlayerDeathUpdate_t o_PlayerDeathUpdate = nullptr;
PlayerRestoreSlot_t o_PlayerRestoreSlot = nullptr;
PlayerBuildParty_t o_PlayerBuildParty = nullptr;
HudUpdate_t o_HudUpdate = nullptr;

bool InParty(uintptr_t player, int slot)
{
    for (int i = 0; i < kSlotCount; ++i)
        if (At<int>(player, kPlayerParty + i * 4) == slot)
            return true;
    return false;
}

// The game's own "is anyone else left" check (0x493910, 0x499150), over the Toa.
bool ToaAvailable(uintptr_t player)
{
    const int current = At<int>(player, kPlayerSlot);
    for (int slot = 0; slot < kPirakaSlot; ++slot)
        if (slot != current && At<float>(player, kPlayerSlotLock + slot * 4) == 0.0f &&
            At<int>(player, kPlayerSlotChar + slot * 4) != kCharNone)
            return true;
    return false;
}

// With no Toa left the game would take the death as the last one (5 s, then the whole party comes
// back). The other Piraka is still there, so for the length of one game call a locked Toa slot that
// is not in the party is shown as available: the game takes its usual path (2 s, slot locked, switch
// to the party's next available character, which with only slot 6 in the party is slot 6 itself).
class OtherPirakaStandIn
{
public:
    explicit OtherPirakaStandIn(uintptr_t player) : player_(player)
    {
        if (At<int>(player, kPlayerSlot) != kPirakaSlot || ToaAvailable(player))
            return;
        for (int slot = 0; slot < kPirakaSlot; ++slot)
            if (At<int>(player, kPlayerSlotChar + slot * 4) != kCharNone && !InParty(player, slot))
            {
                slot_ = slot;
                break;
            }
        if (slot_ < 0)
            return;
        float& lock = At<float>(player, kPlayerSlotLock + slot_ * 4);
        saved_ = lock;
        lock = 0.0f;
    }
    ~OtherPirakaStandIn()
    {
        if (slot_ >= 0)
            At<float>(player_, kPlayerSlotLock + slot_ * 4) = saved_;
    }
    OtherPirakaStandIn(const OtherPirakaStandIn&) = delete;
    OtherPirakaStandIn& operator=(const OtherPirakaStandIn&) = delete;

private:
    uintptr_t player_;
    int slot_ = -1;
    float saved_ = 0.0f;
};

void __fastcall hk_PlayerDeathStart(void* playerPtr)
{
    const auto player = reinterpret_cast<uintptr_t>(playerPtr);
    g_dying = -1;
    int bossId = -1;
    if (!g_cfg.extraCharacter || At<int>(player, kPlayerSlot) != kPirakaSlot || !ExtraActive(player, bossId))
        return o_PlayerDeathStart(playerPtr);
    const int dying = LoadedPiraka();
    const int other = dying == kCharVezon ? bossId : kCharVezon;
    if ((dying != bossId && dying != kCharVezon) || PirakaLost(other))
        return o_PlayerDeathStart(playerPtr);
    g_dying = dying;
    const OtherPirakaStandIn standIn(player);
    o_PlayerDeathStart(playerPtr);
}

// The death is over and the game has locked slot 6: the dead Piraka is lost, the other one takes
// slot 6 and the slot is given back.
void FinishPirakaDeath(uintptr_t player)
{
    const int dead = g_dying;
    g_dying = -1;
    const int other = dead == kCharVezon ? ZoneBossForLevel(-1) : kCharVezon;
    if (!IsZoneBoss(other) && other != kCharVezon)
        return;
    (dead == kCharVezon ? g_lostVezon : g_lostBoss) = true;

    void* playerPtr = reinterpret_cast<void*>(player);
    const bool noToaLeft = At<int>(player, kPlayerSlot) == kPirakaSlot;  // the game stayed in slot 6
    if (noToaLeft)
    {
        o_PlayerRestoreSlot(playerPtr, kPirakaSlot);
        SwapInPlace(player, other);
    }
    else if (LoadSlot6Character(player, other))
        o_PlayerRestoreSlot(playerPtr, kPirakaSlot);

    if (LoadedPiraka() != other)
    {
        // Loading failed: slot 6 keeps the dead Piraka, as in the original game (locked unless it
        // was the last character, in which case it is back as the game's respawn).
        Log("VezonExtra: char %d died, char %d could not take slot 6", dead, other);
        if (noToaLeft)
            (dead == kCharVezon ? g_lostVezon : g_lostBoss) = false;
        return;
    }
    At<uint8_t>(player, kPlayerMaskDropped + kPirakaSlot) = 0;
    // Slot 6's wheel icon went out with the death and RestoreSlot lets it come in again; the one
    // that stays should just stay.
    At<int>(GameWheelSlot(player, kPirakaSlot), 4) = 1;
    At<float>(GameWheelSlot(player, kPirakaSlot), 0xc) = 0.0f;
    Log("VezonExtra: char %d lost its mask, char %d keeps slot 6%s", dead, other, noToaLeft ? " (no Toa left)" : "");
}

void __fastcall hk_PlayerDeathUpdate(void* playerPtr, void*, void* pad)
{
    if (g_dying < 0)
        return o_PlayerDeathUpdate(playerPtr, pad);
    const auto player = reinterpret_cast<uintptr_t>(playerPtr);
    const bool wasDying = At<float>(player, kPlayerDeathTimer) != 0.0f;
    {
        const OtherPirakaStandIn standIn(player);
        o_PlayerDeathUpdate(playerPtr, pad);
    }
    // g_dying is cleared if the game rebuilt the party instead (nobody left after all).
    if (g_dying >= 0 && wasDying && At<float>(player, kPlayerDeathTimer) == 0.0f)
        FinishPirakaDeath(player);
}

// The game gives slot 6 back (the "all masks" cheat): both Piraka are back.
void __fastcall hk_PlayerRestoreSlot(void* player, void*, int slot)
{
    if (slot == kPirakaSlot)
        ResetLostMasks("slot 6 restored");
    o_PlayerRestoreSlot(player, slot);
}

void __fastcall hk_PlayerBuildParty(void* player, void*, int arg)
{
    ResetLostMasks("party rebuilt");
    o_PlayerBuildParty(player, arg);
}

// Near a construction for a Piraka whose mask is lost, the HUD shows it as for any lost mask (no
// "switch" prompt, no hint switch): during the update it sees slot 6 locked.
void __fastcall hk_HudUpdate(void* hud, void*, void* a, void* b)
{
    const uintptr_t player = Player();
    int bossId = -1;
    if (!(g_lostBoss || g_lostVezon) || !player || !ExtraActive(player, bossId))
        return o_HudUpdate(hud, a, b);
    // The piece the update itself looks at (0x466b2b).
    const uintptr_t nearby = At<uintptr_t>(player, kPlayerNearConstruction);
    const uintptr_t piece = nearby ? At<uintptr_t>(nearby, kNearConstructionPiece) : 0;
    if (!piece)
        return o_HudUpdate(hud, a, b);
    const int type = At<int8_t>(piece, kPieceType);
    int wanted = -1;
    if (type == kCharVezon + 2)
        wanted = kCharVezon;
    else if (type == kPirakaSlot || (type >= kCharHakann + 2 && type <= kCharReidak + 2))
        wanted = bossId;
    if (wanted < 0 || !PirakaLost(wanted))
        return o_HudUpdate(hud, a, b);
    float& lock = At<float>(player, kPlayerSlotLock + kPirakaSlot * 4);
    lock = kMaskLostLock;
    o_HudUpdate(hud, a, b);
    lock = 0.0f;
}

} // namespace

bool InstallVezonExtra(const VezonExtraConfig& cfg)
{
    g_cfg = cfg;

    const HookSpec hooks[] = {
        {"GetLevelPirakaCharId", kGetLevelPirakaCharId, reinterpret_cast<void*>(&hk_GetLevelPirakaCharId),
         reinterpret_cast<void**>(&o_GetLevelPirakaCharId)},
        {"PlayerInitSlotChars", kPlayerInitSlotChars, reinterpret_cast<void*>(&hk_PlayerInitSlotChars),
         reinterpret_cast<void**>(&o_PlayerInitSlotChars)},
        {"PlayerSwitchInput", kPlayerSwitchInput, reinterpret_cast<void*>(&hk_PlayerSwitchInput),
         reinterpret_cast<void**>(&o_PlayerSwitchInput)},
        {"ConstructionInteract", kConstructionInteract, reinterpret_cast<void*>(&hk_ConstructionInteract),
         reinterpret_cast<void**>(&o_ConstructionInteract)},
        {"ConstructionBuilding", kConstructionBuilding, reinterpret_cast<void*>(&hk_ConstructionBuilding),
         reinterpret_cast<void**>(&o_ConstructionBuilding)},
        {"LevelLoadConfig", kLevelLoadConfig, reinterpret_cast<void*>(&hk_LevelLoadConfig),
         reinterpret_cast<void**>(&o_LevelLoadConfig)},
        {"DrawMaskIcons", kDrawMaskIcons, reinterpret_cast<void*>(&hk_DrawMaskIcons),
         reinterpret_cast<void**>(&o_DrawMaskIcons)},
        {"PlayerDeathStart", kPlayerDeathStart, reinterpret_cast<void*>(&hk_PlayerDeathStart),
         reinterpret_cast<void**>(&o_PlayerDeathStart)},
        {"PlayerDeathUpdate", kPlayerDeathUpdate, reinterpret_cast<void*>(&hk_PlayerDeathUpdate),
         reinterpret_cast<void**>(&o_PlayerDeathUpdate)},
        {"PlayerRestoreSlot", kPlayerRestoreSlot, reinterpret_cast<void*>(&hk_PlayerRestoreSlot),
         reinterpret_cast<void**>(&o_PlayerRestoreSlot)},
        {"PlayerBuildParty", kPlayerBuildParty, reinterpret_cast<void*>(&hk_PlayerBuildParty),
         reinterpret_cast<void**>(&o_PlayerBuildParty)},
        {"HudUpdate", kHudUpdate, reinterpret_cast<void*>(&hk_HudUpdate), reinterpret_cast<void**>(&o_HudUpdate)},
    };

    // Everything is checked before anything is patched.
    if (!AddressesFound("VezonExtra",
                        {&addr::GetLevelPirakaCharId, &addr::ZoneToPirakaCharId, &addr::LoadPirakaModel, &addr::VisualReset, &addr::VisualSetup,
                         &addr::PlayerSetSlot, &addr::PartyNext, &addr::PartyPrev, &addr::PlayerSwitchInput, &addr::PlayerInitSlotChars,
                         &addr::ConstructionInteract, &addr::ConstructionBuilding, &addr::SaveIsPirakaUnlocked, &addr::SaveIsPirakaUnlocked2,
                         &addr::SwitchFx, &addr::PlaySound, &addr::IsSwitchBlocked, &addr::LoadCfgVezonCheck1, &addr::LoadCfgVezonCheck2,
                         &addr::LevelLoadConfig, &addr::DrawMaskIcons, &addr::DrawSprite, &addr::SlotSaveVariant, &addr::HintCheckLowTypes,
                         &addr::HintCheckBossTypes, &addr::ReleaseScene, &addr::FreeSpine, &addr::FreeCharacterModel, &addr::FreeFace, &addr::PlayerPtr,
                         &addr::CharMgrPtr, &addr::LevelsPtr, &addr::SaveObject, &addr::CharDefTable, &addr::GuiPtr, &addr::FrameDt, &addr::SmoothRate,
                         &addr::WheelPulse, &addr::WheelX, &addr::WheelY, &addr::WheelCountCopy, &addr::WheelCount, &addr::SinTable, &addr::ToaIcons,
                         &addr::PirakaIcons, &addr::UpgradeSprite, &addr::HudPtr, &addr::MaterialTable, &addr::MaterialTableEnd,
                         &addr::PlayerDeathStart, &addr::PlayerDeathUpdate, &addr::PlayerRestoreSlot, &addr::PlayerBuildParty,
                         &addr::HudUpdate}))
        return false;

    if (!HooksInit())
        return false;

    bool ok = true;
    for (const auto& h : hooks)
        ok = InstallHook(h) && ok;
    ok = PatchCallRel32(kLoadCfgVezonCheck1, reinterpret_cast<void*>(&hk_LoadCfgVezonCheck1)) && ok;
    ok = WriteCallAndNops(kHintCheckLowTypes, kHintCheckLowTypesLen, reinterpret_cast<void*>(&HintCheckLowTypes)) && ok;
    ok = WriteCallAndNops(kHintCheckBossTypes, kHintCheckBossTypesLen, reinterpret_cast<void*>(&HintCheckBossTypes)) && ok;
    ok = PatchCallRel32(kLoadCfgVezonCheck2, reinterpret_cast<void*>(&hk_LoadCfgVezonCheck2)) && ok;
    Log("VezonExtra: %s (extraCharacter=%d restrictVezon=%d instantSwap=%d)", ok ? "installed" : "PARTIALLY installed",
        g_cfg.extraCharacter, g_cfg.restrictVezon, g_cfg.instantSwap);
    return ok;
}
