#pragma once

// Bionicle.exe (SHA-256 E1DAA3DC...C4A8, image base 0x400000, no ASLR).
// Every address below was found in Ghidra; the ones marked [rt] were also confirmed at runtime.

#include <cstddef>
#include <cstdint>

namespace game
{
// Character ids (table at 0x5f97a0).
constexpr int kCharHakann = 6;
constexpr int kCharReidak = 11;
constexpr int kCharVezon = 12;
constexpr int kCharNone = 13;
constexpr int kPirakaSlot = 6;
constexpr int kSlotCount = 7;

// Globals.
constexpr uintptr_t kPlayerPtr = 0x652cbc;       // Player*                                   [rt]
constexpr uintptr_t kCharMgrPtr = 0x64ce84;      // character manager*                        [rt]
constexpr uintptr_t kLevelsPtr = 0x64e0f0;       // +0xc current level, +0xf38 level->zone    [rt]
constexpr uintptr_t kSaveObject = 0x652d38;      // save/progress object (thiscall `this`)
constexpr uintptr_t kCharDefTable = 0x63a7c0;    // per-char definition, stride 0x690
constexpr uintptr_t kCharDefStride = 0x690;
constexpr uintptr_t kCharDefVisual = 0x2b0;      // visual descriptor inside a char definition

// Player fields.
constexpr uintptr_t kPlayerSlot = 0x12a4;        // int current slot                          [rt]
constexpr uintptr_t kPlayerSlotChar = 0x12ac;    // int[7] slot -> char id                    [rt]
constexpr uintptr_t kPlayerSlotLock = 0x12cc;    // float[7], 0 = available                    [rt]
constexpr uintptr_t kPlayerSwitchCooldown = 0x1404;
constexpr uintptr_t kPlayerFlags690 = 0x690;
constexpr uintptr_t kPlayerSlotVisual = 0x17f0;  // void*[7]                                  [rt]
constexpr uintptr_t kPlayerSwitchSound = 0x1e90;
constexpr uintptr_t kPlayerParty = 0x1eec;       // int[7] rotating ring, [0] = current       [rt]

// Character manager fields.
constexpr uintptr_t kMgrToy = 0x10;              // `this` for LoadPirakaModel                [rt]
constexpr uintptr_t kMgrLoadedPiraka = 0x20;     // char id in the Piraka buffer              [rt]
constexpr uintptr_t kMgrToyBusy = 0x30;          // nonzero while a load is in progress       [rt]
constexpr uintptr_t kMgrSwitchState = 0x3068;    // switching disabled while in [0x16, 0x19]

// Construction piece fields.
constexpr uintptr_t kPieceType = 0x1d5;          // int8: 0-5 Toa, 6 Piraka, 7 gold, 8-13 boss (charId+2), 14 Vezon [rt]

// HUD object (also drives the "switch to the needed mask" prompt).
constexpr uintptr_t kHudPtr = 0x6509b8;
constexpr uintptr_t kHudHintSlot = 0xb0;         // int16 slot the hint asks for (types > 6 clamp to 6), -1 none
constexpr uintptr_t kHudHintType = 0xb4;         // int16 raw type of the construction nearby
constexpr uintptr_t kPlayerHintCooldown = 0x140c;  // float, nonzero right after a hint switch

// HUD update 0x466af0: "can the current character use this?" checks, patched to call our code.
constexpr uintptr_t kHintCheckLowTypes = 0x466ca5;
constexpr size_t kHintCheckLowTypesLen = 13;
constexpr uintptr_t kHintCheckBossTypes = 0x466d70;
constexpr size_t kHintCheckBossTypesLen = 14;

// Pad fields.
constexpr uintptr_t kPadPressed = 0x3c;          // bit0/bit1 = next character, bit2 = previous

// Functions.
using GetLevelPirakaCharId_t = int(__cdecl*)(int level);
using ZoneToPirakaCharId_t = int(__cdecl*)(int zone);
using LoadPirakaModel_t = int(__thiscall*)(void* toy, int charId);
using VisualReset_t = void(__thiscall*)(void* visual);
using VisualSetup_t = void(__thiscall*)(void* visual, const void* descriptor);
using PlayerSetSlot_t = void(__thiscall*)(void* player, int slot);
using PlayerPartyStep_t = int(__thiscall*)(void* player);
using PlayerSwitchInput_t = void(__thiscall*)(void* player, void* pad);
using PlayerInitSlotChars_t = void(__thiscall*)(void* player);
using ConstructionUpdate_t = void(__thiscall*)(void* self, void* group, uint8_t* piece, void* pad);
using SaveUnlockCheck_t = int(__thiscall*)(void* save, int pirakaIndex);
using SwitchFx_t = void(__thiscall*)(void* visual);
using PlaySound_t = void(__cdecl*)(void* sound, int flags);
using IsSwitchBlocked_t = int(__cdecl*)();

constexpr uintptr_t kGetLevelPirakaCharId = 0x425340;
constexpr uintptr_t kZoneToPirakaCharId = 0x4252e0;
constexpr uintptr_t kLoadPirakaModel = 0x4291d0;      // [rt]
constexpr uintptr_t kVisualReset = 0x453f40;          // [rt]
constexpr uintptr_t kVisualSetup = 0x452cb0;          // [rt]
constexpr uintptr_t kPlayerSetSlot = 0x494de0;
constexpr uintptr_t kPartyNext = 0x48ea20;
constexpr uintptr_t kPartyPrev = 0x48ea70;
constexpr uintptr_t kPlayerSwitchInput = 0x496220;    // [rt] runs every gameplay frame
constexpr uintptr_t kPlayerInitSlotChars = 0x48e9b0;
constexpr uintptr_t kConstructionInteract = 0x47ccc0;
constexpr uintptr_t kConstructionBuilding = 0x47a9c0;
constexpr uintptr_t kSaveIsPirakaUnlocked = 0x49a430;
constexpr uintptr_t kSaveIsPirakaUnlocked2 = 0x49a470;
constexpr uintptr_t kSwitchFx = 0x4538f0;
constexpr uintptr_t kPlaySound = 0x4c3dc0;
constexpr uintptr_t kIsSwitchBlocked = 0x4ce8c0;

constexpr uintptr_t kLevelLoadConfig = 0x46d140;      // int __cdecl(int level, void* arena, void* extra)
using LevelLoadConfig_t = int(__cdecl*)(int level, void* arena, void* extra);

// Level_LoadConfig (0x46d140): "Vezon unlocked -> load Vezon" checks, patched at the call sites.
constexpr uintptr_t kLoadCfgVezonCheck1 = 0x46d1fb;   // call Save_IsPirakaUnlocked2(6)
constexpr uintptr_t kLoadCfgVezonCheck2 = 0x46d20f;   // call Save_IsPirakaUnlocked(6)

// Character wheel ("DrawMaskIcons" GUI callback, 0x457550). Layout/animation per slot lives in the
// player (+0x1f08 + slot*0x18); our 8-entry wheel mirrors its rules (decoded from 0x493080/0x457550).
constexpr uintptr_t kDrawMaskIcons = 0x457550;        // void __cdecl(void* guiElement)
constexpr uintptr_t kDrawSprite = 0x4ba3a0;           // thiscall, 13 args (ret 0x34)
constexpr uintptr_t kSlotSaveVariant = 0x49a900;      // int __thiscall(save, slot): Toa upgrade marker
constexpr uintptr_t kGuiPtr = 0x6a7548;
constexpr uintptr_t kFrameDt = 0x69aac0;              // float seconds
constexpr uintptr_t kSmoothRate = 0x69aa88;           // float
constexpr uintptr_t kWheelPulse = 0x6506b0;           // float 0..1
constexpr uintptr_t kWheelX = 0x6506b8;               // float screen origin
constexpr uintptr_t kWheelY = 0x6506bc;
constexpr uintptr_t kWheelCountCopy = 0x6506c0;
constexpr uintptr_t kWheelCount = 0x6506c4;           // also read by the wheel frame (0x457ad0)
constexpr uintptr_t kSinTable = 0x360a300;            // float[0x8000]
constexpr uintptr_t kToaIcons = 0x64eed0;             // sprite*[6] by slot
constexpr uintptr_t kPirakaIcons = 0x64eed8;          // sprite*[] by char id (6..12 used)
constexpr uintptr_t kUpgradeSprite = 0x64f388;        // sprite*
constexpr uintptr_t kPlayerWheel = 0x1f08;            // per slot 0x18: +4 state, +8 pos, +0xc t, +0x14 scale
constexpr uintptr_t kPlayerWheelStride = 0x18;
constexpr float kWheelStep = 35.0f;                   // 0x603d34
constexpr float kWheelIcon = 32.0f;                   // 0x5c86ac

using DrawMaskIcons_t = void(__cdecl*)(void* guiElement);
using DrawSprite_t = void(__thiscall*)(void* gui, float x, float y, float w, float h, float u0, float v0,
                                       float u1, float v1, int a9, int a10, int a11, uint32_t color,
                                       void* texture);
using SlotSaveVariant_t = int(__thiscall*)(void* save, int slot);

template <typename T>
inline T& At(uintptr_t base, uintptr_t offset)
{
    return *reinterpret_cast<T*>(base + offset);
}

inline uintptr_t Player() { return *reinterpret_cast<uintptr_t*>(kPlayerPtr); }
inline uintptr_t CharMgr() { return *reinterpret_cast<uintptr_t*>(kCharMgrPtr); }
inline const void* CharVisualDescriptor(int charId)
{
    return reinterpret_cast<const void*>(kCharDefTable + charId * kCharDefStride + kCharDefVisual);
}
} // namespace game
